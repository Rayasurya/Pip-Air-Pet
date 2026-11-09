// Turns sensor readings into one mood. Pure C++ (no Arduino), so it is unit tested on the Mac.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum class Mood : uint8_t {
  Happy,
  Muggy,
  Dry,
  Hot,
  Stuffy,
  Gasping,
  Asleep,
  Relieved,  // event: CO2 dropped fast (window opened)
  Petted,    // event: touch tap
  Worried,   // forecast: stuffy soon (only shown while the air is still fine)
  Asking,    // Kin: checking that the person is OK (bubble says OK? / HI! / SENT)
  Count
};

const char* moodName(Mood m);

struct Readings {
  bool airValid = false;  // false until the SCD40 gives its first reading
  bool airMissing = false; // the SCD40 is not answering (wiring)
  uint16_t co2 = 0;       // ppm
  float tempC = 0;
  float humidity = 0;     // % RH
  bool lightValid = false;
  uint16_t ldrRaw = 0;    // 0-4095
  // From the forecast (see forecast.h). Filled in once a minute.
  bool soon = false;       // predicted to get stuffy within ~20 min
  int soonEtaMin = 0;
  float risePerMin = 0;    // ppm per minute over the last 10 minutes
};

// Personal comfort limits. Start from config.h; comfort.h learns them from feedback.
struct Limits {
  float co2Stuffy;   // ppm
  float tempHot;     // C
  float humDry;      // % RH, below = dry
  float humMuggy;    // % RH, above = damp
  static Limits defaults();
};

class MoodEngine {
 public:
  // Call every loop. Returns the mood to show right now (events win over the base mood).
  Mood update(const Readings& r, uint32_t nowMs);
  void pet(uint32_t nowMs);  // touch tap
  // The mood behind any event: the air mood, or Worried when the forecast says "soon".
  Mood base() const { return shown_; }
  bool isDark() const { return dark_; }

  // Three short lines (max 21 chars) saying why the pet feels this way.
  static void explain(Mood base, const Readings& r, char* l1, char* l2, char* l3, size_t n,
                      const Limits& lim = Limits::defaults());
  void setLimits(const Limits& l) { lim_ = l; }
  const Limits& limits() const { return lim_; }

  // The mood the readings point to with no hold time. Exposed for tests.
  static Mood instant(const Readings& r, bool asleep, const Limits& lim = Limits::defaults());

 private:
  void updateDark(const Readings& r, uint32_t now);
  void updateRelief(const Readings& r, uint32_t now);
  void startEvent(Mood m, uint32_t now);

  Limits lim_ = Limits::defaults();
  bool started_ = false;
  Mood base_ = Mood::Happy;
  Mood shown_ = Mood::Happy;
  Mood candidate_ = Mood::Happy;
  uint32_t candidateSince_ = 0;

  bool dark_ = false;
  bool darkTiming_ = false;
  uint32_t darkSince_ = 0;

  Mood event_ = Mood::Count;  // Count = no event
  uint32_t eventUntil_ = 0;

  // CO2 history for relief detection: one sample per ~10 s over the window.
  static const int kHist = 16;
  uint16_t co2Hist_[kHist] = {0};
  uint32_t tHist_[kHist] = {0};
  int histLen_ = 0;
  int histHead_ = 0;
  uint32_t lastHistAt_ = 0;
};
