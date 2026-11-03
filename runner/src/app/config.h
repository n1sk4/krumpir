#ifndef KRUMPIR_CONFIG_H
#define KRUMPIR_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring.h" /* TELEM_MAX_CH */

#define CFG_MAX_NAME 64
#define CFG_MAX_PATH 256
#define CFG_MAX_PARAMS 16

typedef enum { WAIT_NONE, WAIT_SLEEP, WAIT_BUSY } wait_mode_t;
#define CFG_MAX_SEGMENTS 8

typedef enum { STIM_CONSTANT, STIM_STEP, STIM_SINE, STIM_SEGMENTS } stim_kind_t;

/* One segment of a signal (the segment types of an ASAM XIL signal description). u is the time
 * since the start of the segment, d its duration. */
typedef enum {
  SEG_CONST, /* value */
  SEG_RAMP,  /* from + (to - from) * u / d; `from` defaults to the end of the previous segment */
  SEG_SINE,  /* offset + amplitude * sin(2 pi freq_hz u + phase) */
  SEG_PULSE, /* high while fmod(u, period) < duty * period, otherwise low */
  SEG_IDLE,  /* holds the end value of the previous segment (0 for the first) */
  SEG_DATA   /* CSV file of time,value rows (time from 0), linear or step interpolation */
} seg_kind_t;

typedef struct {
  seg_kind_t kind;
  double duration; /* s; INFINITY: to the end of the run (last segment only); NAN: data file length */
  double value;
  double from, to;
  bool has_from;
  double offset, amplitude, freq_hz, phase;
  double low, high, period, duty;
  char file[CFG_MAX_PATH];
  bool step_interp; /* data: hold each value until the next row instead of interpolating */
} segment_t;

/* Value of one model input over time. The short kinds (constant, step, sine) are single signals;
 * STIM_SEGMENTS plays segs[0..n_segs) in order, `loops` times (0 = forever). */
typedef struct {
  char input[CFG_MAX_NAME];
  stim_kind_t kind;
  double value;                      /* constant */
  double t, from, to;                /* step: `from` before time t, `to` from t on */
  double offset, amplitude, freq_hz; /* sine: offset + amplitude * sin(2 pi freq t) */
  segment_t segs[CFG_MAX_SEGMENTS];
  size_t n_segs;
  int loops;
} stimulus_t;

typedef struct {
  char name[CFG_MAX_NAME];
  double value;
} param_t;

typedef struct {
  /* [model] */
  char fmu[CFG_MAX_PATH];
  double step;      /* s */
  double duration;  /* s; INFINITY when absent: the run goes on until it is stopped */
  uint64_t n_steps; /* duration / step, validated to be a whole number; UINT64_MAX: no end */
  param_t params[CFG_MAX_PARAMS];
  size_t n_params;

  /* [rt]; cpu pins the loop thread, priority makes it SCHED_FIFO and locks memory (-1 = unset) */
  wait_mode_t wait;
  int max_overruns;
  int cpu;
  int priority;

  /* [io] */
  char backend[CFG_MAX_NAME];
  char inputs[TELEM_MAX_CH][CFG_MAX_NAME];
  size_t n_inputs;
  char outputs[TELEM_MAX_CH][CFG_MAX_NAME];
  size_t n_outputs;
  stimulus_t stimuli[TELEM_MAX_CH];
  size_t n_stimuli;
  /* backend "uart": the serial device, its baud rate, how long a step waits for the controller's
   * answer (0: never, take its newest; for a free-running controller), and what too many late
   * answers in a row do (fault the run, or hold the last values and count them) */
  char device[CFG_MAX_PATH];
  int baud;
  double timeout_ms;
  bool late_fault;
  int max_late;

  /* [log] */
  char log_file[CFG_MAX_PATH];
} scenario_t;

/* Parse and validate scenario text. Returns 0 on success. On failure returns -1 and writes a
 * one-line message to err. Unknown keys are errors. */
int config_parse(const char *text, scenario_t *sc, char *err, size_t errlen);

/* Parses one stimulus written as a TOML inline table (the form of an io.stimulus entry) for an
 * input of `sc`. Returns 0, or -1 with a message in err. */
int config_parse_stimulus(const char *entry, const scenario_t *sc, stimulus_t *s, char *err,
                          size_t errlen);

/* Reads the file and calls config_parse. Relative paths in the file are kept as written. */
int config_load(const char *path, scenario_t *sc, char *err, size_t errlen);

#endif
