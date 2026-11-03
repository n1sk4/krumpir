#include "rte_cmd.h"

#include <stdio.h>

#include "console_util.h"

static rte_t *s_rte; /* Limitation: one runtime per firmware; the console API has no context */

static int cmd_param(int argc, char **argv)
{
  int id = rte_find(s_rte, argv[0]);
  float v;
  if (!console_arg_float(argc, argv, s_rte->params[id].min, s_rte->params[id].max, &v))
    return 1;
  rte_set(s_rte, id, v);
  return 0;
}

static int cmd_clear(int argc, char **argv)
{
  (void)argc, (void)argv;
  rte_request_clear(s_rte);
  return 0;
}

static int cmd_start(int argc, char **argv)
{
  (void)argc, (void)argv;
  const char *trip = rte_trip(s_rte);
  if (trip != NULL) {
    printf("error: trip (%s) is latched, use clear first\n", trip);
    return 1;
  }
  rte_set_run(s_rte, true);
  return 0;
}

static int cmd_stop(int argc, char **argv)
{
  (void)argc, (void)argv;
  rte_set_run(s_rte, false);
  return 0;
}

void rte_cmd_register_params(rte_t *r)
{
  s_rte = r;
  for (int i = 0; i < r->n; i++)
    console_register(r->params[i].name, r->params[i].help, r->params[i].hint, cmd_param);
}

void rte_cmd_register_run(rte_t *r)
{
  s_rte = r;
  console_register("clear", "Clear a latched trip", NULL, cmd_clear);
  console_register("start", "Start the controller", NULL, cmd_start);
  console_register("stop", "Stop: duty 0, PID reset", NULL, cmd_stop);
}
