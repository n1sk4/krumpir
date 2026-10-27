#include "loader.h"

#include <dlfcn.h>
#include <errno.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "miniz.h"
#include "platform.h"

#define MAX_ENTRIES 100000
#define MAX_TOTAL_BYTES (512ull << 20)
#define MAX_XML_BYTES (16u << 20)

static int rm_cb(const char *p, const struct stat *sb, int type, struct FTW *f)
{
  (void)sb, (void)type, (void)f;
  return remove(p);
}

static void rmtree(const char *dir) { nftw(dir, rm_cb, 16, FTW_DEPTH | FTW_PHYS); }

/* mkdir -p for `path`. */
static int mkdir_p(const char *path)
{
  char tmp[1024];
  size_t n = strlen(path);
  if (n == 0 || n >= sizeof tmp) {
    return -1;
  }
  memcpy(tmp, path, n + 1);
  for (char *p = tmp + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
  }
  return mkdir(tmp, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

/* Archive member names must be relative and must not climb out of the directory. */
static bool safe_name(const char *name)
{
  if (name[0] == '/' || strchr(name, '\\') != NULL) {
    return false;
  }
  for (const char *p = name; *p != '\0';) {
    const char *slash = strchr(p, '/');
    size_t len = slash != NULL ? (size_t)(slash - p) : strlen(p);
    if (len == 2 && p[0] == '.' && p[1] == '.') {
      return false;
    }
    p += len + (slash != NULL ? 1 : 0);
  }
  return true;
}

static bool wanted(const char *name)
{
  static const char bin_prefix[] = "binaries/" KRUMPIR_FMI_PLATFORM "/";
  return strcmp(name, "modelDescription.xml") == 0 ||
         strncmp(name, bin_prefix, sizeof bin_prefix - 1) == 0 ||
         strncmp(name, "resources/", 10) == 0;
}

static int extract(mz_zip_archive *zip, const char *dir, char *err, size_t errlen)
{
  mz_uint count = mz_zip_reader_get_num_files(zip);
  if (count > MAX_ENTRIES) {
    snprintf(err, errlen, "archive has more than %d entries", MAX_ENTRIES);
    return -1;
  }
  unsigned long long total = 0;
  for (mz_uint i = 0; i < count; i++) {
    char name[512];
    if (mz_zip_reader_get_filename(zip, i, name, sizeof name) == 0 ||
        strlen(name) >= sizeof name - 1) {
      snprintf(err, errlen, "archive entry %u has an unreadable or too long name", (unsigned)i);
      return -1;
    }
    if (!wanted(name)) {
      continue;
    }
    if (!safe_name(name)) {
      snprintf(err, errlen, "unsafe path in archive: %s", name);
      return -1;
    }
    char dest[1100];
    snprintf(dest, sizeof dest, "%s/%s", dir, name);
    if (mz_zip_reader_is_file_a_directory(zip, i)) {
      if (mkdir_p(dest) != 0) {
        snprintf(err, errlen, "cannot create %s", dest);
        return -1;
      }
      continue;
    }
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(zip, i, &st)) {
      snprintf(err, errlen, "cannot read archive entry %s", name);
      return -1;
    }
    total += st.m_uncomp_size;
    if (total > MAX_TOTAL_BYTES) {
      snprintf(err, errlen, "archive expands to more than %llu MiB", MAX_TOTAL_BYTES >> 20);
      return -1;
    }
    char *slash = strrchr(dest, '/');
    *slash = '\0';
    int rc = mkdir_p(dest);
    *slash = '/';
    if (rc != 0 || !mz_zip_reader_extract_to_file(zip, i, dest, 0)) {
      snprintf(err, errlen, "cannot extract %s", name);
      return -1;
    }
  }
  return 0;
}

static int read_file(const char *path, char **out, size_t *len)
{
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return -1;
  }
  char *buf = malloc(MAX_XML_BYTES + 1);
  if (buf == NULL) {
    fclose(fp);
    return -1;
  }
  *len = fread(buf, 1, MAX_XML_BYTES + 1, fp);
  fclose(fp);
  if (*len > MAX_XML_BYTES) {
    free(buf);
    return -1;
  }
  *out = buf;
  return 0;
}

static bool safe_identifier(const char *s)
{
  for (; *s != '\0'; s++) {
    bool ok = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '_' || *s == '-' || *s == '.';
    if (!ok) {
      return false;
    }
  }
  return true;
}

int fmu_open(const char *path, fmu_pkg_t *pkg, char *err, size_t errlen)
{
  *pkg = (fmu_pkg_t){0};

  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!mz_zip_reader_init_file(&zip, path, 0)) {
    snprintf(err, errlen, "cannot open '%s' as an FMU (not a readable zip archive)", path);
    return -1;
  }

  const char *tmp = getenv("TMPDIR");
  int n = snprintf(pkg->dir, sizeof pkg->dir, "%s/krumpir_fmu_XXXXXX",
                   tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
  if (n < 0 || (size_t)n >= sizeof pkg->dir - 64 || mkdtemp(pkg->dir) == NULL) {
    snprintf(err, errlen, "cannot create a temporary directory");
    mz_zip_reader_end(&zip);
    pkg->dir[0] = '\0';
    return -1;
  }

  char *xml = NULL;
  size_t xml_len = 0;
  char file[1100];
  int rc = -1;

  if (extract(&zip, pkg->dir, err, errlen) < 0) {
    goto done;
  }
  snprintf(file, sizeof file, "%s/modelDescription.xml", pkg->dir);
  if (read_file(file, &xml, &xml_len) < 0) {
    snprintf(err, errlen, "'%s' has no readable modelDescription.xml", path);
    goto done;
  }
  if (modeldesc_parse(xml, xml_len, &pkg->md, err, errlen) < 0) {
    goto done;
  }
  if (!safe_identifier(pkg->md.model_identifier)) {
    snprintf(err, errlen, "modelIdentifier '%s' has unsupported characters",
             pkg->md.model_identifier);
    goto done;
  }
  snprintf(file, sizeof file, "%s/binaries/%s/%s.so", pkg->dir, KRUMPIR_FMI_PLATFORM,
           pkg->md.model_identifier);
  if (access(file, R_OK) != 0) {
    snprintf(err, errlen, "FMU has no binary for %s (%s)", KRUMPIR_FMI_PLATFORM,
             pkg->md.model_identifier);
    goto done;
  }
  pkg->lib = dlopen(file, RTLD_NOW | RTLD_LOCAL);
  if (pkg->lib == NULL) {
    const char *why = dlerror();
    snprintf(err, errlen, "cannot load %s: %s", file, why != NULL ? why : "unknown error");
    goto done;
  }
  rc = 0;

done:
  free(xml);
  mz_zip_reader_end(&zip);
  if (rc != 0) {
    fmu_close(pkg);
  }
  return rc;
}

void fmu_close(fmu_pkg_t *pkg)
{
  if (pkg->lib != NULL) {
    dlclose(pkg->lib);
  }
  modeldesc_free(&pkg->md);
  if (pkg->dir[0] != '\0') {
    rmtree(pkg->dir);
  }
  *pkg = (fmu_pkg_t){0};
}

void *fmu_sym(const fmu_pkg_t *pkg, const char *name)
{
  return pkg->lib != NULL ? dlsym(pkg->lib, name) : NULL;
}
