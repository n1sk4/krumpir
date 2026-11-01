/* The platform-independent part of the console contract (console_util.h). */
#include <stdio.h>

#include "console_util.h"
#include "param_parse.h"

bool console_arg_float(int argc, char **argv, float lo, float hi, float *out)
{
  if (argc != 2) {
    printf("usage: %s <value>\n", argv[0]);
    return false;
  }
  if (!parse_float(argv[1], lo, hi, out)) {
    printf("error: %s is not a number in [%g, %g]\n", argv[1], lo, hi);
    return false;
  }
  return true;
}
