#include "param_parse.h"

#include <math.h>
#include <stdlib.h>

bool parse_float(const char *s, float lo, float hi, float *out)
{
  if (!s || !*s)
    return false;
  char *end;
  float v = strtof(s, &end);
  if (*end != '\0' || !isfinite(v) || v < lo || v > hi)
    return false;
  *out = v;
  return true;
}
