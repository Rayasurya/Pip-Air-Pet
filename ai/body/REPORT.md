# Body diary pattern report: does it work? (SIMULATED people)

All results here are from **simulated** people (see `simulate_person.py`). There is no real diary data yet. Results: `final_results.json` (fresh seeds 101-130, run once) and `dev_results.json` (seeds 1-10, used while building).

## What was tested

Six kinds of simulated person, 30 of each, with 6 attacks a month:

| Person | Hidden rule | How often the condition occurs |
|---|---|---|
| stale | 3x the risk after 15+ min of CO2 above 1,400 ppm in the 3-24 h before | 13% of waking hours |
| pressure | 3x the risk after air pressure fell 2+ hPa over the 3-24 h before | 23% |
| stale_rare | 3x the risk after 2+ hours of stale air | 2% |
| pressure_rare | 4x the risk after a 4+ hPa fall | 3% |
| none | no rule: attacks at random | n/a |
| early_sign | attacks at random, but the lights are dimmed 30-90 min before each press | n/a |

The method compares each mark with the same clock time on the other days of the same month (time-stratified case-crossover) and uses a permutation test. A "pattern" must survive a correction for checking 6 factors. A "hint" is any uncorrected chance below 5%.

## Final results (fresh seeds, 30 people per row)

| Person | 3 months (~15-21 marks) | 6 months (~30-41) | 12 months (~60-81) |
|---|---|---|---|
| stale: found as a pattern / as pattern or hint | 3 / 10 | 5 / 17 | **15 / 23** |
| pressure: found as a pattern / as pattern or hint | 0 / 4 | 3 / 9 | 6 / 16 |
| stale_rare: pattern or hint | 0 | 1 | 2 |
| pressure_rare: pattern or hint | 1 | 2 | 4 |
| none: any false "pattern" | 1 | 1 | 2 |
| early_sign: dimmed lights labelled an early sign | **30** | **30** | **30** |
| early_sign: dimmed lights wrongly called a cause | 0 | 0 | 0 |

## What it means

1. **It rarely invents patterns.** For a person with no hidden rule, 1 or 2 runs in 30 showed a false "pattern". Weak hints appear by chance more often (up to about a third of runs), and the report says so.
2. **Early warning signs are handled correctly every time.** Lights dimmed just before a press were always reported as "a possible early sign, not a cause".
3. **It is slow.** Even a common pattern that triples the risk needs about a year of attacks to be called a pattern half the time. Weak hints show up earlier.
4. **Rare patterns are practically invisible to one person within a year.** If a condition happens only 2-3% of the time, only a handful of attacks ever follow it. This matches the published view that one person can rarely prove their own trigger from a diary (Turner et al. 2013).
5. **Factors that move together can't be told apart.** In the simulated room, people make the air both stale and more humid, so humidity sometimes shows up next to stale air. The report presents patterns to *test* (change one thing for two weeks), not causes.

## Fixed along the way

- **Biased comparison days.**
  - Version 1 left out comparison days within 24 h of any mark.
  - With frequent marks, the remaining days were unusual. The no-rule person then got false patterns in up to 4 of 10 runs.
  - Now all other days of the month are used, which is the standard design. False patterns dropped to about 1 in 30.
- **Misleading chance level.** The report said "by chance: about half" for stale air, which is zero most of the time. It now compares the share of marks with the share of ordinary days, for example 30% vs 17%.
- **Too few attacks to measure.** The first simulated triggers (2+ h stale air, 4+ hPa drops) happen so rarely that 0-2 attacks per run followed them. I kept them as the "rare" rows instead of hiding them.

## Limits

- Simulated room (Oct-Dec Bengaluru climate only; longer use = joined Oct-Dec seasons) and invented pressure. Real headaches, rooms and weather will differ.
- Pip sees only the room's air and light plus outdoor pressure. Sleep, food, stress and hormones are invisible to it.
- Not a medical device. It suggests patterns worth testing.
