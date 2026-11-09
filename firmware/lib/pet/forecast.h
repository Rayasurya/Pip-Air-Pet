// "Stuffy soon" forecast. Feed one reading per minute; it predicts CO2 20 minutes ahead.
// The model is trained on the Mac (ai/forecast/train.py) and pasted in as forecast_model.h.
// Feature maths MUST match ai/forecast/features.py; a parity test checks it.
// Pure C++, unit tested on the Mac.
#pragma once
#include <stdint.h>

struct ForecastResult {
  bool ready = false;    // enough history yet (31 minutes)
  bool learned = false;  // true: the trained model; false: straight-line fallback (no clock)
  bool alert = false;    // CO2 is below the limit now but predicted to cross it
  float co2 = 0;         // smoothed CO2 now
  float delta = 0;       // predicted change over the next 20 minutes, ppm
  float risePerMin = 0;  // how fast CO2 rose over the last 10 minutes
  int etaMin = 0;        // rough minutes until it crosses (when alert)
};

class Forecaster {
 public:
  static const int kHistory = 31;
  static const int kHorizon = 20;
  static const int kFeatures = 12;

  // One call per minute with that minute's average readings.
  void addMinute(float co2Raw, float ldr, float tempC, float humidity);
  bool ready() const { return count_ >= kHistory; }

  // minuteOfDay 0..1439. Without a real clock, pass clockValid=false: the
  // learned model needs time of day, so it falls back to the trend line.
  ForecastResult predict(uint16_t limitPpm, int minuteOfDay, bool weekend, bool clockValid) const;

  void features(float out[kFeatures], int minuteOfDay, bool weekend) const;
  static float runModel(const float* x);  // predicted 20-minute CO2 change, ppm

 private:
  // value `ago` minutes back (0 = newest)
  float at(const float* buf, int ago) const { return buf[(head_ - 1 - ago + 2 * kHistory) % kHistory]; }

  float co2_[kHistory] = {0}, ldr_[kHistory] = {0}, temp_[kHistory] = {0}, hum_[kHistory] = {0};
  int head_ = 0;
  int count_ = 0;
  // Smoothing state: the last two raw values and the running average.
  float raw1_ = 0, raw2_ = 0;
  float ema_ = 0;
  int rawSeen_ = 0;
};
