#ifndef HIL_H
#define HIL_H

/* HIL link frames between the runner (plant side) and a controller, over a serial line.
 *
 *   0xA5 0x5A | type u8 | seq u16 LE | n u8 | n x float32 LE | CRC-16/CCITT-FALSE (BE) over type..data
 *
 * HELLO (both ways, before the run): v = {HIL_VERSION, plant slots, cmd slots, hash lo16, hash hi16},
 *   hash = hil_names_hash() of the slot names, so both ends check they agree on the values.
 * PLANT (runner -> controller): the plant outputs after step k, seq = k.
 * CMD (controller -> runner): the controller's commands, seq = the PLANT seq it answers (lockstep)
 *   or its own tick count (free-running).
 * BYE (runner -> controller): the run ends or faulted; the controller goes to its safe state.
 *
 * Plain C, no allocation, no OS. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HIL_VERSION 1
#define HIL_MAX_VALUES 16
#define HIL_FRAME_MAX (6 + 4 * HIL_MAX_VALUES + 2)

enum { HIL_HELLO = 1, HIL_PLANT = 2, HIL_CMD = 3, HIL_BYE = 4 };

typedef struct {
  uint8_t type;
  uint16_t seq;
  uint8_t n;
  float v[HIL_MAX_VALUES];
} hil_frame_t;

/* Writes the frame to buf (at least HIL_FRAME_MAX bytes). Returns its length, 0 if n is too big. */
size_t hil_encode(const hil_frame_t *f, uint8_t *buf);

typedef struct {
  uint8_t buf[HIL_FRAME_MAX];
  size_t len;          /* bytes of the current frame so far, sync included */
  uint32_t crc_errors; /* frames dropped for a bad CRC */
  uint32_t resyncs;    /* times the parser lost a frame and looked for the next sync */
} hil_parser_t;

void hil_parser_init(hil_parser_t *p);

/* Feeds one byte. Returns true and fills *out when it completes a valid frame. */
bool hil_parse_byte(hil_parser_t *p, uint8_t b, hil_frame_t *out);

uint16_t hil_crc16(const uint8_t *d, size_t n);

/* FNV-1a over the plant names, then the cmd names, each NUL-terminated, a 0x01 between the lists. */
uint32_t hil_names_hash(const char *const *plant, size_t n_plant, const char *const *cmd,
                        size_t n_cmd);

void hil_hello(hil_frame_t *f, size_t n_plant, size_t n_cmd, uint32_t hash);

/* True when a HELLO frame carries this version, these slot counts and this hash. */
bool hil_hello_matches(const hil_frame_t *f, size_t n_plant, size_t n_cmd, uint32_t hash);

#endif
