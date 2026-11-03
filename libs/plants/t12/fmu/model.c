/* T12 tip plant as an FMU: the Reference FMUs framework provides the FMI API, this file maps its
 * variables onto t12_plant. One plant step per solver step (FIXED_SOLVER_STEP, 1 ms), done in
 * eventUpdate on a time event every step: the inputs set before fmi3DoStep are held over the step,
 * and the outputs read after it are the state at its end. Until the simulation starts, the state
 * follows the ambient input and the parameters (all three nodes at ambient). A change of load_wk
 * recomputes the coefficients (a matrix exponential) at the next step. */
#include <math.h>

#include "config.h"
#include "model.h"

static double *param(ModelInstance *comp, ValueReference vr)
{
  t12_params_t *p = &M(par);
  switch (vr) {
  case vr_c_h: return &p->c_h;
  case vr_c_t: return &p->c_t;
  case vr_c_c: return &p->c_c;
  case vr_r_ht: return &p->r_ht;
  case vr_r_ta: return &p->r_ta;
  case vr_r_tc: return &p->r_tc;
  case vr_r_ca: return &p->r_ca;
  case vr_v_supply: return &p->v_supply;
  case vr_r_coil: return &p->r_coil;
  case vr_r_short: return &p->r_short;
  case vr_seebeck: return &p->seebeck;
  case vr_amp_gain: return &p->amp_gain;
  case vr_amp_offset: return &p->amp_offset;
  case vr_v_clamp: return &p->v_clamp;
  case vr_tau_sense: return &p->tau_sense;
  default: return NULL;
  }
}

/* Parameters that must be above zero (a heat capacity, resistance or time constant of 0 has no
 * meaning and divides by zero); the others must be >= 0, except the amplifier offset. */
static bool must_be_positive(ValueReference vr)
{
  switch (vr) {
  case vr_c_h:
  case vr_c_t:
  case vr_c_c:
  case vr_r_ht:
  case vr_r_ta:
  case vr_r_tc:
  case vr_r_ca:
  case vr_r_coil:
  case vr_r_short:
  case vr_tau_sense: return true;
  default: return false;
  }
}

static bool before_start(const ModelInstance *comp)
{
  return comp->state == Instantiated || comp->state == InitializationMode;
}

Status setStartValues(ModelInstance *comp)
{
  const t12_params_t defaults = T12_PARAMS_DEFAULT;
  M(par) = defaults;
  M(heater) = 0.0;
  M(series_on) = 1.0;
  M(ambient_c) = 25.0;
  M(load_wk) = 0.0;
  M(fault) = 0.0;
  t12_plant_init(&M(sim), &M(par), FIXED_SOLVER_STEP, M(ambient_c));
  M(coeffs_changed) = false;

  comp->nextEventTime = FIXED_SOLVER_STEP;
  comp->nextEventTimeDefined = true;
  return OK;
}

Status calculateValues(ModelInstance *comp)
{
  M(par).g_load = M(load_wk);
  if (before_start(comp)) {
    t12_plant_init(&M(sim), &M(par), FIXED_SOLVER_STEP, M(ambient_c));
    M(coeffs_changed) = false;
  } else if (M(coeffs_changed)) {
    t12_plant_set_params(&M(sim), &M(par), FIXED_SOLVER_STEP); /* keeps the state */
    M(coeffs_changed) = false;
  }
  return OK;
}

Status getFloat64(ModelInstance *comp, ValueReference vr, double values[], size_t nValues,
                  size_t *index)
{
  ASSERT_NVALUES(1);
  calculateValues(comp);
  const double *p = param(comp, vr);
  double v;
  switch (vr) {
  case vr_time: v = comp->time; break;
  case vr_heater: v = M(heater); break;
  case vr_series_on: v = M(series_on); break;
  case vr_ambient_c: v = M(ambient_c); break;
  case vr_load_wk: v = M(load_wk); break;
  case vr_fault: v = M(fault); break;
  case vr_v_amp: v = M(sim).v_amp; break;
  case vr_i_heater: v = M(sim).i_heater; break;
  case vr_t_heater_c: v = M(sim).t_h; break;
  case vr_t_tip_c: v = M(sim).t_t; break;
  case vr_t_handle_c: v = M(sim).t_c; break;
  default:
    if (!p) {
      logError(comp, "Get Float64 is not allowed for value reference %u.", vr);
      return Error;
    }
    v = *p;
  }
  values[(*index)++] = v;
  return OK;
}

Status setFloat64(ModelInstance *comp, ValueReference vr, const double values[], size_t nValues,
                  size_t *index)
{
  ASSERT_NVALUES(1);
  const double v = values[(*index)++];
  if (!isfinite(v)) {
    logError(comp, "Value reference %u: %g is not a finite value.", vr, v);
    return Error;
  }
  switch (vr) {
  case vr_heater: M(heater) = v; return OK;
  case vr_series_on: M(series_on) = v; return OK;
  case vr_ambient_c: M(ambient_c) = v; return OK;
  case vr_load_wk:
    if (v < 0.0) {
      logError(comp, "load_wk = %g must be >= 0.", v);
      return Error;
    }
    if (v != M(load_wk))
      M(coeffs_changed) = true;
    M(load_wk) = v;
    return OK;
  case vr_fault:
    if (v != floor(v) || v < T12_FAULT_NONE || v > T12_FAULT_STUCK_ON) {
      logError(comp, "fault = %g must be 0 (none), 1 (open), 2 (short) or 3 (stuck on).", v);
      return Error;
    }
    M(fault) = v;
    return OK;
  default: break;
  }

  double *p = param(comp, vr);
  if (!p) {
    logError(comp, "Set Float64 is not allowed for value reference %u.", vr);
    return Error;
  }
  if (!before_start(comp)) {
    logError(comp, "Parameter %u can only be set before the simulation starts.", vr);
    return Error;
  }
  if (vr != vr_amp_offset && (v < 0.0 || (must_be_positive(vr) && v == 0.0))) {
    logError(comp, "Parameter %u = %g is not a valid value.", vr, v);
    return Error;
  }
  *p = v;
  M(coeffs_changed) = true;
  return OK;
}

Status eventUpdate(ModelInstance *comp)
{
  calculateValues(comp);

  /* The time event of this solver step: advance the plant by one step */
  if (comp->nextEventTimeDefined && comp->time >= comp->nextEventTime - 0.5 * FIXED_SOLVER_STEP) {
    M(sim).fault = (t12_fault_t)M(fault);
    M(sim).series_on = M(series_on) >= 0.5;
    t12_plant_step(&M(sim), M(heater) >= 0.5, M(ambient_c));
    comp->nextEventTime = comp->startTime + (double)(comp->nSteps + 1) * FIXED_SOLVER_STEP;
  }

  comp->valuesOfContinuousStatesChanged = false;
  comp->nominalsOfContinuousStatesChanged = false;
  comp->terminateSimulation = false;
  comp->nextEventTimeDefined = true;
  return OK;
}
