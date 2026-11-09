// Outdoor air pressure from Open-Meteo (free, no key), for the body diary. Homes are not airtight,
// so the room's pressure follows the outdoor pressure; no barometer part is needed.
// Pure C++ (parsing and URL), unit tested on the Mac. The HTTPS request itself lives in main.cpp.
#pragma once
#include <stddef.h>

// Only a coarse location is sent (1 decimal place, about 10 km): enough for weather, not a home.
int pressureUrl(float lat, float lon, char* out, size_t n);

// Reads current.surface_pressure (hPa) from an Open-Meteo "current" response.
// Returns false if it is missing or not a plausible value (600-1100 hPa).
bool parseSurfacePressure(const char* json, float& hpa);
