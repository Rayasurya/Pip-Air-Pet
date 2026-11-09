// Kin engine (C++ port of kin/engine/kin_engine.py). Notices when an older adult's
// day stops looking normal, asks them first on Pip, then tells the family.
// Must behave exactly like the Python reference; tools/kin_parity checks it minute by minute.
// Pure C++, no Arduino. Uses double so the maths matches Python bit for bit where it matters.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace kin {

enum class Kind : uint8_t { None, Morning, Quiet, Away, Night };
enum class Level : uint8_t { Soft, Loud, Family, Resolved };

const char* kindName(Kind k);
const char* levelName(Level l);

struct Config {
  int morningMarginMin = 15;
  int quietLimitMin = 240;
  int awayMarginMin = 45;
  int nightLampAfterMin = 120;
  int softToLoudMin = 10;
  int loudToFamilyMin = 15;
  double presenceOn = 0.6;
  double presenceOff = 0.35;
  int enterConfirmMin = 15;
  double breathingMin = 2.5;
  double emptyMax = 1.2;
  double stillMax = 15.0;  // above this: a gas stove or a crowd, not one still person
  int leaveConfirmMin = 20;
  int realReturnMin = 30;
  bool absenceByTime = true;
  bool bedroomRadio = false;   // Hum in the bedroom: morning song at the usual wake-up time
  int radioMarginMin = 0;
  int radioSoftMin = 10;
  int radioLoudMin = 15;
};

struct Alert {
  bool valid = false;
  long minute = 0;
  Kind kind = Kind::None;
  Level level = Level::Soft;
};

class Engine {
 public:
  static const int kLearnDays = 14;
  static const int kDayStart = 4 * 60;

  explicit Engine(const Config& c = Config()) : cfg_(c) {}

  // One call per minute. absMinute counts minutes from a midnight (minute 0 = 00:00).
  // Up to 3 alerts can come out of one step (e.g. "resolved" then a new ask).
  // radioTouched: a knob turn on Hum (the bedroom radio) this minute, received over Wi-Fi.
  int step(long absMinute, double co2, int ldr, bool touched, bool travelMode, Alert out[3],
           bool radioTouched = false);

  // Plain-language family message for the alert just raised (call right after a Family alert).
  void familyText(Kind k, long absMinute, char* buf, size_t n, const char* name) const;

  bool learning() const { return daysSeen_ < kLearnDays; }
  Kind askKind() const { return askKind_; }
  Level askLevel() const { return askLevel_; }
  bool present() const { return present_; }
  double breathing() const { return genAvg_; }  // estimated CO2 production, ppm/min
  double leakPerHour() const { return leak_ * 60; }
  int daysSeen() const { return daysSeen_; }
  // What the engine has learned about this home, saved to flash nightly so a power cut does
  // not restart the 14-day learning. Plain data, versioned.
  struct Learned {
    uint16_t version;
    int16_t daysSeen, nLife, nAbs;
    int16_t firstLife[kLearnDays], lastLife[kLearnDays];
    int16_t absStart[160], absLen[160];
    double leak, cout;
    double breathingMin, emptyMax;  // self-calibrated thresholds (v2)
    uint8_t calibrated;
  };
  void exportLearned(Learned& out) const;
  bool importLearned(const Learned& in);  // false if the data is unusable (wrong version, out of range)

  // For the daily digest: first and last sign of life today (minute of day, -1 if none).
  int todayFirst() const { return todayFirst_; }
  int todayLast() const { return todayLast_; }

 private:
  // signals
  void observe(double co2, int mod, bool emptyRoom);
  double generation() const;
  void features(int ldr, int mod, double f[6]) const;
  double presenceProb(const double f[6]) const;
  double hist(int ago) const { return co2Hist_[(histHead_ - 1 - ago + 2 * kHist) % kHist]; }
  // checks
  Kind check(long t, int mod);
  int usualAbsence(int startMod) const;
  void clearAsk() { askKind_ = Kind::None; }
  void endOfDay(long t);
  void push(Alert out[3], int& n, long t, Kind k, Level l) const;

  Config cfg_;
  // learned
  int firstLife_[kLearnDays];
  int lastLife_[kLearnDays];
  int nLife_ = 0;
  static const int kMaxAbs = 160;
  int absStart_[kMaxAbs], absLen_[kMaxAbs];
  int nAbs_ = 0;
  int daysSeen_ = 0;
  // CO2 and room physics
  static const int kHist = 361;
  double co2Hist_[kHist];
  int histHead_ = 0, histLen_ = 0;
  double ema_ = 0;
  bool haveEma_ = false;
  double cout_ = 450.0, dayMin_ = 1e9;
  static const int kLeak = 512;
  double leakS_[kLeak];
  int leakHead_ = 0, nLeak_ = 0;
  long leakAdded_ = 0;
  double leak_ = 1.0 / 60;
  // presence
  bool present_ = false;
  double presentP_ = 0, genAvg_ = 0;
  int onRun_ = 0, offRun_ = 0, genLowRun_ = 0;
  uint32_t genHist_[80] = {0};  // CO2 production 0-20 ppm/min in 0.25 bins, while learning
  bool calibrated_ = false;
  void calibrate();
  bool burst_ = false;  // CO2 production far above one person (stove or crowd)
  int ldrPrev_[3];
  int nLdr_ = 0;
  bool recent_[60];
  int recentHead_ = 0, nRecent_ = 0;
  // today
  int todayFirst_ = -1, todayLast_ = -1;
  long lastLifeAbs_ = -1, lastStrongAbs_ = -1;
  long awaySince_ = -1;
  long quietCooldownUntil_ = 0;
  uint8_t doneToday_ = 0;  // bit per Kind
  // escalation
  Kind askKind_ = Kind::None;
  Level askLevel_ = Level::Soft;
  long askSince_ = 0;
};

}  // namespace kin
