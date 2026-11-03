#ifndef HIL_HOST_H
#define HIL_HOST_H

/* The PC port of a controller on the HIL link (Linux): tick_task.h, telemetry.h and hil_port.h on
 * a serial device (a pty pair from socat, or a USB-serial adapter). main_host.c is the program
 * around it; a controller's host/ build links both (target hil_host).
 *
 * Lockstep: one tick per PLANT frame, answered by CMD with the same seq: deterministic, plant time
 * only. Free-running: ticks on its own clock at hz from the first PLANT on; a tick without a new
 * PLANT holds the last values (slots.age counts them), CMD carries the tick count. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opens and sets up the device. Returns 0, or -1 with a message in err. */
int hil_host_open(const char *device, char *err, size_t errlen);

/* Runs the link after control_task_start(): never returns unless the device fails (-1). */
int hil_host_run(bool lockstep, uint32_t hz);

/* A console command run at a tick, before that tick's body (a setpoint profile in plant time:
 * tick = seconds * rate). At most 16, in any order; returns false when full. */
bool hil_host_at(uint32_t tick, const char *line);

/* Called after every tick's body with its tick number (main_host: reports trips). */
void hil_host_after_tick(void (*fn)(uint32_t tick));

#endif
