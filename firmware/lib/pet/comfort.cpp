#include "comfort.h"

#include <stdio.h>

#include "config.h"

namespace {
// Safe bands. The CO2 band stays below the fixed gasping limit.
const float kCo2Min = 800, kCo2Max = 1400;
const float kHotMin = 28, kHotMax = 34;
const float kDryMin = 20, kDryMax = 40;
const float kMugMin = 65, kMugMax = 85;
// How far past the reading a new limit lands, and how big each step is.
const float kCo2Margin = 50, kTempMargin = 0.5f, kHumMargin = 3;
const float kStep = 0.4f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float toward(float cur, float target, float lo, float hi) { return clampf(cur + kStep * (target - cur), lo, hi); }
}  // namespace

void ComfortLearner::set(const Limits& l) {
  lim_.co2Stuffy = clampf(l.co2Stuffy, kCo2Min, kCo2Max);
  lim_.tempHot = clampf(l.tempHot, kHotMin, kHotMax);
  lim_.humDry = clampf(l.humDry, kDryMin, kDryMax);
  lim_.humMuggy = clampf(l.humMuggy, kMugMin, kMugMax);
}

bool ComfortLearner::feedback(Feedback f, Mood shown, const Readings& r, char* m1, char* m2, size_t n) {
  m1[0] = m2[0] = '\0';
  if (!r.airValid) {
    snprintf(m1, n, "NO READINGS YET");
    snprintf(m2, n, "TRY AGAIN SOON");
    return false;
  }
  answers_++;
  const Limits before = lim_;
  const float co2 = r.co2, t = r.tempC, h = r.humidity;

  if (f == Feedback::FeelsFine) {
    switch (shown) {
      case Mood::Stuffy:
      case Mood::Gasping:  // the gasping limit itself never moves; only the stuffy one can rise
        lim_.co2Stuffy = toward(lim_.co2Stuffy, co2 + kCo2Margin, kCo2Min, kCo2Max);
        break;
      case Mood::Worried:  // it warned too early: lift the limit a little above where it is
        lim_.co2Stuffy = toward(lim_.co2Stuffy, lim_.co2Stuffy + 2 * kCo2Margin, kCo2Min, kCo2Max);
        break;
      case Mood::Hot:
        lim_.tempHot = toward(lim_.tempHot, t + kTempMargin, kHotMin, kHotMax);
        break;
      case Mood::Dry:
        lim_.humDry = toward(lim_.humDry, h - kHumMargin, kDryMin, kDryMax);
        break;
      case Mood::Muggy:
        lim_.humMuggy = toward(lim_.humMuggy, h + kHumMargin, kMugMin, kMugMax);
        break;
      default:  // Pip was happy and you feel fine: you agree, nothing to learn
        snprintf(m1, n, "GOOD, WE AGREE");
        snprintf(m2, n, "THE AIR IS FINE");
        return false;
    }
  } else {  // FeelsBad
    if (shown != Mood::Happy && shown != Mood::Worried) {
      snprintf(m1, n, "WE AGREE");
      snprintf(m2, n, "OPENING A WINDOW?");
      return false;
    }
    // Pip thought the air was fine. Blame whichever reading is closest to its limit.
    const float cCo2 = (co2 - (lim_.co2Stuffy - 300)) / 300;
    const float cHot = (t - (lim_.tempHot - 4)) / 4;
    const float cDry = ((lim_.humDry + 10) - h) / 10;
    const float cMug = (h - (lim_.humMuggy - 10)) / 10;
    if (cCo2 >= cHot && cCo2 >= cDry && cCo2 >= cMug) {
      lim_.co2Stuffy = toward(lim_.co2Stuffy, co2 - kCo2Margin, kCo2Min, kCo2Max);
    } else if (cHot >= cDry && cHot >= cMug) {
      lim_.tempHot = toward(lim_.tempHot, t - kTempMargin, kHotMin, kHotMax);
    } else if (cDry >= cMug) {
      lim_.humDry = toward(lim_.humDry, h + kHumMargin, kDryMin, kDryMax);
    } else {
      lim_.humMuggy = toward(lim_.humMuggy, h - kHumMargin, kMugMin, kMugMax);
    }
  }

  // Say what changed, in the units people know.
  if ((int)lim_.co2Stuffy != (int)before.co2Stuffy) {
    snprintf(m1, n, "GOT IT. CO2 LIMIT");
    snprintf(m2, n, "NOW %d PPM", (int)(lim_.co2Stuffy + 0.5f));
  } else if ((int)(lim_.tempHot * 10) != (int)(before.tempHot * 10)) {
    snprintf(m1, n, "GOT IT. HOT LIMIT");
    snprintf(m2, n, "NOW %.1fC", lim_.tempHot);
  } else if ((int)lim_.humDry != (int)before.humDry) {
    snprintf(m1, n, "GOT IT. DRY BELOW");
    snprintf(m2, n, "%d%% HUMIDITY", (int)(lim_.humDry + 0.5f));
  } else if ((int)lim_.humMuggy != (int)before.humMuggy) {
    snprintf(m1, n, "GOT IT. DAMP ABOVE");
    snprintf(m2, n, "%d%% HUMIDITY", (int)(lim_.humMuggy + 0.5f));
  } else {
    snprintf(m1, n, "NOTED");
    snprintf(m2, n, "AT MY SAFE LIMIT");  // clamped by the safe band
    return false;
  }
  return true;
}
