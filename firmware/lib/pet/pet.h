// Draws the pet for a mood. Procedural (shapes, not bitmaps), so moods can morph smoothly.
#pragma once
#include <stdint.h>

#include "gfx.h"
#include "mood.h"

class PetRenderer {
 public:
  // Draw one frame. When `mood` changes, the body morphs over ~400 ms and the
  // pet blinks through the swap, so there is never an instant cut.
  void draw(Gfx& g, Mood mood, uint32_t nowMs);
  // Full-screen reason text for explain mode (long-press).
  static void drawExplain(Gfx& g, const char* l1, const char* l2, const char* l3, const char* hint = nullptr);
  static void drawQuietIcon(Gfx& g);
  // A quote typed onto the screen (`letters` shown so far), with a small Pip bottom left whose
  // mouth moves while the letters appear. The author shows once the typing is done.
  static void drawQuote(Gfx& g, const char* const* lines, int n, const char* who, int letters, uint32_t nowMs);
  // While Pip's head is held toward a mark (0..1): a bar fills along the bottom with a hint.
  static void drawHoldBar(Gfx& g, float progress);
  // Text in the thought bubble while Worried ("~12M") or Asking ("OK?", "HI!", "SENT").
  void setSoonText(const char* s);

 private:
  Mood cur_ = Mood::Happy;
  Mood prev_ = Mood::Happy;
  uint32_t changedAt_ = 0;
  bool started_ = false;
  char soon_[8] = "SOON";
};
