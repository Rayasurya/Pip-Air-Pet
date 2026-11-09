// Body diary: the 24 h summary saved with each "I feel unwell" mark, and the hourly roll-up.
#include <string.h>
#include <unity.h>

#include "config.h"
#include "diary.h"

static AirSample sample(uint16_t co2, float temp, float hum, bool dark, float press = 0) {
  AirSample s;
  s.co2 = co2;
  s.temp10 = (int16_t)(temp * 10);
  s.hum10 = (uint16_t)(hum * 10);
  s.ldr = dark == cfg::LDR_DARK_IS_HIGH ? 4000 : 500;
  s.press10 = (uint16_t)(press * 10);
  return s;
}

void setUp() {}
void tearDown() {}

void test_summary_of_a_day() {
  static AirHistory h;  // static: 14 KB, keep it off the stack
  h = AirHistory();
  // 24 h: 20 h of fresh air at 700 ppm, then 4 h stale at 1,600 ppm; the last 2 h in the dark.
  for (int i = 0; i < 1440; i++) {
    const bool stale = i >= 1200;
    const float temp = 24.0f + (i >= 1080 ? 3.0f : 0.0f);           // 3 C warmer for the last 6 h
    const float press = 912.0f - 6.0f * i / 1439.0f;                 // pressure falls 6 hPa
    h.add(sample(stale ? 1600 : 700, temp, 60, i >= 1320, press));
  }
  const MarkSummary m = h.summarize();
  TEST_ASSERT_EQUAL(1440, m.minutes);
  TEST_ASSERT_EQUAL_FLOAT(1600, m.co2Max);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, m.hoursOver1400);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, (700 * 1200 + 1600 * 240) / 1440.0f, m.co2Mean);
  TEST_ASSERT_EQUAL_FLOAT(1600, m.co2MeanLast2h);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, m.tempChange6h);
  TEST_ASSERT_EQUAL(120, m.darkMinutes);
  TEST_ASSERT_EQUAL(120, m.darkMinutesLast2h);
  TEST_ASSERT_TRUE(m.pressKnown);
  TEST_ASSERT_FLOAT_WITHIN(0.15f, -6.0f, m.pressChange24h);
}

void test_short_history_and_unknown_pressure() {
  static AirHistory h;
  h = AirHistory();
  for (int i = 0; i < 90; i++) h.add(sample(800, 25, 55, false));
  const MarkSummary m = h.summarize();
  TEST_ASSERT_EQUAL(90, m.minutes);
  TEST_ASSERT_EQUAL_FLOAT(0, m.tempChange6h);   // under 6 h: no change reported
  TEST_ASSERT_FALSE(m.pressKnown);
  TEST_ASSERT_EQUAL_FLOAT(800, m.co2MeanLast2h);  // whole history is inside the last 2 h
  char row[160];
  formatMarkRow("2026-11-02 16:05", m, row, sizeof(row));
  TEST_ASSERT_EQUAL_STRING("2026-11-02 16:05,90,800,800,0.0,800,25.0,25.0,0.0,55,0,0,", row);
}

void test_history_keeps_only_the_last_day() {
  static AirHistory h;
  h = AirHistory();
  for (int i = 0; i < 2000; i++) h.add(sample(i < 560 ? 3000 : 600, 25, 50, false));
  TEST_ASSERT_EQUAL(AirHistory::kMinutes, h.size());
  TEST_ASSERT_EQUAL_FLOAT(600, h.summarize().co2Max);  // the old stale minutes have rolled off
}

void test_header_matches_row() {
  int commas = 0;
  for (const char* p = kMarkHeader; *p; p++) commas += *p == ',';
  static AirHistory h;
  h = AirHistory();
  h.add(sample(700, 24, 60, false, 910));
  char row[160];
  formatMarkRow("+5", h.summarize(), row, sizeof(row));
  int rowCommas = 0;
  for (const char* p = row; *p; p++) rowCommas += *p == ',';
  TEST_ASSERT_EQUAL(commas, rowCommas);
}

void test_hourly_rollup() {
  HourAccumulator a;
  char out[100];
  TEST_ASSERT_EQUAL(0, a.format("2026-11-02 15", out, sizeof(out)));  // nothing yet, nothing written
  for (int i = 0; i < 60; i++) a.add(sample(i < 30 ? 600 : 1000, 26, 50, i >= 50, i == 59 ? 909.4f : 0), i == 40);
  a.format("2026-11-02 15", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("2026-11-02 15,60,800,1000,26.0,50,10,909.4,1", out);
  a.reset();
  TEST_ASSERT_TRUE(a.empty());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_summary_of_a_day);
  RUN_TEST(test_short_history_and_unknown_pressure);
  RUN_TEST(test_history_keeps_only_the_last_day);
  RUN_TEST(test_header_matches_row);
  RUN_TEST(test_hourly_rollup);
  return UNITY_END();
}
