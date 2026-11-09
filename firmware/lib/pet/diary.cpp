#include "diary.h"

#include <stdio.h>

#include "config.h"

const char* const kMarkHeader =
    "time,minutes,co2_mean,co2_max,hours_over_1400,co2_mean_last2h,temp_min,temp_max,temp_change_6h,"
    "hum_mean,dark_min,dark_min_last2h,press_change_24h";
const char* const kHourHeader = "hour,minutes,co2_mean,co2_max,temp,hum,dark_min,press,marks";

bool isDark(uint16_t ldr) { return cfg::LDR_DARK_IS_HIGH ? ldr >= cfg::LDR_DARK_LEVEL : ldr <= cfg::LDR_DARK_LEVEL; }

void AirHistory::add(const AirSample& s) {
  buf_[head_] = s;
  head_ = (head_ + 1) % kMinutes;
  if (n_ < kMinutes) n_++;
}

MarkSummary AirHistory::summarize() const {
  MarkSummary m;
  m.minutes = n_;
  if (n_ == 0) return m;
  float co2Sum = 0, humSum = 0, co2Last = 0;
  int over = 0, last2h = 0;
  int firstPress = -1, lastPress = -1;
  m.tempMin = m.tempMax = at(0).temp10 / 10.0f;
  for (int i = 0; i < n_; i++) {
    const AirSample& s = at(i);
    const float t = s.temp10 / 10.0f;
    co2Sum += s.co2;
    humSum += s.hum10 / 10.0f;
    if (s.co2 > m.co2Max) m.co2Max = s.co2;
    if (s.co2 > 1400) over++;
    if (t < m.tempMin) m.tempMin = t;
    if (t > m.tempMax) m.tempMax = t;
    const bool dark = isDark(s.ldr);
    if (dark) m.darkMinutes++;
    if (i >= n_ - 120) {
      co2Last += s.co2;
      last2h++;
      if (dark) m.darkMinutesLast2h++;
    }
    if (s.press10) {
      if (firstPress < 0) firstPress = i;
      lastPress = i;
    }
  }
  m.co2Mean = co2Sum / n_;
  m.humMean = humSum / n_;
  m.hoursOver1400 = over / 60.0f;
  m.co2MeanLast2h = co2Last / last2h;
  if (n_ > 360) m.tempChange6h = (at(n_ - 1).temp10 - at(n_ - 361).temp10) / 10.0f;
  if (firstPress >= 0 && lastPress > firstPress) {
    m.pressKnown = true;
    m.pressChange24h = (at(lastPress).press10 - at(firstPress).press10) / 10.0f;
  }
  return m;
}

int formatMarkRow(const char* stamp, const MarkSummary& m, char* out, size_t n) {
  char press[12] = "";
  if (m.pressKnown) snprintf(press, sizeof(press), "%.1f", m.pressChange24h);
  return snprintf(out, n, "%s,%d,%.0f,%.0f,%.1f,%.0f,%.1f,%.1f,%.1f,%.0f,%d,%d,%s", stamp, m.minutes, m.co2Mean,
                  m.co2Max, m.hoursOver1400, m.co2MeanLast2h, m.tempMin, m.tempMax, m.tempChange6h, m.humMean,
                  m.darkMinutes, m.darkMinutesLast2h, press);
}

void HourAccumulator::add(const AirSample& s, bool marked) {
  n_++;
  co2Sum_ += s.co2;
  if (s.co2 > co2Max_) co2Max_ = s.co2;
  tempSum_ += s.temp10 / 10.0f;
  humSum_ += s.hum10 / 10.0f;
  if (isDark(s.ldr)) dark_++;
  if (marked) marks_++;
  if (s.press10) lastPress10_ = s.press10;
}

int HourAccumulator::format(const char* hourStamp, char* out, size_t n) const {
  if (n_ == 0) return 0;
  char press[12] = "";
  if (lastPress10_) snprintf(press, sizeof(press), "%.1f", lastPress10_ / 10.0f);
  return snprintf(out, n, "%s,%d,%.0f,%.0f,%.1f,%.0f,%d,%s,%d", hourStamp, n_, co2Sum_ / n_, co2Max_, tempSum_ / n_,
                  humSum_ / n_, dark_, press, marks_);
}

void HourAccumulator::reset() { *this = HourAccumulator(); }
