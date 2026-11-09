// Speaker sound: tunes, clips, volume and the mood-to-words map.
#include <stdlib.h>
#include <unity.h>

#include "audio.h"

static int16_t buf[64000];

void setUp() {}
void tearDown() {}

static int peak(size_t n) {
  int p = 0;
  for (size_t i = 0; i < n; i++) p = abs(buf[i]) > p ? abs(buf[i]) : p;
  return p;
}

void test_silent_when_idle() {
  AudioOut a;
  TEST_ASSERT_FALSE(a.busy());
  TEST_ASSERT_EQUAL(0, a.render(buf, 512));
}

void test_tune_length_and_fade() {
  AudioOut a;
  static const Note t[] = {{1000, 100}, {0, 50}, {1500, 100}};
  a.playTune(t, 3);
  const size_t n = a.render(buf, 64000);
  TEST_ASSERT_EQUAL(250 * kAudioRate / 1000, n);  // exactly the tune's length
  TEST_ASSERT_FALSE(a.busy());
  TEST_ASSERT_TRUE(abs(buf[0]) < 50);              // fades in: no click
  TEST_ASSERT_EQUAL(0, buf[1700]);                 // the pause is silent
}

void test_clip_plays_at_double_rate_after_the_tune() {
  AudioOut a;
  static const Note t[] = {{1000, 10}};
  a.playTune(t, 1);
  a.playClip(Clip::Thanks);
  const size_t n = a.render(buf, 64000);
  TEST_ASSERT_EQUAL(160 + 2 * kClipTable[(int)Clip::Thanks].len, n);
  TEST_ASSERT_FALSE(a.busy());
}

void test_volume_levels_are_ordered_and_capped() {
  int peaks[3];
  for (int v = 1; v <= 3; v++) {
    AudioOut a;
    a.setVolume(v);
    a.playClip(Clip::AskOk);
    peaks[v - 1] = peak(a.render(buf, 64000));
  }
  TEST_ASSERT_TRUE(peaks[0] < peaks[1] && peaks[1] < peaks[2]);
  TEST_ASSERT_TRUE(peaks[2] <= (int)(0.61f * 32767));  // never full scale: protects the 0.5 W speaker
  AudioOut a;
  a.setVolume(9);
  TEST_ASSERT_EQUAL(3, a.volume());
  a.setVolume(0);
  TEST_ASSERT_EQUAL(1, a.volume());
}

void test_buffered_rendering_matches_one_go() {
  AudioOut a, b;
  a.playClip(Clip::Hot);
  b.playClip(Clip::Hot);
  const size_t whole = a.render(buf, 64000);
  size_t parts = 0, got;
  static int16_t small[256];
  while ((got = b.render(small, 256)) > 0) parts += got;
  TEST_ASSERT_EQUAL(whole, parts);
}

void test_every_clip_has_sound_and_moods_map() {
  for (int c = 0; c < (int)Clip::Count; c++) TEST_ASSERT_TRUE(kClipTable[c].len > 2000);  // > 0.25 s
  TEST_ASSERT_EQUAL((int)Clip::Stale, (int)clipForMood(Mood::Stuffy));
  TEST_ASSERT_EQUAL((int)Clip::None, (int)clipForMood(Mood::Asleep));
  AudioOut a;
  a.playClip(Clip::None);  // ignored safely
  TEST_ASSERT_FALSE(a.busy());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_silent_when_idle);
  RUN_TEST(test_tune_length_and_fade);
  RUN_TEST(test_clip_plays_at_double_rate_after_the_tune);
  RUN_TEST(test_volume_levels_are_ordered_and_capped);
  RUN_TEST(test_buffered_rendering_matches_one_go);
  RUN_TEST(test_every_clip_has_sound_and_moods_map);
  return UNITY_END();
}
