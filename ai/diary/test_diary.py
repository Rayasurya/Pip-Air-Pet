"""Run: ../tools/.venv/bin/python diary/test_diary.py   (from air-pet/ai)"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from diary import answer, template_diary, unknown_numbers  # noqa: E402
from stats import compute  # noqa: E402

s = compute(str(Path(__file__).parent.parent / "data" / "sim_room.csv"))
week = s["week"]

# The number check: real numbers pass, an invented one is caught.
ok_text = f"Stuffy for {week['stuffy_hours']} hours, worst on {week['worst_day']}."
assert unknown_numbers(ok_text, s) == [], unknown_numbers(ok_text, s)
assert 4321.0 in unknown_numbers(ok_text + " Peak was 4321 ppm.", s)

# The template only states numbers that are in the stats.
assert unknown_numbers(template_diary(s), s) == [], unknown_numbers(template_diary(s), s)

# Questions: a weekday from the last week, a date, and an unknown question.
last = s["days"][-1]
assert last["date"] in answer(s, f"why were you sad on {last['weekday']}?")
assert "try a weekday" in answer(s, "how are you?")
assert s["simulated"] is True
print("diary tests passed")
