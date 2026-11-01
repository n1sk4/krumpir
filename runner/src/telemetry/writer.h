#ifndef KRUMPIR_WRITER_H
#define KRUMPIR_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "ring.h"

typedef struct writer writer_t;

/* Called by the writer thread with every record after it is written (may be NULL). */
typedef void (*writer_tap_t)(void *ctx, const telem_rec_t *rec);

/* Starts a thread that drains `ring` into a CSV file:
 *   n,t,<in names>,<out names>,wake_ns,step_ns,flags
 * and passes each record to `tap`. Returns NULL if the file or thread cannot be created. */
writer_t *writer_start(ring_t *ring, const char *path, const char *const *in_names, size_t n_in,
                       const char *const *out_names, size_t n_out, writer_tap_t tap,
                       void *tap_ctx);

/* Waits until every record pushed so far has been written and tapped. */
void writer_flush(writer_t *w);

/* Drains what is left in the ring, joins the thread and closes the file.
 * Returns the number of rows written. */
uint64_t writer_stop(writer_t *w);

#endif
