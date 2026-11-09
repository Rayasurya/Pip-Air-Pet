#include "mood.h"

#include <stdio.h>

#include "config.h"

const char* moodName(Mood m) {
  switch (m) {
    case Mood::Happy: return "happy";
    case Mood::Muggy: return "muggy";
    case Mood::Dry: return "dry";
    case Mood::Hot: return "hot";
    case Mood::Stuffy: return "stuffy";
    case Mood::Gasping: return "gasping";
    case Mood::Asleep: return "asleep";
    case Mood::Relieved: return "relieved";
    case Mood::Petted: return "petted";
    case Mood::Worried: return "worried";
    case Mood::Asking: return "asking";
    default: return "?";
  }
}

Limits Limits::defaults() {
  return {(float)cfg::CO2_STUFFY, cfg::TEMP_HOT, cfg::HUMID_DRY, cfg::HUMID_MUGGY};
}

Mood MoodEngine::instant(const Readings& r, bool asleep, const Limits& lim) {
  if (asleep) return Mood::Asleep;
  if (!r.airValid) return Mood::Happy;
  if (r.co2 > cfg::CO2_GASPING) return Mood::Gasping;  // fixed: never learned
  if (r.co2 >= lim.co2Stuffy) return Mood::Stuffy;
  if (r.tempC > lim.tempHot) return Mood::Hot;
  if (r.humidity < lim.humDry) return Mood::Dry;
  if (r.humidity > lim.humMuggy) return Mood::Muggy;
  return Mood::Happy;
}

void MoodEngine::updateDark(const Readings& r, uint32_t now) {
  if (!r.lightValid) return;
  const int raw = r.ldrRaw;
  const int lvl = cfg::LDR_DARK_LEVEL;
  const int h = cfg::LDR_HYSTERESIS;
  bool darkNow;
  if (cfg::LDR_DARK_IS_HIGH) {
    darkNow = darkTiming_ || dark_ ? raw > lvl - h : raw > lvl + h;
  } else {
    darkNow = darkTiming_ || dark_ ? raw < lvl + h : raw < lvl - h;
  }
  if (!darkNow) {
    darkTiming_ = false;
    dark_ = false;
    return;
  }
  if (!darkTiming_) {
    darkTiming_ = true;
    darkSince_ = now;
  }
  if (now - darkSince_ >= cfg::DARK_HOLD_MS) dark_ = true;
}

void MoodEngine::updateRelief(const Readings& r, uint32_t now) {
  if (!r.airValid) return;
  if (histLen_ > 0 && now - lastHistAt_ < 10000) return;
  lastHistAt_ = now;

  co2Hist_[histHead_] = r.co2;
  tHist_[histHead_] = now;
  histHead_ = (histHead_ + 1) % kHist;
  if (histLen_ < kHist) histLen_++;

  uint16_t peak = 0;
  for (int i = 0; i < histLen_; i++) {
    if (now - tHist_[i] <= cfg::RELIEF_WINDOW_MS && co2Hist_[i] > peak) peak = co2Hist_[i];
  }
  if (peak >= lim_.co2Stuffy && peak >= r.co2 + cfg::RELIEF_DROP_PPM) {
    startEvent(Mood::Relieved, now);
    // Start fresh so one window opening cheers only once.
    histLen_ = 1;
    histHead_ = 1;
    co2Hist_[0] = r.co2;
    tHist_[0] = now;
  }
}

void MoodEngine::startEvent(Mood m, uint32_t now) {
  event_ = m;
  eventUntil_ = now + cfg::EVENT_SHOW_MS;
}

void MoodEngine::pet(uint32_t now) { startEvent(Mood::Petted, now); }

Mood MoodEngine::update(const Readings& r, uint32_t now) {
  updateDark(r, now);
  updateRelief(r, now);

  const Mood target = instant(r, dark_, lim_);
  if (!started_) {
    started_ = true;
    base_ = candidate_ = target;
    candidateSince_ = now;
  } else if (target != candidate_) {
    candidate_ = target;
    candidateSince_ = now;
  }
  // Falling asleep already waited for the dark hold, and waking up should feel instant.
  const bool noHold = candidate_ == Mood::Asleep || base_ == Mood::Asleep;
  if (candidate_ != base_ && (noHold || now - candidateSince_ >= cfg::MOOD_HOLD_MS)) {
    base_ = candidate_;
  }

  // The forecast only matters while the air is still fine and Pip is awake.
  shown_ = base_ == Mood::Happy && r.soon && r.airValid ? Mood::Worried : base_;

  if (event_ != Mood::Count && (int32_t)(now - eventUntil_) >= 0) event_ = Mood::Count;
  // A sleeping pet ignores the window event but still wakes up for pets.
  if (event_ == Mood::Relieved && base_ == Mood::Asleep) event_ = Mood::Count;
  return event_ != Mood::Count ? event_ : shown_;
}

void MoodEngine::explain(Mood base, const Readings& r, char* l1, char* l2, char* l3, size_t n, const Limits& lim) {
  l1[0] = l2[0] = l3[0] = '\0';
  if (base == Mood::Asleep) {
    snprintf(l1, n, "ROOM IS DARK");
    snprintf(l2, n, "SLEEPING");
    snprintf(l3, n, "LIGHTS ON TO WAKE");
    return;
  }
  if (r.airMissing) {
    snprintf(l1, n, "NO CO2 SENSOR");
    snprintf(l2, n, "CHECK THE WIRING");
    snprintf(l3, n, "THEN RESTART");
    return;
  }
  if (!r.airValid) {
    snprintf(l1, n, "SENSOR WARMING UP");
    snprintf(l2, n, "BACK IN A MOMENT");
    return;
  }
  switch (base) {
    case Mood::Worried:
      snprintf(l1, n, "CO2 %u (+%d/MIN)", r.co2, (int)(r.risePerMin + 0.5f));
      snprintf(l2, n, "STUFFY IN ~%d MIN", r.soonEtaMin);
      snprintf(l3, n, "OPEN A WINDOW SOON?");
      break;
    case Mood::Gasping:
      snprintf(l1, n, "CO2 %u PPM", r.co2);
      snprintf(l2, n, "OVER %u", cfg::CO2_GASPING);
      snprintf(l3, n, "OPEN A WINDOW NOW");
      break;
    case Mood::Stuffy:
      snprintf(l1, n, "CO2 %u PPM", r.co2);
      snprintf(l2, n, "OVER %d", (int)(lim.co2Stuffy + 0.5f));
      snprintf(l3, n, "OPEN A WINDOW?");
      break;
    case Mood::Hot:
      snprintf(l1, n, "%.1fC", r.tempC);
      snprintf(l2, n, "OVER %.1fC", lim.tempHot);
      snprintf(l3, n, "FAN OR WINDOW?");
      break;
    case Mood::Dry:
      snprintf(l1, n, "HUMIDITY %.0f%%", r.humidity);
      snprintf(l2, n, "UNDER %.0f%%", lim.humDry);
      snprintf(l3, n, "AIR IS DRY");
      break;
    case Mood::Muggy:
      snprintf(l1, n, "HUMIDITY %.0f%%", r.humidity);
      snprintf(l2, n, "OVER %.0f%%", lim.humMuggy);
      snprintf(l3, n, "LET AIR FLOW?");
      break;
    default:
      snprintf(l1, n, "AIR IS GOOD");
      snprintf(l2, n, "CO2 %u PPM", r.co2);
      snprintf(l3, n, "%.1fC  %.0f%%", r.tempC, r.humidity);
      break;
  }
}
