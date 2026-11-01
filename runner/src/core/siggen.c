#include "siggen.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.283185307179586476925286766559

typedef struct {
  segment_t s;     /* ramp: s.from is resolved */
  double start;    /* s, within one loop */
  double dur;      /* s, resolved (INFINITY only for the last segment) */
  double v0, v1;   /* end value of the previous segment (0 for the first), and of this one */
  double *dt, *dv; /* data rows */
  size_t dn;
} seg_t;

struct signal {
  seg_t seg[CFG_MAX_SEGMENTS];
  size_t n;
  double total; /* length of one loop */
  int loops;    /* 0 = forever */
};

static double data_value(const seg_t *g, double u)
{
  if (u <= g->dt[0]) {
    return g->dv[0];
  }
  size_t lo = 0, hi = g->dn - 1; /* binary search for the rows around u */
  if (u >= g->dt[hi]) {
    return g->dv[hi];
  }
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if (g->dt[mid] <= u) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  if (g->s.step_interp) {
    return g->dv[lo];
  }
  return g->dv[lo] + (g->dv[hi] - g->dv[lo]) * (u - g->dt[lo]) / (g->dt[hi] - g->dt[lo]);
}

static double seg_value(const seg_t *g, double u)
{
  const segment_t *s = &g->s;
  switch (s->kind) {
  case SEG_CONST:
    return s->value;
  case SEG_RAMP:
    return s->from + (s->to - s->from) * u / g->dur;
  case SEG_SINE:
    return s->offset + s->amplitude * sin(TWO_PI * s->freq_hz * u + s->phase);
  case SEG_PULSE:
    return fmod(u, s->period) < s->duty * s->period ? s->high : s->low;
  case SEG_IDLE:
    return g->v0;
  case SEG_DATA:
    return data_value(g, u);
  }
  return 0.0;
}

double signal_value(const signal_t *g, double t)
{
  const seg_t *last = &g->seg[g->n - 1];
  if (g->loops != 1) {
    if (g->loops > 0 && t >= g->total * g->loops) {
      return last->v1;
    }
    t = fmod(t, g->total);
  } else if (t >= g->total) {
    return last->v1;
  }
  for (size_t i = 0; i + 1 < g->n; i++) {
    if (t < g->seg[i].start + g->seg[i].dur) {
      return seg_value(&g->seg[i], t - g->seg[i].start);
    }
  }
  return seg_value(last, t - last->start);
}

/* Reads time,value rows; blank lines, # comments and a header line are skipped. Times must rise. */
static int read_data(seg_t *g, char *err, size_t errlen)
{
  const char *path = g->s.file;
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    snprintf(err, errlen, "cannot open data file '%s': %s", path, strerror(errno));
    return -1;
  }
  char line[256];
  size_t cap = 0;
  int rc = 0;
  for (int ln = 1; fgets(line, sizeof line, f) != NULL; ln++) {
    char *p = line + strspn(line, " \t");
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') {
      continue;
    }
    char *end, *vend;
    double t = strtod(p, &end);
    bool ok = end != p;
    end += strspn(end, " \t");
    ok = ok && *end == ',';
    double v = ok ? strtod(end + 1, &vend) : 0.0;
    ok = ok && vend != end + 1 && isfinite(t) && isfinite(v);
    if (!ok) {
      if (g->dn == 0 && ln == 1) {
        continue; /* header */
      }
      snprintf(err, errlen, "%s:%d: expected 'time,value'", path, ln);
      rc = -1;
      break;
    }
    if (t < 0 || (g->dn > 0 && t <= g->dt[g->dn - 1])) {
      snprintf(err, errlen, "%s:%d: times must start at >= 0 and rise", path, ln);
      rc = -1;
      break;
    }
    if (g->dn == cap) {
      cap = cap ? cap * 2 : 256;
      double *dt = realloc(g->dt, cap * sizeof *dt);
      if (dt != NULL) {
        g->dt = dt;
      }
      double *dv = dt != NULL ? realloc(g->dv, cap * sizeof *dv) : NULL;
      if (dv == NULL) {
        snprintf(err, errlen, "out of memory reading '%s'", path);
        rc = -1;
        break;
      }
      g->dv = dv;
    }
    g->dt[g->dn] = t;
    g->dv[g->dn++] = v;
  }
  fclose(f);
  if (rc == 0 && g->dn == 0) {
    snprintf(err, errlen, "data file '%s' has no rows", path);
    rc = -1;
  }
  return rc;
}

/* The short kinds as segments; the values match their old formulas exactly. */
static size_t to_segments(const stimulus_t *s, segment_t *out, int *loops)
{
  *loops = 1;
  switch (s->kind) {
  case STIM_CONSTANT:
    out[0] = (segment_t){.kind = SEG_CONST, .duration = INFINITY, .value = s->value};
    return 1;
  case STIM_STEP:
    if (s->t <= 0) {
      out[0] = (segment_t){.kind = SEG_CONST, .duration = INFINITY, .value = s->to};
      return 1;
    }
    out[0] = (segment_t){.kind = SEG_CONST, .duration = s->t, .value = s->from};
    out[1] = (segment_t){.kind = SEG_CONST, .duration = INFINITY, .value = s->to};
    return 2;
  case STIM_SINE:
    out[0] = (segment_t){.kind = SEG_SINE,
                         .duration = INFINITY,
                         .offset = s->offset,
                         .amplitude = s->amplitude,
                         .freq_hz = s->freq_hz};
    return 1;
  case STIM_SEGMENTS:
    break;
  }
  size_t n = s->n_segs < CFG_MAX_SEGMENTS ? s->n_segs : CFG_MAX_SEGMENTS;
  memcpy(out, s->segs, n * sizeof *out);
  *loops = s->loops;
  return n;
}

static signal_t *fail(signal_t *g) /* frees g, returns NULL */
{
  signal_free(g);
  return NULL;
}

signal_t *signal_create(const stimulus_t *s, char *err, size_t errlen)
{
  signal_t *g = calloc(1, sizeof *g);
  if (g == NULL) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  segment_t segs[CFG_MAX_SEGMENTS];
  g->n = to_segments(s, segs, &g->loops);
  if (g->n == 0) {
    snprintf(err, errlen, "signal for '%s' has no segments", s->input);
    return fail(g);
  }
  double t = 0.0, prev = 0.0;
  for (size_t i = 0; i < g->n; i++) {
    seg_t *q = &g->seg[i];
    q->s = segs[i];
    q->start = t;
    q->dur = q->s.duration;
    q->v0 = prev;
    if (q->s.kind == SEG_RAMP && !q->s.has_from) {
      q->s.from = prev;
    }
    if (q->s.kind == SEG_DATA) {
      if (read_data(q, err, errlen) < 0) {
        return fail(g);
      }
      if (isnan(q->dur)) {
        q->dur = q->dt[q->dn - 1];
      }
    }
    if (!(q->dur > 0) || (isinf(q->dur) && i + 1 < g->n) ||
        (q->s.kind == SEG_RAMP && isinf(q->dur))) {
      snprintf(err, errlen, "signal for '%s': segment %zu needs a finite duration > 0", s->input,
               i + 1);
      return fail(g);
    }
    q->v1 = q->s.kind == SEG_RAMP ? q->s.to : isinf(q->dur) ? NAN : seg_value(q, q->dur);
    prev = q->v1;
    t += q->dur;
  }
  g->total = t;
  if (g->loops != 1 && isinf(g->total)) {
    snprintf(err, errlen, "signal for '%s': loops needs a duration on every segment", s->input);
    return fail(g);
  }
  return g;
}

void signal_free(signal_t *g)
{
  if (g == NULL) {
    return;
  }
  for (size_t i = 0; i < CFG_MAX_SEGMENTS; i++) {
    free(g->seg[i].dt);
    free(g->seg[i].dv);
  }
  free(g);
}
