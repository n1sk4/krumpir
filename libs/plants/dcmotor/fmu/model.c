/* DC motor plant as an FMU: the Reference FMUs framework provides the FMI API, this file maps its
 * variables onto plant_sim. One plant step per solver step (FIXED_SOLVER_STEP, 1 ms), done in
 * eventUpdate on a time event every step: the inputs set before fmi3DoStep are held over the step,
 * and the outputs read after it are the state at its end. Same arithmetic as the firmware's
 * simulated motor (volts = duty * Vbus in float). */
#include <math.h>

#include "config.h"
#include "model.h"

#define RAD_S_TO_RPM (60.0 / (2.0 * 3.14159265358979323846))

static void reconfigure(ModelInstance *comp)
{
  const plant_params_t p = {(float)M(R),  (float)M(L), (float)M(Kt),  (float)M(Ke),
                            (float)M(J),  (float)M(b), (float)M(Vbus)};
  plant_sim_set_params(&M(sim), &p, (float)FIXED_SOLVER_STEP); /* keeps the state */
  M(params_changed) = false;
}

static float clamped_duty(double d) { return d > 1.0 ? 1.0f : d < -1.0 ? -1.0f : (float)d; }

Status setStartValues(ModelInstance *comp)
{
  /* PLANT_PARAMS_N20, written as the start values of FMI3.xml and FMI2.xml */
  M(R) = 2.5;
  M(L) = 1e-3;
  M(Kt) = 0.01;
  M(Ke) = 0.01;
  M(J) = 1e-6;
  M(b) = 1e-6;
  M(Vbus) = 6.0;
  M(duty) = 0.0;
  M(load_torque) = 0.0;
  M(fault) = 0.0;
  plant_sim_reset(&M(sim));
  M(params_changed) = true;

  comp->nextEventTime = FIXED_SOLVER_STEP;
  comp->nextEventTimeDefined = true;
  return OK;
}

Status calculateValues(ModelInstance *comp)
{
  if (M(params_changed))
    reconfigure(comp);
  return OK;
}

Status getFloat64(ModelInstance *comp, ValueReference vr, double values[], size_t nValues,
                  size_t *index)
{
  ASSERT_NVALUES(1);
  calculateValues(comp);
  double v;
  switch (vr) {
  case vr_time: v = comp->time; break;
  case vr_duty: v = M(duty); break;
  case vr_load_torque: v = M(load_torque); break;
  case vr_speed: v = M(sim).w; break;
  case vr_speed_rpm: v = M(fault) >= 0.5 ? NAN : M(sim).w * RAD_S_TO_RPM; break; /* encoder */
  case vr_current: v = M(sim).i; break;
  case vr_R: v = M(R); break;
  case vr_L: v = M(L); break;
  case vr_Kt: v = M(Kt); break;
  case vr_Ke: v = M(Ke); break;
  case vr_J: v = M(J); break;
  case vr_b: v = M(b); break;
  case vr_Vbus: v = M(Vbus); break;
  case vr_fault: v = M(fault); break;
  default:
    logError(comp, "Get Float64 is not allowed for value reference %u.", vr);
    return Error;
  }
  values[(*index)++] = v;
  return OK;
}

/* Parameters are fixed: only before the simulation starts, and only physically possible values. */
static Status set_parameter(ModelInstance *comp, double *dst, double v, bool positive,
                            const char *name)
{
  if (comp->state != Instantiated && comp->state != InitializationMode) {
    logError(comp, "%s can only be set before the simulation starts.", name);
    return Error;
  }
  if (!isfinite(v) || v < 0.0 || (positive && v == 0.0)) {
    logError(comp, "%s = %g is not a valid value.", name, v);
    return Error;
  }
  *dst = v;
  M(params_changed) = true;
  return OK;
}

Status setFloat64(ModelInstance *comp, ValueReference vr, const double values[], size_t nValues,
                  size_t *index)
{
  ASSERT_NVALUES(1);
  const double v = values[(*index)++];
  switch (vr) {
  case vr_duty:
  case vr_load_torque:
  case vr_fault:
    if (!isfinite(v)) {
      logError(comp, "Input %u must be finite.", vr);
      return Error;
    }
    if (vr == vr_duty)
      M(duty) = v;
    else if (vr == vr_load_torque)
      M(load_torque) = v;
    else
      M(fault) = v;
    return OK;
  case vr_R: return set_parameter(comp, &M(R), v, true, "R");
  case vr_L: return set_parameter(comp, &M(L), v, true, "L");
  case vr_Kt: return set_parameter(comp, &M(Kt), v, false, "Kt");
  case vr_Ke: return set_parameter(comp, &M(Ke), v, false, "Ke");
  case vr_J: return set_parameter(comp, &M(J), v, true, "J");
  case vr_b: return set_parameter(comp, &M(b), v, false, "b");
  case vr_Vbus: return set_parameter(comp, &M(Vbus), v, false, "Vbus");
  default:
    logError(comp, "Set Float64 is not allowed for value reference %u.", vr);
    return Error;
  }
}

Status eventUpdate(ModelInstance *comp)
{
  if (M(params_changed))
    reconfigure(comp);

  /* The time event of this solver step: advance the plant by one step */
  if (comp->nextEventTimeDefined && comp->time >= comp->nextEventTime - 0.5 * FIXED_SOLVER_STEP) {
    plant_sim_step(&M(sim), clamped_duty(M(duty)) * (float)M(Vbus), (float)M(load_torque));
    comp->nextEventTime = comp->startTime + (double)(comp->nSteps + 1) * FIXED_SOLVER_STEP;
  }

  comp->valuesOfContinuousStatesChanged = false;
  comp->nominalsOfContinuousStatesChanged = false;
  comp->terminateSimulation = false;
  comp->nextEventTimeDefined = true;
  return OK;
}
