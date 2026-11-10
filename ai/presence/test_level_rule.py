"""Run: tools/.venv/bin/python ai/presence/test_level_rule.py   (from air-pet)

Made-up rooms with a known answer, then the simulated room with its true labels.
No real home data is needed or read.
"""
import sys
import tempfile
from dataclasses import replace
from datetime import datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from level_rule import Row, Rule, empty_level, load, presence, run  # noqa: E402

T0 = datetime(2026, 1, 5, 0, 0)
RULE = Rule()


def room(*parts, start=T0):
    """room((minutes, ppm), (minutes, ppm), ("off", minutes), ...) -> one row per logged minute."""
    rows, t = [], start
    for a, b in parts:
        if a == "off":
            t += timedelta(minutes=b)
            continue
        for _ in range(a):
            rows.append(Row(t, float(b)))
            t += timedelta(minutes=1)
    return rows


def said(rows, rule=RULE):
    return run(rows, rule)[1]


# 1. The sleeper. An empty day, then a flat 110 ppm above the empty level for six hours.
#    There is no rise to find. The level rule still says "here" for the whole night.
rows = room((720, 430), (360, 540))
p = said(rows)
assert sum(p[:720]) == 0
assert p[720:720 + RULE.on_min - 1] == [0] * (RULE.on_min - 1)      # waits 5 minutes
assert all(p[720 + RULE.on_min - 1:]), "a flat sleeper must stay 'here'"

# 2. An empty room that wobbles a little never becomes "here".
rows = [Row(T0 + timedelta(minutes=i), 430 + (i * 7) % 21 - 10) for i in range(1440)]
assert sum(said(rows)) == 0

# 3. A 3-minute spike (a breath on the sensor) is not a person.
assert sum(said(room((60, 430), (3, 900), (60, 430)))) == 0

# 4. Between the two limits nothing flips. Empty stays empty, here stays here.
assert sum(said(room((60, 430), (120, 430 + 70)))) == 0
p = said(room((60, 430), (30, 430 + 150), (120, 430 + 70)))
assert all(p[60 + RULE.on_min - 1:])

# 5. Leaving. "Empty" comes after 10 minutes under the lower limit, not before.
p = said(room((60, 430), (30, 600), (30, 440)))
assert all(p[90:90 + RULE.off_min - 1])
assert sum(p[90 + RULE.off_min - 1:]) == 0

# 6. After a hole in the log the state restarts from the level. Nothing is carried over the hole.
rows = room((30, 600), ("off", 120), (5, 440))
assert said(rows)[30:] == [0] * 5, "here before the hole, empty air after it: empty at once"
rows = room((30, 430), ("off", 120), (5, 600))
assert said(rows)[30:] == [1] * 5, "empty before the hole, breathed air after it: here at once"
assert len(rows) == 35, "minutes inside the hole do not exist: never filled in"

# 7. The empty level. 430 until the room has 10 hours of readings, then the room's own low level.
rows = room((599, 470), (200, 470))
base = empty_level(rows)
assert base[0] == base[598] == 430
assert base[-1] == 470, base[-1]
#    It is kept between 400 and 500, so a room that never empties can't hide a person.
assert empty_level(room((2000, 700)))[-1] == 500
assert sum(said(room((2000, 700)))[RULE.on_min:]) == 2000 - RULE.on_min
assert empty_level(room((2000, 380)))[-1] == 400
#    It only uses the past: changing later readings does not change earlier answers.
a = room((900, 450), (300, 600))
b = room((900, 450), (300, 900))
assert empty_level(a)[:900] == empty_level(b)[:900]
assert said(a)[:900] == said(b)[:900]

# 8. Reading a log: comment lines, rows before the clock was set, and readings of 0 are skipped.
with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
    f.write("# a comment\ntime,co2,temp,hum\n+12,600,25,60\n2026-01-05 10:00,0,0,0\n"
            "2026-01-05 10:02,512,25.5,61\n2026-01-05 10:01,510,25.4,60\n")
got = load(f.name)
Path(f.name).unlink()
assert [(r.t.minute, r.co2) for r in got] == [(1, 510.0), (2, 512.0)], got

# 9. The simulated room, which knows who was really in it. SIMULATED, not a real home.
SIM = Path(__file__).resolve().parents[1] / "data" / "sim_room.csv"
import csv  # noqa: E402

truth = {}
with SIM.open() as f:
    for x in csv.DictReader(ln for ln in f if not ln.startswith("#")):
        truth[x["time"]] = (int(x["truth_people"]) > 0, x["truth_activity"])
rows = load(SIM)
p = said(rows)
tally = {}
for r, here in zip(rows, p):
    someone, activity = truth[r.t.strftime("%Y-%m-%d %H:%M")]
    for key in ("someone" if someone else "nobody", activity):
        n, hit = tally.get(key, (0, 0))
        tally[key] = (n + 1, hit + here)
share = {k: hit / n for k, (n, hit) in tally.items()}
assert share["sleep"] > 0.99, share            # the sleeper is caught
assert share["someone"] > 0.95, share
# KNOWN LIMIT, kept here on purpose. The simulated room is closed and holds CO2 for hours after
# the person leaves, so the level alone calls the empty room "here" most of the time.
# events.py adds an "air clearing" state for exactly this.
assert share["nobody"] > 0.5, share

# 10. A different limit is a different rule: the on/off limits are the only tuned numbers.
strict = replace(RULE, on_ppm=200, off_ppm=185)
assert sum(said(room((720, 430), (360, 540)), strict)) == 0

print("level rule tests passed "
      f"(simulated room: sleeper caught {share['sleep']:.1%}, "
      f"empty room wrongly 'here' {share['nobody']:.1%})")
