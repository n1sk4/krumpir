#include <unity.h>

#include "ring_buf.h"

static ring_buf_t r;

void setUp(void) { ring_buf_init(&r); }
void tearDown(void) {}

static telem_sample_t sample(uint32_t t)
{
  telem_sample_t s = {t, 1.0f, 2.0f, 0.5f};
  return s;
}

static void test_empty_pop(void)
{
  telem_sample_t out;
  TEST_ASSERT_FALSE(ring_buf_pop(&r, &out));
}

static void test_order_and_wraparound(void)
{
  telem_sample_t out;
  uint32_t next_in = 0, next_out = 0;
  /* Cycle through the buffer several times with a few samples in flight. */
  for (int round = 0; round < 5 * RING_BUF_CAP; round++) {
    for (int k = 0; k < 3; k++) {
      telem_sample_t s = sample(next_in++);
      TEST_ASSERT_TRUE(ring_buf_push(&r, &s));
    }
    for (int k = 0; k < 3; k++) {
      TEST_ASSERT_TRUE(ring_buf_pop(&r, &out));
      TEST_ASSERT_EQUAL_UINT32(next_out++, out.t_ms);
    }
  }
  TEST_ASSERT_FALSE(ring_buf_pop(&r, &out));
}

static void test_full_drops_and_counts(void)
{
  for (uint32_t i = 0; i < RING_BUF_CAP; i++) {
    telem_sample_t s = sample(i);
    TEST_ASSERT_TRUE(ring_buf_push(&r, &s));
  }
  telem_sample_t extra = sample(999);
  TEST_ASSERT_FALSE(ring_buf_push(&r, &extra));
  TEST_ASSERT_FALSE(ring_buf_push(&r, &extra));
  TEST_ASSERT_EQUAL_UINT32(2, ring_buf_dropped(&r));

  telem_sample_t out;
  TEST_ASSERT_TRUE(ring_buf_pop(&r, &out));
  TEST_ASSERT_EQUAL_UINT32(0, out.t_ms);       /* oldest kept, newest dropped */
  TEST_ASSERT_TRUE(ring_buf_push(&r, &extra)); /* space again */
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_empty_pop);
  RUN_TEST(test_order_and_wraparound);
  RUN_TEST(test_full_drops_and_counts);
  return UNITY_END();
}
