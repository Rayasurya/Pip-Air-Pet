// Typed commands over USB serial, for testing before (and after) the sensors are wired.
// Pure C++, unit tested on the Mac. Type `help` in the serial monitor for the list.
#pragma once
#include "mood.h"

enum class Cmd : uint8_t {
  None,     // empty line
  Unknown,
  Help,
  Status,
  Scan,     // list I2C devices
  Co2,      // fake CO2 ppm
  Temp,     // fake temperature C
  Hum,      // fake humidity %
  Dark,     // fake lights off
  Light,    // fake lights on
  Mood,     // force a mood
  Demo,     // cycle every mood
  Auto,     // back to real sensors
  Pet,      // same as a tap
  Explain,  // same as a long-press
  Quiet,    // same as a double-tap
  Dump,     // print the minute log (for the Mac tools)
  ClearLog, // delete the log
  FeelsBad, // same as tap on the explain screen
  FeelsFine,// same as hold on the explain screen
  Learned,  // show the learned comfort limits
  Forget,   // reset the learned limits
  Kin,   // kin on | off | status (argument in `arg`)
  Travel,   // travel on | off
  FamilyTest, // family test
  Mode,       // mode home | car | room
  Vehicle,    // vehicle car | van: cabin size for car mode
  Park,       // car mode test: ignition off
  Drive,      // car mode test: ignition on
  RoomSize,   // room 180: room volume in cubic metres (room mode)
  Volume,     // volume 1|2|3 (speaker)
  Voice,      // voice on | off: spoken words (pet sounds stay)
  Mark,       // same as a 3 s hold: "I feel unwell now" (body diary)
  Marks,      // list the saved marks
  Calibrate,  // calibrate [ppm]: Pip is in fresh air now (value 0 = use cfg::FRESH_AIR_PPM)
  Quote,      // show a quote now
};

struct Command {
  Cmd cmd = Cmd::None;
  float value = 0;
  Mood mood = Mood::Happy;
  char arg[12] = "";  // the word after the command, lower-case
};

Command parseCommand(const char* line);
bool moodFromName(const char* name, Mood& out);
extern const char* const kHelpText;

// Demo mode: every mood in story order, `kDemoStepMs` each.
const uint32_t kDemoStepMs = 6000;
Mood demoMoodAt(uint32_t elapsedMs);
