// Comfort learning, tested with simulated users whose true comfort limit is known.
#include <math.h>
#include <string.h>
#include <unity.h>

#include "comfort.h"
#include "config.h"

void setUp() {}
void tearDown() {}

static uint32_t seed = 12345;
static float rnd() {  // small deterministic random, 0..1
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 8) / 16777216.0f;
}

static Readings at(float co2, float t = 26, float h = 60) {
  Readings r;
  r.airValid = true;
  r.co2 = (uint16_t)co2;
  r.tempC = t;
  r.humidity = h;
  return r;
}

// A person who finds CO2 stuffy above `truth`. They answer 70% of the time Pip gets it wrong.
static int runUser(ComfortLearner& c, float truth, int moments) {
  char m1[22], m2[22];
  int answers = 0;
  for (int i = 0; i < moments; i++) {
    const Readings r = at(600 + rnd() * 850);  // 600..1450, below the fixed gasping limit
    const Mood shown = MoodEngine::instant(r, false, c.limits());
    const bool feelsBad = r.co2 >= truth;
    if (rnd() > 0.7f) continue;
    if (shown == Mood::Happy && feelsBad) { c.feedback(Feedback::FeelsBad, shown, r, m1, m2, sizeof(m1)); answers++; }
    if (shown == Mood::Stuffy && !feelsBad) { c.feedback(Feedback::FeelsFine, shown, r, m1, m2, sizeof(m1)); answers++; }
  }
  return answers;
}

void test_learns_a_sensitive_user() {
  ComfortLearner c;
  const int answers = runUser(c, 880, 300);
  TEST_ASSERT_FLOAT_WITHIN(75, 880, c.limits().co2Stuffy);
  TEST_ASSERT_TRUE(answers <= 40);  // settles without endless nagging
}

void test_learns_a_relaxed_user() {
  ComfortLearner c;
  runUser(c, 1250, 300);
  TEST_ASSERT_FLOAT_WITHIN(75, 1250, c.limits().co2Stuffy);
}

void test_safe_band_holds_and_gasping_never_moves() {
  ComfortLearner c;
  runUser(c, 5000, 400);  // someone who never minds stale air
  TEST_ASSERT_TRUE(c.limits().co2Stuffy <= 1400.0f);
  TEST_ASSERT_EQUAL(Mood::Gasping, MoodEngine::instant(at(1600), false, c.limits()));
  ComfortLearner d;
  runUser(d, 300, 400);  // someone who always says it feels bad
  TEST_ASSERT_TRUE(d.limits().co2Stuffy >= 800.0f);
}

void test_agreeing_changes_nothing() {
  ComfortLearner c;
  char m1[22], m2[22];
  TEST_ASSERT_FALSE(c.feedback(Feedback::FeelsFine, Mood::Happy, at(700), m1, m2, sizeof(m1)));
  TEST_ASSERT_EQUAL_STRING("GOOD, WE AGREE", m1);
  TEST_ASSERT_FALSE(c.feedback(Feedback::FeelsBad, Mood::Stuffy, at(1200), m1, m2, sizeof(m1)));
  TEST_ASSERT_EQUAL_FLOAT(cfg::CO2_STUFFY, c.limits().co2Stuffy);
}

void test_hot_and_humidity_learn_too() {
  ComfortLearner c;
  char m1[22], m2[22];
  // Pip says hot at 32 C, you feel fine: the hot limit rises toward 32.5.
  TEST_ASSERT_TRUE(c.feedback(Feedback::FeelsFine, Mood::Hot, at(600, 32), m1, m2, sizeof(m1)));
  TEST_ASSERT_TRUE(c.limits().tempHot > cfg::TEMP_HOT);
  TEST_ASSERT_EQUAL_STRING("GOT IT. HOT LIMIT", m1);
  // Air seems fine but you feel bad at 72% humidity: the damp limit drops.
  TEST_ASSERT_TRUE(c.feedback(Feedback::FeelsBad, Mood::Happy, at(600, 26, 72), m1, m2, sizeof(m1)));
  TEST_ASSERT_TRUE(c.limits().humMuggy < cfg::HUMID_MUGGY);
}

void test_messages_fit_the_screen() {
  ComfortLearner c;
  char m1[22], m2[22];
  const Mood moods[] = {Mood::Happy, Mood::Worried, Mood::Stuffy, Mood::Gasping, Mood::Hot, Mood::Dry, Mood::Muggy};
  for (Mood m : moods)
    for (int f = 0; f < 2; f++) {
      c.feedback(f ? Feedback::FeelsFine : Feedback::FeelsBad, m, at(1100, 31, 50), m1, m2, sizeof(m1));
      TEST_ASSERT_TRUE(strlen(m1) <= 21 && strlen(m2) <= 21);
    }
}

void test_loaded_limits_are_clamped() {
  ComfortLearner c;
  c.set({5000, 99, 0, 100});
  TEST_ASSERT_EQUAL_FLOAT(1400, c.limits().co2Stuffy);
  TEST_ASSERT_EQUAL_FLOAT(34, c.limits().tempHot);
  TEST_ASSERT_EQUAL_FLOAT(20, c.limits().humDry);
  TEST_ASSERT_EQUAL_FLOAT(85, c.limits().humMuggy);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_learns_a_sensitive_user);
  RUN_TEST(test_learns_a_relaxed_user);
  RUN_TEST(test_safe_band_holds_and_gasping_never_moves);
  RUN_TEST(test_agreeing_changes_nothing);
  RUN_TEST(test_hot_and_humidity_learn_too);
  RUN_TEST(test_messages_fit_the_screen);
  RUN_TEST(test_loaded_limits_are_clamped);
  return UNITY_END();
}
