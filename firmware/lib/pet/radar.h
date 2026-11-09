// HLK-LD2410C 24 GHz radar: parser for the serial "report" frames. OPTIONAL and OFF by default
// (cfg::RADAR_ENABLED). Pure C++, no Arduino headers, no heap.
//
// Frame layout, as used by the public ESPHome ld2410 component (esphome/components/ld2410) and the
// vendor's serial protocol document. WRITTEN FROM MEMORY OF THOSE SOURCES, NOT RE-FETCHED, and not yet
// run against a real sensor. Check every byte against the vendor datasheet before trusting it.
//
//   F4 F3 F2 F1          header
//   LL LL                length of the data block, little-endian (13 in basic mode)
//   data block:
//     02 or 01           02 = basic report, 01 = engineering report (same first 13 bytes, longer)
//     AA                 head marker
//     SS                 target state: 0 none, 1 moving, 2 still, 3 moving and still
//     MM MM  EE          moving target distance in cm (little-endian), moving energy 0-100
//     TT TT  EE          still target distance in cm, still energy 0-100
//     DD DD              detection distance in cm
//     (engineering reports carry gate energies here)
//     55 00              tail marker and a fixed check byte
//   F8 F7 F6 F5          footer
//
// There is NO checksum in a report frame. A frame is accepted only if the header, the length range,
// the data type, the head marker, the state value, the tail marker and the footer all match.
// Command replies use a different header (FD FC FB FA) and are skipped.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct RadarFrame {
  uint8_t state;         // 0 none, 1 moving, 2 still, 3 both
  uint16_t movingCm;
  uint8_t movingEnergy;
  uint16_t stillCm;
  uint8_t stillEnergy;
  uint16_t detectCm;
  bool engineering;
};

class RadarParser {
 public:
  // Feed one byte. Returns true when a complete valid frame has just been read; get it with frame().
  bool feed(uint8_t b);
  const RadarFrame& frame() const { return frame_; }
  uint32_t goodFrames() const { return good_; }
  uint32_t badFrames() const { return bad_; }
  void reset() { pos_ = 0; }

 private:
  static constexpr size_t kMaxData = 48;  // engineering reports are 35 bytes; leave room
  enum Stage : uint8_t { Hunt, Len, Data, Foot };
  bool decode();

  Stage stage_ = Hunt;
  uint8_t pos_ = 0;       // bytes of the current stage already read
  uint16_t len_ = 0;
  uint8_t data_[kMaxData];
  RadarFrame frame_ = {};
  uint32_t good_ = 0, bad_ = 0;
};

// Kin sign of life from radar. A collapsed person is still but present, so STILL presence must never
// count. Only a MOVING target (state 1 or 3) with moving energy >= minEnergy, held continuously for
// holdSec, counts. Anything else resets the timer.
class RadarMotion {
 public:
  explicit RadarMotion(uint8_t minEnergy = 30, uint16_t holdSec = 10) : minEnergy_(minEnergy), hold_(holdSec) {}
  void feed(uint32_t tSec, const RadarFrame& f) {
    const bool moving = (f.state == 1 || f.state == 3) && f.movingEnergy >= minEnergy_;
    if (!moving) {
      active_ = false;
      return;
    }
    if (!active_) {
      active_ = true;
      since_ = tSec;
    }
    if (tSec - since_ >= hold_) signOfLife_ = true;
  }
  // True once movement has been sustained; stays true until takeSignOfLife() reads it.
  bool takeSignOfLife() {
    const bool v = signOfLife_;
    signOfLife_ = false;
    return v;
  }

 private:
  uint8_t minEnergy_;
  uint16_t hold_;
  bool active_ = false, signOfLife_ = false;
  uint32_t since_ = 0;
};
