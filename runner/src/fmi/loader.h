#ifndef KRUMPIR_LOADER_H
#define KRUMPIR_LOADER_H

#include <stddef.h>

#include "modeldesc.h"

/* An opened FMU: unpacked into a private temp directory, model description parsed, shared library
 * loaded. */
typedef struct {
  char dir[512];
  modeldesc_t md;
  void *lib;
} fmu_pkg_t;

/* Unpacks `path`, parses modelDescription.xml and dlopens binaries/<platform>/<id>.so. Returns 0 on
 * success; on failure returns -1, writes a one-line message to err and leaves nothing behind. */
int fmu_open(const char *path, fmu_pkg_t *pkg, char *err, size_t errlen);

/* dlclose, free the model description and delete the temp directory. Safe on a zeroed struct. */
void fmu_close(fmu_pkg_t *pkg);

/* Symbol from the FMU's shared library, or NULL. */
void *fmu_sym(const fmu_pkg_t *pkg, const char *name);

#endif
