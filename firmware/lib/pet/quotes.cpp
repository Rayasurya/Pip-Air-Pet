#include "quotes.h"

#include <string.h>

namespace quotes {

// Lines from books that keep trending online (the Stoics, the classics wave), all from
// public-domain texts or translations. Each was checked word for word against the source text
// (Gutenberg, Standard Ebooks) on 7 Nov 2026. Hamlet and Wilde drop a first word ("Lord,", "No,").
// ASCII only: the screen's font has no curly quotes or accents. Each must fit 5 lines of 24.
const Quote LIST[] = {
    {"We suffer more often in imagination than in reality.", "Seneca"},                 // Letters 13, tr. Gummere
    {"While we are postponing, life speeds by.", "Seneca"},                             // Letters 1, tr. Gummere
    {"The universe is transformation: life is opinion.", "Marcus Aurelius"},           // Meditations 4, tr. Long
    {"No longer talk at all about the kind of man that a good man ought to be, but be such.", "Marcus Aurelius"},  // 10, tr. Long
    {"Men are disturbed not by things, but by the views which they take of things.", "Epictetus"},  // Enchiridion 5, tr. Higginson
    {"Simplify, simplify.", "Thoreau"},                                                  // Walden
    {"Rather than love, than money, than fame, give me truth.", "Thoreau"},             // Walden, Conclusion
    {"Nothing can bring you peace but yourself.", "Emerson"},                           // Self-Reliance
    {"Trust thyself: every heart vibrates to that iron string.", "Emerson"},            // Self-Reliance
    {"If you shed tears when you miss the sun, you also miss the stars.", "Tagore"},     // Stray Birds 7
    {"We know what we are, but know not what we may be.", "Shakespeare"},               // Hamlet 4.5
    {"We are all in the gutter, but some of us are looking at the stars.", "Oscar Wilde"},  // Lady Windermere's Fan
    {"For every atom belonging to me as good belongs to you.", "Walt Whitman"},         // Song of Myself 1
};
const int COUNT = sizeof(LIST) / sizeof(LIST[0]);
static_assert(sizeof(LIST) / sizeof(LIST[0]) <= MAX_COUNT, "too many quotes for the deck");

int wrap(const char* s, char out[][COLS + 1], int maxLines, int cols) {
  if (cols > COLS) cols = COLS;
  int n = 0, len = 0;
  const char* p = s;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* w = p;
    while (*p && *p != ' ') p++;
    const int wl = (int)(p - w);
    if (wl > cols) return -1;
    if (n > 0 && len + 1 + wl <= cols) {  // fits on this line
      out[n - 1][len++] = ' ';
    } else {  // start a new line
      if (n == maxLines) return -1;
      n++;
      len = 0;
    }
    memcpy(&out[n - 1][len], w, wl);
    len += wl;
    out[n - 1][len] = '\0';
  }
  return n;
}

int lettersShown(uint32_t ms, uint32_t msPerLetter) {
  if (msPerLetter == 0) return 1 << 14;
  const uint32_t k = ms / msPerLetter;
  return k > (1u << 14) ? (1 << 14) : (int)k;
}

Deck::Deck(uint32_t seed) : rng_(seed ? seed : 1) {
  for (int i = 0; i < COUNT; i++) order_[i] = (uint8_t)i;
  shuffle();
}

uint32_t Deck::rand() {  // xorshift32
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

void Deck::shuffle() {
  for (int i = COUNT - 1; i > 0; i--) {
    const int j = (int)(rand() % (uint32_t)(i + 1));
    const uint8_t t = order_[i];
    order_[i] = order_[j];
    order_[j] = t;
  }
  if (COUNT > 1 && order_[0] == last_) {  // no repeat across rounds
    const uint8_t t = order_[0];
    order_[0] = order_[COUNT - 1];
    order_[COUNT - 1] = t;
  }
  pos_ = 0;
}

int Deck::next() {
  if (pos_ >= COUNT) shuffle();
  last_ = order_[pos_++];
  return last_;
}

}  // namespace quotes
