// LD2410C report-frame parser, against byte frames built from the documented layout (see radar.h).
// These frames are not recordings from a real sensor: none has been run yet.
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "radar.h"

// Build a basic report frame. Returns its length.
static size_t build(uint8_t* out, uint8_t state, uint16_t mvCm, uint8_t mvE, uint16_t stCm, uint8_t stE,
                    uint16_t detCm) {
  const uint8_t head[4] = {0xF4, 0xF3, 0xF2, 0xF1}, foot[4] = {0xF8, 0xF7, 0xF6, 0xF5};
  size_t n = 0;
  memcpy(out, head, 4);
  n = 4;
  out[n++] = 13;
  out[n++] = 0;
  out[n++] = 0x02;
  out[n++] = 0xAA;
  out[n++] = state;
  out[n++] = mvCm & 0xFF;
  out[n++] = mvCm >> 8;
  out[n++] = mvE;
  out[n++] = stCm & 0xFF;
  out[n++] = stCm >> 8;
  out[n++] = stE;
  out[n++] = detCm & 0xFF;
  out[n++] = detCm >> 8;
  out[n++] = 0x55;
  out[n++] = 0x00;
  memcpy(out + n, foot, 4);
  return n + 4;
}

static int feedAll(RadarParser& p, const uint8_t* b, size_t n) {
  int frames = 0;
  for (size_t i = 0; i < n; i++)
    if (p.feed(b[i])) frames++;
  return frames;
}

void test_valid_basic_frame() {
  uint8_t f[40];
  const size_t n = build(f, 3, 150, 40, 280, 65, 280);
  RadarParser p;
  TEST_ASSERT_EQUAL(1, feedAll(p, f, n));
  TEST_ASSERT_EQUAL(3, p.frame().state);
  TEST_ASSERT_EQUAL(150, p.frame().movingCm);
  TEST_ASSERT_EQUAL(40, p.frame().movingEnergy);
  TEST_ASSERT_EQUAL(280, p.frame().stillCm);
  TEST_ASSERT_EQUAL(65, p.frame().stillEnergy);
  TEST_ASSERT_EQUAL(280, p.frame().detectCm);
  TEST_ASSERT_FALSE(p.frame().engineering);
  TEST_ASSERT_EQUAL(1, p.goodFrames());
}

void test_distance_above_255_cm_uses_both_bytes() {
  uint8_t f[40];
  const size_t n = build(f, 2, 0, 0, 0x0123, 10, 0x0123);
  RadarParser p;
  TEST_ASSERT_EQUAL(1, feedAll(p, f, n));
  TEST_ASSERT_EQUAL(0x0123, p.frame().stillCm);
}

void test_engineering_frame_is_accepted() {
  uint8_t f[80];
  const uint8_t head[4] = {0xF4, 0xF3, 0xF2, 0xF1}, foot[4] = {0xF8, 0xF7, 0xF6, 0xF5};
  size_t n = 0;
  memcpy(f, head, 4);
  n = 4;
  f[n++] = 33;
  f[n++] = 0;
  f[n++] = 0x01;
  f[n++] = 0xAA;
  f[n++] = 2;
  const uint8_t rest[8] = {0, 0, 0, 100, 0, 45, 100, 0};  // moving 0 cm, still 100 cm, detect 100
  memcpy(f + n, rest, 8);
  n += 8;
  for (int i = 0; i < 20; i++) f[n++] = (uint8_t)i;  // gate energies, ignored here
  f[n++] = 0x55;
  f[n++] = 0x00;
  memcpy(f + n, foot, 4);
  n += 4;
  RadarParser p;
  TEST_ASSERT_EQUAL(1, feedAll(p, f, n));
  TEST_ASSERT_TRUE(p.frame().engineering);
  TEST_ASSERT_EQUAL(2, p.frame().state);
  TEST_ASSERT_EQUAL(100, p.frame().stillCm);
  TEST_ASSERT_EQUAL(45, p.frame().stillEnergy);
}

void test_bad_tail_footer_state_and_length_are_rejected() {
  uint8_t f[40];
  RadarParser p;
  size_t n = build(f, 2, 0, 0, 100, 50, 100);
  f[n - 5] = 0x01;  // tail check byte wrong
  TEST_ASSERT_EQUAL(0, feedAll(p, f, n));
  n = build(f, 2, 0, 0, 100, 50, 100);
  f[n - 1] = 0x00;  // footer wrong
  TEST_ASSERT_EQUAL(0, feedAll(p, f, n));
  n = build(f, 9, 0, 0, 100, 50, 100);  // state 9 does not exist
  TEST_ASSERT_EQUAL(0, feedAll(p, f, n));
  n = build(f, 2, 0, 0, 100, 50, 100);
  f[4] = 5;  // length too short
  TEST_ASSERT_EQUAL(0, feedAll(p, f, n));
  n = build(f, 2, 0, 0, 100, 50, 100);
  f[4] = 0xFF;
  f[5] = 0xFF;  // absurd length
  TEST_ASSERT_EQUAL(0, feedAll(p, f, n));
  TEST_ASSERT_EQUAL(0, p.goodFrames());
  TEST_ASSERT_TRUE(p.badFrames() >= 4);
  // and it still recovers: a good frame right after bad ones
  n = build(f, 1, 120, 30, 0, 0, 120);
  TEST_ASSERT_EQUAL(1, feedAll(p, f, n));
  TEST_ASSERT_EQUAL(1, p.frame().state);
}

void test_split_frame_and_garbage_and_command_reply() {
  uint8_t a[40], b[40];
  const size_t na = build(a, 2, 0, 0, 90, 70, 90);
  const size_t nb = build(b, 3, 60, 20, 90, 70, 90);
  RadarParser p;
  const uint8_t junk[] = {0x00, 0xF4, 0xF4, 0x13, 0xF3};  // includes a stray F4 and a partial header
  TEST_ASSERT_EQUAL(0, feedAll(p, junk, sizeof junk));
  const uint8_t cmdReply[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
  TEST_ASSERT_EQUAL(0, feedAll(p, cmdReply, sizeof cmdReply));  // different header: skipped
  // first frame arrives in two chunks, then the second frame whole
  TEST_ASSERT_EQUAL(0, feedAll(p, a, 7));
  TEST_ASSERT_EQUAL(1, feedAll(p, a + 7, na - 7));
  TEST_ASSERT_EQUAL(2, p.frame().state);
  TEST_ASSERT_EQUAL(1, feedAll(p, b, nb));
  TEST_ASSERT_EQUAL(3, p.frame().state);
  TEST_ASSERT_EQUAL(2, p.goodFrames());
}

void test_one_byte_at_a_time_many_frames() {
  uint8_t f[40];
  RadarParser p;
  int got = 0;
  for (int k = 0; k < 20; k++) {
    const size_t n = build(f, k % 4, k, 1, 2 * k + 1, 3, k);
    got += feedAll(p, f, n);
    TEST_ASSERT_EQUAL(k % 4, p.frame().state);
    TEST_ASSERT_EQUAL(2 * k + 1, p.frame().stillCm);
  }
  TEST_ASSERT_EQUAL(20, got);
}

static RadarFrame fr(uint8_t state, uint8_t mvE) {
  RadarFrame f = {};
  f.state = state;
  f.movingEnergy = mvE;
  f.stillEnergy = 90;
  f.stillCm = 100;
  return f;
}

void test_still_target_alone_is_never_a_sign_of_life() {
  RadarMotion m;
  for (uint32_t t = 0; t < 3600; t++) m.feed(t, fr(2, 0));  // an hour of a strong still target
  TEST_ASSERT_FALSE(m.takeSignOfLife());
  for (uint32_t t = 0; t < 600; t++) m.feed(t, fr(2, 99));  // even with moving energy set, state says still
  TEST_ASSERT_FALSE(m.takeSignOfLife());
}

void test_sustained_movement_counts_brief_or_weak_does_not() {
  RadarMotion m;
  for (uint32_t t = 0; t < 9; t++) m.feed(t, fr(1, 60));  // 9 s: too short
  TEST_ASSERT_FALSE(m.takeSignOfLife());
  m.feed(9, fr(2, 0));  // interrupted
  for (uint32_t t = 10; t < 30; t++) m.feed(t, fr(3, 20));  // weak
  TEST_ASSERT_FALSE(m.takeSignOfLife());
  for (uint32_t t = 30; t <= 40; t++) m.feed(t, fr(3, 30));  // moving and still, energy 30, 10 s
  TEST_ASSERT_TRUE(m.takeSignOfLife());
  TEST_ASSERT_FALSE(m.takeSignOfLife());  // read once
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_basic_frame);
  RUN_TEST(test_distance_above_255_cm_uses_both_bytes);
  RUN_TEST(test_engineering_frame_is_accepted);
  RUN_TEST(test_bad_tail_footer_state_and_length_are_rejected);
  RUN_TEST(test_split_frame_and_garbage_and_command_reply);
  RUN_TEST(test_one_byte_at_a_time_many_frames);
  RUN_TEST(test_still_target_alone_is_never_a_sign_of_life);
  RUN_TEST(test_sustained_movement_counts_brief_or_weak_does_not);
  return UNITY_END();
}
