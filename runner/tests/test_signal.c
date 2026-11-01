/* Signals: the short kinds (bit-identical to their formulas), segments, loops, data files. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "siggen.h"

#define TWO_PI 6.283185307179586476925286766559
#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

/* The stimulus of a one-input scenario whose io.stimulus entry is `entry`. */
static stimulus_t stim(const char *entry)
{
  char text[2048], err[256];
  scenario_t sc;
  snprintf(text, sizeof text,
           "[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\n"
           "[io]\nbackend = \"null\"\ninputs = [\"u\"]\nstimulus = [{ input = \"u\", %s }]\n"
           "[log]\nfile = \"o.csv\"\n",
           entry);
  int rc = config_parse(text, &sc, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "config: %s\n", err);
  }
  assert(rc == 0 && sc.n_stimuli == 1);
  return sc.stimuli[0];
}

static signal_t *make(const char *entry)
{
  char err[256];
  stimulus_t s = stim(entry);
  signal_t *g = signal_create(&s, err, sizeof err);
  if (g == NULL) {
    fprintf(stderr, "signal: %s\n", err);
  }
  assert(g != NULL);
  return g;
}

static void test_short_kinds(void)
{
  signal_t *g = make("kind = \"constant\", value = 3");
  assert(signal_value(g, 0.0) == 3.0 && signal_value(g, 100.0) == 3.0);
  signal_free(g);

  g = make("kind = \"step\", t = 1.0, from = -1, to = 4");
  assert(signal_value(g, 0.0) == -1.0);
  assert(signal_value(g, 0.999999) == -1.0);
  assert(signal_value(g, 1.0) == 4.0); /* the new value applies from t on */
  assert(signal_value(g, 5.0) == 4.0);
  signal_free(g);

  g = make("kind = \"step\", t = 0, from = -1, to = 4");
  assert(signal_value(g, 0.0) == 4.0);
  signal_free(g);

  g = make("kind = \"sine\", offset = 1, amplitude = 2, freq_hz = 0.25");
  for (double t = 0.0; t < 5.0; t += 0.0137) { /* exactly the old formula */
    assert(signal_value(g, t) == 1.0 + 2.0 * sin(TWO_PI * 0.25 * t));
  }
  assert(NEAR(signal_value(g, 1.0), 3.0)); /* quarter period: sin = 1 */
  signal_free(g);
}

static void test_segments(void)
{
  signal_t *g = make("segments = [\n"
                     "  { kind = \"const\", value = 2, duration = 1 },\n"
                     "  { kind = \"ramp\", to = 6, duration = 2 },\n" /* from 2 (previous end) */
                     "  { kind = \"idle\", duration = 1 },\n"         /* holds 6 */
                     "  { kind = \"pulse\", high = 1, period = 0.5, duty = 0.2, duration = 1 },\n"
                     "  { kind = \"sine\", offset = 10, amplitude = 1, freq_hz = 1 },\n"
                     "]");
  assert(signal_value(g, 0.5) == 2.0);
  assert(NEAR(signal_value(g, 1.0), 2.0));
  assert(NEAR(signal_value(g, 2.0), 4.0));
  assert(NEAR(signal_value(g, 2.999), 5.998));
  assert(signal_value(g, 3.0) == 6.0 && signal_value(g, 3.9) == 6.0);
  assert(signal_value(g, 4.05) == 1.0 && signal_value(g, 4.2) == 0.0);
  assert(signal_value(g, 4.55) == 1.0 && signal_value(g, 4.8) == 0.0);
  assert(NEAR(signal_value(g, 5.25), 11.0)); /* the last segment runs to the end */
  signal_free(g);

  /* A finite signal holds its last value; a ramp with its own from */
  g = make("segments = [{ kind = \"ramp\", from = -1, to = 1, duration = 2 }]");
  assert(signal_value(g, 0.0) == -1.0 && NEAR(signal_value(g, 1.0), 0.0));
  assert(signal_value(g, 2.0) == 1.0 && signal_value(g, 50.0) == 1.0);
  signal_free(g);
}

static void test_loops(void)
{
  const char *segs = "segments = [{ kind = \"const\", value = 1, duration = 1 },"
                     " { kind = \"const\", value = 2, duration = 1 }]";
  char entry[256];
  snprintf(entry, sizeof entry, "%s, loops = 3", segs);
  signal_t *g = make(entry);
  assert(signal_value(g, 0.5) == 1.0 && signal_value(g, 1.5) == 2.0);
  assert(signal_value(g, 4.5) == 1.0 && signal_value(g, 5.5) == 2.0);
  assert(signal_value(g, 6.0) == 2.0 && signal_value(g, 99.0) == 2.0); /* after 3 plays */
  signal_free(g);

  snprintf(entry, sizeof entry, "%s, loops = 0", segs);
  g = make(entry);
  assert(signal_value(g, 1000.5) == 1.0 && signal_value(g, 1001.5) == 2.0); /* forever */
  signal_free(g);
}

static void test_data(void)
{
  char path[128], entry[512], err[256];
  snprintf(path, sizeof path, "/tmp/krumpir_test_signal_%d.csv", (int)getpid());
  FILE *f = fopen(path, "w");
  assert(f != NULL);
  fputs("time,value\n# comment\n0,0\n1,10\n\n3,30\n", f);
  fclose(f);

  snprintf(entry, sizeof entry, "segments = [{ kind = \"data\", file = \"%s\" }]", path);
  signal_t *g = make(entry);
  assert(signal_value(g, 0.0) == 0.0 && NEAR(signal_value(g, 0.5), 5.0));
  assert(NEAR(signal_value(g, 2.0), 20.0));
  assert(signal_value(g, 3.0) == 30.0 && signal_value(g, 9.0) == 30.0); /* holds the last row */
  signal_free(g);

  /* Step interpolation, then a const after the file's length (3 s) */
  snprintf(entry, sizeof entry,
           "segments = [{ kind = \"data\", file = \"%s\", interpolation = \"step\" },"
           " { kind = \"const\", value = -1 }]",
           path);
  g = make(entry);
  assert(signal_value(g, 0.5) == 0.0 && signal_value(g, 2.9) == 10.0);
  assert(signal_value(g, 3.0) == -1.0);
  signal_free(g);

  /* Bad rows and a missing file are refused when the signal is built */
  f = fopen(path, "w");
  assert(f != NULL);
  fputs("0,0\n2,1\n1,2\n", f);
  fclose(f);
  snprintf(entry, sizeof entry, "segments = [{ kind = \"data\", file = \"%s\" }]", path);
  stimulus_t s = stim(entry);
  assert(signal_create(&s, err, sizeof err) == NULL && strstr(err, ":3: times must") != NULL);
  remove(path);
  assert(signal_create(&s, err, sizeof err) == NULL && strstr(err, "cannot open") != NULL);
}

int main(void)
{
  test_short_kinds();
  test_segments();
  test_loops();
  test_data();
  puts("test_signal: ok");
  return 0;
}
