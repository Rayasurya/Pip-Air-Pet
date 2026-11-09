// Body diary: "I feel unwell now" marks, each saved with what the air did in the 24 h before.
// Also an hourly roll-up, so months of air history fit in flash after the minute log rolls over.
// Pure C++, unit tested on the Mac.
#pragma once
#include <stddef.h>
#include <stdint.h>

// One minute of air, packed small (1440 of these = 14 KB of RAM).
struct AirSample {
  uint16_t co2 = 0;      // ppm
  int16_t temp10 = 0;    // C x 10
  uint16_t hum10 = 0;    // % x 10
  uint16_t ldr = 0;      // raw 0-4095
  uint16_t press10 = 0;  // outdoor air pressure, hPa x 10; 0 = unknown
};

// What the room was like before a mark. "Last 2 h" is kept apart: migraine warning signs
// (light sensitivity and the like) can start hours before the pain, so they are not causes.
struct MarkSummary {
  int minutes = 0;              // how much history there was (up to 1440)
  float co2Mean = 0, co2Max = 0;
  float hoursOver1400 = 0;      // stale-air hours
  float co2MeanLast2h = 0;
  float tempMin = 0, tempMax = 0;
  float tempChange6h = 0;       // now minus 6 h ago (0 if under 6 h of history)
  float humMean = 0;
  int darkMinutes = 0, darkMinutesLast2h = 0;
  bool pressKnown = false;
  float pressChange24h = 0;     // hPa, latest minus earliest known reading
};

class AirHistory {
 public:
  static const int kMinutes = 1440;
  void add(const AirSample& s);
  int size() const { return n_; }
  // The i-th oldest sample still held (0 = oldest).
  const AirSample& at(int i) const { return buf_[(head_ - n_ + i + kMinutes) % kMinutes]; }
  MarkSummary summarize() const;

 private:
  AirSample buf_[kMinutes];
  int head_ = 0;
  int n_ = 0;
};

bool isDark(uint16_t ldr);

extern const char* const kMarkHeader;
// One CSV row for /marks.csv. Returns the length written.
int formatMarkRow(const char* stamp, const MarkSummary& m, char* out, size_t n);

// Hourly averages for /hours.csv.
class HourAccumulator {
 public:
  void add(const AirSample& s, bool marked);
  bool empty() const { return n_ == 0; }
  int format(const char* hourStamp, char* out, size_t n) const;
  void reset();

 private:
  int n_ = 0;
  float co2Sum_ = 0, co2Max_ = 0, tempSum_ = 0, humSum_ = 0;
  int dark_ = 0, marks_ = 0;
  uint16_t lastPress10_ = 0;
};
extern const char* const kHourHeader;
