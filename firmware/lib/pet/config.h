// Air Pet tuning values. Change these on Day 10-11 after living with the pet.
#pragma once
#include <stdint.h>

namespace cfg {

// --- Pins (ESP32 NodeMCU 30-pin) ---
constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr int PIN_LDR = 34;     // analog in, LDR module AO
constexpr int PIN_TOUCH = 27;   // TTP223 SIG (HIGH while touched)
constexpr int PIN_BUZZER = 25;  // passive buzzer I/O
// Speaker (MAX98357A amplifier + 8 ohm 0.5 W speaker) replaces the buzzer. Set true once it is wired:
// BCLK -> D26, LRC -> D25 (the buzzer's old pin), DIN -> D33, VIN -> 3V3 (not 5 V), GND -> GND.
constexpr bool AUDIO_SPEAKER = false;
constexpr int PIN_I2S_BCLK = 26;
constexpr int PIN_I2S_LRC = 25;
constexpr int PIN_I2S_DIN = 33;

// --- Optional radar (HLK-LD2410C, 24 GHz). OFF by default. Not tested on real hardware. ---
// Wiring: radar 5 V -> 5 V, GND -> GND, radar TX -> PIN_RADAR_RX, radar RX -> PIN_RADAR_TX (3.3 V logic).
// GPIO16/17 are the ESP32's second hardware UART and are free here (not on a board with PSRAM).
// Build with -DRADAR_ENABLED_DEFAULT=true, or edit the line below, to turn it on.
#ifndef RADAR_ENABLED_DEFAULT
#define RADAR_ENABLED_DEFAULT false
#endif
constexpr bool RADAR_ENABLED = RADAR_ENABLED_DEFAULT;
constexpr int PIN_RADAR_RX = 16;
constexpr int PIN_RADAR_TX = 17;
constexpr uint32_t RADAR_BAUD = 256000;
constexpr uint16_t RADAR_ROOM_MAX_CM = 600;  // Kin: a target farther than this does not count as presence

// --- Mood thresholds ---
constexpr uint16_t CO2_STUFFY = 1000;   // ppm
constexpr uint16_t CO2_GASPING = 1500;  // ppm
constexpr float TEMP_HOT = 31.0f;       // degrees C
constexpr float HUMID_DRY = 30.0f;      // % RH
constexpr float HUMID_MUGGY = 75.0f;    // % RH

// LDR: raw 12-bit ADC reading (0-4095). Measure your room on Day 7 and set this.
// Many LDR modules read HIGHER when it is darker. If yours reads lower, set false.
constexpr bool LDR_DARK_IS_HIGH = true;
constexpr uint16_t LDR_DARK_LEVEL = 3000;
constexpr uint16_t LDR_HYSTERESIS = 150;  // stops flicker at dusk

// --- Timing ---
constexpr uint32_t MOOD_HOLD_MS = 30000;     // a new mood must hold this long before it shows
constexpr uint32_t DARK_HOLD_MS = 60000;     // dark this long before sleeping
constexpr uint32_t RELIEF_WINDOW_MS = 120000;
constexpr uint16_t RELIEF_DROP_PPM = 200;    // CO2 drop inside the window = "you opened a window"
constexpr uint32_t EVENT_SHOW_MS = 3000;     // relieved / petted animation length
constexpr uint32_t EXPLAIN_SHOW_MS = 4000;

// --- CO2 sensor (SCD40) ---
// CO2 readings depend on air pressure. Bengaluru is about 920 m above sea level.
constexpr uint16_t ALTITUDE_M = 920;
// The SCD40 runs a little warm next to the board. On Day 7, compare with a room
// thermometer and put the difference here (e.g. -2.0 if Pip reads 2 C too high).
constexpr float TEMP_OFFSET_C = 0.0f;
// No new CO2 reading for this long = treat the sensor as missing.
constexpr uint32_t AIR_STALE_MS = 30000;
// Calibration. The SCD40's automatic self-calibration assumes it sees fresh air (~400 ppm) at
// least once a week, and corrects itself every 156 h. A lived-in room that never airs out
// fools it, and each correction is a sudden jump that Kin could read as someone breathing.
// So it stays off. Calibrate by hand instead: Pip in fresh air (outdoors in shade, or at a wide
// open window, nobody breathing nearby) for 3+ minutes, then type `calibrate`. Once a month.
constexpr bool SCD_AUTO_CALIBRATION = false;
constexpr uint16_t FRESH_AIR_PPM = 420;              // outdoor CO2 today (Bengaluru suburbs: ~420-460)
constexpr uint32_t CALIBRATE_SETTLE_MS = 3UL * 60 * 1000;  // datasheet: run 3 min in steady air first

// --- Modes: home (pet + Kin + diary), car (left-behind check), room (shared fresh air) ---
// Car mode: a pin that reads HIGH while the car's USB power is on (5 V through a 10k/20k divider
// to the pin), so Pip knows when the ignition goes off. -1 = no wire: use the `drive` / `park`
// commands instead (for testing).
constexpr int PIN_POWER_SENSE = -1;
constexpr uint32_t CAR_STEP_MS = 5000;        // the left-behind check runs on every 5 s reading
constexpr uint32_t ROOM_SCREEN_SWAP_MS = 10000;  // room mode: face and fresh-air screen take turns
constexpr float ROOM_DEFAULT_M3 = 180;        // a classroom; set with `room 60`

// --- Outdoor air pressure (body diary) ---
// Fetched hourly from Open-Meteo over Wi-Fi. Only a coarse location (about 10 km) is sent.
// Set HOME_LAT / HOME_LON in secrets.h for another city; these defaults are central Bengaluru.
constexpr float DEFAULT_LAT = 12.97f;
constexpr float DEFAULT_LON = 77.59f;
constexpr uint32_t PRESSURE_FETCH_MS = 3600000;     // once an hour
constexpr uint32_t PRESSURE_STALE_MS = 3 * 3600000;  // older than this = unknown in the log

// --- OLED care ---
// Nudges the whole picture by a pixel now and then, so no pixel is lit in the
// same place all day (less burn-in).
constexpr uint32_t PIXEL_SHIFT_MS = 180000;

// --- Night mode ---
// The screen's glow kept Raya awake, so Pip was switched off at night and lost its night data.
// The light sensor can't be trusted to see night, so the clock decides: between these times the
// screen is off and pet sounds are silent, while sensing and logging carry on. A tap shows the
// face, dim, for a moment. Safety alerts (Kin asking, a car alert) still show and sound.
constexpr int NIGHT_START_MIN = 21 * 60 + 30;   // 21:30
constexpr int NIGHT_END_MIN = 7 * 60 + 30;      // 07:30
constexpr uint32_t NIGHT_PEEK_MS = 15000;
// Quotes: a line from a book is typed onto the screen every half hour (never at night or over an alert).
constexpr uint32_t QUOTE_EVERY_MS = 30u * 60u * 1000u;
constexpr uint32_t QUOTE_SHOW_MS = 20000;   // then Pip's face comes back; a touch ends it sooner
constexpr uint32_t QUOTE_FIRST_MS = 60000;  // the first one, a minute after start
// True if minuteOfDay (0-1439) falls in [start, end), including windows that cross midnight.
inline bool inNight(int minuteOfDay, int start = NIGHT_START_MIN, int end = NIGHT_END_MIN) {
  return start <= end ? (minuteOfDay >= start && minuteOfDay < end)
                      : (minuteOfDay >= start || minuteOfDay < end);
}

// --- Touch gestures ---
constexpr uint32_t LONG_PRESS_MS = 1000;   // hold 1-3 s, then let go: explain
constexpr uint32_t MARK_HOLD_MS = 3000;    // keep holding 3 s: "I feel unwell now" (body diary)
constexpr uint32_t MARK_QUIET_MS = 3600000;  // after a mark: dim and silent for an hour
constexpr uint32_t DOUBLE_TAP_GAP_MS = 300;

}  // namespace cfg
