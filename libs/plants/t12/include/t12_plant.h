#ifndef T12_PLANT_H
#define T12_PLANT_H

#include <stdbool.h>

/* T12 soldering tip: a three-node thermal network plus the thermocouple front end.
 *   C_h dT_h/dt = P - (T_h - T_t)/R_ht                                             heater
 *   C_t dT_t/dt = (T_h - T_t)/R_ht - (T_t - T_a)(1/R_ta + G) - (T_t - T_c)/R_tc    copper tip
 *   C_c dT_c/dt = (T_t - T_c)/R_tc - (T_c - T_a)/R_ca                              handle
 * Temperatures in degC, heat capacities in J/K, thermal resistances in K/W, G in W/K, T_a is the
 * ambient input. P = V^2/R when the switch is on, else 0. G is the extra conductance of touching a
 * joint; changing it, or any other thermal parameter, means recomputing the coefficients.
 *
 * Sensing: the thermocouple sees T_h - T_c (Seebeck, about 21 uV/K). While the heater is on the
 * sense node is clamped at v_clamp. After switch-off it relaxes to gain * (V_tc + offset) with
 * time constant tau_sense, which stands for the filter, the switching transient and the Peltier
 * artifact. The amplifier cannot go below 0 V or above v_clamp.
 *
 * Doubles throughout: at a 1 ms step the slow modes move by about 1e-5 of their error per step,
 * which a float state would round away.
 * Each step is an exact zero-order-hold discretization (heater state held for the step). */
typedef struct {
  double c_h, c_t, c_c;          /* heat capacities [J/K] */
  double r_ht, r_ta, r_tc, r_ca; /* heater-tip, tip-air, tip-handle, handle-air [K/W] */
  double g_load;                 /* extra tip-to-ambient conductance [W/K] */
  double v_supply, r_coil;       /* [V], [ohm] */
  double r_short;                /* loop resistance when the tip is shorted [ohm] */
  double seebeck;                /* [V/K] */
  double amp_gain, amp_offset;   /* gain, input-referred offset [V] */
  double v_clamp;                /* sense-node supply / clamp [V] */
  double tau_sense;              /* settle time constant after switch-off [s] */
} t12_params_t;

/* Placeholder thermal values for a T12 on a 24 V / 8 ohm heater (72 W): about 5 s to 350 degC,
 * about 9 W to hold it in free air. Replace with values fitted to a measured step response. */
#define T12_PARAMS_DEFAULT                                                                       \
  {                                                                                              \
      .c_h = 0.25, .c_t = 0.8, .c_c = 15.0, .r_ht = 0.5, .r_ta = 35.0, .r_tc = 150.0,          \
      .r_ca = 15.0, .g_load = 0.0, .v_supply = 24.0, .r_coil = 8.0, .r_short = 1.0,            \
      .seebeck = 21e-6, .amp_gain = 250.0, .amp_offset = 10e-6, .v_clamp = 3.3,                \
      .tau_sense = 0.5e-3,                                                                       \
  }

typedef enum {
  T12_FAULT_NONE = 0,
  T12_FAULT_OPEN,     /* no tip: no current, sense input floats to 0 V */
  T12_FAULT_SHORT,    /* shorted tip: r_short, no heating, no thermocouple signal */
  T12_FAULT_STUCK_ON, /* switch conducts regardless of the command */
} t12_fault_t;

/* x[k+1] = ad x[k] + bd [P; T_a]. */
typedef struct {
  double ad[3][3], bd[3][2];
  double settle; /* exp(-dt / tau_sense) */
} t12_coeffs_t;

typedef struct {
  t12_params_t p;
  t12_coeffs_t c;
  t12_fault_t fault;    /* set directly to inject a fault */
  bool series_on;       /* second switch in series (Q3): off blocks the heater whatever Q1 does */
  double t_h, t_t, t_c; /* heater, tip, handle [degC]; the handle NTC reads t_c */
  double v_amp;         /* thermocouple amplifier output [V] */
  double i_heater;      /* heater current of the last step [A] */
} t12_plant_t;

/* Matrix exponential in double math (slow on small MCUs): call it outside the control loop and
 * hand the result over with t12_plant_set_coeffs(). */
void t12_plant_coeffs(const t12_params_t *p, double dt, t12_coeffs_t *out);
void t12_plant_set_coeffs(t12_plant_t *s, const t12_coeffs_t *c); /* keeps state */

void t12_plant_init(t12_plant_t *s, const t12_params_t *p, double dt, double ambient);
void t12_plant_set_params(t12_plant_t *s, const t12_params_t *p, double dt); /* keeps state */

/* Advance one step with the heater switch (Q1) commanded on or off. */
void t12_plant_step(t12_plant_t *s, bool heater_on, double ambient);

#endif
