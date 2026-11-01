#ifndef KRUMPIR_SIGGEN_H
#define KRUMPIR_SIGGEN_H

#include <stddef.h>

#include "config.h"

/* A signal built from one stimulus: its segments with their start times and values resolved, and
 * its data files read. Built before the run; evaluating it does not allocate. */
typedef struct signal signal_t;

/* Builds the signal of `s` (any kind; constant, step and sine become one or two segments). Returns
 * NULL with a message in err when a data file cannot be read or is not valid. */
signal_t *signal_create(const stimulus_t *s, char *err, size_t errlen);

/* Value at time t (s) since the signal started. After its last segment (and last loop) the signal
 * holds its final value. */
double signal_value(const signal_t *g, double t);

void signal_free(signal_t *g);

#endif
