#ifndef MOTOR_IO_H
#define MOTOR_IO_H

/* What the controller needs from the board. Backends: the simulated motor (motor_io_sim), later
 * the real driver and encoder, and the Krumpir HIL link. */
typedef struct {
  float (*speed_read)(void *ctx);            /* measured speed [rpm], NaN when there is none */
  void (*duty_write)(void *ctx, float duty); /* duty in -1..+1, sign = direction */
  void (*safe_state)(void *ctx);             /* duty 0 / driver disabled */
  void *ctx;
} motor_io_t;

#endif
