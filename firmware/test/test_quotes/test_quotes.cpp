// Quote tests that run on the Mac: pio test -e native -f test_quotes
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "quotes.h"

void setUp() {}
void tearDown() {}

static char out[quotes::MAX_LINES][quotes::COLS + 1];

static void test_wrap_basic() {
  TEST_ASSERT_EQUAL(2, quotes::wrap("aaa bbb ccc", out, 5, 7));
  TEST_ASSERT_EQUAL_STRING("aaa bbb", out[0]);
  TEST_ASSERT_EQUAL_STRING("ccc", out[1]);
}

static void test_wrap_exact_fit_and_spaces() {
  TEST_ASSERT_EQUAL(1, quotes::wrap("  abc  def ", out, 5, 7));
  TEST_ASSERT_EQUAL_STRING("abc def", out[0]);
  TEST_ASSERT_EQUAL(0, quotes::wrap("   ", out, 5, 7));
}

static void test_wrap_refuses_what_does_not_fit() {
  TEST_ASSERT_EQUAL(-1, quotes::wrap("abcdefgh", out, 5, 7));        // one word longer than a line
  TEST_ASSERT_EQUAL(-1, quotes::wrap("aa bb cc dd", out, 2, 4));     // needs 4 lines, only 2 allowed
  TEST_ASSERT_EQUAL(2, quotes::wrap("aa bb cc dd", out, 2, 5));      // "aa bb" fills a 5-letter line exactly
}

// Every quote must fit the screen as written, in the font's letters, with a short author.
static void test_every_quote_fits_the_screen() {
  TEST_ASSERT_TRUE(quotes::COUNT >= 10);
  TEST_ASSERT_TRUE(quotes::COUNT <= quotes::MAX_COUNT);
  for (int i = 0; i < quotes::COUNT; i++) {
    const Quote& q = quotes::LIST[i];
    char msg[80];
    snprintf(msg, sizeof msg, "quote %d does not fit", i);
    TEST_ASSERT_TRUE_MESSAGE(quotes::wrap(q.text, out, quotes::MAX_LINES) > 0, msg);
    TEST_ASSERT_TRUE_MESSAGE(strlen(q.who) > 0 && (int)strlen(q.who) <= quotes::WHO_COLS, msg);
    const char* const parts[2] = {q.text, q.who};
    for (const char* s : parts)
      for (const char* p = s; *p; p++) {
        snprintf(msg, sizeof msg, "quote %d has a letter the screen font lacks", i);
        TEST_ASSERT_TRUE_MESSAGE(*p >= 32 && *p < 127, msg);
      }
  }
}

static void test_deck_shows_each_once_per_round() {
  quotes::Deck d(12345);
  for (int round = 0; round < 5; round++) {
    bool seen[quotes::MAX_COUNT] = {};
    for (int k = 0; k < quotes::COUNT; k++) {
      const int i = d.next();
      TEST_ASSERT_TRUE(i >= 0 && i < quotes::COUNT);
      TEST_ASSERT_FALSE(seen[i]);
      seen[i] = true;
    }
  }
}

static void test_deck_never_repeats_back_to_back() {
  for (uint32_t seed = 1; seed < 200; seed++) {
    quotes::Deck d(seed);
    int prev = d.next();
    for (int k = 0; k < quotes::COUNT * 4; k++) {
      const int i = d.next();
      TEST_ASSERT_NOT_EQUAL(prev, i);
      prev = i;
    }
  }
}

static void test_typewriter() {
  TEST_ASSERT_EQUAL(0, quotes::lettersShown(0));
  TEST_ASSERT_EQUAL(0, quotes::lettersShown(34));
  TEST_ASSERT_EQUAL(1, quotes::lettersShown(35));
  TEST_ASSERT_EQUAL(100, quotes::lettersShown(3500));
  // The longest quote is fully typed well before it leaves the screen (20 s).
  TEST_ASSERT_TRUE(quotes::lettersShown(5000) >= quotes::MAX_LINES * quotes::COLS);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wrap_basic);
  RUN_TEST(test_wrap_exact_fit_and_spaces);
  RUN_TEST(test_wrap_refuses_what_does_not_fit);
  RUN_TEST(test_every_quote_fits_the_screen);
  RUN_TEST(test_deck_shows_each_once_per_round);
  RUN_TEST(test_deck_never_repeats_back_to_back);
  RUN_TEST(test_typewriter);
  return UNITY_END();
}
