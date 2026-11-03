#include "serve.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "loader.h"
#include "scaffold.h"
#include "session.h"

#define PROTOCOL_VERSION 1
#define MAX_LINE (8u << 20) /* bytes per request */

static volatile sig_atomic_t g_quit;
static _Atomic(atomic_int *) g_run_stop;

static void on_signal(int sig)
{
  (void)sig;
  g_quit = 1;
  atomic_int *stop = g_run_stop;
  if (stop != NULL) {
    *stop = 1; /* also ends a run, so a blocking wait returns */
  }
}

/* Growable string. */
typedef struct {
  char *p;
  size_t len, cap;
  bool oom;
} sb_t;

static void sb_add(sb_t *b, const char *fmt, ...)
{
  for (;;) {
    size_t room = b->cap - b->len;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->p != NULL ? b->p + b->len : NULL, room, fmt, ap);
    va_end(ap);
    if (n < 0 || b->oom) {
      b->oom = true;
      return;
    }
    if ((size_t)n < room) {
      b->len += (size_t)n;
      return;
    }
    size_t cap = b->cap > 0 ? b->cap : 256;
    while (cap - b->len <= (size_t)n) {
      cap *= 2;
    }
    char *p = realloc(b->p, cap);
    if (p == NULL) {
      b->oom = true;
      return;
    }
    b->p = p;
    b->cap = cap;
  }
}

static void sb_number(sb_t *b, double v)
{
  if (isfinite(v)) {
    sb_add(b, "%.17g", v);
  } else {
    sb_add(b, "null");
  }
}

static void toml_string(sb_t *b, const char *s)
{
  sb_add(b, "\"");
  for (; *s != '\0'; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '"' || ch == '\\') {
      sb_add(b, "\\%c", ch);
    } else if (ch < 0x20 || ch == 0x7f) {
      sb_add(b, "\\u%04X", ch);
    } else {
      sb_add(b, "%c", ch);
    }
  }
  sb_add(b, "\"");
}

/* A JSON value written as TOML (an inline table for an object), so the scenario parser checks
 * a stimulus sent as JSON exactly as it checks one in a scenario file. */
static int to_toml(sb_t *b, const cJSON *j, char *err, size_t errlen)
{
  if (cJSON_IsNumber(j)) {
    sb_number(b, j->valuedouble);
  } else if (cJSON_IsBool(j)) {
    sb_add(b, cJSON_IsTrue(j) ? "true" : "false");
  } else if (cJSON_IsString(j)) {
    toml_string(b, j->valuestring);
  } else if (cJSON_IsArray(j) || cJSON_IsObject(j)) {
    bool obj = cJSON_IsObject(j);
    sb_add(b, obj ? "{ " : "[");
    const cJSON *e;
    cJSON_ArrayForEach(e, j)
    {
      if (obj) {
        toml_string(b, e->string);
        sb_add(b, " = ");
      }
      if (to_toml(b, e, err, errlen) < 0) {
        return -1;
      }
      sb_add(b, e->next != NULL ? ", " : "");
    }
    sb_add(b, obj ? " }" : "]");
  } else {
    snprintf(err, errlen, "null is not a value");
    return -1;
  }
  return 0;
}

static const cJSON *item(const cJSON *req, const char *key)
{
  return cJSON_GetObjectItemCaseSensitive(req, key);
}

static const char *get_str(const cJSON *req, const char *key)
{
  const cJSON *v = item(req, key);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}

/* 1 present, 0 absent, -1 not a number */
static int get_num(const cJSON *req, const char *key, double *out, char *err, size_t errlen)
{
  const cJSON *v = item(req, key);
  if (v == NULL) {
    return 0;
  }
  if (!cJSON_IsNumber(v)) {
    snprintf(err, errlen, "\"%s\" must be a number", key);
    return -1;
  }
  *out = v->valuedouble;
  return 1;
}

static int get_id(const cJSON *req, int *id, char *err, size_t errlen)
{
  double v = -1;
  if (get_num(req, "id", &v, err, errlen) <= 0 || v != floor(v) || fabs(v) > 1e6) {
    snprintf(err, errlen, "\"id\" must be a capture id");
    return -1;
  }
  *id = (int)v;
  return 0;
}

static void add_state(session_t *s, cJSON *r)
{
  cJSON_AddStringToObject(r, "state", lifecycle_name(session_state(s)));
  cJSON_AddStringToObject(r, "fault", session_fault(s));
}

/* add_state plus the loaded run's progress, for a client that watches it. */
static void add_status(session_t *s, cJSON *r)
{
  add_state(s, r);
  const scenario_t *sc = session_scenario(s);
  if (sc == NULL) {
    return;
  }
  session_summary_t m;
  session_live(s, &m);
  cJSON_AddStringToObject(r, "fmu", sc->fmu);
  cJSON_AddNumberToObject(r, "step", sc->step);
  if (isfinite(sc->duration)) {
    cJSON_AddNumberToObject(r, "duration", sc->duration);
  }
  cJSON_AddNumberToObject(r, "t", (double)m.steps * sc->step);
  cJSON_AddNumberToObject(r, "steps", (double)m.steps);
  cJSON_AddNumberToObject(r, "overruns", (double)m.overruns);
  cJSON_AddNumberToObject(r, "max_wake_ns", (double)m.max_wake_ns);
  cJSON_AddNumberToObject(r, "max_step_ns", (double)m.max_step_ns);
  cJSON_AddNumberToObject(r, "late", (double)m.late);
}

static int by_name(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Adds key: the sorted paths "dir/name" of the files in dir ending in ext (none if dir is
 * missing). */
static void add_listing(cJSON *r, const char *key, const char *dir, const char *ext)
{
  cJSON *arr = cJSON_AddArrayToObject(r, key);
  DIR *d = opendir(dir);
  if (d == NULL) {
    return;
  }
  char *names[256]; /* Limitation: first 256 files per directory; a bench has a handful */
  size_t n = 0, el = strlen(ext);
  struct dirent *e;
  while ((e = readdir(d)) != NULL && n < 256) {
    size_t len = strlen(e->d_name);
    if (len > el && strcmp(e->d_name + len - el, ext) == 0) {
      names[n] = malloc(strlen(dir) + len + 2);
      if (names[n] != NULL) {
        sprintf(names[n++], "%s/%s", dir, e->d_name);
      }
    }
  }
  closedir(d);
  qsort(names, n, sizeof names[0], by_name);
  for (size_t i = 0; i < n; i++) {
    cJSON_AddItemToArray(arr, cJSON_CreateString(names[i]));
    free(names[i]);
  }
}

/* The scenarios and FMUs a client can load, relative to the directory the service runs in. */
static void op_files(cJSON *r)
{
  add_listing(r, "scenarios", "scenarios", ".toml");
  add_listing(r, "fmus", "fmus", ".fmu");
}

/* The text of a .toml file on the server, in a buffer valid until the next call; NULL with a
 * message in err. */
static const char *read_toml(const char *op, const char *path, char *err, size_t errlen)
{
  size_t len = path != NULL ? strlen(path) : 0;
  if (len < 5 || strcmp(path + len - 5, ".toml") != 0) {
    snprintf(err, errlen, "%s takes \"path\" of a .toml file", op);
    return NULL;
  }
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    snprintf(err, errlen, "cannot open %s: %s", path, strerror(errno));
    return NULL;
  }
  static char buf[1u << 16]; /* Limitation: one request at a time; scenarios are a few kB */
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  bool more = fgetc(f) != EOF;
  fclose(f);
  if (more) {
    snprintf(err, errlen, "%s is larger than %u bytes", path, (unsigned)sizeof buf - 1);
    return NULL;
  }
  buf[n] = '\0';
  return buf;
}

/* The text of a scenario file on the server (*.toml only), for a client to show before loading. */
static int op_text(const cJSON *req, cJSON *r, char *err, size_t errlen)
{
  const char *text = read_toml("text", get_str(req, "path"), err, errlen);
  if (text == NULL) {
    return -1;
  }
  cJSON_AddStringToObject(r, "text", text);
  return 0;
}

/* The starter scenario of an FMU (krumpir init) as malloc'd text; NULL with a message in err. */
static char *starter(const char *fmu, char *err, size_t errlen)
{
  char *text = NULL;
  size_t len = 0;
  FILE *mem = open_memstream(&text, &len);
  if (mem == NULL) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  int rc = scaffold_scenario(mem, fmu, err, errlen);
  fclose(mem);
  if (rc < 0) {
    free(text);
    return NULL;
  }
  return text;
}

static void add_variables(cJSON *r, const modeldesc_t *md)
{
  cJSON *arr = cJSON_AddArrayToObject(r, "variables");
  for (size_t i = 0; i < md->n_vars; i++) {
    const mvar_t *v = &md->vars[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", v->name);
    cJSON_AddNumberToObject(o, "vr", v->vr);
    cJSON_AddStringToObject(o, "type", v->type);
    cJSON_AddStringToObject(o, "causality", v->causality_str);
    cJSON_AddBoolToObject(o, "tunable", v->tunable);
    cJSON_AddStringToObject(o, "unit", v->unit);
    cJSON_AddStringToObject(o, "start", v->start);
    cJSON_AddStringToObject(o, "description", v->description);
    cJSON_AddItemToArray(arr, o);
  }
}

static cJSON *number_or_null(double v)
{
  return isfinite(v) ? cJSON_CreateNumber(v) : cJSON_CreateNull();
}

/* One io.stimulus entry with the keys the scenario file and the signal op use. */
static cJSON *stimulus_json(const stimulus_t *st)
{
  static const char *const seg_kinds[] = {"const", "ramp", "sine", "pulse", "idle", "data"};
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "input", st->input);
  switch (st->kind) {
  case STIM_CONSTANT:
    cJSON_AddStringToObject(o, "kind", "constant");
    cJSON_AddNumberToObject(o, "value", st->value);
    break;
  case STIM_STEP:
    cJSON_AddStringToObject(o, "kind", "step");
    cJSON_AddNumberToObject(o, "t", st->t);
    cJSON_AddNumberToObject(o, "from", st->from);
    cJSON_AddNumberToObject(o, "to", st->to);
    break;
  case STIM_SINE:
    cJSON_AddStringToObject(o, "kind", "sine");
    cJSON_AddNumberToObject(o, "offset", st->offset);
    cJSON_AddNumberToObject(o, "amplitude", st->amplitude);
    cJSON_AddNumberToObject(o, "freq_hz", st->freq_hz);
    break;
  case STIM_SEGMENTS: {
    cJSON *arr = cJSON_AddArrayToObject(o, "segments");
    for (size_t i = 0; i < st->n_segs; i++) {
      const segment_t *g = &st->segs[i];
      cJSON *e = cJSON_CreateObject();
      cJSON_AddStringToObject(e, "kind", seg_kinds[g->kind]);
      if (isfinite(g->duration)) { /* absent: to the end, or the data file's length */
        cJSON_AddNumberToObject(e, "duration", g->duration);
      }
      if (g->kind == SEG_CONST) {
        cJSON_AddNumberToObject(e, "value", g->value);
      } else if (g->kind == SEG_RAMP) {
        if (g->has_from) {
          cJSON_AddNumberToObject(e, "from", g->from);
        }
        cJSON_AddNumberToObject(e, "to", g->to);
      } else if (g->kind == SEG_SINE) {
        cJSON_AddNumberToObject(e, "offset", g->offset);
        cJSON_AddNumberToObject(e, "amplitude", g->amplitude);
        cJSON_AddNumberToObject(e, "freq_hz", g->freq_hz);
        cJSON_AddNumberToObject(e, "phase", g->phase);
      } else if (g->kind == SEG_PULSE) {
        cJSON_AddNumberToObject(e, "low", g->low);
        cJSON_AddNumberToObject(e, "high", g->high);
        cJSON_AddNumberToObject(e, "period", g->period);
        cJSON_AddNumberToObject(e, "duty", g->duty);
      } else if (g->kind == SEG_DATA) {
        cJSON_AddStringToObject(e, "file", g->file);
        cJSON_AddStringToObject(e, "interpolation", g->step_interp ? "step" : "linear");
      }
      cJSON_AddItemToArray(arr, e);
    }
    cJSON_AddNumberToObject(o, "loops", st->loops);
    break;
  }
  }
  return o;
}

static void add_names(cJSON *o, const char *key, const char (*names)[CFG_MAX_NAME], size_t n)
{
  cJSON *arr = cJSON_AddArrayToObject(o, key);
  for (size_t i = 0; i < n; i++) {
    cJSON_AddItemToArray(arr, cJSON_CreateString(names[i]));
  }
}

/* The scenario as JSON, every key present (defaults filled in; null for "not set"). */
static cJSON *scenario_json(const scenario_t *sc)
{
  static const char *const waits[] = {"none", "sleep", "busy"};
  cJSON *j = cJSON_CreateObject();
  cJSON *m = cJSON_AddObjectToObject(j, "model");
  cJSON_AddStringToObject(m, "fmu", sc->fmu);
  cJSON_AddNumberToObject(m, "step", sc->step);
  cJSON_AddItemToObject(m, "duration", number_or_null(sc->duration));
  cJSON *p = cJSON_AddObjectToObject(m, "params");
  for (size_t i = 0; i < sc->n_params; i++) {
    cJSON_AddNumberToObject(p, sc->params[i].name, sc->params[i].value);
  }
  cJSON *rt = cJSON_AddObjectToObject(j, "rt");
  cJSON_AddStringToObject(rt, "wait", waits[sc->wait]);
  cJSON_AddNumberToObject(rt, "max_overruns", sc->max_overruns);
  cJSON_AddItemToObject(rt, "cpu", sc->cpu >= 0 ? cJSON_CreateNumber(sc->cpu) : cJSON_CreateNull());
  cJSON_AddItemToObject(rt, "priority",
                        sc->priority >= 0 ? cJSON_CreateNumber(sc->priority) : cJSON_CreateNull());
  cJSON_AddItemToObject(rt, "timing_gpio", sc->timing_gpio >= 0
                                               ? cJSON_CreateNumber(sc->timing_gpio)
                                               : cJSON_CreateNull());
  cJSON *io = cJSON_AddObjectToObject(j, "io");
  cJSON_AddStringToObject(io, "backend", sc->backend);
  cJSON_AddStringToObject(io, "device", sc->device);
  cJSON_AddNumberToObject(io, "baud", sc->baud);
  cJSON_AddNumberToObject(io, "timeout_ms", sc->timeout_ms);
  cJSON_AddStringToObject(io, "late", sc->late_fault ? "fault" : "hold");
  cJSON_AddNumberToObject(io, "max_late", sc->max_late);
  add_names(io, "inputs", sc->inputs, sc->n_inputs);
  add_names(io, "outputs", sc->outputs, sc->n_outputs);
  cJSON *stim = cJSON_AddArrayToObject(io, "stimulus");
  for (size_t i = 0; i < sc->n_stimuli; i++) {
    cJSON_AddItemToArray(stim, stimulus_json(&sc->stimuli[i]));
  }
  cJSON_AddStringToObject(cJSON_AddObjectToObject(j, "log"), "file", sc->log_file);
  return j;
}

/* A scenario file, scenario text or an FMU's starter scenario as the text, the scenario as JSON
 * and its FMU's variables, for a client to edit before loading. What cannot be read goes into
 * "problem" (the reply is still ok, with what could be read). */
static int op_describe(const cJSON *req, cJSON *r, char *err, size_t errlen)
{
  static scenario_t sc; /* Limitation: one request at a time */
  const char *path = get_str(req, "scenario"), *text = get_str(req, "toml"),
             *fmu = get_str(req, "fmu");
  if ((path != NULL) + (text != NULL) + (fmu != NULL) != 1) {
    snprintf(err, errlen,
             "describe takes \"scenario\" (a path on the server), \"toml\" (the text) or \"fmu\" "
             "(a path on the server: its starter scenario)");
    return -1;
  }
  char *owned = NULL;
  if (path != NULL && (text = read_toml("describe", path, err, errlen)) == NULL) {
    return -1;
  }
  if (fmu != NULL && (text = owned = starter(fmu, err, errlen)) == NULL) {
    return -1;
  }
  cJSON_AddStringToObject(r, "text", text);
  char problem[400];
  int rc = config_parse(text, &sc, problem, sizeof problem);
  free(owned);
  if (rc < 0) {
    cJSON_AddStringToObject(r, "problem", problem);
    return 0;
  }
  cJSON_AddItemToObject(r, "scenario", scenario_json(&sc));
  fmu_pkg_t pkg = {0};
  if (fmu_open(sc.fmu, &pkg, problem, sizeof problem) < 0) {
    cJSON_AddStringToObject(r, "problem", problem);
  } else {
    add_variables(r, &pkg.md);
  }
  fmu_close(&pkg);
  return 0;
}

/* Parses the text and checks it against its FMU as loading would, without loading. */
static int check_text(const char *text, char *err, size_t errlen)
{
  static scenario_t sc; /* Limitation: one request at a time */
  char perr[400];
  if (config_parse(text, &sc, perr, sizeof perr) < 0) {
    snprintf(err, errlen, "scenario: %s", perr);
    return -1;
  }
  return scenario_check(&sc, err, errlen);
}

static int op_check(const cJSON *req, char *err, size_t errlen)
{
  const char *text = get_str(req, "toml");
  if (text == NULL) {
    snprintf(err, errlen, "check takes \"toml\" (the scenario text)");
    return -1;
  }
  return check_text(text, err, errlen);
}

/* Writes a checked scenario to scenarios/<name>.toml: through a temp file and a rename, so a
 * reader never sees half a file. */
static int op_save(const cJSON *req, char *err, size_t errlen)
{
  const char *path = get_str(req, "path"), *text = get_str(req, "toml");
  const char *name = path != NULL && strncmp(path, "scenarios/", 10) == 0 ? path + 10 : "";
  size_t n = strlen(name);
  if (text == NULL || n < 6 || n > 100 || strcmp(name + n - 5, ".toml") != 0 || name[0] == '.' ||
      strpbrk(name, "/\\") != NULL) {
    snprintf(err, errlen, "save takes \"path\": scenarios/<name>.toml and \"toml\": the text");
    return -1;
  }
  if (check_text(text, err, errlen) < 0) {
    return -1;
  }
  if (!cJSON_IsTrue(item(req, "overwrite")) && access(path, F_OK) == 0) {
    snprintf(err, errlen, "%s exists (\"overwrite\": true replaces it)", path);
    return -1;
  }
  mkdir("scenarios", 0777); /* fails harmlessly when it exists */
  char tmp[128];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  bool ok = f != NULL && fputs(text, f) >= 0;
  ok = f != NULL && fclose(f) == 0 && ok;
  if (!ok || rename(tmp, path) < 0) {
    snprintf(err, errlen, "cannot write %s: %s", path, strerror(errno));
    remove(tmp);
    return -1;
  }
  return 0;
}

static int op_load(session_t *s, const cJSON *req, cJSON *r, char *err, size_t errlen)
{
  static scenario_t sc; /* Limitation: one request at a time, so one buffer will do */
  const char *path = get_str(req, "scenario"), *text = get_str(req, "toml");
  if ((path == NULL) == (text == NULL)) {
    snprintf(err, errlen, "load takes \"scenario\" (a path on the server) or \"toml\" (the text)");
    return -1;
  }
  char perr[400];
  if ((path != NULL ? config_load(path, &sc, perr, sizeof perr)
                    : config_parse(text, &sc, perr, sizeof perr)) < 0) {
    snprintf(err, errlen, "scenario: %s", perr);
    return -1;
  }
  if (session_load(s, &sc, err, errlen) < 0) {
    return -1;
  }
  add_state(s, r);
  return 0;
}

/* A starter scenario for an FMU on the server (krumpir init), returned as text. */
static int op_init(const cJSON *req, cJSON *r, char *err, size_t errlen)
{
  const char *fmu = get_str(req, "fmu");
  if (fmu == NULL) {
    snprintf(err, errlen, "init takes \"fmu\" (a path on the server)");
    return -1;
  }
  char *text = starter(fmu, err, errlen);
  if (text == NULL) {
    return -1;
  }
  cJSON_AddStringToObject(r, "toml", text);
  free(text);
  return 0;
}

static int op_variables(session_t *s, cJSON *r, char *err, size_t errlen)
{
  const modeldesc_t *md = session_model(s);
  if (md == NULL) {
    snprintf(err, errlen, "no scenario loaded");
    return -1;
  }
  add_variables(r, md);
  return 0;
}

static int op_read(session_t *s, const cJSON *req, cJSON *r, char *err, size_t errlen)
{
  const cJSON *names = item(req, "names");
  if (!cJSON_IsArray(names)) {
    snprintf(err, errlen, "read takes \"names\": [ ... ]");
    return -1;
  }
  cJSON *values = cJSON_AddObjectToObject(r, "values");
  const cJSON *n;
  cJSON_ArrayForEach(n, names)
  {
    double v;
    if (!cJSON_IsString(n)) {
      snprintf(err, errlen, "\"names\" must hold strings");
      return -1;
    }
    if (session_read(s, n->valuestring, &v, err, errlen) < 0) {
      return -1;
    }
    cJSON_AddNumberToObject(values, n->valuestring, v);
  }
  return 0;
}

static int op_write(session_t *s, const cJSON *req, char *err, size_t errlen)
{
  const cJSON *values = item(req, "values");
  if (!cJSON_IsObject(values)) {
    snprintf(err, errlen, "write takes \"values\": { name: number, ... }");
    return -1;
  }
  const cJSON *v;
  cJSON_ArrayForEach(v, values)
  {
    if (!cJSON_IsNumber(v)) {
      snprintf(err, errlen, "value of '%s' must be a number", v->string);
      return -1;
    }
    if (session_write(s, v->string, v->valuedouble, err, errlen) < 0) {
      return -1; /* the writes before this one have been applied */
    }
  }
  return 0;
}

static int op_signal(session_t *s, const cJSON *req, char *err, size_t errlen)
{
  const cJSON *j = item(req, "stimulus");
  const scenario_t *sc = session_scenario(s);
  if (!cJSON_IsObject(j)) {
    snprintf(err, errlen, "signal takes \"stimulus\": { input, kind or segments, ... }");
    return -1;
  }
  if (sc == NULL) {
    snprintf(err, errlen, "no scenario loaded");
    return -1;
  }
  sb_t b = {0};
  int rc = to_toml(&b, j, err, errlen);
  if (rc == 0 && b.oom) {
    snprintf(err, errlen, "out of memory");
    rc = -1;
  }
  stimulus_t st;
  if (rc == 0) {
    rc = config_parse_stimulus(b.p, sc, &st, err, errlen);
  }
  free(b.p);
  return rc == 0 ? session_set_signal(s, &st, err, errlen) : -1;
}

static int get_cond(const cJSON *req, const char *key, capture_cond_t *c, char *err,
                    size_t errlen)
{
  static const char *const ops[] = {">", ">=", "<", "<="};
  static const cond_op_t codes[] = {COND_GT, COND_GE, COND_LT, COND_LE};
  const cJSON *o = item(req, key);
  *c = (capture_cond_t){.op = COND_NONE};
  if (o == NULL) {
    return 0;
  }
  const char *var = get_str(o, "var"), *op = get_str(o, "op");
  const cJSON *value = item(o, "value");
  if (var == NULL || op == NULL || !cJSON_IsNumber(value) || strlen(var) >= sizeof c->var) {
    snprintf(err, errlen, "\"%s\" must be { \"var\": name, \"op\": \">\", \"value\": number }",
             key);
    return -1;
  }
  for (size_t i = 0; i < 4; i++) {
    if (strcmp(op, ops[i]) == 0) {
      c->op = codes[i];
    }
  }
  if (c->op == COND_NONE) {
    snprintf(err, errlen, "\"%s\".op must be >, >=, < or <=", key);
    return -1;
  }
  snprintf(c->var, sizeof c->var, "%s", var);
  c->value = value->valuedouble;
  return 0;
}

static int op_capture_start(session_t *s, const cJSON *req, cJSON *r, char *err, size_t errlen)
{
  static capture_spec_t spec; /* Limitation: large; one request at a time */
  spec = (capture_spec_t){0};
  const cJSON *vars = item(req, "vars"), *v;
  if (vars != NULL && !cJSON_IsArray(vars)) {
    snprintf(err, errlen, "\"vars\" must be a list of names");
    return -1;
  }
  cJSON_ArrayForEach(v, vars)
  {
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= CFG_MAX_NAME ||
        spec.n_vars == 2 * TELEM_MAX_CH) {
      snprintf(err, errlen, "\"vars\" must be up to %d names", 2 * TELEM_MAX_CH);
      return -1;
    }
    snprintf(spec.vars[spec.n_vars++], CFG_MAX_NAME, "%s", v->valuestring);
  }
  double max_rows = 0;
  if (get_cond(req, "start", &spec.start, err, errlen) < 0 ||
      get_cond(req, "stop", &spec.stop, err, errlen) < 0 ||
      get_num(req, "duration", &spec.duration, err, errlen) < 0 ||
      get_num(req, "max_rows", &max_rows, err, errlen) < 0) {
    return -1;
  }
  if (!(max_rows >= 0 && max_rows <= 1e12)) {
    snprintf(err, errlen, "\"max_rows\" must be >= 0");
    return -1;
  }
  spec.max_rows = (size_t)max_rows;
  int id = session_capture_start(s, &spec, err, errlen);
  if (id < 0) {
    return -1;
  }
  cJSON_AddNumberToObject(r, "id", id);
  return 0;
}

/* The rows go into *rows as JSON text: a cJSON tree of a large capture would cost far more
 * memory than the numbers. */
static int op_capture_fetch(session_t *s, const cJSON *req, cJSON *r, sb_t *rows, char *err,
                            size_t errlen)
{
  int id;
  double from = 0, max = INFINITY;
  if (get_id(req, &id, err, errlen) < 0 || get_num(req, "from", &from, err, errlen) < 0 ||
      get_num(req, "max", &max, err, errlen) < 0) {
    return -1;
  }
  capture_data_t d;
  if (session_capture_fetch(s, id, &d, err, errlen) < 0) {
    return -1;
  }
  size_t first = from > 0 ? (size_t)fmin(from, (double)d.n_rows) : 0;
  size_t last = max > 0 ? (size_t)fmin((double)d.n_rows, (double)first + max) : first;
  cJSON_AddStringToObject(r, "capture_state", cap_state_name(d.state));
  cJSON_AddNumberToObject(r, "n_rows", (double)d.n_rows);
  cJSON_AddNumberToObject(r, "from", (double)first);
  cJSON_AddItemToObject(r, "names", cJSON_CreateStringArray(d.names, (int)d.n_cols));
  sb_add(rows, "[");
  for (size_t i = first; i < last; i++) {
    sb_add(rows, i > first ? ",[" : "[");
    for (size_t k = 0; k < d.n_cols; k++) {
      sb_add(rows, k > 0 ? "," : "");
      sb_number(rows, d.rows[i * d.n_cols + k]);
    }
    sb_add(rows, "]");
  }
  sb_add(rows, "]");
  free(d.rows);
  return 0;
}

static void add_summary(cJSON *r, const session_summary_t *m)
{
  cJSON *o = cJSON_AddObjectToObject(r, "summary");
  cJSON_AddStringToObject(o, "state", lifecycle_name(m->state));
  cJSON_AddBoolToObject(o, "stopped_early", m->stopped_early);
  cJSON_AddNumberToObject(o, "steps", (double)m->steps);
  cJSON_AddNumberToObject(o, "overruns", (double)m->overruns);
  cJSON_AddNumberToObject(o, "dropped", (double)m->dropped);
  cJSON_AddNumberToObject(o, "rows", (double)m->rows);
  cJSON_AddNumberToObject(o, "max_wake_ns", (double)m->max_wake_ns);
  cJSON_AddNumberToObject(o, "max_step_ns", (double)m->max_step_ns);
  cJSON_AddNumberToObject(o, "mean_step_ns", m->mean_step_ns);
  cJSON_AddNumberToObject(o, "late", (double)m->late);
}

static int dispatch(session_t *s, const char *op, const cJSON *req, cJSON *r, sb_t *rows,
                    char *err, size_t errlen)
{
  if (strcmp(op, "info") == 0) {
    cJSON_AddNumberToObject(r, "protocol", PROTOCOL_VERSION);
    add_state(s, r);
    return 0;
  }
  if (strcmp(op, "state") == 0) {
    add_status(s, r);
    return 0;
  }
  if (strcmp(op, "files") == 0) {
    op_files(r);
    return 0;
  }
  if (strcmp(op, "text") == 0) {
    return op_text(req, r, err, errlen);
  }
  if (strcmp(op, "load") == 0) {
    return op_load(s, req, r, err, errlen);
  }
  if (strcmp(op, "variables") == 0) {
    return op_variables(s, r, err, errlen);
  }
  if (strcmp(op, "init") == 0) {
    return op_init(req, r, err, errlen);
  }
  if (strcmp(op, "describe") == 0) {
    return op_describe(req, r, err, errlen);
  }
  if (strcmp(op, "check") == 0) {
    return op_check(req, err, errlen);
  }
  if (strcmp(op, "save") == 0) {
    return op_save(req, err, errlen);
  }
  if (strcmp(op, "start") == 0) {
    if (session_start(s, err, errlen) < 0) {
      return -1;
    }
    add_state(s, r);
    return 0;
  }
  if (strcmp(op, "stop") == 0) {
    session_stop(s);
    add_state(s, r);
    return 0;
  }
  if (strcmp(op, "wait") == 0) {
    double ms = -1;
    if (get_num(req, "timeout_ms", &ms, err, errlen) < 0) {
      return -1;
    }
    cJSON_AddBoolToObject(r, "done", session_wait(s, ms < 0 ? -1 : (int)fmin(ms, 1e9)));
    add_state(s, r);
    return 0;
  }
  if (strcmp(op, "reset") == 0) {
    session_summary_t m;
    if (session_reset(s, &m, err, errlen) < 0) {
      return -1;
    }
    add_summary(r, &m);
    add_state(s, r);
    return 0;
  }
  if (strcmp(op, "unload") == 0) {
    session_summary_t m;
    session_unload(s, &m);
    add_summary(r, &m);
    add_state(s, r);
    return 0;
  }
  if (strcmp(op, "read") == 0) {
    return op_read(s, req, r, err, errlen);
  }
  if (strcmp(op, "write") == 0) {
    return op_write(s, req, err, errlen);
  }
  if (strcmp(op, "signal") == 0) {
    return op_signal(s, req, err, errlen);
  }
  if (strcmp(op, "capture_start") == 0) {
    return op_capture_start(s, req, r, err, errlen);
  }
  if (strcmp(op, "capture_fetch") == 0) {
    return op_capture_fetch(s, req, r, rows, err, errlen);
  }
  if (strcmp(op, "capture_stop") == 0) {
    int id;
    return get_id(req, &id, err, errlen) < 0 ? -1 : session_capture_stop(s, id, err, errlen);
  }
  snprintf(err, errlen, "unknown op '%s'", op);
  return -1;
}

static cJSON *new_reply(const cJSON *tag, bool ok)
{
  cJSON *r = cJSON_CreateObject();
  if (tag != NULL) {
    cJSON_AddItemToObject(r, "tag", cJSON_Duplicate(tag, true));
  }
  cJSON_AddBoolToObject(r, "ok", ok);
  return r;
}

/* One request line in, one reply line out (malloc'd, without the newline). */
static char *handle(session_t *s, const char *line)
{
  char err[512] = "";
  cJSON *req = cJSON_Parse(line);
  const cJSON *tag = cJSON_IsObject(req) ? item(req, "tag") : NULL;
  const char *op = cJSON_IsObject(req) ? get_str(req, "op") : NULL;
  cJSON *r = new_reply(tag, true);
  sb_t rows = {0};
  int rc = -1;
  if (!cJSON_IsObject(req)) {
    snprintf(err, sizeof err, "a request is one JSON object per line");
  } else if (op == NULL) {
    snprintf(err, sizeof err, "missing \"op\"");
  } else {
    rc = dispatch(s, op, req, r, &rows, err, sizeof err);
  }
  if (rc == 0 && rows.oom) {
    snprintf(err, sizeof err, "out of memory");
    rc = -1;
  }
  if (rc < 0) {
    cJSON_Delete(r);
    r = new_reply(tag, false);
    cJSON_AddStringToObject(r, "error", err[0] != '\0' ? err : "failed");
  }
  char *out = cJSON_PrintUnformatted(r);
  cJSON_Delete(r);
  cJSON_Delete(req);
  if (out != NULL && rc == 0 && rows.p != NULL) { /* splice the rows in before the closing } */
    sb_t b = {0};
    sb_add(&b, "%.*s,\"rows\":%s}", (int)(strlen(out) - 1), out, rows.p);
    free(out);
    out = b.oom ? NULL : b.p;
  }
  free(rows.p);
  return out != NULL ? out : strdup("{\"ok\":false,\"error\":\"out of memory\"}");
}

static int send_all(int fd, const char *p, size_t n)
{
  while (n > 0) {
    ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
    if (k < 0 && errno == EINTR) {
      continue;
    }
    if (k <= 0) {
      return -1;
    }
    p += k;
    n -= (size_t)k;
  }
  return 0;
}

static void serve_client(session_t *s, int fd)
{
  char *buf = NULL;
  size_t len = 0, cap = 0;
  while (!g_quit) {
    if (cap - len < 4096) {
      if (cap > MAX_LINE) {
        const char *msg = "{\"ok\":false,\"error\":\"request line too long\"}\n";
        send_all(fd, msg, strlen(msg));
        break;
      }
      size_t ncap = cap > 0 ? cap * 2 : 65536;
      char *p = realloc(buf, ncap);
      if (p == NULL) {
        break;
      }
      buf = p;
      cap = ncap;
    }
    ssize_t n = recv(fd, buf + len, cap - len, 0);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      break; /* the client has gone; the session stays as it is */
    }
    len += (size_t)n;
    char *start = buf, *nl;
    while ((nl = memchr(start, '\n', (size_t)(buf + len - start))) != NULL) {
      *nl = '\0';
      if (nl > start && nl[-1] == '\r') {
        nl[-1] = '\0';
      }
      if (*start != '\0') {
        char *reply = handle(s, start);
        bool sent = send_all(fd, reply, strlen(reply)) == 0 && send_all(fd, "\n", 1) == 0;
        free(reply);
        if (!sent) {
          free(buf);
          return;
        }
      }
      start = nl + 1;
    }
    len = (size_t)(buf + len - start);
    memmove(buf, start, len);
  }
  free(buf);
}

int serve(const char *host, int port)
{
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_signal; /* no SA_RESTART: accept() and recv() return EINTR */
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (port < 0 || port > 65535 || inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    fprintf(stderr, "krumpir: listen address must be an IPv4 address and a port 0..65535\n");
    return 1;
  }
  int lfd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  if (lfd < 0 || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) < 0 ||
      bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(lfd, 4) < 0) {
    fprintf(stderr, "krumpir: cannot listen on %s:%d: %s\n", host, port, strerror(errno));
    if (lfd >= 0) {
      close(lfd);
    }
    return 1;
  }
  socklen_t alen = sizeof addr;
  getsockname(lfd, (struct sockaddr *)&addr, &alen);
  printf("listening on %s:%d\n", host, ntohs(addr.sin_port));
  fflush(stdout);

  session_t *s = session_create();
  if (s == NULL) {
    close(lfd);
    return 1;
  }
  g_run_stop = session_stop_flag(s);
  while (!g_quit) {
    int fd = accept(lfd, NULL, NULL);
    if (fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "krumpir: accept: %s\n", strerror(errno));
      break;
    }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    serve_client(s, fd);
    close(fd);
  }
  close(lfd);
  g_run_stop = NULL;
  session_destroy(s); /* stops and unloads a run that is still going */
  return 0;
}
