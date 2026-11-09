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

Log columns: `LOG_FORMAT.md`.
