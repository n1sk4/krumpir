#ifndef PARAM_PARSE_H
#define PARAM_PARSE_H

#include <stdbool.h>

/* Parses a whole string as a finite float in [lo, hi]. Returns false (out untouched) on empty
 * input, trailing garbage, NaN/inf or out-of-range values. */
bool parse_float(const char *s, float lo, float hi, float *out);

#endif
