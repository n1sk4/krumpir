#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buf.h"

/* Samples: v[0] setpoint [degC], v[1] tip [degC], v[2] duty 0..1, v[3] heater current [A].
 * Starts the low-priority drain task (core 0); samples are printed as CSV only while log is on. */
void telemetry_start(void);

/* Called from the control task; never blocks. */
void telemetry_push(const telem_sample_t *s);

void telemetry_set_log(bool on); /* printing the CSV header when turned on */
bool telemetry_log(void);
uint32_t telemetry_dropped(void);

#endif
