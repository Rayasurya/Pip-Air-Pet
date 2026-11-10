# Pip's AI (Phase 2)

Everything here runs on the Mac. Until Pip has logged a real week, the data is **simulated** and labelled that way.

Run from this `ai` folder. `P` stands for `../tools/.venv/bin/python`.

| Step | Command | What you get |
|------|---------|--------------|
| 1. Simulate a room | `P sim/simulate.py --out data/sim_room.csv` | 92 days of minute-by-minute simulated air (inputs in `sim/SOURCES.md`) |
| Check it looks real | `P sim/plot_days.py data/sim_room.csv 2026-10-12 3 data/sim_3days.png` | A 3-day chart |
| 2. Train the forecast | `cd forecast && P train.py` | `forecast/REPORT.md` (trend vs linear vs small network) and `firmware/lib/pet/forecast_model.h` |
| 3. Diary | `P diary/diary.py data/sim_room.csv` | This week in plain English |
| Ask Pip | `P diary/diary.py data/sim_room.csv --ask "why were you sad on Tuesday?"` | An answer from the numbers |
| Diary page | `P diary/build_page.py data/sim_room.csv` | `diary/out/air-diary.html` (opens offline) |
| Claude diary (optional) | `P diary/diary.py data/sim_room.csv --claude` | Needs `pip install anthropic` and a key. Falls back to the template if any number doesn't check out. |
| Tests | `P diary/test_diary.py` and, in `../firmware`, `pio test -e native` | |

**With real data:** pull the log off Pip with `P ../tools/pull_log.py`, then use that CSV instead of `data/sim_room.csv` in steps 2 and 3. Retrain step 2 on about a week of real minutes before trusting the forecast numbers.

## Presence and the room diary

Can the air alone tell that someone is in the room? These run from the `air-pet` folder. `P` stands for `tools/.venv/bin/python`.

| What | Command | What you get |
|------|---------|--------------|
| Room diary, demo | `P ai/presence/room_diary.py --demo` | The simulated room as a page: every day as a strip, then one day with its CO2 curve, each block's evidence and how sure Pip is. Opens at `http://127.0.0.1:8765`, on your computer only. |
| Room diary, one file | `P ai/presence/room_diary.py --demo --export ai/presence/out/room-diary-demo.html` | The same page as one file that opens offline. A copy is in `presence/out/`. |
| Room diary, your room | `P ai/presence/room_diary.py --log your_log.csv` | Your own log. "Right" and "Wrong?" are saved to a CSV on your computer, which becomes the labels the rule is scored against. |
| Score the rule | `P ai/presence/score_level.py` | The level rule against the simulated room (true labels). With a real log it also shows how the limit was picked and tested. |
| Tests | `P ai/presence/test_level_rule.py`, `test_events.py`, `test_room_diary.py` | |

What to know before you trust it:

- `level_rule.py` is the tested rule: "someone here" when CO2 stays 80 ppm or more above the room's own empty level for 5 minutes.
- Known limit: in a closed room CO2 stays for hours after the person leaves, so the rule alone says "here" for too long. `test_level_rule.py` keeps that failure as a test.
- `events.py` adds "air clearing" and "likely asleep" for the diary. Their settings were tuned on the same days they are shown on. They are a reading aid, not a tested result.
- "How sure" is not a probability. It means any limit from 60 to 110 ppm gives the same answer.
- Time when Pip logged nothing is shown as "Pip was off". It is never filled in.

Log columns: `LOG_FORMAT.md`.
