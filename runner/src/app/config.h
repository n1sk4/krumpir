#ifndef KRUMPIR_CONFIG_H
#define KRUMPIR_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "ring.h" /* TELEM_MAX_CH */

#define CFG_MAX_NAME 64
#define CFG_MAX_PATH 256
#define CFG_MAX_PARAMS 16

typedef enum { WAIT_NONE, WAIT_SLEEP, WAIT_BUSY } wait_mode_t;
typedef enum { STIM_CONSTANT, STIM_STEP, STIM_SINE } stim_kind_t;

/* Value of one model input over time, used by the null I/O backend. */
typedef struct {
  char input[CFG_MAX_NAME];
  stim_kind_t kind;
  double value;                      /* constant */
  double t, from, to;                /* step: `from` before time t, `to` from t on */
  double offset, amplitude, freq_hz; /* sine: offset + amplitude * sin(2 pi freq t) */
} stimulus_t;

typedef struct {
  char name[CFG_MAX_NAME];
  double value;
} param_t;

typedef struct {
  /* [model] */
  char fmu[CFG_MAX_PATH];
  double step;      /* s */
  double duration;  /* s */
  uint64_t n_steps; /* duration / step, validated to be a whole number */
  param_t params[CFG_MAX_PARAMS];
  size_t n_params;

  /* [rt]; cpu and priority are parsed but not applied yet (-1 = unset) */
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

  /* [log] */
  char log_file[CFG_MAX_PATH];
} scenario_t;

/* Parse and validate scenario text. Returns 0 on success. On failure returns -1 and writes a
 * one-line message to err. Unknown keys are errors. */
int config_parse(const char *text, scenario_t *sc, char *err, size_t errlen);

/* Reads the file and calls config_parse. Relative paths in the file are kept as written. */
int config_load(const char *path, scenario_t *sc, char *err, size_t errlen);

#endif
