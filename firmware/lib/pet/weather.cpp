#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pressureUrl(float lat, float lon, char* out, size_t n) {
  return snprintf(out, n, "https://api.open-meteo.com/v1/forecast?latitude=%.1f&longitude=%.1f&current=surface_pressure",
                  lat, lon);
}

bool parseSurfacePressure(const char* json, float& hpa) {
  if (!json) return false;
  // "current_units" also names surface_pressure (as "hPa"), so look inside "current":{ ... } only.
  const char* cur = strstr(json, "\"current\":{");
  if (!cur) return false;
  const char* key = strstr(cur, "\"surface_pressure\":");
  if (!key) return false;
  key += strlen("\"surface_pressure\":");
  char* end = nullptr;
  const float v = strtof(key, &end);
  if (end == key || v < 600 || v > 1100) return false;
  hpa = v;
  return true;
}
