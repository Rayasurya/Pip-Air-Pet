#include "kin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kin_presence.h"

namespace kin {

namespace {

uint8_t bit(Kind k) { return (uint8_t)(1u << (int)k); }

// Same as the Python reference: sorted values, linear interpolation at (n-1)*q.
double percentile(const double* xs, int n, double q) {
  double tmp[512];
  if (n <= 0) return 0;
  if (n > 512) n = 512;
  memcpy(tmp, xs, sizeof(double) * n);
  // insertion sort: n is small (<= 512) and this runs at most once every 30 minutes at night
  for (int i = 1; i < n; i++) {
    double v = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > v) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = v;
  }
  const double k = (n - 1) * q;
  const int lo = (int)floor(k), hi = (int)ceil(k);
  return tmp[lo] + (tmp[hi] - tmp[lo]) * (k - lo);
}

double percentileInt(const int* xs, int n, double q) {
  double d[64];
  if (n > 64) n = 64;
  for (int i = 0; i < n; i++) d[i] = xs[i];
  return percentile(d, n, q);
}

// 1-D two-means on a histogram (same as two_clusters in the Python reference).
bool twoClusters(const uint32_t* hist, int nb, double binW, double& lo, double& hi) {
  double n = 0;
  for (int i = 0; i < nb; i++) n += hist[i];
  if (n < 500) return false;
  lo = 1 * binW + binW / 2;
  hi = (nb / 3) * binW + binW / 2;
  double wh = 0;
  for (int it = 0; it < 30; it++) {
    double sl = 0, sh = 0, wl = 0;
    wh = 0;
    for (int i = 0; i < nb; i++) {
      const double c = i * binW + binW / 2;
      if (fabs(c - lo) <= fabs(c - hi)) { sl += c * hist[i]; wl += hist[i]; }
      else { sh += c * hist[i]; wh += hist[i]; }
    }
    if (wl == 0 || wh == 0) return false;
    lo = sl / wl;
    hi = sh / wh;
  }
  return !(hi - lo < 0.8 || wh < 0.05 * n);
}

void hhmm(long minuteOfDay, char* out) {
  long m = ((minuteOfDay % 1440) + 1440) % 1440;
  snprintf(out, 6, "%02ld:%02ld", m / 60, m % 60);
}

}  // namespace

const char* kindName(Kind k) {
  switch (k) {
    case Kind::Morning: return "morning";
    case Kind::Quiet: return "quiet";
    case Kind::Away: return "away";
    case Kind::Night: return "night";
    default: return "none";
  }
}

const char* levelName(Level l) {
  switch (l) {
    case Level::Soft: return "soft";
    case Level::Loud: return "loud";
    case Level::Family: return "family";
    default: return "resolved";
  }
}

void Engine::push(Alert out[3], int& n, long t, Kind k, Level l) const {
  if (n >= 3) return;
  out[n].valid = true;
  out[n].minute = t;
  out[n].kind = k;
  out[n].level = l;
  n++;
}

// ---------------- signals ----------------

void Engine::observe(double co2, int mod, bool emptyRoom) {
  ema_ = haveEma_ ? 0.5 * ema_ + 0.5 * co2 : co2;
  haveEma_ = true;
  co2Hist_[histHead_] = ema_;
  histHead_ = (histHead_ + 1) % kHist;
  if (histLen_ < kHist) histLen_++;
  if (ema_ < dayMin_) dayMin_ = ema_;
  (void)mod;
  // Learn the leak rate from clean decays: whenever the room is confirmed empty (after bedtime,
  // during outings) and CO2 is still clearly above the outdoor level.
  if (emptyRoom && histLen_ > 10) {
    const double excess = hist(5) - cout_;
    if (excess > 60) {
      const double rate = -(hist(0) - hist(10)) / 10 / excess;
      if (rate > 0 && rate < 0.2) {
        leakS_[leakHead_] = rate;
        leakHead_ = (leakHead_ + 1) % kLeak;
        if (nLeak_ < kLeak) nLeak_++;
        leakAdded_++;
        if (nLeak_ >= 60 && leakAdded_ % 30 == 0) leak_ = percentile(leakS_, nLeak_, 0.5);
      }
    }
  }
}

double Engine::generation() const {
  if (histLen_ <= 10) return 0.0;
  const double slope = (hist(0) - hist(10)) / 10;
  return slope + leak_ * (hist(5) - cout_);
}

void Engine::features(int ldr, int mod, double f[6]) const {
  const double s0 = hist(0);
  const double slope10 = histLen_ > 10 ? (s0 - hist(10)) / 10 : 0.0;
  const double slope30 = histLen_ > 30 ? (s0 - hist(30)) / 30 : 0.0;
  double base = s0;
  const int n = histLen_ < 360 ? histLen_ : 360;
  for (int i = 0; i < n; i++) {
    const double v = hist(i);
    if (v < base) base = v;
  }
  const double darkSky = (mod < 6 * 60 + 15 || mod > 18 * 60 + 15) ? 1.0 : 0.0;
  const double lamp = (darkSky > 0 && ldr < 2600) ? 1.0 : 0.0;
  f[0] = slope10 / 5.0;
  f[1] = slope30 / 5.0;
  f[2] = (s0 - base) / 200.0;
  f[3] = lamp;
  f[4] = darkSky;
  f[5] = generation() / 5.0;
}

double Engine::presenceProb(const double f[6]) const {
  double z = kPresenceB;
  for (int i = 0; i < 6; i++) z += kPresenceW[i] * f[i];
  if (z > 30) z = 30;
  if (z < -30) z = -30;
  return 1.0 / (1.0 + exp(-z));
}

// ---------------- main step ----------------

int Engine::step(long t, double co2, int ldr, bool touched, bool travelMode, Alert out[3], bool radioTouched) {
  int n = 0;
  const int mod = (int)(t % 1440);
  if (mod == 0) endOfDay(t);
  const bool empty = !present_ && awaySince_ >= 0 && t - awaySince_ >= 10 && (lastLifeAbs_ < 0 || t - lastLifeAbs_ > 30);
  observe(co2, mod, empty);
  double f[6];
  features(ldr, mod, f);
  presentP_ = presenceProb(f);
  const bool was = present_;
  const double g = generation();
  genAvg_ = 0.9 * genAvg_ + 0.1 * g;
  if (learning()) {
    int b = (int)(genAvg_ / 0.25);
    if (b < 0) b = 0;
    if (b > 79) b = 79;
    genHist_[b]++;
  }
  // Physics decides presence; the learned model only assists (see the Python reference).
  const bool breathing = genAvg_ >= cfg_.breathingMin;
  onRun_ = (presentP_ > cfg_.presenceOn || breathing) ? onRun_ + 1 : 0;
  offRun_ = (presentP_ < cfg_.presenceOff && !breathing) ? offRun_ + 1 : 0;
  genLowRun_ = genAvg_ < cfg_.emptyMax ? genLowRun_ + 1 : 0;
  if (!present_ && onRun_ >= cfg_.enterConfirmMin) {
    present_ = true;
  } else if (present_ && (offRun_ >= cfg_.leaveConfirmMin || genLowRun_ >= cfg_.leaveConfirmMin)) {
    present_ = false;
  }

  // Signs of life
  bool life = touched || radioTouched;  // a knob turn on Hum is a touch from the bedroom
  bool strong = life;
  if (nLdr_ == 3) {
    ldrPrev_[0] = ldrPrev_[1];
    ldrPrev_[1] = ldrPrev_[2];
    ldrPrev_[2] = ldr;
  } else {
    ldrPrev_[nLdr_++] = ldr;
  }
  if (nLdr_ == 3 && abs(ldrPrev_[2] - ldrPrev_[0]) > 700) {
    const bool skyChange = (mod >= 6 * 60 && mod <= 6 * 60 + 40) || (mod >= 17 * 60 + 40 && mod <= 18 * 60 + 30);
    if (!skyChange || abs(ldrPrev_[2] - ldrPrev_[0]) > 1200) {
      life = true;  // a switch, not the sunrise
      strong = true;
    }
  }
  // A burner being lit (or a room filling with people) needs active people: a burst of CO2
  // production far above one person's rate is itself a sign of life.
  const bool burstNow = genAvg_ > cfg_.stillMax;
  if (burstNow && !burst_) {
    life = true;
    strong = true;
  }
  burst_ = burstNow;
  if (present_ && !was && awaySince_ >= 0 && t - awaySince_ >= cfg_.realReturnMin) life = true;
  if (present_ && awaySince_ >= 0) {
    const int startMod = (int)(awaySince_ % 1440);
    if (learning() && startMod >= 8 * 60 && startMod <= 20 * 60 && nAbs_ < kMaxAbs) {
      absStart_[nAbs_] = startMod;
      absLen_[nAbs_] = (int)(t - awaySince_);
      nAbs_++;
    }
    awaySince_ = -1;
  }
  if (!present_ && was) awaySince_ = t - cfg_.leaveConfirmMin;
  if (life && strong) {
    awaySince_ = -1;  // a hand on Pip or a light switch: someone is here
    lastStrongAbs_ = t;
  }
  recent_[recentHead_] = g >= cfg_.breathingMin || presentP_ > 0.7;
  recentHead_ = (recentHead_ + 1) % 60;
  if (nRecent_ < 60) nRecent_++;
  if (life) doneToday_ &= (uint8_t)~(bit(Kind::Away) | bit(Kind::Quiet) | bit(Kind::Night));
  if (life && mod >= kDayStart) {
    lastLifeAbs_ = t;
    if (todayFirst_ < 0) todayFirst_ = mod;
    todayLast_ = mod;
    if (askKind_ != Kind::None) {
      push(out, n, t, askKind_, Level::Resolved);
      clearAsk();
    }
  }

  if (travelMode || learning()) {
    clearAsk();
    return n;
  }

  auto recentSum = [&](int last) {
    int s = 0;
    const int k = last < nRecent_ ? last : nRecent_;
    for (int i = 0; i < k; i++) s += recent_[(recentHead_ - 1 - i + 120) % 60] ? 1 : 0;
    return s;
  };

  // Someone left the room while Pip was asking for a pat: an outing, not a collapse.
  if (askKind_ == Kind::Quiet && (awaySince_ >= 0 || recentSum(10) <= 2)) {
    push(out, n, t, Kind::Quiet, Level::Resolved);
    clearAsk();
    quietCooldownUntil_ = t + 60;
  }
  // Escalate an open ask
  if (askKind_ != Kind::None) {
    const long waited = t - askSince_;
    const bool radio = askKind_ == Kind::Morning && cfg_.bedroomRadio;
    const int toLoud = radio ? cfg_.radioSoftMin : cfg_.softToLoudMin;
    const int toFamily = radio ? cfg_.radioLoudMin : cfg_.loudToFamilyMin;
    if (askLevel_ == Level::Soft && waited >= toLoud) {
      askLevel_ = Level::Loud;
      askSince_ = t;
      push(out, n, t, askKind_, Level::Loud);
    } else if (askLevel_ == Level::Loud && waited >= toFamily) {
      if (askKind_ == Kind::Quiet && recentSum(20) < 15) {
        push(out, n, t, Kind::Quiet, Level::Resolved);
        clearAsk();
        quietCooldownUntil_ = t + 60;
        return n;
      }
      push(out, n, t, askKind_, Level::Family);
      doneToday_ |= bit(askKind_);
      clearAsk();
    }
    return n;
  }

  const Kind k = check(t, mod);
  if (k != Kind::None) {
    askKind_ = k;
    askLevel_ = (k == Kind::Morning || k == Kind::Away) ? Level::Loud : Level::Soft;
    if (k == Kind::Morning && cfg_.bedroomRadio) askLevel_ = Level::Soft;  // a soft song first, in the bedroom
    askSince_ = t;
    push(out, n, t, k, askLevel_);
  }
  return n;
}

// ---------------- checks ----------------

int Engine::usualAbsence(int startMod) const {
  int nearMax = -1, nearN = 0, allMax = -1;
  for (int i = 0; i < nAbs_; i++) {
    if (absLen_[i] > allMax) allMax = absLen_[i];
    if (abs(absStart_[i] - startMod) <= 90) {
      nearN++;
      if (absLen_[i] > nearMax) nearMax = absLen_[i];
    }
  }
  if (cfg_.absenceByTime && nearN >= 3) return nearMax;
  return allMax >= 0 ? allMax : 180;
}

Kind Engine::check(long t, int mod) {
  if (!(doneToday_ & bit(Kind::Morning)) && todayFirst_ < 0 && nLife_ > 0) {
    const double deadline = cfg_.bedroomRadio ? percentileInt(firstLife_, nLife_, 0.5) + cfg_.radioMarginMin
                                              : percentileInt(firstLife_, nLife_, 1.0) + cfg_.morningMarginMin;
    if (mod >= deadline && mod < 12 * 60) return Kind::Morning;
  }
  const double usualEnd = nLife_ > 0 ? percentileInt(lastLife_, nLife_, 0.5) : 21 * 60;
  const bool inDay = todayFirst_ >= 0 && mod < usualEnd;
  const long sinceStrong = t - (lastStrongAbs_ >= 0 ? lastStrongAbs_ : t);
  int recent = 0;
  for (int i = 0; i < nRecent_; i++) recent += recent_[i] ? 1 : 0;
  // A still or collapsed person makes ~3-7 ppm/min. Far more is a gas burner or a crowd.
  const bool around = nRecent_ >= 60 && recent >= 40 && genAvg_ <= cfg_.stillMax;
  if (!(doneToday_ & bit(Kind::Quiet)) && inDay && around && sinceStrong >= cfg_.quietLimitMin &&
      t >= quietCooldownUntil_)
    return Kind::Quiet;
  if (!(doneToday_ & bit(Kind::Away)) && !present_ && awaySince_ >= 0 && todayFirst_ >= 0) {
    const int start = (int)(awaySince_ % 1440);
    const double earliestBed = nLife_ > 0 ? percentileInt(lastLife_, nLife_, 0.0) : 20 * 60;
    const bool watching = inDay || (start < earliestBed - 30 && t - awaySince_ < 10 * 60);
    if (watching && t - awaySince_ >= usualAbsence(start) + cfg_.awayMarginMin) return Kind::Away;
  }
  if (!(doneToday_ & bit(Kind::Night)) && nLife_ > 0) {
    const double bedtime = percentileInt(lastLife_, nLife_, 0.9);
    double f[6];
    features(ldrPrev_[nLdr_ - 1], mod, f);
    const bool lampOn = f[3] > 0;
    const bool late = mod >= bedtime + cfg_.nightLampAfterMin || mod < kDayStart;
    if (late && lampOn && sinceStrong >= cfg_.nightLampAfterMin) return Kind::Night;
  }
  return Kind::None;
}

void Engine::endOfDay(long t) {
  // The daily floor tracks outdoor air only when the room really aired out (see the Python reference:
  // a real office whose floor climbed to 1,030 ppm hid people from the physics).
  if (dayMin_ < 500) cout_ = 0.7 * cout_ + 0.3 * dayMin_;
  dayMin_ = 1e9;
  if (learning() && todayFirst_ >= 0 && nLife_ < kLearnDays) {
    firstLife_[nLife_] = todayFirst_;
    lastLife_[nLife_] = todayLast_;
    nLife_++;
  }
  if (todayFirst_ >= 0 || learning()) daysSeen_++;
  if (!learning() && !calibrated_) calibrate();
  todayFirst_ = todayLast_ = -1;
  doneToday_ = 0;
  // Fresh absence if the room is empty (so a morning entry counts), never last evening's.
  awaySince_ = present_ ? -1 : t;
}

// Self-calibration at the end of learning: this room's own "empty" and "someone here" levels of CO2
// production set the thresholds; falls back to the defaults if they aren't clearly separate.
void Engine::calibrate() {
  calibrated_ = true;
  double lo, hi;
  if (!twoClusters(genHist_, 80, 0.25, lo, hi)) return;
  double b = lo + 0.45 * (hi - lo), e = lo + 0.2 * (hi - lo);
  cfg_.breathingMin = b > 2.5 ? 2.5 : (b < 0.6 ? 0.6 : b);
  cfg_.emptyMax = e > 1.2 ? 1.2 : (e < 0.3 ? 0.3 : e);
}

void Engine::exportLearned(Learned& o) const {
  memset(&o, 0, sizeof(o));
  o.version = 2;
  o.daysSeen = (int16_t)daysSeen_;
  o.nLife = (int16_t)nLife_;
  o.nAbs = (int16_t)nAbs_;
  for (int i = 0; i < nLife_; i++) {
    o.firstLife[i] = (int16_t)firstLife_[i];
    o.lastLife[i] = (int16_t)lastLife_[i];
  }
  for (int i = 0; i < nAbs_; i++) {
    o.absStart[i] = (int16_t)absStart_[i];
    o.absLen[i] = (int16_t)absLen_[i];
  }
  o.leak = leak_;
  o.cout = cout_;
  o.breathingMin = cfg_.breathingMin;
  o.emptyMax = cfg_.emptyMax;
  o.calibrated = calibrated_ ? 1 : 0;
}

bool Engine::importLearned(const Learned& in) {
  if (in.version != 2 || in.nLife < 0 || in.nLife > kLearnDays || in.nAbs < 0 || in.nAbs > kMaxAbs) return false;
  if (!(in.leak > 0 && in.leak < 0.2) || !(in.cout > 300 && in.cout < 700)) return false;
  daysSeen_ = in.daysSeen;
  nLife_ = in.nLife;
  nAbs_ = in.nAbs;
  for (int i = 0; i < nLife_; i++) {
    firstLife_[i] = in.firstLife[i];
    lastLife_[i] = in.lastLife[i];
  }
  for (int i = 0; i < nAbs_; i++) {
    absStart_[i] = in.absStart[i];
    absLen_[i] = in.absLen[i];
  }
  leak_ = in.leak;
  cout_ = in.cout;
  if (in.calibrated && in.breathingMin >= 0.6 && in.breathingMin <= 2.5 && in.emptyMax >= 0.3 && in.emptyMax <= 1.2) {
    cfg_.breathingMin = in.breathingMin;
    cfg_.emptyMax = in.emptyMax;
    calibrated_ = true;
  }
  return true;
}

void Engine::familyText(Kind k, long t, char* buf, size_t n, const char* name) const {
  char a[6], b[6], c[6];
  hhmm(t % 1440, c);
  hhmm(lastStrongAbs_ >= 0 ? lastStrongAbs_ % 1440 : 0, a);
  switch (k) {
    case Kind::Morning:
      hhmm((long)percentileInt(firstLife_, nLife_, 0.5), a);
      hhmm((long)percentileInt(firstLife_, nLife_, 1.0), b);
      snprintf(buf, n, "No sign that %s is up yet. Usually up around %s (latest seen %s). It's %s. "
               "Pip chimed and got no answer. Please call %s.", name, a, b, c, name);
      break;
    case Kind::Quiet:
      snprintf(buf, n, "%s seems to be in the main room, but there has been no movement since %s. "
               "Pip asked twice and got no answer. Please call %s.", name, lastStrongAbs_ >= 0 ? a : "unknown", name);
      break;
    case Kind::Away: {
      const int start = awaySince_ >= 0 ? (int)(awaySince_ % 1440) : 0;
      const int usual = usualAbsence(start);
      hhmm(start, b);
      snprintf(buf, n, "No one has been in the main room since %s, longer than %s's usual time away "
               "(up to %d h %d min). Pip chimed with no answer. If %s is out, all is fine. Otherwise please call.",
               b, name, usual / 60, usual % 60, name);
      break;
    }
    default:
      snprintf(buf, n, "The main-room lamp has been on since late evening with no movement since %s. "
               "Pip asked twice with no answer. Please call %s.", lastStrongAbs_ >= 0 ? a : "unknown", name);
  }
}

}  // namespace kin
