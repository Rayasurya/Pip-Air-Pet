// Logic tests that run on the Mac: pio test -e native
#include <string.h>
#include <unity.h>

#include "config.h"
#include "console.h"
#include "gesture.h"
#include "mood.h"

static Readings air(uint16_t co2, float t = 24, float h = 50) {
  Readings r;
  r.airValid = true;
  r.co2 = co2;
  r.tempC = t;
  r.humidity = h;
  r.lightValid = true;
  r.ldrRaw = cfg::LDR_DARK_IS_HIGH ? 500 : 3800;  // bright room
  return r;
}

void setUp() {}
void tearDown() {}

void test_priority() {
  TEST_ASSERT_EQUAL(Mood::Happy, MoodEngine::instant(air(600), false));
  TEST_ASSERT_EQUAL(Mood::Stuffy, MoodEngine::instant(air(1000), false));
  TEST_ASSERT_EQUAL(Mood::Gasping, MoodEngine::instant(air(1600, 35, 20), false));  // CO2 beats heat
  TEST_ASSERT_EQUAL(Mood::Hot, MoodEngine::instant(air(600, 32, 20), false));       // heat beats dry
  TEST_ASSERT_EQUAL(Mood::Dry, MoodEngine::instant(air(600, 24, 25), false));
  TEST_ASSERT_EQUAL(Mood::Muggy, MoodEngine::instant(air(600, 24, 80), false));
  TEST_ASSERT_EQUAL(Mood::Asleep, MoodEngine::instant(air(1600), true));             // sleep beats all
  Readings none;
  TEST_ASSERT_EQUAL(Mood::Happy, MoodEngine::instant(none, false));
}

void test_hold_stops_flicker() {
  MoodEngine e;
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(air(600), 0));
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(air(1200), 1000));               // just crossed
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(air(1200), 1000 + 29000));       // not held long enough
  TEST_ASSERT_EQUAL(Mood::Stuffy, e.update(air(1200), 1000 + 30000));      // held 30 s
}

void test_dark_sleep_and_instant_wake() {
  MoodEngine e;
  Readings r = air(600);
  e.update(r, 0);
  r.ldrRaw = cfg::LDR_DARK_IS_HIGH ? 4000 : 100;  // lights off
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(r, 1000));
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(r, 1000 + 59000));
  TEST_ASSERT_EQUAL(Mood::Asleep, e.update(r, 1000 + 60000));
  r.ldrRaw = cfg::LDR_DARK_IS_HIGH ? 500 : 3800;  // lights on
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(r, 70000));
}

void test_relief_when_window_opens() {
  MoodEngine e;
  uint32_t t = 0;
  for (; t <= 60000; t += 10000) e.update(air(1400), t);
  TEST_ASSERT_EQUAL(Mood::Stuffy, e.base());
  // CO2 falls 250 ppm within 30 s.
  TEST_ASSERT_EQUAL(Mood::Stuffy, e.update(air(1300), t += 10000));
  TEST_ASSERT_EQUAL(Mood::Stuffy, e.update(air(1220), t += 10000));
  TEST_ASSERT_EQUAL(Mood::Relieved, e.update(air(1150), t += 10000));
  // Event ends after 3 s and does not repeat for the same drop.
  TEST_ASSERT_NOT_EQUAL(Mood::Relieved, e.update(air(1100), t + 3100));
  TEST_ASSERT_NOT_EQUAL(Mood::Relieved, e.update(air(1100), t + 10000));
}

void test_slow_drift_is_not_relief() {
  MoodEngine e;
  uint32_t t = 0;
  uint16_t co2 = 1400;
  for (int i = 0; i < 40; i++, t += 10000, co2 -= 10) {  // -10 ppm per 10 s, -120 per 2 min
    TEST_ASSERT_NOT_EQUAL(Mood::Relieved, e.update(air(co2), t));
  }
}

void test_petted_event() {
  MoodEngine e;
  e.update(air(600), 0);
  e.pet(1000);
  TEST_ASSERT_EQUAL(Mood::Petted, e.update(air(600), 1500));
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(air(600), 1000 + cfg::EVENT_SHOW_MS));
}

void test_gestures() {
  GestureDetector g;
  // Single tap: confirmed only after the double-tap gap.
  TEST_ASSERT_EQUAL(Gesture::None, g.update(true, 0));
  TEST_ASSERT_EQUAL(Gesture::None, g.update(false, 100));
  TEST_ASSERT_EQUAL(Gesture::None, g.update(false, 300));
  TEST_ASSERT_EQUAL(Gesture::Tap, g.update(false, 450));
  // Double tap.
  g.update(true, 1000);
  g.update(false, 1080);
  g.update(true, 1200);
  TEST_ASSERT_EQUAL(Gesture::DoubleTap, g.update(false, 1280));
  TEST_ASSERT_EQUAL(Gesture::None, g.update(false, 2000));
  // A 1-3 s hold is "explain", fired on release; the release is not a tap.
  g.update(true, 3000);
  TEST_ASSERT_EQUAL(Gesture::None, g.update(true, 4500));
  TEST_ASSERT_EQUAL_FLOAT(0.25f, g.markProgress(4500));            // the ring starts filling after 1 s
  TEST_ASSERT_EQUAL(Gesture::LongPress, g.update(false, 4600));
  TEST_ASSERT_EQUAL(Gesture::None, g.update(false, 5500));
  // A 3 s hold is a mark, fired once while still held; letting go adds nothing.
  g.update(true, 6000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, g.markProgress(6500));             // short touches show no ring
  TEST_ASSERT_EQUAL(Gesture::None, g.update(true, 8999));
  TEST_ASSERT_EQUAL(Gesture::Mark, g.update(true, 9000));
  TEST_ASSERT_EQUAL(Gesture::None, g.update(true, 12000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, g.markProgress(12000));
  TEST_ASSERT_EQUAL(Gesture::None, g.update(false, 12100));
  TEST_ASSERT_EQUAL(Gesture::None, g.update(false, 13000));
}

void test_explain_text_fits() {
  char l1[22], l2[22], l3[22];
  Readings r = air(1450, 27.4f, 58);
  MoodEngine::explain(Mood::Stuffy, r, l1, l2, l3, sizeof(l1));
  TEST_ASSERT_EQUAL_STRING("CO2 1450 PPM", l1);
  TEST_ASSERT_EQUAL_STRING("OPEN A WINDOW?", l3);
  for (int m = 0; m < (int)Mood::Count; m++) {
    MoodEngine::explain((Mood)m, r, l1, l2, l3, sizeof(l1));
    // 5x7 font is 5 px wide: 25 chars max on 128 px, keep a margin.
    TEST_ASSERT_TRUE(strlen(l1) <= 21 && strlen(l2) <= 21 && strlen(l3) <= 21);
  }
}

void test_commands() {
  TEST_ASSERT_EQUAL(Cmd::None, parseCommand("   ").cmd);
  TEST_ASSERT_EQUAL(Cmd::Help, parseCommand("HELP").cmd);
  Command c = parseCommand("co2 1400");
  TEST_ASSERT_EQUAL(Cmd::Co2, c.cmd);
  TEST_ASSERT_EQUAL_FLOAT(1400, c.value);
  TEST_ASSERT_EQUAL(Cmd::Temp, parseCommand("  temp 33.5 ").cmd);
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("co2").cmd);        // missing number
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("co2 abc").cmd);    // not a number
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("hum 150").cmd);    // out of range
  c = parseCommand("mood Damp");
  TEST_ASSERT_EQUAL(Cmd::Mood, c.cmd);
  TEST_ASSERT_EQUAL(Mood::Muggy, c.mood);
  TEST_ASSERT_EQUAL(Mood::Petted, parseCommand("mood loved").mood);
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("mood grumpy").cmd);
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("dance").cmd);
  TEST_ASSERT_EQUAL(Cmd::FeelsBad, parseCommand("bad").cmd);
  TEST_ASSERT_EQUAL(Cmd::FeelsFine, parseCommand("Fine").cmd);
  TEST_ASSERT_EQUAL(Cmd::Learned, parseCommand("learned").cmd);
  TEST_ASSERT_EQUAL(Cmd::Mark, parseCommand("Mark").cmd);
  TEST_ASSERT_EQUAL_FLOAT(3, parseCommand("volume 3").value);
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("volume 5").cmd);
  c = parseCommand("mode car");
  TEST_ASSERT_EQUAL(Cmd::Mode, c.cmd);
  TEST_ASSERT_EQUAL_STRING("car", c.arg);
  TEST_ASSERT_EQUAL(Cmd::Park, parseCommand("park").cmd);
  TEST_ASSERT_EQUAL_STRING("van", parseCommand("vehicle van").arg);
  TEST_ASSERT_EQUAL_FLOAT(60, parseCommand("room 60").value);
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("room 5").cmd);
  c = parseCommand("voice off");
  TEST_ASSERT_EQUAL(Cmd::Voice, c.cmd);
  TEST_ASSERT_EQUAL_STRING("off", c.arg);
  TEST_ASSERT_EQUAL(Cmd::Marks, parseCommand("marks").cmd);
  c = parseCommand("calibrate");
  TEST_ASSERT_EQUAL(Cmd::Calibrate, c.cmd);
  TEST_ASSERT_EQUAL_FLOAT(0, c.value);                               // 0 = use the configured level
  TEST_ASSERT_EQUAL_FLOAT(450, parseCommand("Calibrate 450").value);
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("calibrate 1200").cmd);  // indoor air, not fresh
  TEST_ASSERT_EQUAL(Cmd::Unknown, parseCommand("calibrate abc").cmd);
  TEST_ASSERT_EQUAL(Cmd::Forget, parseCommand("forget").cmd);
}

void test_demo_cycles_every_mood() {
  bool seen[(int)Mood::Count] = {false};
  for (uint32_t t = 0; t < 11 * kDemoStepMs; t += kDemoStepMs) seen[(int)demoMoodAt(t)] = true;
  for (int m = 0; m < (int)Mood::Count; m++) TEST_ASSERT_TRUE(seen[m]);
  TEST_ASSERT_EQUAL(Mood::Happy, demoMoodAt(11 * kDemoStepMs));  // loops
}

void test_missing_sensor_explains_itself() {
  char l1[22], l2[22], l3[22];
  Readings r;
  r.airMissing = true;
  MoodEngine::explain(Mood::Happy, r, l1, l2, l3, sizeof(l1));
  TEST_ASSERT_EQUAL_STRING("NO CO2 SENSOR", l1);
}

void test_worried_only_while_air_is_fine() {
  MoodEngine e;
  Readings r = air(850);
  r.soon = true;
  r.soonEtaMin = 12;
  r.risePerMin = 9;
  TEST_ASSERT_EQUAL(Mood::Worried, e.update(r, 0));
  char l1[22], l2[22], l3[22];
  MoodEngine::explain(e.base(), r, l1, l2, l3, sizeof(l1));
  TEST_ASSERT_EQUAL_STRING("STUFFY IN ~12 MIN", l2);
  e.pet(1000);
  TEST_ASSERT_EQUAL(Mood::Petted, e.update(r, 1500));       // events still win
  Readings hot = air(850, 33);
  hot.soon = true;
  MoodEngine e2;
  TEST_ASSERT_EQUAL(Mood::Hot, e2.update(hot, 0));          // a real problem beats a forecast
  r.soon = false;
  TEST_ASSERT_EQUAL(Mood::Happy, e.update(r, 5000));        // forecast cleared
}

void test_night_window() {
  // default 21:30-07:30 crosses midnight
  TEST_ASSERT_FALSE(cfg::inNight(21 * 60 + 29));
  TEST_ASSERT_TRUE(cfg::inNight(21 * 60 + 30));
  TEST_ASSERT_TRUE(cfg::inNight(23 * 60 + 59));
  TEST_ASSERT_TRUE(cfg::inNight(0));
  TEST_ASSERT_TRUE(cfg::inNight(3 * 60));
  TEST_ASSERT_TRUE(cfg::inNight(7 * 60 + 29));
  TEST_ASSERT_FALSE(cfg::inNight(7 * 60 + 30));
  TEST_ASSERT_FALSE(cfg::inNight(13 * 60));
  // a window inside one day
  TEST_ASSERT_TRUE(cfg::inNight(14 * 60, 13 * 60, 15 * 60));
  TEST_ASSERT_FALSE(cfg::inNight(15 * 60, 13 * 60, 15 * 60));
  TEST_ASSERT_FALSE(cfg::inNight(12 * 60, 13 * 60, 15 * 60));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_night_window);
  RUN_TEST(test_priority);
  RUN_TEST(test_hold_stops_flicker);
  RUN_TEST(test_dark_sleep_and_instant_wake);
  RUN_TEST(test_relief_when_window_opens);
  RUN_TEST(test_slow_drift_is_not_relief);
  RUN_TEST(test_petted_event);
  RUN_TEST(test_gestures);
  RUN_TEST(test_explain_text_fits);
  RUN_TEST(test_commands);
  RUN_TEST(test_demo_cycles_every_mood);
  RUN_TEST(test_missing_sensor_explains_itself);
  RUN_TEST(test_worried_only_while_air_is_fine);
  return UNITY_END();
}
