// Mac preview of the speaker: runs the device's own AudioOut code and writes WAV files,
// so the pet sounds and spoken lines can be heard before the speaker arrives.
#include <stdio.h>
#include <string>
#include <vector>

#include "audio.h"

static void writeWav(const std::string& path, const std::vector<int16_t>& s) {
  FILE* f = fopen(path.c_str(), "wb");
  const uint32_t data = s.size() * 2, rate = kAudioRate, byteRate = rate * 2;
  const uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
  const uint32_t riff = 36 + data, fmtLen = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
  fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fwrite(s.data(), 2, s.size(), f);
  fclose(f);
}

static std::vector<int16_t> run(AudioOut& a) {
  std::vector<int16_t> all;
  int16_t buf[256];
  size_t n;
  while ((n = a.render(buf, 256)) > 0) all.insert(all.end(), buf, buf + n);
  return all;
}

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : "preview-out/speaker";
  // The same tunes as main.cpp.
  static const Note petted[] = {{1568, 70}, {0, 30}, {2093, 90}};
  static const Note relieved[] = {{1047, 90}, {1319, 90}, {1568, 90}, {2093, 160}};
  static const Note askLoud[] = {{1568, 200}, {0, 100}, {1568, 200}, {0, 100}, {2093, 300}};
  AudioOut a;
  a.playTune(petted, 3);
  writeWav(out + "/petted.wav", run(a));
  a.playTune(relieved, 4);
  writeWav(out + "/relieved.wav", run(a));
  for (int v = 1; v <= 3; v++) {  // Kin's question: chime then words, at each volume
    a.setVolume(v);
    a.playTune(askLoud, 5);
    a.playClip(Clip::AskOk);
    writeWav(out + "/kin_ask_volume" + std::to_string(v) + ".wav", run(a));
  }
  a.setVolume(2);
  a.playClip(clipForMood(Mood::Stuffy));
  writeWav(out + "/explain_stuffy.wav", run(a));
  printf("Wrote WAVs to %s\n", out.c_str());
  return 0;
}
