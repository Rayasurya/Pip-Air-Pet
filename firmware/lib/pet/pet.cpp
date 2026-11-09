// Air Pet character, v3: "Pip", a sprout creature.
//
// Based on the Pip expression sheet (art/Pip_Sprout_Expression_Sheet.png), redrawn in code at
// about 2x so the face has enough pixels on a 128x64 OLED. Design rules: ../../../DESIGN.md
// - The leaves are the first signal: you can read the mood from the outline alone.
// - The eyes carry worry, tiredness and panic; each mood also has its own mouth.
// - A thought bubble names the cause (CO2, HOT, DRY, DAMP).
// - Comic symbols back up the face. Each mood is a short acted loop.
// - Everything continuous is a number in Pose, so mood changes morph instead of cutting.
#include "pet.h"

#include <math.h>
#include <string.h>

#include "raster_gfx.h"

namespace {

const float kPi = 3.14159265f;
const uint32_t kMorphMs = 500;
const int kFeetY = 62;  // bottom of the feet

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float smooth(float k) { k = clamp01(k); return k * k * (3 - 2 * k); }
float lerp(float a, float b, float k) { return a + (b - a) * k; }
int ri(float v) { return (int)lroundf(v); }
// 0 -> 1 -> 0 between a and b, with `ramp` seconds of easing at each end.
float window(float t, float a, float b, float ramp = 0.25f) {
  if (t <= a || t >= b) return 0;
  return smooth((t - a) / ramp) * smooth((b - t) / ramp);
}
float pulse(float t, float period, float width) { return fmodf(t, period) < width ? 1.0f : 0.0f; }

// ---------------------------------------------------------------------------
// Pose
// ---------------------------------------------------------------------------
struct EyeP {
  float w, h;     // size
  float dx, dy;   // look offset
  float top;      // upper lid, 0 open .. 1 shut
  float tilt;     // + inner corner lower (cross/tired), - inner corner higher (worried)
  float bot;      // curved lower lid: 0 none .. 0.6 smiling eyes
};

struct LeafP {
  float angle;  // radians from straight up, outward
  float bend;   // radians per pixel along the leaf (+ curls outward and down)
  float len;    // px
  float width;  // px at the widest point
};

struct Pose {
  float rx, ry;   // body radii
  float bob;      // px up
  float sway;     // px sideways
  LeafP ll, lr;   // left and right leaf
  EyeP el, er;    // left and right eye
};
static_assert(sizeof(Pose) == sizeof(float) * 26, "Pose must be all floats");

Pose blend(const Pose& a, const Pose& b, float k) {
  Pose out;
  const float* pa = (const float*)&a;
  const float* pb = (const float*)&b;
  float* po = (float*)&out;
  for (size_t i = 0; i < sizeof(Pose) / sizeof(float); i++) po[i] = lerp(pa[i], pb[i], k);
  return out;
}

EyeP eye(float top = 0, float tilt = 0, float bot = 0, float w = 8, float h = 10) {
  return {w, h, 0, 0, top, tilt, bot};
}
LeafP leaf(float angle, float bend = 0, float len = 25, float width = 14) { return {angle, bend, len, width}; }

void eyes(Pose& p, const EyeP& e) { p.el = p.er = e; }
void leaves(Pose& p, const LeafP& l) { p.ll = p.lr = l; }
void look(Pose& p, float dx, float dy) {
  p.el.dx += dx; p.er.dx += dx;
  p.el.dy += dy; p.er.dy += dy;
}
void blinkBoth(Pose& p, float amount) {
  p.el.top += (1 - p.el.top) * amount;
  p.er.top += (1 - p.er.top) * amount;
}
float naturalBlink(float t, float period = 3.7f) {
  const float ph = fmodf(t, period);
  return ph < 0.15f ? sinf(kPi * ph / 0.15f) : 0;
}

Pose base() {
  Pose p;
  p.rx = 23; p.ry = 17; p.bob = 0; p.sway = 0;
  leaves(p, leaf(0.62f));
  eyes(p, eye());
  return p;
}

// ---- One function per mood: t = clock seconds, s = seconds since the mood started ----

Pose poseHappy(float t) {
  Pose p = base();
  const float c = fmodf(t, 12.0f);
  p.bob = 0.8f * sinf(2 * kPi * t / 1.6f);
  p.ll.angle += 0.06f * sinf(t * 2.1f);  // leaves sway in a breeze
  p.lr.angle += 0.06f * sinf(t * 2.1f + 1);
  look(p, -3 * window(c, 2.8f, 4.0f) + 3 * window(c, 4.0f, 5.2f), 0);
  blinkBoth(p, window(c, 5.5f, 5.65f, 0.07f) + window(c, 5.75f, 5.9f, 0.07f));
  // Hop: crouch, jump (leaves trail down), land.
  const float crouch = window(c, 6.2f, 6.5f, 0.12f) + window(c, 7.1f, 7.35f, 0.1f);
  const float air = window(c, 6.45f, 7.15f, 0.3f);
  p.ry *= 1 - 0.12f * crouch + 0.08f * air;
  p.rx *= 1 + 0.07f * crouch - 0.05f * air;
  p.bob += 6 * air;
  p.ll.angle += 0.5f * air; p.lr.angle += 0.5f * air;
  p.ll.bend += 0.03f * air; p.lr.bend += 0.03f * air;
  // Curious: one eye wider, head tilt, one leaf perks up.
  const float cur = window(c, 8.0f, 9.6f, 0.3f);
  p.el.h += 2 * cur; p.el.w += 1 * cur;
  p.er.top += 0.3f * cur;
  look(p, 2 * cur, -1 * cur);
  p.sway += 1.5f * cur;
  p.lr.angle -= 0.35f * cur;
  // Waves with the right leaf.
  const float wave = window(c, 9.8f, 11.4f, 0.3f);
  p.lr.angle += wave * (0.6f + 0.35f * sinf(t * 14));
  p.el.bot += 0.4f * wave; p.er.bot += 0.4f * wave;
  if (c < 5.4f || c > 6.0f) blinkBoth(p, naturalBlink(t));
  return p;
}

Pose poseStuffy(float t) {  // drowsy: droops, nods off, jolts awake, yawns
  Pose p = base();
  const float c = fmodf(t, 20.0f);
  float lids = 0.35f + 0.4f * smooth(c / 7.0f);
  const float nod = window(c, 7.0f, 7.7f, 0.25f);
  const float jolt = window(c, 7.7f, 8.4f, 0.08f);
  if (c > 7.7f && c < 13) lids = 0.25f + 0.45f * smooth((c - 8.2f) / 4.5f);
  if (c >= 13) lids = 0.5f;
  lids += (0.95f - lids) * nod;
  lids *= 1 - 0.8f * jolt;
  eyes(p, eye(lids, -0.25f));
  p.el.h += 2 * jolt; p.er.h += 2 * jolt;
  p.bob = -2 * nod + 0.4f * sinf(2 * kPi * t / 3.5f);
  look(p, 0, 1.5f * nod);
  p.sway = 1.2f * jolt * sinf(t * 40);
  // Leaves sag more as it gets drowsier, spring up on the jolt.
  const float sag = clamp01(lids);
  leaves(p, leaf(0.9f + 0.4f * sag - 0.4f * jolt, 0.012f + 0.03f * sag, 25 + 4 * sag, 13));
  // Yawn and stretch (13 s - 16 s): leaves lift like arms.
  const float yawn = window(c, 13.0f, 16.0f, 0.6f);
  p.ry *= 1 + 0.07f * yawn;
  p.ll.angle -= 0.8f * yawn; p.lr.angle -= 0.8f * yawn;
  p.ll.bend *= 1 - yawn; p.lr.bend *= 1 - yawn;
  return p;
}

Pose poseGasping(float t) {  // panic: wide eyes, shaking, leaves flapping
  Pose p = base();
  eyes(p, eye(0, 0, 0, 10, 11));
  look(p, 0, 1);
  p.sway = 1.0f * sinf(t * 45);
  p.bob = 0.5f * sinf(t * 9);
  const float flap = sinf(t * 14);
  p.ll = leaf(0.6f + 0.45f * flap, 0.01f);
  p.lr = leaf(0.6f - 0.45f * flap, 0.01f);
  return p;
}

Pose poseHot(float t) {  // wilted: heavy lids, panting, fanning with one leaf
  Pose p = base();
  eyes(p, eye(0.45f, -0.15f, 0.1f));
  p.bob = 0.6f * sinf(2 * kPi * t / 0.8f);
  p.ll = leaf(1.25f, 0.045f, 29, 13);
  p.lr = leaf(1.0f + 0.5f * (0.5f + 0.5f * sinf(t * 16)), 0.02f, 24, 13);  // fanning
  blinkBoth(p, naturalBlink(t, 4.5f));
  return p;
}

Pose poseDry(float t) {  // parched: tired squint, leaves curled into thin tendrils
  Pose p = base();
  eyes(p, eye(0.5f, -0.1f, 0.25f));
  p.rx = 22;
  p.bob = 0.5f * sinf(2 * kPi * t / 2.2f);
  leaves(p, leaf(0.35f, 0.11f + 0.02f * sinf(t * 1.5f), 23, 6));
  look(p, 2 * window(fmodf(t, 6), 3.0f, 5.0f), -1 * window(fmodf(t, 6), 3.0f, 5.0f));
  blinkBoth(p, naturalBlink(t, 3.0f));
  return p;
}

Pose poseDamp(float t) {  // sticky and sad: squashed, worried droopy eyes, heavy leaves
  Pose p = base();
  p.rx = 26; p.ry = 14;
  eyes(p, eye(0.3f, -0.4f, 0, 8, 9));
  p.bob = 0.3f * sinf(2 * kPi * t / 3.0f);
  leaves(p, leaf(1.25f, 0.045f, 29, 13));
  blinkBoth(p, window(fmodf(t, 5.0f), 0, 0.9f, 0.35f));  // slow heavy blink
  return p;
}

Pose poseAsleep(float t) {  // leaves folded down like a blanket
  Pose p = base();
  p.ry = 16 + 0.7f * sinf(2 * kPi * t / 4.0f);  // breathing
  p.rx = 24;
  eyes(p, eye(1));
  leaves(p, leaf(1.3f, 0.045f, 30, 14));
  return p;
}

Pose poseRelieved(float s) {  // big stretch and a sigh, leaves spread wide
  Pose p = base();
  const float stretch = window(s, 0.0f, 1.4f, 0.5f);
  p.ry *= 1 + 0.08f * stretch;
  p.rx *= 1 - 0.06f * stretch;
  eyes(p, eye(0, 0, 0.6f));
  leaves(p, leaf(1.05f - 0.3f * stretch, -0.01f, 24, 14));
  p.bob = 1.2f * fabsf(sinf(2 * kPi * s / 0.9f)) * (1 - stretch);
  return p;
}

Pose poseLoved(float t) {  // heart eyes, wiggle, leaves perk up and bounce
  Pose p = base();
  eyes(p, eye(0, 0, 0.4f));
  p.sway = 1.8f * sinf(t * 9);
  leaves(p, leaf(0.55f + 0.12f * sinf(t * 9)));
  return p;
}

Pose poseWorried(float t) {  // forecast says stuffy soon: uneasy, keeps checking the bubble
  Pose p = base();
  eyes(p, eye(0.1f, -0.25f));
  const float glance = window(fmodf(t, 4.0f), 1.0f, 2.6f, 0.3f);
  look(p, 2.5f * glance, -1.5f * glance);
  p.bob = 0.5f * sinf(2 * kPi * t / 2.0f);
  p.sway = 0.6f * sinf(t * 3.1f);  // fidgeting
  leaves(p, leaf(0.85f, 0.008f));
  p.lr.angle -= 0.25f * glance;  // the leaf near the bubble perks up as it looks
  blinkBoth(p, naturalBlink(t, 3.1f));
  return p;
}

Pose poseAsking(float t) {  // Kin check-in: looks right at you, one leaf raised like a hand
  Pose p = base();
  eyes(p, eye(0, 0, 0, 9, 11));          // a little wider: attentive, not scared
  p.bob = 1.2f * fabsf(sinf(2 * kPi * t / 1.4f));
  p.ll = leaf(0.7f, 0.01f);
  p.lr = leaf(-0.15f + 0.25f * sinf(t * 6), -0.005f);  // raised and gently waving
  blinkBoth(p, naturalBlink(t, 3.0f));
  return p;
}

Pose poseFor(Mood m, float t, float s) {
  switch (m) {
    case Mood::Asking: return poseAsking(t);
    case Mood::Worried: return poseWorried(t);
    case Mood::Stuffy: return poseStuffy(t);
    case Mood::Gasping: return poseGasping(t);
    case Mood::Hot: return poseHot(t);
    case Mood::Dry: return poseDry(t);
    case Mood::Muggy: return poseDamp(t);
    case Mood::Asleep: return poseAsleep(t);
    case Mood::Relieved: return poseRelieved(s);
    case Mood::Petted: return poseLoved(t);
    case Mood::Happy:
    default: return poseHappy(t);
  }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void heartShape(RasterGfx& g, int cx, int cy, float sc, bool on = true) {
  const int r = ri(3 * sc) < 1 ? 1 : ri(3 * sc);
  g.disc(cx - r + 1, cy - 1, r, on);
  g.disc(cx + r - 1, cy - 1, r, on);
  const int rows = 2 * r;
  for (int j = 0; j <= rows; j++) {
    const int half = ri((2 * r - 1) * (1 - (float)j / rows));
    g.line(cx - half, cy + j, cx + half, cy + j, on);
  }
}

void drop(RasterGfx& g, int x, int y, int r = 2, bool on = true) {  // teardrop, tip up
  g.disc(x, y, r, on);
  for (int i = 1; i <= r + 1; i++) g.line(x - (r + 1 - i) / 2, y - r - i + 1, x + (r + 1 - i) / 2, y - r - i + 1, on);
}

// A leaf grows from (bx, by) along a bending centre line. dir = -1 left, +1 right.
// Drawn twice: a dark halo first so a leaf lying over the head stays readable.
void drawLeaf(RasterGfx& g, float bx, float by, const LeafP& l, int dir) {
  const float step = 0.5f;
  for (int pass = 0; pass < 2; pass++) {
    float x = bx, y = by, a = l.angle;
    for (float s = 0; s <= l.len; s += step) {
      // sinf(pi) in float is about -9e-8, and powf of a negative number is NaN. Clamp it.
      const float prof = fmaxf(0.0f, sinf(kPi * clamp01(s / l.len)));
      const float halfW = 0.5f * l.width * powf(prof, 0.7f);
      const int r = ri(halfW);
      if (pass == 0) g.disc(ri(x), ri(y), r + 1, false);
      else if (r >= 1) g.disc(ri(x), ri(y), r, true);
      else g.pixel(ri(x), ri(y), true);
      x += dir * sinf(a) * step;
      y -= cosf(a) * step;
      a += l.bend * step;
    }
  }
  // Midrib: a dark vein down the middle of wide leaves.
  if (l.width >= 7) {
    float x = bx, y = by, a = l.angle;
    for (float s = 0; s <= l.len; s += 0.5f) {
      if (s > l.len * 0.3f && s < l.len * 0.8f) g.pixel(ri(x), ri(y), false);
      x += dir * sinf(a) * 0.5f;
      y -= cosf(a) * 0.5f;
      a += l.bend * 0.5f;
    }
  }
}

enum class EyeStyle { Normal, Panic, Squeeze, Heart, Sleep };

// Eyes are dark shapes on the lit body. Eyelids are drawn in the body colour.
void drawEye(RasterGfx& g, int cx, int cy, const EyeP& e, bool left, EyeStyle style, float t) {
  const int w = ri(e.w), h = ri(e.h);
  const int ex = ri(cx + e.dx), ey = ri(cy + e.dy);
  const int x0 = ex - w / 2, y0 = ey - h / 2;

  switch (style) {
    case EyeStyle::Squeeze: {  // > <
      const int d = left ? 1 : -1;
      for (int k = 0; k < 2; k++) {
        g.line(ex - 3 * d, ey - 3 + k, ex + 2 * d, ey + k, false);
        g.line(ex + 2 * d, ey + k, ex - 3 * d, ey + 3 + k, false);
      }
      return;
    }
    case EyeStyle::Heart:
      heartShape(g, ex, ey - 1, 1.3f + 0.15f * sinf(t * 9), false);
      g.pixel(ex - 3, ey - 3, true);
      return;
    case EyeStyle::Sleep:
      for (int dx = -4; dx <= 4; dx++) {
        const int dy = ri(sqrtf(16.0f - dx * dx) * 0.45f);
        g.pixel(ex + dx, ey + dy, false);
        g.pixel(ex + dx, ey + dy + 1, false);
      }
      return;
    case EyeStyle::Panic: {  // "O" eyes with a tiny shaking pupil, worried brows
      g.fillRoundBox(x0, y0, w, h, w / 2, false);
      g.fillRoundBox(x0 + 2, y0 + 2, w - 4, h - 4, (w - 4) / 2, true);
      g.disc(ex + ri(sinf(t * 31)), ey + 1, 1, false);
      const int d = left ? 1 : -1;
      g.line(ex - 4 * d, y0 - 2, ex + 3 * d, y0 - 4, false);
      return;
    }
    case EyeStyle::Normal:
      break;
  }

  g.fillRoundBox(x0, y0, w, h, w / 2, false);
  if (h > 5 && e.bot < 0.2f) g.box(x0 + 2, y0 + 2, 2, 2, true);  // glint
  const int side = left ? 1 : -1;  // the inner corner faces the middle
  const float tanT = tanf(e.tilt);
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) {
      const float rel = (float)(x - ex);
      const float topLine = y0 + e.top * h + tanT * rel * side;
      const float xn = rel / (w * 0.5f);
      const float botLine = y0 + h - e.bot * h + e.bot * h * 0.6f * xn * xn;
      if (y < topLine || (e.bot > 0.01f && y > botLine)) g.pixel(x, y, true);
    }
  // Lid edge: a dark line along the lid so heavy lids read as lids.
  if (e.top > 0.2f && e.top < 0.85f) {
    for (int x = x0; x < x0 + w; x++) {
      const int y = ri(y0 + e.top * h + tanT * (x - ex) * side);
      if (y >= y0 - 1 && y < y0 + h) g.pixel(x, y, false);
    }
  }
  if (e.top >= 0.85f) g.box(x0, y0 + h - 2, w, 2, false);  // shut: a lash line
}

// Thought bubble, top right.
void bubble(RasterGfx& g) {
  g.fillRoundBox(100, 0, 28, 17, 5, false);
  g.roundFrame(100, 0, 28, 17, 5);
  g.disc(96, 20, 1);
  g.pixel(93, 23);
}
void iconWord(RasterGfx& g, const char* s) {
  g.text(114 - g.textWidth(s) / 2, 8 + g.textHeight() / 2, s);
}
void iconWindow(RasterGfx& g) {
  g.frame(108, 3, 13, 11);
  g.line(114, 3, 114, 13);
  g.line(108, 8, 120, 8);
}

void zed(RasterGfx& g, int x, int y, int s) {
  g.line(x, y, x + s, y);
  g.line(x + s, y, x, y + s);
  g.line(x, y + s, x + s, y + s);
}

void sparkle(RasterGfx& g, int x, int y, int s) {
  if (s <= 0) return;
  g.line(x - s, y, x + s, y);
  g.line(x, y - s, x, y + s);
}

void heatLine(RasterGfx& g, int x, int y, float t) {
  for (int i = 0; i < 8; i++) g.pixel(x + ri(sinf(t * 7 - i * 0.9f) * 1.5f), y - i);
}

void blush(RasterGfx& g, int x, int y) {  // "///" in dark on the body
  for (int i = 0; i < 3; i++) g.line(x + i * 2, y + 1, x + i * 2 + 1, y, false);
}

// Small dark mouths on the lit body.
void mouthSmile(RasterGfx& g, int x, int y, int r) {
  for (int dx = -r; dx <= r; dx++) g.pixel(x + dx, y + ri(sqrtf((float)(r * r - dx * dx)) * 0.6f), false);
}
void mouthOpen(RasterGfx& g, int x, int y, int rx, int ry) {  // "D" laugh
  g.fillEllipse(x, y, rx, ry, false);
  g.box(x - rx - 1, y - ry - 1, 2 * rx + 3, ry + 1, true);
}
void mouthFrown(RasterGfx& g, int x, int y, int r) {
  for (int dx = -r; dx <= r; dx++) g.pixel(x + dx, y - ri(sqrtf((float)(r * r - dx * dx)) * 0.6f), false);
}

}  // namespace

// ---------------------------------------------------------------------------

void PetRenderer::draw(Gfx& gIn, Mood mood, uint32_t now) {
  RasterGfx& g = static_cast<RasterGfx&>(gIn);
  if (!started_) {
    started_ = true;
    cur_ = prev_ = mood;
    changedAt_ = now - kMorphMs;
  }
  if (mood != cur_) {
    prev_ = cur_;
    cur_ = mood;
    changedAt_ = now;
  }
  const float t = now / 1000.0f;
  const float s = (now - changedAt_) / 1000.0f;
  const float k = clamp01((now - changedAt_) / (float)kMorphMs);
  const Mood face = k < 0.5f ? prev_ : cur_;  // discrete features swap mid-blink

  Pose p = blend(poseFor(prev_, t, s), poseFor(cur_, t, s), smooth(k));
  if (k < 1) blinkBoth(p, 1 - fabsf(1 - 2 * k));

  const int rx = ri(p.rx), ry = ri(p.ry);
  const int cx = ri(64 + p.sway);
  const int cy = ri(kFeetY - 3 - p.ry - p.bob);
  const float c = fmodf(t, 20.0f);

  g.clear();

  // Ground shadow.
  const int half = ri(p.rx * 0.8f - p.bob * 1.5f);
  for (int x = 64 - half; x <= 64 + half; x += 2) g.pixel(x, 63);

  // Body: a bulb with two stubby feet and a notch between them.
  g.fillRoundBox(cx - 13, cy + ry - 5, 8, 8, 2);
  g.fillRoundBox(cx + 5, cy + ry - 5, 8, 8, 2);
  g.fillEllipse(cx, cy, rx, ry);
  g.box(cx - 2, cy + ry - 1, 5, 3, false);

  // Stem and leaves.
  const int top = cy - ry + 1;
  g.box(cx - 1, top - 3, 2, 4);
  drawLeaf(g, cx - 0.5f, top - 3, p.ll, -1);
  drawLeaf(g, cx + 0.5f, top - 3, p.lr, 1);
  g.box(cx - 1, top - 3, 2, 3);  // keep the stem joint clean over the leaf halos

  // Eyes.
  EyeStyle style = EyeStyle::Normal;
  if (face == Mood::Gasping) style = EyeStyle::Panic;
  if (face == Mood::Petted) style = EyeStyle::Heart;
  if (face == Mood::Asleep) style = EyeStyle::Sleep;
  if (face == Mood::Stuffy && window(c, 13.4f, 15.6f, 0.05f) > 0.5f) style = EyeStyle::Squeeze;
  const int ey = cy - 1;
  drawEye(g, cx - 9, ey, p.el, true, style, t);
  drawEye(g, cx + 9, ey, p.er, false, style, t);

  // Mouth and props.
  const int my = cy + ri(p.ry * 0.45f);
  switch (face) {
    case Mood::Happy: {
      const float cc = fmodf(t, 12.0f);
      if (window(cc, 6.2f, 7.4f) > 0.3f || window(cc, 9.8f, 11.4f) > 0.3f) mouthOpen(g, cx, my, 3, 3);
      else mouthSmile(g, cx, my - 1, 3);
      blush(g, cx - 18, my - 3);
      blush(g, cx + 13, my - 3);
      break;
    }
    case Mood::Stuffy: {
      const float yawn = window(c, 13.0f, 16.0f, 0.6f);
      if (yawn > 0.05f) g.fillEllipse(cx, my, 2 + ri(yawn), 1 + ri(3 * yawn), false);
      else mouthFrown(g, cx, my + 1, 2);
      if (window(c, 7.7f, 8.6f, 0.05f) > 0.5f) {  // jolt lines
        g.line(cx - rx - 7, cy - 9, cx - rx - 4, cy - 6);
        g.line(cx + rx + 7, cy - 9, cx + rx + 4, cy - 6);
      }
      bubble(g);
      iconWord(g, "CO2");
      break;
    }
    case Mood::Gasping:
      g.fillEllipse(cx, my, 3, 2 + ri(1.5f * fabsf(sinf(t * 5))), false);
      if (pulse(t, 0.6f, 0.4f)) { g.box(20, 6, 2, 8); g.box(20, 16, 2, 2); }
      drop(g, ri(cx - rx - 4 - fmodf(t * 20, 10)), ri(cy - 6 + fmodf(t * 20, 10) * 0.4f), 1);
      drop(g, ri(cx + rx + 4 + fmodf(t * 20 + 5, 10)), ri(cy - 6 + fmodf(t * 20 + 5, 10) * 0.4f), 1);
      bubble(g);
      if (pulse(t, 2.0f, 1.0f)) iconWord(g, "CO2"); else iconWindow(g);
      break;
    case Mood::Hot: {
      mouthOpen(g, cx, my - 1, 3, 3);
      const int tg = my + 2 + ri(sinf(t * 8));  // tongue out, panting
      g.fillEllipse(cx, tg, 2, 2, false);
      g.fillEllipse(cx, tg, 1, 1, true);
      heatLine(g, cx - rx - 8, cy - 2, t);
      heatLine(g, cx - rx - 2, cy - ry - 2, t + 2);
      drop(g, cx + rx + 3, ri(cy - 4 + fmodf(t * 10, 14)), 2);
      bubble(g);
      iconWord(g, "HOT");
      break;
    }
    case Mood::Asking:
      g.fillEllipse(cx, my, 2, 2, false);  // a small "o": asking
      blush(g, cx - 18, my - 3);
      blush(g, cx + 13, my - 3);
      bubble(g);
      iconWord(g, soon_);
      break;
    case Mood::Worried:
      for (int dx = -3; dx <= 3; dx++) g.pixel(cx + dx, my + ((dx + 10) % 2), false);  // uneasy wavy mouth
      drop(g, cx - rx + 2, cy - ry + 6, 1, false);  // one nervous sweat drop on the forehead
      bubble(g);
      iconWord(g, soon_);
      break;
    case Mood::Dry:
      for (int dx = -3; dx <= 3; dx++) g.pixel(cx + dx, my + (dx & 1), false);  // cracked lips
      bubble(g);
      iconWord(g, "DRY");
      break;
    case Mood::Muggy:
      for (int dx = -4; dx <= 4; dx++) g.pixel(cx + dx, my + ((dx + 10) % 2), false);
      drop(g, cx - rx - 3, ri(cy + 2 + fmodf(t * 5, 10)), 2);
      drop(g, cx + rx + 3, ri(cy + 2 + fmodf(t * 5 + 4, 10)), 1);
      bubble(g);
      iconWord(g, "DAMP");
      break;
    case Mood::Asleep:
      mouthSmile(g, cx, my - 1, 2);
      for (int i = 0; i < 3; i++) {
        const float q = fmodf(t / 3 + i / 3.0f, 1.0f);
        zed(g, ri(cx - rx - 14 + q * 6), ri(cy - ry - 2 - q * 14), 3 + ri(q * 3));
      }
      break;
    case Mood::Relieved: {
      mouthOpen(g, cx, my - 1, 4, 3);
      if (s > 0.3f && s < 1.6f) {  // sigh puffs
        const float q = (s - 0.3f) / 1.3f;
        g.disc(ri(cx - rx - 2 - q * 8), ri(my - q * 4), 1 + ri(q));
      }
      for (int i = 0; i < 4; i++) {
        const int sx = cx + (i % 2 ? 1 : -1) * (rx + 10 + (i / 2) * 6);
        sparkle(g, sx, cy - 8 + (i / 2) * 14, ri(2.5f * fabsf(sinf(t * 4 + i))));
      }
      break;
    }
    case Mood::Petted: {
      mouthOpen(g, cx, my - 1, 3, 3);
      blush(g, cx - 18, my - 3);
      blush(g, cx + 13, my - 3);
      for (int i = 0; i < 3; i++) {
        const float q = fmodf(s * 0.7f + i / 3.0f, 1.0f);
        const int hx = i == 1 ? cx + rx + 8 : (i == 0 ? cx - rx - 9 : cx + rx + 2);
        heartShape(g, hx, ri(cy + 4 - q * 26), 0.6f + 0.25f * q);
      }
      break;
    }
    default:
      break;
  }
}

void PetRenderer::drawExplain(Gfx& g, const char* l1, const char* l2, const char* l3, const char* hint) {
  RasterGfx& rg = static_cast<RasterGfx&>(g);
  rg.clear();
  rg.roundFrame(0, 0, 128, 64, 6);
  const char* lines[3] = {l1, l2, l3};
  const int th = g.textHeight();
  const int top = hint ? 11 : 16, gap = hint ? 12 : 17;
  for (int i = 0; i < 3; i++) {
    if (!lines[i] || !lines[i][0]) continue;
    const int w = g.textWidth(lines[i]);
    g.text((128 - w) / 2, top + i * gap + th / 2, lines[i]);
  }
  if (hint && hint[0]) {  // the answer options, in their own pill
    rg.roundFrame(8, 47, 112, 14, 5);
    g.text((128 - g.textWidth(hint)) / 2, 54 + th / 2, hint);
  }
}

void PetRenderer::setSoonText(const char* s) {
  strncpy(soon_, s, sizeof(soon_) - 1);
  soon_[sizeof(soon_) - 1] = '\0';
}

void PetRenderer::drawQuote(Gfx& g, const char* const* lines, int n, const char* who, int letters, uint32_t nowMs) {
  g.clear();
  const int pitch = 9;                              // 7 px letters + 2 px gap
  const int top = (52 - ((n - 1) * pitch + 7)) / 2;  // centred above the bottom row
  int left = letters;
  bool typing = false;
  for (int i = 0; i < n; i++) {
    if (left <= 0) {
      typing = true;
      break;
    }
    const int len = (int)strlen(lines[i]);
    const int k = left < len ? left : len;
    char part[32];
    memcpy(part, lines[i], k);
    part[k] = '\0';
    g.text((128 - g.textWidth(lines[i])) / 2, top + 6 + i * pitch, part);  // centred on the full line, so it never shifts
    left -= k;
    if (k < len) {
      typing = true;
      break;
    }
  }
  // Small Pip, bottom left, as pixel art (15 x 14): two leaves, a round body, two eyes, and a
  // mouth that opens and closes while the letters appear.
  static const char* const kMiniPip[14] = {
      "..###.....###..",
      ".#####...#####.",
      "..####...####..",
      "...###...###...",
      ".....##.##.....",
      "......###......",
      "....#######....",
      "..###########..",
      ".#############.",
      ".##..#####..##.",
      ".##..#####..##.",
      ".#############.",
      "..####...####..",
      "...#########...",
  };
  const bool open = typing && (nowMs / 120) % 2 == 0;
  for (int r = 0; r < 14; r++)
    for (int c = 0; c < 15; c++) {
      const bool mouthOpen = open && r == 11 && c >= 6 && c <= 8;
      if (kMiniPip[r][c] == '#' && !mouthOpen) g.pixel(2 + c, 50 + r);
    }
  if (!typing && who && who[0]) g.text(124 - g.textWidth(who), 62, who);
}

void PetRenderer::drawQuietIcon(Gfx& g) {
  g.box(1, 4, 3, 4);
  g.line(4, 4, 7, 1);
  g.line(7, 1, 7, 10);
  g.line(7, 10, 4, 7);
  g.line(0, 11, 10, 0);
}

void PetRenderer::drawHoldBar(Gfx& g, float progress) {
  if (progress <= 0) return;
  if (progress > 1) progress = 1;
  const char* hint = "HOLD: I FEEL UNWELL";
  g.box(0, 44, 128, 20, false);  // clear a strip so the hint is readable over the face
  g.text((128 - g.textWidth(hint)) / 2, 52, hint);  // y is the text baseline
  g.frame(4, 56, 120, 7);
  g.box(6, 58, (int)(116 * progress), 3);
}
