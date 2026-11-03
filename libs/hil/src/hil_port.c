/* hil_port.h on one link; the platform supplies hil_port_write() and feeds the bytes. */
#include "hil_port.h"

#include "hil_link.h"

static hil_link_t s_link;
static bool s_bound;

static void write_line(void *ctx, const uint8_t *buf, size_t n)
{
  (void)ctx;
  hil_port_write(buf, n);
}

void hil_port_bind(hil_slots_t *s, const char *const *plant, size_t n_plant,
                   const char *const *cmd, size_t n_cmd)
{
  hil_link_init(&s_link, s, plant, n_plant, cmd, n_cmd, write_line, NULL);
  s_bound = true;
}

void hil_port_tick_begin(void) { hil_link_tick_begin(&s_link); }
void hil_port_tick_end(void) { hil_link_tick_end(&s_link); }
bool hil_port_bye(void) { return hil_link_take_bye(&s_link); }

int hil_port_feed(uint8_t b)
{
  if (!s_bound) {
    return 0;
  }
  const int type = hil_link_feed(&s_link, b);
  return type == HIL_HELLO && s_link.hello_mismatch ? -HIL_HELLO : type;
}
