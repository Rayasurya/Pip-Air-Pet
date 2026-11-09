// Shapes built from pixel(). The device and the Mac preview both use this, so the
// pet looks pixel-identical in both (only text differs). Subclasses implement
// putPixel, clear and the text calls.
#pragma once
#include <stdlib.h>

#include "gfx.h"

class RasterGfx : public Gfx {
 public:
  static const int W = 128, H = 64;

  void pixel(int x, int y, bool on = true) override {
    if (x >= 0 && x < W && y >= 0 && y < H) putPixel(x, y, on);
  }
  void line(int x0, int y0, int x1, int y1, bool on = true) override {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      pixel(x0, y0, on);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void box(int x, int y, int w, int h, bool on = true) override {
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++) pixel(i, j, on);
  }
  void frame(int x, int y, int w, int h, bool on = true) override {
    line(x, y, x + w - 1, y, on);
    line(x, y + h - 1, x + w - 1, y + h - 1, on);
    line(x, y, x, y + h - 1, on);
    line(x + w - 1, y, x + w - 1, y + h - 1, on);
  }
  void disc(int x, int y, int r, bool on = true) override { fillEllipse(x, y, r, r, on); }
  void fillEllipse(int x, int y, int rx, int ry, bool on = true) override {
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    if (rx > W) rx = W;  // a bad size must never turn into a near-endless loop
    if (ry > W) ry = W;
    // Slightly under 1 trims the single-pixel nubs at the ends of each axis.
    const float limit = 0.95f;
    for (int dy = -ry; dy <= ry; dy++)
      for (int dx = -rx; dx <= rx; dx++)
        if ((float)dx * dx / (rx * rx) + (float)dy * dy / (ry * ry) <= limit) pixel(x + dx, y + dy, on);
  }

  // Ellipse outline `t` pixels thick.
  void ellipseRing(int x, int y, int rx, int ry, int t, bool on = true) {
    const int irx = rx - t, iry = ry - t;
    for (int dy = -ry; dy <= ry; dy++)
      for (int dx = -rx; dx <= rx; dx++) {
        const float o = (float)dx * dx / (rx * rx) + (float)dy * dy / (ry * ry);
        if (o > 0.95f) continue;
        const bool inner = irx > 0 && iry > 0 &&
                           (float)dx * dx / (irx * irx) + (float)dy * dy / (iry * iry) <= 0.95f;
        if (!inner) pixel(x + dx, y + dy, on);
      }
  }
  // Filled rectangle with rounded corners; (x, y) is the top-left.
  void fillRoundBox(int x, int y, int w, int h, int r, bool on = true) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int cx = i < r ? r : (i >= w - r ? w - r - 1 : i);
        int cy = j < r ? r : (j >= h - r ? h - r - 1 : j);
        const int dx = i - cx, dy = j - cy;
        if (dx * dx + dy * dy <= r * r) pixel(x + i, y + j, on);
      }
  }
  void roundFrame(int x, int y, int w, int h, int r, bool on = true) {
    fillRoundBox(x, y, w, h, r, on);
    fillRoundBox(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, !on);
  }

 protected:
  virtual void putPixel(int x, int y, bool on) = 0;
};
