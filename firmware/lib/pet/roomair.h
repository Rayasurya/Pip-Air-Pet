// Room mode: fresh air and shared-air risk for a shared room (classroom, clinic waiting room, office).
// One CO2 value per minute in, a report about the ROOM out. Never about any individual.
// Pure C++17, no Arduino headers, fixed memory (about 1 KB), no heap.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct RoomAirReport {
  bool valid;                // false until about 20 minutes of data have arrived
  bool steady;               // CO2 is near steady AND the room is occupied: fresh-air-per-person is valid
  bool occupied;             // CO2 is clearly above outdoor air (more than kOccupiedExcessPpm)
  bool leakLearned;          // leakPerHour was learned from a decay (else it is the configured default)
  float outdoorPpm;          // outdoor CO2 estimate used (guarded learned low value, 400..500)
  float excessPpm;           // room CO2 minus outdoor CO2, lag-corrected, never negative
  float rebreathedPct;       // percent of inhaled air that someone already exhaled
  float freshAirLsPerPerson; // L/s of outdoor air per person (capped at 99); meaningful only when steady
  float peopleLow, peopleMid, peopleHigh;  // headcount range from the mass balance, never negative
  uint8_t risk;              // 0..3 from rebreathedPct
  float leakPerHour;         // air changes per hour from the last clean decay (or the default)
  float leakFilterPerHour;   // air changes per hour as estimated by the particle filter (v2)
};

struct RoomAirConfig {
  float volumeM3 = 180;           // room volume, set by the installer
  float genLsPerPerson = 0.0050f; // CO2 made per person, L/s. 0.0050 = seated adult. About 0.0035 for children.
  float exhaledPpm = 38000;       // CO2 in exhaled breath (Rudnick & Milton 2003)
  float leakPerHourDefault = 2.0f;// air changes per hour assumed until a decay has been seen
  float maxPeople = 0;            // most people the room can hold. 0 means volume / 4 (m3 per person is never below 4)
};

class RoomAir {
 public:
  using Config = RoomAirConfig;

  // ---- Constants. Each one says where it comes from. ----
  static constexpr int kBuf = 40;               // samples kept (minutes). Must be >= kSteadyMin and >= kDecayWin.
  static constexpr int kValidMin = 20;          // minutes of data before a report is valid
  static constexpr int kFitMin = 10;            // minutes used for the level and slope of the headcount. Design choice:
                                                // short enough to follow people, long enough that +-10 ppm noise gives a slope error under 1 ppm/min.
  static constexpr int kSteadyMin = 20;         // minutes used to judge "steady". Design choice.
  static constexpr float kLagMin = 1.0f;        // sensor response time, about 60 s (USE_CASES.md section 1). First-order lag correction: C = measured + tau * slope.
  static constexpr float kOccupiedExcessPpm = 80.0f;  // design choice: below this the offset and outdoor error (tens of ppm) swamp the signal.
  static constexpr float kSteadyFrac = 0.05f;   // steady if CO2 moved less than 5% of the excess across kSteadyMin. Design choice.
  static constexpr float kSteadyFloorPpm = 20.0f;  // ...or less than 20 ppm (about 3 sigma of the slope noise for +-8 ppm noise).
  static constexpr float kFreshAirCap = 99.0f;  // display cap, L/s per person
  static constexpr float kOutdoorMin = 400.0f;  // guard: outdoor CO2 is never below this
  static constexpr float kOutdoorMax = 500.0f;  // guard: ...or above this
  static constexpr float kOutdoorDefault = 420.0f;
  static constexpr int kOutdoorWarmup = 120;    // minutes of data before the learned outdoor value is used
  static constexpr int kDays = 7;               // daily minima kept for the rolling minimum
  static constexpr int kDecayWin = 24;          // minutes looked at for a decay; the first kDecaySkip are skipped (sensor lag, people leaving)
  static constexpr int kDecaySkip = 4;
  static constexpr float kDecayMinExcess = 30.0f;     // decay points below this excess are noise-dominated and are dropped
  static constexpr float kDecayStartExcess = 100.0f;  // the decay must start at least this far above outdoor air
  static constexpr int kDecayMinPoints = 8;
  static constexpr float kLeakMin = 0.2f;       // learned air-change rates outside 0.2..15 per hour are rejected
  static constexpr float kLeakMax = 15.0f;
  // Risk thresholds on rebreathedPct. DESIGN CHOICE, not a published limit: 0.5%, 1%, 2%.
  static constexpr float kRisk1 = 0.5f, kRisk2 = 1.0f, kRisk3 = 2.0f;
  // ---- v2 particle filter over people, air-change rate and sensor offset. Design choices tuned on dev rooms. ----
  static constexpr int kPf = 400;                 // particles. 400 x 6 floats x 2 buffers = about 19 KB; about 1 ms per minute on an ESP32.
  static constexpr float kPfRefPpm = 440.0f;      // outdoor CO2 plus sensor offset is modelled as this plus a hidden offset b
  static constexpr float kPfOffsetSd = 35.0f;     // starting spread of b (outdoor 420-470, sensor offset +-30)
  static constexpr float kPfOffsetWalk = 0.2f;    // ppm per minute random walk of b
  static constexpr float kPfSigmaY = 12.0f;       // assumed measurement noise, ppm (real +-10 repeatability, plus model error)
  static constexpr float kPfJumpPeople = 0.03f;   // chance per minute that the headcount jumps (half to zero, half anywhere)
  static constexpr float kPfWalkAbs = 0.3f, kPfWalkRel = 0.03f;  // otherwise it drifts by 0.3 + 3% of itself
  static constexpr float kPfJumpLeak = 0.03f;     // chance per minute that the air-change rate jumps to a fresh value
  static constexpr float kPfWalkLeak = 0.03f;     // otherwise ln(lambda) drifts by this
  static constexpr float kPfLeakPriorLow = 0.5f, kPfLeakPriorHigh = 8.0f;  // air changes per hour: prior for start and jumps
  static constexpr float kPfLeakClipLow = 0.3f, kPfLeakClipHigh = 12.0f;
  static constexpr float kPfQLow = 0.05f, kPfQHigh = 0.95f;  // the headcount range is the 5% to 95% band
  static constexpr float kPfEssFrac = 0.5f;       // resample when effective particles fall under half
  static constexpr float kTargetLsPerPerson = 10.0f;  // DESIGN CHOICE for the advice line, not a cited standard.

  RoomAir() : RoomAir(Config()) {}
  explicit RoomAir(Config c);
  // v1 headcount formula, kept as a plain function: n = V * (dC/dt + lambda * (C - Cout)) / G.
  // slopePpmPerMin and excessPpm in ppm, lambda in air changes per hour. Never negative.
  static float massBalancePeople(float volumeM3, float genLsPerPerson, float slopePpmPerMin, float excessPpm, float lambdaPerHour);
  void addMinute(float co2Ppm);   // call once a minute
  RoomAirReport report() const;
  // Three advice lines for a 128x64 OLED. Each is upper case and at most 21 characters; n is the buffer size (>= 22).
  static void advice(const RoomAirReport& r, char* l1, char* l2, char* l3, size_t n);
  static uint8_t riskFor(float rebreathedPct);

 private:
  Config c_;
  double buf_[kBuf];
  int count_ = 0;       // valid samples in buf_ (max kBuf)
  int head_ = 0;        // next write index
  long minutes_ = 0;    // minutes since start
  // outdoor learning
  double dayMin_[kDays];
  int dayCount_ = 0;
  double curDayMin_ = 1e9;
  int dayMinute_ = 0;
  // learned leak
  bool leakLearned_ = false;
  double leak_ = 2.0;
  long leakAt_ = 0;
  double at(int ago) const;   // 0 = newest
  void fit(int m, double& slope, double& endFit, double& resid) const;
  double outdoor() const;
  void learnDecay();
  // particle filter
  float pfN_[kPf], pfE_[kPf], pfS_[kPf], pfL_[kPf], pfB_[kPf], pfW_[kPf];
  float pfT_[5][kPf];      // scratch for resampling
  struct Pair { float v, w; };
  Pair pfSort_[kPf];
  uint32_t rng_ = 2463534242u;
  float pfLow_ = 0, pfMid_ = 0, pfHigh_ = 0, pfFresh_ = 0, pfLeak_ = 0;
  float pfNmax_ = 45, pfGppm_ = 1;
  float uni();
  float gauss();
  void pfInit();
  void pfStep(double co2);
};
