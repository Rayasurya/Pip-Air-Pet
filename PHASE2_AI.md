# Pip Phase 2: adding AI

Phase 1 (done) is rule-based: thresholds turn readings into moods. Phase 2 adds three AI features. The story:

> Pip senses the air, predicts when it will get bad, learns what *you* find comfortable, and explains every decision. You can always overrule it.

**Data honesty:** until Pip has run in the room, every model here is trained and tested on **simulated** data. The simulator is built from published Bengaluru climate averages and standard indoor-air physics. All charts and numbers from it are labelled "simulated". The forecast model is retrained on real readings after about a week of logging. The case study must say this.

Legend: `[ ]` to do · `[x]` done · **NP** = no parts needed (can be done before delivery) · **HW** = needs the hardware.

---

## 0. Shared groundwork

- [x] **NP** Bengaluru climate inputs (Oct to Dec): temperature range, humidity, sunrise and sunset, outdoor CO2, CO2 per person, air change rates. Sourced, in `ai/sim/SOURCES.md`.
- [x] **NP** Room simulator (`ai/sim/`): minute-by-minute indoor CO2, temperature, humidity and light for 60+ days.
  - Physics: CO2 balance = people breathing in, minus air exchange with outside (slow with windows shut, fast with them open).
  - A daily routine with randomness: asleep, working at the desk, out, guests.
  - Behaviour: sometimes opens the window when it gets stuffy, sometimes forgets.
  - Weather: the Bengaluru daily temperature and humidity cycle, rainy days, the laptop warming the room.
  - Light: sunrise and sunset, lamp on in the evening, lights off at night.
- [x] **NP** Data log format (CSV, one row per minute), the same for simulated and real data, so every tool reads both.
- [~] **HW** Logging on the device (code done and compiling; needs the board): save one row per minute to the ESP32's flash (weeks of space). A `dump` command sends the log to the Mac over USB.
  - The code is written and compile-checked before delivery.
- [~] **HW** Real clock time (code done: Wi-Fi time sync via `src/secrets.h`; needs the board): the ESP32 gets the time from the internet over Wi-Fi.
  - Your Wi-Fi name and password go in a local file that is never committed.
  - Without Wi-Fi, rows still save with "minutes since power-on", and the Mac adds the clock time when downloading.

## 1. Forecast: "stuffy in ~20 minutes"

- [x] **NP** Define the prediction. Every minute, will CO2 cross the stuffy limit within the next 20 minutes, and roughly when?
- [x] **NP** Baselines to beat. (a) Assume CO2 stays where it is. (b) Draw a straight line through the last 10 minutes. If a model can't beat (b), we don't ship a model, and we say so.
- [x] **NP** Features: CO2 level and its rise over the last 5, 10 and 30 minutes, time of day, weekday or weekend, temperature trend, light level. No camera, no microphone, no personal data.
- [x] **NP** Train a tiny model on the Mac (a small neural network, a few hundred numbers in total). Test it on simulated days it has never seen.
  - Report: how often the warning was right (precision), how many stuffy spells it caught (recall), and how many minutes of warning it gave.
- [x] **NP** Export the model to C++ (plain arrays, no ML library on the device).
  - A parity test proves the C++ version gives the same answers as the Mac version.
- [x] **NP** Device side: keep the last 30 minutes of readings and run the model once a minute (under 1 ms).
- [x] **NP** A new Pip mood, **Worried**: Pip glances at the bubble, which shows `~20m`. The leaves start to sag early. Drawn and previewed on the Mac.
- [x] **NP** Explain screen: "CO2 RISING 25/MIN" · "STUFFY IN ~18 MIN" · "OPEN A WINDOW SOON?"
- [ ] **HW** Retrain on a week of real logs, then compare the simulated-only model against the real-data model.

## 2. Learns your comfort

- [x] **NP** Feedback gesture (decision needed, see below), e.g. from the explain screen: tap = "agree", hold = "I'm fine".
- [x] **NP** Learning rule: each feedback nudges *your* limits for CO2, heat and humidity, in small steps.
- [x] **NP** Guardrails:
  - Limits can only move inside a safe band. The "gasping" CO2 limit is fixed, so Pip can never learn to ignore truly bad air.
  - A `reset` command forgets everything learned.
- [x] **NP** Saved in flash, so learning survives a power cut.
- [x] **NP** Pip shows what it learned: "YOU FIND 900 PPM STUFFY · I NOW WARN AT 900".
- [x] **NP** Test with a simulated user whose true comfort limit is known. Check the learning settles near it within about 10 to 20 feedbacks and never leaves the safe band.
- [ ] **HW** Live with it, give real feedback, and check it feels right.

## 3. Air diary (companion on the Mac)

- [x] **NP** Stats in plain code first (no AI):
  - stuffy hours per day
  - worst time of day
  - window-open moments (detected from sudden CO2 drops)
  - how well the forecast did
  - comfort learned
- [x] **NP** Claude writes the diary from **those stats only**, not the raw data, so it cannot invent numbers. Every number it uses is checked against the stats before showing.
- [x] **NP** Ask Pip: questions like "why were you sad on Tuesday?", answered from the same stats.
- [x] **NP** A small local web page with charts plus the diary. The charts are labelled "simulated" until real data exists.
- [x] **NP** API key handling: the key stays on the Mac (environment variable), never on the ESP32, never committed.
- [ ] **HW** Run the diary on a real week.

## 4. Wrap-up

- [ ] **NP** Update the case-study notes: what is rules, what is AI, what is simulated, and what was tested.
- [ ] **HW** Demo video: a closed room, Pip forecasts, the window opens, Pip cheers, then the diary summarises the day.
- [ ] **HW** Quick readability test of the moods with 3 to 5 people.

---

## Decisions needed (from Raya)

1. **Feedback gesture** for comfort learning: (a) from the explain screen, tap = agree and hold = "I'm fine"; or (b) a second touch pad (5 are coming in Order 1).
2. **Claude API key** for the diary: is there one on the Mac? If not, the diary falls back to a template summary until there is.
3. **Neighbourhood**: Bengaluru city climate averages are used. A different area within the city changes little.

## Progress log

- 10 Oct 2026: plan written. Climate research started.
- 10 Oct 2026: simulator done (92 simulated days). Forecast trained and compared. The small neural network wins on simulated data (see `ai/forecast/REPORT.md`). Ported to C++, with parity tests matching Python. Worried mood, `~12M` bubble, explain text, per-minute log + `dump` + `tools/pull_log.py`, Wi-Fi clock with a straight-line fallback. 16 tests pass, ESP32 build compiles (31% flash). Waiting on decisions 1 and 2 for comfort learning and the diary.
- 10 Oct 2026: comfort learning done (`lib/pet/comfort.*`): learns from tap/hold on the explain screen, safe bands, fixed gasping limit, saved in flash (NVS), `bad`/`fine`/`learned`/`forget` commands. Simulated-user tests: sensitive (880 ppm) and relaxed (1,250 ppm) users learned within ±75 ppm. 23 device tests pass, ESP32 build 32% flash. Air diary done (`ai/diary/`): stats → template diary, "ask Pip" answers, optional Claude writer with number check, offline HTML page. Diary tests pass. Remaining: only HW items.
