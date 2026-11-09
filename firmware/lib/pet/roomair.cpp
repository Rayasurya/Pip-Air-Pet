#include "roomair.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>

RoomAir::RoomAir(Config c) : c_(c) {
  memset(buf_, 0, sizeof(buf_));
  for (int i = 0; i < kDays; i++) dayMin_[i] = 1e9;
  leak_ = c.leakPerHourDefault;
  pfNmax_ = c.maxPeople > 0 ? c.maxPeople : c.volumeM3 * 0.25f;
  pfGppm_ = c.genLsPerPerson * 60.0f / (c.volumeM3 * 1000.0f) * 1e6f;   // ppm per minute per person
  pfInit();
}

// ---- v2 particle filter ----
float RoomAir::uni() {   // xorshift32, uniform in [0,1)
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return (rng_ >> 8) * (1.0f / 16777216.0f);
}
float RoomAir::gauss() {   // sum of 4 uniforms, unit variance
  return (uni() + uni() + uni() + uni() - 2.0f) * 1.7320508f;
}
void RoomAir::pfInit() {
  const float lo = logf(kPfLeakPriorLow), hi = logf(kPfLeakPriorHigh);
  for (int i = 0; i < kPf; i++) {
    pfN_[i] = pfE_[i] = pfS_[i] = 0;
    pfL_[i] = lo + uni() * (hi - lo);
    pfB_[i] = gauss() * kPfOffsetSd;
    pfW_[i] = 1.0f / kPf;
  }
}

// One minute: move each particle (people, ln lambda, offset, true excess CO2, sensor state), weigh it by how well it
// explains the reading, report weighted quantiles, resample if the weights collapsed.
void RoomAir::pfStep(double co2) {
  const float lo = logf(kPfLeakPriorLow), hi = logf(kPfLeakPriorHigh);
  const float clo = logf(kPfLeakClipLow), chi = logf(kPfLeakClipHigh);
  const float lagK = 1.0f - expf(-1.0f);
  const float y = (float)co2 - kPfRefPpm;
  float maxLl = -1e30f;
  float ll[kPf];
  for (int i = 0; i < kPf; i++) {
    float n = pfN_[i];
    if (uni() < kPfJumpPeople) n = uni() < 0.5f ? 0.0f : uni() * pfNmax_;
    else n += kPfWalkAbs * gauss() + kPfWalkRel * n * gauss();
    n = n < 0 ? 0 : (n > pfNmax_ ? pfNmax_ : n);
    float L = pfL_[i];
    if (uni() < kPfJumpLeak) L = lo + uni() * (hi - lo);
    else L += kPfWalkLeak * gauss();
    L = L < clo ? clo : (L > chi ? chi : L);
    const float b = pfB_[i] + kPfOffsetWalk * gauss();
    const float lam = expf(L) / 60.0f, e = expf(-lam);
    const float E = pfE_[i] * e + n * pfGppm_ * (1.0f - e) / lam;
    const float s = pfS_[i] + (E - pfS_[i]) * lagK;
    pfN_[i] = n; pfL_[i] = L; pfB_[i] = b; pfE_[i] = E; pfS_[i] = s;
    const float d = (y - b - s) / kPfSigmaY;
    ll[i] = -0.5f * d * d;
    if (ll[i] > maxLl) maxLl = ll[i];
  }
  float sum = 0;
  for (int i = 0; i < kPf; i++) { pfW_[i] *= expf(ll[i] - maxLl); sum += pfW_[i]; }
  float ess = 0;
  for (int i = 0; i < kPf; i++) { pfW_[i] /= sum; ess += pfW_[i] * pfW_[i]; }
  // weighted quantiles of people
  for (int i = 0; i < kPf; i++) pfSort_[i] = {pfN_[i], pfW_[i]};
  std::sort(pfSort_, pfSort_ + kPf, [](const Pair& a, const Pair& b) { return a.v < b.v; });
  float cum = 0; bool gl = false, gm = false, gh = false;
  for (int i = 0; i < kPf; i++) {
    cum += pfSort_[i].w;
    if (!gl && cum >= kPfQLow) { pfLow_ = pfSort_[i].v; gl = true; }
    if (!gm && cum >= 0.5f) { pfMid_ = pfSort_[i].v; gm = true; }
    if (!gh && cum >= kPfQHigh) { pfHigh_ = pfSort_[i].v; gh = true; }
  }
  if (!gl) pfLow_ = pfSort_[kPf - 1].v;
  if (!gm) pfMid_ = pfSort_[kPf - 1].v;
  if (!gh) pfHigh_ = pfSort_[kPf - 1].v;
  // weighted median fresh air per person q = lambda V / n, and weighted mean lambda
  float lamMean = 0;
  for (int i = 0; i < kPf; i++) {
    const float lamH = expf(pfL_[i]);
    lamMean += pfW_[i] * lamH;
    const float n = pfN_[i] < 0.5f ? 0.5f : pfN_[i];
    pfSort_[i] = {lamH * c_.volumeM3 * 1000.0f / 3600.0f / n, pfW_[i]};
  }
  pfLeak_ = lamMean;
  std::sort(pfSort_, pfSort_ + kPf, [](const Pair& a, const Pair& b) { return a.v < b.v; });
  cum = 0; pfFresh_ = pfSort_[kPf - 1].v;
  for (int i = 0; i < kPf; i++) { cum += pfSort_[i].w; if (cum >= 0.5f) { pfFresh_ = pfSort_[i].v; break; } }
  if (pfFresh_ > kFreshAirCap) pfFresh_ = kFreshAirCap;
  // systematic resampling when the effective number of particles is low
  if (1.0f / ess < kPf * kPfEssFrac) {
    const float u0 = uni();
    float c = pfW_[0]; int j = 0;
    for (int i = 0; i < kPf; i++) {
      const float u = (u0 + i) / kPf;
      while (u > c && j < kPf - 1) { j++; c += pfW_[j]; }
      pfT_[0][i] = pfN_[j]; pfT_[1][i] = pfE_[j]; pfT_[2][i] = pfS_[j]; pfT_[3][i] = pfL_[j]; pfT_[4][i] = pfB_[j];
    }
    for (int i = 0; i < kPf; i++) {
      pfN_[i] = pfT_[0][i]; pfE_[i] = pfT_[1][i]; pfS_[i] = pfT_[2][i]; pfL_[i] = pfT_[3][i]; pfB_[i] = pfT_[4][i];
      pfW_[i] = 1.0f / kPf;
    }
  }
}

float RoomAir::massBalancePeople(float volumeM3, float gen, float slopePpmPerMin, float excessPpm, float lam) {
  const double n = volumeM3 * 1000.0 * 1e-6 * (slopePpmPerMin / 60.0 + lam / 3600.0 * excessPpm) / gen;
  return n < 0 ? 0.0f : (float)n;
}

double RoomAir::at(int ago) const {
  int i = head_ - 1 - ago;
  while (i < 0) i += kBuf;
  return buf_[i];
}

// Least-squares line through the newest m samples (m >= 3). Time is in minutes, 0 = oldest of the m.
// slope: ppm per minute. endFit: the line's value at the newest sample. resid: std of the residuals.
void RoomAir::fit(int m, double& slope, double& endFit, double& resid) const {
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int i = 0; i < m; i++) {
    const double y = at(m - 1 - i);
    sx += i; sy += y; sxx += (double)i * i; sxy += i * y;
  }
  const double d = m * sxx - sx * sx;
  slope = (m * sxy - sx * sy) / d;
  const double b = (sy - slope * sx) / m;
  endFit = b + slope * (m - 1);
  double ss = 0;
  for (int i = 0; i < m; i++) {
    const double r = at(m - 1 - i) - (b + slope * i);
    ss += r * r;
  }
  resid = sqrt(ss / (m - 2));
}

// Outdoor CO2: rolling minimum of 5-minute means over the last days, clamped to 400..500. Default 420 at first.
double RoomAir::outdoor() const {
  if (minutes_ < kOutdoorWarmup) return kOutdoorDefault;
  double m = curDayMin_;
  for (int i = 0; i < dayCount_; i++) if (dayMin_[i] < m) m = dayMin_[i];
  if (m > 1e8) return kOutdoorDefault;
  if (m < kOutdoorMin) m = kOutdoorMin;
  if (m > kOutdoorMax) m = kOutdoorMax;
  return m;
}

void RoomAir::addMinute(float co2Ppm) {
  double v = co2Ppm;
  if (v < 0) v = 0;
  if (v > 40000) v = 40000;
  buf_[head_] = v;
  head_ = (head_ + 1) % kBuf;
  if (count_ < kBuf) count_++;
  minutes_++;
  if (count_ >= 5) {
    double s = 0;
    for (int i = 0; i < 5; i++) s += at(i);
    s /= 5;
    if (s < curDayMin_) curDayMin_ = s;
  }
  if (++dayMinute_ >= 1440) {   // close the day: keep its minimum, drop the oldest beyond kDays
    if (dayCount_ < kDays) {
      dayMin_[dayCount_++] = curDayMin_;
    } else {
      for (int i = 1; i < kDays; i++) dayMin_[i - 1] = dayMin_[i];
      dayMin_[kDays - 1] = curDayMin_;
    }
    curDayMin_ = 1e9;
    dayMinute_ = 0;
  }
  learnDecay();
  pfStep(v);
}

// Learn the air-change rate from a clean decay: after people leave, excess CO2 falls as exp(-lambda t).
// ln(excess) vs time is a line with slope -lambda. Weighted by excess^2 (noise in ln is sigma/excess).
void RoomAir::learnDecay() {
  if (count_ < kDecayWin) return;
  const double cout = outdoor();
  double e[kDecayWin];
  int n = 0;
  for (int i = kDecaySkip; i < kDecayWin; i++) {          // oldest first
    const double ex = at(kDecayWin - 1 - i) - cout;
    if (ex < kDecayMinExcess) break;                      // keep only the leading run still above the noise
    e[n++] = ex;
  }
  if (n < kDecayMinPoints || e[0] < kDecayStartExcess) return;
  // Falling in three blocks, and still falling in the second half (a plateau means people are still there).
  const int b = n / 3;
  double m1 = 0, m2 = 0, m3 = 0;
  for (int i = 0; i < b; i++) { m1 += e[i]; m2 += e[b + i]; m3 += e[n - b + i]; }
  m1 /= b; m2 /= b; m3 /= b;
  if (!(m1 > m2 && m2 > m3)) return;
  const double s1 = log(m2 / m1), s2 = log(m3 / m2);     // both negative
  if (s2 > 0.8 * s1) return;   // 0.8 chosen on dev rooms; a clean exponential gives s2 = s1
  double sw = 0, swx = 0, swy = 0, swxx = 0, swxy = 0;
  for (int i = 0; i < n; i++) {
    const double w = e[i] * e[i], y = log(e[i]);
    sw += w; swx += w * i; swy += w * y; swxx += w * i * i; swxy += w * i * y;
  }
  const double slope = (sw * swxy - swx * swy) / (sw * swxx - swx * swx);  // per minute
  const double lam = -slope * 60.0;
  if (!(lam >= kLeakMin && lam <= kLeakMax)) return;
  if (leakLearned_ && minutes_ - leakAt_ <= 3) leak_ = 0.7 * leak_ + 0.3 * lam;  // same decay seen again: smooth
  else leak_ = lam;
  leakLearned_ = true;
  leakAt_ = minutes_;
}

uint8_t RoomAir::riskFor(float p) {
  if (p < kRisk1) return 0;
  if (p < kRisk2) return 1;
  if (p < kRisk3) return 2;
  return 3;
}

RoomAirReport RoomAir::report() const {
  RoomAirReport r;
  memset(&r, 0, sizeof(r));
  r.outdoorPpm = (float)outdoor();
  r.leakPerHour = (float)leak_;
  r.leakLearned = leakLearned_;
  if (count_ < kValidMin) return r;
  r.valid = true;
  const double cout = outdoor();
  double slope10, end10, res10;
  fit(kFitMin, slope10, end10, res10);
  const double corr = end10 + kLagMin * slope10;           // undo the sensor lag
  double excess = corr - cout;
  if (excess < 0) excess = 0;
  r.excessPpm = (float)excess;
  r.rebreathedPct = (float)(100.0 * excess / c_.exhaledPpm);
  r.risk = riskFor(r.rebreathedPct);
  r.occupied = excess >= kOccupiedExcessPpm;

  // Fresh air per person, steady-state CO2 method (ASTM D6245): q = G / (C - Cout), CO2 as a fraction.
  if (r.occupied) {
    double q = c_.genLsPerPerson / (excess * 1e-6);
    if (q > kFreshAirCap) q = kFreshAirCap;
    r.freshAirLsPerPerson = (float)q;
    double s20, e20, r20;
    fit(kSteadyMin, s20, e20, r20);
    double lim = kSteadyFrac * excess;
    if (lim < kSteadyFloorPpm) lim = kSteadyFloorPpm;
    r.steady = fabs(s20) * kSteadyMin <= lim;
  }

  // Headcount and a fresh-air value for moments when CO2 is still moving: from the particle filter.
  r.peopleLow = pfLow_;
  r.peopleMid = pfMid_;
  r.peopleHigh = pfHigh_;
  r.leakFilterPerHour = pfLeak_;
  if (r.occupied && !r.steady) r.freshAirLsPerPerson = pfFresh_;
  return r;
}

static void put(char* out, size_t n, const char* s) {
  snprintf(out, n, "%s", s);
  if (n > 21) out[21] = 0;   // at most 21 characters
}

void RoomAir::advice(const RoomAirReport& r, char* l1, char* l2, char* l3, size_t n) {
  char buf[48];
  if (!r.valid) {
    put(l1, n, "WARMING UP");
    put(l2, n, "WAIT 20 MIN");
    put(l3, n, "");
    return;
  }
  if (!r.occupied) {
    put(l1, n, "ROOM EMPTY OR FRESH");
    put(l2, n, "");
    put(l3, n, "AIR IS GOOD");
    return;
  }
  if (r.steady) {
    const float q = r.freshAirLsPerPerson;
    if (q >= 10.0f) snprintf(buf, sizeof(buf), "FRESH AIR %.0f L/S/P", q);
    else snprintf(buf, sizeof(buf), "FRESH AIR %.1f L/S/P", q);
  } else {
    snprintf(buf, sizeof(buf), "AIR SETTLING");
  }
  put(l1, n, buf);
  snprintf(buf, sizeof(buf), "TARGET %.0f", kTargetLsPerPerson);
  put(l2, n, buf);
  switch (r.risk) {
    case 0: put(l3, n, "AIR IS GOOD"); break;
    case 1: put(l3, n, "OPEN A WINDOW"); break;
    case 2: put(l3, n, "OPEN WINDOWS"); break;
    default: put(l3, n, "OPEN WINDOWS NOW"); break;
  }
}
