#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toml.h"

#define MAX_FILE_BYTES (1u << 20)

typedef struct {
  char *err;
  size_t errlen;
} ctx_t;

static int fail(ctx_t *c, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(c->err, c->errlen, fmt, ap);
  va_end(ap);
  return -1;
}

static bool in_list(const char *s, const char *const *list)
{
  for (; *list != NULL; list++) {
    if (strcmp(s, *list) == 0) {
      return true;
    }
  }
  return false;
}

static int check_keys(ctx_t *c, const toml_table_t *tab, const char *where,
                      const char *const *allowed)
{
  for (int i = 0; toml_key_in(tab, i) != NULL; i++) {
    const char *k = toml_key_in(tab, i);
    if (!in_list(k, allowed)) {
      return fail(c, "unknown key '%s' in %s", k, where);
    }
  }
  return 0;
}

/* Returns 1 if present and copied, 0 if absent and optional, -1 on error. */
static int get_string(ctx_t *c, const toml_table_t *tab, const char *where, const char *key,
                      char *dst, size_t cap, bool required)
{
  toml_datum_t d = toml_string_in(tab, key);
  if (!d.ok) {
    if (toml_key_exists(tab, key)) {
      return fail(c, "%s.%s must be a string", where, key);
    }
    return required ? fail(c, "missing %s.%s", where, key) : 0;
  }
  size_t len = strlen(d.u.s);
  int rc = 1;
  if (len == 0 || len >= cap) {
    rc = fail(c, "%s.%s must be 1 to %zu characters", where, key, cap - 1);
  } else {
    memcpy(dst, d.u.s, len + 1);
  }
  free(d.u.s);
  return rc;
}

static int get_number(ctx_t *c, const toml_table_t *tab, const char *where, const char *key,
                      double *dst, bool required)
{
  toml_datum_t d = toml_double_in(tab, key);
  if (d.ok) {
    *dst = d.u.d;
  } else {
    d = toml_int_in(tab, key);
    if (d.ok) {
      *dst = (double)d.u.i;
    } else if (toml_key_exists(tab, key)) {
      return fail(c, "%s.%s must be a number", where, key);
    } else {
      return required ? fail(c, "missing %s.%s", where, key) : 0;
    }
  }
  if (!isfinite(*dst)) {
    return fail(c, "%s.%s must be finite", where, key);
  }
  return 1;
}

static int get_int(ctx_t *c, const toml_table_t *tab, const char *where, const char *key, int *dst,
                   int lo, int hi)
{
  toml_datum_t d = toml_int_in(tab, key);
  if (!d.ok) {
    return toml_key_exists(tab, key) ? fail(c, "%s.%s must be an integer", where, key) : 0;
  }
  if (d.u.i < lo || d.u.i > hi) {
    return fail(c, "%s.%s must be between %d and %d", where, key, lo, hi);
  }
  *dst = (int)d.u.i;
  return 1;
}

static int get_names(ctx_t *c, const toml_table_t *tab, const char *where, const char *key,
                     char names[][CFG_MAX_NAME], size_t *n)
{
  *n = 0;
  if (!toml_key_exists(tab, key)) {
    return 0;
  }
  toml_array_t *arr = toml_array_in(tab, key);
  if (arr == NULL) {
    return fail(c, "%s.%s must be an array of strings", where, key);
  }
  int count = toml_array_nelem(arr);
  if (count > TELEM_MAX_CH) {
    return fail(c, "%s.%s has more than %d entries", where, key, TELEM_MAX_CH);
  }
  for (int i = 0; i < count; i++) {
    toml_datum_t d = toml_string_at(arr, i);
    if (!d.ok) {
      return fail(c, "%s.%s must contain only strings", where, key);
    }
    size_t len = strlen(d.u.s);
    bool bad = len == 0 || len >= CFG_MAX_NAME;
    for (int j = 0; !bad && j < i; j++) {
      bad = strcmp(names[j], d.u.s) == 0;
    }
    if (bad) {
      free(d.u.s);
      return fail(c, "%s.%s entry %d is empty, too long or duplicated", where, key, i);
    }
    memcpy(names[i], d.u.s, len + 1);
    free(d.u.s);
  }
  *n = (size_t)count;
  return 0;
}

static int parse_params(ctx_t *c, const toml_table_t *model, scenario_t *sc)
{
  if (!toml_key_exists(model, "params")) {
    return 0;
  }
  toml_table_t *p = toml_table_in(model, "params");
  if (p == NULL) {
    return fail(c, "model.params must be a table");
  }
  for (int i = 0; toml_key_in(p, i) != NULL; i++) {
    const char *k = toml_key_in(p, i);
    if (sc->n_params == CFG_MAX_PARAMS) {
      return fail(c, "model.params has more than %d entries", CFG_MAX_PARAMS);
    }
    if (strlen(k) >= CFG_MAX_NAME) {
      return fail(c, "model.params key '%s' is too long", k);
    }
    param_t *out = &sc->params[sc->n_params];
    if (get_number(c, p, "model.params", k, &out->value, true) < 0) {
      return -1;
    }
    memcpy(out->name, k, strlen(k) + 1);
    sc->n_params++;
  }
  return 0;
}

static bool has_input(const scenario_t *sc, const char *name)
{
  for (size_t i = 0; i < sc->n_inputs; i++) {
    if (strcmp(sc->inputs[i], name) == 0) {
      return true;
    }
  }
  return false;
}

/* One entry of io.stimulus[].segments (see segment_t). duration: optional, > 0; absent means to
 * the end of the run (last segment only), or the file's length for a data segment. */
static int parse_segment(ctx_t *c, const toml_table_t *t, segment_t *g, bool last)
{
  static const char *const keys[] = {"kind",   "duration", "value",  "from",   "to",
                                     "offset", "amplitude", "freq_hz", "phase", "low",
                                     "high",   "period",   "duty",   "file",   "interpolation",
                                     NULL};
  const char *w = "io.stimulus.segments";
  char kind[CFG_MAX_NAME];
  *g = (segment_t){0};
  if (check_keys(c, t, w, keys) < 0 || get_string(c, t, w, "kind", kind, sizeof kind, true) < 0) {
    return -1;
  }
  int has_duration = get_number(c, t, w, "duration", &g->duration, false);
  if (has_duration < 0) {
    return -1;
  }
  if (has_duration == 1 && g->duration <= 0) {
    return fail(c, "%s duration must be > 0", w);
  }

  if (strcmp(kind, "const") == 0) {
    g->kind = SEG_CONST;
    if (get_number(c, t, w, "value", &g->value, true) < 0) {
      return -1;
    }
  } else if (strcmp(kind, "ramp") == 0) {
    g->kind = SEG_RAMP;
    int f = get_number(c, t, w, "from", &g->from, false);
    if (f < 0 || get_number(c, t, w, "to", &g->to, true) < 0) {
      return -1;
    }
    g->has_from = f == 1;
    if (has_duration == 0) {
      return fail(c, "%s ramp needs a duration", w);
    }
  } else if (strcmp(kind, "sine") == 0) {
    g->kind = SEG_SINE;
    if (get_number(c, t, w, "offset", &g->offset, false) < 0 ||
        get_number(c, t, w, "amplitude", &g->amplitude, true) < 0 ||
        get_number(c, t, w, "freq_hz", &g->freq_hz, true) < 0 ||
        get_number(c, t, w, "phase", &g->phase, false) < 0) {
      return -1;
    }
    if (g->freq_hz <= 0) {
      return fail(c, "%s freq_hz must be > 0", w);
    }
  } else if (strcmp(kind, "pulse") == 0) {
    g->kind = SEG_PULSE;
    if (get_number(c, t, w, "low", &g->low, false) < 0 ||
        get_number(c, t, w, "high", &g->high, true) < 0 ||
        get_number(c, t, w, "period", &g->period, true) < 0 ||
        get_number(c, t, w, "duty", &g->duty, true) < 0) {
      return -1;
    }
    if (g->period <= 0 || g->duty < 0 || g->duty > 1) {
      return fail(c, "%s pulse needs period > 0 and duty in 0..1", w);
    }
  } else if (strcmp(kind, "idle") == 0) {
    g->kind = SEG_IDLE;
  } else if (strcmp(kind, "data") == 0) {
    g->kind = SEG_DATA;
    char interp[16] = "linear";
    if (get_string(c, t, w, "file", g->file, sizeof g->file, true) < 0 ||
        get_string(c, t, w, "interpolation", interp, sizeof interp, false) < 0) {
      return -1;
    }
    if (strcmp(interp, "linear") != 0 && strcmp(interp, "step") != 0) {
      return fail(c, "%s interpolation must be linear or step", w);
    }
    g->step_interp = strcmp(interp, "step") == 0;
  } else {
    return fail(c, "%s kind '%s' is not const, ramp, sine, pulse, idle or data", w, kind);
  }
  if (has_duration == 0) {
    if (g->kind == SEG_DATA) {
      g->duration = NAN; /* the file's length, known once the file is read */
    } else if (!last) {
      return fail(c, "%s: only the last segment may leave out its duration", w);
    } else {
      g->duration = INFINITY;
    }
  }
  return 0;
}

static int parse_segments(ctx_t *c, const toml_table_t *t, stimulus_t *s)
{
  toml_array_t *arr = toml_array_in(t, "segments");
  int count = arr != NULL ? toml_array_nelem(arr) : 0;
  if (arr == NULL || count == 0 || toml_array_kind(arr) != 't') {
    return fail(c, "io.stimulus.segments must be a non-empty array of tables");
  }
  if (count > CFG_MAX_SEGMENTS) {
    return fail(c, "io.stimulus.segments has more than %d entries", CFG_MAX_SEGMENTS);
  }
  for (int i = 0; i < count; i++) {
    if (parse_segment(c, toml_table_at(arr, i), &s->segs[i], i == count - 1) < 0) {
      return -1;
    }
  }
  s->n_segs = (size_t)count;
  s->loops = 1;
  if (get_int(c, t, "io.stimulus", "loops", &s->loops, 0, 1000000000) < 0) {
    return -1;
  }
  if (s->loops != 1 && isinf(s->segs[count - 1].duration)) {
    return fail(c, "io.stimulus loops needs a duration on every segment");
  }
  s->kind = STIM_SEGMENTS;
  return 0;
}

/* One io.stimulus entry; its input must be one of sc's inputs. */
static int parse_stimulus_entry(ctx_t *c, const toml_table_t *t, const scenario_t *sc,
                                stimulus_t *s)
{
  static const char *const keys[] = {"input",  "kind",      "value",   "t",        "from", "to",
                                     "offset", "amplitude", "freq_hz", "segments", "loops", NULL};
  char kind[CFG_MAX_NAME];
  *s = (stimulus_t){0};
  bool segmented = toml_key_exists(t, "segments");
  if (check_keys(c, t, "io.stimulus", keys) < 0 ||
      get_string(c, t, "io.stimulus", "input", s->input, sizeof s->input, true) < 0 ||
      (!segmented && get_string(c, t, "io.stimulus", "kind", kind, sizeof kind, true) < 0)) {
    return -1;
  }
  if (segmented && toml_key_exists(t, "kind")) {
    return fail(c, "io.stimulus has both kind and segments");
  }
  if (!segmented && toml_key_exists(t, "loops")) {
    return fail(c, "io.stimulus loops needs segments");
  }
  if (!has_input(sc, s->input)) {
    return fail(c, "io.stimulus input '%s' is not listed in io.inputs", s->input);
  }
  if (segmented) {
    return parse_segments(c, t, s);
  }
  if (strcmp(kind, "constant") == 0) {
    s->kind = STIM_CONSTANT;
    return get_number(c, t, "io.stimulus", "value", &s->value, true) < 0 ? -1 : 0;
  }
  if (strcmp(kind, "step") == 0) {
    s->kind = STIM_STEP;
    if (get_number(c, t, "io.stimulus", "t", &s->t, true) < 0 ||
        get_number(c, t, "io.stimulus", "from", &s->from, true) < 0 ||
        get_number(c, t, "io.stimulus", "to", &s->to, true) < 0) {
      return -1;
    }
    return s->t < 0 ? fail(c, "io.stimulus step time must be >= 0") : 0;
  }
  if (strcmp(kind, "sine") == 0) {
    s->kind = STIM_SINE;
    if (get_number(c, t, "io.stimulus", "offset", &s->offset, false) < 0 ||
        get_number(c, t, "io.stimulus", "amplitude", &s->amplitude, true) < 0 ||
        get_number(c, t, "io.stimulus", "freq_hz", &s->freq_hz, true) < 0) {
      return -1;
    }
    return s->freq_hz <= 0 ? fail(c, "io.stimulus freq_hz must be > 0") : 0;
  }
  return fail(c, "io.stimulus kind '%s' is not constant, step or sine", kind);
}

static int parse_stimulus(ctx_t *c, const toml_table_t *tab, scenario_t *sc)
{
  if (!toml_key_exists(tab, "stimulus")) {
    return 0;
  }
  toml_array_t *arr = toml_array_in(tab, "stimulus");
  if (arr == NULL || (toml_array_nelem(arr) > 0 && toml_array_kind(arr) != 't')) {
    return fail(c, "io.stimulus must be an array of tables");
  }
  int count = toml_array_nelem(arr);
  if (count > TELEM_MAX_CH) {
    return fail(c, "io.stimulus has more than %d entries", TELEM_MAX_CH);
  }
  for (int i = 0; i < count; i++) {
    stimulus_t *s = &sc->stimuli[i];
    if (parse_stimulus_entry(c, toml_table_at(arr, i), sc, s) < 0) {
      return -1;
    }
    for (int j = 0; j < i; j++) {
      if (strcmp(sc->stimuli[j].input, s->input) == 0) {
        return fail(c, "io.stimulus has two entries for input '%s'", s->input);
      }
    }
    sc->n_stimuli++;
  }
  return 0;
}

int config_parse_stimulus(const char *entry, const scenario_t *sc, stimulus_t *s, char *err,
                          size_t errlen)
{
  ctx_t c = {err, errlen};
  err[0] = '\0';
  size_t len = strlen(entry) + 8;
  char *text = malloc(len);
  if (text == NULL) {
    return fail(&c, "out of memory");
  }
  snprintf(text, len, "s = %s\n", entry);
  char perr[200];
  toml_table_t *root = toml_parse(text, perr, sizeof perr);
  free(text);
  if (root == NULL) {
    return fail(&c, "stimulus: %s", perr);
  }
  toml_table_t *t = toml_table_in(root, "s");
  int rc = t != NULL ? parse_stimulus_entry(&c, t, sc, s)
                     : fail(&c, "a stimulus must be an inline table { input = ..., ... }");
  toml_free(root);
  return rc;
}

static int parse_model(ctx_t *c, const toml_table_t *model, scenario_t *sc)
{
  static const char *const keys[] = {"fmu", "step", "duration", "params", NULL};
  if (check_keys(c, model, "[model]", keys) < 0 ||
      get_string(c, model, "model", "fmu", sc->fmu, sizeof sc->fmu, true) < 0 ||
      get_number(c, model, "model", "step", &sc->step, true) < 0 ||
      parse_params(c, model, sc) < 0) {
    return -1;
  }
  if (sc->step < 1e-9) {
    return fail(c, "model.step must be at least 1e-9 s");
  }
  int has_duration = get_number(c, model, "model", "duration", &sc->duration, false);
  if (has_duration < 0) {
    return -1;
  }
  if (has_duration == 0) { /* runs until it is stopped */
    sc->duration = INFINITY;
    sc->n_steps = UINT64_MAX;
    return 0;
  }
  if (sc->duration < sc->step) {
    return fail(c, "model.duration must be at least one step");
  }
  double n = round(sc->duration / sc->step);
  if (fabs(n * sc->step - sc->duration) > 1e-6 * sc->step) {
    return fail(c, "model.duration must be a whole number of steps");
  }
  sc->n_steps = (uint64_t)n;
  return 0;
}

static int parse_rt(ctx_t *c, const toml_table_t *rt, scenario_t *sc)
{
  static const char *const keys[] = {"wait", "max_overruns", "cpu", "priority", "timing_gpio",
                                     NULL};
  char wait[CFG_MAX_NAME] = "none";
  if (check_keys(c, rt, "[rt]", keys) < 0 ||
      get_string(c, rt, "rt", "wait", wait, sizeof wait, false) < 0 ||
      get_int(c, rt, "rt", "max_overruns", &sc->max_overruns, 0, 1000000000) < 0 ||
      get_int(c, rt, "rt", "cpu", &sc->cpu, 0, 255) < 0 ||
      get_int(c, rt, "rt", "priority", &sc->priority, 1, 99) < 0 ||
      get_int(c, rt, "rt", "timing_gpio", &sc->timing_gpio, 0, 511) < 0) {
    return -1;
  }
  if (strcmp(wait, "none") == 0) {
    sc->wait = WAIT_NONE;
  } else if (strcmp(wait, "sleep") == 0) {
    sc->wait = WAIT_SLEEP;
  } else if (strcmp(wait, "busy") == 0) {
    sc->wait = WAIT_BUSY;
  } else {
    return fail(c, "rt.wait '%s' is not none, sleep or busy", wait);
  }
  return 0;
}

static int parse_io(ctx_t *c, const toml_table_t *io, scenario_t *sc)
{
  static const char *const keys[] = {"backend", "inputs",     "outputs", "stimulus", "device",
                                     "baud",    "timeout_ms", "late",    "max_late", NULL};
  char late[CFG_MAX_NAME] = "fault";
  sc->baud = 921600;
  sc->timeout_ms = 100;
  sc->max_late = 10;
  if (check_keys(c, io, "[io]", keys) < 0 ||
      get_string(c, io, "io", "backend", sc->backend, sizeof sc->backend, true) < 0 ||
      get_names(c, io, "io", "inputs", sc->inputs, &sc->n_inputs) < 0 ||
      get_names(c, io, "io", "outputs", sc->outputs, &sc->n_outputs) < 0 ||
      parse_stimulus(c, io, sc) < 0 ||
      get_string(c, io, "io", "device", sc->device, sizeof sc->device, false) < 0 ||
      get_int(c, io, "io", "baud", &sc->baud, 1200, 4000000) < 0 ||
      get_number(c, io, "io", "timeout_ms", &sc->timeout_ms, false) < 0 ||
      get_string(c, io, "io", "late", late, sizeof late, false) < 0 ||
      get_int(c, io, "io", "max_late", &sc->max_late, 1, 1000000000) < 0) {
    return -1;
  }
  if (strcmp(sc->backend, "uart") == 0) {
    if (sc->device[0] == '\0') {
      return fail(c, "io.backend 'uart' needs io.device");
    }
  } else if (strcmp(sc->backend, "null") != 0) {
    return fail(c, "io.backend '%s' is not supported (null or uart)", sc->backend);
  }
  if (sc->timeout_ms < 0 || sc->timeout_ms > 60000) {
    return fail(c, "io.timeout_ms must be between 0 and 60000");
  }
  if (strcmp(late, "fault") != 0 && strcmp(late, "hold") != 0) {
    return fail(c, "io.late '%s' is not fault or hold", late);
  }
  sc->late_fault = strcmp(late, "fault") == 0;
  return 0;
}

static int parse_root(ctx_t *c, const toml_table_t *root, scenario_t *sc)
{
  static const char *const root_keys[] = {"model", "rt", "io", "log", NULL};
  static const char *const log_keys[] = {"file", NULL};
  if (check_keys(c, root, "the top level", root_keys) < 0) {
    return -1;
  }
  toml_table_t *model = toml_table_in(root, "model");
  toml_table_t *rt = toml_table_in(root, "rt");
  toml_table_t *io = toml_table_in(root, "io");
  toml_table_t *log = toml_table_in(root, "log");
  if (model == NULL) {
    return fail(c, "missing [model]");
  }
  if (io == NULL) {
    return fail(c, "missing [io]");
  }
  if (log == NULL) {
    return fail(c, "missing [log]");
  }
  if (parse_model(c, model, sc) < 0 || (rt != NULL && parse_rt(c, rt, sc) < 0) ||
      parse_io(c, io, sc) < 0 || check_keys(c, log, "[log]", log_keys) < 0 ||
      get_string(c, log, "log", "file", sc->log_file, sizeof sc->log_file, true) < 0) {
    return -1;
  }
  return 0;
}

int config_parse(const char *text, scenario_t *sc, char *err, size_t errlen)
{
  ctx_t c = {err, errlen};
  char parse_err[200];
  *sc = (scenario_t){0};
  sc->wait = WAIT_NONE;
  sc->cpu = -1;
  sc->priority = -1;
  sc->timing_gpio = -1;

  char *copy = strdup(text); /* toml_parse modifies its input */
  if (copy == NULL) {
    return fail(&c, "out of memory");
  }
  toml_table_t *root = toml_parse(copy, parse_err, sizeof parse_err);
  free(copy);
  if (root == NULL) {
    return fail(&c, "invalid TOML: %s", parse_err);
  }
  int rc = parse_root(&c, root, sc);
  toml_free(root);
  return rc;
}

int config_load(const char *path, scenario_t *sc, char *err, size_t errlen)
{
  ctx_t c = {err, errlen};
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return fail(&c, "cannot open '%s'", path);
  }
  char *buf = malloc(MAX_FILE_BYTES + 1);
  if (buf == NULL) {
    fclose(fp);
    return fail(&c, "out of memory");
  }
  size_t n = fread(buf, 1, MAX_FILE_BYTES + 1, fp);
  bool read_error = ferror(fp) != 0;
  fclose(fp);
  if (read_error || n > MAX_FILE_BYTES) {
    free(buf);
    return fail(&c, "cannot read '%s' (error or larger than %u bytes)", path, MAX_FILE_BYTES);
  }
  buf[n] = '\0';
  int rc = config_parse(buf, sc, err, errlen);
  free(buf);
  return rc;
}
