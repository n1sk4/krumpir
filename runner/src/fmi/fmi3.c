#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fmi3FunctionTypes.h"
#include "fmu.h"

typedef struct {
  fmu_t base; /* must be first */
  fmi3Instance inst;
  bool in_step_mode; /* ExitInitializationMode done and not yet terminated */

  fmi3FreeInstanceTYPE *free_instance;
  fmi3EnterInitializationModeTYPE *enter_init;
  fmi3ExitInitializationModeTYPE *exit_init;
  fmi3TerminateTYPE *terminate;
  fmi3SetFloat64TYPE *set_float64;
  fmi3GetFloat64TYPE *get_float64;
  fmi3DoStepTYPE *do_step;

  char msg[256]; /* last warning or error reported by the FMU */
} fmi3_t;

const char *fmu_result_str(fmu_result_t r)
{
  switch (r) {
  case FMU_OK:
    return "ok";
  case FMU_ERROR:
    return "FMU returned an error";
  case FMU_FATAL:
    return "FMU returned a fatal error";
  case FMU_DISCARD:
    return "FMU discarded the step";
  case FMU_EVENT:
    return "FMU requested event handling (not supported)";
  case FMU_EARLY_RETURN:
    return "FMU returned early from the step (not supported)";
  case FMU_TERMINATE_REQUEST:
    return "FMU requested termination";
  }
  return "?";
}

static fmu_result_t map_status(fmi3Status s)
{
  switch (s) {
  case fmi3OK:
  case fmi3Warning:
    return FMU_OK;
  case fmi3Discard:
    return FMU_DISCARD;
  case fmi3Error:
    return FMU_ERROR;
  case fmi3Fatal:
    return FMU_FATAL;
  }
  return FMU_FATAL;
}

/* The FMU may call this from the step loop; it only copies text, no I/O. */
static void log_message(fmi3InstanceEnvironment env, fmi3Status status, fmi3String category,
                        fmi3String message)
{
  fmi3_t *f = env;
  if (status >= fmi3Warning) {
    snprintf(f->msg, sizeof f->msg, "%s: %s", category != NULL ? category : "fmu",
             message != NULL ? message : "");
  }
}

static fmu_result_t f_init(fmu_t *m, double t0)
{
  fmi3_t *f = (fmi3_t *)m;
  fmu_result_t r = map_status(f->enter_init(f->inst, fmi3False, 0.0, t0, fmi3False, 0.0));
  if (r != FMU_OK) {
    return r;
  }
  r = map_status(f->exit_init(f->inst));
  f->in_step_mode = r == FMU_OK;
  return r;
}

static fmu_result_t f_set_real(fmu_t *m, const uint32_t *vr, size_t n, const double *v)
{
  fmi3_t *f = (fmi3_t *)m;
  return map_status(f->set_float64(f->inst, vr, n, v, n));
}

static fmu_result_t f_get_real(fmu_t *m, const uint32_t *vr, size_t n, double *v)
{
  fmi3_t *f = (fmi3_t *)m;
  return map_status(f->get_float64(f->inst, vr, n, v, n));
}

static fmu_result_t f_step(fmu_t *m, double t, double h)
{
  fmi3_t *f = (fmi3_t *)m;
  fmi3Boolean event = fmi3False, terminate = fmi3False, early = fmi3False;
  fmi3Float64 last = 0.0;
  fmu_result_t r =
      map_status(f->do_step(f->inst, t, h, fmi3True, &event, &terminate, &early, &last));
  if (r != FMU_OK) {
    return r;
  }
  if (event) {
    return FMU_EVENT;
  }
  if (terminate) {
    return FMU_TERMINATE_REQUEST;
  }
  return early ? FMU_EARLY_RETURN : FMU_OK;
}

static const char *f_last_message(const fmu_t *m) { return ((const fmi3_t *)m)->msg; }

static void f_destroy(fmu_t *m)
{
  fmi3_t *f = (fmi3_t *)m;
  if (f->inst != NULL) {
    if (f->in_step_mode) {
      f->terminate(f->inst);
    }
    f->free_instance(f->inst);
  }
  free(f);
}

static const fmu_api_t api = {f_init, f_set_real, f_get_real, f_step, f_last_message, f_destroy};

#define BIND(field, sym)                                                                           \
  do {                                                                                             \
    f->field = (void *)fmu_sym(pkg, sym);                                                          \
    if (f->field == NULL) {                                                                        \
      snprintf(err, errlen, "FMU does not export %s", sym);                                        \
      free(f);                                                                                     \
      return NULL;                                                                                 \
    }                                                                                              \
  } while (0)

fmu_t *fmi3_create(const fmu_pkg_t *pkg, char *err, size_t errlen)
{
  if (pkg->md.instantiation_token[0] == '\0') {
    snprintf(err, errlen, "modelDescription.xml has no instantiationToken");
    return NULL;
  }
  fmi3_t *f = calloc(1, sizeof *f);
  if (f == NULL) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  f->base.api = &api;

  fmi3InstantiateCoSimulationTYPE *instantiate =
      (void *)fmu_sym(pkg, "fmi3InstantiateCoSimulation");
  if (instantiate == NULL) {
    snprintf(err, errlen, "FMU does not export fmi3InstantiateCoSimulation");
    free(f);
    return NULL;
  }
  BIND(free_instance, "fmi3FreeInstance");
  BIND(enter_init, "fmi3EnterInitializationMode");
  BIND(exit_init, "fmi3ExitInitializationMode");
  BIND(terminate, "fmi3Terminate");
  BIND(set_float64, "fmi3SetFloat64");
  BIND(get_float64, "fmi3GetFloat64");
  BIND(do_step, "fmi3DoStep");

  char resources[600];
  snprintf(resources, sizeof resources, "%s/resources/", pkg->dir);
  f->inst = instantiate("krumpir", pkg->md.instantiation_token, resources, fmi3False, fmi3False,
                        fmi3False, fmi3False, NULL, 0, f, log_message, NULL);
  if (f->inst == NULL) {
    snprintf(err, errlen, "fmi3InstantiateCoSimulation failed%s%s", f->msg[0] ? ": " : "", f->msg);
    free(f);
    return NULL;
  }
  return &f->base;
}
