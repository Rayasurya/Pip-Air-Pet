// Drawing interface. The device implements it with U8g2, the Mac preview with a
// plain framebuffer, so the pet's look is written once and runs in both places.
#pragma once

class Gfx {
 public:
  virtual ~Gfx() {}
  virtual void clear() = 0;
  // `on` = lit pixel. Drawing with on=false cuts dark features into a lit body.
  virtual void pixel(int x, int y, bool on = true) = 0;
  virtual void line(int x0, int y0, int x1, int y1, bool on = true) = 0;
  virtual void box(int x, int y, int w, int h, bool on = true) = 0;
  virtual void frame(int x, int y, int w, int h, bool on = true) = 0;
  virtual void disc(int x, int y, int r, bool on = true) = 0;
  virtual void fillEllipse(int x, int y, int rx, int ry, bool on = true) = 0;
  // Text baseline is the bottom of the glyph. Returns nothing; use textWidth to center.
  virtual void text(int x, int y, const char* s) = 0;
  virtual int textWidth(const char* s) = 0;
  virtual int textHeight() = 0;
};
