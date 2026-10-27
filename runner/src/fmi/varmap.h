#ifndef KRUMPIR_VARMAP_H
#define KRUMPIR_VARMAP_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "modeldesc.h"

/* Scenario names resolved to FMI value references, in scenario order. */
typedef struct {
  uint32_t in_vr[TELEM_MAX_CH];
  size_t n_in;
  uint32_t out_vr[TELEM_MAX_CH];
  size_t n_out;
  uint32_t param_vr[CFG_MAX_PARAMS];
  double param_value[CFG_MAX_PARAMS];
  size_t n_params;
} varmap_t;

/* Checks that every input, output and parameter named in the scenario exists in the model, is
 * Float64 and has the matching causality (input, output, parameter). Returns 0 on success; on
 * failure returns -1 and writes a one-line message to err. */
int varmap_build(const modeldesc_t *md, const scenario_t *sc, varmap_t *vm, char *err,
                 size_t errlen);

#endif
