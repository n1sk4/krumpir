#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "modeldesc.h"

#define HEAD                                                                                       \
  "<?xml version=\"1.0\"?>\n<fmiModelDescription fmiVersion=\"3.0\" modelName=\"m\" "              \
  "instantiationToken=\"{1234-ABCD}\">\n"
#define COSIM "<CoSimulation modelIdentifier=\"m\"/>\n"

static const char VALID[] = HEAD COSIM
    "<ModelVariables>\n"
    " <Float64 name=\"time\" valueReference=\"0\" causality=\"independent\" "
    "variability=\"continuous\"/>\n"
    " <Float64 name=\"u\" valueReference=\"1\" causality=\"input\" start=\"0\"/>\n"
    " <Float64 name=\"y\" valueReference=\"2\" causality=\"output\"/>\n"
    " <Float64 name=\"k\" valueReference=\"3\" causality=\"parameter\" variability=\"fixed\" "
    "start=\"1\"/>\n"
    " <Int32 name=\"n\" valueReference=\"4\" causality=\"parameter\" variability=\"fixed\" "
    "start=\"1\"/>\n"
    " <Float64 name=\"x\" valueReference=\"5\"/>\n"
    "</ModelVariables>\n"
    "<ModelStructure><Output valueReference=\"2\"/></ModelStructure>\n"
    "</fmiModelDescription>\n";

static void expect_err(const char *xml, const char *substr)
{
  modeldesc_t md;
  char err[256] = "";
  int rc = modeldesc_parse(xml, strlen(xml), &md, err, sizeof err);
  if (rc == 0 || strstr(err, substr) == NULL) {
    fprintf(stderr, "expected error containing '%s', got rc=%d err='%s'\n", substr, rc, err);
  }
  assert(rc == -1);
  assert(strstr(err, substr) != NULL);
  assert(md.vars == NULL && md.n_vars == 0); /* nothing left allocated on failure */
}

static void test_valid(void)
{
  modeldesc_t md;
  char err[256] = "";
  assert(modeldesc_parse(VALID, strlen(VALID), &md, err, sizeof err) == 0);
  assert(strcmp(md.fmi_version, "3.0") == 0);
  assert(strcmp(md.model_identifier, "m") == 0);
  assert(strcmp(md.instantiation_token, "{1234-ABCD}") == 0);
  assert(md.n_vars == 6);

  const mvar_t *v = modeldesc_find(&md, "u");
  assert(v != NULL && v->vr == 1 && v->causality == CAUS_INPUT && mvar_is_float64(v));
  v = modeldesc_find(&md, "y");
  assert(v != NULL && v->vr == 2 && v->causality == CAUS_OUTPUT);
  v = modeldesc_find(&md, "k");
  assert(v != NULL && v->vr == 3 && v->causality == CAUS_PARAMETER);
  v = modeldesc_find(&md, "n");
  assert(v != NULL && !mvar_is_float64(v) && strcmp(v->type, "Int32") == 0);
  v = modeldesc_find(&md, "time");
  assert(v != NULL && v->causality == CAUS_OTHER && strcmp(v->causality_str, "independent") == 0);
  v = modeldesc_find(&md, "x"); /* causality defaults to local */
  assert(v != NULL && strcmp(v->causality_str, "local") == 0 && v->causality == CAUS_OTHER);
  assert(modeldesc_find(&md, "nope") == NULL);
  modeldesc_free(&md);
  assert(md.vars == NULL);
}

static void test_rejected(void)
{
  expect_err("<fmiModelDescription fmiVersion=\"2.0\"><CoSimulation modelIdentifier=\"m\"/>"
             "</fmiModelDescription>",
             "unsupported fmiVersion");
  expect_err("<fmiModelDescription><CoSimulation modelIdentifier=\"m\"/></fmiModelDescription>",
             "unsupported fmiVersion");
  expect_err("<other fmiVersion=\"3.0\"/>", "expected <fmiModelDescription>");
  expect_err(HEAD "<ModelExchange modelIdentifier=\"m\"/></fmiModelDescription>",
             "no <CoSimulation>");
  expect_err(HEAD "<CoSimulation/></fmiModelDescription>", "needs a modelIdentifier");
  expect_err("<fmiModelDescription fmiVersion=\"3.0\"", "line 1");
  expect_err("", "line 1");
  expect_err(HEAD COSIM "<ModelVariables><Float64 valueReference=\"1\"/></ModelVariables>"
                        "</fmiModelDescription>",
             "needs name and valueReference");
  expect_err(HEAD COSIM "<ModelVariables><Float64 name=\"a\" valueReference=\"x1\"/>"
                        "</ModelVariables></fmiModelDescription>",
             "invalid valueReference");
  expect_err(HEAD COSIM "<ModelVariables><Float64 name=\"a\" valueReference=\"4294967296\"/>"
                        "</ModelVariables></fmiModelDescription>",
             "invalid valueReference");
  expect_err(HEAD COSIM "<ModelVariables><Float64 name=\"a\" valueReference=\"-1\"/>"
                        "</ModelVariables></fmiModelDescription>",
             "invalid valueReference");
  expect_err("<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY a \"b\">]>"
             "<fmiModelDescription fmiVersion=\"3.0\"><CoSimulation modelIdentifier=\"m\"/>"
             "</fmiModelDescription>",
             "entity declaration");
}

static void test_long_names(void)
{
  char xml[1024];
  char longname[100];
  memset(longname, 'a', sizeof longname - 1);
  longname[sizeof longname - 1] = '\0';
  snprintf(xml, sizeof xml,
           HEAD COSIM "<ModelVariables><Float64 name=\"%s\" valueReference=\"1\"/>"
                      "</ModelVariables></fmiModelDescription>",
           longname);
  expect_err(xml, "longer than");
  snprintf(xml, sizeof xml, HEAD "<CoSimulation modelIdentifier=\"%s\"/></fmiModelDescription>",
           longname);
  expect_err(xml, "needs a modelIdentifier");
}

int main(void)
{
  test_valid();
  test_rejected();
  test_long_names();
  puts("test_modeldesc: ok");
  return 0;
}
