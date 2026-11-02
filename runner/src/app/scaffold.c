#include "scaffold.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "loader.h"

#define DEFAULT_STEP 0.001
#define DEFAULT_STOP 1.0

static void put_str(FILE *out, const char *s)
{
  fputc('"', out);
  for (; *s != '\0'; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '"' || ch == '\\') {
      fprintf(out, "\\%c", ch);
    } else if (ch < 0x20 || ch == 0x7f) {
      fprintf(out, "\\u%04X", ch);
    } else {
      fputc(ch, out);
    }
  }
  fputc('"', out);
}

/* A TOML key: bare when it can be, quoted otherwise (e.g. "der(x)", "a.b"). */
static void put_key(FILE *out, const char *s)
{
  bool bare = *s != '\0';
  for (const char *p = s; bare && *p != '\0'; p++) {
    bare = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
           *p == '_' || *p == '-';
  }
  if (bare) {
    fputs(s, out);
  } else {
    put_str(out, s);
  }
}

static bool number(const char *s, double *v)
{
  char *end;
  *v = strtod(s, &end);
  return *s != '\0' && end != s && *end == '\0' && isfinite(*v);
}

/* Shortest of %.15g / %.17g that reads back exactly; always a valid TOML number. */
static void put_num(FILE *out, double v)
{
  char buf[40];
  snprintf(buf, sizeof buf, "%.15g", v);
  if (strtod(buf, NULL) != v) {
    snprintf(buf, sizeof buf, "%.17g", v);
  }
  fputs(buf, out);
}

/* "  # [unit] description extra" up to the end of the line. */
static void put_comment(FILE *out, const mvar_t *v, const char *extra)
{
  if (v->unit[0] != '\0' || v->description[0] != '\0' || extra[0] != '\0') {
    fputs("  #", out);
    if (v->unit[0] != '\0') {
      fprintf(out, " [%s]", v->unit);
    }
    if (v->description[0] != '\0') {
      fputc(' ', out);
      for (const char *p = v->description; *p != '\0'; p++) {
        fputc((unsigned char)*p < 0x20 ? ' ' : *p, out);
      }
    }
    fputs(extra, out);
  }
  fputc('\n', out);
}

/* The first `max` Float64 variables of one causality; *skipped counts the others of it. */
static size_t collect(const modeldesc_t *md, causality_t c, const mvar_t **list, size_t max,
                      size_t *skipped)
{
  size_t n = 0;
  *skipped = 0;
  for (size_t i = 0; i < md->n_vars; i++) {
    const mvar_t *v = &md->vars[i];
    if (v->causality != c) {
      continue;
    }
    if (mvar_is_float64(v) && n < max) {
      list[n++] = v;
    } else {
      (*skipped)++;
    }
  }
  return n;
}

/* One comment line naming the variables of causality c that are not in list. */
static void put_skipped(FILE *out, const modeldesc_t *md, causality_t c, const mvar_t *const *list,
                        size_t n, const char *what, int max)
{
  fprintf(out, "# %s left out (only Float64, at most %d):", what, max);
  for (size_t i = 0; i < md->n_vars; i++) {
    const mvar_t *v = &md->vars[i];
    bool listed = false;
    for (size_t j = 0; j < n && !listed; j++) {
      listed = list[j] == v;
    }
    if (v->causality == c && !listed) {
      fprintf(out, " %s (%s)", v->name, v->type);
    }
  }
  fputc('\n', out);
}

static void put_names(FILE *out, const char *key, const mvar_t *const *list, size_t n)
{
  if (n == 0) {
    fprintf(out, "%s = []\n", key);
    return;
  }
  fprintf(out, "%s = [\n", key);
  for (size_t i = 0; i < n; i++) {
    fputs("  ", out);
    put_str(out, list[i]->name);
    fputc(',', out);
    put_comment(out, list[i], "");
  }
  fputs("]\n", out);
}

int scaffold_scenario(FILE *out, const char *fmu_path, char *err, size_t errlen)
{
  fmu_pkg_t pkg;
  if (fmu_open(fmu_path, &pkg, err, errlen) < 0) {
    return -1;
  }
  const modeldesc_t *md = &pkg.md;

  const mvar_t *params[CFG_MAX_PARAMS], *ins[TELEM_MAX_CH], *outs[TELEM_MAX_CH];
  size_t skip_p, skip_i, skip_o;
  size_t np = collect(md, CAUS_PARAMETER, params, CFG_MAX_PARAMS, &skip_p);
  size_t ni = collect(md, CAUS_INPUT, ins, TELEM_MAX_CH, &skip_i);
  size_t no = collect(md, CAUS_OUTPUT, outs, TELEM_MAX_CH, &skip_o);

  /* duration: a whole number of steps, at least one */
  double step = md->step_size > 0 ? md->step_size : DEFAULT_STEP;
  double stop = md->stop_time > 0 ? md->stop_time : DEFAULT_STOP;
  double n_steps = fmax(1.0, round(stop / step));

  fprintf(out,
          "# Scenario for %s (FMI %s), from `krumpir init`. It runs as written; edit the\n"
          "# parameters, the stimulus and the duration. All keys: docs/scenario-reference.md\n",
          md->model_identifier, md->fmi_version);
  fputs("[model]\nfmu = ", out);
  put_str(out, fmu_path);
  fputs("\nstep = ", out);
  put_num(out, step);
  fputs(md->step_size > 0 ? "  # DefaultExperiment stepSize\n" : "\n", out);
  fputs("duration = ", out);
  put_num(out, n_steps * step);
  fputs(md->stop_time > 0 ? "  # DefaultExperiment stopTime\n" : "\n", out);

  bool header = false;
  for (size_t i = 0; i < np; i++) {
    double v;
    if (!number(params[i]->start, &v)) {
      continue; /* no usable start value: the FMU keeps its own */
    }
    if (!header) {
      fputs("\n[model.params]  # start values from the FMU\n", out);
      header = true;
    }
    put_key(out, params[i]->name);
    fputs(" = ", out);
    put_num(out, v);
    put_comment(out, params[i], params[i]->tunable ? " (tunable)" : "");
  }
  if (skip_p > 0) {
    put_skipped(out, md, CAUS_PARAMETER, params, np, "parameters", CFG_MAX_PARAMS);
  }

  fputs("\n[rt]\nwait = \"none\"  # as fast as possible; \"sleep\" or \"busy\" for real time\n",
        out);

  fputs("\n[io]\nbackend = \"null\"\n", out);
  put_names(out, "inputs", ins, ni);
  if (skip_i > 0) {
    put_skipped(out, md, CAUS_INPUT, ins, ni, "inputs", TELEM_MAX_CH);
  }
  put_names(out, "outputs", outs, no);
  if (skip_o > 0) {
    put_skipped(out, md, CAUS_OUTPUT, outs, no, "outputs", TELEM_MAX_CH);
  }
  if (ni > 0) {
    /* The null backend feeds 0, so inputs with another start value are held at it. */
    fputs("stimulus = [\n", out);
    for (size_t i = 0; i < ni; i++) {
      double v;
      if (number(ins[i]->start, &v) && v != 0) {
        fputs("  { input = ", out);
        put_str(out, ins[i]->name);
        fputs(", kind = \"constant\", value = ", out);
        put_num(out, v);
        fputs(" },  # start value\n", out);
      }
    }
    fputs("  # { input = ", out);
    put_str(out, ins[0]->name);
    fputs(", kind = \"step\", t = 0.1, from = 0, to = 1 },\n]\n", out);
  }

  char log[CFG_MAX_PATH];
  snprintf(log, sizeof log, "runs/%s.csv", md->model_identifier);
  fputs("\n[log]\nfile = ", out);
  put_str(out, log);
  fputc('\n', out);

  fmu_close(&pkg);
  return 0;
}
