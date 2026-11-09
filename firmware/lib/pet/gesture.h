// Tap, double-tap, long-press and mark (a 3 s hold) from one touch pin. Pure C++, unit tested on the Mac.
#pragma once
#include <stdint.h>

// LongPress fires when a 1-3 s hold is released. Mark fires while still held, at 3 s.
enum class Gesture : uint8_t { None, Tap, DoubleTap, LongPress, Mark };

class GestureDetector {
 public:
  // Call every loop with the raw touch state. Returns at most one gesture per call.
  Gesture update(bool touching, uint32_t nowMs);
  // 0..1 while a hold is heading for a mark (drawn as a filling ring), else 0.
  float markProgress(uint32_t nowMs) const;

 private:
  bool down_ = false;
  bool markFired_ = false;
  uint32_t downAt_ = 0;
  bool tapPending_ = false;
  uint32_t lastTapAt_ = 0;
};
