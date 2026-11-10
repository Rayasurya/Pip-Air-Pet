"""Run: tools/.venv/bin/python ai/presence/test_events.py   (from air-pet)

Made-up rooms with a known answer. No real home data is needed or read.
"""
import math
import sys
from datetime import date, datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from events import (Block, STUFFY_PPM, day_blocks, describe, diary, hm, mark_unusual,  # noqa: E402
                    stuffy_spells, when)
from level_rule import Row  # noqa: E402

MON = datetime(2026, 1, 5, 0, 0)          # a Monday


def room(*parts, start=MON):
    """room((minutes, ppm), (minutes, function of the minute), ("off", minutes), ...)"""
    rows, t = [], start
    for a, b in parts:
        if a == "off":
            t += timedelta(minutes=b)
            continue
        for i in range(a):
            rows.append(Row(t, float(b(i) if callable(b) else b)))
            t += timedelta(minutes=1)
    return rows


def fall(top, floor, half_life=25):
    """CO2 leaving a room nobody breathes in: it drops fast at first, then slower."""
    return lambda i: floor + (top - floor) * math.exp(-i * math.log(2) / half_life)


def kinds(rows):
    return [(b.kind, b.outcome) for b in diary(rows)[0]]


def at(rows, hour, minute=0, day=0):
    """What the diary says at one clock time."""
    t = rows[0].t.replace(hour=0, minute=0) + timedelta(days=day, hours=hour, minutes=minute)
    return next(b for b in diary(rows)[0] if b.start <= t < b.end)


# 1. Pip was off. A hole in the log becomes one "off" block with the hole's exact times.
#    Nothing is filled in, and the words say so.
rows = room((60, 430), ("off", 300), (60, 430))
bl = diary(rows)[0]
assert [b.kind for b in bl] == ["empty", "off", "empty"], bl
off = bl[1]
assert (off.start, off.end) == (MON + timedelta(minutes=60), MON + timedelta(minutes=360))
assert off.minutes == 0 and off.sure == ""
assert sum(b.minutes for b in bl) == len(rows) == 120, "no minute is invented"
d = describe(off)
assert d["title"] == "Pip was off"
assert d["evidence"] == "No readings from 01:00 to 06:00 (5 h 00 min)."
assert d["reading"] == "Nothing is guessed for this time."
assert bl[2].after_off and "just come back on" in describe(bl[2])["why"]

# 2. Someone leaves and the air clears to the empty level: here, air clearing (emptied), empty.
#    The clearing block starts where the fall began, not when the rule finally lets go.
rows = room((600, 430), (120, 700), (240, fall(700, 432)), (60, 432), start=MON)
assert kinds(rows) == [("empty", ""), ("here", ""), ("clearing", "emptied"), ("empty", "")], kinds(rows)
clearing = diary(rows)[0][2]
left = MON + timedelta(minutes=720)
assert abs((clearing.start - left).total_seconds()) <= 5 * 60, clearing.start
assert clearing.sure == "not sure", "falling air has two causes, so Pip is never sure"
d = describe(clearing)
assert "probably emptied around 12:0" in d["reading"] and "can't tell them apart" in d["why"]

# 3. The fall stops well above the empty level: someone is still in there (or came back).
#    Pip says "air clearing", then "someone here". It never says "empty".
rows = room((600, 430), (120, 900), (60, fall(900, 600, 15)), (180, 600))
k = kinds(rows)
assert ("clearing", "held") in k and ("empty", "") not in k[1:], k
held = next(b for b in diary(rows)[0] if b.kind == "clearing")
assert "window or door" in describe(held)["reading"]

# 4. The log stops in the middle of a fall: Pip says it can't tell how it ended.
rows = room((600, 430), (120, 900), (45, fall(900, 430, 40)))
last = diary(rows)[0][-1]
assert (last.kind, last.outcome) == ("clearing", "cut"), last
assert "can't say how this ended" in describe(last)["reading"]

# 5. Likely asleep needs all three: night, steady air, and 2 hours or more.
night = MON.replace(hour=20)
rows = room((180, 430), (60, lambda i: 430 + i * 2), (480, 550), start=night)   # still from 00:00
assert at(rows, 3, day=1).kind == "asleep"
assert describe(at(rows, 3, day=1))["title"] == "Likely asleep"
assert "A guess" in describe(at(rows, 3, day=1))["reading"]
#    the same air at midday is only "someone here"
rows = room((180, 430), (60, lambda i: 430 + i * 2), (480, 550), start=MON.replace(hour=8))
assert at(rows, 15).kind == "here"
#    90 quiet minutes at night are not enough
rows = room((180, 430), (60, lambda i: 430 + i * 2), (90, 550), (120, fall(550, 430, 10)), start=night)
assert "asleep" not in [b.kind for b in diary(rows)[0]]
#    restless air at night is "someone here", not sleep
rows = room((180, 430), (60, lambda i: 430 + i * 2), (480, lambda i: 600 + 60 * math.sin(i / 4)), start=night)
assert at(rows, 3, day=1).kind == "here"

# 6. How sure. Far above every limit: sure. Between the limits: not sure, and it says why.
rows = room((600, 430), (240, 630))
b = at(rows, 12)
assert (b.kind, b.sure) == ("here", "sure") and "60 to 110" in describe(b)["why"]
rows = room((600, 430), (240, 525))            # 95 over: limit 80 says here, limit 110 says empty
b = at(rows, 12)
assert (b.kind, b.sure) == ("here", "not sure") and "different limit" in describe(b)["why"]
assert Block("off", MON, MON).sure == ""

# 7. One day at a time. Blocks are cut at midnight, and time with no log is "off".
rows = room((120, 430), start=MON.replace(hour=23))          # 23:00 Monday to 01:00 Tuesday
bl = diary(rows)[0]
mon, tue = day_blocks(bl, date(2026, 1, 5)), day_blocks(bl, date(2026, 1, 6))
assert [(b.kind, lo.hour, hi.day) for b, lo, hi in mon] == [("off", 0, 5), ("empty", 23, 6)]
assert [(b.kind, lo.hour, hi.hour) for b, lo, hi in tue] == [("empty", 0, 1), ("off", 1, 0)]
for day in (mon, tue):
    assert sum((hi - lo).total_seconds() for _, lo, hi in day) == 24 * 3600, "a day is always 24 hours"
assert [b.kind for b, _, _ in day_blocks(bl, date(2026, 1, 9))] == ["off"], "a day with no log is all off"

# 8. Stuffy spells: 5 minutes or more at Pip's stuffy limit. Shorter is ignored.
rows = room((60, 600), (12, STUFFY_PPM + 50), (60, 600), (3, STUFFY_PPM + 50), (60, 600))
spells = stuffy_spells(rows)
assert len(spells) == 1 and spells[0]["peak"] == STUFFY_PPM + 50
assert spells[0]["start"] == MON + timedelta(minutes=60)
#    a short dip does not split a spell, and a hole in the log always does
assert len(stuffy_spells(room((30, 600), (20, 1100), (10, 950), (20, 1100), (30, 600)))) == 1
assert len(stuffy_spells(room((30, 600), (20, 1100), ("off", 10), (20, 1100), (30, 600)))) == 2
#    the same weekday, around the same time, is remembered once per day
week = [(720, 600), (30, 1100), (5, 950), (30, 1100), (655, 600)] + [(1440, 600)] * 6
again = stuffy_spells(room(*(week * 2)))
assert len(again) == 2 and again[1]["also"] == [MON.date()], again

# 9. Unusual: four weekdays empty at midday, then a weekday when someone is in at midday.
day = [(600, 430), (240, 430), (600, 430)]
busy = [(600, 430), (240, 700), (600, 430)]
rows = room(*(day * 4 + busy))
flagged = [b for b in diary(rows)[0] if b.unusual]
assert len(flagged) == 1 and flagged[0].kind == "here", flagged
assert flagged[0].unusual == "This time was empty on 4 of the 4 earlier weekdays Pip logged."
#    with too few earlier days to compare, nothing is flagged
assert not [b for b in diary(room(*(day * 2 + busy)))[0] if b.unusual]
assert mark_unusual([]) == []

# 10. Small wording helpers.
assert hm(59) == "59 min" and hm(125) == "2 h 05 min"
assert when(MON.replace(hour=23), MON + timedelta(days=1)) == "23:00 to midnight"
assert when(MON.replace(hour=23), MON + timedelta(days=1, hours=7)) == "Mon 5 Jan 23:00 to Tue 6 Jan 07:00"
for kind, outcome in [("here", ""), ("asleep", ""), ("empty", ""), ("clearing", "emptied"),
                      ("clearing", "held"), ("clearing", "cut"), ("off", "")]:
    text = " ".join(describe(Block(kind, MON, MON + timedelta(hours=1), minutes=60, outcome=outcome)).values())
    assert chr(0x2014) not in text and "definitely" not in text.lower(), text

print("events tests passed")
