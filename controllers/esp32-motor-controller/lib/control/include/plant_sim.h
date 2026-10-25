#ifndef PLANT_SIM_H
#define PLANT_SIM_H

/* DC motor model, exact zero-order-hold discretization.
 *   L di/dt = V - R i - Ke w
 *   J dw/dt = Kt i - b w - T_load
 * SI units: ohm, henry, V*s/rad, kg*m^2, N*m*s/rad, volt. */
typedef struct {
    float R, L, Kt, Ke, J, b, Vbus;
} plant_params_t;

/* Placeholder values for an N20-class 6 V gearmotor; replace with measured ones. */
#define PLANT_PARAMS_N20 {2.5f, 1e-3f, 0.01f, 0.01f, 1e-6f, 1e-6f, 6.0f}

/* Discretized model: x[k+1] = ad x[k] + bd [V; T_load]. */
typedef struct {
    float ad[2][2], bd[2][2];
} plant_coeffs_t;

typedef struct {
    plant_coeffs_t c;
    float i; /* armature current [A] */
    float w; /* shaft speed [rad/s] */
} plant_sim_t;

/* Computes the coefficients with double math (slow on small MCUs): call it outside the
 * control loop and hand the result over with plant_sim_set_coeffs(). */
void plant_sim_coeffs(const plant_params_t *p, float dt, plant_coeffs_t *out);
void plant_sim_set_coeffs(plant_sim_t *s, const plant_coeffs_t *c); /* keeps state */

void plant_sim_init(plant_sim_t *s, const plant_params_t *p, float dt); /* zero state */
void plant_sim_set_params(plant_sim_t *s, const plant_params_t *p, float dt); /* keeps state */
void plant_sim_reset(plant_sim_t *s);
void plant_sim_step(plant_sim_t *s, float volts, float load_torque);

#endif
