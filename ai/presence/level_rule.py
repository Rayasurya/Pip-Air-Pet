"""Presence from the CO2 LEVEL, not the CO2 rise.

Why this exists: Pip's first presence rule looked for rising CO2 (a breathing person adds CO2).
In the real room it missed a sleeper almost every minute. A sleeping person in an airy room
holds the CO2 at a steady level above the empty room, so there is no rise to find.

This rule looks at the level instead:

  1. The room's own EMPTY LEVEL is the 5th percentile of the last 7 days of readings, kept
     between 400 and 500 ppm (outdoor air). Until the room has 10 hours of readings it is 430.
     It only ever uses the past, so the same rule could run on the device.
  2. "Someone here" starts when CO2 stays at least `on_ppm` above the empty level for `on_min`
     minutes in a row.
  3. "Empty" starts when CO2 stays below `off_ppm` above the empty level for `off_min` minutes
     in a row. The two limits differ so the answer doesn't flicker.
  4. After a hole in the log (Pip was off), the state restarts from the level itself.
     Minutes Pip did not log are never filled in.

Known limit: CO2 lingers after a person leaves, so "empty" comes late. See events.py.

Pure Python, no other files needed. Tests: test_level_rule.py
"""
import csv
from bisect import bisect_left, insort
from collections import deque
from dataclasses import dataclass
from datetime import datetime, timedelta

GAP_MIN = 3   # a hole longer than this many minutes means Pip was off


@dataclass(frozen=True)
class Row:
    t: datetime
    co2: float
    temp: float = 0.0
    hum: float = 0.0


@dataclass(frozen=True)
class Rule:
    on_ppm: float = 80.0          # "someone here" at or above this much over the empty level
    off_ppm: float = 65.0         # "empty" below this much over the empty level
    on_min: int = 5
    off_min: int = 10
    base_default: float = 430.0   # empty level before the room has shown its own
    base_window_h: int = 168      # 7 days: always includes weekday daytimes
    base_min_minutes: int = 600   # 10 hours of readings before the room's own level is trusted
    base_q: float = 0.05
    base_lo: float = 400.0
    base_hi: float = 500.0


def load(path):
    """Read a Pip log. Skips comment lines, rows without a real date, and readings of 0."""
    rows = []
    with open(path) as f:
        lines = (ln for ln in f if not ln.startswith("#"))
        for x in csv.DictReader(lines):
            try:
                t = datetime.strptime(x["time"], "%Y-%m-%d %H:%M")
                co2 = float(x["co2"])
            except (ValueError, KeyError, TypeError):
                continue
            if co2 <= 0:
                continue
            rows.append(Row(t, co2, _num(x.get("temp")), _num(x.get("hum"))))
    rows.sort(key=lambda r: r.t)
    return rows


def _num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return 0.0


def empty_level(rows, rule=Rule()):
    """The room's empty level at every row, from past readings only. Recomputed once an hour."""
    out = []
    window = deque()     # (time, co2) inside the trailing window
    ordered = []         # the same co2 values, kept sorted
    span = timedelta(hours=rule.base_window_h)
    level, stamp = rule.base_default, None
    for r in rows:
        while window and r.t - window[0][0] > span:
            _, old = window.popleft()
            del ordered[bisect_left(ordered, old)]
        hour = (r.t.date(), r.t.hour)
        if hour != stamp:
            stamp = hour
            if len(ordered) >= rule.base_min_minutes:
                q = ordered[int(rule.base_q * (len(ordered) - 1))]
                level = min(rule.base_hi, max(rule.base_lo, q))
            else:
                level = rule.base_default
        out.append(level)
        window.append((r.t, r.co2))
        insort(ordered, r.co2)
    return out


def presence(rows, base, rule=Rule()):
    """1 = someone here, 0 = empty, for every row."""
    out = []
    here, on_run, off_run, prev = 0, 0, 0, None
    for r, b in zip(rows, base):
        over = r.co2 - b
        if prev is None or (r.t - prev).total_seconds() / 60 > GAP_MIN:
            on_run = off_run = 0
            here = 1 if over >= rule.on_ppm else 0     # after a hole, start from the level
        on_run = on_run + 1 if over >= rule.on_ppm else 0
        off_run = off_run + 1 if over < rule.off_ppm else 0
        if not here and on_run >= rule.on_min:
            here = 1
        elif here and off_run >= rule.off_min:
            here = 0
        out.append(here)
        prev = r.t
    return out


def run(rows, rule=Rule()):
    base = empty_level(rows, rule)
    return base, presence(rows, base, rule)
