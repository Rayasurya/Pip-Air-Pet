// Learns what *you* find comfortable from two gestures on the explain screen:
//   tap  = "the air feels bad"     hold = "I feel fine"
// Each answer nudges one personal limit a little. Limits can only move inside a safe
// band, and the "gasping" CO2 limit never moves, so Pip cannot learn to ignore bad air.
// Pure C++, unit tested on the Mac with a simulated user.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "mood.h"

enum class Feedback : uint8_t { FeelsBad, FeelsFine };

class ComfortLearner {
 public:
  ComfortLearner() : lim_(Limits::defaults()) {}

  const Limits& limits() const { return lim_; }
  void set(const Limits& l);  // e.g. loaded from flash; clamped into the safe band
  void reset() { lim_ = Limits::defaults(); answers_ = 0; }
  int answers() const { return answers_; }

  // `shown` is the air mood Pip showed when you answered (not an event like Petted).
  // Returns true if a limit changed. `msg1`/`msg2` say what Pip learned (max 21 chars).
  bool feedback(Feedback f, Mood shown, const Readings& r, char* msg1, char* msg2, size_t n);

 private:
  Limits lim_;
  int answers_ = 0;
};
