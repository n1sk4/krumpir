#ifndef T12_IO_SIM_H
#define T12_IO_SIM_H

#include <stdint.h>

#include "t12_io.h"
#include "t12_plant.h"

/* t12_io_t backend that talks to the simulated tip. Commands written in cycle k are applied by
 * t12_io_sim_step(); the next read sees the result. Q3 follows the heartbeat: it drops when no
 * toggle arrives for T12_HEARTBEAT_TIMEOUT_S. */
typedef struct {
  t12_plant_t plant;
  double ambient; /* [degC] */
  bool heater;
  uint32_t hb_age;     /* steps since the last heartbeat toggle */
  uint32_t hb_timeout; /* steps */
} t12_io_sim_t;

void t12_io_sim_init(t12_io_sim_t *s, t12_io_t *io_out, const t12_params_t *p, double dt,
                     double ambient);
void t12_io_sim_step(t12_io_sim_t *s); /* advance the plant by one control period */

#endif
