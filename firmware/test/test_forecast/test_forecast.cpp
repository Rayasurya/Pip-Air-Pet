// Forecast tests: the C++ port must match the Mac-trained model and Python features.
#include <math.h>
#include <unity.h>

#include "forecast.h"
#include "forecast_model.h"

void setUp() {}
void tearDown() {}

void test_model_matches_python() {
  for (int k = 0; k < kParityN; k++) {
    const float y = Forecaster::runModel(&kParityX[k * kForecastFeatures]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f + 1e-4f * fabsf(kParityY[k]), kParityY[k], y);
  }
}

void test_features_match_python() {
  Forecaster f;
  for (int i = 0; i < 60; i++) f.addMinute(kSeriesCo2[i], kSeriesLdr[i], kSeriesTemp[i], kSeriesHum[i]);
  float x[Forecaster::kFeatures];
  f.features(x, kSeriesMinute, kSeriesWeekend != 0);
  for (int i = 0; i < Forecaster::kFeatures; i++) TEST_ASSERT_FLOAT_WITHIN(1e-3f, kSeriesFeats[i], x[i]);
}

void test_needs_history() {
  Forecaster f;
  for (int i = 0; i < 30; i++) f.addMinute(800, 2000, 25, 60);
  TEST_ASSERT_FALSE(f.predict(1000, 600, false, true).ready);
  f.addMinute(800, 2000, 25, 60);
  TEST_ASSERT_TRUE(f.predict(1000, 600, false, true).ready);
}

void test_rising_room_alerts_and_flat_room_does_not() {
  Forecaster rising, over, flat;
  for (int i = 0; i < 40; i++) {
    rising.addMinute(560 + 9.0f * i, 1500, 26, 65);  // one person working, window shut: ends ~911 ppm
    over.addMinute(700 + 9.0f * i, 1500, 26, 65);    // ends ~1051 ppm, already stuffy
    flat.addMinute(650, 1500, 26, 65);
  }
  ForecastResult a = rising.predict(1000, 11 * 60, false, false);  // trend fallback
  TEST_ASSERT_TRUE(a.alert);
  TEST_ASSERT_TRUE(a.etaMin >= 5 && a.etaMin <= 15);
  TEST_ASSERT_FALSE(flat.predict(1000, 11 * 60, false, false).alert);
  TEST_ASSERT_FALSE(flat.predict(1000, 11 * 60, false, true).alert);
  // Already over the limit: no "soon" alert (the stuffy mood takes over).
  TEST_ASSERT_FALSE(over.predict(1000, 11 * 60, false, false).alert);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_model_matches_python);
  RUN_TEST(test_features_match_python);
  RUN_TEST(test_needs_history);
  RUN_TEST(test_rising_room_alerts_and_flat_room_does_not);
  return UNITY_END();
}
