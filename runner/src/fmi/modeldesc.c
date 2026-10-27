#include "modeldesc.h"

#include <expat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VARS 100000
#define MAX_XML_BYTES (16u << 20)

typedef struct {
  modeldesc_t *md;
  char *err;
  size_t errlen;
  XML_Parser parser;
  int depth;
  bool in_vars;
  bool saw_root;
  bool failed;
} pctx_t;

static void set_fail(pctx_t *c, const char *fmt, ...) {
  if (c->failed) {
    return;
  }
  c->failed = true;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(c->err, c->errlen, fmt, ap);
  va_end(ap);
  XML_StopParser(c->parser, XML_FALSE);
}

static const char *attr(const XML_Char **atts, const char *key) {
  for (; atts[0] != NULL; atts += 2) {
    if (strcmp(atts[0], key) == 0) {
      return atts[1];
    }
  }
  return NULL;
}

/* Copies src into dst if it fits. Returns false otherwise. */
static bool copy_str(char *dst, size_t cap, const char *src) {
  size_t n = strlen(src);
  if (n == 0 || n >= cap) {
    return false;
  }
  memcpy(dst, src, n + 1);
  return true;
}

static causality_t map_causality(const char *s) {
  if (strcmp(s, "parameter") == 0) {
    return CAUS_PARAMETER;
  }
  if (strcmp(s, "input") == 0) {
    return CAUS_INPUT;
  }
  if (strcmp(s, "output") == 0) {
    return CAUS_OUTPUT;
  }
  return CAUS_OTHER;
}

static void add_variable(pctx_t *c, const char *type, const XML_Char **atts) {
  modeldesc_t *md = c->md;
  const char *name = attr(atts, "name");
  const char *vr_str = attr(atts, "valueReference");
  const char *caus = attr(atts, "causality");
  if (name == NULL || vr_str == NULL) {
    set_fail(c, "<%s> needs name and valueReference", type);
    return;
  }
  char *end = NULL;
  unsigned long long vr = strtoull(vr_str, &end, 10);
  if (*vr_str == '\0' || *end != '\0' || vr > UINT32_MAX) {
    set_fail(c, "variable '%s' has an invalid valueReference '%s'", name, vr_str);
    return;
  }
  if (md->n_vars >= MAX_VARS) {
    set_fail(c, "more than %d variables", MAX_VARS);
    return;
  }
  mvar_t *v = realloc(md->vars, (md->n_vars + 1) * sizeof *md->vars);
  if (v == NULL) {
    set_fail(c, "out of memory");
    return;
  }
  md->vars = v;
  mvar_t *nv = &md->vars[md->n_vars];
  memset(nv, 0, sizeof *nv);
  if (!copy_str(nv->name, sizeof nv->name, name)) {
    set_fail(c, "variable name '%s' is empty or longer than %d characters", name, MD_MAX_NAME - 1);
    return;
  }
  if (!copy_str(nv->type, sizeof nv->type, type) ||
      !copy_str(nv->causality_str, sizeof nv->causality_str, caus != NULL ? caus : "local")) {
    set_fail(c, "variable '%s' has an unsupported type or causality", name);
    return;
  }
  nv->vr = (uint32_t)vr;
  nv->causality = map_causality(nv->causality_str);
  md->n_vars++;
}

static void XMLCALL on_start(void *ud, const XML_Char *name, const XML_Char **atts) {
  pctx_t *c = ud;
  if (c->failed) {
    return;
  }
  if (c->depth == 0) {
    const char *ver = attr(atts, "fmiVersion");
    if (strcmp(name, "fmiModelDescription") != 0) {
      set_fail(c, "root element is <%s>, expected <fmiModelDescription>", name);
    } else if (ver == NULL || strncmp(ver, "3.", 2) != 0) {
      set_fail(c, "unsupported fmiVersion '%s' (FMI 3.x required)", ver != NULL ? ver : "");
    } else if (!copy_str(c->md->fmi_version, sizeof c->md->fmi_version, ver)) {
      set_fail(c, "fmiVersion is too long");
    }
    const char *token = attr(atts, "instantiationToken");
    if (token != NULL && !copy_str(c->md->instantiation_token, sizeof c->md->instantiation_token,
                                   token)) {
      set_fail(c, "instantiationToken is empty or longer than %zu characters",
               sizeof c->md->instantiation_token - 1);
    }
    c->saw_root = true;
  } else if (c->depth == 1) {
    if (strcmp(name, "CoSimulation") == 0) {
      const char *id = attr(atts, "modelIdentifier");
      if (id == NULL || !copy_str(c->md->model_identifier, sizeof c->md->model_identifier, id)) {
        set_fail(c, "<CoSimulation> needs a modelIdentifier of 1 to %d characters",
                 MD_MAX_NAME - 1);
      }
    } else if (strcmp(name, "ModelVariables") == 0) {
      c->in_vars = true;
    }
  } else if (c->depth == 2 && c->in_vars) {
    add_variable(c, name, atts);
  }
  c->depth++;
}

static void XMLCALL on_end(void *ud, const XML_Char *name) {
  pctx_t *c = ud;
  (void)name;
  c->depth--;
  if (c->depth == 1) {
    c->in_vars = false;
  }
}

/* Untrusted input: refuse entity declarations (billion-laughs style expansion). */
static void XMLCALL on_entity_decl(void *ud, const XML_Char *name, int is_param,
                                   const XML_Char *value, int value_len, const XML_Char *base,
                                   const XML_Char *system_id, const XML_Char *public_id,
                                   const XML_Char *notation) {
  (void)is_param, (void)value, (void)value_len, (void)base, (void)system_id, (void)public_id,
      (void)notation;
  set_fail(ud, "entity declaration '%s' is not allowed in modelDescription.xml", name);
}

int modeldesc_parse(const char *xml, size_t len, modeldesc_t *md, char *err, size_t errlen) {
  *md = (modeldesc_t){0};
  if (len > MAX_XML_BYTES) {
    snprintf(err, errlen, "modelDescription.xml is larger than %u bytes", MAX_XML_BYTES);
    return -1;
  }
  pctx_t c = {.md = md, .err = err, .errlen = errlen};
  c.parser = XML_ParserCreate(NULL);
  if (c.parser == NULL) {
    snprintf(err, errlen, "out of memory");
    return -1;
  }
  XML_SetUserData(c.parser, &c);
  XML_SetElementHandler(c.parser, on_start, on_end);
  XML_SetEntityDeclHandler(c.parser, on_entity_decl);

  enum XML_Status st = XML_Parse(c.parser, xml, (int)len, 1);
  if (!c.failed && st == XML_STATUS_ERROR) {
    snprintf(err, errlen, "modelDescription.xml line %lu: %s",
             (unsigned long)XML_GetCurrentLineNumber(c.parser),
             XML_ErrorString(XML_GetErrorCode(c.parser)));
    c.failed = true;
  }
  XML_ParserFree(c.parser);

  if (!c.failed && (!c.saw_root || md->model_identifier[0] == '\0')) {
    snprintf(err, errlen, "FMU has no <CoSimulation> element (co-simulation is required)");
    c.failed = true;
  }
  if (c.failed) {
    modeldesc_free(md);
    return -1;
  }
  return 0;
}

void modeldesc_free(modeldesc_t *md) {
  free(md->vars);
  *md = (modeldesc_t){0};
}

const mvar_t *modeldesc_find(const modeldesc_t *md, const char *name) {
  for (size_t i = 0; i < md->n_vars; i++) {
    if (strcmp(md->vars[i].name, name) == 0) {
      return &md->vars[i];
    }
  }
  return NULL;
}
