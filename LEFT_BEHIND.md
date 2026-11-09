# Left-behind detection (car mode)

Status: the detection engine (now v5, with learning) and a cabin simulator are built and tested. Every result below is **SIMULATED**. Nothing was measured in a real car. This is not a certified safety device.

## The problem

- About 37 children a year die in the US after being left in a hot car (NoHeatStroke.org).
- Euro NCAP rewards new cars that detect a child left behind. Carmakers mostly use 60 GHz radar for this. Vendors claim detection in about 7 s.
- The gap is the cars already on the road, and school vans.

Pip already has a CO2 and temperature sensor. A person left in a closed cabin keeps adding CO2. A closed cabin is small, so the rise is fast.

## How it works

Code: `firmware/lib/pet/leftbehind.h` and `.cpp`. Plain C++17, no heap, about 2 KB of state.

1. **Ignition on:** nothing to do. The engine resets.
2. **Ignition off (Settling):** the device sees its USB power from the car go away. Doors are probably open and the air is being flushed, so the reading falls. The engine waits at least 120 s, and then until the reading stops falling faster than 8 ppm/min (at most 10 min). While it waits it tracks the lowest 30 s average. That becomes the **outdoor estimate**, kept between 400 and 600 ppm.
3. **Watching:** every 30 s it fits a straight line to the last 4 min of CO2 and computes

   `production = slope + leak x (CO2 - outdoor)`

   The CO2 in that formula is projected forward by the 60 s sensor lag. The leak term (0.3 air changes per hour) stops a slowly decaying empty cabin from looking like a negative or zero source, and it stays on the safe side because it is the lowest leak in the simulated range.
4. **Alert (Check):** production above 3 ppm/min, and above zero by 3 standard errors of the slope, for 3 judgements in a row (90 s).
5. **Urgent:** after an alert, if the cabin is at or above 42 C, or at or above 32 C and still rising 2 C per 10 min, or rising 6 C per 10 min at any temperature.
6. **Dismiss:** a touch silences it until the next ignition cycle.
7. **Level rule (v2):** see "v2" below.

Every constant has a comment in the header saying why it has that value. They were tuned on dev seeds 1-200 only.

## Assumptions (all SIMULATED)

| Item | Value drawn per scenario | Source or reason |
|---|---|---|
| Cabin volume | car 2.5-3.5 m3, school van 6-10 m3 | Assumed |
| Air changes per hour, parked and closed | 0.3-1.5 | A measured study found under 1/h with windows closed |
| Air changes per hour, window cracked | 3-6 | Assumed |
| Air changes per hour, driving | 8-30 (recirculation 4-8) | Assumed |
| Doors open | 20-90 s (hard cases 10-25 s), 30-80 air changes per hour | Assumed |
| Outdoor CO2 | 420-480 ppm | Typical outdoor air |
| CO2 per adult | 15-19 L/h | Assumed range |
| CO2, infant / toddler / dog | 2.5-4.5 / 5-7 / 6-12 L/h | Assumed ranges |
| Adults on the trip | 1-4, leave 10-20 s after ignition off | Assumed |
| Sun | Sunny (50%): cabin heads to 55-70 C, time constant 10-20 min. Otherwise ambient (15-35 C) + 3-10 C | Assumed |
| Sensor | 60 s lag, noise 5-10 ppm, offset +-30 ppm | Sensirion SCD40 datasheet figures |
| Mix | 40% empty (half of those hard cases), 20% each infant, toddler, dog. 25% window cracked, 25% van | Chosen for balance |

Hard empty cases: 4 adults on recirculation after a 30-60 min drive (CO2 about 2,000-4,000 ppm at ignition off) and doors opened for only 10-25 s.

Not modelled: sensor failure above 60 C, sensor self-heating, wind, anyone returning to the car, engine heat.

## Results: v1 final run, SIMULATED

This is the record for v1 (the rise rule only). v2 results follow below.

Fresh seeds 1001-1600 (600 scenarios), run once after the code was frozen. Time is counted from the moment the doors close. Median and 95th percentile (p95) use only the cases that were detected.

| Case | n | Within 5 min | Within 10 min | Within 20 min | Median | p95 |
|---|---|---|---|---|---|---|
| Infant, closed car | 69 | 1% | **100%** | 100% | 6.1 min | 7.5 min |
| Infant, closed van | 21 | 0% | 76% | 76% | 6.9 min | 16.9 min |
| Toddler, closed car | 69 | 43% | 100% | 100% | 5.1 min | 6.6 min |
| Toddler, closed van | 21 | 5% | 100% | 100% | 6.0 min | 7.3 min |
| Dog, closed car | 69 | 59% | 100% | 100% | 4.9 min | 6.2 min |
| Dog, closed van | 21 | 24% | 100% | 100% | 5.3 min | 6.8 min |
| Infant, window cracked (car + van) | 30 | 0% | 13% | 13% | 7.4 min | 8.4 min |
| Toddler, window cracked (car + van) | 30 | 7% | 57% | 60% | 7.5 min | 67.6 min |
| Dog, window cracked (car + van) | 30 | 20% | 67% | 70% | 5.6 min | 56.3 min |

Summary of the whole set:

- **Empty-car false alarms: 0 of 240** (0 of 115 hard cases), any Check in the full 90 min run. Target was under 1%. Met. With 240 cases the true rate could still be up to about 1.2% (95% upper bound).
- **Closed car, infant, within 10 min: 69 of 69.** Target was 95%. Met. Counting closed vans too: 86 of 90 (94.4%).
- All occupied, window closed: 98% within 10 min. Window cracked: 46%.
- 3 of the 315 alerts came after the true cabin had already passed 60 C (sensor out of spec).

Raw numbers: `ai/vehicle/results_final.json` (dev run: `results_dev.json`).

## What failed or fell short (v1)

- **Window cracked.** Fresh air leaks out as fast as the child breathes it in, so CO2 climbs only a little and then flattens. A rise-rate test cannot see it. Infants with a cracked window were found 13% of the time. v2 adds a level-based rule for this (see below).
- **Infant in a closed van.** About 4 ppm/min is close to the threshold. 4 of 21 were missed.
- **Nothing is fast.** The best median is about 5 min. The sensor takes about 60 s to respond, and the engine has to wait for the flush to end. Radar is far faster.
- Lowering the wait and confirm time (dev seeds) saved about 30 s and did not change false alarms, so those were kept.

## v2: a second rule for the cracked window

**Why:** in v1 a cracked window hides the rise. Fresh air leaks out as fast as the child breathes it in, so the slope goes flat. But a person still holds the cabin about G / (leak x volume) above outdoor air (about 100 to 600 ppm for an infant at 3 to 6 air changes per hour in a car). An empty cabin falls toward outdoor air.

**What changed** (`leftbehind.cpp`, new `level*` fields in `LBConfig`; public API and the first four `LBResult` fields are the same):

- After settling, every 30 s it fits a line to the last 10 min of CO2.
- Alert (Check) when the excess over outdoor stays above 100 ppm and the CO2 is not falling the way an empty cabin would (the slope is above -0.8 x the decay that the lowest leak, 0.3 per hour, would cause), for 5 min in a row.
- The outdoor value used by this rule is capped at 500 ppm. In dev runs the old estimate was often stuck at 600 ppm, because the 60 s sensor lag stops the reading from reaching outdoor air during a short flush. That hid most infants.
- The rise rule from v1 is unchanged.

**Tuning:** dev seeds 1-200 only, with a 800-empty false alarm check on non-final seeds 5001-7000. Settings that gave false alarms on that check were rejected (for example a fixed slope limit of -0.7 ppm/min, and a 1.0 standard-error margin was too slow). The final run is on new seeds 2001-2600, run once. `results_final.json` (v1) was not touched.

### v2 results: seeds 2001-2600, SIMULATED

Different seeds from the v1 table, so compare the pattern, not the exact digits.

| Case | n | Within 5 min | Within 10 min | Within 20 min | Found at all (90 min) | Median | p95 |
|---|---|---|---|---|---|---|---|
| Infant, window cracked | 30 | 0% | 10% | 30% | 77% | 45.9 min | 66.7 min |
| Toddler, window cracked | 30 | 7% | 57% | 63% | 90% | 7.2 min | 44.0 min |
| Dog, window cracked | 30 | 13% | 63% | 67% | 93% | 7.2 min | 57.3 min |
| Infant, closed car | 66 | 0% | 93.9% | 98.5% | 100% | 6.4 min | 10.7 min |
| Infant, closed van | 24 | 0% | 83% | 96% | 100% | 7.0 min | 12.0 min |
| Toddler, closed car / van | 66 / 24 | | 100% / 96% | 100% / 100% | 100% | 5.3 / 6.0 min | 6.7 / 6.9 min |
| Dog, closed car / van | 66 / 24 | | 100% / 100% | 100% / 100% | 100% | 4.7 / 5.5 min | 5.7 / 6.8 min |

- **Empty-car false alarms: 0 of 240** (0 of 122 hard cases). Target met.
- **Closed car, infant, within 10 min: 62 of 66 (93.9%). Target of 95% missed.** v1 reached 100% on its seeds, so that was partly seed luck. The rise rule did not change in v2. The 4 late cases were all found, one of them after 20 min.
- Most of the cracked-window gain is late, not fast: an infant behind a cracked window is mostly found after 20 min or more, because the level rule needs a 10 min fit plus 5 min of persistence and a flat slope is noisy to judge. In v1 only 4 of 30 were found at all.
- Trade-off: a cracked-window infant is still found within 10 min only about 1 time in 10. 8 of 348 alerts came after the cabin passed 60 C, up from 3 of 315, because late finds land past the sensor's rating.
- Cracked-window vans are still weak (excess is only about 40 to 150 ppm in 6 to 10 m3).

Raw numbers: `ai/vehicle/results_final_v2.json`. Dev run: `results_dev.json` (v1 dev: `results_dev_v1.json`).

## v3: cabin volume

**Why:** what a person adds is G / V ppm per minute, so the same infant looks 2 to 3 times smaller in a school van. A fixed threshold in ppm/min is too high for a van (v2: closed-van infants found within 10 min only 83% of the time).

**What changed** (new `LBConfig` fields `cabinVolumeM3` (default 3, a car), `referenceVolumeM3` (3) and `levelExcessFloorPpm` (60); public API and the first four `LBResult` fields are the same):

- The production threshold (3 ppm/min for a 3 m3 car) is scaled by 3 / cabinVolumeM3. In a van set to 8 m3 it is about 1.1 ppm/min. This is the same as thresholding the production in litres of CO2 per hour.
- The level rule's excess threshold (100 ppm) is scaled the same way, but never below 60 ppm, so it stays above sensor offset (+-30 ppm).
- `zMargin` went from 3 to 2.5. With the scaled threshold, the noise margin was the limit for a van: 3 gave 88% closed-van infants within 10 min on dev and extra non-final seeds, 2.5 gave 94%. A longer fit window for vans was tried and was slower, so it was dropped.
- The evaluation passes the volume class a user would set (3 for a car, 8 for a van), not the true volume (6 to 10 m3 for vans, 2.5 to 3.5 for cars).

**Tuning:** dev seeds 1-200 plus non-final seeds 4001-4600. Final run once on new seeds 3001-3600. `results_final.json` (v1) and `results_final_v2.json` were not touched.

### v3 results: seeds 3001-3600, SIMULATED

Different seeds again, so compare the pattern, not the exact digits.

| Case | n | Within 5 min | Within 10 min | Within 20 min | Median | p95 |
|---|---|---|---|---|---|---|
| Infant, closed van | 21 | 0% | **100%** | 100% | 7.2 min | 8.8 min |
| Infant, closed car | 69 | 6% | **94.2%** | 98.6% | 6.2 min | 10.0 min |
| Toddler, closed car / van | 69 / 21 | 33% / 10% | 100% / 100% | 100% / 100% | 5.3 / 6.1 min | 7.0 / 7.1 min |
| Dog, closed car / van | 69 / 21 | 65% / 14% | 100% / 100% | 100% / 100% | 4.8 / 5.2 min | 5.6 / 6.8 min |
| Infant, window cracked | 30 | n/a | 20% | 40% | 34 min | 65 min |
| Toddler, window cracked | 30 | n/a | 30% | 47% | 21 min | 59 min |
| Dog, window cracked | 30 | n/a | 73% | 77% | 6.6 min | 41 min |

Found at all in the 90 min run: infant 27 of 30, toddler 29 of 30, dog 30 of 30.

- **Empty-car false alarms: 0 of 183. Empty-van false alarms: 0 of 57.** 0 of 129 hard cases. Target met.
- **Closed-van infant within 10 min: 21 of 21.** The small count (21) means the true rate could be well under 100%. On the larger tuning set (33 van infants) it was 94%.
- **Closed-car infant within 10 min: 94.2%. Target of 95% missed again** (v2: 93.9%). The car rule did not change in v3 (only zMargin).
- 7 of 356 alerts came after the cabin passed 60 C.
- On the non-final tuning seeds there was one empty-car and one empty-van alarm at about 72 min (past the 60 min window) at the final setting. Over long parked times, noise alone can trigger the rise rule.

Raw numbers: `ai/vehicle/results_final_v3.json`.

## v4: model comparison, close to the range oracle

**Why:** the range oracle above shows a cracked-window infant can be found at a median of about 11 min with zero false alarms, and v3 needs about four times longer. The oracle compares "pure decay to outdoor air" against "decay plus a constant source" on the whole sensor curve.

**What changed** (new `model*` fields in `LBConfig`; public API, `cabinVolumeM3` and the first four `LBResult` fields are the same; the older rules still run):

- From 2 min after ignition off, the engine keeps running sums for a fixed basis: a constant, the sensor's start state, and seven lagged decays for air-change rates of 0.3, 0.5, 0.8, 1.3, 2.2, 3.6 and 6 per hour. Memory is fixed (about 1 KB).
- Every 30 s, after 5 min of samples, it fits two models in closed form (small least squares, 4 parameters at a time). A decay is a non-negative mix of two neighbouring rates. H0: the asymptote is at most 510 ppm (480 outdoor + 30 offset). H1: the asymptote is free. Score = (RSS0 - RSS1) / sigma^2. Alert (Check) above 20.
- A first try with one rate per decay failed: an empty car that starts at 3000 ppm has a decay so large that a 10% rate error looked like a source, and empty cars scored in the thousands. Mixing two neighbouring rates fixed it (empty cars now score at most about 19 on non-final seeds).
- Threshold 20: 2,400 empty cars from non-final seeds 7001-13000 (half hard cases) gave no alarm from this rule at 20, 2 at 12 and 6 at 8 (per 800). Tuned on dev seeds 1-200 and non-final seeds 6001-6600 for detection.

**Seeds:** v3 tuning used 4001-4600 as extra seeds, so they are not new. The final v4 run uses untouched seeds 20001-20600, once. No earlier result file was changed.

### v4 results: seeds 20001-20600, SIMULATED, next to the oracle

| Detector | Case | n | Median | Within 10 min | Within 20 min | Within 30 min |
|---|---|---|---|---|---|---|
| v3 (seeds 3001-3600) | Infant, cracked | 30 | 34 min | 20% | 40% | not measured |
| **v4** | Infant, cracked | 30 | **17.8 min** | 17% | 60% | 87% |
| Range oracle (300 cars, other seeds) | Infant, cracked | 300 | 11.0 min | 47% | 78% | 88% |
| **v4** | Toddler, cracked | 30 | 10.9 min | 47% | 80% | 100% |
| **v4** | Dog, cracked | 30 | 6.1 min | 67% | 80% | 100% |
| v3 (seeds 3001-3600) | Infant, closed car | 69 | 6.2 min | 94.2% | 98.6% | |
| **v4** | Infant, closed car | 66 | 5.9 min | **95.5%** | 95.5% | |
| **v4** | Infant, closed van | 24 | 6.4 min | 95.8% | 95.8% | |
| **v4** | Toddler / dog, closed (car and van) | 90 / 90 | 4.6 to 6.0 min | 100% | 100% | |

- **Empty-car false alarms: 0 of 177. Empty-van false alarms: 0 of 63** (0 of 124 hard cases).
- Closed-car infant within 10 min: 63 of 66 (95.5%). Target of 95% met on these seeds. It was 93.9% and 94.2% before, so this is within seed noise.
- The 20 and 30 min cracked-window figures are close to the oracle (60% against 78% at 20 min, 87% against 88% at 30 min), but the 10 min figure and the median are not. Small sets (30 per row): treat them as roughly right.
- Cracked-window vans are still the weak case (infant 6 of 8 found at all).
- 1 of 358 alerts came after the cabin passed 60 C.

### Cost (the detector, one update)

- Mac (Apple silicon, -O2): about 0.5 us for an ordinary update and 6 to 8 us for the update that runs the fit (every 30 s).
- ESP32 estimate, **not measured on the device**: about 90 small solves of at most 4 x 4 per fit, roughly 9,000 double-precision operations in the worst case. The ESP32 has no double-precision hardware, so each operation costs about 0.4 us. That is about 4 ms for the fit, and about 0.2 ms for the sums at each 5 s reading. Well under 50 ms. The firmware builds for esp32dev.

## v5: learns its own vehicle

**Why:** the oracle table below shows what separates the real detector from the best CO2 can do for a cracked-window infant: it does not know this vehicle's own leak rate, its outdoor baseline or its sensor offset. A device that lives in one vehicle sees many parkings, and most of them are empty (a school van parks for hours every day). Kin already learns a room's leak rate from empty periods. v5 does the same for a vehicle.

**What it learns**, only from parkings that look empty:

- **Settled level:** the CO2 level an empty cabin settles at, as this sensor reads it (outdoor air plus its fixed offset).
- **Leak rate:** from the decay after the adults leave, when the starting excess is big enough to fit. Two lists: closed (under 2 air changes per hour) and window cracked (2 or more).

It keeps the last 10 of each and uses the **median**, so one parking learned by mistake cannot move the answer much.

**When a parking counts as empty** (all must hold; `learnFromParking()` in `leftbehind.cpp`):

- No alert and no dismiss during the parking.
- At least 40 min of data after the first 2 min (480 readings). This is what keeps most "child collected in time" parkings out.
- A fit of "decay to a level" leaves a residual under 14 ppm.
- The fitted level is between 390 and 510 ppm (outdoor 420-480 plus offset of up to 30).
- The last 10 min show no significant rise.
- The level is stored only if its fit error is under 12 ppm. The leak is stored only if the fit gets clearly worse when the rate is moved about 35% either way, so the leak is pinned to roughly +-30%.

**How the next parking uses it** (the model comparison of v4, same alert score of 20):

- The decay rates tried become a band from 0.55 to 1.8 times the learned median leak, kept inside the old range (0.3-1.5 closed, 3-6 cracked): three rates for closed, four for cracked. v4 tried seven rates from 0.3 to 6. The pair that bridges closed and cracked is dropped.
- The cap on the "pure decay" level becomes the median learned level plus 80 ppm. It is used only after 6 learned levels, and never above v4's 510 ppm.
- A window state with fewer than 2 learned leaks keeps v4's default rates for that state. With nothing learned the detector is exactly v4 (a unit test and the evaluation check this).

**API and storage:** `learnFromParking()` is called when the ignition comes back on, before `setIgnition(true)`. It returns true when it learned something. `saveLearned()` and `loadLearned()` move a fixed 126 bytes (version, counts, 3 x 10 values, checksum). `loadLearned()` refuses a wrong size, version, checksum, NaN or out-of-range value. `main.cpp` stores the bytes in Preferences key `car_learn` after each parking that ends empty, loads them when car mode starts, and clears them when the vehicle type changes. Cost: 1.7 KB more RAM in the object (4.3 KB total), about 9 KB more flash. Mac timing: 2 us per reading on average (v4: 0.5 us), the end-of-parking learning step about 4 us. ESP32 time is **not measured**; at roughly 28 small exponentials per reading in software double precision it should stay near 1 ms per reading.

### New assumptions (all SIMULATED, in `ai/vehicle/cabin_sim.py`, fleet mode)

- Each vehicle has a fixed volume, sensor offset, noise, closed-leak mean (0.3-1.5 per hour), cracked-leak mean (3-6) and window habit (10-40% of trips cracked).
- Each day the leak is the vehicle mean times a random 0.75 to 1.25 for the same window state, clipped to the old ranges (so v4's assumptions stay true).
- Outdoor CO2 is a new 420-480 each day, plus a straight-line drift of up to +-10 ppm over the run.
- History parkings are 60-90 min and mostly empty. 30% of empty ones are hard cases (v4's test set uses 50%). About 10% hold a dog, toddler or infant who is collected after 5-60 min, with no alarm needed.
- The test parking takes its occupant and window state from the same plan as before, so cells stay balanced.
- Not modelled: a new driver, seasons, a sensor swap, a fan left on, anything that changes the vehicle between days.

### What was tried and rejected (dev seeds 40001-60000 and 10,400 empty vehicles from seeds 200001-226000)

- **A tight cap on the level.** The learner's own level estimate has an error of about 20 ppm (sd) with a tail to 60 ppm, and the day's outdoor CO2 swings by 60 ppm. A cap of median + 45 or + 60 ppm after 3 learned levels gave false alarms (6 new alarms in 10,400 empty vehicles with 3 history parkings, 1 new with 10). The setting kept is + 80 ppm after 6 levels, which gave none. It rarely binds, so **almost all of the gain comes from the leak rate, not the baseline.**
- **Narrower leak bands** (0.65-1.5 and 0.6-1.6 times the median): more detections and new false alarms. The median of a few noisy estimates is itself uncertain.
- **A lower alert score** (16 and 17 instead of 20): new false alarms. Kept at 20.

### v5 results: fresh seeds 100001-102400, SIMULATED

2,400 vehicles, never used for tuning, run once. Each vehicle gets N earlier parkings of itself, then one test parking. "v4" sees only the test parking. v5 with 0 history is identical to v4 by construction (checked). v5 runs the real C++ detector (`tools/left_behind/fleet_eval.cpp`); the Python reference matches it exactly (`tools/left_behind/parity.sh`).

Infant cells (the hard ones). n is the number of test vehicles in the cell. "Found" is by 90 min.

| Case | n | Detector | Within 10 min | Within 20 min | Within 30 min | Median | Found |
|---|---|---|---|---|---|---|---|
| Infant, closed car | 270 | v4 | 98.9% | 99.3% | 100% | 6.0 min | 270 |
| | | v5, 3 history | 99.3% | 99.6% | 100% | 6.0 min | 270 |
| | | v5, 10 history | 99.6% | 100% | 100% | 6.0 min | 270 |
| Infant, closed van | 90 | v4 | 95.6% | 98.9% | 98.9% | 6.4 min | 90 |
| | | v5, 3 history | 97.8% | 100% | 100% | 6.4 min | 90 |
| | | v5, 10 history | 97.8% | 98.9% | 100% | 6.3 min | 90 |
| Infant, cracked car | 90 | v4 | 38.9% | 85.6% | 100% | 13.2 min | 90 |
| | | v5, 3 history | 43.3% | 88.9% | 100% | 12.0 min | 90 |
| | | v5, 10 history | 44.4% | 90.0% | 100% | 11.1 min | 90 |
| **Infant, cracked van** | **30** | v4 | 16.7% | 30.0% | 50.0% | 27.7 min | 25 |
| | | v5, 3 history | 16.7% | 30.0% | 50.0% | 27.7 min | 25 |
| | | v5, 10 history | 16.7% | **40.0%** | 53.3% | 24.2 min | 25 |

Toddler and dog cells (n = 270 closed car, 90 closed van, 90 cracked car, 30 cracked van each): v5 with 10 history is equal to or better than v4 in every cell. Examples: toddler cracked van within 20 min 80% to 90%, toddler cracked car 94.4% to 96.7%, dog cracked car 98.9% to 100%. All occupied cells together (1,440 vehicles): within 10 min 88.8% (v4), 89.6% (v5, 3 history), 89.9% (v5, 10 history); within 20 min 96.1%, 96.7%, 97.2%. Never found in 90 min: 5 for all three.

**False alarms, empty vehicles (n = 960: 720 cars, 240 vans, 482 hard cases):** within 60 min after the doors close, v4 had 1 and v5 had **0** (3 and 10 history). Any alarm in the full 90 min run: v4 2, v5 1. The one v5 alarm is on the same vehicle v4 also alarmed on. 611 of these 960 empty vehicles (64%) had at least one dog or child parking among their 10 earlier parkings (935 of 9,600 earlier parkings, 9.7%). A separate run on 10,400 empty vehicles (seeds 200001-226000, 5,203 hard) gave 2 alarms within 60 min for v4, 2 for v5 with 0 history, 2 for v5 with 3 history and 2 for v5 with 10 history (10 alarms in the full 90 min run for all four), so v5 adds none (`ai/vehicle/stress_v5.json`). Both v4 and v5 also raise an alarm on about 0.13% of the long (60 to 90 min) empty history parkings (130 of about 104,000 in that run, the same for both). The earlier rise and level rules cause these; v5 did not change that.

**Poisoning check (dev seeds):** of 315 history parkings with a dog or child in 300 vehicles, 306 raised an alarm and 9 did not (collected before any alarm). The learner learned from none of the 315.

**Does v5 beat v4 on the cracked van?** Only slightly, and not with confidence. With 10 history parkings 12 of 30 infants were found within 20 min against 9 of 30. With 3 history it is identical to v4. With n = 30 that gap is inside the noise. On a larger set of 150 dev vans (tuning seeds) the same direction held: within 20 min 41% for v4 and 45% for v5, and found by 90 min 129 and 134 of 150. In cracked cars the gain is clearer (median 13.2 to 11.1 min, within 20 min 85.6% to 90.0%, n = 90) and closed infants gain a few points. It is no worse in any cell and adds no false alarms, so it was ported to the firmware.

### The new oracle row: "learned priors", SIMULATED

`ai/vehicle/floor_v5.py` and `floor_results_v5.json`. Same method as the floor analysis below, on fleet parkings (seeds 70001 and up), 300 infants per window state (200 cars, 100 vans), 600 empty vehicles per state for the threshold. Time is minutes after the doors close. "Learned priors" knows what a **perfect learner** could know from many parkings: the vehicle's mean leak for the window state (the day's value still varies +-25%) and its exact sensor offset. It does not know the day's outdoor CO2 or the day's leak.

| Infant, cracked window | Range oracle | **Learned priors oracle** | Exact oracle | v4 | v5 (10 history) |
|---|---|---|---|---|---|
| Car: median / within 20 min | 10.3 min / 86% | **6.8 min / 99%** | 6.0 min / 100% | 13.2 min / 85.6% | 11.1 min / 90.0% |
| Van: median / within 20 min | 24.3 min / 40% | **12.3 min / 77%** | 11.1 min / 90% | 27.7 min / 30% | 24.2 min / 40% |
| Closed car: median / within 20 min | 11.5 min / 93% | 5.8 min / 100% | 5.0 min / 100% | 6.0 min / 99.3% | 6.0 min / 100% |
| Closed van: median / within 20 min | 21.6 min / 40% | 9.8 min / 100% | 6.3 min / 100% | 6.4 min / 98.9% | 6.3 min / 98.9% |

The oracle columns come from different seeds and use only the model comparison, so compare patterns, not digits. The real detector also has the rise and level rules.

- **The new floor is much lower than the old one.** A perfect learner roughly doubles the cracked-van result (40% to 77% within 20 min) and brings cracked cars to 99%.
- **v5 gets part of it.** The learner's estimate of the leak is good (within about 10% of the day's true value in most parkings), but the day-to-day spread of +-25% forces a wide band, and the level estimate is too noisy to use as a tight cap. The oracle gets both from exact knowledge.

### What is still beyond the limit

- **Cracked window in a van.** Even the learned-priors oracle finds 77% within 20 min, and the exact oracle 90%. v5 finds 40%. The excess over outdoor air is only 40 to 150 ppm in 6 to 10 m3, against a daily outdoor swing of 60 ppm. A second signal is still needed.
- **The day's outdoor CO2.** The learner cannot know it. It limits how tight the level cap can be.
- **A changed vehicle.** A new window habit, a new seal or a swapped sensor makes old history wrong until 10 new parkings push it out. Nothing here detects that, apart from the medians.
- **Real cars.** All of this is simulated with assumed day-to-day spreads. Whether a real vehicle's leak varies by +-25% or by half or twice that is unknown.

Raw numbers: `ai/vehicle/results_final_v5.json`, `floor_results_v5.json`, `stress_v5.json`. Dev tuning: `ai/vehicle/evaluate_v5.py sweep`.

## v6 attempt: the cracked-window van (not ported, SIMULATED)

**Question:** v5 finds 40% of cracked-window vans within 20 min (n = 30). The learned-priors oracle finds 77%. What does the oracle have that v5 does not?

**Diagnosis** (`ai/vehicle/floor_v6_diag.py`; 100 infants in cracked vans, 300 empty cracked vehicles to set the threshold; within 20 min):

| Oracle knows | Within 20 min |
|---|---|
| Everything a perfect learner could: the window is cracked, the leak within +-25%, the sensor offset | 77% |
| Same, but not the offset (level cap 510 ppm) | 65% |
| Same, but only the wide leak band v5 can afford (0.55 to 1.8 times the mean) | 54% |
| No leak knowledge, only that the window is cracked (3 to 6 per hour) | 54% |
| Leak known, but **the window state is not known** (closed rates also allowed) | **22%** |

The window state is worth about 55 points, a tight leak band about 23, the sensor offset about 12. v5 gets none of the first: it cannot tell whether the window is cracked, because a plateau 100 ppm above outdoor air can also be a slowly decaying closed cabin. The oracle is simply told. The learner cannot be: the cracked share of trips varies by driver and day, and a habit gives only a prior.

**Ideas tried** (dev seeds 40001-60000 only; 150 dev cracked vans, 300 cracked cars, 150 closed infants, 6,000 empty vehicles; the real C++ engine in a scratch copy):

1. **Score both models with the same window state** (pick the state the source model prefers, then compare pure decay in that state). Big gain on paper (cracked van within 20 min 45% to 72%, cars 88% to 99%) and a disaster for false alarms: 2,088 of 6,000 empty vehicles alarmed. An empty cabin that looks like the wrong state is scored against the wrong decay. Raising the score to 80 still left 1,921. Using it only when the state looks clear: a margin of 10 gave 4 new false alarms in 6,000 and van 51%; margins of 20 and above changed nothing. There was no setting with a gain and no new false alarm.
2. **A lower alert score that must persist** (12 for 6 checks, 14 for 4): no real gain (van 45% to 46%) and 1 new false alarm.
3. **Temperature rise to bound the leak:** not tested, because in this simulator the cabin temperature does not depend on the leak rate, so it carries no information here by construction. A real cabin might (a cracked window vents heat), but that cannot be shown without real data. Per-location or per-time-of-day baselines were not tried either: the simulator has no time of day or location, and the level cap is already the weak part of v5.

**Result:** none of the three gives a gain with zero false alarms, so there was no fresh-seed run and nothing was ported. The firmware is unchanged from v5.

**What this means, in plain words:** the remaining gap on the cracked-window van is about the window state, which CO2 alone does not reveal. With a wide leak band the oracle itself falls to 54%. Getting to the oracle's 77% needs a second piece of information: the owner telling the device the window is cracked, a window sensor, or another signal such as motion or radar. We could not find a CO2-only way to get it without false alarms.

## Radar option (not tested)

A 24 GHz radar (HLK-LD2410C) can see a still, breathing person through a cracked window, which CO2 cannot do reliably in a van. The code path is built and **off by default**. **No detection numbers exist and none should be quoted until it runs on real hardware.** The parser was written from the public ESPHome ld2410 driver and the vendor frame layout, from memory and not re-checked against the datasheet; the unit tests use hand-built frames, not recordings.

- **Wiring:** radar 5 V to 5 V, GND to GND, radar TX to the ESP32 RX pin (GPIO16), radar RX to GPIO17. 256000 baud. Turn it on with `RADAR_ENABLED` in `config.h` or the build flag `-DRADAR_ENABLED_DEFAULT=true`.
- **Price and stock:** about Rs 495 at Probots; out of stock on 16 Oct 2026.
- **Driver:** `firmware/lib/pet/radar.h` and `.cpp`, fed one byte at a time. A report frame has no checksum, so a frame is accepted only if header, length, data type, head marker, state, tail marker and footer all match.
- **Car mode:** a still target (state 2 or 3) within 3 m and energy of at least 30, seen continuously for 30 s while the cabin is Watching, raises a Check alert on its own. It does not fire while Settling (the adults are still getting out) or after a dismiss. The CO2 rules are unchanged and keep running. The 3 m, energy 30 and 30 s values are guesses.
- **Kin:** only sustained MOVEMENT counts as a sign of life (moving target, moving energy of at least 30, held 10 s, within 6 m), through the existing touch input. A still target never counts, because a collapsed person is still but present. With the radar off nothing changes.
- **Still unknown:** real false-alarm rate (a pet, a bag swinging, a person walking past the car), range through a windscreen, behaviour in sun and heat, and power draw on a battery.

## Limits

- The sensor is rated only up to 60 C. A sunny car passes that. The design relies on finding someone early, before the cabin gets there. The result flags `outOfSpec`.
- It needs a battery for when the car power goes away, and a way to tell someone outside the car. A Bluetooth walk-away alert or phone notification is **not built**. Today the only alert is on the device itself.
- It cannot tell a child from a pet.
- Radar is faster and works with the windows cracked.
- All results are simulated with assumed numbers. Real cars, real leaks and real people will differ. A real-car test is needed before any claim.
- Not a certified safety device. Do not rely on it to protect a child.

## Run it

```
cd firmware && pio test -e native -f test_leftbehind
tools/.venv/bin/python ai/vehicle/evaluate.py dev      # tuning set, seeds 1-200
tools/.venv/bin/python ai/vehicle/cabin_sim.py --seeds 1-20 --out build/vehicle/try
tools/left_behind/run.sh build/vehicle/try.csv
```

`evaluate.py final` and `final_v2` refuse to overwrite their result files.

v5 (fleet mode, learning):

```
tools/.venv/bin/python ai/vehicle/evaluate_v5.py sweep '[{"learnModelThreshold":18}]'   # dev tuning, C++ engine, env overrides
tools/.venv/bin/python ai/vehicle/evaluate_v5.py final_cpp                               # once; refuses to overwrite results_final_v5.json
tools/.venv/bin/python ai/vehicle/floor_v5.py                                            # oracle floors incl. "learned priors"
bash tools/left_behind/parity.sh 60001-60060 10                                          # C++ against the Python reference, whole histories
```

## How close is this to the physical limit?

All numbers here are **SIMULATED**. Code: `ai/vehicle/floor_analysis.py`. Raw numbers: `ai/vehicle/floor_results.json`.

Question: with a window cracked, the detector finds an infant after a median of about 45 min. Is the algorithm weak, or is that close to what CO2 alone can do with this sensor (lag 60 s, noise 5 to 10 ppm, offset up to 30 ppm, a reading every 5 s)?

### The physics

- With a source of G litres of CO2 per hour in a cabin of volume V and an air-change rate of lambda per hour, the cabin settles at **G / (lambda x V)** ppm above outdoor air.
- It gets there with a time constant of **1 / lambda**. At 4.5 air changes per hour that is 13 min. Closed at 0.9 per hour it is 67 min.
- An infant (3.5 L/h) in a 3 m3 car with a cracked window (4.5/h) settles only about 260 ppm above outdoors. Closed, it would settle about 1300 ppm above and rise at the same 19 ppm/min at the start. The crack does not remove the signal. It caps it at a small number.
- Why an empty car can look occupied: after a drive with four adults the cabin starts well above outdoors, maybe 1500 ppm. It then falls toward outdoor air. For the first minutes a falling curve that is still high looks like "CO2 is present". The two curves only separate once the empty car's curve heads for outdoor level and the infant's curve flattens at outdoor plus about 260 ppm. A flat tail 260 ppm up is a small gap against a sensor offset of +-30 ppm, 5 to 10 ppm of noise, and an unknown outdoor level (420 to 480).

### The test

An oracle detector fits the sensor curve after the doors close with two models, both passed through the 60 s sensor lag: "pure decay to outdoor level" and "decay plus a constant source". It uses an exact likelihood ratio. The offset is unknown but bounded to +-30 ppm. The threshold is the highest score seen on empty cars, so **those cars give zero false alarms by construction**. A separate group of 150 empty cars per case, never used to set the threshold, also gave 0 false alarms.

- Cars: seeds 9001 and up (not the dev or final seeds). 450 empty cars set the threshold for each case (232 and 221 of them are the hard empty cases), 150 more check it. 300 infant cars per case are scored.
- **Exact oracle** (asked for): knows the true air-change rate and the true outdoor CO2. A real device knows neither.
- **Range oracle** (added, closer to a real device): knows only the assumed ranges (3 to 6 air changes per hour cracked, 0.3 to 1.5 closed, outdoor 420 to 480) and fits the air-change rate itself.
- Alarm time is minutes after the doors close. Both oracles wait at least 5 min before they may alarm.

### Result (SIMULATED, infant, 300 cars per row)

| Detector | Case | Median | 90th percentile | Within 10 min | Within 20 min | Within 30 min | Found by 90 min |
|---|---|---|---|---|---|---|---|
| Real detector (same seeds) | Cracked window | 44.3 min | not found | 20% | 35% | 36% | 83% |
| Exact oracle | Cracked window | 5.0 min | 5.0 min | 99% | 100% | 100% | 100% |
| Range oracle | Cracked window | 11.0 min | 30.8 min | 47% | 78% | 88% | 96% |
| Real detector (same seeds) | Closed | 6.6 min | 8.6 min | 93% | 98% | 98% | 100% |
| Exact oracle | Closed | 5.4 min | 9.2 min | 95% | 100% | 100% | 100% |
| Range oracle | Closed | 11.8 min | 22.3 min | 32% | 84% | 94% | 100% |

The real detector had 0 false alarms on the 600 empty cars of each case from these seeds. In small cars the range oracle finds a cracked-window infant at a median of 8.5 min. In vans it is slower, median 23.6 min, and finds 83% by 90 min.

### What this says

- **The 45 min median is not the physical floor.** Even an oracle that knows only the assumed ranges finds a cracked-window infant at a median of 11 min with no false alarms. The real detector is about four times slower. The extra time is mostly spent not knowing the air-change rate: it uses a straight-line slope and a fixed leak term, while the oracle uses the full shape of the curve.
- **Closed cars are already near the limit.** The real detector's 6.6 min is close to the exact oracle's 5.4 min and faster than the range oracle, because the closed-car rise is large and fast.
- **Exact knowledge is not available.** The exact oracle's 5 min needs the true air-change rate and outdoor level. A real car's leak changes with wind, window position and fan state. The range oracle is the fairer limit, and it still leaves about 22% of cracked-window infants waiting more than 20 min and 4% never found. Vans are worst.
- **The model here is ideal:** one well-mixed room, a constant leak, a sensor that behaves as assumed. Real cabins will be harder, so these oracle numbers are best cases for CO2 alone, not predictions.

### Conclusion

There is room to improve the CO2 algorithm in simulation. A detector that fits the decay shape, rather than a slope, could move the cracked-window median from about 45 min toward 11 min. That has **not been built or tested**. Even then, CO2 alone leaves a slow tail (90th percentile about 31 min, vans slower) because the signal is only about 100 to 350 ppm above outdoors in a cracked cabin. A second signal is needed for that case. Candidates, none of them tested here:

- **Humidity:** a person adds water vapour by the same physics (source, leak, time constant), so it is a second look at the same curve. Confounders: rain, wet clothes, wet seats, a cold or hot cabin that changes how much water the air can hold, and an air conditioner that dried the cabin on the drive.
- **Motion:** a small movement sensor or camera-free motion check. Confounders: a sleeping infant barely moves, and a car rocking in wind.
- **Radar (60 GHz):** detects breathing directly and works with windows cracked. Needs extra hardware.
- **Seat pressure:** works on seats with a sensor. Confounders: bags and car seats left in place.
- **Thermal:** a rise in cabin temperature or a warm body against the cabin. Confounders: sun heating is much larger than body heat.
