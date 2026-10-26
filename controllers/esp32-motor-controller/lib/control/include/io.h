#ifndef IO_H
#define IO_H

/* Backend interface: simulated plant, real motor or Krumpir implement this. */
typedef struct {
  float (*sensor_read)(void *ctx);               /* measured speed [rpm] */
  void (*actuator_write)(void *ctx, float duty); /* duty in -1..+1 */
  void (*safe_state)(void *ctx);                 /* duty 0 / driver disabled */
  void *ctx;
} io_t;

#endif
