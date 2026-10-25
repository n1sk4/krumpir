#include "plant_sim.h"

#define TAYLOR_TERMS 24 /* plenty for ||A*dt|| below ~5 */

static void mul(const double a[2][2], const double b[2][2], double out[2][2])
{
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 2; c++) out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c];
}

/* ad = exp(A dt), bd = (integral_0^dt exp(A s) ds) B, both by Taylor series (no inverse). */
void plant_sim_coeffs(const plant_params_t *p, float dt, plant_coeffs_t *out)
{
    double m[2][2] = {{-(double)p->R / p->L * dt, -(double)p->Ke / p->L * dt},
                      {(double)p->Kt / p->J * dt, -(double)p->b / p->J * dt}};
    double term[2][2] = {{1, 0}, {0, 1}};
    double ad[2][2] = {{1, 0}, {0, 1}};
    double bi[2][2] = {{1, 0}, {0, 1}};

    for (int k = 1; k <= TAYLOR_TERMS; k++) {
        double next[2][2];
        mul(term, m, next);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 2; c++) {
                term[r][c] = next[r][c] / k; /* m^k / k! */
                ad[r][c] += term[r][c];
                bi[r][c] += term[r][c] / (k + 1); /* m^k / (k+1)! */
            }
    }

    double bcol[2] = {1.0 / p->L, -1.0 / p->J}; /* B is diagonal */
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            out->ad[r][c] = (float)ad[r][c];
            out->bd[r][c] = (float)(dt * bi[r][c] * bcol[c]);
        }
    }
}

void plant_sim_set_coeffs(plant_sim_t *s, const plant_coeffs_t *c)
{
    s->c = *c;
}

void plant_sim_set_params(plant_sim_t *s, const plant_params_t *p, float dt)
{
    plant_coeffs_t c;
    plant_sim_coeffs(p, dt, &c);
    plant_sim_set_coeffs(s, &c);
}

void plant_sim_reset(plant_sim_t *s)
{
    s->i = 0.0f;
    s->w = 0.0f;
}

void plant_sim_init(plant_sim_t *s, const plant_params_t *p, float dt)
{
    plant_sim_set_params(s, p, dt);
    plant_sim_reset(s);
}

void plant_sim_step(plant_sim_t *s, float volts, float load_torque)
{
    float i = s->c.ad[0][0] * s->i + s->c.ad[0][1] * s->w + s->c.bd[0][0] * volts +
              s->c.bd[0][1] * load_torque;
    float w = s->c.ad[1][0] * s->i + s->c.ad[1][1] * s->w + s->c.bd[1][0] * volts +
              s->c.bd[1][1] * load_torque;
    s->i = i;
    s->w = w;
}
