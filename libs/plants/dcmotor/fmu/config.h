#ifndef config_h
#define config_h

/* DC motor plant (libs/plants/dcmotor) as an FMI co-simulation FMU, Reference FMUs framework. */

#include <stdbool.h>

#include "plant_sim.h"

#define MODEL_IDENTIFIER dcmotor
#define INSTANTIATION_TOKEN "{6b8f1a52-3c1e-4d7a-9f0b-2a5e7c4d1e01}"

#define CO_SIMULATION

#define GET_FLOAT64
#define SET_FLOAT64
#define EVENT_UPDATE

/* The plant is discretized exactly for this step: one plant step per solver step (1 ms). */
#define FIXED_SOLVER_STEP 0.001
#define DEFAULT_STOP_TIME 1

/* Value references, as in FMI3.xml and FMI2.xml */
typedef enum {
  vr_time,
  vr_duty,
  vr_load_torque,
  vr_speed,
  vr_speed_rpm,
  vr_current,
  vr_R,
  vr_L,
  vr_Kt,
  vr_Ke,
  vr_J,
  vr_b,
  vr_Vbus,
  vr_fault
} ValueReference;

typedef struct {
  double duty;        /* input, -1..1 (clamped) */
  double load_torque; /* input [N*m] */
  double fault;       /* input: 0 none, 1 encoder lost (speed_rpm reads NaN) */
  double R, L, Kt, Ke, J, b, Vbus; /* parameters, see plant_params_t */
  bool params_changed;             /* coefficients need recomputing */
  plant_sim_t sim;
} ModelData;

#endif /* config_h */
