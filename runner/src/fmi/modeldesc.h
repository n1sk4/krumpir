#ifndef KRUMPIR_MODELDESC_H
#define KRUMPIR_MODELDESC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MD_MAX_NAME 64

typedef enum { CAUS_OTHER, CAUS_PARAMETER, CAUS_INPUT, CAUS_OUTPUT } causality_t;

/* One entry of <ModelVariables>. `type` is the XML element name (Float64, Int32, ...). */
typedef struct {
  char name[MD_MAX_NAME];
  char type[16];
  char causality_str[24];
  uint32_t vr;
  causality_t causality;
} mvar_t;

typedef struct {
  char fmi_version[16];
  char instantiation_token[128];      /* may be empty if the attribute is missing */
  char model_identifier[MD_MAX_NAME]; /* from <CoSimulation> */
  mvar_t *vars;
  size_t n_vars;
} modeldesc_t;

/* Parses modelDescription.xml. Accepts FMI 3.x with a <CoSimulation> element. Returns 0 on
 * success; on failure returns -1 and writes a one-line message to err. */
int modeldesc_parse(const char *xml, size_t len, modeldesc_t *md, char *err, size_t errlen);
void modeldesc_free(modeldesc_t *md);

/* First variable with this name, or NULL. */
const mvar_t *modeldesc_find(const modeldesc_t *md, const char *name);

static inline bool mvar_is_float64(const mvar_t *v) { return strcmp(v->type, "Float64") == 0; }

#endif
