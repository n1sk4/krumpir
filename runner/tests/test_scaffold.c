/* krumpir init: the starter scenario of every FMU in fmus/ parses and runs as written. Skipped
 * (exit 77) if the FMUs are not installed. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "runner.h"
#include "scaffold.h"

#ifndef KRUMPIR_FMU_DIR
#error "KRUMPIR_FMU_DIR must be defined by the build"
#endif

static char *scaffold(const char *fmu)
{
  char *text = NULL;
  size_t len = 0;
  FILE *mem = open_memstream(&text, &len);
  char err[512] = "";
  int rc = scaffold_scenario(mem, fmu, err, sizeof err);
  fclose(mem);
  if (rc != 0) {
    fprintf(stderr, "%s: %s\n", fmu, err);
  }
  assert(rc == 0);
  return text;
}

static void run(const char *name, const char *dir)
{
  char fmu[256];
  snprintf(fmu, sizeof fmu, "%s/%s.fmu", KRUMPIR_FMU_DIR, name);
  char *text = scaffold(fmu);

  scenario_t sc;
  char err[512] = "";
  if (config_parse(text, &sc, err, sizeof err) != 0) {
    fprintf(stderr, "%s scenario does not parse: %s\n%s", name, err, text);
    abort();
  }
  snprintf(sc.log_file, sizeof sc.log_file, "%s/%s.csv", dir, name);
  runner_result_t res;
  int rc = runner_run(&sc, &res, err, sizeof err);
  if (rc != RUNNER_OK || res.steps != sc.n_steps) {
    fprintf(stderr, "%s scenario run: rc %d, %llu steps: %s\n%s", name, rc,
            (unsigned long long)res.steps, err, text);
    abort();
  }
  printf("%-12s %llu steps, %zu in, %zu out, %zu params\n", name, (unsigned long long)res.steps,
         sc.n_inputs, sc.n_outputs, sc.n_params);
  remove(sc.log_file);
  free(text);
}

int main(void)
{
  char probe[256];
  snprintf(probe, sizeof probe, "%s/Dahlquist.fmu", KRUMPIR_FMU_DIR);
  if (access(probe, R_OK) != 0) {
    printf("SKIPPED: Reference FMUs not installed\n");
    return 77;
  }
  char dir[] = "/tmp/krumpir_scaffold_XXXXXX";
  assert(mkdtemp(dir) != NULL);

  run("Feedthrough", dir);
  run("Dahlquist", dir);
  run("VanDerPol", dir);

  snprintf(probe, sizeof probe, "%s/t12tip.fmu", KRUMPIR_FMU_DIR);
  if (access(probe, R_OK) == 0) {
    run("dcmotor", dir);
    run("t12tip", dir);

    /* The plant FMUs carry units, descriptions and non-zero input start values. */
    snprintf(probe, sizeof probe, "%s/dcmotor.fmu", KRUMPIR_FMU_DIR);
    char *t = scaffold(probe);
    assert(strstr(t, "R = 2.5  # [Ohm] Armature resistance\n") != NULL);
    assert(strstr(t, "duration = 1  # DefaultExperiment stopTime\n") != NULL);
    free(t);
    snprintf(probe, sizeof probe, "%s/t12tip.fmu", KRUMPIR_FMU_DIR);
    t = scaffold(probe);
    assert(strstr(t, "{ input = \"series_on\", kind = \"constant\", value = 1 }") != NULL);
    assert(strstr(t, "{ input = \"ambient_c\", kind = \"constant\", value = 25 }") != NULL);
    free(t);
  }

  char err[512] = "";
  FILE *null = fopen("/dev/null", "w");
  assert(scaffold_scenario(null, "/nonexistent.fmu", err, sizeof err) == -1 && err[0] != '\0');
  fclose(null);
  rmdir(dir);
  printf("scaffold: ok\n");
  return 0;
}
