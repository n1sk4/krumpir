#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buf.h"

/* Starts the low-priority drain task (core 0). Samples are printed as CSV only while log is on. */
void telemetry_start(void);

/* Called from the control task; never blocks. */
void telemetry_push(const telem_sample_t *s);

void telemetry_set_log(bool on); /* printing the CSV header when turned on */
bool telemetry_log(void);
uint32_t telemetry_dropped(void);

#endif
