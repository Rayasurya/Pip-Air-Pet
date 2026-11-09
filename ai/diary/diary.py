"""Air diary, step 2: turn the stats into plain English, and answer questions.

Two writers:
  template (default) - fixed sentences filled from the stats. No AI, no key, cannot invent.
  claude  (--claude)  - Claude writes a friendlier diary from the same stats JSON. Every number
                        in its text is checked against the stats; if any number is not found,
                        the Claude text is rejected and the template is used instead.

Usage (from air-pet/ai):
  ../tools/.venv/bin/python diary/diary.py data/sim_room.csv
  ../tools/.venv/bin/python diary/diary.py data/sim_room.csv --ask "why were you sad on Tuesday?"
  ../tools/.venv/bin/python diary/diary.py data/sim_room.csv --claude
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from stats import compute  # noqa: E402

WEEKDAYS = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"]


def template_diary(s):
    w = s["week"]
    lim = s["limits"]
    lines = []
    if s["simulated"]:
        lines.append("> Written from SIMULATED data, not real measurements.")
        lines.append("")
    lines.append(f"## Pip's air diary, {w['from']} to {w['to']}")
    lines.append("")
    lines.append(f"The air was stuffy (CO2 at or over {lim['co2']} ppm) for {w['stuffy_hours']} hours this week, "
                 f"about {w['stuffy_hours_per_day']} hours a day.")
    if w["worst_two_hours"]:
        lines.append(f"The stuffiest time of day was {w['worst_two_hours']}.")
    lines.append(f"The worst day was {w['worst_day']} ({w['worst_day_stuffy_hours']} hours). "
                 f"The best was {w['best_day']} ({w['best_day_stuffy_hours']} hours).")
    lines.append(f"A window was opened about {w['window_opens']} times (spotted from sudden CO2 drops).")
    extras = []
    if w["hot_hours"]:
        extras.append(f"hot for {w['hot_hours']} hours")
    if w["damp_hours"]:
        extras.append(f"damp for {w['damp_hours']} hours")
    if w["dry_hours"]:
        extras.append(f"dry for {w['dry_hours']} hours")
    if extras:
        lines.append("It was also " + ", ".join(extras) + ".")
    if w["worst_two_hours"]:
        start = w["worst_two_hours"].split("-")[0]
        lines.append("")
        lines.append(f"**Try this:** open the window a little before {start}, before the stuffy stretch starts.")
    lines.append("")
    lines.append("### Day by day")
    for d in s["days"][-7:]:
        part = f"- **{d['weekday'][:3]} {d['date'][5:]}**: stuffy {d['stuffy_h']} h, peak {d['co2_peak']} ppm at {d['co2_peak_time']}"
        if d["window_opens"]:
            part += f", window opened at {', '.join(d['window_opens'])}"
        lines.append(part + ".")
    return "\n".join(lines) + "\n"


def answer(s, question):
    """Answers 'why were you sad/stuffy/... on <weekday or date>?' from the stats."""
    q = question.lower()
    day = None
    for d in reversed(s["days"]):
        if d["date"] in q or d["weekday"].lower() in q:
            day = d
            break
    if day is None:
        if "today" in q or "yesterday" in q:
            day = s["days"][-1] if "today" in q else s["days"][-2]
        else:
            return "I can answer about a day: try a weekday name or a date like 2026-12-15."
    lim = s["limits"]
    parts = []
    if day["stuffy_min"]:
        when = f", mostly around {day['worst_hour']:02d}:00" if day["worst_hour"] is not None else ""
        parts.append(f"the air was stuffy for {day['stuffy_h']} hours{when} "
                     f"(over {lim['co2']} ppm, peak {day['co2_peak']} at {day['co2_peak_time']})")
    if day["hot_min"]:
        parts.append(f"it was hot for {day['hot_h']} hours (up to {day['temp_max']} C at {day['temp_max_time']})")
    if day["damp_min"]:
        parts.append(f"it was damp for {day['damp_h']} hours (humidity up to {day['hum_max']}%)")
    if day["dry_min"]:
        parts.append(f"it was dry for {day['dry_h']} hours (humidity down to {day['hum_min']}%)")
    head = f"On {day['weekday']} {day['date']}"
    if not parts:
        return f"{head} the air was fine all day, so I wasn't unhappy."
    tail = ""
    if day["window_opens"]:
        tail = f" A window was opened at {', '.join(day['window_opens'])}, which helped."
    return f"{head}, " + "; ".join(parts) + "." + tail


# ---- Optional: Claude writes the diary ----

def allowed_numbers(obj, out=None):
    out = set() if out is None else out
    if isinstance(obj, dict):
        for v in obj.values():
            allowed_numbers(v, out)
    elif isinstance(obj, list):
        for v in obj:
            allowed_numbers(v, out)
    elif isinstance(obj, (int, float)) and not isinstance(obj, bool):
        out.add(float(obj))
        out.add(float(round(obj)))
    elif isinstance(obj, str):
        for n in re.findall(r"\d+(?:\.\d+)?", obj):
            out.add(float(n))
    return out


def unknown_numbers(text, stats):
    ok = allowed_numbers(stats) | {float(i) for i in range(0, 13)}  # small counts and "2 hours" style words
    found = [float(n) for n in re.findall(r"(?<![\w.])\d+(?:\.\d+)?", text)]
    return sorted({n for n in found if n not in ok})


def claude_diary(s):
    try:
        import anthropic
    except ImportError:
        return None, "the anthropic package is not installed"
    client = anthropic.Anthropic()
    compact = {k: v for k, v in s.items() if k != "co2_by_hour"}
    compact["days"] = s["days"][-7:]
    prompt = (
        "You are Pip, a small sprout creature that lives on a desk and senses the room's air. "
        "Write this week's air diary for your owner from the JSON below: a short, warm paragraph, "
        "then one line per day, then one practical tip. Plain, simple English. "
        "Use ONLY numbers that appear in the JSON, copied exactly; do not compute new numbers. "
        "If 'simulated' is true, start with the line: (Written from simulated data.)\n\n"
        + json.dumps(compact)
    )
    try:
        resp = client.beta.messages.create(
            model="claude-opus-5-5",
            max_tokens=16000,
            betas=["server-side-fallback-2026-07-01"],
            fallbacks="default",
            output_config={"effort": "medium"},
            messages=[{"role": "user", "content": prompt}],
        )
    except anthropic.AuthenticationError:
        return None, "no Claude credentials found (set ANTHROPIC_API_KEY or run `ant auth login`)"
    except anthropic.APIConnectionError:
        return None, "could not reach the Claude API"
    except anthropic.APIStatusError as e:
        return None, f"Claude API error {e.status_code}"
    if resp.stop_reason == "refusal":
        return None, "Claude declined this request"
    text = "".join(b.text for b in resp.content if b.type == "text").strip()
    bad = unknown_numbers(text, compact)
    if bad:
        return None, f"Claude's text used numbers not in the stats {bad[:5]}, so it was rejected"
    return text + "\n", None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--ask")
    ap.add_argument("--claude", action="store_true")
    ap.add_argument("--co2-limit", type=float, default=1000)
    ap.add_argument("--json", help="also save the stats here")
    a = ap.parse_args()
    s = compute(a.log, {"co2": a.co2_limit})
    if a.json:
        Path(a.json).write_text(json.dumps(s, indent=1))
    if a.ask:
        print(answer(s, a.ask))
        return
    if a.claude:
        text, why = claude_diary(s)
        if text:
            print(text)
            return
        print(f"[Using the template diary: {why}.]\n")
    print(template_diary(s))


if __name__ == "__main__":
    main()
