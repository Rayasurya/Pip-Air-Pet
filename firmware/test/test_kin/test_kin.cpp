// Kin engine unit tests (the minute-by-minute parity with Python is tools/kin_parity).
#include <string.h>
#include <unity.h>

#include "kin.h"

using namespace kin;

void setUp() {}
void tearDown() {}

// A very regular fake home: up at 06:00 (light switch), sits in the room till 21:00, bed elsewhere.
static void liveDay(Engine& e, long day, int wakeMin, bool collapseAt14 = false, int* familyAt = nullptr) {
  Alert out[3];
  double co2 = 450;
  for (int m = 0; m < 1440; m++) {
    const long t = day * 1440 + m;
    const bool inRoom = m >= wakeMin && m < 21 * 60;
    const bool lightOn = inRoom && (m < 6 * 60 + 30 || m > 18 * 60 + 30);
    // one person breathing: rises toward ~820 ppm; empty: decays to 450
    co2 += inRoom ? 6.2 - (co2 - 450) / 60.0 : -(co2 - 450) / 60.0;
    int ldr = lightOn ? 1900 : ((m > 6 * 60 + 30 && m < 18 * 60) ? 900 : 3900);
    bool touch = inRoom && !(collapseAt14 && m >= 14 * 60) && (m % 120 == 0);
    if (collapseAt14 && m >= 14 * 60) ldr = (m > 6 * 60 + 30 && m < 18 * 60) ? 900 : 3900;
    const int n = e.step(t, co2, ldr, touch, false, out);
    for (int i = 0; i < n; i++)
      if (out[i].level == Level::Family && familyAt && *familyAt < 0) *familyAt = m;
  }
}

void test_learns_for_14_days_without_alerting() {
  Engine e;
  int fam = -1;
  for (int d = 0; d < 14; d++) liveDay(e, d, 6 * 60, false, &fam);
  TEST_ASSERT_EQUAL(-1, fam);
  TEST_ASSERT_FALSE(e.learning());
}

void test_morning_check_reaches_family_when_nobody_gets_up() {
  Engine e;
  for (int d = 0; d < 16; d++) liveDay(e, d, 6 * 60);
  int fam = -1;
  liveDay(e, 16, 24 * 60, false, &fam);  // never comes to the room
  TEST_ASSERT_TRUE(fam > 6 * 60 && fam < 8 * 60);  // usual 06:00 + 15 min margin + chime 15 min
}

void test_normal_days_raise_no_family_alert() {
  Engine e;
  int fam = -1;
  for (int d = 0; d < 40; d++) liveDay(e, d, 6 * 60, false, &fam);
  TEST_ASSERT_EQUAL(-1, fam);
}

void test_collapse_in_room_reaches_family() {
  Engine e;
  for (int d = 0; d < 16; d++) liveDay(e, d, 6 * 60);
  int fam = -1;
  liveDay(e, 16, 6 * 60, true, &fam);
  TEST_ASSERT_TRUE(fam > 14 * 60 && fam <= 14 * 60 + 240 + 60);  // 4 h quiet limit + asks
}

void test_travel_mode_silences_everything() {
  Engine e;
  for (int d = 0; d < 16; d++) liveDay(e, d, 6 * 60);
  Alert out[3];
  int alerts = 0;
  for (int m = 0; m < 1440; m++) alerts += e.step(16L * 1440 + m, 450, 3900, false, true, out);
  TEST_ASSERT_EQUAL(0, alerts);
}

void test_learning_survives_a_power_cut() {
  Engine a;
  for (int d = 0; d < 16; d++) liveDay(a, d, 6 * 60);
  Engine::Learned saved;
  a.exportLearned(saved);
  Engine b;  // fresh boot
  TEST_ASSERT_TRUE(b.importLearned(saved));
  TEST_ASSERT_FALSE(b.learning());
  int fam = -1;
  liveDay(b, 16, 24 * 60, false, &fam);  // the morning check works straight away
  TEST_ASSERT_TRUE(fam > 6 * 60 && fam < 8 * 60);
  Engine::Learned junk = saved;
  junk.version = 99;
  Engine c;
  TEST_ASSERT_FALSE(c.importLearned(junk));
}

void test_family_text_fits_and_is_specific() {
  Engine e;
  for (int d = 0; d < 16; d++) liveDay(e, d, 6 * 60);
  char buf[400];
  e.familyText(Kind::Morning, 16L * 1440 + 7 * 60 + 30, buf, sizeof(buf), "Mom");
  TEST_ASSERT_NOT_NULL(strstr(buf, "Usually up around 06:00"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "It's 07:30"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_learns_for_14_days_without_alerting);
  RUN_TEST(test_morning_check_reaches_family_when_nobody_gets_up);
  RUN_TEST(test_normal_days_raise_no_family_alert);
  RUN_TEST(test_collapse_in_room_reaches_family);
  RUN_TEST(test_travel_mode_silences_everything);
  RUN_TEST(test_learning_survives_a_power_cut);
  RUN_TEST(test_family_text_fits_and_is_specific);
  return UNITY_END();
}
