#ifndef KRUMPIR_WRITER_H
#define KRUMPIR_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "ring.h"

typedef struct writer writer_t;

/* Starts a thread that drains `ring` into a CSV file:
 *   n,t,<in names>,<out names>,wake_ns,step_ns,flags
 * Returns NULL if the file or thread cannot be created. */
writer_t *writer_start(ring_t *ring, const char *path, const char *const *in_names, size_t n_in,
                       const char *const *out_names, size_t n_out);

/* Drains what is left in the ring, joins the thread and closes the file.
 * Returns the number of rows written. */
uint64_t writer_stop(writer_t *w);

#endif
