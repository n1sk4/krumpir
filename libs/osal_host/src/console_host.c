#include "console_host.h"

#include <stdio.h>
#include <string.h>

#include "console_util.h"

#define MAX_CMDS 64
#define MAX_ARGS 8

typedef struct {
  const char *name, *help;
  console_cmd_fn_t fn;
} cmd_t;

static cmd_t s_cmds[MAX_CMDS];
static int s_n;

void console_init(const char *prompt)
{
  (void)prompt;
  s_n = 0;
}

void console_register(const char *name, const char *help, const char *hint, console_cmd_fn_t fn)
{
  (void)hint;
  if (s_n < MAX_CMDS)
    s_cmds[s_n++] = (cmd_t){name, help, fn};
}

void console_run(void) {}

void console_print_loop_stats(void) {} /* no tick task on the host */

static const cmd_t *find(const char *name)
{
  for (int i = 0; i < s_n; i++) {
    if (!strcmp(s_cmds[i].name, name))
      return &s_cmds[i];
  }
  return NULL;
}

int console_host_exec(const char *line)
{
  char buf[128];
  snprintf(buf, sizeof buf, "%s", line);
  char *argv[MAX_ARGS];
  int argc = 0;
  for (char *tok = strtok(buf, " "); tok && argc < MAX_ARGS; tok = strtok(NULL, " "))
    argv[argc++] = tok;
  const cmd_t *c = argc > 0 ? find(argv[0]) : NULL;
  return c ? c->fn(argc, argv) : -1;
}

const char *console_host_help(const char *name)
{
  const cmd_t *c = find(name);
  return c ? c->help : NULL;
}
