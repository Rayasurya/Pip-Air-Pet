#include "radar.h"

static const uint8_t kHead[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t kFoot[4] = {0xF8, 0xF7, 0xF6, 0xF5};

bool RadarParser::decode() {
  // data_[0] type, [1] 0xAA, [2] state, [3..4] moving cm, [5] energy, [6..7] still cm, [8] energy,
  // [9..10] detection cm, ..., [len-2] 0x55, [len-1] 0x00
  if (len_ < 13) return false;
  if (data_[0] != 0x02 && data_[0] != 0x01) return false;
  if (data_[1] != 0xAA) return false;
  if (data_[len_ - 2] != 0x55 || data_[len_ - 1] != 0x00) return false;
  if (data_[2] > 3) return false;
  frame_.engineering = data_[0] == 0x01;
  frame_.state = data_[2];
  frame_.movingCm = (uint16_t)(data_[3] | (data_[4] << 8));
  frame_.movingEnergy = data_[5];
  frame_.stillCm = (uint16_t)(data_[6] | (data_[7] << 8));
  frame_.stillEnergy = data_[8];
  frame_.detectCm = (uint16_t)(data_[9] | (data_[10] << 8));
  return true;
}

bool RadarParser::feed(uint8_t b) {
  switch (stage_) {
    case Hunt:
      if (b == kHead[pos_]) {
        if (++pos_ == 4) {
          stage_ = Len;
          pos_ = 0;
          len_ = 0;
        }
      } else {
        pos_ = (b == kHead[0]) ? 1 : 0;  // a stray F4 may start the real header
      }
      return false;
    case Len:
      len_ |= (uint16_t)b << (8 * pos_);
      if (++pos_ == 2) {
        if (len_ < 13 || len_ > kMaxData) {  // not a report frame we understand: look for the next header
          bad_++;
          stage_ = Hunt;
          pos_ = 0;
        } else {
          stage_ = Data;
          pos_ = 0;
        }
      }
      return false;
    case Data:
      data_[pos_++] = b;
      if (pos_ == len_) {
        stage_ = Foot;
        pos_ = 0;
      }
      return false;
    case Foot:
      if (b != kFoot[pos_]) {
        bad_++;
        stage_ = Hunt;
        pos_ = (b == kHead[0]) ? 1 : 0;
        return false;
      }
      if (++pos_ == 4) {
        stage_ = Hunt;
        pos_ = 0;
        if (decode()) {
          good_++;
          return true;
        }
        bad_++;
      }
      return false;
  }
  return false;
}
