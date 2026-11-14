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

typedef struct fmu fmu_t; /* an FMI 3.0 co-simulation instance (fmi3.c) */

/* Binds the functions exported by pkg's library and instantiates the model. `pkg` must stay open
 * until fmu_destroy(). Returns NULL and writes a message to err on failure. Parameters can be set
 * with fmu_set_real() right after creation, before fmu_init(). */
fmu_t *fmi3_create(const fmu_pkg_t *pkg, char *err, size_t errlen);

fmu_result_t fmu_init(fmu_t *m, double t0);
fmu_result_t fmu_set_real(fmu_t *m, const uint32_t *vr, size_t n, const double *v);
fmu_result_t fmu_get_real(fmu_t *m, const uint32_t *vr, size_t n, double *v);
fmu_result_t fmu_step(fmu_t *m, double t, double h);
const char *fmu_last_message(const fmu_t *m);
void fmu_destroy(fmu_t *m); /* terminate if needed, free the instance */

const char *fmu_result_str(fmu_result_t r);

#endif
