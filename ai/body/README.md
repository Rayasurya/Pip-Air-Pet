# Body diary (pattern report)

Pip's "I feel unwell" press (hold its head 3 s) saves the moment and a summary of the 24 h of air before it. This folder turns the saved data into a short report of **patterns worth testing**. The plan is in `../../PIP_DIARY_VOICE.md`, and the results on simulated people are in `REPORT.md`.

| File | What it does |
|---|---|
| `patterns.py` | The report. Run on real data after `tools/pull_log.py`: `tools/.venv/bin/python ai/body/patterns.py --log "ai/data/real_log_*.csv" --marks "ai/data/real_marks_*.csv"` |
| `simulate_person.py` | SIMULATED people with hidden rules, for testing |
| `evaluate.py` | Runs the test (`--final` = fresh seeds, run once) |
| `test_patterns.py` | Quick checks: `tools/.venv/bin/python ai/body/test_patterns.py` |
| `out/sample_report_simulated.md` | What a report looks like (simulated person) |

Pull the log from Pip at least every two weeks: the board keeps about two weeks of minutes. Marks are never dropped.
