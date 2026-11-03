#include <string.h>
#include <unity.h>

#include "hil.h"

static hil_parser_t p;

void setUp(void) { hil_parser_init(&p); }
void tearDown(void) {}

static hil_frame_t frame(uint8_t type, uint16_t seq, uint8_t n)
{
  hil_frame_t f = {type, seq, n, {0}};
  for (unsigned i = 0; i < n; i++) {
    f.v[i] = 1.5f * (float)i - 3.25f;
  }
  return f;
}

/* Feeds bytes; returns the number of frames out, the last one in *out. */
static int feed(const uint8_t *b, size_t n, hil_frame_t *out)
{
  int frames = 0;
  for (size_t i = 0; i < n; i++) {
    frames += hil_parse_byte(&p, b[i], out);
  }
  return frames;
}

static void assert_same(const hil_frame_t *a, const hil_frame_t *b)
{
  TEST_ASSERT_EQUAL_UINT8(a->type, b->type);
  TEST_ASSERT_EQUAL_UINT16(a->seq, b->seq);
  TEST_ASSERT_EQUAL_UINT8(a->n, b->n);
  if (a->n > 0) {
    TEST_ASSERT_EQUAL_MEMORY(a->v, b->v, a->n * sizeof(float));
  }
}

static void test_crc_check_value(void)
{
  TEST_ASSERT_EQUAL_HEX16(0x29B1, hil_crc16((const uint8_t *)"123456789", 9));
}

static void test_round_trip_every_size(void)
{
  uint8_t buf[HIL_FRAME_MAX];
  for (uint8_t n = 0; n <= HIL_MAX_VALUES; n++) {
    const hil_frame_t f = frame(HIL_PLANT, (uint16_t)(1000 + n), n);
    const size_t len = hil_encode(&f, buf);
    TEST_ASSERT_EQUAL_size_t(8u + 4u * n, len);
    hil_frame_t out;
    TEST_ASSERT_EQUAL_INT(1, feed(buf, len, &out));
    assert_same(&f, &out);
  }
  hil_frame_t big = frame(HIL_CMD, 0, HIL_MAX_VALUES);
  big.n = HIL_MAX_VALUES + 1;
  TEST_ASSERT_EQUAL_size_t(0, hil_encode(&big, buf));
}

static void test_split_at_every_byte(void)
{
  /* Two frames back to back, fed in two parts cut at every position. */
  uint8_t buf[2 * HIL_FRAME_MAX];
  const hil_frame_t a = frame(HIL_PLANT, 7, 3), b = frame(HIL_CMD, 7, 2);
  size_t len = hil_encode(&a, buf);
  len += hil_encode(&b, buf + len);
  for (size_t cut = 0; cut <= len; cut++) {
    hil_parser_init(&p);
    hil_frame_t out;
    int got = feed(buf, cut, &out);
    got += feed(buf + cut, len - cut, &out);
    TEST_ASSERT_EQUAL_INT(2, got);
    assert_same(&b, &out);
  }
}

static void test_garbage_between_frames(void)
{
  uint8_t buf[3 * HIL_FRAME_MAX];
  const hil_frame_t f = frame(HIL_PLANT, 65535, 4);
  size_t len = 0;
  const uint8_t junk[] = {0x00, 0xA5, 0x00, 0xA5, 0xA5, 0x5A, 0x77, 0xFF, 0x5A};
  memcpy(buf, junk, sizeof junk);
  len += sizeof junk;
  len += hil_encode(&f, buf + len);
  memcpy(buf + len, junk, sizeof junk);
  len += sizeof junk;
  len += hil_encode(&f, buf + len);
  hil_frame_t out;
  TEST_ASSERT_EQUAL_INT(2, feed(buf, len, &out));
  assert_same(&f, &out);
  TEST_ASSERT_TRUE(p.resyncs > 0);
}

static void test_bit_flips_caught(void)
{
  uint8_t buf[HIL_FRAME_MAX], good[HIL_FRAME_MAX];
  const hil_frame_t f = frame(HIL_CMD, 42, HIL_MAX_VALUES);
  const size_t len = hil_encode(&f, good);
  /* Every single-bit flip past the sync bytes: never a frame out; the next good one parses. */
  for (size_t i = 2; i < len; i++) {
    for (int bit = 0; bit < 8; bit++) {
      memcpy(buf, good, len);
      buf[i] ^= (uint8_t)(1 << bit);
      hil_parser_init(&p);
      hil_frame_t out;
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(buf, len, &out), "a corrupted frame was accepted");
      const uint8_t pad[HIL_FRAME_MAX] = {0}; /* ends a frame a flipped length left open */
      feed(pad, sizeof pad, &out);
      TEST_ASSERT_EQUAL_INT(1, feed(good, len, &out));
      assert_same(&f, &out);
    }
  }
}

static void test_hello(void)
{
  const char *plant[] = {"speed_rpm", "current"}, *cmd[] = {"duty"};
  const uint32_t h = hil_names_hash(plant, 2, cmd, 1);
  TEST_ASSERT_NOT_EQUAL(h, hil_names_hash(cmd, 1, plant, 2));
  const char *moved[] = {"speed_rpm"}, *moved_cmd[] = {"current", "duty"};
  TEST_ASSERT_NOT_EQUAL(h, hil_names_hash(moved, 1, moved_cmd, 2));

  hil_frame_t f, out;
  uint8_t buf[HIL_FRAME_MAX];
  hil_hello(&f, 2, 1, 0xDEADBEEF);
  TEST_ASSERT_EQUAL_INT(1, feed(buf, hil_encode(&f, buf), &out));
  TEST_ASSERT_TRUE(hil_hello_matches(&out, 2, 1, 0xDEADBEEF));
  TEST_ASSERT_FALSE(hil_hello_matches(&out, 2, 1, 0xDEADBEEE));
  TEST_ASSERT_FALSE(hil_hello_matches(&out, 1, 1, 0xDEADBEEF));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_crc_check_value);
  RUN_TEST(test_round_trip_every_size);
  RUN_TEST(test_split_at_every_byte);
  RUN_TEST(test_garbage_between_frames);
  RUN_TEST(test_bit_flips_caught);
  RUN_TEST(test_hello);
  return UNITY_END();
}
