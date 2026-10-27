#include "varmap.h"

#include <stdio.h>

static int resolve(const modeldesc_t *md, const char *role, const char *name, causality_t want,
                   uint32_t *vr, char *err, size_t errlen) {
  const mvar_t *v = modeldesc_find(md, name);
  if (v == NULL) {
    snprintf(err, errlen, "%s '%s' is not a variable of the model", role, name);
    return -1;
  }
  if (!mvar_is_float64(v)) {
    snprintf(err, errlen, "%s '%s' is %s, only Float64 is supported", role, name, v->type);
    return -1;
  }
  if (v->causality != want) {
    snprintf(err, errlen, "%s '%s' has causality '%s' in the model", role, name,
             v->causality_str);
    return -1;
  }
  *vr = v->vr;
  return 0;
}

int varmap_build(const modeldesc_t *md, const scenario_t *sc, varmap_t *vm, char *err,
                 size_t errlen) {
  *vm = (varmap_t){0};
  for (size_t i = 0; i < sc->n_inputs; i++) {
    if (resolve(md, "input", sc->inputs[i], CAUS_INPUT, &vm->in_vr[i], err, errlen) < 0) {
      return -1;
    }
  }
  vm->n_in = sc->n_inputs;
  for (size_t i = 0; i < sc->n_outputs; i++) {
    if (resolve(md, "output", sc->outputs[i], CAUS_OUTPUT, &vm->out_vr[i], err, errlen) < 0) {
      return -1;
    }
  }
  vm->n_out = sc->n_outputs;
  for (size_t i = 0; i < sc->n_params; i++) {
    if (resolve(md, "parameter", sc->params[i].name, CAUS_PARAMETER, &vm->param_vr[i], err,
                errlen) < 0) {
      return -1;
    }
    vm->param_value[i] = sc->params[i].value;
  }
  vm->n_params = sc->n_params;
  return 0;
}
