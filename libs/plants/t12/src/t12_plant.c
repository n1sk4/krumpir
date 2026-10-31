#include "t12_plant.h"

#include <math.h>

#define N 5             /* three temperatures plus the inputs P and T_a, for the augmented exp */
#define TAYLOR_TERMS 24 /* plenty for ||M|| below ~5 */

static void mul(const double a[N][N], const double b[N][N], double out[N][N])
{
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      double sum = 0.0;
      for (int k = 0; k < N; k++)
        sum += a[r][k] * b[k][c];
      out[r][c] = sum;
    }
}

/* exp([[A B], [0 0]] dt) = [[ad bd], [0 I]], so one series gives both matrices (no inverse). */
void t12_plant_coeffs(const t12_params_t *p, double dt, t12_coeffs_t *out)
{
  double g_ta = 1.0 / p->r_ta + p->g_load;
  double m[N][N] = {{0}};
  m[0][0] = -1.0 / (p->r_ht * p->c_h);
  m[0][1] = 1.0 / (p->r_ht * p->c_h);
  m[0][3] = 1.0 / p->c_h;
  m[1][0] = 1.0 / (p->r_ht * p->c_t);
  m[1][1] = -(1.0 / p->r_ht + g_ta + 1.0 / p->r_tc) / p->c_t;
  m[1][2] = 1.0 / (p->r_tc * p->c_t);
  m[1][4] = g_ta / p->c_t;
  m[2][1] = 1.0 / (p->r_tc * p->c_c);
  m[2][2] = -(1.0 / p->r_tc + 1.0 / p->r_ca) / p->c_c;
  m[2][4] = 1.0 / (p->r_ca * p->c_c);
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      m[r][c] *= dt;

  double term[N][N] = {{0}}, e[N][N] = {{0}};
  for (int i = 0; i < N; i++)
    term[i][i] = e[i][i] = 1.0;
  for (int k = 1; k <= TAYLOR_TERMS; k++) {
    double next[N][N];
    mul(term, m, next);
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) {
        term[r][c] = next[r][c] / k; /* m^k / k! */
        e[r][c] += term[r][c];
      }
  }

  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++)
      out->ad[r][c] = e[r][c];
    out->bd[r][0] = e[r][3];
    out->bd[r][1] = e[r][4];
  }
  out->settle = p->tau_sense > 0.0 ? exp(-dt / p->tau_sense) : 0.0;
}

void t12_plant_set_coeffs(t12_plant_t *s, const t12_coeffs_t *c) { s->c = *c; }

void t12_plant_set_params(t12_plant_t *s, const t12_params_t *p, double dt)
{
  s->p = *p;
  t12_plant_coeffs(p, dt, &s->c);
}

static double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

void t12_plant_init(t12_plant_t *s, const t12_params_t *p, double dt, double ambient)
{
  t12_plant_set_params(s, p, dt);
  s->fault = T12_FAULT_NONE;
  s->t_h = s->t_t = s->t_c = ambient;
  s->i_heater = 0.0;
  s->v_amp = clamp(p->amp_gain * p->amp_offset, 0.0, p->v_clamp);
}

void t12_plant_step(t12_plant_t *s, bool heater_on, double ambient)
{
  const t12_params_t *p = &s->p;
  bool on = heater_on || s->fault == T12_FAULT_STUCK_ON;

  double current = 0.0, power = 0.0;
  if (on && s->fault != T12_FAULT_OPEN) {
    current = p->v_supply / (s->fault == T12_FAULT_SHORT ? p->r_short : p->r_coil);
    if (s->fault != T12_FAULT_SHORT)
      power = p->v_supply * current;
  }

  double h = s->c.ad[0][0] * s->t_h + s->c.ad[0][1] * s->t_t + s->c.ad[0][2] * s->t_c +
             s->c.bd[0][0] * power + s->c.bd[0][1] * ambient;
  double t = s->c.ad[1][0] * s->t_h + s->c.ad[1][1] * s->t_t + s->c.ad[1][2] * s->t_c +
             s->c.bd[1][0] * power + s->c.bd[1][1] * ambient;
  double c = s->c.ad[2][0] * s->t_h + s->c.ad[2][1] * s->t_t + s->c.ad[2][2] * s->t_c +
             s->c.bd[2][0] * power + s->c.bd[2][1] * ambient;
  s->t_h = h;
  s->t_t = t;
  s->t_c = c;
  s->i_heater = current;

  bool tc_connected = s->fault != T12_FAULT_OPEN && s->fault != T12_FAULT_SHORT;
  double v_tc = tc_connected ? p->seebeck * (h - c) : 0.0;
  double target = clamp(p->amp_gain * (v_tc + p->amp_offset), 0.0, p->v_clamp);
  s->v_amp = on ? p->v_clamp : target + (s->v_amp - target) * s->c.settle;
}
