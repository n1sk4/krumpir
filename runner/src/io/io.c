#include <stdio.h>
#include <string.h>

#include "io.h"

io_t *io_open(const scenario_t *sc, char *err, size_t errlen) {
  if (strcmp(sc->backend, "null") == 0) {
    return null_io_open(sc, err, errlen);
  }
  snprintf(err, errlen, "io backend '%s' is not supported", sc->backend);
  return NULL;
}
