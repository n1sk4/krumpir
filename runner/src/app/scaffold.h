#ifndef KRUMPIR_SCAFFOLD_H
#define KRUMPIR_SCAFFOLD_H

#include <stddef.h>
#include <stdio.h>

/* Writes a starter scenario for the FMU at fmu_path to out: the DefaultExperiment step and stop
 * time, every Float64 parameter at its start value, every Float64 input and output, inputs held at
 * their non-zero start values, the null backend and runs/<model>.csv as the log. The result runs
 * as written. Returns 0, or -1 with a message in err (FMU not loadable). */
int scaffold_scenario(FILE *out, const char *fmu_path, char *err, size_t errlen);

#endif
