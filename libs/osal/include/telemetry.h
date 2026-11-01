#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buf.h"

/* CSV telemetry from the control task to the serial port without ever blocking it.
 * The control task pushes samples (a timestamp and up to TELEM_VALUES floats) into a lock-free
 * ring; a low-priority task drains it and prints one CSV row per sample, but only while logging is
 * on. A full ring drops the sample and counts it. */

typedef void (*telemetry_row_t)(const telem_sample_t *s); /* prints one CSV row with printf */

/* Starts the drain task. csv_header is printed once whenever logging is turned on. */
void telemetry_start(const char *csv_header, telemetry_row_t print_row);

/* Called from the control task; never blocks. */
void telemetry_push(const telem_sample_t *s);

void telemetry_set_log(bool on);
bool telemetry_log(void);
uint32_t telemetry_dropped(void);

/* Rounds to the nearest integer, for the integer columns of a row. */
int telemetry_round(float x);

#endif
