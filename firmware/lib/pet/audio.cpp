#include "audio.h"

#include <math.h>

namespace {
constexpr float kTwoPi = 6.2831853f;
constexpr uint32_t kRamp = kAudioRate / 200;  // 5 ms fade in and out: no clicks
}  // namespace

void AudioOut::playTune(const Note* t, uint8_t n) {
  tune_ = n ? t : nullptr;
  tuneLen_ = n;
  tuneIdx_ = 0;
  noteLeft_ = noteLen_ = 0;
}

void AudioOut::playClip(Clip c) {
  if ((int)c >= (int)Clip::Count) return;
  clip_ = &kClipTable[(int)c];
  clipPos_ = 0;
}

void AudioOut::stop() {
  tune_ = nullptr;
  clip_ = nullptr;
}

void AudioOut::setVolume(uint8_t level) { level_ = level < 1 ? 1 : (level > 3 ? 3 : level); }

// Capped well below full scale: with the amplifier on 3.3 V this keeps a 0.5 W speaker safe.
float AudioOut::gain() const { return level_ == 1 ? 0.12f : (level_ == 2 ? 0.30f : 0.60f); }

size_t AudioOut::render(int16_t* out, size_t n) {
  size_t i = 0;
  const float g = gain() * 32767.0f;
  while (i < n && tune_) {
    if (noteLeft_ == 0) {
      if (tuneIdx_ >= tuneLen_) {
        tune_ = nullptr;
        break;
      }
      noteLen_ = noteLeft_ = tune_[tuneIdx_].ms * kAudioRate / 1000;
      tuneIdx_++;
      phase_ = 0;
      if (noteLeft_ == 0) continue;
    }
    const uint16_t hz = tune_[tuneIdx_ - 1].hz;
    const uint32_t done = noteLen_ - noteLeft_;
    float env = 1.0f;
    if (done < kRamp) env = (float)done / kRamp;
    if (noteLeft_ < kRamp) env = (float)noteLeft_ / kRamp;
    float s = 0;
    if (hz) {
      // A soft "chirp" tone: a sine with a little third harmonic, gentler than a buzzer's square.
      s = sinf(phase_) + 0.25f * sinf(3 * phase_);
      phase_ += kTwoPi * hz / kAudioRate;
      if (phase_ > kTwoPi) phase_ -= kTwoPi;
    }
    out[i++] = (int16_t)(s * 0.8f * env * g);
    noteLeft_--;
  }
  while (i < n && clip_) {
    const uint32_t k = clipPos_ / 2;  // 8 kHz clip, 16 kHz output: each sample used twice, smoothed
    if (k >= clip_->len) {
      clip_ = nullptr;
      break;
    }
    const float a = (clip_->pcm[k] - 128) / 128.0f;
    const float b = k + 1 < clip_->len ? (clip_->pcm[k + 1] - 128) / 128.0f : 0.0f;
    const float s = (clipPos_ & 1) ? 0.5f * (a + b) : a;
    out[i++] = (int16_t)(s * g);
    clipPos_++;
  }
  return i;
}

Clip clipForMood(Mood m) {
  switch (m) {
    case Mood::Happy: return Clip::Fresh;
    case Mood::Worried: return Clip::StaleSoon;
    case Mood::Stuffy: return Clip::Stale;
    case Mood::Gasping: return Clip::VeryStale;
    case Mood::Hot: return Clip::Hot;
    case Mood::Dry: return Clip::Dry;
    case Mood::Muggy: return Clip::Damp;
    default: return Clip::None;  // asleep, relieved, petted, asking: no words
  }
}
