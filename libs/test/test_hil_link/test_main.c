#include <math.h>
#include <string.h>
#include <unity.h>

#include "hil_link.h"

/* What the link wrote, parsed back */
static hil_parser_t out_parser;
static hil_frame_t out_last;
static int out_frames;

static void capture(void *ctx, const uint8_t *buf, size_t n)
{
  (void)ctx;
  for (size_t i = 0; i < n; i++) {
    out_frames += hil_parse_byte(&out_parser, buf[i], &out_last);
  }
}

static const char *const plant[] = {"speed_rpm", "current"}, *const cmd[] = {"duty"};
static hil_slots_t slots;
static hil_link_t link;

void setUp(void)
{
  memset(&slots, 0, sizeof slots);
  hil_parser_init(&out_parser);
  out_frames = 0;
  hil_link_init(&link, &slots, plant, 2, cmd, 1, capture, NULL);
}
void tearDown(void) {}

static int feed(const hil_frame_t *f)
{
  uint8_t buf[HIL_FRAME_MAX + 2] = {0x00, 0xA5}; /* garbage first */
  const size_t n = 2 + hil_encode(f, buf + 2);
  int last = 0;
  for (size_t i = 0; i < n; i++) {
    const int t = hil_link_feed(&link, buf[i]);
    last = t ? t : last;
  }
  return last;
}

static hil_frame_t plant_frame(uint16_t seq, float a, float b)
{
  hil_frame_t f = {HIL_PLANT, seq, 2, {a, b}};
  return f;
}

static void test_hello_answered(void)
{
  hil_frame_t h;
  hil_hello(&h, 2, 1, hil_names_hash(plant, 2, cmd, 1));
  TEST_ASSERT_EQUAL_INT(HIL_HELLO, feed(&h));
  TEST_ASSERT_FALSE(link.hello_mismatch);
  TEST_ASSERT_EQUAL_INT(1, out_frames);
  TEST_ASSERT_TRUE(hil_hello_matches(&out_last, 2, 1, link.hash));

  hil_hello(&h, 1, 1, 1234); /* the runner expects other values: still answered, flagged */
  feed(&h);
  TEST_ASSERT_TRUE(link.hello_mismatch);
  TEST_ASSERT_EQUAL_INT(2, out_frames);
}

static void test_newest_plant_and_age(void)
{
  hil_link_tick_begin(&link);
  TEST_ASSERT_TRUE(slots.age > 1000); /* nothing yet: lost */

  hil_frame_t f = plant_frame(7, 1.0f, 2.0f);
  TEST_ASSERT_EQUAL_INT(HIL_PLANT, feed(&f));
  f = plant_frame(8, 3.0f, 4.0f);
  feed(&f);
  hil_link_tick_begin(&link); /* the newest of the two */
  TEST_ASSERT_EQUAL_UINT32(0, slots.age);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, slots.plant[0]);
  TEST_ASSERT_EQUAL_FLOAT(4.0f, slots.plant[1]);

  hil_link_tick_begin(&link); /* none new: held, one tick older */
  hil_link_tick_begin(&link);
  TEST_ASSERT_EQUAL_UINT32(2, slots.age);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, slots.plant[0]);

  f = plant_frame(9, 5.0f, 6.0f);
  feed(&f);
  hil_link_tick_begin(&link);
  TEST_ASSERT_EQUAL_UINT32(0, slots.age);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, slots.plant[0]);
}

static void test_cmd_answers_the_plant_seq(void)
{
  hil_frame_t f = plant_frame(41, 1.0f, 0.0f);
  feed(&f);
  hil_link_tick_begin(&link);
  slots.cmd[0] = 0.25f;
  hil_link_tick_end(&link);
  TEST_ASSERT_EQUAL_INT(1, out_frames);
  TEST_ASSERT_EQUAL_UINT8(HIL_CMD, out_last.type);
  TEST_ASSERT_EQUAL_UINT16(41, out_last.seq);
  TEST_ASSERT_EQUAL_UINT8(1, out_last.n);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, out_last.v[0]);
}

static void test_wrong_size_ignored(void)
{
  hil_frame_t f = {HIL_PLANT, 1, 3, {1, 2, 3}};
  TEST_ASSERT_EQUAL_INT(0, feed(&f));
  TEST_ASSERT_EQUAL_UINT32(1, link.wrong_size);
  hil_link_tick_begin(&link);
  TEST_ASSERT_TRUE(slots.age > 1000);
}

static void test_bye_once(void)
{
  TEST_ASSERT_FALSE(hil_link_take_bye(&link));
  const hil_frame_t bye = {HIL_BYE, 0, 0, {0}};
  TEST_ASSERT_EQUAL_INT(HIL_BYE, feed(&bye));
  TEST_ASSERT_TRUE(hil_link_take_bye(&link));
  TEST_ASSERT_FALSE(hil_link_take_bye(&link));
}

static void test_waits_again_after_bye(void)
{
  hil_frame_t f = plant_frame(1, 1.0f, 2.0f);
  feed(&f);
  hil_link_tick_begin(&link);
  TEST_ASSERT_EQUAL_UINT32(0, slots.age);
  const hil_frame_t bye = {HIL_BYE, 0, 0, {0}};
  feed(&bye);
  hil_link_tick_begin(&link); /* one more tick on the last values: the stop takes effect */
  TEST_ASSERT_EQUAL_UINT32(1, slots.age);
  hil_link_tick_begin(&link); /* then waiting, not a stale reading */
  TEST_ASSERT_EQUAL_UINT32(HIL_AGE_NONE, slots.age);
  f = plant_frame(0, 5.0f, 6.0f); /* the next run */
  feed(&f);
  hil_link_tick_begin(&link);
  TEST_ASSERT_EQUAL_UINT32(0, slots.age);
  hil_link_tick_begin(&link); /* and a quiet link in that run counts up again (no BYE) */
  hil_link_tick_begin(&link);
  TEST_ASSERT_EQUAL_UINT32(2, slots.age);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_hello_answered);
  RUN_TEST(test_newest_plant_and_age);
  RUN_TEST(test_cmd_answers_the_plant_seq);
  RUN_TEST(test_wrong_size_ignored);
  RUN_TEST(test_bye_once);
  RUN_TEST(test_waits_again_after_bye);
  return UNITY_END();
}
