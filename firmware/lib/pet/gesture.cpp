#include "gesture.h"

#include "config.h"

Gesture GestureDetector::update(bool touching, uint32_t now) {
  if (touching && !down_) {
    down_ = true;
    markFired_ = false;
    downAt_ = now;
    return Gesture::None;
  }
  if (touching && down_) {
    if (!markFired_ && now - downAt_ >= cfg::MARK_HOLD_MS) {
      markFired_ = true;
      tapPending_ = false;
      return Gesture::Mark;
    }
    return Gesture::None;
  }
  if (!touching && down_) {
    down_ = false;
    if (markFired_) return Gesture::None;
    if (now - downAt_ >= cfg::LONG_PRESS_MS) {
      tapPending_ = false;
      return Gesture::LongPress;
    }
    if (tapPending_ && now - lastTapAt_ <= cfg::DOUBLE_TAP_GAP_MS) {
      tapPending_ = false;
      return Gesture::DoubleTap;
    }
    tapPending_ = true;
    lastTapAt_ = now;
    return Gesture::None;
  }
  // Idle: a single tap is only confirmed once the double-tap window has passed.
  if (tapPending_ && now - lastTapAt_ > cfg::DOUBLE_TAP_GAP_MS) {
    tapPending_ = false;
    return Gesture::Tap;
  }
  return Gesture::None;
}

float GestureDetector::markProgress(uint32_t now) const {
  if (!down_ || markFired_) return 0.0f;
  const uint32_t held = now - downAt_;
  if (held < cfg::LONG_PRESS_MS) return 0.0f;  // short touches never show the ring
  const float p = (float)(held - cfg::LONG_PRESS_MS) / (float)(cfg::MARK_HOLD_MS - cfg::LONG_PRESS_MS);
  return p > 1.0f ? 1.0f : p;
}
