// Mac preview: runs the real pet drawing code into a 128x64 framebuffer and writes
// raw frames, so moods can be judged before the OLED arrives.
// Build + run: tools/preview/run.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "mood.h"
#include "pet.h"
#include "raster_gfx.h"

class FrameBuffer : public RasterGfx {
 public:
  bool px[H][W];
  void clear() override { memset(px, 0, sizeof(px)); }
  // Tiny 3x5 font. The device uses U8g2's 5x7 font, so text is narrower here.
  void text(int x, int y, const char* s) override {
    for (; *s; s++, x += 4) glyph(x, y - 5, *s);
  }
  int textWidth(const char* s) override { return (int)strlen(s) * 4 - 1; }
  int textHeight() override { return 5; }

 protected:
  void putPixel(int x, int y, bool on) override { px[y][x] = on; }

 private:
  void glyph(int x, int y, char c) {
    static const char* keys = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,?!%:-/() ~+=";
    static const unsigned char rows[][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
        {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
        {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7},
        {7, 4, 6, 4, 4}, {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2},
        {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7}, {5, 7, 7, 5, 5}, {6, 5, 5, 5, 5}, {2, 5, 5, 5, 2},
        {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5}, {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2},
        {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5}, {5, 5, 2, 2, 2},
        {7, 1, 2, 4, 7}, {0, 0, 0, 0, 2}, {0, 0, 0, 2, 4}, {7, 1, 2, 0, 2}, {2, 2, 2, 0, 2},
        {5, 1, 2, 4, 5}, {0, 2, 0, 2, 0}, {0, 0, 7, 0, 0}, {1, 1, 2, 4, 4}, {1, 2, 2, 2, 1},
        {4, 2, 2, 2, 4}, {0, 0, 0, 0, 0}, {0, 0, 3, 6, 0}, {0, 2, 7, 2, 0}, {0, 7, 0, 7, 0}};
    if (c >= 'a' && c <= 'z') c -= 32;
    const char* k = strchr(keys, c);
    if (!k) return;
    const unsigned char* g = rows[k - keys];
    for (int r = 0; r < 5; r++)
      for (int b = 0; b < 3; b++)
        if (g[r] & (4 >> b)) pixel(x + b, y + r, true);
  }
};

static void writeFrame(FILE* f, const FrameBuffer& fb) {
  for (int y = 0; y < RasterGfx::H; y++)
    for (int x = 0; x < RasterGfx::W; x++) fputc(fb.px[y][x] ? 1 : 0, f);
}

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : ".";
  const Mood moods[] = {Mood::Happy, Mood::Stuffy, Mood::Gasping, Mood::Hot, Mood::Dry,
                        Mood::Muggy, Mood::Asleep, Mood::Relieved, Mood::Petted, Mood::Worried, Mood::Asking};
  const int fps = 20;
  FrameBuffer fb;

  // One clip per mood: 0.6 s of Happy, then the morph into the mood, then the mood.
  // Happy and Stuffy run a full loop so every acted moment is included.
  for (Mood m : moods) {
    const float secs = m == Mood::Happy ? 12.6f : (m == Mood::Stuffy ? 20.6f : 4.6f);
    const std::string path = out + "/" + moodName(m) + ".raw";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return 1;
    PetRenderer pet;
    pet.setSoonText(m == Mood::Asking ? "OK?" : "~12M");
    const uint32_t start = m == Mood::Happy || m == Mood::Stuffy ? 19400 : 10000;
    const int frames = (int)(secs * fps);
    for (int i = 0; i < frames; i++) {
      const uint32_t now = start + i * 1000 / fps;
      const bool before = m != Mood::Happy && now - start < 600;
      pet.draw(fb, before ? Mood::Happy : m, now);
      writeFrame(f, fb);
    }
    fclose(f);
  }

  // One still per mood at a telling moment, for the contact sheet.
  // The last three are acted moments: Happy mid-hop, Stuffy jolting awake, Stuffy yawning.
  const Mood stillMood[] = {Mood::Happy, Mood::Stuffy, Mood::Gasping, Mood::Hot, Mood::Dry, Mood::Muggy,
                            Mood::Asleep, Mood::Relieved, Mood::Petted, Mood::Worried, Mood::Happy, Mood::Stuffy,
                            Mood::Asking, Mood::Asking, Mood::Asking};
  const uint32_t stillAt[] = {25000, 25000, 10300, 10500, 10200, 12000, 11000, 10450, 10900, 11800, 30750, 34500,
                              10600, 10700, 10800};
  const char* bubbleFor[] = {"~12M", "~12M", "~12M", "~12M", "~12M", "~12M", "~12M", "~12M", "~12M", "~12M",
                             "~12M", "~12M", "HI!", "OK?", "SENT"};
  for (int i = 0; i < 15; i++) {
    PetRenderer pet;
    pet.setSoonText(bubbleFor[i]);
    pet.draw(fb, stillMood[i], stillAt[i]);
    char idx[4];
    snprintf(idx, sizeof(idx), "%02d", i);
    const std::string path = out + "/still_" + idx + "_" + moodName(stillMood[i]) + ".raw";
    FILE* f = fopen(path.c_str(), "wb");
    writeFrame(f, fb);
    fclose(f);
  }

  // Explain screen samples.
  Readings r;
  r.airValid = true;
  r.co2 = 1450;
  r.tempC = 27.4f;
  r.humidity = 58;
  char l1[22], l2[22], l3[22];
  MoodEngine::explain(Mood::Stuffy, r, l1, l2, l3, sizeof(l1));
  PetRenderer::drawExplain(fb, l1, l2, l3, "TAP=BAD  HOLD=FINE");
  FILE* f = fopen((out + "/explain.raw").c_str(), "wb");
  writeFrame(f, fb);
  fclose(f);
  PetRenderer::drawExplain(fb, "GOT IT. CO2 LIMIT", "NOW 1060 PPM", "");
  f = fopen((out + "/learned.raw").c_str(), "wb");
  writeFrame(f, fb);
  fclose(f);
  // Body diary: holding Pip's head toward a mark (bar two-thirds full), then the "noted" screen.
  PetRenderer holder;
  for (uint32_t t = 0; t <= 40000; t += 100) holder.draw(fb, Mood::Stuffy, t);
  PetRenderer::drawHoldBar(fb, 0.66f);
  f = fopen((out + "/mark_hold.raw").c_str(), "wb");
  writeFrame(f, fb);
  fclose(f);
  PetRenderer::drawExplain(fb, "NOTED 16:05", "I SAVED THE AIR", "QUIET FOR 1 HOUR");
  f = fopen((out + "/mark_noted.raw").c_str(), "wb");
  writeFrame(f, fb);
  fclose(f);
  return 0;
}
