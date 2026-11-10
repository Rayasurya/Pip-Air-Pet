"""Score the level rule, and say exactly what each number rests on.

Run from air-pet:
    tools/.venv/bin/python ai/presence/score_level.py

Two rooms are scored.

  SIMULATED ROOM   ai/data/sim_room.csv. It knows who was really in it, so these are true
                   labels. But it is a simulation, not a home.
  REAL ROOM        data/pip/log_all.csv (private, not in the repo). There is no record of who
                   was in the room, so the labels are ASSUMED from a routine:
                     asleep        01:00 to 06:00, every day
                     out           10:00 to 15:00, weekdays
                     home evening  18:00 to 22:00, every day
                   Only minutes Pip logged are counted. Nights when Pip was off are not filled.

How the limit was picked, so it can be checked:
  - Days before 27 Oct 2026 are for picking. Days after it are for testing. 27 Oct is unused.
  - On the picking days, try on-limits 30 to 130 ppm (off-limit 15 below). Keep the one with the
    biggest gap between "asleep called here" and "out called here".
  - Score that one limit on the test days. Then show the other limits on the test days too,
    so you can see how much the answer depends on the pick.

If data/pip/presence/ground_truth.csv exists (made by the "Right / Wrong?" buttons in
room_diary.py), the rule is also scored against those checked minutes.

Writes data/pip/presence/level_summary.json when the real log is there.
"""
import argparse
import csv
import json
from collections import Counter, defaultdict
from dataclasses import replace
from datetime import date, datetime, timedelta
from pathlib import Path

from events import read
from level_rule import Rule, empty_level, load, presence

ROOT = Path(__file__).resolve().parents[2]
SIM = ROOT / "ai" / "data" / "sim_room.csv"
REAL = ROOT / "data" / "pip" / "log_all.csv"
OUT = ROOT / "data" / "pip" / "presence"
SPLIT = date(2026, 10, 27)
LIMITS = range(30, 131, 10)
ROUTINE = ("asleep", "out", "home_evening")


def routine(t):
    """ASSUMED label for one minute, from a routine. Not a record of where anyone was."""
    if 1 <= t.hour < 6:
        return "asleep"
    if t.weekday() < 5 and 10 <= t.hour < 15:
        return "out"
    if 18 <= t.hour < 22:
        return "home_evening"
    return None


def share_here(rows, said, keep):
    """For each routine label: (share of its minutes called "here", how many minutes)."""
    n, hit = Counter(), Counter()
    for r, p in zip(rows, said):
        lab = routine(r.t)
        if lab and keep(r.t.date()):
            n[lab] += 1
            hit[lab] += p
    return {k: {"called_here": round(hit[k] / n[k], 3) if n[k] else None, "minutes": n[k]} for k in ROUTINE}


def at_limit(rows, base, on):
    return presence(rows, base, replace(Rule(), on_ppm=on, off_ppm=on - 15))


def pct(x):
    return "  n/a" if x is None else f"{x:6.1%}"


def real_room(path):
    rows = load(path)
    base = empty_level(rows)
    cal, test = (lambda d: d < SPLIT), (lambda d: d > SPLIT)
    out = {"log": {"rows": len(rows), "from": str(rows[0].t), "to": str(rows[-1].t)},
           "labels": "ASSUMED from a routine, not measured. See score_level.py.",
           "split": f"picked on days before {SPLIT}, tested on days after it"}

    print(f"\nREAL ROOM  {len(rows):,} logged minutes, {rows[0].t:%d %b} to {rows[-1].t:%d %b %Y}")
    print("Labels are ASSUMED from a routine. Share of minutes the rule called 'here':\n")
    print("  picking days        asleep     out   evening    gap")
    best, sweep = None, {}
    for on in LIMITS:
        s = share_here(rows, at_limit(rows, base, on), cal)
        gap = (s["asleep"]["called_here"] or 0) - (s["out"]["called_here"] or 0)
        sweep[on] = s
        print(f"  limit {on:3d} ppm      {pct(s['asleep']['called_here'])} {pct(s['out']['called_here'])}"
              f"  {pct(s['home_evening']['called_here'])} {gap:6.1%}")
        if best is None or gap > best[1] + 1e-9:
            best = (on, gap)
    chosen = best[0]
    out["picking_days"] = {"sweep": sweep, "chosen_on_ppm": chosen,
                           "why": "biggest gap between asleep and out; the lowest limit wins a tie"}
    print(f"\n  picked: {chosen} ppm  (the rule ships with {Rule().on_ppm:.0f})")
    if chosen != Rule().on_ppm:
        print("  NOTE: this pick differs from the limit in level_rule.py. Check before quoting numbers.")

    print("\n  test days           asleep     out   evening")
    tests = {}
    for on in LIMITS:
        s = share_here(rows, at_limit(rows, base, on), test)
        tests[on] = s
        mark = "  <- the picked limit" if on == chosen else ""
        print(f"  limit {on:3d} ppm      {pct(s['asleep']['called_here'])} {pct(s['out']['called_here'])}"
              f"  {pct(s['home_evening']['called_here'])}{mark}")
    out["test_days"] = {"at_chosen": tests[chosen], "other_limits": tests}
    n = tests[chosen]
    print(f"\n  Test days at {chosen} ppm: asleep called here {pct(n['asleep']['called_here']).strip()} "
          f"of {n['asleep']['minutes']:,} minutes, out called here {pct(n['out']['called_here']).strip()} "
          f"of {n['out']['minutes']:,}.")

    old = old_rule(rows, at_limit(rows, base, chosen), test)
    if old:
        out["old_rule_same_minutes"] = old
        print("\n  Old rule (looks for rising CO2) and new rule, on the test minutes both answered:")
        for k in ROUTINE:
            o = old[k]
            print(f"    {k:13s} old {pct(o['old_called_here'])}   new {pct(o['new_called_here'])}   ({o['minutes']:,} min)")

    _, _, state, _ = read(rows)
    tally = defaultdict(Counter)
    for r, s in zip(rows, state):
        lab = routine(r.t)
        if lab and test(r.t.date()):
            tally[lab][s] += 1
    out["diary_states_test_days"] = shares(tally)
    print("\n  What the diary shows on the test days (reading aid, not a tested result):")
    table(out["diary_states_test_days"])

    truth = OUT / "ground_truth.csv"
    if truth.exists():
        out["checked_by_hand"] = checked(rows, at_limit(rows, base, chosen), state, truth)
        c = out["checked_by_hand"]
        print(f"\n  Checked by hand ({c['checks']} checks, {c['minutes']:,} logged minutes):")
        print(f"    someone-or-nobody right on {pct(c['rule_right']).strip()} of those minutes")
        for k, v in c["by_truth"].items():
            print(f"    truth '{k}': {v['minutes']:,} min, rule right {pct(v['rule_right']).strip()}, diary said {v['diary_said']}")
    else:
        print("\n  No hand-checked minutes yet. Run room_diary.py and use the Right / Wrong? buttons.")
    return out


def old_rule(rows, new, keep):
    """The first rule's answers are in timeline.csv (made by real_pip.py). Compare minute by minute,
    only where the old rule gave an answer."""
    path = OUT / "timeline.csv"
    if not path.exists():
        return None
    old = {}
    with path.open() as f:
        for x in csv.DictReader(f):
            if x["present"] != "":
                old[x["time"]] = int(x["present"])
    n, o, w = Counter(), Counter(), Counter()
    for r, p in zip(rows, new):
        lab = routine(r.t)
        key = r.t.strftime("%Y-%m-%d %H:%M")
        if lab and keep(r.t.date()) and key in old:
            n[lab] += 1
            o[lab] += old[key]
            w[lab] += p
    return {k: {"minutes": n[k], "old_called_here": round(o[k] / n[k], 3) if n[k] else None,
                "new_called_here": round(w[k] / n[k], 3) if n[k] else None} for k in ROUTINE}


def checked(rows, said, state, path):
    """Score against minutes a person checked. state: in | asleep = someone there, anything else = nobody."""
    at = {r.t: i for i, r in enumerate(rows)}
    n = hit = checks = 0
    by = defaultdict(lambda: [0, 0, Counter()])
    with path.open() as f:
        for x in csv.DictReader(f):
            try:
                a = datetime.strptime(x["start"], "%Y-%m-%d %H:%M")
                b = datetime.strptime(x["end"], "%Y-%m-%d %H:%M")
            except (KeyError, ValueError):
                continue
            checks += 1
            truth = x["state"].strip()
            want = 1 if truth in ("in", "asleep") else 0
            t = a
            while t < b:
                i = at.get(t)
                if i is not None:
                    n += 1
                    hit += said[i] == want
                    by[truth][0] += 1
                    by[truth][1] += said[i] == want
                    by[truth][2][state[i]] += 1
                t += timedelta(minutes=1)
    return {"checks": checks, "minutes": n, "rule_right": round(hit / n, 3) if n else None,
            "by_truth": {k: {"minutes": v[0], "rule_right": round(v[1] / v[0], 3) if v[0] else None,
                             "diary_said": dict(v[2])} for k, v in by.items()}}


def shares(tally):
    return {k: {"minutes": sum(c.values()),
                **{s: round(c[s] / sum(c.values()), 3) for s in ("here", "asleep", "clearing", "empty")}}
            for k, c in tally.items()}


def table(sh):
    print("    " + " " * 22 + "minutes    here  asleep clearing  empty")
    for k, v in sh.items():
        print(f"    {k:22s}{v['minutes']:7,} {pct(v['here'])} {pct(v['asleep'])}  {pct(v['clearing'])} {pct(v['empty'])}")


def sim_room():
    truth = {}
    with SIM.open() as f:
        for x in csv.DictReader(ln for ln in f if not ln.startswith("#")):
            truth[x["time"]] = (int(x["truth_people"]) > 0, x["truth_activity"], x["truth_window"] not in ("0", ""))
    rows = load(SIM)
    base, here, state, _ = read(rows)
    rule, diary = defaultdict(Counter), defaultdict(Counter)
    for r, h, s in zip(rows, here, state):
        someone, activity, window = truth[r.t.strftime("%Y-%m-%d %H:%M")]
        who = "someone in" if someone else "nobody in"
        keys = [who, f"  {activity}" + (", window open" if window else "")] if someone else [who]
        for k in keys:
            rule[k]["here" if h else "empty"] += 1
            diary[k][s] += 1
    print(f"\nSIMULATED ROOM  {len(rows):,} minutes. True labels, but a simulation, not a home.\n")
    print("  The level rule alone:")
    out = {"data": "SIMULATED. True labels from the simulator.", "level_rule": {}, "diary_states": shares(diary)}
    for k in sorted(rule, key=lambda k: (k.strip() != k, k)):
        c = rule[k]
        n = sum(c.values())
        out["level_rule"][k.strip()] = {"minutes": n, "called_here": round(c["here"] / n, 3)}
        print(f"    {k:28s}{n:8,} min   called here {c['here'] / n:6.1%}")
    nobody = out["level_rule"]["nobody in"]["called_here"]
    print(f"\n  KNOWN LIMIT: the empty room is still called 'here' {nobody:.1%} of the time. This room is")
    print("  closed, so CO2 hangs around for hours after the person leaves. The level alone can't see that.")
    print("\n  With the diary's 'air clearing' state (reading aid, not a tested result):")
    table({k.strip() if k.strip() == k else k: v for k, v in sorted(out["diary_states"].items(),
                                                                    key=lambda kv: (kv[0].strip() != kv[0], kv[0]))})
    print("  'Likely asleep' never fires here: the simulated sensor is about 4 times noisier than the")
    print("  real one, so the air never looks steady. The diary shows those nights as 'someone here'.")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--log", type=Path, default=REAL, help="a real Pip log (default: the private one)")
    args = ap.parse_args()
    result = {"simulated_room": sim_room()}
    if args.log.exists():
        result["real_room"] = real_room(args.log)
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "level_summary.json").write_text(json.dumps(result, indent=1, default=str))
        print(f"\nSaved {OUT / 'level_summary.json'} (private)")
    else:
        print(f"\nNo real log at {args.log}. That data is private, so only the simulated room was scored.")


if __name__ == "__main__":
    main()
