// Left-behind detection (car mode).
//
// The device sits in a parked car and reads CO2 and temperature every ~5 s. When the ignition goes
// off it waits for the doors-open flush to finish, then watches the closed cabin. If CO2 is still
// being produced, someone (infant, toddler, pet) is breathing inside. Hot cabin air raises the level.
//
// Pure C++17, no Arduino headers, no heap. It compiles on the Mac test environment and on the ESP32.
// Not a certified safety device. See LEFT_BEHIND.md for the limits.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum class LBState : uint8_t {
  Driving,   // ignition on: nothing to watch
  Settling,  // ignition just went off: doors open, air is being flushed, readings still falling
  Watching,  // cabin closed: looking for CO2 production
  Alert,     // production held above the threshold: someone is probably inside
};

enum class LBLevel : uint8_t {
  None,
  Check,   // "check the back seat"
  Urgent,  // same, and the cabin is hot or heating fast
};

// Plain aggregate with no default member initializers, so `LBResult{state, level, prod, changed}`
// works under older C++ standards too. The first four fields are the stable interface.
struct LBResult {
  LBState state;
  LBLevel level;
  float productionPpmMin;  // latest estimate of CO2 produced inside, in ppm per minute (0 until known)
  bool changed;            // state or level differs from what the last update returned
  float outdoorPpm;        // current outdoor CO2 estimate
  float tempRise10Min;     // cabin temperature change per 10 min (0 until 4 min of data)
  bool outOfSpec;          // sensor above its rated 60 C: readings and alerts are less trustworthy
};

struct LBConfig {
  // Settling. Doors stay open for 20-90 s in the SIMULATED cases and the sensor lags ~60 s, so the
  // reading keeps falling for up to 2-3 minutes after the doors close. 120 s is the shortest wait; the
  // slope check below then decides whether to keep waiting.
  uint16_t settleMinSec = 120;
  // Give up waiting for the fall to stop after 10 min (a very high start, such as 4 adults on
  // recirculation, falls slowly in a tight cabin). The leak term below keeps this safe.
  uint16_t settleMaxSec = 600;
  // "Still falling fast" means a fitted slope below -8 ppm/min over the last 120 s. At 5-10 ppm
  // noise the slope over 120 s is only known to about +-3 ppm/min, so -8 is clearly a real fall.
  float settleFallPpmMin = 8.0f;
  uint16_t settleSlopeWindowSec = 120;

  // Regression window. 4 min balances noise (slope error ~1.3 ppm/min at 8 ppm noise) against speed.
  uint16_t windowSec = 240;
  // Do not judge before 2 min of settled data.
  uint16_t minWindowSec = 120;
  // Judge every 30 s. Windows overlap, so the confirm count below is about persistence, not
  // independent votes.
  uint16_t evalEverySec = 30;
  // Production threshold. A sleeping infant (2.5-4.5 L/h) in a 3 m3 car adds 14-25 ppm/min; in an
  // 8-10 m3 van it adds 4-8 ppm/min. 3 ppm/min is above the noise floor after the SE check but
  // still catches a small infant in a van. Tuned on dev seeds only.
  float productionThresholdPpmMin = 3.0f;
  // The slope must also be this many standard errors above zero (guards against noise and a
  // single wobble). v3: 2.5, down from 3, because a van's small rise sits close to the noise;
  // tuned on dev and non-final seeds (3 gave 88% closed-van infants in 10 min, 2.5 gave 94%).
  float zMargin = 2.5f;
  // Number of consecutive judgements above threshold before Alert: 3 x 30 s = 90 s of persistence.
  // Tuned on dev seeds: 4 was 30 s slower with the same false alarm count (zero).
  uint8_t confirmCount = 3;

  // Leak: assumed air changes per hour when closed. The estimate adds leak * (C - outdoor) to the
  // slope. A parked closed car measured under 1/h. Using a value at or below the true leak never
  // creates a false alarm from a decaying empty cabin; using too high a value would. 0.3/h is the
  // lowest value in the SIMULATED range (0.3-1.5). A cracked window (3-6/h) is mostly missed
  // on purpose: it is not modelled by this term.
  float leakPerHour = 0.3f;
  // Sensor response time (first-order lag). Used to project the lagging reading forward.
  float lagSec = 60.0f;

  // Outdoor CO2 estimate: lowest 30 s average seen since ignition off, kept between 400 and 600.
  // Fresh outdoor air is ~420-480 ppm today. 600 is the cap so a high-CO2 start cannot hide a rise.
  float outdoorMinPpm = 400.0f;
  float outdoorMaxPpm = 600.0f;
  float outdoorDefaultPpm = 450.0f;

  // v2 level rule (finds a child when a cracked window hides the rise). A person keeps the cabin
  // G/(leak*V) above outdoor even when the slope is flat. An empty cabin decays toward outdoor at
  // least as fast as the lowest leak allows: slope <= -leakPerHour/60 * excess.
  // Alert when the excess over outdoor stays above levelExcessPpm AND the CO2 falls less than
  // levelDecayFrac of that empty-cabin decay (slope minus levelZ standard errors must still be
  // above -levelDecayFrac * leak * excess), for levelSustainSec in a row, on a levelWindowSec fit.
  float levelExcessPpm = 100.0f;
  float levelDecayFrac = 0.8f;
  float levelZ = 0.0f;
  // The outdoor estimate is often too high: the 60 s lag stops the reading from reaching outdoor
  // during a short flush. For this rule only, outdoor is capped at 500 ppm (today's outdoor air
  // is ~420-480).
  float levelOutdoorCapPpm = 500.0f;
  uint16_t levelWindowSec = 600;
  uint16_t levelSustainSec = 300;
  bool levelRuleOn = true;

  // v3: cabin volume. What a person adds is G / V ppm per minute, so the same child looks 2-3 times
  // smaller in a school van. The user sets 3 for a car (default) or about 8 for a van. The
  // production threshold and the level excess threshold are both scaled by referenceVolumeM3 /
  // cabinVolumeM3 (the thresholds below are the values for a 3 m3 car). That is the same as
  // thresholding the production in litres of CO2 per hour. levelExcessFloorPpm stops the scaled
  // level threshold from falling into the sensor offset (+-30 ppm) and outdoor estimate error.
  float cabinVolumeM3 = 3.0f;
  float referenceVolumeM3 = 3.0f;
  float levelExcessFloorPpm = 60.0f;

  // v4: model-comparison rule. After the doors close the sensor curve is fitted with two models,
  // both passed through the 60 s sensor lag:
  //   H0 "pure decay":    cabin = c + sum of decays (weights >= 0), with c <= modelAsymptoteMaxPpm
  //   H1 "decay+source":  the same with c free (c - outdoor is then G / (ach * V)) and the decay
  //                       weights either all >= 0 (starting above) or all <= 0 (starting below)
  // The air-change rate (ach) is not known. Seven values from 0.3 to 6 per hour are tried. A decay is
  // a non-negative mix of two NEIGHBOURING values, which covers rates in between. (A single value
  // fails: an empty car starting at 3000 ppm has a decay so large that a 10% error in the rate looks
  // like a source.) The best fit of each model over all six pairs is kept. The score is
  // (RSS0 - RSS1) / sigma^2 with sigma^2 = RSS1 / (n - 4). A high score means a constant source
  // explains the curve and plain decay does not. Sums are kept for the full basis, so memory is
  // fixed and an update costs a few hundred operations.
  bool modelRuleOn = true;
  // The fit starts this long after ignition off (doors are closed by then in the simulated cases).
  uint16_t modelStartSec = 120;
  // No verdict before this many samples (60 x 5 s = 5 min): fewer points cannot tell the curves apart.
  uint16_t modelMinSamples = 60;
  // Largest value the asymptote may take under "pure decay": top of the outdoor range (480 ppm) plus
  // the sensor offset bound (30 ppm).
  float modelAsymptoteMaxPpm = 510.0f;
  // Score needed for an alert. 2,400 simulated empty cars from non-final seeds (7001-13000, half of
  // them hard cases) gave no alarm from this rule at 20; 12 gave 2 and 8 gave 6 false alarms per 800.
  float modelThreshold = 20.0f;

  // v5: learns its own vehicle. A device that lives in one vehicle sees many parkings. From parkings
  // that look empty it learns the settled CO2 level as its own sensor reads it (outdoor air plus the
  // sensor's fixed offset) and the vehicle's leak rates (air changes per hour), one list for the
  // closed state and one for the window-cracked state. It keeps the last 10 of each and uses the
  // MEDIAN, so a parking learned by mistake cannot move the answer much. The next parking uses them
  // as tighter priors in the model comparison above: the decay rates tried are a band around the
  // learned leak (kept inside the assumed ranges), and the cap on the pure-decay level comes from
  // the learned level. With fewer than learnMinBase / learnMinLeak values it is exactly v4.
  bool learnOn = true;
  uint8_t learnMinBase = 6;
  uint8_t learnMinLeak = 2;
  // Cap on the pure-decay level = median learned level + this margin (never above modelAsymptoteMaxPpm),
  // used only after learnMinBase learned levels. Outdoor CO2 differs by day (420-480) and drifts +-10, and
  // the learner's own level estimate has an error of about 20 ppm (sd) with a long tail (up to 60 ppm in
  // dev runs). A margin of 45 gave false alarms, 60 with 3 values gave 5 new ones in 10,400 empty
  // vehicles, and 80 with 6 values gave none. The cap rarely binds at this setting: the day-to-day
  // outdoor spread leaves little to gain (see LEFT_BEHIND.md, v5).
  float learnBaseMargin = 80.0f;
  // Decay-rate band tried = learned median x lo .. x hi, inside the assumed range below. The vehicle's
  // leak varies about +-25% by day, and the median of a few estimates is itself uncertain and a little
  // high. Narrower bands (0.65-1.5, 0.6-1.6) gave false alarms on dev seeds.
  float learnLeakLo = 0.55f;
  float learnLeakHi = 1.8f;
  float closedLo = 0.3f, closedHi = 1.5f, crackedLo = 3.0f, crackedHi = 6.0f;
  // Alert threshold for the model comparison when something has been learned.
  float learnModelThreshold = 20.0f;
  // A parking is learned from only if: no alert, at least learnMinSamples readings (5 s each, from 2
  // min after ignition off) so about 40 min of closed cabin, the decay-plus-level fit leaves a residual
  // below learnMaxResidualSd ppm, the fitted level is plausible (outdoor 420-480 plus offset +-30),
  // and the last 10 min show no significant rise (slope within learnTailZ standard errors of zero).
  uint16_t learnMinSamples = 480;
  float learnMaxResidualSd = 14.0f;
  float learnBaseMin = 390.0f, learnBaseMax = 510.0f;
  float learnTailZ = 2.5f;
  // The level is stored only if its standard error is at most this many ppm.
  float learnMaxBaseSe = 12.0f;
  // The leak is stored only if the fit gets worse by learnDetectZ x noise variance when the rate is
  // moved two grid steps (about +-35%) either way, i.e. the rate is pinned down to about +-30%.
  float learnDetectZ = 9.0f;
  // Learned leaks below this go to the closed list, others to the cracked list (per hour).
  float learnLeakSplit = 2.0f;

  // Radar option (HLK-LD2410C, see radar.h). Not tested on hardware: these are guesses.
  // A STILL target (state 2 or 3) inside radarCabinMaxCm with energy at least radarStillMinEnergy, seen
  // continuously for radarStillSec while the cabin is Watching, raises an alert on its own. Frames older
  // than radarStaleSec count as "no target". The CO2 rules are untouched and still run.
  uint16_t radarCabinMaxCm = 300;
  uint8_t radarStillMinEnergy = 30;
  uint16_t radarStillSec = 30;
  uint16_t radarStaleSec = 8;

  // Heat. Urgent when the cabin is at or above dangerC (a child's core temperature can reach a
  // dangerous level in about an hour at this heat), or at or above hotC and still rising by hotRise
  // per 10 min, or rising by fastRise per 10 min at any temperature (a sunny car gains ~1 C/min
  // early on).
  float hotC = 32.0f;
  float dangerC = 42.0f;
  float hotRiseC10 = 2.0f;
  float fastRiseC10 = 6.0f;
  // SCD4x rated temperature ceiling.
  float sensorMaxC = 60.0f;
};

// Size of the v4 model basis (see LBConfig).
constexpr int kLbGrid = 7;
constexpr int kLbDim = 2 + kLbGrid;
// v5: learned values kept per category, decay rates the learner tries, bytes of saved state.
constexpr int kLbHist = 10;
constexpr int kLbLam = 28;
constexpr size_t kLbLearnedBytes = 4 + 3 * kLbHist * 4 + 2;  // 126

class LeftBehind {
 public:
  using Config = LBConfig;

  explicit LeftBehind(Config c = Config());

  // on = driving. Off = parked: start settling. tSec is a clock in seconds that only has to count up.
  void setIgnition(bool on, uint32_t tSec);
  // Call every ~5 s while parked.
  LBResult update(uint32_t tSec, float co2Ppm, float tempC);
  // Owner confirms the car is empty (touch). Silences everything until the next ignition cycle.
  void dismiss(uint32_t tSec);

  // ---- v5 learning -------------------------------------------------------------------------
  // Call when a parking ends (ignition back on), BEFORE setIgnition(true). If the parking looks empty
  // (see LBConfig) it adds what it saw to the learned values and returns true: save the state then.
  // Returns false (and learns nothing) after an alert, a dismiss, a short parking or a poor fit.
  // Radar: call with each radar frame (state byte, still distance in cm, still energy). Only used by the
  // next update() calls, and only while parked; with no radar attached it is never called.
  void setRadar(uint32_t tSec, uint8_t targetState, uint16_t stillCm, uint8_t stillEnergy);
  // True if the current alert came from the radar rather than from CO2.
  bool radarAlerted() const { return radarAlerted_; }

  bool learnFromParking();
  // Plain bytes: version, three counts, 3 x 10 float32 (oldest first), 16-bit sum. kLbLearnedBytes long.
  // save returns the number of bytes written (0 if cap is too small). load returns false and leaves
  // the learned state unchanged if the size, version, sum, counts or values are wrong.
  size_t saveLearned(uint8_t* out, size_t cap) const;
  bool loadLearned(const uint8_t* in, size_t n);
  void clearLearned();
  uint8_t learnedBaseCount() const { return lrn_.nB; }
  uint8_t learnedClosedCount() const { return lrn_.nC; }
  uint8_t learnedCrackedCount() const { return lrn_.nK; }
  // The priors the next/current parking uses (for tests and logs).
  float priorAsymptoteMax() const { return asymMax_; }
  float priorThreshold() const { return modelThr_; }

  LBState state() const { return state_; }
  LBLevel level() const { return level_; }

 private:
  struct Sample {
    uint32_t t;
    float co2;
    float temp;
  };
  // 128 samples: 10.7 min at 5 s. Enough for the 4 min window and the 10 min temperature trend.
  static constexpr size_t kRing = 128;

  struct Fit {
    bool ok = false;
    float slope = 0;   // ppm per minute
    float atNow = 0;   // fitted CO2 at the newest time
    float se = 0;      // standard error of the slope, ppm per minute
    int n = 0;
  };

  void reset();
  void push(uint32_t t, float co2, float temp);
  const Sample& at(size_t back) const;  // back = 0 is the newest
  Fit fit(uint32_t now, uint32_t fromT, uint32_t windowSec) const;
  void trackOutdoor();
  float tempRise10(uint32_t now) const;
  LBResult result(float prod, float rise, float temp);

  Config cfg_;
  LBState state_ = LBState::Driving;
  LBLevel level_ = LBLevel::None;
  LBState lastState_ = LBState::Driving;
  LBLevel lastLevel_ = LBLevel::None;
  bool dismissed_ = false;
  Sample ring_[kRing];
  size_t head_ = 0;   // next write index
  size_t count_ = 0;
  uint32_t tOff_ = 0;
  uint32_t tSettled_ = 0;
  uint32_t lastT_ = 0;
  uint32_t nextEval_ = 0;
  uint8_t streak_ = 0;
  uint32_t levelSince_ = 0;  // when the level condition began holding (0 = not holding)
  // Model-comparison sums (see LBConfig). Basis: [1, sensor start-state term, 7 lagged decays].
  static constexpr int kGrid = kLbGrid;
  static constexpr int kDim = kLbDim;
  double mS_[kDim][kDim];  // X'X, upper triangle used
  double mV_[kDim];        // X'y
  double mYY_;
  double mDrive_[kGrid];   // exp(-ach * t)
  double mG_[kGrid];       // sensor response to that decay, from zero state
  uint32_t mT0_ = 0, mLast_ = 0, nextModel_ = 0;
  int mN_ = 0;
  bool mOn_ = false;
  void modelAdd(uint32_t t, float co2);
  float modelScore() const;
  float modelScore_ = 0;
  float outdoor_ = 450.0f;
  float minAvg_ = 1e9f;
  float prod_ = 0;
  bool outOfSpec_ = false;

  // ---- radar ----
  bool radarStill_ = false, radarHolding_ = false, radarAlerted_ = false;
  uint32_t radarT_ = 0, radarSince_ = 0;
  void radarCheck(uint32_t t);

  // ---- v5 ----
  struct Learned {
    uint8_t nB = 0, nC = 0, nK = 0;
    float b[kLbHist] = {};  // settled level as read by this sensor
    float c[kLbHist] = {};  // closed-cabin leak, per hour
    float k[kLbHist] = {};  // window-cracked leak, per hour
  };
  Learned lrn_;
  double grid_[kLbGrid];       // decay rates tried by the model comparison (per hour)
  bool pairOn_[kLbGrid - 1];   // which neighbouring pairs of grid_ are used
  float asymMax_ = 510.0f;     // cap on the pure-decay level
  float modelThr_ = 20.0f;     // alert score for this parking
  void applyPriors();
  void band(float median, int n, double rmin, double rmax, double* out) const;
  static void pushLearned(float* v, uint8_t& n, double x);
  static float medianOf(const float* v, uint8_t n);
  // Learner sums: fit [1, sensor start state, decay at rate kLam[k]] for each of kLbLam rates.
  bool lOn_ = false;
  bool parkLearn_ = false;
  bool everAlerted_ = false;
  int lN_ = 0;
  uint32_t lT0_ = 0, lLast_ = 0;
  double lSx1_ = 0, lSx1x1_ = 0, lV0_ = 0, lV1_ = 0, lYY_ = 0;
  double lDrive_[kLbLam], lG_[kLbLam], lSG_[kLbLam], lSx1G_[kLbLam], lSGG_[kLbLam], lVG_[kLbLam];
  void learnAdd(uint32_t t, float co2);
  void learnSolve(int k, double* rss, double* c0, double* inv00, int* npar) const;
};
