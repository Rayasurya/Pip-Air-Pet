#include "leftbehind.h"

#include <math.h>
#include <string.h>

LeftBehind::LeftBehind(Config c) : cfg_(c) {
  for (int i = 0; i < kLbGrid; i++) grid_[i] = 0;
  reset();
}

void LeftBehind::reset() {
  state_ = LBState::Driving;
  level_ = LBLevel::None;
  dismissed_ = false;
  head_ = 0;
  count_ = 0;
  streak_ = 0;
  levelSince_ = 0;
  mOn_ = false;
  mN_ = 0;
  nextModel_ = 0;
  modelScore_ = 0;
  // v4 priors until setIgnition(false) applies the learned ones.
  static const double kV4[kLbGrid] = {0.3, 0.5, 0.8, 1.3, 2.2, 3.6, 6.0};
  for (int i = 0; i < kLbGrid; i++) grid_[i] = kV4[i];
  for (int i = 0; i < kLbGrid - 1; i++) pairOn_[i] = true;
  asymMax_ = cfg_.modelAsymptoteMaxPpm;
  modelThr_ = cfg_.modelThreshold;
  outdoor_ = cfg_.outdoorDefaultPpm;
  minAvg_ = 1e9f;
  prod_ = 0;
  outOfSpec_ = false;
  radarStill_ = radarHolding_ = radarAlerted_ = false;
  radarT_ = radarSince_ = 0;
  tOff_ = tSettled_ = lastT_ = nextEval_ = 0;
}

void LeftBehind::setIgnition(bool on, uint32_t tSec) {
  reset();
  if (on) {
    parkLearn_ = false;
  } else {
    state_ = LBState::Settling;
    tOff_ = tSec;
    lastT_ = tSec;
    applyPriors();
    lOn_ = false;
    parkLearn_ = true;
    everAlerted_ = false;
  }
}

void LeftBehind::dismiss(uint32_t) {
  if (state_ == LBState::Driving) return;
  dismissed_ = true;
  everAlerted_ = true;
  state_ = LBState::Watching;
  level_ = LBLevel::None;
  streak_ = 0;
}

void LeftBehind::push(uint32_t t, float co2, float temp) {
  ring_[head_] = {t, co2, temp};
  head_ = (head_ + 1) % kRing;
  if (count_ < kRing) count_++;
}

const LeftBehind::Sample& LeftBehind::at(size_t back) const {
  return ring_[(head_ + kRing - 1 - back) % kRing];
}

// Least-squares line through the samples with fromT <= t and t >= now - windowSec.
LeftBehind::Fit LeftBehind::fit(uint32_t now, uint32_t fromT, uint32_t windowSec) const {
  Fit f;
  const uint32_t lo = now > windowSec ? now - windowSec : 0;
  const uint32_t start = lo > fromT ? lo : fromT;
  // Use minutes relative to the newest sample so floats stay small.
  double st = 0, sy = 0;
  int n = 0;
  for (size_t i = 0; i < count_; i++) {
    const Sample& s = at(i);
    if (s.t < start) break;
    st += ((double)s.t - (double)now) / 60.0;
    sy += s.co2;
    n++;
  }
  if (n < 6) return f;
  const double mt = st / n, my = sy / n;
  double sxx = 0, sxy = 0;
  for (size_t i = 0; i < count_; i++) {
    const Sample& s = at(i);
    if (s.t < start) break;
    const double dt = ((double)s.t - (double)now) / 60.0 - mt;
    sxx += dt * dt;
    sxy += dt * (s.co2 - my);
  }
  if (sxx < 1e-6) return f;
  const double slope = sxy / sxx;
  double sse = 0;
  for (size_t i = 0; i < count_; i++) {
    const Sample& s = at(i);
    if (s.t < start) break;
    const double dt = ((double)s.t - (double)now) / 60.0 - mt;
    const double r = s.co2 - (my + slope * dt);
    sse += r * r;
  }
  f.n = n;
  f.slope = (float)slope;
  f.atNow = (float)(my + slope * (0.0 - mt));
  f.se = n > 2 ? (float)sqrt(sse / (n - 2) / sxx) : 0.0f;
  f.ok = true;
  return f;
}

// Outdoor estimate: the lowest 30 s average seen since ignition off (doors open flush the cabin
// toward outdoor air), clamped to the allowed range. It can only move down.
void LeftBehind::trackOutdoor() {
  if (count_ < 6) return;
  float s = 0;
  for (size_t i = 0; i < 6; i++) s += at(i).co2;
  const float avg = s / 6.0f;
  if (avg < minAvg_) minAvg_ = avg;
  float o = minAvg_;
  if (o < cfg_.outdoorMinPpm) o = cfg_.outdoorMinPpm;
  if (o > cfg_.outdoorMaxPpm) o = cfg_.outdoorMaxPpm;
  outdoor_ = o;
}

// Temperature change scaled to 10 min, from the oldest sample inside the last 10 min.
// Returns 0 until there are 4 min of data.
float LeftBehind::tempRise10(uint32_t now) const {
  const uint32_t lo = now > 600 ? now - 600 : 0;
  const Sample* old = nullptr;
  for (size_t i = 0; i < count_; i++) {
    const Sample& s = at(i);
    if (s.t < lo) break;
    old = &s;
  }
  if (!old) return 0;
  const uint32_t span = now - old->t;
  if (span < 240) return 0;
  return (at(0).temp - old->temp) * 600.0f / (float)span;
}


// ---- Model-comparison rule ---------------------------------------------------------------
// Air-change rates tried (grid_, per hour): v4 uses 0.3-1.5 for a closed car and 3-6 for a cracked
// window. v5 narrows them to a band around the learned leak (applyPriors).
static const double kTau = 60.0;  // sensor response time, seconds

// Add one reading to the running sums.
void LeftBehind::modelAdd(uint32_t t, float co2) {
  double dt = 5.0;
  if (!mOn_) {
    mOn_ = true;
    mT0_ = t;
    mN_ = 0;
    for (int i = 0; i < kDim; i++) {
      mV_[i] = 0;
      for (int j = 0; j < kDim; j++) mS_[i][j] = 0;
    }
    mYY_ = 0;
    for (int i = 0; i < kGrid; i++) {
      mDrive_[i] = 1.0;
      mG_[i] = 0.0;
    }
  } else {
    dt = (double)(t - mLast_);
    for (int i = 0; i < kGrid; i++) mDrive_[i] *= exp(-grid_[i] / 3600.0 * dt);
  }
  mLast_ = t;
  const double alpha = 1.0 - exp(-dt / kTau);
  double x[kDim];
  x[0] = 1.0;
  x[1] = exp(-((double)(t - mT0_) + 5.0) / kTau);  // sensor start state, decaying
  for (int i = 0; i < kGrid; i++) {
    mG_[i] += alpha * (mDrive_[i] - mG_[i]);
    x[2 + i] = mG_[i];
  }
  const double y = co2;
  for (int i = 0; i < kDim; i++) {
    for (int j = i; j < kDim; j++) mS_[i][j] += x[i] * x[j];
    mV_[i] += x[i] * y;
  }
  mYY_ += y * y;
  mN_++;
}

// Least squares on 4 chosen basis columns idx[0..3]. Columns whose bit is set in fixedMask are held
// at fixedVal. Returns the residual sum of squares and fills beta (all 4, fixed ones included).
static double fit4(const double S[kLbDim][kLbDim], const double* V, double yy, const int* idx,
                   unsigned fixedMask, const double* fixedVal, double* beta) {
  int fr[4], nf = 0;
  for (int a = 0; a < 4; a++) {
    if (fixedMask & (1u << a)) beta[a] = fixedVal[a];
    else fr[nf++] = a;
  }
  auto Sij = [&](int a, int b) { int i = idx[a], j = idx[b]; return i <= j ? S[i][j] : S[j][i]; };
  if (nf) {
    double M[4][5];
    for (int a = 0; a < nf; a++) {
      double r = V[idx[fr[a]]];
      for (int b = 0; b < 4; b++)
        if (fixedMask & (1u << b)) r -= Sij(fr[a], b) * fixedVal[b];
      for (int b = 0; b < nf; b++) M[a][b] = Sij(fr[a], fr[b]) + (a == b ? 1e-9 : 0.0);
      M[a][nf] = r;
    }
    for (int c = 0; c < nf; c++) {  // Gaussian elimination, matrix is positive definite
      const double d = M[c][c];
      for (int r = c + 1; r < nf; r++) {
        const double f = M[r][c] / d;
        for (int k = c; k <= nf; k++) M[r][k] -= f * M[c][k];
      }
    }
    for (int c = nf - 1; c >= 0; c--) {
      double r = M[c][nf];
      for (int k = c + 1; k < nf; k++) r -= M[c][k] * beta[fr[k]];
      beta[fr[c]] = r / M[c][c];
    }
  }
  double lin = 0, quad = 0;
  for (int a = 0; a < 4; a++) {
    lin += beta[a] * V[idx[a]];
    for (int b = 0; b < 4; b++) quad += beta[a] * beta[b] * Sij(a, b);
  }
  return yy - 2 * lin + quad;
}

float LeftBehind::modelScore() const {
  if (!mOn_ || mN_ < (int)cfg_.modelMinSamples) return 0;
  const double cmax = asymMax_;
  const double tiny = 1e-9;
  double best1 = 1e300, best0 = 1e300;
  for (int p = 0; p + 1 < kGrid; p++) {
    if (!pairOn_[p]) continue;
    const int idx[4] = {0, 1, 2 + p, 3 + p};
    double b[4];
    const double zero[4] = {0, 0, 0, 0};
    // H1: c free, decay weights both >= 0 or both <= 0.
    const double rU = fit4(mS_, mV_, mYY_, idx, 0, zero, b);
    const double bU[4] = {b[0], b[1], b[2], b[3]};
    double r1;
    if ((bU[2] >= -tiny && bU[3] >= -tiny) || (bU[2] <= tiny && bU[3] <= tiny)) {
      r1 = rU;
    } else {
      r1 = 1e300;
      for (int sgn = -1; sgn <= 1; sgn += 2) {
        for (unsigned m = 0; m < 4; m++) {  // bit0: weight 1 held at 0, bit1: weight 2 held at 0
          const unsigned fm = ((m & 1) ? 4u : 0u) | ((m & 2) ? 8u : 0u);
          const double q = fit4(mS_, mV_, mYY_, idx, fm, zero, b);
          if ((fm & 4u || sgn * b[2] >= -tiny) && (fm & 8u || sgn * b[3] >= -tiny) && q < r1) r1 = q;
        }
      }
    }
    if (r1 < best1) best1 = r1;
    // H0: c <= cmax, decay weights >= 0. The unconstrained fit is optimal when it is feasible.
    double r0;
    if (bU[0] <= cmax && bU[2] >= -tiny && bU[3] >= -tiny) {
      r0 = rU;
    } else {
      r0 = 1e300;
      double fv[4] = {cmax, 0, 0, 0};
      for (unsigned m = 0; m < 8; m++) {  // bit0: c held at cmax, bit1/bit2: weights held at 0
        const unsigned fm = ((m & 1) ? 1u : 0u) | ((m & 2) ? 4u : 0u) | ((m & 4) ? 8u : 0u);
        const double q = fit4(mS_, mV_, mYY_, idx, fm, fv, b);
        if ((fm & 1u || b[0] <= cmax + tiny) && (fm & 4u || b[2] >= -tiny) && (fm & 8u || b[3] >= -tiny) && q < r0) r0 = q;
      }
    }
    if (r0 < best0) best0 = r0;
  }
  if (best1 >= 1e299 || best0 >= 1e299) return 0;
  if (best1 < 1e-9) best1 = 1e-9;
  const double sig2 = best1 / (double)(mN_ - 4 > 1 ? mN_ - 4 : 1);
  const double lr = (best0 - best1) / sig2;
  return lr > 0 ? (float)lr : 0.0f;
}

void LeftBehind::setRadar(uint32_t tSec, uint8_t targetState, uint16_t stillCm, uint8_t stillEnergy) {
  radarT_ = tSec;
  radarStill_ = (targetState == 2 || targetState == 3) && stillCm > 0 && stillCm <= cfg_.radarCabinMaxCm &&
                stillEnergy >= cfg_.radarStillMinEnergy;
}

void LeftBehind::radarCheck(uint32_t t) {
  const bool fresh = radarT_ != 0 && t >= radarT_ && t - radarT_ <= cfg_.radarStaleSec;
  if (!(radarStill_ && fresh)) {
    radarHolding_ = false;
    return;
  }
  if (!radarHolding_) {
    radarHolding_ = true;
    radarSince_ = t;
  }
  if (t - radarSince_ >= cfg_.radarStillSec) {
    state_ = LBState::Alert;
    level_ = LBLevel::Check;
    radarAlerted_ = true;
  }
}

LBResult LeftBehind::result(float prod, float rise, float temp) {
  LBResult r{};
  r.state = state_;
  r.level = level_;
  r.productionPpmMin = prod;
  r.outdoorPpm = outdoor_;
  r.tempRise10Min = rise;
  r.outOfSpec = temp > cfg_.sensorMaxC;
  r.changed = (state_ != lastState_) || (level_ != lastLevel_);
  lastState_ = state_;
  lastLevel_ = level_;
  return r;
}

LBResult LeftBehind::update(uint32_t t, float co2, float temp) {
  if (state_ == LBState::Driving) return result(0, 0, temp);
  if (t <= lastT_ && count_ > 0) return result(prod_, 0, temp);  // duplicate or backwards time
  lastT_ = t;
  push(t, co2, temp);
  trackOutdoor();
  const float rise = tempRise10(t);
  if (dismissed_) return result(0, rise, temp);
  if (level_ == LBLevel::None && t >= tOff_ + cfg_.modelStartSec) {
    if (cfg_.modelRuleOn) modelAdd(t, co2);
    if (cfg_.learnOn) learnAdd(t, co2);
  }

  if (state_ == LBState::Settling) {
    const uint32_t since = t - tOff_;
    bool done = since >= cfg_.settleMaxSec;
    if (!done && since >= cfg_.settleMinSec) {
      const Fit f = fit(t, tOff_, cfg_.settleSlopeWindowSec);
      done = f.ok && f.slope > -cfg_.settleFallPpmMin;
    }
    if (done) {
      state_ = LBState::Watching;
      tSettled_ = t;
      nextEval_ = t;
    }
    return result(0, rise, temp);
  }

  if (state_ == LBState::Watching) radarCheck(t);

  // Watching or Alert. Use the last settleSlopeWindowSec of the settling period too, because its
  // slope was already checked to be flat enough, and it saves two minutes of waiting.
  const uint32_t from = tSettled_ > cfg_.settleSlopeWindowSec ? tSettled_ - cfg_.settleSlopeWindowSec : 0;
  const uint32_t fromT = from > tOff_ ? from : tOff_;
  const float vol = cfg_.cabinVolumeM3 > 0.5f ? cfg_.cabinVolumeM3 : cfg_.referenceVolumeM3;
  const float volScale = cfg_.referenceVolumeM3 / vol;  // 1 for a car, ~0.4 for a van
  const float prodThr = cfg_.productionThresholdPpmMin * volScale;
  float levelThr = cfg_.levelExcessPpm * volScale;
  if (levelThr < cfg_.levelExcessFloorPpm) levelThr = cfg_.levelExcessFloorPpm;
  const Fit f = fit(t, fromT, cfg_.windowSec);
  if (f.ok && (uint32_t)(f.n) * 1 > 0) {
    const float leak = cfg_.leakPerHour / 60.0f;  // per minute
    const float cNow = f.atNow + cfg_.lagSec / 60.0f * f.slope;  // project the lagging reading forward
    float excess = cNow - outdoor_;
    if (excess < 0) excess = 0;
    prod_ = f.slope + leak * excess;
  }

  if (state_ == LBState::Watching && f.ok && t >= nextEval_) {
    nextEval_ = t + cfg_.evalEverySec;
    const uint32_t span = t - (fromT > t - cfg_.windowSec ? fromT : t - cfg_.windowSec);
    const bool enough = span >= cfg_.minWindowSec;
    const bool hit = enough && prod_ > prodThr && (prod_ - cfg_.zMargin * f.se) > 0;
    if (hit) {
      if (++streak_ >= cfg_.confirmCount) {
        state_ = LBState::Alert;
        level_ = LBLevel::Check;
      }
    } else {
      streak_ = 0;
    }
    if (cfg_.modelRuleOn && state_ == LBState::Watching && t >= nextModel_) {
      nextModel_ = t + cfg_.evalEverySec;
      modelScore_ = modelScore();
      if (modelScore_ > modelThr_) {
        state_ = LBState::Alert;
        level_ = LBLevel::Check;
      }
    }
    if (cfg_.levelRuleOn && state_ == LBState::Watching) {
      const Fit g = fit(t, fromT, cfg_.levelWindowSec);
      const uint32_t gStart = t - (fromT > t - cfg_.levelWindowSec ? fromT : t - cfg_.levelWindowSec);
      bool holds = false;
      if (g.ok && gStart + 15 >= cfg_.levelWindowSec) {  // window (nearly) full
        const float cNow = g.atNow + cfg_.lagSec / 60.0f * g.slope;
        const float base = outdoor_ < cfg_.levelOutdoorCapPpm ? outdoor_ : cfg_.levelOutdoorCapPpm;
        const float ex = cNow - base;
        const float allow = -cfg_.levelDecayFrac * (cfg_.leakPerHour / 60.0f) * ex;
        holds = ex > levelThr && (g.slope - cfg_.levelZ * g.se) >= allow;
      }
      if (!holds) {
        levelSince_ = 0;
      } else {
        if (levelSince_ == 0) levelSince_ = t;
        if (t - levelSince_ >= cfg_.levelSustainSec) {
          state_ = LBState::Alert;
          level_ = LBLevel::Check;
        }
      }
    }
  }

  if (state_ == LBState::Alert) everAlerted_ = true;
  if (state_ == LBState::Alert && level_ != LBLevel::Urgent) {
    const bool urgent = temp >= cfg_.dangerC || (temp >= cfg_.hotC && rise >= cfg_.hotRiseC10) ||
                        rise >= cfg_.fastRiseC10;
    if (urgent) level_ = LBLevel::Urgent;  // latched until dismiss or the next ignition cycle
  }
  return result(prod_, rise, temp);
}


// ---- v5: priors, learner, saved state ----------------------------------------------------------
static const double kLamLo = 0.15, kLamHi = 8.0;
static double lamAt(int k) {
  static double tab[kLbLam];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < kLbLam; i++) tab[i] = kLamLo * pow(kLamHi / kLamLo, i / (kLbLam - 1.0));
    ready = true;
  }
  return tab[k];
}

float LeftBehind::medianOf(const float* v, uint8_t n) {
  float a[kLbHist];
  for (int i = 0; i < n; i++) a[i] = v[i];
  for (int i = 1; i < n; i++) {  // insertion sort
    const float x = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > x) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = x;
  }
  if (n == 0) return 0;
  if (n % 2) return a[n / 2];
  return (a[n / 2 - 1] + a[n / 2]) / 2.0f;
}

void LeftBehind::pushLearned(float* v, uint8_t& n, double x) {
  if (n >= kLbHist) {
    for (int i = 1; i < kLbHist; i++) v[i - 1] = v[i];
    n = kLbHist - 1;
  }
  v[n++] = (float)x;
}

// n rates spaced evenly in log between lo*median and hi*median, kept inside the assumed range.
void LeftBehind::band(float median, int n, double rmin, double rmax, double* out) const {
  double lo = (double)cfg_.learnLeakLo * (double)median;
  double hi = (double)cfg_.learnLeakHi * (double)median;
  lo = lo < rmin ? rmin : (lo > rmax ? rmax : lo);
  hi = hi > rmax ? rmax : (hi < rmin ? rmin : hi);
  if (hi < lo * 1.05) hi = lo * 1.05;
  for (int k = 0; k < n; k++) out[k] = lo * pow(hi / lo, k / (n - 1.0));
}

void LeftBehind::applyPriors() {
  static const double kV4[kLbGrid] = {0.3, 0.5, 0.8, 1.3, 2.2, 3.6, 6.0};
  for (int i = 0; i < kLbGrid; i++) grid_[i] = kV4[i];
  for (int i = 0; i < kLbGrid - 1; i++) pairOn_[i] = true;
  asymMax_ = cfg_.modelAsymptoteMaxPpm;
  modelThr_ = cfg_.modelThreshold;
  const bool haveB = lrn_.nB >= cfg_.learnMinBase;
  const bool haveC = lrn_.nC >= cfg_.learnMinLeak;
  const bool haveK = lrn_.nK >= cfg_.learnMinLeak;
  if (!cfg_.learnOn || !(haveB || haveC || haveK)) return;
  modelThr_ = cfg_.learnModelThreshold;
  if (haveB) {
    const float cap = medianOf(lrn_.b, lrn_.nB) + cfg_.learnBaseMargin;
    asymMax_ = cap < cfg_.modelAsymptoteMaxPpm ? cap : cfg_.modelAsymptoteMaxPpm;
  }
  double closed[3] = {0.3, 0.75, 1.5};
  double cracked[4] = {3.0, 4.0, 5.0, 6.0};
  if (haveC) band(medianOf(lrn_.c, lrn_.nC), 3, cfg_.closedLo, cfg_.closedHi, closed);
  if (haveK) band(medianOf(lrn_.k, lrn_.nK), 4, cfg_.crackedLo, cfg_.crackedHi, cracked);
  for (int i = 0; i < 3; i++) grid_[i] = closed[i];
  for (int i = 0; i < 4; i++) grid_[3 + i] = cracked[i];
  for (int i = 0; i < kLbGrid - 1; i++) pairOn_[i] = i != 2;
}

void LeftBehind::learnAdd(uint32_t t, float co2) {
  double dt = 5.0;
  if (!lOn_) {
    lOn_ = true;
    lT0_ = t;
    lN_ = 0;
    lSx1_ = lSx1x1_ = lV0_ = lV1_ = lYY_ = 0;
    for (int k = 0; k < kLbLam; k++) {
      lDrive_[k] = 1.0;
      lG_[k] = lSG_[k] = lSx1G_[k] = lSGG_[k] = lVG_[k] = 0;
    }
  } else {
    dt = (double)(t - lLast_);
    for (int k = 0; k < kLbLam; k++) lDrive_[k] *= exp(-lamAt(k) / 3600.0 * dt);
  }
  lLast_ = t;
  const double alpha = 1.0 - exp(-dt / kTau);
  const double x1 = exp(-((double)(t - lT0_) + 5.0) / kTau);
  const double y = co2;
  lN_++;
  lSx1_ += x1;
  lSx1x1_ += x1 * x1;
  lV0_ += y;
  lV1_ += x1 * y;
  lYY_ += y * y;
  for (int k = 0; k < kLbLam; k++) {
    lG_[k] += alpha * (lDrive_[k] - lG_[k]);
    const double g = lG_[k];
    lSG_[k] += g;
    lSx1G_[k] += x1 * g;
    lSGG_[k] += g * g;
    lVG_[k] += g * y;
  }
}

// Gaussian elimination, no pivoting, on an n x (n+1) augmented matrix. SPD, so safe.
static void solveSpd(double M[3][4], int n, double* x) {
  for (int c = 0; c < n; c++) {
    const double d = M[c][c];
    for (int r = c + 1; r < n; r++) {
      const double f = M[r][c] / d;
      for (int k = c; k <= n; k++) M[r][k] -= f * M[c][k];
    }
  }
  for (int c = n - 1; c >= 0; c--) {
    double r = M[c][n];
    for (int k = c + 1; k < n; k++) r -= M[c][k] * x[k];
    x[c] = r / M[c][c];
  }
}

// Fit [1, start state, decay k] with the decay weight >= 0. rss, fitted level, (X'X)^-1[0][0], columns used.
void LeftBehind::learnSolve(int k, double* rss, double* c0, double* inv00, int* npar) const {
  const double n = (double)lN_;
  const double S[3][3] = {{n, lSx1_, lSG_[k]}, {lSx1_, lSx1x1_, lSx1G_[k]}, {lSG_[k], lSx1G_[k], lSGG_[k]}};
  const double V[3] = {lV0_, lV1_, lVG_[k]};
  double M[3][4];
  double beta[3] = {0, 0, 0};
  int np = 3;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) M[i][j] = S[i][j] + (i == j ? 1e-9 : 0.0);
    M[i][3] = V[i];
  }
  solveSpd(M, 3, beta);
  if (beta[2] < 0.0) {
    np = 2;
    for (int i = 0; i < 2; i++) {
      for (int j = 0; j < 2; j++) M[i][j] = S[i][j] + (i == j ? 1e-9 : 0.0);
      M[i][2] = V[i];  // solveSpd with n = 2 reads the right-hand side from column 2
    }
    beta[0] = beta[1] = beta[2] = 0.0;
    solveSpd(M, 2, beta);
    beta[2] = 0.0;
  }
  double lin = 0;
  for (int a = 0; a < 3; a++) lin += beta[a] * V[a];
  double quad = 0;
  for (int a = 0; a < 3; a++)
    for (int b = 0; b < 3; b++) quad += beta[a] * beta[b] * S[a][b];
  *rss = lYY_ - 2 * lin + quad;
  *c0 = beta[0];
  *npar = np;
  // (X'X)^-1 [0][0] for the active columns
  double Mi[3][4];
  for (int i = 0; i < np; i++) {
    for (int j = 0; j < np; j++) Mi[i][j] = S[i][j] + (i == j ? 1e-9 : 0.0);
    Mi[i][np] = i == 0 ? 1.0 : 0.0;
  }
  double xi[3] = {0, 0, 0};
  solveSpd(Mi, np, xi);
  *inv00 = xi[0];
}

bool LeftBehind::learnFromParking() {
  if (!cfg_.learnOn || !parkLearn_) return false;
  parkLearn_ = false;
  if (everAlerted_ || dismissed_ || level_ != LBLevel::None || !lOn_) return false;
  if (lN_ < (int)cfg_.learnMinSamples) return false;
  double rss[kLbLam], c0[kLbLam], inv[kLbLam];
  int np[kLbLam];
  for (int k = 0; k < kLbLam; k++) learnSolve(k, &rss[k], &c0[k], &inv[k], &np[k]);
  int kb = 0;
  for (int k = 1; k < kLbLam; k++)
    if (rss[k] < rss[kb]) kb = k;
  const double dof = (double)(lN_ - np[kb]);
  const double sig2 = (rss[kb] > 0 ? rss[kb] : 0.0) / dof;
  if (sqrt(sig2) > (double)cfg_.learnMaxResidualSd) return false;
  const double base = c0[kb];
  if (!((double)cfg_.learnBaseMin <= base && base <= (double)cfg_.learnBaseMax)) return false;
  const Fit tail = fit(lastT_, 0, 600);
  if (!tail.ok || (double)tail.slope > (double)cfg_.learnTailZ * (double)tail.se) return false;
  bool learned = false;
  if (sqrt(sig2 * inv[kb]) <= (double)cfg_.learnMaxBaseSe) {
    pushLearned(lrn_.b, lrn_.nB, base);
    learned = true;
  }
  if (kb >= 2 && kb < kLbLam - 2) {
    const double d = (double)cfg_.learnDetectZ * sig2;
    if (rss[kb - 2] - rss[kb] >= d && rss[kb + 2] - rss[kb] >= d) {
      const double lam = lamAt(kb);
      if (lam < (double)cfg_.learnLeakSplit) pushLearned(lrn_.c, lrn_.nC, lam);
      else pushLearned(lrn_.k, lrn_.nK, lam);
      learned = true;
    }
  }
  return learned;
}

void LeftBehind::clearLearned() { lrn_ = Learned(); }

size_t LeftBehind::saveLearned(uint8_t* out, size_t cap) const {
  if (cap < kLbLearnedBytes) return 0;
  out[0] = 1;  // version
  out[1] = lrn_.nB;
  out[2] = lrn_.nC;
  out[3] = lrn_.nK;
  size_t o = 4;
  const float* lists[3] = {lrn_.b, lrn_.c, lrn_.k};
  const uint8_t counts[3] = {lrn_.nB, lrn_.nC, lrn_.nK};
  for (int l = 0; l < 3; l++) {
    for (int i = 0; i < kLbHist; i++) {
      const float v = i < counts[l] ? lists[l][i] : 0.0f;
      memcpy(out + o, &v, 4);  // little-endian on every target we build for
      o += 4;
    }
  }
  uint32_t sum = 0;
  for (size_t i = 0; i < o; i++) sum += out[i];
  out[o] = (uint8_t)(sum & 0xFF);
  out[o + 1] = (uint8_t)((sum >> 8) & 0xFF);
  return kLbLearnedBytes;
}

bool LeftBehind::loadLearned(const uint8_t* in, size_t n) {
  if (!in || n != kLbLearnedBytes || in[0] != 1) return false;
  uint32_t sum = 0;
  for (size_t i = 0; i < kLbLearnedBytes - 2; i++) sum += in[i];
  if ((sum & 0xFFFF) != (uint32_t)(in[kLbLearnedBytes - 2] | (in[kLbLearnedBytes - 1] << 8))) return false;
  const uint8_t counts[3] = {in[1], in[2], in[3]};
  for (int l = 0; l < 3; l++)
    if (counts[l] > kLbHist) return false;
  Learned t;
  t.nB = counts[0];
  t.nC = counts[1];
  t.nK = counts[2];
  float* lists[3] = {t.b, t.c, t.k};
  static const double lo[3] = {300.0, 0.05, 1.0}, hi[3] = {700.0, 3.0, 12.0};
  size_t o = 4;
  for (int l = 0; l < 3; l++) {
    for (int i = 0; i < kLbHist; i++) {
      float v;
      memcpy(&v, in + o, 4);
      o += 4;
      if (i < counts[l]) {
        if (!(v >= lo[l] && v <= hi[l])) return false;  // also rejects NaN
        lists[l][i] = v;
      }
    }
  }
  lrn_ = t;
  return true;
}
