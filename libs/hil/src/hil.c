#include "hil.h"

#include <string.h>

#define SYNC0 0xA5
#define SYNC1 0x5A

uint16_t hil_crc16(const uint8_t *d, size_t n)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)(d[i] << 8);
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

size_t hil_encode(const hil_frame_t *f, uint8_t *buf)
{
  if (f->n > HIL_MAX_VALUES) {
    return 0;
  }
  size_t k = 0;
  buf[k++] = SYNC0;
  buf[k++] = SYNC1;
  buf[k++] = f->type;
  buf[k++] = (uint8_t)(f->seq & 0xFF);
  buf[k++] = (uint8_t)(f->seq >> 8);
  buf[k++] = f->n;
  for (unsigned i = 0; i < f->n; i++) {
    uint32_t u;
    memcpy(&u, &f->v[i], 4);
    for (int b = 0; b < 4; b++) {
      buf[k++] = (uint8_t)(u >> (8 * b));
    }
  }
  const uint16_t crc = hil_crc16(buf + 2, k - 2);
  buf[k++] = (uint8_t)(crc >> 8);
  buf[k++] = (uint8_t)(crc & 0xFF);
  return k;
}

void hil_parser_init(hil_parser_t *p) { memset(p, 0, sizeof *p); }

static bool valid_type(uint8_t t) { return t >= HIL_HELLO && t <= HIL_BYE; }

/* Drops the frame so far; b may start the next one. */
static void restart(hil_parser_t *p, uint8_t b)
{
  p->resyncs++;
  p->len = 0;
  if (b == SYNC0) {
    p->buf[p->len++] = b;
  }
}

bool hil_parse_byte(hil_parser_t *p, uint8_t b, hil_frame_t *out)
{
  // ponytail: a failed frame is dropped whole, without rescanning its bytes for a sync; the next
  // frame (1 ms later) resyncs.
  if (p->len == 0) {
    if (b == SYNC0) {
      p->buf[p->len++] = b;
    }
    return false;
  }
  if (p->len == 1) {
    if (b == SYNC1) {
      p->buf[p->len++] = b;
    } else {
      restart(p, b);
    }
    return false;
  }
  if ((p->len == 2 && !valid_type(b)) || (p->len == 5 && b > HIL_MAX_VALUES)) {
    restart(p, b);
    return false;
  }
  p->buf[p->len++] = b;
  if (p->len < 6 || p->len < 6u + 4u * p->buf[5] + 2u) {
    return false;
  }
  const size_t n = p->buf[5], end = 6 + 4 * n;
  const uint16_t crc = (uint16_t)(p->buf[end] << 8 | p->buf[end + 1]);
  p->len = 0;
  if (crc != hil_crc16(p->buf + 2, end - 2)) {
    p->crc_errors++;
    p->resyncs++;
    return false;
  }
  out->type = p->buf[2];
  out->seq = (uint16_t)(p->buf[3] | p->buf[4] << 8);
  out->n = (uint8_t)n;
  for (size_t i = 0; i < n; i++) {
    const uint8_t *q = p->buf + 6 + 4 * i;
    const uint32_t u = (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 |
                       (uint32_t)q[3] << 24;
    memcpy(&out->v[i], &u, 4);
  }
  return true;
}

static uint32_t fnv(uint32_t h, const char *s, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    h = (h ^ (uint8_t)s[i]) * 16777619u;
  }
  return h;
}

uint32_t hil_names_hash(const char *const *plant, size_t n_plant, const char *const *cmd,
                        size_t n_cmd)
{
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n_plant; i++) {
    h = fnv(h, plant[i], strlen(plant[i]) + 1);
  }
  h = fnv(h, "\x01", 1);
  for (size_t i = 0; i < n_cmd; i++) {
    h = fnv(h, cmd[i], strlen(cmd[i]) + 1);
  }
  return h;
}

void hil_hello(hil_frame_t *f, size_t n_plant, size_t n_cmd, uint32_t hash)
{
  memset(f, 0, sizeof *f);
  f->type = HIL_HELLO;
  f->n = 5;
  f->v[0] = HIL_VERSION;
  f->v[1] = (float)n_plant;
  f->v[2] = (float)n_cmd;
  f->v[3] = (float)(hash & 0xFFFF); /* 16-bit halves: exact in a float */
  f->v[4] = (float)(hash >> 16);
}

bool hil_hello_matches(const hil_frame_t *f, size_t n_plant, size_t n_cmd, uint32_t hash)
{
  hil_frame_t want;
  hil_hello(&want, n_plant, n_cmd, hash);
  return f->type == HIL_HELLO && f->n == 5 && memcmp(f->v, want.v, 5 * sizeof(float)) == 0;
}
