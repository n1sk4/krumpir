/* fmu_open failure paths, using small zip archives built on the fly. The success path is covered
 * by test_loader_fmus with the real Reference FMUs. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "loader.h"
#include "miniz.h"
#include "platform.h"

#define MD_PREFIX "<fmiModelDescription fmiVersion=\"3.0\">"
#define MD_SUFFIX "</fmiModelDescription>"
#define MD_COSIM(id) MD_PREFIX "<CoSimulation modelIdentifier=\"" id "\"/>" MD_SUFFIX

typedef struct {
  const char *name;
  const char *data;
} entry_t;

static char base[256]; /* private TMPDIR for fmu_open */
static char zip_path[300];

static void make_zip(const entry_t *entries, size_t n) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  assert(mz_zip_writer_init_file(&z, zip_path, 0));
  for (size_t i = 0; i < n; i++) {
    assert(mz_zip_writer_add_mem(&z, entries[i].name, entries[i].data, strlen(entries[i].data),
                                 MZ_DEFAULT_COMPRESSION));
  }
  assert(mz_zip_writer_finalize_archive(&z));
  assert(mz_zip_writer_end(&z));
}

static int count_entries(const char *dir) {
  DIR *d = opendir(dir);
  assert(d != NULL);
  int n = 0;
  for (struct dirent *e; (e = readdir(d)) != NULL;) {
    if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
      n++;
    }
  }
  closedir(d);
  return n;
}

/* fmu_open must fail with a message containing `substr` and leave nothing behind. */
static void expect_fail(const char *path, const char *substr) {
  fmu_pkg_t pkg;
  char err[512] = "";
  int rc = fmu_open(path, &pkg, err, sizeof err);
  if (rc == 0 || strstr(err, substr) == NULL) {
    fprintf(stderr, "expected error containing '%s', got rc=%d err='%s'\n", substr, rc, err);
  }
  assert(rc == -1);
  assert(strstr(err, substr) != NULL);
  assert(pkg.lib == NULL && pkg.md.vars == NULL);
  assert(count_entries(base) == 0); /* temp dir removed, nothing escaped into base */
}

static void expect_fail_zip(const entry_t *entries, size_t n, const char *substr) {
  make_zip(entries, n);
  expect_fail(zip_path, substr);
}

int main(void) {
  snprintf(base, sizeof base, "/tmp/krumpir_test_loader_XXXXXX");
  assert(mkdtemp(base) != NULL);
  assert(setenv("TMPDIR", base, 1) == 0);
  snprintf(zip_path, sizeof zip_path, "%s.zip", base);

  /* Not a zip / not there. */
  expect_fail("/nonexistent/x.fmu", "not a readable zip");
  FILE *fp = fopen(zip_path, "w");
  assert(fp != NULL);
  fputs("this is not a zip file", fp);
  fclose(fp);
  expect_fail(zip_path, "not a readable zip");

  /* Missing or wrong content. */
  expect_fail_zip((entry_t[]){{"other.txt", "x"}}, 1, "no readable modelDescription.xml");
  expect_fail_zip((entry_t[]){{"modelDescription.xml", "<fmiModelDescription fmiVersion=\"2.0\"/>"}},
                  1, "unsupported fmiVersion");
  expect_fail_zip(
      (entry_t[]){{"modelDescription.xml", MD_PREFIX "<ModelExchange modelIdentifier=\"m\"/>" MD_SUFFIX}},
      1, "no <CoSimulation>");
  expect_fail_zip((entry_t[]){{"modelDescription.xml", MD_COSIM("m")}}, 1,
                  "no binary for " KRUMPIR_FMI_PLATFORM);
  /* A binary for another platform does not count. */
  expect_fail_zip((entry_t[]){{"modelDescription.xml", MD_COSIM("m")},
                              {"binaries/other-platform/m.so", "x"}},
                  2, "no binary for " KRUMPIR_FMI_PLATFORM);
  /* Binary present but not a loadable library. */
  expect_fail_zip((entry_t[]){{"modelDescription.xml", MD_COSIM("m")},
                              {"binaries/" KRUMPIR_FMI_PLATFORM "/m.so", "not an ELF file"}},
                  2, "cannot load");
  /* modelIdentifier must not be a path. */
  expect_fail_zip((entry_t[]){{"modelDescription.xml", MD_COSIM("../m")}}, 1,
                  "unsupported characters");

  /* Path traversal: entries that would land outside the temp directory are refused. */
  expect_fail_zip((entry_t[]){{"modelDescription.xml", MD_COSIM("m")},
                              {"binaries/" KRUMPIR_FMI_PLATFORM "/../../../evil.txt", "x"}},
                  2, "unsafe path in archive");
  expect_fail_zip((entry_t[]){{"resources/../../evil.txt", "x"}}, 1, "unsafe path in archive");
  expect_fail_zip((entry_t[]){{"resources//abs/../../../evil.txt", "x"}}, 1,
                  "unsafe path in archive");
  assert(access("/tmp/evil.txt", F_OK) != 0);

  remove(zip_path);
  assert(rmdir(base) == 0);
  puts("test_loader: ok");
  return 0;
}
