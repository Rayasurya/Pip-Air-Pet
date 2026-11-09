// Outdoor pressure parsing, against a real Open-Meteo response saved on 11 Oct 2026.
#include <string.h>
#include <unity.h>

#include "weather.h"

static const char* kReal =
    "{\"latitude\":12.970123,\"longitude\":77.56364,\"generationtime_ms\":66.8,\"utc_offset_seconds\":0,"
    "\"timezone\":\"GMT\",\"timezone_abbreviation\":\"GMT\",\"elevation\":914.0,\"current_units\":{\"time\":"
    "\"iso8601\",\"interval\":\"seconds\",\"surface_pressure\":\"hPa\",\"pressure_msl\":\"hPa\"},\"current\":{"
    "\"time\":\"2026-10-11T10:30\",\"interval\":900,\"surface_pressure\":913.1,\"pressure_msl\":1011.3}}";

void setUp() {}
void tearDown() {}

void test_reads_the_real_response() {
  float p = 0;
  TEST_ASSERT_TRUE(parseSurfacePressure(kReal, p));
  TEST_ASSERT_EQUAL_FLOAT(913.1f, p);
}

void test_rejects_bad_responses() {
  float p = 123;
  TEST_ASSERT_FALSE(parseSurfacePressure(nullptr, p));
  TEST_ASSERT_FALSE(parseSurfacePressure("", p));
  TEST_ASSERT_FALSE(parseSurfacePressure("{\"error\":true,\"reason\":\"rate limit\"}", p));
  TEST_ASSERT_FALSE(parseSurfacePressure("{\"current\":{\"surface_pressure\":null}}", p));
  TEST_ASSERT_FALSE(parseSurfacePressure("{\"current\":{\"surface_pressure\":12.5}}", p));  // not air pressure
  TEST_ASSERT_EQUAL_FLOAT(123, p);  // untouched on failure
}

void test_url_sends_only_a_coarse_location() {
  char url[160];
  pressureUrl(12.9716f, 77.5946f, url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.open-meteo.com/v1/forecast?latitude=13.0&longitude=77.6&current=surface_pressure", url);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_the_real_response);
  RUN_TEST(test_rejects_bad_responses);
  RUN_TEST(test_url_sends_only_a_coarse_location);
  return UNITY_END();
}
