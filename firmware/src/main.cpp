#include <new>
// Air Pet ("Pip"): an OLED creature whose mood follows the room's air.
// Hardware and wiring: ../README.md and ../../docs/wiring.svg. Type `help` in the serial monitor.
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SensirionI2CScd4x.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <driver/i2s.h>

#include "config.h"
#include "audio.h"
#include "comfort.h"
#include "console.h"
#include "diary.h"
#include "weather.h"
#include "forecast.h"
#include "kin.h"
#include "leftbehind.h"
#include "radar.h"
#include "roomair.h"
#include "gesture.h"
#include "mood.h"
#include "pet.h"
#include "quotes.h"
#include "raster_gfx.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifdef WIFI_SSID
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "dashboard.h"
#endif

#ifdef OLED_SSD1306
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#endif

// Draws through U8g2's buffer with the shared shape code. Can nudge the whole
// picture by a pixel (burn-in care).
class OledGfx : public RasterGfx {
 public:
  explicit OledGfx(U8G2& u) : u_(u) {}
  void setShift(int x, int y) { sx_ = x; sy_ = y; }
  void clear() override { u_.clearBuffer(); }
  void text(int x, int y, const char* s) override {
    u_.setDrawColor(1);
    u_.drawStr(x + sx_, y + sy_, s);
  }
  int textWidth(const char* s) override { return u_.getStrWidth(s); }
  int textHeight() override { return 7; }

 protected:
  void putPixel(int x, int y, bool on) override {
    x += sx_;
    y += sy_;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    u_.setDrawColor(on ? 1 : 0);
    u_.drawPixel(x, y);
  }

 private:
  U8G2& u_;
  int sx_ = 0, sy_ = 0;
};

OledGfx gfx(oled);
SensirionI2CScd4x scd4x;
MoodEngine engine;
PetRenderer pet;
GestureDetector gestures;
Readings readings;

bool scdFound = false;
uint32_t scdStartedAt = 0;  // when periodic measurement last (re)started; calibration needs 3 min of it
bool quiet = false;
uint32_t explainUntil = 0;
uint8_t lastContrast = 0;

// ---- Comfort learning (tap / hold on the explain screen) ----
ComfortLearner comfort;
Preferences prefs;
uint32_t confirmUntil = 0;
char confirm1[22], confirm2[22];
const char* pendingFeedback = "";  // written into the next log row
const char* kAnswerHint = "TAP=BAD  HOLD=FINE";

void saveLimits() {
  const Limits& l = comfort.limits();
  prefs.putFloat("co2", l.co2Stuffy);
  prefs.putFloat("hot", l.tempHot);
  prefs.putFloat("dry", l.humDry);
  prefs.putFloat("mug", l.humMuggy);
}

void loadLimits() {
  prefs.begin("pip", false);
  if (prefs.isKey("co2")) {
    comfort.set({prefs.getFloat("co2"), prefs.getFloat("hot"), prefs.getFloat("dry"), prefs.getFloat("mug")});
  }
}
uint32_t lastScdPoll = 0, lastScdData = 0;
uint32_t lastLdrRead = 0, lastLog = 0;
float ldrAvg = -1;
Mood lastShown = Mood::Count;

// Test overrides from typed commands.
struct Fakes {
  bool co2 = false, temp = false, hum = false, light = false;
  uint16_t co2v = 600;
  float tempv = 25, humv = 50;
  bool dark = false;
  bool forced = false;
  Mood forcedMood = Mood::Happy;
  bool demo = false;
  uint32_t demoStart = 0;
  bool any() const { return co2 || temp || hum || light || forced || demo; }
} fakes;

// ---- Forecast and minute log ----
Forecaster forecaster;
float co2Sum = 0, ldrSum = 0;
int co2N = 0, ldrN = 0;
uint32_t lastMinute = 0, minutesUp = 0;
bool prevAlert = false;
ForecastResult lastForecast;
bool logOk = false;
const char* kLog = "/log.csv";
const char* kLogOld = "/log_old.csv";
// The flash file system is 1.375 MB. Two log files of up to 450 KB (about 2 weeks of minutes in all),
// plus the hourly record and the marks, must fit together. (Was 1.2 MB per file: the second file
// ran out of space after ~3 weeks and logging stopped.)
const size_t kLogMaxBytes = 450000;
const char* kMarks = "/marks.csv";          // body diary: one row per "I feel unwell" mark, never rotated
const char* kHours = "/hours.csv";          // hourly averages: months of history
const char* kHoursOld = "/hours_old.csv";
const size_t kHoursMaxBytes = 120000;       // ~3 months per file

// ---- Body diary ----
#ifndef SHARE_MARKS_WITH_FAMILY
#define SHARE_MARKS_WITH_FAMILY 0           // the evening note mentions marks only if the person agreed
#endif
AirHistory airHistory;                      // the last 24 h, summarised into each mark
HourAccumulator hourAcc;
char hourStamp[16] = "";
bool markThisMinute = false;
uint32_t markQuietUntil = 0, notedUntil = 0;
char notedLine[22] = "NOTED";
int marksToday = 0;
char lastMarkAt[6] = "--:--";
float outdoorPressHpa = 0;                  // outdoor air pressure from Open-Meteo; 0 = unknown
uint32_t pressAt = 0, pressTriedAt = 0;
#ifndef HOME_LAT
#define HOME_LAT cfg::DEFAULT_LAT
#define HOME_LON cfg::DEFAULT_LON
#endif

// Once an hour (and soon after Wi-Fi first connects): outdoor pressure for the body diary.
// Public weather data, so the connection is not certificate-checked; the value is range-checked.
void fetchPressure(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (pressTriedAt != 0 && now - pressTriedAt < (pressAt ? cfg::PRESSURE_FETCH_MS : 300000u)) return;
  pressTriedAt = now;
  char url[160];
  pressureUrl(HOME_LAT, HOME_LON, url, sizeof(url));
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(client, url)) return;
  float hpa = 0;
  if (http.GET() == 200 && parseSurfacePressure(http.getString().c_str(), hpa)) {
    outdoorPressHpa = hpa;
    pressAt = millis();
  }
  http.end();
}

bool clockValid() { return time(nullptr) > 1700000000; }  // set once NTP has answered

void say(Clip c, bool force = false);  // spoken words (speaker), defined with the sound code

// ---- Modes: what Pip is watching ----
// home: you (moods, Kin, body diary). car: a parked car (someone left inside?). room: a shared
// room's air (fresh air per person, shared-air level, a headcount range; never anyone's identity).
enum class PipMode : uint8_t { Home, Car, Room };
PipMode pipMode = PipMode::Home;
const char* modeName(PipMode m) { return m == PipMode::Car ? "car" : (m == PipMode::Room ? "room" : "home"); }
LeftBehind leftBehind;
float cabinM3 = 3.0f;              // vehicle car (3 m3) or van (8 m3); the detector scales its thresholds
LBResult carResult = {LBState::Driving, LBLevel::None, 0, false};
void setMode(PipMode m);  // defined next to the car check
bool carIgnition = true;          // from the power-sense pin, or the drive / park commands
uint32_t lastCarStep = 0;
RoomAir::Config roomCfg;
RoomAir roomAir;

// v5: what the detector learned about this vehicle (settled CO2 level, leak rates) lives in flash.
void saveCarLearned() {
  uint8_t b[kLbLearnedBytes];
  const size_t n = leftBehind.saveLearned(b, sizeof b);
  if (n) prefs.putBytes("car_learn", b, n);
}

void loadCarLearned() {
  uint8_t b[kLbLearnedBytes];
  if (prefs.getBytes("car_learn", b, sizeof b) == sizeof b) leftBehind.loadLearned(b, sizeof b);
}

// Ignition comes back on: if the parking that just ended looked empty, learn from it and save.
void carParkingEnded() {
  if (leftBehind.learnFromParking()) saveCarLearned();
}

// Optional radar (HLK-LD2410C on Serial2). Compiled in always, runs only if cfg::RADAR_ENABLED.
RadarParser radarParser;
RadarMotion radarMotion;
bool radarSeenThisMinute = false;  // sustained movement since Kin's last minute step

void radarPoll(uint32_t now) {
  if (!cfg::RADAR_ENABLED) return;
  int budget = 256;  // bytes per loop pass, so a noisy line cannot starve the rest
  while (budget-- > 0 && Serial2.available() > 0) {
    if (!radarParser.feed((uint8_t)Serial2.read())) continue;
    const RadarFrame& f = radarParser.frame();
    // Only sustained MOVEMENT counts as a sign of life; a still person (maybe collapsed) never does.
    if (f.detectCm <= cfg::RADAR_ROOM_MAX_CM) radarMotion.feed(now / 1000, f);
    if (radarMotion.takeSignOfLife()) radarSeenThisMinute = true;
    if (pipMode == PipMode::Car) leftBehind.setRadar(now / 1000, f.state, f.stillCm, f.stillEnergy);
  }
}

// ---- Kin: watching over someone who lives alone ----
#ifndef PARENT_NAME
#define PARENT_NAME "Mom"
#endif
kin::Engine kinEngine;
bool kinOn = false, travelMode = false;
bool touchedThisMinute = false;
bool kinAsking = false, kinLoud = false;
uint32_t lastChime = 0, sentUntil = 0;
char kinBubble[8] = "OK?";
char soonText[8] = "~?M";
int asksToday = 0, familyToday = 0;
int lastDigestDay = -1;
bool sensorProblemSent = false;
String pendingFamily;                 // one message waiting for Wi-Fi
uint32_t pendingSince = 0, lastSendTry = 0;

bool sendTelegram(const String& text) {
#if defined(TELEGRAM_BOT_TOKEN) && defined(TELEGRAM_CHAT_ID)
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
#ifdef TELEGRAM_ROOT_CA
  client.setCACert(TELEGRAM_ROOT_CA);
#else
  client.setInsecure();  // warned at boot: add TELEGRAM_ROOT_CA in secrets.h to verify the server
#endif
  HTTPClient http;
  if (!http.begin(client, "https://api.telegram.org/bot" TELEGRAM_BOT_TOKEN "/sendMessage")) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "chat_id=" TELEGRAM_CHAT_ID "&text=";
  for (size_t i = 0; i < text.length(); i++) {  // URL-encode
    const char ch = text[i];
    if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') body += ch;
    else { char h[4]; snprintf(h, sizeof(h), "%%%02X", (unsigned char)ch); body += h; }
  }
  const int code = http.POST(body);
  http.end();
  return code == 200;
#else
  (void)text;
  return false;
#endif
}

// Tell the family. If Wi-Fi is down, keep retrying for an hour.
void tellFamily(const String& text) {
  Serial.println("FAMILY: " + text);
  if (!sendTelegram(text)) {
    pendingFamily = text;
    pendingSince = lastSendTry = millis();
  }
}

void retryFamily(uint32_t now) {
  if (pendingFamily.length() == 0 || now - lastSendTry < 120000) return;
  lastSendTry = now;
  if (sendTelegram(pendingFamily) || now - pendingSince > 3600000) pendingFamily = "";
}

void saveKin() {
  kin::Engine::Learned l;
  kinEngine.exportLearned(l);
  prefs.putBytes("th_learn", &l, sizeof(l));
}

void loadKin() {
  kinOn = prefs.getBool("th_on", false);
  travelMode = prefs.getBool("travel", false);
  kin::Engine::Learned l;
  if (prefs.getBytes("th_learn", &l, sizeof(l)) == sizeof(l) && kinEngine.importLearned(l)) {
    Serial.printf("Kin: restored %d learned days.\n", kinEngine.daysSeen());
  }
}

void kinMinute(float co2, float ldr) {
  // Radar presence is one more sign of life, through the same input as a touch. Off = no change.
  const bool touched = touchedThisMinute || (cfg::RADAR_ENABLED && radarSeenThisMinute);
  touchedThisMinute = false;
  radarSeenThisMinute = false;
  if (!kinOn || !clockValid()) return;
  if (!readings.airValid) {
    // Silence is never safety: say so if the sensor has stopped.
    if (!sensorProblemSent && !travelMode && readings.airMissing) {
      tellFamily(String("Kin: the air sensor in ") + PARENT_NAME + "'s room stopped working, so I can't watch over "
                 "the day right now. Please check the device.");
      sensorProblemSent = true;
    }
    return;
  }
  sensorProblemSent = false;
  const long absMinute = (long)((time(nullptr) + 19800) / 60);  // IST: UTC + 5:30
  const int mod = (int)(absMinute % 1440);
  if (mod == 0) {
    asksToday = familyToday = 0;
    saveKin();  // nightly: learning survives power cuts
  }
  kin::Alert out[3];
  const int n = kinEngine.step(absMinute, co2, (int)ldr, touched, travelMode, out);
  for (int i = 0; i < n; i++) {
    const kin::Alert& a = out[i];
    Serial.printf("Kin: %s %s\n", kin::kindName(a.kind), kin::levelName(a.level));
    switch (a.level) {
      case kin::Level::Soft:
        kinAsking = true;
        kinLoud = false;
        asksToday++;
        snprintf(kinBubble, sizeof(kinBubble), "OK?");
        break;
      case kin::Level::Loud:
        kinAsking = kinLoud = true;
        if (a.kind != kin::Kind::Quiet && a.kind != kin::Kind::Night) asksToday++;
        snprintf(kinBubble, sizeof(kinBubble), a.kind == kin::Kind::Morning ? "HI!" : "OK?!");
        lastChime = 0;
        break;
      case kin::Level::Family: {
        char msg[400];
        kinEngine.familyText(a.kind, a.minute, msg, sizeof(msg), PARENT_NAME);
        tellFamily(String("Kin: ") + msg);
        familyToday++;
        kinAsking = kinLoud = false;
        snprintf(kinBubble, sizeof(kinBubble), "SENT");
        sentUntil = millis() + 600000;  // Pip shows the family was told, for 10 min
        say(Clip::ToldFamily, true);    // the person hears it too: nothing happens behind their back
        break;
      }
      case kin::Level::Resolved:
        kinAsking = kinLoud = false;
        break;
    }
  }
  // Daily digest at 22:30: the family hears "all normal" every day, so silence means "check the device".
  const int today = (int)(absMinute / 1440);
  if (mod == 22 * 60 + 30 && today != lastDigestDay && !travelMode) {
    lastDigestDay = today;
    if (kinEngine.learning()) {
      tellFamily(String("Kin: learning ") + PARENT_NAME + "'s routine, day " + kinEngine.daysSeen() + " of 14. "
                 "No alerts until learning finishes.");
    } else {
      char first[6] = "--:--", last[6] = "--:--";
      if (kinEngine.todayFirst() >= 0) snprintf(first, 6, "%02d:%02d", kinEngine.todayFirst() / 60, kinEngine.todayFirst() % 60);
      if (kinEngine.todayLast() >= 0) snprintf(last, 6, "%02d:%02d", kinEngine.todayLast() / 60, kinEngine.todayLast() % 60);
      String m = String("Kin daily: ") + PARENT_NAME + "'s day " + (familyToday ? "had an alert (see above)." : "looked normal.") +
                 " Up at " + first + ", last active " + last + ". Pip asked " + asksToday + (asksToday == 1 ? " time" : " times") +
                 ". Device online.";
      if (SHARE_MARKS_WITH_FAMILY && marksToday > 0)
        m += String(" Pressed \"not feeling well\" ") + marksToday + (marksToday == 1 ? " time" : " times") +
             ", last at " + lastMarkAt + ".";
      tellFamily(m);
    }
    marksToday = 0;
  }
}

// Frame timing, reported in the log.
uint32_t frames = 0, slowestFrameMs = 0;

// ---- Sound: buzzer tones, or the speaker (pet sounds + a few spoken lines) ----
AudioOut audio;
bool voiceOn = true;
int16_t audioBuf[256];
size_t audioLen = 0, audioSent = 0;
bool i2sIdle = true;
const Note* tune = nullptr;
uint8_t tuneLen = 0, tuneIdx = 0;
uint32_t noteEnds = 0;

const Note TUNE_PETTED[] = {{1568, 70}, {0, 30}, {2093, 90}};
const Note TUNE_RELIEVED[] = {{1047, 90}, {1319, 90}, {1568, 90}, {2093, 160}};
const Note TUNE_GASP[] = {{523, 120}, {0, 80}, {440, 160}};
const Note TUNE_CLICK[] = {{2637, 15}};
const Note TUNE_QUIET_OFF[] = {{1319, 60}, {1760, 60}};
const Note TUNE_HELLO[] = {{1319, 80}, {1568, 80}, {2093, 120}};
const Note TUNE_ASK_SOFT[] = {{1175, 120}, {0, 80}, {1568, 160}};
const Note TUNE_ASK_LOUD[] = {{1568, 200}, {0, 100}, {1568, 200}, {0, 100}, {2093, 300}};

bool nightNow = false;      // night mode: screen off, pet sounds silent (clock-based, see cfg::inNight)
uint32_t peekUntil = 0;     // a tap at night shows the face, dim, until then
bool screenOff = false;
bool hushed() { return quiet || nightNow || (int32_t)(markQuietUntil - millis()) > 0; }

// Quotes: a line from a book, typed onto the screen every half hour over Pip's ordinary face.
quotes::Deck quoteDeck;
bool quoteOn = false;
uint32_t quoteStart = 0, nextQuoteAt = cfg::QUOTE_FIRST_MS;
char quoteLines[quotes::MAX_LINES][quotes::COLS + 1];
const char* quotePtrs[quotes::MAX_LINES];
int quoteN = 0, quoteIdx = 0;

void startQuote(uint32_t now) {
  quoteN = 0;
  for (int tries = 0; tries < quotes::COUNT && quoteN <= 0; tries++) {
    quoteIdx = quoteDeck.next();
    quoteN = quotes::wrap(quotes::LIST[quoteIdx].text, quoteLines, quotes::MAX_LINES);
  }
  if (quoteN <= 0) return;
  for (int i = 0; i < quoteN; i++) quotePtrs[i] = quoteLines[i];
  quoteOn = true;
  quoteStart = now;
}

void updateNight(uint32_t now) {
  static uint32_t lastCheck = 0;
  if (lastCheck != 0 && now - lastCheck < 5000) return;
  lastCheck = now;
  struct tm tmv;
  // No clock yet (no Wi-Fi since boot): stay in day mode rather than guess.
  nightNow = clockValid() && getLocalTime(&tmv, 0) && cfg::inNight(tmv.tm_hour * 60 + tmv.tm_min);
}

void play(const Note* t, uint8_t n, bool force = false) {
  if (hushed() && !force) return;
  if (cfg::AUDIO_SPEAKER) {
    audio.playTune(t, n);
    return;
  }
  tune = t;
  tuneLen = n;
  tuneIdx = 0;
  noteEnds = 0;
}

// Spoken words: only where there is something to do or know. `force` = a safety question from Kin,
// which speaks even in quiet mode (like the chime). "voice off" turns all words off.
void say(Clip c, bool force) {
  if (!cfg::AUDIO_SPEAKER || c == Clip::None || !voiceOn) return;
  if (hushed() && !force) return;
  audio.playClip(c);
}

void startSpeaker() {
  i2s_config_t ic = {};
  ic.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  ic.sample_rate = kAudioRate;
  ic.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  ic.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  ic.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  ic.dma_buf_count = 8;   // 8 x 256 samples = 128 ms of sound queued: longer than one screen frame
  ic.dma_buf_len = 256;
  ic.tx_desc_auto_clear = true;  // silence, not a repeated buzz, if the loop is ever late
  i2s_pin_config_t pins = {};
  pins.bck_io_num = cfg::PIN_I2S_BCLK;
  pins.ws_io_num = cfg::PIN_I2S_LRC;
  pins.data_out_num = cfg::PIN_I2S_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_NUM_0, &ic, 0, nullptr) != ESP_OK || i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK)
    Serial.println("Speaker did not start.");
}

// Keeps the speaker's queue topped up without waiting (called every loop).
void pumpSpeaker() {
  for (int k = 0; k < 8; k++) {
    if (audioSent >= audioLen) {
      audioLen = audio.render(audioBuf, 256);
      audioSent = 0;
      if (audioLen == 0) {
        if (!i2sIdle) i2s_zero_dma_buffer(I2S_NUM_0);
        i2sIdle = true;
        return;
      }
      i2sIdle = false;
    }
    size_t wrote = 0;
    i2s_write(I2S_NUM_0, audioBuf + audioSent, (audioLen - audioSent) * sizeof(int16_t), &wrote, 0);
    audioSent += wrote / sizeof(int16_t);
    if (wrote == 0) return;  // queue full; more next loop
  }
}

void updateSound(uint32_t now) {
  if (cfg::AUDIO_SPEAKER) {
    pumpSpeaker();
    return;
  }
  if (!tune || now < noteEnds) return;
  if (tuneIdx >= tuneLen) {
    noTone(cfg::PIN_BUZZER);
    tune = nullptr;
    return;
  }
  const Note& n = tune[tuneIdx++];
  if (n.hz) tone(cfg::PIN_BUZZER, n.hz);
  else noTone(cfg::PIN_BUZZER);
  noteEnds = now + n.ms;
}

// ---- I2C check ----
const char* partName(uint8_t addr) {
  switch (addr) {
    case 0x3C: case 0x3D: return "OLED screen";
    case 0x62: return "SCD40 CO2 sensor";
    default: return "unknown part";
  }
}

int scanI2C(bool print) {
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      found++;
      if (print) Serial.printf("  found 0x%02X  %s\n", a, partName(a));
    }
  }
  if (print && !found) Serial.println("  nothing found. Check 3V3, GND, SDA (D21), SCL (D22).");
  return found;
}

bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// ---- Sensors ----
void readSensors(uint32_t now) {
  if (now - lastLdrRead >= 200) {
    lastLdrRead = now;
    const int raw = analogRead(cfg::PIN_LDR);
    ldrAvg = ldrAvg < 0 ? raw : ldrAvg * 0.8f + raw * 0.2f;
    ldrSum += raw;
    ldrN++;
    readings.ldrRaw = (uint16_t)ldrAvg;
    readings.lightValid = true;
  }
  if (scdFound && now - lastScdPoll >= 1000) {  // the SCD40 makes a new reading every 5 s
    lastScdPoll = now;
    bool ready = false;
    if (scd4x.getDataReadyFlag(ready) == 0 && ready) {
      uint16_t co2 = 0;
      float t = 0, h = 0;
      if (scd4x.readMeasurement(co2, t, h) == 0 && co2 != 0) {
        t += cfg::TEMP_OFFSET_C;
        co2Sum += co2;
        co2N++;
        // Light smoothing so one odd reading does not flip the mood.
        const float a = readings.airValid ? 0.4f : 1.0f;
        readings.co2 = (uint16_t)(readings.co2 * (1 - a) + co2 * a);
        readings.tempC = readings.tempC * (1 - a) + t * a;
        readings.humidity = readings.humidity * (1 - a) + h * a;
        readings.airValid = true;
        lastScdData = now;
      }
    }
  }
  // No data for a while: the sensor came loose or never started.
  if (readings.airValid && now - lastScdData > cfg::AIR_STALE_MS) readings.airValid = false;
  readings.airMissing = !scdFound || (lastScdData == 0 && now > cfg::AIR_STALE_MS) ||
                        (lastScdData != 0 && now - lastScdData > cfg::AIR_STALE_MS);
}

// Typed test values replace the real ones.
void applyFakes() {
  if (fakes.co2 || fakes.temp || fakes.hum) {
    if (!readings.airValid) {
      readings.co2 = 600;
      readings.tempC = 25;
      readings.humidity = 50;
    }
    if (fakes.co2) readings.co2 = fakes.co2v;
    if (fakes.temp) readings.tempC = fakes.tempv;
    if (fakes.hum) readings.humidity = fakes.humv;
    readings.airValid = true;
    readings.airMissing = false;
  }
  if (fakes.light) {
    const bool high = cfg::LDR_DARK_IS_HIGH == fakes.dark;
    readings.ldrRaw = high ? 4000 : 100;
    readings.lightValid = true;
  }
}

// ---- Minute tick: forecast + log ----
// Appends a CSV row, writing the header to a new file. Past `maxBytes` the file becomes `oldPath`
// (dropping the previous old file). No oldPath = never rotated.
void appendRow(const char* path, const char* oldPath, size_t maxBytes, const char* header, const char* row) {
  if (!logOk) return;
  if (oldPath && LittleFS.exists(path)) {
    File f = LittleFS.open(path, "r");
    const size_t size = f.size();
    f.close();
    if (size > maxBytes) {
      LittleFS.remove(oldPath);
      LittleFS.rename(path, oldPath);
    }
  }
  const bool fresh = !LittleFS.exists(path);
  File f = LittleFS.open(path, "a");
  if (!f) return;
  if (fresh) f.println(header);
  f.println(row);
  f.close();
}

void appendLog(const char* row) {
  appendRow(kLog, kLogOld, kLogMaxBytes, "time,co2,temp,hum,ldr,mood,pred,soon,fb,mark,press", row);
}

void timeStamp(char* out, size_t n, struct tm* tmv, bool haveClock) {
  if (haveClock) strftime(out, n, "%Y-%m-%d %H:%M", tmv);
  else snprintf(out, n, "+%lu", (unsigned long)minutesUp);
}

// "I feel unwell now": save what the air did in the 24 h before, then stay dim and silent for an hour.
void markUnwell(uint32_t now) {
  markThisMinute = true;
  touchedThisMinute = true;  // for Kin, a mark proves the person is up and able
  markQuietUntil = now + cfg::MARK_QUIET_MS;
  notedUntil = now + 5000;
  struct tm tmv;
  const bool haveClock = clockValid() && getLocalTime(&tmv, 0);
  char stamp[20], row[200];
  timeStamp(stamp, sizeof(stamp), &tmv, haveClock);
  formatMarkRow(stamp, airHistory.summarize(), row, sizeof(row));
  appendRow(kMarks, nullptr, 0, kMarkHeader, row);
  marksToday++;
  if (haveClock) {
    strftime(lastMarkAt, sizeof(lastMarkAt), "%H:%M", &tmv);
    snprintf(notedLine, sizeof(notedLine), "NOTED %s", lastMarkAt);
  } else {
    snprintf(notedLine, sizeof(notedLine), "NOTED");
  }
  Serial.printf("Marked. %s\n%s\n", kMarkHeader, row);
}

void minuteTick(uint32_t now, Mood shown) {
  if (now - lastMinute < 60000) return;
  lastMinute = now;
  minutesUp++;
  const float co2 = fakes.co2 ? fakes.co2v : (co2N ? co2Sum / co2N : readings.co2);
  const float ldr = ldrN ? ldrSum / ldrN : readings.ldrRaw;
  co2Sum = ldrSum = 0;
  co2N = ldrN = 0;
  if (!readings.airValid) {
    readings.soon = false;
    if (pipMode == PipMode::Home) kinMinute(co2, ldr);
    return;
  }
  forecaster.addMinute(co2, ldr, readings.tempC, readings.humidity);
  if (pipMode == PipMode::Room) roomAir.addMinute(co2);

  struct tm tmv;
  const bool haveClock = clockValid() && getLocalTime(&tmv, 0);
  const int minuteOfDay = haveClock ? tmv.tm_hour * 60 + tmv.tm_min : 0;
  const bool weekend = haveClock && (tmv.tm_wday == 0 || tmv.tm_wday == 6);
  lastForecast = forecaster.predict((uint16_t)comfort.limits().co2Stuffy, minuteOfDay, weekend, haveClock);
  // Two alerts in a row before Pip worries, so one noisy minute does not flip it.
  readings.soon = lastForecast.alert && prevAlert;
  prevAlert = lastForecast.alert;
  readings.soonEtaMin = lastForecast.etaMin;
  readings.risePerMin = lastForecast.risePerMin;
  snprintf(soonText, sizeof(soonText), "~%dM", lastForecast.etaMin);

  char stamp[20];
  timeStamp(stamp, sizeof(stamp), &tmv, haveClock);
  if (pressAt && now - pressAt > cfg::PRESSURE_STALE_MS) outdoorPressHpa = 0;  // too old to trust
  char press[10] = "";
  if (outdoorPressHpa > 0) snprintf(press, sizeof(press), "%.1f", outdoorPressHpa);
  char row[120];
  snprintf(row, sizeof(row), "%s,%.0f,%.1f,%.1f,%.0f,%s,%.0f,%d,%s,%s,%s", stamp, co2, readings.tempC, readings.humidity,
           ldr, moodName(shown), lastForecast.delta, readings.soon ? 1 : 0, pendingFeedback, markThisMinute ? "1" : "",
           press);
  pendingFeedback = "";
  appendLog(row);

  // Body diary: keep the last 24 h for marks, and an hourly record for the long run.
  AirSample a;
  a.co2 = (uint16_t)co2;
  a.temp10 = (int16_t)(readings.tempC * 10);
  a.hum10 = (uint16_t)(readings.humidity * 10);
  a.ldr = (uint16_t)ldr;
  a.press10 = (uint16_t)(outdoorPressHpa * 10);
  airHistory.add(a);
  if (haveClock) {
    char hour[16];
    strftime(hour, sizeof(hour), "%Y-%m-%d %H", &tmv);
    if (strcmp(hour, hourStamp) != 0) {
      char hrow[100];
      if (hourAcc.format(hourStamp, hrow, sizeof(hrow)) > 0) appendRow(kHours, kHoursOld, kHoursMaxBytes, kHourHeader, hrow);
      hourAcc.reset();
      strcpy(hourStamp, hour);
    }
    hourAcc.add(a, markThisMinute);
  }
  markThisMinute = false;
  if (pipMode == PipMode::Home) kinMinute(co2, ldr);
}

void dumpLog() {
  Serial.println("---LOG START---");
  bool header = false;
  for (const char* path : {kLogOld, kLog}) {
    if (!logOk || !LittleFS.exists(path)) continue;
    File f = LittleFS.open(path, "r");
    while (f.available()) {
      String line = f.readStringUntil('\n');
      if (line.startsWith("time,")) {
        if (header) continue;
        header = true;
      }
      Serial.println(line);
    }
    f.close();
  }
  Serial.println("---LOG END---");
  for (const char* part : {"MARKS", "HOURS"}) {
    Serial.printf("---%s START---\n", part);
    const bool marks = part[0] == 'M';
    bool header = false;
    for (const char* path : {marks ? kMarks : kHoursOld, marks ? kMarks : kHours}) {
      if (!logOk || !LittleFS.exists(path) || (marks && header)) continue;
      File f = LittleFS.open(path, "r");
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.startsWith("time,") || line.startsWith("hour,")) {
          if (header) continue;
          header = true;
        }
        Serial.println(line);
      }
      f.close();
    }
    Serial.printf("---%s END---\n", part);
  }
}

// The air mood the explain screen talks about: what is on screen in test modes,
// otherwise the real one behind any event (hearts, cheering).
Mood explainedMood() {
  Mood m = engine.base();
  if (fakes.forced) m = fakes.forcedMood;
  if (fakes.demo) m = demoMoodAt(millis() - fakes.demoStart);
  if (m == Mood::Petted || m == Mood::Relieved) m = Mood::Happy;
  return m;
}

void answer(Feedback f, uint32_t now) {
  const bool changed = comfort.feedback(f, explainedMood(), readings, confirm1, confirm2, sizeof(confirm1));
  if (changed) {
    engine.setLimits(comfort.limits());
    saveLimits();
  }
  pendingFeedback = f == Feedback::FeelsBad ? "bad" : "fine";
  explainUntil = 0;
  confirmUntil = now + 3000;
  play(TUNE_CLICK, 1);
  Serial.printf("Feedback: %s. %s %s\n", pendingFeedback, confirm1, confirm2);
}

void printLearned() {
  const Limits& l = comfort.limits();
  Serial.printf("Learned from %d answers: stuffy at %.0f ppm, hot above %.1f C, dry below %.0f%%, damp above %.0f%%"
                " (gasping stays fixed at %u ppm)\n",
                comfort.answers(), l.co2Stuffy, l.tempHot, l.humDry, l.humMuggy, cfg::CO2_GASPING);
}

void printStatus(Mood shown) {
  Serial.printf("co2=%u temp=%.1f hum=%.0f ldr=%u dark=%d mood=%s forecast=%s%+.0f soon=%d clock=%d%s%s fps=%lu slowest=%lums\n",
                readings.co2, readings.tempC, readings.humidity, readings.ldrRaw, engine.isDark(), moodName(shown),
                !lastForecast.ready ? "warming-up " : (lastForecast.learned ? "model " : "trend "), lastForecast.delta,
                readings.soon, clockValid(),
                readings.airMissing ? " [no CO2 sensor]" : "", fakes.any() ? " [test values]" : "",
                (unsigned long)(frames / 5), (unsigned long)slowestFrameMs);
  if (pipMode == PipMode::Car) {
    static const char* const kState[] = {"driving", "settling", "watching", "ALERT"};
    Serial.printf("mode=car ignition=%s state=%s production=%.1f ppm/min%s\n", carIgnition ? "on" : "off",
                  kState[(int)carResult.state], carResult.productionPpmMin,
                  carResult.level == LBLevel::Urgent ? " HOT" : "");
  } else if (pipMode == PipMode::Room) {
    const RoomAirReport r = roomAir.report();
    Serial.printf("mode=room volume=%.0fm3 valid=%d steady=%d outdoor=%.0f rebreathed=%.2f%% fresh_air=%.1f L/s/person "
                  "people=%.0f-%.0f risk=%u leak=%.2f/h\n", roomCfg.volumeM3, r.valid, r.steady, r.outdoorPpm,
                  r.rebreathedPct, r.freshAirLsPerPerson, r.peopleLow, r.peopleHigh, r.risk, r.leakPerHour);
  }
}

// ---- Serial commands ----
char lineBuf[48];
uint8_t lineLen = 0;

void handleCommand(const Command& c, uint32_t now) {
  switch (c.cmd) {
    case Cmd::None: return;
    case Cmd::Unknown: Serial.println("Not understood. Type help."); return;
    case Cmd::Help: Serial.print(kHelpText); return;
    case Cmd::Status: printStatus(lastShown); return;
    case Cmd::Scan: scanI2C(true); return;
    case Cmd::Co2: fakes.co2 = true; fakes.co2v = (uint16_t)c.value; break;
    case Cmd::Temp: fakes.temp = true; fakes.tempv = c.value; break;
    case Cmd::Hum: fakes.hum = true; fakes.humv = c.value; break;
    case Cmd::Dark: fakes.light = true; fakes.dark = true; break;
    case Cmd::Light: fakes.light = true; fakes.dark = false; break;
    case Cmd::Mood: fakes.forced = true; fakes.forcedMood = c.mood; fakes.demo = false; break;
    case Cmd::Demo: fakes.demo = true; fakes.demoStart = now; fakes.forced = false; break;
    case Cmd::Auto: fakes = Fakes(); break;
    case Cmd::Pet: engine.pet(now); play(TUNE_PETTED, 3); break;
    case Cmd::Explain: explainUntil = now + cfg::EXPLAIN_SHOW_MS; break;
    case Cmd::Quiet: quiet = !quiet; break;
    case Cmd::Dump: dumpLog(); return;
    case Cmd::FeelsBad: answer(Feedback::FeelsBad, now); return;
    case Cmd::FeelsFine: answer(Feedback::FeelsFine, now); return;
    case Cmd::Learned: printLearned(); return;
    case Cmd::Kin:
      if (!strcmp(c.arg, "on") || !strcmp(c.arg, "off")) {
        kinOn = !strcmp(c.arg, "on");
        prefs.putBool("th_on", kinOn);
        Serial.println(kinOn ? (clockValid() ? "Kin on. Learning the routine for 14 days first."
                                                : "Kin on, but waiting for the Wi-Fi clock.")
                                : "Kin off.");
      } else {
        Serial.printf("Kin %s%s. Learned days: %d/14. Present now: %s. Breathing estimate: %.1f ppm/min. "
                      "Room air change: %.2f per hour. Asking: %s.\n",
                      kinOn ? "on" : "off", travelMode ? " (travel mode)" : "", kinEngine.daysSeen(),
                      kinEngine.present() ? "yes" : "no", kinEngine.breathing(), kinEngine.leakPerHour(),
                      kinAsking ? "yes" : "no");
      }
      return;
    case Cmd::Travel:
      travelMode = !strcmp(c.arg, "on");
      prefs.putBool("travel", travelMode);
      if (travelMode) tellFamily(String("Kin: travel mode on. Not watching until it is turned off."));
      break;
    case Cmd::FamilyTest:
      Serial.println(sendTelegram(String("Kin test message from ") + PARENT_NAME + "'s Pip.") ? "Sent."
                     : "Not sent. Check Wi-Fi and TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID in secrets.h.");
      return;
    case Cmd::Mode:
      if (!strcmp(c.arg, "car")) setMode(PipMode::Car);
      else if (!strcmp(c.arg, "room")) setMode(PipMode::Room);
      else if (!strcmp(c.arg, "home")) setMode(PipMode::Home);
      else Serial.printf("Mode is %s. Use: mode home | car | room\n", modeName(pipMode));
      return;
    case Cmd::Vehicle:
      cabinM3 = !strcmp(c.arg, "van") ? 8.0f : 3.0f;
      prefs.putFloat("cabin", cabinM3);
      prefs.remove("car_learn");  // a different vehicle: what was learned no longer applies
      Serial.printf("Vehicle: %s (%.0f m3 cabin).\n", cabinM3 > 4 ? "van" : "car", cabinM3);
      if (pipMode == PipMode::Car) setMode(PipMode::Car);  // restart the check with the new size
      return;
    case Cmd::Park:
    case Cmd::Drive:
      if (pipMode != PipMode::Car) {
        Serial.println("Only in car mode: type  mode car  first.");
        return;
      }
      carIgnition = c.cmd == Cmd::Drive;
      if (carIgnition) carParkingEnded();
      leftBehind.setIgnition(carIgnition, now / 1000);
      Serial.println(carIgnition ? "Ignition on: driving." : "Ignition off: watching the cabin.");
      return;
    case Cmd::RoomSize: {
      roomCfg.volumeM3 = c.value;
      prefs.putFloat("roomv", c.value);
      // Rebuilt in place: RoomAir is ~21 KB, more than the 8 KB loop stack.
      roomAir.~RoomAir();
      new (&roomAir) RoomAir(roomCfg);
      Serial.printf("Room volume %.0f m3. Fresh-air numbers restart.\n", c.value);
      return;
    }
    case Cmd::Volume:
      audio.setVolume((uint8_t)c.value);
      prefs.putUChar("vol", audio.volume());
      say(Clip::Fresh, true);  // a sample at the new loudness
      if (!cfg::AUDIO_SPEAKER) Serial.println("Saved. No speaker yet: set AUDIO_SPEAKER in config.h once it is wired.");
      break;
    case Cmd::Voice:
      voiceOn = strcmp(c.arg, "off") != 0;
      prefs.putBool("voice", voiceOn);
      Serial.println(voiceOn ? "Words on." : "Words off. Pet sounds and safety chimes stay.");
      return;
    case Cmd::Mark:
      markUnwell(now);
      return;
    case Cmd::Quote:
      startQuote(now);
      Serial.printf("Quote %d: %s (%s)\n", quoteIdx, quotes::LIST[quoteIdx].text, quotes::LIST[quoteIdx].who);
      return;
    case Cmd::Marks:
      if (logOk && LittleFS.exists(kMarks)) {
        File f = LittleFS.open(kMarks, "r");
        while (f.available()) Serial.println(f.readStringUntil('\n'));
        f.close();
      } else {
        Serial.println("No marks yet. Hold Pip's head for 3 seconds when you feel unwell.");
      }
      return;
    case Cmd::Calibrate: {
      if (!scdFound) {
        Serial.println("No CO2 sensor to calibrate.");
        return;
      }
      if (now - scdStartedAt < cfg::CALIBRATE_SETTLE_MS) {
        Serial.printf("Keep Pip in fresh air a little longer (%lu s), then try again.\n",
                      (unsigned long)((cfg::CALIBRATE_SETTLE_MS - (now - scdStartedAt)) / 1000));
        return;
      }
      const uint16_t target = c.value > 0 ? (uint16_t)c.value : cfg::FRESH_AIR_PPM;
      uint16_t correction = 0xFFFF;
      scd4x.stopPeriodicMeasurement();
      delay(500);  // datasheet: wait 500 ms after stopping before a forced recalibration
      const uint16_t err = scd4x.performForcedRecalibration(target, correction);
      scd4x.startPeriodicMeasurement();
      scdStartedAt = millis();
      if (err != 0 || correction == 0xFFFF) {
        Serial.println("Calibration failed. Was the sensor running for 3 minutes?");
      } else {
        Serial.printf("Calibrated to %u ppm (shifted by %d ppm). Kin re-learns the room's floor tonight.\n",
                      target, (int)correction - 0x8000);
      }
      return;
    }
    case Cmd::Forget:
      comfort.reset();
      engine.setLimits(comfort.limits());
      prefs.clear();
      break;
    case Cmd::ClearLog:
      if (logOk) {
        LittleFS.remove(kLog);
        LittleFS.remove(kLogOld);
      }
      break;
  }
  Serial.println("ok");
}

void readSerial(uint32_t now) {
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      lineBuf[lineLen] = '\0';
      if (lineLen) handleCommand(parseCommand(lineBuf), now);
      lineLen = 0;
    } else if (lineLen + 1 < sizeof(lineBuf)) {
      lineBuf[lineLen++] = ch;
    }
  }
}

// ---- Boot ----
void bootScreen(bool oledOk) {
  if (!oledOk) return;
  oled.clearBuffer();
  oled.drawStr(4, 12, "PIP  starting up");
  oled.drawStr(4, 30, "screen      OK");
  oled.drawStr(4, 42, scdFound ? "CO2 sensor  OK" : "CO2 sensor  MISSING");
  oled.drawStr(4, 58, "type help on USB");
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nPip (Air Pet) starting.");
  pinMode(cfg::PIN_TOUCH, INPUT);
  analogReadResolution(12);

  Wire.begin(cfg::PIN_SDA, cfg::PIN_SCL);
  Wire.setClock(400000);
  Serial.println("Parts on the I2C wires:");
  scanI2C(true);
  const bool oledOk = present(0x3C) || present(0x3D);
  scdFound = present(0x62);

  quoteDeck = quotes::Deck(esp_random());
  oled.begin();
  oled.setFont(u8g2_font_5x7_tr);

  if (scdFound) {
    scd4x.begin(Wire);
    scd4x.stopPeriodicMeasurement();  // in case it was left running
    delay(500);
    // Settings can only change while the sensor is idle.
    if (scd4x.setSensorAltitude(cfg::ALTITUDE_M) != 0) Serial.println("Could not set altitude.");
    // Not persisted to the sensor's EEPROM (limited writes): set on every boot instead.
    if (scd4x.setAutomaticSelfCalibration(cfg::SCD_AUTO_CALIBRATION ? 1 : 0) != 0)
      Serial.println("Could not set CO2 self-calibration.");
    scdStartedAt = millis();
    if (scd4x.startPeriodicMeasurement() != 0) {
      Serial.println("SCD40 did not start.");
      scdFound = false;
    }
  } else {
    Serial.println("No CO2 sensor. Pip still runs; try: co2 1400  or  demo");
  }
  if (cfg::AUDIO_SPEAKER) startSpeaker();  // before the hello tune
  bootScreen(oledOk);
  play(TUNE_HELLO, 3);
  const uint32_t until = millis() + 2500;
  while (millis() < until) updateSound(millis());

  loadLimits();
  loadKin();
  audio.setVolume(prefs.getUChar("vol", 2));
  voiceOn = prefs.getBool("voice", true);
  roomCfg.volumeM3 = prefs.getFloat("roomv", cfg::ROOM_DEFAULT_M3);
  cabinM3 = prefs.getFloat("cabin", 3.0f);
  if (cfg::PIN_POWER_SENSE >= 0) pinMode(cfg::PIN_POWER_SENSE, INPUT);
  if (cfg::RADAR_ENABLED) Serial2.begin(cfg::RADAR_BAUD, SERIAL_8N1, cfg::PIN_RADAR_RX, cfg::PIN_RADAR_TX);
  const uint8_t savedMode = prefs.getUChar("mode", 0);
  setMode(savedMode <= (uint8_t)PipMode::Room ? (PipMode)savedMode : PipMode::Home);
#if defined(TELEGRAM_BOT_TOKEN) && !defined(TELEGRAM_ROOT_CA)
  Serial.println("Warning: Telegram TLS certificate not pinned (add TELEGRAM_ROOT_CA to secrets.h).");
#endif
  engine.setLimits(comfort.limits());
  printLearned();
  logOk = LittleFS.begin(true);  // formats the storage the first time
  if (!logOk) Serial.println("Log storage failed to start. Pip runs without a log.");
#ifdef WIFI_SSID
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);  // connects in the background
  configTzTime("IST-5:30", "pool.ntp.org", "time.google.com");
  Serial.println("Wi-Fi: connecting for the clock time.");
#else
  Serial.println("No Wi-Fi set (src/secrets.h). Forecast uses the straight-line method.");
#endif

  Serial.println("Ready. Type help for test commands.");
}

// Car mode: every 5 s, check whether someone is still breathing in the parked car.
void carStep(uint32_t now) {
  if (pipMode != PipMode::Car) return;
  if (cfg::PIN_POWER_SENSE >= 0) {
    const bool on = digitalRead(cfg::PIN_POWER_SENSE) == HIGH;
    if (on != carIgnition) {
      carIgnition = on;
      if (on) carParkingEnded();
      leftBehind.setIgnition(on, now / 1000);
    }
  }
  if (now - lastCarStep < cfg::CAR_STEP_MS || !readings.airValid) return;
  lastCarStep = now;
  const LBResult r = leftBehind.update(now / 1000, readings.co2, readings.tempC);
  const bool urgentNow = r.level == LBLevel::Urgent && carResult.level != LBLevel::Urgent;
  const bool checkNow = r.state == LBState::Alert && carResult.state != LBState::Alert;
  carResult = r;
  if (checkNow || urgentNow) {
    play(TUNE_ASK_LOUD, 5, true);
    say(urgentNow ? Clip::CarHot : Clip::CarCheck, true);
    lastChime = now;
    tellFamily(String("Pip in the car: ") + (urgentNow ? "the parked car is getting hot and " : "") +
               (leftBehind.radarAlerted() ? "someone may still be inside (the radar sees a still person). Please check the seats."
                                          : "someone may still be inside (CO2 still rising after the car was parked). Please check the seats."));
  } else if (r.state == LBState::Alert && now - lastChime > 30000) {
    play(TUNE_ASK_LOUD, 5, true);  // keep calling every 30 s until someone checks
    say(r.level == LBLevel::Urgent ? Clip::CarHot : Clip::CarCheck, true);
    lastChime = now;
  }
}

void setMode(PipMode m) {
  pipMode = m;
  prefs.putUChar("mode", (uint8_t)m);
  LeftBehind::Config lc;
  lc.cabinVolumeM3 = cabinM3;
  // Rebuilt in place: a temporary would sit on the 8 KB loop stack.
  leftBehind.~LeftBehind();
  new (&leftBehind) LeftBehind(lc);
  loadCarLearned();
  carIgnition = true;
  leftBehind.setIgnition(true, millis() / 1000);
  carResult = {LBState::Driving, LBLevel::None, 0, false};
  // Rebuilt in place: RoomAir is ~21 KB, more than the 8 KB loop stack.
  roomAir.~RoomAir();
  new (&roomAir) RoomAir(roomCfg);
  Serial.printf("Mode: %s.\n", modeName(m));
}

#ifdef WIFI_SSID
// Home network only: the log can be read at http://pip.local/ and new firmware arrives over
// Wi-Fi (pio run -e ota -t upload), so Pip never has to come off the wall.
WebServer web(80);
bool netStarted = false;

void sendFile(const char* path) {
  if (!logOk || !LittleFS.exists(path)) {
    web.send(404, "text/plain", "not found\n");
    return;
  }
  File f = LittleFS.open(path, "r");
  web.streamFile(f, "text/csv");
  f.close();
}

void startNet() {
  netStarted = true;
  MDNS.begin("pip");
  web.on("/", [] { web.send_P(200, "text/html", kDashboardHtml); });
  web.on("/files", [] {
    web.send(200, "text/plain",
             "Pip. Files: /log.csv /log_old.csv /hours.csv /hours_old.csv /marks.csv /now.json\n");
  });
  web.on("/now.json", [] {
    char j[220];
    snprintf(j, sizeof j, "{\"co2\":%u,\"temp\":%.2f,\"hum\":%.1f,\"ldr\":%u,\"dark\":%d,\"mood\":\"%s\",\"valid\":%d,\"clock\":%d,\"night\":%d,\"screen\":%d,\"quote\":%d}",
             readings.co2, readings.tempC, readings.humidity, readings.ldrRaw, engine.isDark() ? 1 : 0,
             lastShown == Mood::Count ? "starting" : moodName(lastShown), readings.airValid ? 1 : 0, clockValid() ? 1 : 0,
             nightNow ? 1 : 0, screenOff ? 0 : 1, quoteOn ? quoteIdx : -1);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", j);
  });
  web.on("/log.csv", [] { sendFile(kLog); });
  web.on("/log_old.csv", [] { sendFile(kLogOld); });
  web.on("/hours.csv", [] { sendFile(kHours); });
  web.on("/hours_old.csv", [] { sendFile(kHoursOld); });
  web.on("/marks.csv", [] { sendFile(kMarks); });
  web.begin();
  ArduinoOTA.setHostname("pip");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.begin();
  Serial.printf("Wi-Fi up: http://pip.local/  (%s)\n", WiFi.localIP().toString().c_str());
}

void serviceNet() {
  if (WiFi.status() != WL_CONNECTED) {
    // Once, 30 s after boot: say why Wi-Fi is not up, so setup problems are visible.
    static bool reported = false;
    if (!reported && millis() > 30000) {
      reported = true;
      Serial.printf("Wi-Fi not connected (status %d). Networks seen:\n", (int)WiFi.status());
      WiFi.disconnect();
      delay(100);
      const int n = WiFi.scanNetworks();
      Serial.printf("  (scan result %d)\n", n);
      for (int i = 0; i < n; i++)
        Serial.printf("  %s  ch %d  %d dBm  auth %d%s\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                      (int)WiFi.encryptionType(i), WiFi.SSID(i) == WIFI_SSID ? "  <- yours" : "");
      WiFi.scanDelete();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }
  if (!netStarted) startNet();
  web.handleClient();
  ArduinoOTA.handle();
}
#else
void serviceNet() {}
#endif

void loop() {
  const uint32_t now = millis();
  serviceNet();
  readSerial(now);
  readSensors(now);
  applyFakes();
  radarPoll(now);
  carStep(now);

  updateNight(now);
  const bool explaining = (int32_t)(explainUntil - now) > 0;
  const Gesture g = gestures.update(digitalRead(cfg::PIN_TOUCH) == HIGH, now);
  if (nightNow && g != Gesture::None) peekUntil = now + cfg::NIGHT_PEEK_MS;
  if (g != Gesture::None) quoteOn = false;  // a touch ends a quote
  switch (g) {
    case Gesture::Tap:
      touchedThisMinute = true;
      if (pipMode == PipMode::Car && carResult.state == LBState::Alert) {
        leftBehind.dismiss(now / 1000);  // "I checked, the car is empty"
        carResult.state = LBState::Watching;
        carResult.level = LBLevel::None;
        play(TUNE_PETTED, 3, true);
      } else if (kinAsking) {  // "I'm OK": answers Kin, and Pip says thank you
        kinAsking = kinLoud = false;
        engine.pet(now);
        play(TUNE_PETTED, 3, true);
        say(Clip::Thanks, true);
      } else if (explaining) {
        answer(Feedback::FeelsBad, now);
      } else {
        engine.pet(now);
        play(TUNE_PETTED, 3);
      }
      break;
    case Gesture::LongPress:
      touchedThisMinute = true;
      if (explaining) {
        answer(Feedback::FeelsFine, now);
      } else {
        explainUntil = now + cfg::EXPLAIN_SHOW_MS + 2000;  // a little extra time to answer
        play(TUNE_CLICK, 1);
        say(clipForMood(explainedMood()));
      }
      break;
    case Gesture::Mark:
      if (kinAsking) kinAsking = kinLoud = false;  // a mark also answers Kin: they're up
      markUnwell(now);
      break;
    case Gesture::DoubleTap:
      touchedThisMinute = true;
      quiet = !quiet;
      if (!quiet) play(TUNE_QUIET_OFF, 2, true);
      break;
    default:
      break;
  }

  Mood mood = engine.update(readings, now);
  minuteTick(now, mood);
  if (kinAsking || (int32_t)(sentUntil - now) > 0) mood = Mood::Asking;
  if (fakes.forced) mood = fakes.forcedMood;
  if (fakes.demo) mood = demoMoodAt(now - fakes.demoStart);
  if (mood != lastShown) {
    if (mood == Mood::Relieved) play(TUNE_RELIEVED, 4);
    if (mood == Mood::Gasping) play(TUNE_GASP, 3);
    lastShown = mood;
  }
  // Dim when asleep, at night, and for an hour after a mark (a migraine wants a dark screen).
  const uint8_t contrast = (mood == Mood::Asleep || nightNow || (int32_t)(markQuietUntil - now) > 0) ? 8 : 255;
  if (contrast != lastContrast) {
    oled.setContrast(contrast);
    lastContrast = contrast;
  }
  // Kin asks: a gentle chime once, a loud one every 30 s. Safety chimes ignore quiet mode.
  if (kinAsking && (lastChime == 0 || now - lastChime > (kinLoud ? 30000u : 600000u))) {
    if (kinLoud) play(TUNE_ASK_LOUD, 5, true);
    else play(TUNE_ASK_SOFT, 3, true);
    say(Clip::AskOk, true);
    lastChime = now;
  }
  if (!kinAsking) lastChime = 0;
  retryFamily(now);
  fetchPressure(now);
  pet.setSoonText(mood == Mood::Asking ? kinBubble : soonText);
  updateSound(now);

  // Quotes only cover Pip's ordinary face: never at night, over an alert, a question or a reason screen.
  const bool quoteBusy = nightNow || kinAsking || mood == Mood::Asking || mood == Mood::Gasping || fakes.demo ||
                         pipMode == PipMode::Room || (pipMode == PipMode::Car && carResult.state == LBState::Alert) ||
                         (int32_t)(explainUntil - now) > 0 || (int32_t)(notedUntil - now) > 0 ||
                         (int32_t)(confirmUntil - now) > 0;
  if (quoteOn && (quoteBusy || now - quoteStart > cfg::QUOTE_SHOW_MS)) quoteOn = false;
  if (!quoteOn && (int32_t)(now - nextQuoteAt) >= 0) {
    if (quoteBusy) {
      nextQuoteAt = now + 60000;  // try again in a minute
    } else {
      startQuote(now);
      nextQuoteAt = now + cfg::QUOTE_EVERY_MS;
    }
  }

  // Burn-in care: cycle through 4 one-pixel offsets.
  const uint32_t phase = (now / cfg::PIXEL_SHIFT_MS) % 4;
  gfx.setShift(phase == 1 ? 1 : 0, phase == 2 ? 1 : 0);

  if (pipMode == PipMode::Car && carResult.state == LBState::Alert) {
    PetRenderer::drawExplain(gfx, carResult.level == LBLevel::Urgent ? "CAR IS GETTING HOT" : "SOMEONE INSIDE?",
                             "CHECK THE SEATS", "TAP WHEN CHECKED");
  } else if (pipMode == PipMode::Room && (now / cfg::ROOM_SCREEN_SWAP_MS) % 2 == 1) {
    char l1[22], l2[22], l3[22];
    RoomAir::advice(roomAir.report(), l1, l2, l3, sizeof(l1));
    PetRenderer::drawExplain(gfx, l1, l2, l3);
  } else if ((int32_t)(notedUntil - now) > 0) {
    PetRenderer::drawExplain(gfx, notedLine, "I SAVED THE AIR", "QUIET FOR 1 HOUR");
  } else if ((int32_t)(confirmUntil - now) > 0) {
    PetRenderer::drawExplain(gfx, confirm1, confirm2, "");
  } else if ((int32_t)(explainUntil - now) > 0) {
    char l1[22], l2[22], l3[22];
    MoodEngine::explain(explainedMood(), readings, l1, l2, l3, sizeof(l1), comfort.limits());
    PetRenderer::drawExplain(gfx, l1, l2, l3, kAnswerHint);
  } else if (quoteOn) {
    PetRenderer::drawQuote(gfx, quotePtrs, quoteN, quotes::LIST[quoteIdx].who, quotes::lettersShown(now - quoteStart), now);
  } else {
    pet.draw(gfx, mood, now);
  }
  if (quiet && !quoteOn) PetRenderer::drawQuietIcon(gfx);
  PetRenderer::drawHoldBar(gfx, gestures.markProgress(now));
  // Night mode: screen off unless a tap asked for a peek, or a safety alert needs to be seen.
  const bool safety = kinAsking || (pipMode == PipMode::Car && carResult.state == LBState::Alert);
  const bool dark = nightNow && !safety && (int32_t)(peekUntil - now) <= 0 && gestures.markProgress(now) <= 0;
  if (dark != screenOff) {
    oled.setPowerSave(dark ? 1 : 0);
    screenOff = dark;
  }
  if (!dark) oled.sendBuffer();

  const uint32_t took = millis() - now;
  frames++;
  if (took > slowestFrameMs) slowestFrameMs = took;
  if (now - lastLog >= 5000) {
    lastLog = now;
    printStatus(mood);
    frames = 0;
    slowestFrameMs = 0;
  }
}
