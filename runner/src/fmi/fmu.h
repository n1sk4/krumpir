#ifndef KRUMPIR_FMU_H
#define KRUMPIR_FMU_H

#include <stddef.h>
#include <stdint.h>

#include "loader.h"

/* Result of one FMU call. Kept as plain codes so the real-time loop can act on them without
 * formatting text; use fmu_result_str() and fmu_last_message() outside the loop. */
typedef enum {
  FMU_OK = 0,
  FMU_ERROR,
  FMU_FATAL,
  FMU_DISCARD,
  FMU_EVENT,            /* the FMU asked for event handling, which is not supported */
  FMU_EARLY_RETURN,     /* the FMU returned before the end of the step */
  FMU_TERMINATE_REQUEST /* the FMU asked the simulation to terminate */
} fmu_result_t;

typedef struct fmu fmu_t;

/* One implementation per FMI version. The instance is created by that version's create function,
 * so there is no separate instantiate entry here. */
typedef struct fmu_api {
  fmu_result_t (*init)(fmu_t *m, double t0);
  fmu_result_t (*set_real)(fmu_t *m, const uint32_t *vr, size_t n, const double *v);
  fmu_result_t (*get_real)(fmu_t *m, const uint32_t *vr, size_t n, double *v);
  fmu_result_t (*step)(fmu_t *m, double t, double h);
  const char *(*last_message)(const fmu_t *m);
  void (*destroy)(fmu_t *m); /* terminate if needed, free the instance and the object */
} fmu_api_t;

struct fmu {
  const fmu_api_t *api;
};

/* FMI 3.0 co-simulation backend. Binds the functions exported by pkg's library and instantiates
 * the model. `pkg` must stay open until fmu_destroy(). Returns NULL and writes a message to err on
 * failure. Parameters can be set with fmu_set_real() right after creation, before fmu_init(). */
fmu_t *fmi3_create(const fmu_pkg_t *pkg, char *err, size_t errlen);

static inline fmu_result_t fmu_init(fmu_t *m, double t0) { return m->api->init(m, t0); }
static inline fmu_result_t fmu_set_real(fmu_t *m, const uint32_t *vr, size_t n, const double *v) {
  return m->api->set_real(m, vr, n, v);
}
static inline fmu_result_t fmu_get_real(fmu_t *m, const uint32_t *vr, size_t n, double *v) {
  return m->api->get_real(m, vr, n, v);
}
static inline fmu_result_t fmu_step(fmu_t *m, double t, double h) { return m->api->step(m, t, h); }
static inline const char *fmu_last_message(const fmu_t *m) { return m->api->last_message(m); }
static inline void fmu_destroy(fmu_t *m) { m->api->destroy(m); }

const char *fmu_result_str(fmu_result_t r);

#endif
