// Pip quotes: short lines from books, typed onto the screen now and then, as if Pip is
// talking. Public-domain texts only (sources in quotes.cpp). Pure C++, unit tested on the Mac.
#pragma once
#include <stdint.h>

struct Quote {
  const char* text;
  const char* who;
};

namespace quotes {

constexpr int COLS = 24;       // U8g2 5x7 font: 5 px a letter, 120 of the screen's 128 px
constexpr int MAX_LINES = 5;   // rows of quote text; the bottom row holds Pip's face and the author
constexpr int WHO_COLS = 21;   // the author shares the bottom row with a small Pip face
constexpr int MAX_COUNT = 64;  // the shuffle deck's size

extern const Quote LIST[];
extern const int COUNT;

// Word-wraps `s` into at most `maxLines` lines of up to `cols` letters (cols <= COLS).
// Returns the number of lines, or -1 if it doesn't fit or one word is longer than a line.
int wrap(const char* s, char out[][COLS + 1], int maxLines, int cols = COLS);

// Typewriter: how many letters are showing `ms` after a quote started.
int lettersShown(uint32_t ms, uint32_t msPerLetter = 35);

// Goes through every quote once in a shuffled order, then reshuffles.
// A new round never starts with the quote that ended the last one.
class Deck {
 public:
  explicit Deck(uint32_t seed = 1);
  int next();  // an index into LIST

 private:
  void shuffle();
  uint32_t rand();
  uint8_t order_[MAX_COUNT];
  int pos_ = 0;
  int last_ = -1;
  uint32_t rng_;
};

}  // namespace quotes
