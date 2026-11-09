# Room mode: fresh air and shared-air risk

12 Oct 2026. One Pip (ESP32 + Sensirion SCD40) in a classroom, clinic waiting room or small office. It says things about the room. It never says anything about a person.

**Every number in the results below is SIMULATED.** No real room was measured.

**Update (v2):** the headcount and the fresh-air value while CO2 is moving now come from a particle filter, not the single-rate formula described in the sections below. Those sections and their results table describe v1 (`results_final.json`, kept as it was). The v2 results and the physical-limit check are in the last section.

## Problem

People in a shared room breathe each other's air. CO2 is a way to see how much. Rudnick & Milton (2003) showed that the share of inhaled air that someone already exhaled equals the extra CO2 divided by about 38,000 ppm, so a CO2 reading becomes a relative airborne-infection risk for the room (well-mixed air assumed). Other facts already in this project's notes (summaries, not read in full):

- US classrooms: absence 10-20% lower per 1,000 ppm less CO2 (Shendell, summary).
- Ventilation control saves 10-50% energy (summary).
- South African studies linked high clinic CO2 to TB exposure in health workers, and estimated high infection risk in a clinic waiting room (Spotlight, 2023, summary).

Room monitors are common. Our edge, from `USE_CASES.md` use case 2, is plain-language "fresh air per person", a risk level, a headcount range, and low cost.

## What Pip shows

Three lines, upper case, at most 21 characters, for the 128x64 OLED:

```
FRESH AIR 8.6 L/S/P      (or AIR SETTLING when CO2 is still moving)
TARGET 10
OPEN WINDOWS             (AIR IS GOOD / OPEN A WINDOW / OPEN WINDOWS / OPEN WINDOWS NOW)
```

The firmware also reports the rebreathed percent, a headcount range (low, mid, high), and a risk level 0 to 3. Nothing is stored or shown per person.

## Formulas

Symbols: C is room CO2 (ppm), Cout outdoor CO2, G CO2 made per person (L/s), V room volume (L), lambda air changes per hour.

| Output | Formula | Notes |
|---|---|---|
| Rebreathed fraction | (C - Cout) / 38,000 | Rudnick & Milton 2003 |
| Fresh air per person | q = G / (C - Cout), C as a fraction | Steady-state CO2 method (ASTM D6245). Valid only when CO2 is near steady, else "settling". |
| Headcount | n = V * (dC/dt + lambda * (C - Cout)) / G | Mass balance. Range from lambda x0.7 to x1.4 (wider when the learned value is old) plus slope and outdoor-CO2 noise. Never below 0. |
| Risk level | rebreathed % below 0.5 / 1 / 2 gives 0 / 1 / 2 / 3 | **Design choice, not a published limit.** |
| Target 10 L/s per person | shown on line 2 | **Design choice, not a cited standard.** |

Details (all constants are documented in `firmware/lib/pet/roomair.h`):

- **C and dC/dt** come from a straight-line fit over the last 10 minutes. The 60 s sensor lag is undone with C = measured + 1 min * slope.
- **Steady** means: the room is occupied (more than 80 ppm above outdoor) and CO2 moved less than max(5% of the excess, 20 ppm) across the last 20 minutes.
- **Outdoor CO2** is a rolling minimum of 5-minute means over up to 7 days, kept between 400 and 500 ppm. It is 420 for the first 2 hours.
- **Lambda** starts at a default (2 per hour). It is learned from a clean decay: after people leave, ln(excess) falls in a straight line with slope -lambda. The decay must start 100 ppm above outdoor, last 8+ minutes above 30 ppm, keep falling in the second half, and give a rate between 0.2 and 15 per hour. Window starts 4 minutes late to skip sensor lag.
- **G** is configured: 0.0050 L/s for seated adults, about 0.0035 for children.

## Files

| File | What |
|---|---|
| `firmware/lib/pet/roomair.h/.cpp` | The engine. Pure C++17, no Arduino headers, fixed memory. |
| `firmware/test/test_roomair/test_roomair.cpp` | 14 Unity tests: hand-computed formulas, steady vs settling, advice length, learned leak from a synthetic decay, never negative, empty room near 0. |
| `ai/room/room_sim.py` | SIMULATED classroom, clinic, meeting rooms, 7 days, 1 minute steps. |
| `ai/room/roomair_ref.py` | Python mirror of the C++ formulas. |
| `tools/roomair/eval.cpp`, `run.sh` | Compile the real C++ on the Mac and check it against the mirror. |
| `ai/room/evaluate.py` | Scores the C++ on simulated rooms. |

Commands:

```
cd firmware && pio test -e native -f test_roomair
tools/roomair/run.sh                           # compile + parity with the Python mirror
cd ai/room && ../../tools/.venv/bin/python evaluate.py --split dev     # seeds 1-60, for tuning
cd ai/room && ../../tools/.venv/bin/python evaluate.py --split final   # seeds 501-800, once
```

Parity: C++ and the Python mirror agree on every flag and to within 1e-6 relative on all values over 3 full simulated weeks.

## The simulation (SIMULATED)

True CO2 follows the well-mixed mass balance, one exact step per minute. The sensor adds a 60 s lag, 5-10 ppm noise, an offset of up to +-30 ppm, and rounding to whole ppm.

| Room | Volume | People | G per person (true) | Schedule |
|---|---|---|---|---|
| Classroom | 150-250 m3 | 25-45 children | 0.0030-0.0040 L/s | Mon-Fri lessons, two breaks (a quarter of breaks only half-empty), empty nights and weekend |
| Clinic waiting room | 60-120 m3 | 3-30 adults, drifting | 0.0045-0.0055 L/s | Mon-Fri 8-17, Sat 8-13 |
| Meeting room | 30-60 m3 | 2-12 adults | 0.0045-0.0055 L/s | 3-5 meetings a weekday |

The air-change rate changes every 30-180 minutes between closed (0.5-3 per hour), fan, and windows open (up to 8 per hour). Outdoor CO2 is 420-470. The configured volume is wrong by up to 10% and the configured G is a fixed typical value, so the device does not get the truth for free. Truth columns: people, lambda, true fresh air per person (lambda * V / n).

Scoring uses days 2 to 7. Seeds 1-60 were used for tuning (a few choices: the range tiers by age of the learned leak, the "still falling" check on decays). Seeds 501-800 (300 seeds, 900 rooms) were run once.

## Results on the final seeds (all SIMULATED)

| | Classroom | Clinic | Meeting room |
|---|---|---|---|
| **Fresh air per person**, median abs % error, when the device says steady | 7.8% | 9.7% | 7.1% |
| same, 90th percentile | 21.7% | 38.1% | 55.2% |
| same, in rooms with true lambda under 1.5 per hour | 12.6% | 12.6% | 15.8% |
| **Steady** flagged, % of occupied minutes | 25.5% | 23.0% | 11.1% |
| Flagged steady and truly near steady (within 10%) | 88% | 59% | 79% |
| **Headcount**, median abs error (people) | 18.3 | 10.0 | 1.5 |
| median true people | 33 | 17 | 7 |
| Truth inside [low, high] (+-0.5 for rounding), % of occupied minutes | 42.9% | 27.0% | 64.2% |
| Median range width (people) | 24.2 | 3.3 | 3.1 |
| **Risk level** matches truth-based level, exact | 96.2% | 97.8% | 95.4% |
| Risk within one level | 100% | 100% | 100% |
| Empty room (30+ min empty): device says "occupied" | 2.9% | 3.5% | 4.5% |

Headcount before any leak had been learned (day 1 included): median error 11.4 / 6.2 / 1.9 people, truth in range 44.7% / 45.7% / 53.0%.

## What is weak

- **Headcount is not good in big rooms or with changing windows.** The classroom median error is 18 people on a class of 33, and the clinic is off by 10 on 17. The truth is inside the range less than half the time in both. The cause is the air-change rate: it is learned only when the room empties, and then windows or fans change it for the rest of the day. The headcount depends on that rate directly, so a stale value gives a wrong count. Meeting rooms are the only case that is usable (about 1.5 people).
- **The range is honest, not tight.** Widening it with the age of the learned rate raised the share of truth inside the range but it still misses often, and for a classroom the range is about 24 people wide. Do not show the headcount in a big room without that range.
- **Open windows are the worst case.** With true lambda above 4 per hour the clinic headcount is inside the range only 15% of the time. In a small meeting room with 2-3 people and fast air change, the extra CO2 is 40 ppm or less, below the 80 ppm occupied threshold, so the device reports nothing.
- **Fresh air per person is good only when it says steady.** Then the median error is 7-10% (G and volume errors are included). But steady is flagged in only 11-26% of occupied minutes, because people keep arriving and leaving and the air-change rate keeps changing. In the clinic only 59% of "steady" minutes are truly near steady. At the 90th percentile the error is 22-55%.
- **Slow rooms (lambda under 1.5 per hour) are worse:** 13-16% error. They take hours to settle, so the steady-state assumption is the weakest there.
- **Risk level agrees 95-98%, but that is easy.** In the simulation 42-65% of occupied minutes are at level 3 and the level only depends on the sensor and outdoor CO2, not on lambda or G. It never missed by more than one level.
- **Empty rooms:** the device wrongly says "occupied" in 3-5% of truly empty minutes (mostly right after people leave and the CO2 is still high). When it does, the headcount mid value is close to 0.
- The simulation is mine. Real rooms will differ. Windows changing every 30-180 minutes is a hard setting.

## Limits

- Well-mixed air is assumed. Near a window, a door or a person, the room is not well mixed. The rebreathed fraction is a room average.
- One sensor reads one room. A large hall needs more than one.
- G changes with age and activity (a school child is about 12 L/h and a seated adult about 18.7 L/h, from `USE_CASES.md`). A wrong G scales fresh air per person and the headcount directly.
- The sensor repeats to about +-10 ppm with about 60 s lag. It can drift (see `research/AIR_SENSING.md`).
- It reports counts and ranges, not identities. It never shows or stores anything about an individual. Air data can still reveal when a room is used, so the owner of the room should know the device is there.
- Not a medical device. The risk level is a relative index with design-choice thresholds. It does not predict infection.
- CO2 does not show carbon monoxide or other pollutants.


## How close is this to the physical limit?

All numbers SIMULATED. Question: is the weak headcount in v1 caused by the physics (the air-change rate keeps changing) or by the method?

**Why physics matters here.** Once CO2 is steady, it fixes only people divided by air-change rate. Twenty people at 3 changes an hour look the same as ten people at 1.5. The two can be told apart only while CO2 is changing (people arriving or leaving).

**What was compared** (all on the same kind of simulated rooms, same sensor model):

- **v1:** one air-change rate learned from the last clean decay, mass-balance formula, range from how old that rate is.
- **v2:** a particle filter (400 particles, about 21 KB, about 30 microseconds per minute on a Mac; about 1 ms per minute is my estimate for an ESP32, not measured) over people, air-change rate and sensor offset. It knows the room volume, G, and that the air-change rate is somewhere between 0.3 and 12 per hour. Range is the 5% to 95% band.
- **Range oracle** (`ai/room/floor_analysis.py`): the same filter with 2,000 particles. It does not know the true air-change rate. It is the best I could build from what a device could know.
- **Exact oracle:** the same filter, told the true air-change rate every minute.

Seeds: v1 is the 501-800 final run, v2 is a single run on new seeds 1501-1800 (`results_final_v2.json`, tuned only on seeds 1-60). Oracles were run on seeds 901-1000, never used for tuning. On those 901-1000 rooms v1 and v2 give the same figures as their final runs within a few percent (headcount error 18.8 / 10.0 / 1.6 for v1 and 7.7 / 5.6 / 1.4 for v2).

| Median absolute headcount error (people) | Classroom | Clinic | Meeting |
|---|---|---|---|
| v1 | 18.3 | 10.0 | 1.5 |
| **v2** | **7.7** | **6.0** | **1.3** |
| Range oracle (air-change rate unknown) | 7.1 | 5.3 | 1.0 |
| Exact oracle (air-change rate known) | 2.2 | 1.8 | 0.5 |

| Truth inside the [low, high] range | Classroom | Clinic | Meeting |
|---|---|---|---|
| v1 | 42.9% | 27.0% | 64.2% |
| **v2** | **64.8%** | **44.6%** | **76.2%** |
| Range oracle | 75.1% | 55.3% | 84.7% |
| Exact oracle | 81.4% | 55.3% | 84.6% |

| Median range width (people) | Classroom | Clinic | Meeting |
|---|---|---|---|
| v1 / v2 | 24.2 / 18.1 | 3.3 / 8.5 | 3.1 / 4.1 |
| Range oracle / exact oracle | 20.9 / 7.6 | 10.5 / 2.8 | 4.9 / 2.1 |

| Median abs % error of fresh air per person | Classroom | Clinic | Meeting |
|---|---|---|---|
| Only when flagged steady: v1 / v2 | 7.8 / 7.6 | 9.7 / 9.7 | 7.1 / 6.7 |
| Every occupied minute (seeds 901-1000): v1 | 18.9 | 19.6 | 33.6 |
| Every occupied minute: **v2** | **10.0** | **15.9** | **12.3** |
| Every occupied minute: range oracle | 9.7 | 14.8 | 11.1 |
| Every occupied minute: exact oracle | 6.3 | 13.3 | 7.2 |

Other v2 results on the final seeds (1501-1800): risk level exact match 96.3% / 97.7% / 95.4%, same as v1; steady flagged 25% / 23% / 11%, same as v1.

**Reading the table.**

- v1 was not near the limit. Its headcount error was 1.9 to 2.6 times the range oracle in the classroom and clinic. That was the method, not the physics. v2 closes almost all of that gap: within 8% of the range oracle in the classroom, 13% in the clinic, and 30% in the meeting room (1.3 vs 1.0 people).
- v2 is now close to the best a device can do without knowing the air-change rate. Doubling the filter to 2,000 particles gains little.
- The gap between the range oracle and the exact oracle (7.1 vs 2.2 people in the classroom) is the physics: not knowing the air-change rate. No method on this sensor closes it, because steady CO2 fixes only people divided by air-change rate. Only a second signal would close it (a door or window sensor, or a measured airflow).
- Fresh air per person improves most in the minutes v1 skipped. v1 gave a value only from the steady-state formula, which is wrong while CO2 moves (19 to 34% error). When the flag says "settling", v2 now gives a filter-based value (10 to 16% error). The advice lines still say "AIR SETTLING" in that case. Fresh-air error when steady is unchanged, because that path did not change.
- Ranges are still not reliable. Truth is inside the range 45% to 76% of the time, against a nominal 90%. The filter is overconfident because steady stretches collapse the cloud of particles onto one line. I tried letting the cloud slide along that line. It raised coverage a little but made the median error clearly worse, so I did not keep it.

**What this does not show.** The simulation is mine, and its air-change rate changes every 30 to 180 minutes. The filter was tuned on a few settings (air-change rate jump chance, measurement noise) using dev rooms from the same simulator, so it is partly fitted to how this simulator behaves. Real rooms may be calmer, which would help, or may have behaviour the model lacks. The C++ filter and the Python mirror use different random numbers, so their parity check is statistical (they give the same kind of answer against the truth), while everything else in the engine still matches exactly.
