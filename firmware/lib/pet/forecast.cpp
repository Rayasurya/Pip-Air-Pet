#include "forecast.h"

#include <math.h>

#include "forecast_model.h"

namespace {
float median3(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}
}  // namespace

void Forecaster::addMinute(float co2Raw, float ldr, float tempC, float humidity) {
  // Same filter as features.smooth_co2: median of the last 3 raw minutes, then EMA 0.5.
  const float med = rawSeen_ >= 2 ? median3(raw2_, raw1_, co2Raw) : co2Raw;
  raw2_ = raw1_;
  raw1_ = co2Raw;
  ema_ = rawSeen_ == 0 ? med : 0.5f * ema_ + 0.5f * med;
  rawSeen_++;

  co2_[head_] = ema_;
  ldr_[head_] = ldr;
  temp_[head_] = tempC;
  hum_[head_] = humidity;
  head_ = (head_ + 1) % kHistory;
  if (count_ < kHistory) count_++;
}

void Forecaster::features(float f[kFeatures], int minuteOfDay, bool weekend) const {
  const float c0 = at(co2_, 0), c5 = at(co2_, 5), c10 = at(co2_, 10), c30 = at(co2_, 30);
  const float tau = 6.283185307f * minuteOfDay / 1440.0f;
  f[0] = (c0 - 800.0f) / 300.0f;
  f[1] = (c0 - c5) / 50.0f;
  f[2] = (c0 - c10) / 100.0f;
  f[3] = (c0 - c30) / 200.0f;
  f[4] = ((c0 - c5) - (c5 - c10)) / 30.0f;
  f[5] = sinf(tau);
  f[6] = cosf(tau);
  f[7] = weekend ? 1.0f : 0.0f;
  f[8] = (at(ldr_, 0) - 2000.0f) / 2000.0f;
  f[9] = (at(ldr_, 0) - at(ldr_, 10)) / 1000.0f;
  f[10] = (at(temp_, 0) - at(temp_, 10)) / 1.0f;
  f[11] = (at(hum_, 0) - at(hum_, 10)) / 5.0f;
}

float Forecaster::runModel(const float* x) {
#if defined(FORECAST_KIND_MLP)
  float h1[16], h2[8];
  for (int j = 0; j < 16; j++) {
    float s = kB1[j];
    for (int i = 0; i < kFeatures; i++) s += x[i] * kW1[i * 16 + j];
    h1[j] = s > 0 ? s : 0;
  }
  for (int j = 0; j < 8; j++) {
    float s = kB2[j];
    for (int i = 0; i < 16; i++) s += h1[i] * kW2[i * 8 + j];
    h2[j] = s > 0 ? s : 0;
  }
  float y = kB3[0];
  for (int i = 0; i < 8; i++) y += h2[i] * kW3[i];
  return y;
#elif defined(FORECAST_KIND_LINEAR)
  float y = kLinB;
  for (int i = 0; i < kFeatures; i++) y += x[i] * kLinW[i];
  return y;
#else
  return x[2] * 100.0f * 2.0f;  // trend: last 10-minute rise, extended 20 minutes
#endif
}

ForecastResult Forecaster::predict(uint16_t limit, int minuteOfDay, bool weekend, bool clockValid) const {
  ForecastResult r;
  if (!ready()) return r;
  r.ready = true;
  float f[kFeatures];
  features(f, minuteOfDay, weekend);
  r.co2 = at(co2_, 0);
  r.risePerMin = (r.co2 - at(co2_, 10)) / 10.0f;
  r.learned = clockValid;
  r.delta = clockValid ? runModel(f) : r.risePerMin * kHorizon;
  r.alert = r.co2 < limit && r.co2 + r.delta >= limit;
  if (r.alert) {
    const float perMin = r.delta / kHorizon;
    int eta = perMin > 0.1f ? (int)lroundf((limit - r.co2) / perMin) : kHorizon;
    r.etaMin = eta < 1 ? 1 : (eta > kHorizon ? kHorizon : eta);
  }
  return r;
}
