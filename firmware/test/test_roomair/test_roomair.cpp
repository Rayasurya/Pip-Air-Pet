// Room mode: rebreathed fraction, fresh air per person, headcount range, risk, advice, learned leak.
#include <math.h>
#include <string.h>
#include <unity.h>

#include "roomair.h"

void setUp() {}
void tearDown() {}

static void feedFlat(RoomAir& a, float ppm, int minutes) {
  for (int i = 0; i < minutes; i++) a.addMinute(ppm);
}

void test_not_valid_before_20_minutes() {
  RoomAir a;
  feedFlat(a, 1000, 19);
  TEST_ASSERT_FALSE(a.report().valid);
  feedFlat(a, 1000, 1);
  TEST_ASSERT_TRUE(a.report().valid);
}

void test_rebreathed_fraction_by_hand() {
  RoomAir a;
  feedFlat(a, 1000, 40);   // outdoor stays at the 420 default (under 120 minutes of data)
  const RoomAirReport r = a.report();
  TEST_ASSERT_EQUAL_FLOAT(420, r.outdoorPpm);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 580, r.excessPpm);
  // 580 / 38000 = 1.5263 %
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5263f, r.rebreathedPct);
  TEST_ASSERT_EQUAL(2, r.risk);   // 1% to 2%
}

void test_fresh_air_per_person_by_hand() {
  RoomAir a;
  feedFlat(a, 1000, 40);
  const RoomAirReport r = a.report();
  TEST_ASSERT_TRUE(r.occupied);
  TEST_ASSERT_TRUE(r.steady);
  // 0.0050 L/s / 580e-6 = 8.6207 L/s per person
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.6207f, r.freshAirLsPerPerson);

  RoomAir::Config c;
  c.genLsPerPerson = 0.0035f;   // children
  RoomAir k(c);
  feedFlat(k, 1000, 40);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0345f, k.report().freshAirLsPerPerson);
}

void test_rising_co2_is_settling_not_steady() {
  RoomAir a;
  for (int i = 0; i < 40; i++) a.addMinute(500 + 10.0f * i);   // +10 ppm/min
  const RoomAirReport r = a.report();
  TEST_ASSERT_TRUE(r.occupied);
  TEST_ASSERT_FALSE(r.steady);
  char l1[24], l2[24], l3[24];
  RoomAir::advice(r, l1, l2, l3, sizeof(l1));
  TEST_ASSERT_EQUAL_STRING("AIR SETTLING", l1);
}

void test_mass_balance_formula_by_hand() {
  // n = V * (dC/dt + lambda*(C-Cout)) / G, V in litres, C as a fraction.
  // Steady, 580 ppm over outdoor, 2 air changes/h: 180000e-6 * (2/3600*580) / 0.005 = 11.6
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.6f, RoomAir::massBalancePeople(180, 0.005f, 0, 580, 2));
  // Rising 10 ppm/min adds 180000e-6 * (10/60) / 0.005 = 6.0 people
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 17.6f, RoomAir::massBalancePeople(180, 0.005f, 10, 580, 2));
  // Falling faster than the room can explain is clamped to zero
  TEST_ASSERT_EQUAL_FLOAT(0, RoomAir::massBalancePeople(180, 0.005f, -50, 100, 2));
}

// Exact one-minute step of the room model, used to build synthetic rooms for the filter tests.
static float roomStep(float& c, float out, float people, float lamPerH, float volM3, float gen) {
  const float l = lamPerH / 60.0f, e = expf(-l);
  const float prod = people * gen * 60.0f / (volM3 * 1000.0f) * 1e6f;
  c = out + (c - out) * e + prod * (1 - e) / l;
  return c;
}

// Physics note: once CO2 is steady, it fixes only people divided by air-change rate. So a long steady stretch cannot
// pin the headcount; the filter shows that as a wide range. These tests check what must hold, not a tight count.
static void emptyThenClass(RoomAir& a, float& c, int classMinutes) {
  for (int t = 0; t < 240; t++) a.addMinute(roomStep(c, 450, 0, 3, 180, 0.005f) + ((t * 7) % 7 - 3));
  for (int t = 0; t < classMinutes; t++) a.addMinute(roomStep(c, 450, 20, 3, 180, 0.005f) + ((t * 5) % 7 - 3));
}

void test_filter_sees_a_class_arrive() {
  RoomAir a;   // 180 m3, G 0.005
  float c = 450;
  emptyThenClass(a, c, 25);
  const RoomAirReport r = a.report();
  TEST_ASSERT_TRUE(r.occupied);
  TEST_ASSERT_TRUE(r.peopleMid > 3.0f);
  TEST_ASSERT_TRUE(r.peopleLow <= r.peopleMid && r.peopleMid <= r.peopleHigh);
  TEST_ASSERT_TRUE(r.peopleHigh <= 45.0f);                      // volume / 4
  TEST_ASSERT_TRUE(r.leakFilterPerHour >= 0.3f && r.leakFilterPerHour <= 12.0f);
}

void test_filter_sees_the_room_empty_again() {
  RoomAir a;
  float c = 450;
  emptyThenClass(a, c, 120);
  for (int t = 0; t < 70; t++) a.addMinute(roomStep(c, 450, 0, 3, 180, 0.005f) + ((t * 3) % 7 - 3));
  TEST_ASSERT_TRUE(a.report().peopleMid < 2.0f);
}

void test_fresh_air_while_settling_comes_from_the_filter() {
  RoomAir a;
  float c = 450;
  for (int t = 0; t < 240; t++) a.addMinute(roomStep(c, 450, 0, 3, 180, 0.005f) + ((t * 7) % 7 - 3));
  for (int t = 0; t < 25; t++) a.addMinute(roomStep(c, 450, 20, 3, 180, 0.005f));   // class just arrived: CO2 still rising
  const RoomAirReport r = a.report();
  TEST_ASSERT_TRUE(r.occupied);
  TEST_ASSERT_FALSE(r.steady);
  TEST_ASSERT_TRUE(r.freshAirLsPerPerson > 0.5f && r.freshAirLsPerPerson <= 99.0f);
}

void test_empty_room_is_about_zero() {
  RoomAir a;
  for (int i = 0; i < 200; i++) a.addMinute(430.0f + ((i * 7) % 11 - 5));   // +-5 ppm wobble
  const RoomAirReport r = a.report();
  TEST_ASSERT_FALSE(r.occupied);
  TEST_ASSERT_FALSE(r.steady);
  TEST_ASSERT_TRUE(r.peopleMid < 1.0f);
  TEST_ASSERT_EQUAL(0, r.risk);
}

void test_people_never_negative() {
  RoomAir a;
  // Falling fast, below outdoor, and a ragged signal: every count must stay at or above zero.
  for (int i = 0; i < 300; i++) {
    const float v = 1500.0f - 12.0f * i + ((i * 13) % 17 - 8);
    a.addMinute(v < 100 ? 100 : v);
    const RoomAirReport r = a.report();
    TEST_ASSERT_TRUE(r.peopleLow >= 0 && r.peopleMid >= 0 && r.peopleHigh >= 0);
    TEST_ASSERT_TRUE(r.excessPpm >= 0 && r.rebreathedPct >= 0);
  }
}

void test_outdoor_is_guarded() {
  RoomAir low;
  feedFlat(low, 300, 200);
  TEST_ASSERT_EQUAL_FLOAT(400, low.report().outdoorPpm);
  RoomAir high;
  feedFlat(high, 800, 200);
  TEST_ASSERT_EQUAL_FLOAT(500, high.report().outdoorPpm);
  RoomAir mid;
  feedFlat(mid, 450, 200);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 450, mid.report().outdoorPpm);
}

void test_risk_levels() {
  TEST_ASSERT_EQUAL(0, RoomAir::riskFor(0.49f));
  TEST_ASSERT_EQUAL(1, RoomAir::riskFor(0.5f));
  TEST_ASSERT_EQUAL(1, RoomAir::riskFor(0.99f));
  TEST_ASSERT_EQUAL(2, RoomAir::riskFor(1.0f));
  TEST_ASSERT_EQUAL(2, RoomAir::riskFor(1.99f));
  TEST_ASSERT_EQUAL(3, RoomAir::riskFor(2.0f));
}

static void checkAdviceFits(const RoomAirReport& r) {
  char l1[22], l2[22], l3[22];
  RoomAir::advice(r, l1, l2, l3, sizeof(l1));
  const char* ls[3] = {l1, l2, l3};
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(strlen(ls[i]) <= 21);
    for (const char* p = ls[i]; *p; p++) TEST_ASSERT_FALSE(*p >= 'a' && *p <= 'z');   // upper case only
  }
}

void test_advice_lines_fit_21_characters() {
  RoomAirReport r;
  memset(&r, 0, sizeof(r));
  checkAdviceFits(r);                      // not valid
  r.valid = true;
  checkAdviceFits(r);                      // empty
  r.occupied = true;
  r.steady = true;
  const float qs[] = {0.1f, 3.5f, 9.99f, 10.0f, 55.5f, 99.0f};
  for (float q : qs)
    for (uint8_t k = 0; k < 4; k++) {
      r.freshAirLsPerPerson = q;
      r.risk = k;
      checkAdviceFits(r);
    }
  r.steady = false;
  checkAdviceFits(r);
}

void test_advice_text() {
  RoomAir a;
  feedFlat(a, 1000, 40);   // 8.6 L/s per person, risk 2
  char l1[22], l2[22], l3[22];
  RoomAir::advice(a.report(), l1, l2, l3, sizeof(l1));
  TEST_ASSERT_EQUAL_STRING("FRESH AIR 8.6 L/S/P", l1);
  TEST_ASSERT_EQUAL_STRING("TARGET 10", l2);
  TEST_ASSERT_EQUAL_STRING("OPEN WINDOWS", l3);
}

void test_learns_leak_from_a_decay() {
  RoomAir a;
  feedFlat(a, 420, 200);          // empty room, learns outdoor = 420
  feedFlat(a, 1200, 60);          // class in, held steady
  TEST_ASSERT_FALSE(a.report().leakLearned);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, a.report().leakPerHour);   // default until a decay is seen
  const double lam = 3.0;         // air changes per hour
  for (int t = 1; t <= 40; t++)   // class leaves, clean exponential fall toward outdoor air
    a.addMinute((float)(420 + 780 * exp(-lam / 60.0 * t)));
  const RoomAirReport r = a.report();
  TEST_ASSERT_TRUE(r.leakLearned);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 3.0f, r.leakPerHour);   // within 10%
}

void test_plateau_is_not_a_decay() {
  RoomAir a;
  feedFlat(a, 420, 200);
  feedFlat(a, 1200, 60);
  feedFlat(a, 1200, 30);          // nothing is falling
  TEST_ASSERT_FALSE(a.report().leakLearned);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_not_valid_before_20_minutes);
  RUN_TEST(test_rebreathed_fraction_by_hand);
  RUN_TEST(test_fresh_air_per_person_by_hand);
  RUN_TEST(test_rising_co2_is_settling_not_steady);
  RUN_TEST(test_mass_balance_formula_by_hand);
  RUN_TEST(test_filter_sees_a_class_arrive);
  RUN_TEST(test_filter_sees_the_room_empty_again);
  RUN_TEST(test_fresh_air_while_settling_comes_from_the_filter);
  RUN_TEST(test_empty_room_is_about_zero);
  RUN_TEST(test_people_never_negative);
  RUN_TEST(test_outdoor_is_guarded);
  RUN_TEST(test_risk_levels);
  RUN_TEST(test_advice_lines_fit_21_characters);
  RUN_TEST(test_advice_text);
  RUN_TEST(test_learns_leak_from_a_decay);
  RUN_TEST(test_plateau_is_not_a_decay);
  return UNITY_END();
}
