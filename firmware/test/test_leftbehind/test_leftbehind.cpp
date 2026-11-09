// Left-behind detection: SIMULATED cabin (small model inside this file) against the real engine.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "leftbehind.h"

// A tiny cabin: CO2 mixing with first-order sensor lag (60 s), noise and a fixed offset.
struct Cabin {
  double volM3 = 3.0;
  double outdoor = 450;
  double c = 450;       // true cabin CO2
  double y = 450;       // sensor state (after lag)
  double tempC = 25;    // true and sensor temperature (no lag, tests set it directly)
  double noiseSd = 8;
  double offset = 10;
  uint32_t rng = 12345;

  double noise() {  // deterministic, roughly normal: sum of 12 uniforms
    double s = 0;
    for (int i = 0; i < 12; i++) {
      rng = rng * 1664525u + 1013904223u;
      s += (rng >> 8) / 16777216.0;
    }
    return (s - 6.0) * noiseSd;
  }
  // Advance 5 s with generation genLh (litres per hour of CO2) and ach air changes per hour.
  void step(double genLh, double ach) {
    const double dtH = 5.0 / 3600.0;
    const double g = genLh / (volM3 * 1000.0) * 1e6;  // ppm per hour
    const double cinf = outdoor + g / ach;
    c = cinf + (c - cinf) * exp(-ach * dtH);
    y += (1.0 - exp(-5.0 / 60.0)) * (c - y);
  }
  float reading() { return (float)(y + offset + noise()); }
};

static uint32_t T = 0;

// Run `sec` seconds, feed the engine, and return the second (from start of this run) of the first
// non-None level, or -1.
struct Run {
  int firstCheck = -1;
  int firstUrgent = -1;
  LBResult last;
};

static Run runFor(LeftBehind& lb, Cabin& cab, int sec, double genLh, double ach, double tempStart,
                  double tempEnd) {
  Run r;
  for (int s = 0; s < sec; s += 5) {
    cab.step(genLh, ach);
    cab.tempC = tempStart + (tempEnd - tempStart) * s / (double)sec;
    T += 5;
    r.last = lb.update(T, cab.reading(), (float)cab.tempC);
    if (r.last.level != LBLevel::None && r.firstCheck < 0) r.firstCheck = s;
    if (r.last.level == LBLevel::Urgent && r.firstUrgent < 0) r.firstUrgent = s;
  }
  return r;
}

// Ignition off, doors open for openSec (fast flush), then closed. Returns the cabin ready for watching.
static void parkAndClose(LeftBehind& lb, Cabin& cab, int openSec, double genLh) {
  T += 5;
  lb.setIgnition(false, T);
  runFor(lb, cab, openSec, genLh, 50.0, 25, 25);
}

void setUp() { T = 1000; }
void tearDown() {}

void test_empty_car_never_alerts() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 1500;  // after a drive
  parkAndClose(lb, cab, 60, 0);
  const Run r = runFor(lb, cab, 60 * 60, 0, 0.5, 25, 25);  // closed, leaking 0.5/h, one hour
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
  TEST_ASSERT_TRUE(r.last.state == LBState::Watching);
}

void test_empty_car_after_crowded_drive_never_alerts() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 3500;  // four adults on recirculation
  parkAndClose(lb, cab, 15, 0);  // doors open only briefly
  const Run r = runFor(lb, cab, 60 * 60, 0, 0.3, 25, 25);  // tightest cabin in the test range
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
}

void test_infant_alerts_within_a_few_minutes() {
  LeftBehind lb;
  Cabin cab;  // 3 m3
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 3.5);
  const Run r = runFor(lb, cab, 20 * 60, 3.5, 0.5, 25, 25);  // 3.5 L/h in 3 m3 is about 19 ppm/min
  TEST_ASSERT_TRUE(r.firstCheck >= 0);
  TEST_ASSERT_TRUE_MESSAGE(r.firstCheck <= 8 * 60, "infant should be found within 8 min of the doors closing");
  TEST_ASSERT_TRUE(r.last.state == LBState::Alert);
  TEST_ASSERT_TRUE(r.last.level == LBLevel::Check);  // mild 25 C: not urgent
  TEST_ASSERT_FLOAT_WITHIN(8.0f, 19.0f, r.last.productionPpmMin);
}

void test_changed_flag_marks_transitions_only() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 900;
  T += 5;
  lb.setIgnition(false, T);
  int changes = 0;
  for (int s = 0; s < 15 * 60; s += 5) {
    cab.step(3.5, s < 45 ? 50.0 : 0.5);
    T += 5;
    if (lb.update(T, cab.reading(), 25).changed) changes++;
  }
  // Driving -> Settling (reported by the first update), Settling -> Watching, Watching -> Alert.
  TEST_ASSERT_EQUAL(3, changes);
}

void test_ignition_on_resets() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 3.5);
  Run r = runFor(lb, cab, 12 * 60, 3.5, 0.5, 25, 25);
  TEST_ASSERT_TRUE(r.last.state == LBState::Alert);
  T += 5;
  lb.setIgnition(true, T);
  T += 5;
  const LBResult d = lb.update(T, 800, 25);
  TEST_ASSERT_TRUE(d.state == LBState::Driving);
  TEST_ASSERT_TRUE(d.level == LBLevel::None);
  // Next parking with an empty car must start clean and stay quiet.
  Cabin cab2;
  cab2.c = cab2.y = 1200;
  parkAndClose(lb, cab2, 60, 0);
  r = runFor(lb, cab2, 30 * 60, 0, 0.5, 25, 25);
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
}

void test_dismiss_silences_until_next_ignition() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 3.5);
  Run r = runFor(lb, cab, 12 * 60, 3.5, 0.5, 25, 25);
  TEST_ASSERT_TRUE(r.last.level == LBLevel::Check);
  lb.dismiss(T);
  r = runFor(lb, cab, 30 * 60, 3.5, 0.5, 25, 45);  // still breathing, getting hot: stays silent
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
  TEST_ASSERT_TRUE(r.last.level == LBLevel::None);
}

void test_heat_escalates_to_urgent() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 3.5);
  // Cabin heats from 25 to 55 C over 30 min (a sunny day, SIMULATED).
  const Run r = runFor(lb, cab, 30 * 60, 3.5, 0.5, 25, 55);
  TEST_ASSERT_TRUE(r.firstCheck >= 0);
  TEST_ASSERT_TRUE(r.firstUrgent >= r.firstCheck);
  TEST_ASSERT_TRUE(r.last.level == LBLevel::Urgent);
  TEST_ASSERT_TRUE_MESSAGE(r.firstUrgent <= 15 * 60, "urgent should come once the cabin is rising fast");
  TEST_ASSERT_TRUE(r.last.tempRise10Min > 5.0f);
}

void test_hot_but_empty_is_still_quiet() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 0);
  const Run r = runFor(lb, cab, 45 * 60, 0, 0.5, 25, 62);
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
  TEST_ASSERT_TRUE(r.last.outOfSpec);  // above the sensor's 60 C rating: flagged
}

void test_sensor_lag_does_not_fake_a_rise_or_hide_one() {
  // Falling reading (sensor still catching up with a flush) must not alert.
  LeftBehind lb;
  Cabin cab;
  cab.c = 2500;
  cab.y = 2500;
  parkAndClose(lb, cab, 90, 0);
  Run r = runFor(lb, cab, 10 * 60, 0, 1.0, 25, 25);
  TEST_ASSERT_EQUAL(-1, r.firstCheck);

  // A van (8 m3) with a toddler (6 L/h, about 12 ppm/min) is found even though the sensor lags 60 s.
  LeftBehind lb2;
  Cabin van;
  van.volM3 = 8;
  van.c = van.y = 1100;
  parkAndClose(lb2, van, 60, 6.0);
  r = runFor(lb2, van, 20 * 60, 6.0, 0.5, 25, 25);
  TEST_ASSERT_TRUE(r.firstCheck >= 0);
  TEST_ASSERT_TRUE(r.firstCheck <= 12 * 60);
}

void test_outdoor_estimate_is_bounded() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 3000;
  parkAndClose(lb, cab, 10, 0);  // barely flushed: lowest reading stays far above outdoor
  const Run r = runFor(lb, cab, 5 * 60, 0, 0.5, 25, 25);
  TEST_ASSERT_TRUE(r.last.outdoorPpm <= 600.0f);
  TEST_ASSERT_TRUE(r.last.outdoorPpm >= 400.0f);
}

void test_memory_is_fixed() {
  // No heap: the engine is a plain object whose size does not depend on how long it runs.
  const size_t before = sizeof(LeftBehind);
  LeftBehind lb;
  Cabin cab;
  parkAndClose(lb, cab, 30, 0);
  runFor(lb, cab, 6 * 3600, 0, 0.5, 25, 25);  // six hours of updates
  TEST_ASSERT_EQUAL(before, sizeof(lb));
  TEST_ASSERT_TRUE(before < 6144);  // v4 was 2.6 KB; v5 adds the learner sums (about 1.4 KB) and the learned lists
}

// v2 level rule. A cracked window (4 air changes per hour) hides the rise, but a sleeping infant still
// holds the cabin about 290 ppm above outdoor air.
void test_level_rule_finds_infant_with_cracked_window() {
  LeftBehind::Config c;
  c.productionThresholdPpmMin = 1e9f;  // switch the rise rule off so only the level rule can fire
  c.modelRuleOn = false;               // and the v4 model rule too
  LeftBehind lb(c);
  Cabin cab;
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 3.5);
  const Run r = runFor(lb, cab, 90 * 60, 3.5, 4.0, 25, 25);
  TEST_ASSERT_TRUE_MESSAGE(r.firstCheck >= 0, "level rule should find the infant");
  TEST_ASSERT_TRUE(r.firstCheck <= 40 * 60);

  // Same scene with the level rule also off: nothing fires. Shows the level rule did the work.
  LeftBehind::Config off = c;
  off.levelRuleOn = false;
  LeftBehind lb2(off);
  Cabin cab2;
  cab2.c = cab2.y = 900;
  T = 1000;
  parkAndClose(lb2, cab2, 45, 3.5);
  const Run r2 = runFor(lb2, cab2, 90 * 60, 3.5, 4.0, 25, 25);
  TEST_ASSERT_EQUAL(-1, r2.firstCheck);
}

void test_empty_cracked_window_car_stays_quiet() {
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 1800;
  parkAndClose(lb, cab, 30, 0);
  const Run r = runFor(lb, cab, 90 * 60, 0, 4.0, 25, 25);  // window cracked, nobody inside
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
}

void test_empty_tight_car_level_rule_stays_quiet() {
  // Slowest decay in the range (0.3/h) from a high start: excess stays big for hours, but it keeps falling.
  LeftBehind lb;
  Cabin cab;
  cab.c = cab.y = 4000;
  parkAndClose(lb, cab, 15, 0);
  const Run r = runFor(lb, cab, 3 * 3600, 0, 0.3, 25, 25);
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
}

// v3: cabin volume. An infant in an 8 m3 van adds only about 4-5 ppm/min.
void test_van_infant_alerts_with_van_setting() {
  LeftBehind::Config c;
  c.cabinVolumeM3 = 8.0f;
  LeftBehind lb(c);
  Cabin van;
  van.volM3 = 8;
  van.c = van.y = 1000;
  parkAndClose(lb, van, 60, 3.0);
  const Run r = runFor(lb, van, 20 * 60, 3.0, 0.5, 25, 25);  // 3 L/h in 8 m3 is about 6 ppm/min
  TEST_ASSERT_TRUE_MESSAGE(r.firstCheck >= 0, "van infant should be found");
  TEST_ASSERT_TRUE(r.firstCheck <= 12 * 60);
  TEST_ASSERT_TRUE(r.last.level == LBLevel::Check);
}

void test_empty_van_stays_quiet() {
  LeftBehind::Config c;
  c.cabinVolumeM3 = 8.0f;
  LeftBehind lb(c);
  Cabin van;
  van.volM3 = 8;
  van.c = van.y = 3000;  // crowded trip
  parkAndClose(lb, van, 15, 0);
  const Run r = runFor(lb, van, 2 * 3600, 0, 0.3, 25, 25);
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
}

void test_volume_setting_scales_the_threshold() {
  // A weak 2 L/h source in a 3 m3 car is ~11 ppm/min. In an 8 m3 van the same source is ~4 ppm/min:
  // the default (car) setting is tuned for the first. The van setting must scale its threshold down.
  LeftBehind::Config car, van;
  van.cabinVolumeM3 = 8.0f;
  LeftBehind a(car), b(van);
  Cabin cabA, cabB;
  cabA.volM3 = cabB.volM3 = 8;
  cabA.c = cabA.y = cabB.c = cabB.y = 1000;
  parkAndClose(a, cabA, 60, 2.0);
  T = 1000;
  parkAndClose(b, cabB, 60, 2.0);
  const Run ra = runFor(a, cabA, 25 * 60, 2.0, 0.5, 25, 25);
  T = 1000 + 60 + 5;
  const Run rb = runFor(b, cabB, 25 * 60, 2.0, 0.5, 25, 25);
  TEST_ASSERT_TRUE(rb.firstCheck >= 0);
  TEST_ASSERT_TRUE(ra.firstCheck < 0 || rb.firstCheck <= ra.firstCheck);
}

// v4: model comparison ("pure decay" against "decay + constant source"). Other rules are switched off
// so only this rule can fire.
static LeftBehind::Config modelOnly() {
  LeftBehind::Config c;
  c.productionThresholdPpmMin = 1e9f;
  c.levelRuleOn = false;
  return c;
}

void test_model_rule_finds_cracked_window_infant() {
  LeftBehind lb(modelOnly());
  Cabin cab;
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 3.5);
  const Run r = runFor(lb, cab, 40 * 60, 3.5, 4.0, 25, 25);  // 3.5 L/h, 3 m3, 4 air changes per hour
  TEST_ASSERT_TRUE_MESSAGE(r.firstCheck >= 0, "model rule should find the infant");
  TEST_ASSERT_TRUE(r.firstCheck <= 25 * 60);
}

void test_model_rule_ignores_empty_cars() {
  // A fast-decaying empty cracked-window car, a hard empty closed car (4 adults, doors open briefly),
  // and a tight empty car at the slowest leak. Same seeds of noise as the other tests.
  const double ach[3] = {4.0, 0.6, 0.3};
  const double start[3] = {1800, 3500, 3000};
  for (int i = 0; i < 3; i++) {
    T = 1000;
    LeftBehind lb(modelOnly());
    Cabin cab;
    cab.c = cab.y = start[i];
    cab.rng = 777 + i;
    parkAndClose(lb, cab, i == 0 ? 40 : 15, 0);
    const Run r = runFor(lb, cab, 90 * 60, 0, ach[i], 25, 25);
    TEST_ASSERT_EQUAL(-1, r.firstCheck);
  }
}

void test_model_rule_can_be_switched_off() {
  LeftBehind::Config c = modelOnly();
  c.modelRuleOn = false;
  LeftBehind lb(c);
  Cabin cab;
  cab.c = cab.y = 900;
  parkAndClose(lb, cab, 45, 3.5);
  const Run r = runFor(lb, cab, 40 * 60, 3.5, 4.0, 25, 25);
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
}

// ---- v5: learns its own vehicle ----------------------------------------------------------------

// One empty parking of a vehicle: ignition off, doors open briefly, then closed for `minutes`. The cabin
// starts at `startPpm`. Returns the run. Learning is NOT triggered here (the caller ends the parking).
static Run emptyParking(LeftBehind& lb, Cabin& cab, double startPpm, double ach, int minutes) {
  cab.c = cab.y = startPpm;
  parkAndClose(lb, cab, 40, 0);
  return runFor(lb, cab, minutes * 60, 0, ach, 25, 25);
}

// End the parking the way main.cpp does: learn first, then ignition on.
static bool endParking(LeftBehind& lb) {
  const bool learned = lb.learnFromParking();
  T += 5;
  lb.setIgnition(true, T);
  return learned;
}

void test_learner_learns_from_empty_parking() {
  LeftBehind lb;
  Cabin cab;
  cab.offset = -20;  // this sensor reads 20 ppm low
  const Run r = emptyParking(lb, cab, 1800, 0.8, 70);
  TEST_ASSERT_EQUAL(-1, r.firstCheck);
  TEST_ASSERT_TRUE(endParking(lb));
  TEST_ASSERT_EQUAL(1, lb.learnedBaseCount());
  TEST_ASSERT_EQUAL(1, lb.learnedClosedCount());
  TEST_ASSERT_EQUAL(0, lb.learnedCrackedCount());
}

void test_learner_ignores_alarmed_short_and_dismissed_parkings() {
  {  // an infant: the detector alarms, nothing is learned
    LeftBehind lb;
    Cabin cab;
    cab.c = cab.y = 900;
    parkAndClose(lb, cab, 45, 3.5);
    const Run r = runFor(lb, cab, 50 * 60, 3.5, 0.5, 25, 25);
    TEST_ASSERT_TRUE(r.firstCheck >= 0);
    TEST_ASSERT_FALSE(endParking(lb));
    TEST_ASSERT_EQUAL(0, lb.learnedBaseCount());
  }
  {  // a short parking
    LeftBehind lb;
    Cabin cab;
    emptyParking(lb, cab, 1800, 0.8, 25);
    TEST_ASSERT_FALSE(endParking(lb));
    TEST_ASSERT_EQUAL(0, lb.learnedBaseCount());
  }
  {  // dismissed by the owner
    LeftBehind lb;
    Cabin cab;
    emptyParking(lb, cab, 1800, 0.8, 30);
    lb.dismiss(T);
    runFor(lb, cab, 40 * 60, 0, 0.8, 25, 25);
    TEST_ASSERT_FALSE(endParking(lb));
  }
  {  // no parking at all
    LeftBehind lb;
    TEST_ASSERT_FALSE(lb.learnFromParking());
  }
}

void test_learner_rejects_implausible_level() {
  LeftBehind lb;
  Cabin cab;
  cab.outdoor = 900;  // not outdoor air: a stuffy garage, or someone inside. Level is far above 510.
  emptyParking(lb, cab, 1800, 0.8, 70);
  TEST_ASSERT_FALSE(endParking(lb));
  TEST_ASSERT_EQUAL(0, lb.learnedBaseCount());
}

void test_without_history_it_is_exactly_v4() {
  LeftBehind lb;
  Cabin cab;
  emptyParking(lb, cab, 1800, 0.8, 70);
  endParking(lb);  // one value only: below learnMinBase
  T += 5;
  lb.setIgnition(false, T);
  TEST_ASSERT_EQUAL_FLOAT(510.0f, lb.priorAsymptoteMax());
  TEST_ASSERT_EQUAL_FLOAT(20.0f, lb.priorThreshold());
}

void test_history_tightens_priors_and_median_resists_one_bad_value() {
  LeftBehind lb;
  for (int i = 0; i < 7; i++) {
    Cabin cab;
    cab.offset = -35;  // a sensor that reads 35 ppm low
    cab.outdoor = 440 + 4 * i;
    cab.rng = 100 + i;
    emptyParking(lb, cab, 1500 + 200 * i, 1.2 + 0.05 * i, 85);
    endParking(lb);
  }
  TEST_ASSERT_TRUE(lb.learnedBaseCount() >= 6);
  T += 5;
  lb.setIgnition(false, T);
  const float cap = lb.priorAsymptoteMax();
  TEST_ASSERT_TRUE_MESSAGE(cap < 500.0f, "level cap should come down from 510 for a sensor that reads low");
  // Poison: save, change one stored level to 500, load: the median hardly moves.
  uint8_t buf[kLbLearnedBytes];
  TEST_ASSERT_EQUAL(kLbLearnedBytes, lb.saveLearned(buf, sizeof buf));
  LeftBehind lb2;
  float poisoned = 500.0f;
  memcpy(buf + 4, &poisoned, 4);  // first stored level
  uint32_t sum = 0;
  for (size_t i = 0; i < kLbLearnedBytes - 2; i++) sum += buf[i];
  buf[kLbLearnedBytes - 2] = sum & 0xFF;
  buf[kLbLearnedBytes - 1] = (sum >> 8) & 0xFF;
  TEST_ASSERT_TRUE(lb2.loadLearned(buf, sizeof buf));
  T += 5;
  lb2.setIgnition(false, T);
  TEST_ASSERT_FLOAT_WITHIN(20.0f, cap, lb2.priorAsymptoteMax());
}

void test_save_and_load_round_trip_and_reject_damage() {
  LeftBehind lb;
  for (int i = 0; i < 4; i++) {
    Cabin cab;
    cab.rng = 300 + i;
    emptyParking(lb, cab, 1600, 0.9, 70);
    endParking(lb);
  }
  uint8_t a[kLbLearnedBytes], b[kLbLearnedBytes];
  TEST_ASSERT_EQUAL(kLbLearnedBytes, lb.saveLearned(a, sizeof a));
  LeftBehind lb2;
  TEST_ASSERT_TRUE(lb2.loadLearned(a, sizeof a));
  TEST_ASSERT_EQUAL(kLbLearnedBytes, lb2.saveLearned(b, sizeof b));
  TEST_ASSERT_EQUAL_MEMORY(a, b, kLbLearnedBytes);
  TEST_ASSERT_EQUAL(lb.learnedBaseCount(), lb2.learnedBaseCount());
  // too small a buffer, wrong size, flipped byte, wrong version, NaN value, out-of-range value
  TEST_ASSERT_EQUAL(0, lb.saveLearned(b, kLbLearnedBytes - 1));
  LeftBehind lb3;
  TEST_ASSERT_FALSE(lb3.loadLearned(a, kLbLearnedBytes - 1));
  memcpy(b, a, sizeof a);
  b[10] ^= 0x40;
  TEST_ASSERT_FALSE(lb3.loadLearned(b, sizeof b));
  memcpy(b, a, sizeof a);
  b[0] = 2;
  TEST_ASSERT_FALSE(lb3.loadLearned(b, sizeof b));
  for (int bad = 0; bad < 2; bad++) {
    memcpy(b, a, sizeof a);
    const float v = bad ? 5000.0f : NAN;
    memcpy(b + 4, &v, 4);
    uint32_t sum = 0;
    for (size_t i = 0; i < kLbLearnedBytes - 2; i++) sum += b[i];
    b[kLbLearnedBytes - 2] = sum & 0xFF;
    b[kLbLearnedBytes - 1] = (sum >> 8) & 0xFF;
    TEST_ASSERT_FALSE(lb3.loadLearned(b, sizeof b));
  }
  TEST_ASSERT_EQUAL(0, lb3.learnedBaseCount());  // damaged loads leave the state unchanged
  lb3.clearLearned();
}

void test_learned_vehicle_stays_quiet_when_empty() {
  LeftBehind lb;
  for (int i = 0; i < 6; i++) {
    Cabin cab;
    cab.rng = 500 + i;
    cab.offset = -10;
    emptyParking(lb, cab, 1500 + 300 * i, 4.5 + 0.2 * i, 75);  // window-cracked habits
    endParking(lb);
  }
  for (int i = 0; i < 3; i++) {  // then empty parkings at the edges of the daily spread
    Cabin cab;
    cab.rng = 900 + i;
    cab.offset = -10;
    const Run r = emptyParking(lb, cab, 3000, i == 0 ? 3.5 : 5.8, 90);
    TEST_ASSERT_EQUAL(-1, r.firstCheck);
    endParking(lb);
  }
}

void test_learned_priors_find_cracked_infant_not_later_than_v4() {
  // Same infant and noise, with and without 6 earlier empty parkings of this window-cracked vehicle.
  int tLearned = -2, tPlain = -2;
  for (int withHistory = 0; withHistory < 2; withHistory++) {
    LeftBehind lb;
    if (withHistory) {
      for (int i = 0; i < 6; i++) {
        Cabin cab;
        cab.rng = 700 + i;
        cab.offset = -15;
        emptyParking(lb, cab, 1400 + 250 * i, 4.2 + 0.2 * i, 75);
        endParking(lb);
      }
    }
    Cabin cab;
    cab.offset = -15;
    cab.rng = 4242;
    cab.c = cab.y = 900;
    parkAndClose(lb, cab, 45, 3.5);
    const Run r = runFor(lb, cab, 60 * 60, 3.5, 4.5, 25, 25);
    (withHistory ? tLearned : tPlain) = r.firstCheck;
  }
  TEST_ASSERT_TRUE(tLearned >= 0);
  TEST_ASSERT_TRUE_MESSAGE(tPlain < 0 || tLearned <= tPlain, "learning should not make the find later");
}

void test_learning_can_be_switched_off() {
  LeftBehind::Config c;
  c.learnOn = false;
  LeftBehind lb(c);
  Cabin cab;
  emptyParking(lb, cab, 1800, 0.8, 70);
  TEST_ASSERT_FALSE(endParking(lb));
  TEST_ASSERT_EQUAL(0, lb.learnedBaseCount());
}

// ---- radar option: a still target for 30 s raises an alert on its own (SIMULATED frames) ----------

// Feed the engine one reading per 5 s with a radar frame each second-ish, for `sec` seconds.
static int radarRun(LeftBehind& lb, Cabin& cab, int sec, uint8_t state, uint16_t stillCm, uint8_t energy,
                    bool sendFrames = true) {
  for (int s = 0; s < sec; s += 5) {
    cab.step(0, 0.5);
    T += 5;
    if (sendFrames) lb.setRadar(T, state, stillCm, energy);
    const LBResult r = lb.update(T, cab.reading(), 25);
    if (r.level != LBLevel::None) return s;
  }
  return -1;
}

static void radarSettled(LeftBehind& lb, Cabin& cab) {
  cab.c = cab.y = 600;
  parkAndClose(lb, cab, 40, 0);
  runFor(lb, cab, 5 * 60, 0, 0.5, 25, 25);  // settled and Watching, no CO2 alert
  TEST_ASSERT_TRUE(lb.state() == LBState::Watching);
}

void test_radar_still_target_alerts_after_30_s() {
  LeftBehind lb;
  Cabin cab;
  radarSettled(lb, cab);
  const int at = radarRun(lb, cab, 120, 2, 120, 60);
  TEST_ASSERT_TRUE(at >= 25 && at <= 40);
  TEST_ASSERT_TRUE(lb.state() == LBState::Alert);
  TEST_ASSERT_TRUE(lb.radarAlerted());
}

void test_radar_ignores_moving_far_weak_stale_and_interrupted() {
  {  // moving only
    LeftBehind lb; Cabin cab; radarSettled(lb, cab);
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 180, 1, 120, 90));
  }
  {  // still but outside the cabin range
    LeftBehind lb; Cabin cab; radarSettled(lb, cab);
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 180, 2, 500, 90));
  }
  {  // still but weak
    LeftBehind lb; Cabin cab; radarSettled(lb, cab);
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 180, 2, 120, 5));
  }
  {  // frames stop: stale after 8 s, never reaches 30 s
    LeftBehind lb; Cabin cab; radarSettled(lb, cab);
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 20, 2, 120, 60));
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 60, 2, 120, 60, false));
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 20, 2, 120, 60));
  }
  {  // interrupted by "no target" frames before 30 s: the timer restarts
    LeftBehind lb; Cabin cab; radarSettled(lb, cab);
    for (int i = 0; i < 4; i++) {
      TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 20, 2, 120, 60));
      TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 10, 0, 0, 0));
    }
  }
}

void test_radar_does_not_fire_while_settling_or_after_dismiss() {
  {
    LeftBehind lb;
    Cabin cab;
    cab.c = cab.y = 900;
    T += 5;
    lb.setIgnition(false, T);  // Settling: the adults are still getting out
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 60, 2, 100, 80));
    TEST_ASSERT_TRUE(lb.state() == LBState::Settling);
  }
  {
    LeftBehind lb; Cabin cab; radarSettled(lb, cab);
    lb.dismiss(T);
    TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 120, 2, 120, 60));
  }
}

void test_no_radar_means_no_radar_alert() {
  LeftBehind lb;
  Cabin cab;
  radarSettled(lb, cab);
  TEST_ASSERT_EQUAL(-1, radarRun(lb, cab, 600, 0, 0, 0, false));
  TEST_ASSERT_FALSE(lb.radarAlerted());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_car_never_alerts);
  RUN_TEST(test_empty_car_after_crowded_drive_never_alerts);
  RUN_TEST(test_infant_alerts_within_a_few_minutes);
  RUN_TEST(test_changed_flag_marks_transitions_only);
  RUN_TEST(test_ignition_on_resets);
  RUN_TEST(test_dismiss_silences_until_next_ignition);
  RUN_TEST(test_heat_escalates_to_urgent);
  RUN_TEST(test_hot_but_empty_is_still_quiet);
  RUN_TEST(test_sensor_lag_does_not_fake_a_rise_or_hide_one);
  RUN_TEST(test_outdoor_estimate_is_bounded);
  RUN_TEST(test_memory_is_fixed);
  RUN_TEST(test_level_rule_finds_infant_with_cracked_window);
  RUN_TEST(test_empty_cracked_window_car_stays_quiet);
  RUN_TEST(test_empty_tight_car_level_rule_stays_quiet);
  RUN_TEST(test_van_infant_alerts_with_van_setting);
  RUN_TEST(test_empty_van_stays_quiet);
  RUN_TEST(test_volume_setting_scales_the_threshold);
  RUN_TEST(test_model_rule_finds_cracked_window_infant);
  RUN_TEST(test_model_rule_ignores_empty_cars);
  RUN_TEST(test_model_rule_can_be_switched_off);
  RUN_TEST(test_learner_learns_from_empty_parking);
  RUN_TEST(test_learner_ignores_alarmed_short_and_dismissed_parkings);
  RUN_TEST(test_learner_rejects_implausible_level);
  RUN_TEST(test_without_history_it_is_exactly_v4);
  RUN_TEST(test_history_tightens_priors_and_median_resists_one_bad_value);
  RUN_TEST(test_save_and_load_round_trip_and_reject_damage);
  RUN_TEST(test_learned_vehicle_stays_quiet_when_empty);
  RUN_TEST(test_learned_priors_find_cracked_infant_not_later_than_v4);
  RUN_TEST(test_learning_can_be_switched_off);
  RUN_TEST(test_radar_still_target_alerts_after_30_s);
  RUN_TEST(test_radar_ignores_moving_far_weak_stale_and_interrupted);
  RUN_TEST(test_radar_does_not_fire_while_settling_or_after_dismiss);
  RUN_TEST(test_no_radar_means_no_radar_alert);
  return UNITY_END();
}
