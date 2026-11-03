#ifndef KRUMPIR_TIMING_PIN_H
#define KRUMPIR_TIMING_PIN_H

#include <stdbool.h>
#include <stddef.h>

/* A GPIO line that is high while each step runs (rt.timing_gpio), for a logic analyzer: its
 * period is the tick, its rising edges the jitter, its width the step's work. Linux GPIO
 * character device (uAPI v2), no library; one ioctl per edge (about 1-2 us on a Pi 4). */

#define TIMING_PIN_CHIP "/dev/gpiochip0" /* the Pi 4's GPIO 0-57 (a Pi 5: gpiochip4) */

/* Requests the line as an output, low. Returns its fd (> 0), or -1 with a message in err. */
int timing_pin_open(int line, char *err, size_t errlen);
void timing_pin_set(int fd, bool high);
void timing_pin_close(int fd);

#endif
