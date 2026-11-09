#include "console.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

const char* const kHelpText =
    "Commands:\n"
    "  co2 1400    fake a CO2 reading (ppm)\n"
    "  temp 33     fake a temperature (C)\n"
    "  hum 80      fake humidity (%)\n"
    "  dark/light  fake lights off/on\n"
    "  mood hot    force a mood (happy worried stuffy gasping hot dry damp asleep relieved loved)\n"
    "  demo        cycle every mood, 6 s each\n"
    "  auto        back to the real sensors\n"
    "  pet / explain / quiet   same as tap / long-press / double-tap\n"
    "  status      show readings and mood\n"
    "  scan        list connected I2C parts\n"
    "  dump        print the minute log (tools/pull_log.py saves it)\n"
    "  clearlog    delete the log\n"
    "  kin on/off   watch over someone living alone (needs Wi-Fi clock)\n"
    "  travel on/off   pause Kin while the home is empty\n"
    "  kin status   what Kin has learned and is doing\n"
    "  family test     send a test message to the family (Telegram)\n"
    "  mode home/car/room   what Pip watches: you, a parked car, or a shared room's air\n"
    "  vehicle car/van   car mode: cabin size (a van needs more sensitive settings)\n"
    "  park / drive   car mode test: ignition off / on\n"
    "  room 180    room mode: the room's volume in cubic metres\n"
    "  volume 2    speaker loudness: 1 soft, 2 normal, 3 loud\n"
    "  voice on/off   spoken words on or off (pet sounds and safety chimes stay)\n"
    "  mark        same as holding Pip 3 s: \"I feel unwell now\" (saves the air before it)\n"
    "  marks       list the saved marks\n"
    "  quote       show a quote from a book now (one also comes every 30 min)\n"
    "  calibrate   Pip has been in fresh air 3+ min: set CO2 to outdoor level (calibrate 450 to choose)\n"
    "  bad / fine  answer Pip: the air feels bad / I feel fine (teaches your comfort)\n"
    "  learned     show what Pip has learned about you\n"
    "  forget      reset everything learned\n";

namespace {

// Lower-cases `in` into `out` and splits off the first word.
const char* firstWord(const char* in, char* out, size_t n) {
  while (*in && isspace((unsigned char)*in)) in++;
  size_t i = 0;
  while (*in && !isspace((unsigned char)*in) && i + 1 < n) out[i++] = (char)tolower((unsigned char)*in++);
  out[i] = '\0';
  while (*in && isspace((unsigned char)*in)) in++;
  return in;  // the rest of the line
}

bool parseNumber(const char* s, float& v) {
  if (!*s) return false;
  char* end = nullptr;
  v = strtof(s, &end);
  return end != s;
}

}  // namespace

bool moodFromName(const char* name, Mood& out) {
  struct Alias { const char* name; Mood mood; };
  static const Alias kAliases[] = {
      {"happy", Mood::Happy},     {"stuffy", Mood::Stuffy},   {"gasping", Mood::Gasping},
      {"hot", Mood::Hot},         {"dry", Mood::Dry},         {"damp", Mood::Muggy},
      {"muggy", Mood::Muggy},     {"asleep", Mood::Asleep},   {"sleep", Mood::Asleep},
      {"relieved", Mood::Relieved}, {"loved", Mood::Petted},  {"petted", Mood::Petted},
      {"worried", Mood::Worried}, {"soon", Mood::Worried}, {"asking", Mood::Asking},
  };
  char word[16];
  firstWord(name, word, sizeof(word));
  for (const Alias& a : kAliases) {
    if (strcmp(word, a.name) == 0) {
      out = a.mood;
      return true;
    }
  }
  return false;
}

Command parseCommand(const char* line) {
  Command c;
  char word[16];
  const char* rest = firstWord(line, word, sizeof(word));
  if (!word[0]) return c;

  struct Simple { const char* name; Cmd cmd; };
  static const Simple kSimple[] = {
      {"help", Cmd::Help}, {"?", Cmd::Help},      {"status", Cmd::Status}, {"scan", Cmd::Scan},
      {"dark", Cmd::Dark}, {"light", Cmd::Light}, {"demo", Cmd::Demo},     {"auto", Cmd::Auto},
      {"pet", Cmd::Pet},   {"explain", Cmd::Explain}, {"quiet", Cmd::Quiet},
      {"dump", Cmd::Dump}, {"clearlog", Cmd::ClearLog},
      {"kin", Cmd::Kin}, {"travel", Cmd::Travel}, {"family", Cmd::FamilyTest},
      {"voice", Cmd::Voice}, {"mode", Cmd::Mode}, {"park", Cmd::Park}, {"vehicle", Cmd::Vehicle}, {"drive", Cmd::Drive}, {"mark", Cmd::Mark}, {"marks", Cmd::Marks}, {"quote", Cmd::Quote},
      {"bad", Cmd::FeelsBad}, {"fine", Cmd::FeelsFine}, {"learned", Cmd::Learned}, {"forget", Cmd::Forget},
  };
  for (const Simple& s : kSimple) {
    if (strcmp(word, s.name) == 0) {
      c.cmd = s.cmd;
      firstWord(rest, c.arg, sizeof(c.arg));
      return c;
    }
  }

  struct Valued { const char* name; Cmd cmd; float lo, hi; };
  static const Valued kValued[] = {
      {"co2", Cmd::Co2, 300, 10000}, {"temp", Cmd::Temp, -10, 60}, {"hum", Cmd::Hum, 0, 100},
      {"volume", Cmd::Volume, 1, 3}, {"room", Cmd::RoomSize, 10, 3000},
  };
  for (const Valued& v : kValued) {
    if (strcmp(word, v.name) == 0) {
      float x;
      if (parseNumber(rest, x) && x >= v.lo && x <= v.hi) {
        c.cmd = v.cmd;
        c.value = x;
      } else {
        c.cmd = Cmd::Unknown;
      }
      return c;
    }
  }

  if (strcmp(word, "calibrate") == 0) {
    float x = 0;
    if (!*rest) {
      c.cmd = Cmd::Calibrate;               // value 0: use the configured outdoor level
    } else if (parseNumber(rest, x) && x >= 350 && x <= 600) {
      c.cmd = Cmd::Calibrate;               // outdoor air is never far from 420 ppm
      c.value = x;
    } else {
      c.cmd = Cmd::Unknown;
    }
    return c;
  }

  if (strcmp(word, "mood") == 0) {
    c.cmd = moodFromName(rest, c.mood) ? Cmd::Mood : Cmd::Unknown;
    return c;
  }
  c.cmd = Cmd::Unknown;
  return c;
}

Mood demoMoodAt(uint32_t elapsedMs) {
  // A small story: fine, air gets stale, worse, window opens, then the other moods.
  static const Mood kOrder[] = {Mood::Happy, Mood::Worried, Mood::Stuffy, Mood::Gasping, Mood::Relieved,
                                Mood::Hot,   Mood::Dry,     Mood::Muggy,  Mood::Petted,  Mood::Asleep,
                                Mood::Asking};
  const uint32_t n = sizeof(kOrder) / sizeof(kOrder[0]);
  return kOrder[(elapsedMs / kDemoStepMs) % n];
}
