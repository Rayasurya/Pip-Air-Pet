// Sound for the speaker (MAX98357A over I2S): pet sounds made in code, plus short spoken clips.
// Pure C++: it fills sample buffers; main.cpp sends them to I2S. Unit tested on the Mac.
// Speaker only. Pip has no microphone.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "mood.h"
#include "voice_clips.h"

struct Note { uint16_t hz; uint16_t ms; };  // hz 0 = a pause

constexpr uint32_t kAudioRate = 16000;      // output samples per second

class AudioOut {
 public:
  void playTune(const Note* t, uint8_t n);  // replaces the current tune (a queued clip still plays)
  void playClip(Clip c);                    // a tune queued before it plays first, then the words
  void stop();
  bool busy() const { return tune_ || clip_; }
  void setVolume(uint8_t level);            // 1 soft, 2 normal, 3 loud
  uint8_t volume() const { return level_; }
  // Fills up to `n` signed 16-bit samples. Returns how many were written (0 = silent, nothing to send).
  size_t render(int16_t* out, size_t n);

 private:
  float gain() const;
  const Note* tune_ = nullptr;
  uint8_t tuneLen_ = 0, tuneIdx_ = 0;
  uint32_t noteLeft_ = 0, noteLen_ = 0;
  float phase_ = 0;
  const ClipData* clip_ = nullptr;
  uint32_t clipPos_ = 0;                    // in output samples (each clip sample is used twice)
  uint8_t level_ = 2;
};

// The spoken line for a mood's explain screen, or Clip::None if that mood has no words.
Clip clipForMood(Mood m);
