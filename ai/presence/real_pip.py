"""REAL-DATA check: can Pip tell when someone is in the room, from CO2 alone?

Runs Kin's presence physics (kin/engine/kin_engine.py) on Pip's own minute log from Raya's room
(data/pip/log_all.csv, private, git-ignored). No camera, no microphone, no labels.

How it works: CO2 production in the room = how fast CO2 rises + what leaks out (leak rate learned
from the room's own clean decays). A breathing person makes CO2; an empty room makes none.

There are no minute-by-minute labels, so the checks are:
  1. the learned room physics (leak rate, the two production levels) look physically sane
  2. a weekday vs weekend split of daytime presence (label-free sanity check)
  3. Raya's own notes of when they were in and out (ground_truth.csv, if present)

Outputs go to data/pip/presence/ (private). Only totals are ever published.

Run: tools/.venv/bin/python ai/presence/real_pip.py
"""
import csv
import json
import sys
from collections import defaultdict
from dataclasses import replace
from datetime import datetime, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "kin" / "engine"))
from kin_engine import Config, Engine, percentile, two_clusters  # noqa: E402

LOG = ROOT / "data" / "pip" / "log_all.csv"
OUT = ROOT / "data" / "pip" / "presence"
TRUTH = OUT / "ground_truth.csv"      # rows: start,end,state  (e.g. 2026-11-03 09:10,2026-11-03 18:40,away)
GAP_MIN = 3


FILLED_LOG = OUT / "log_filled.csv"   # from fill_nights.py: real rows + FILLED night rows (filled=1)
FILLED = set()                        # minutes that were filled, not measured


def load(path=LOG):
    rows = []
    with path.open() as f:
        for x in csv.DictReader(f):
            co2 = float(x["co2"])
            if co2 <= 0:
                continue
            t = datetime.strptime(x["time"], "%Y-%m-%d %H:%M")
            if x.get("filled") == "1":
                FILLED.add(t)
            rows.append((t, co2))
    return rows


def segments(rows):
    seg = [rows[0]]
    for prev, cur in zip(rows, rows[1:]):
        if (cur[0] - prev[0]).total_seconds() / 60 > GAP_MIN:
            yield seg
            seg = []
        seg.append(cur)
    yield seg


def run(rows, cfg):
    """Kin's CO2-only presence rules, carried across gaps (the learned room physics is kept,
    the short-term CO2 history restarts). Returns {time: (present 0/1, production ppm/min)}."""
    e = Engine(cfg=cfg)
    out = {}
    last_day = None
    for seg in segments(rows):
        e.co2_hist, e.ema = [], None
        present, on_run, gen_low_run, away_since, gen_avg = False, 0, 0, None, 0.0
        for i, (t, co2) in enumerate(seg):
            mod = t.hour * 60 + t.minute
            if last_day is not None and t.date() != last_day:
                e._end_of_day(0)
            last_day = t.date()
            empty = (not present) and away_since is not None and i - away_since >= 10
            e.observe(co2, mod, empty)
            g = e.generation()
            gen_avg = 0.9 * gen_avg + 0.1 * g
            on_run = on_run + 1 if gen_avg >= cfg.breathing_min else 0
            gen_low_run = gen_low_run + 1 if gen_avg < cfg.empty_max else 0
            if not present and on_run >= cfg.enter_confirm_min:
                present, away_since = True, None
            elif present and gen_low_run >= cfg.leave_confirm_min:
                present, away_since = False, i - cfg.leave_confirm_min
            if away_since is None and not present:
                away_since = i
            # the first 10 minutes of a segment have no slope yet: mark them unknown
            out[t] = (None if i < 11 else (1 if present else 0), gen_avg)
    return out, e


def calibrated(res, cut_time):
    hist = [0] * 80
    for t, (_, g) in res.items():
        if t < cut_time:
            hist[int(min(79, max(0, g / 0.25)))] += 1
    cl = two_clusters(hist, 0.25)
    if cl is None:
        return Config(), None
    lo, hi = cl
    return replace(Config(), breathing_min=min(2.5, max(0.6, lo + 0.45 * (hi - lo))),
                   empty_max=min(1.2, max(0.3, lo + 0.2 * (hi - lo)))), (round(lo, 2), round(hi, 2))


def share(res, keep):
    vals = [p for t, (p, _) in res.items() if p is not None and keep(t)]
    return round(sum(vals) / len(vals), 3) if vals else None, len(vals)


def truth_check(res):
    if not TRUTH.exists():
        return None
    out = {"minutes": 0, "agree": 0, "by_state": defaultdict(lambda: [0, 0])}
    with TRUTH.open() as f:
        for row in csv.DictReader(f):
            a = datetime.strptime(row["start"], "%Y-%m-%d %H:%M")
            b = datetime.strptime(row["end"], "%Y-%m-%d %H:%M")
            want = 1 if row["state"].strip() in ("in", "asleep") else 0
            t = a
            while t < b:
                p = res.get(t, (None,))[0]
                if p is not None:
                    out["minutes"] += 1
                    out["agree"] += p == want
                    s = out["by_state"][row["state"].strip()]
                    s[0] += 1
                    s[1] += p == want
                t += timedelta(minutes=1)
    out["by_state"] = {k: {"minutes": v[0], "correct": round(v[1] / max(1, v[0]), 3)} for k, v in out["by_state"].items()}
    out["accuracy"] = round(out["agree"] / max(1, out["minutes"]), 3)
    return out


def raya_label(t):
    """ASSUMED labels from Raya's own account (7 Nov 2026), not measured, not minute-exact:
    sleeps in this room every night; out of the house most of the day; home in the evening
    (home, not necessarily in this room). Only minutes Pip actually logged are scored:
    nights Pip was switched off are never filled in."""
    h = t.hour
    if 1 <= h < 6:
        return "asleep"                     # safely inside any night's sleep
    if t.weekday() < 5 and 10 <= h < 15:
        return "out"                        # weekday daytime ("most of the day I'm not home")
    if 18 <= h < 22:
        return "home_evening"               # in the house; may be in another room
    return None


def raya_check(res, rows, cout):
    co2 = dict(rows)
    out = {}
    for state in ("asleep", "out", "home_evening"):
        mins = [t for t, (p, _) in res.items() if p is not None and raya_label(t) == state]
        if not mins:
            continue
        if FILLED:
            real_m = [t for t in mins if t not in FILLED]
            fill_m = [t for t in mins if t in FILLED]
            out[state + " (measured minutes only)"] = {
                "minutes": len(real_m),
                "model_says_someone_here": round(sum(res[t][0] for t in real_m) / max(1, len(real_m)), 3)}
            out[state + " (FILLED minutes only, not measured)"] = {
                "minutes": len(fill_m),
                "model_says_someone_here": round(sum(res[t][0] for t in fill_m) / max(1, len(fill_m)), 3)}
        excess = [co2[t] - cout for t in mins]
        nights = sorted({(t - timedelta(hours=6)).date() for t in mins}) if state == "asleep" else None
        out[state] = {"minutes": len(mins),
                      "model_says_someone_here": round(sum(res[t][0] for t in mins) / len(mins), 3),
                      "co2_above_outdoor_ppm_median": round(percentile(excess, 0.5)),
                      "co2_above_outdoor_ppm_p10_p90": [round(percentile(excess, 0.1)), round(percentile(excess, 0.9))]}
        if nights:
            out[state]["nights_logged"] = [str(d) for d in nights]
    return out


def chart(res, path, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    days = sorted({t.date() for t in res})
    first, last = days[0], days[-1]
    all_days = [first + timedelta(days=i) for i in range((last - first).days + 1)]
    fig, ax = plt.subplots(figsize=(11, 0.32 * len(all_days) + 1.2))
    col = {1: "#2f6f5e", 0: "#e3e1da", None: "#ffffff"}
    for row, d in enumerate(all_days):
        ax.barh(row, 24, left=0, color="#f6f5f1", edgecolor="#cfcdc5", linewidth=0.5, height=0.8)
        runs, cur, start = [], None, None
        for m in range(1440):
            t = datetime.combine(d, datetime.min.time()) + timedelta(minutes=m)
            p = res[t][0] if t in res else "gap"
            if p != cur:
                if cur is not None:
                    runs.append((start, m, cur))
                cur, start = p, m
        runs.append((start, 1440, cur))
        for a, b, p in runs:
            if p == "gap":
                continue
            ax.barh(row, (b - a) / 60, left=a / 60, color=col[p], height=0.8, linewidth=0)
        if FILLED:
            m0 = None
            for m in range(1441):
                t = datetime.combine(d, datetime.min.time()) + timedelta(minutes=m)
                f = m < 1440 and t in FILLED
                if f and m0 is None:
                    m0 = m
                elif not f and m0 is not None:
                    ax.barh(row, (m - m0) / 60, left=m0 / 60, color="none", edgecolor="#8a877d",
                            hatch="////", height=0.8, linewidth=0)
                    m0 = None
    ax.set_yticks(range(len(all_days)))
    ax.set_yticklabels([d.strftime("%a %d %b") for d in all_days], fontsize=8)
    ax.invert_yaxis()
    ax.set_xlim(0, 24)
    ax.set_xticks(range(0, 25, 3))
    ax.set_xticklabels([f"{h:02d}:00" for h in range(0, 25, 3)], fontsize=8)
    ax.set_title(title, fontsize=10, loc="left")
    for s in ax.spines.values():
        s.set_visible(False)
    from matplotlib.patches import Patch
    ax.legend(handles=[Patch(color="#2f6f5e", label="someone breathing in the room"),
                       Patch(color="#e3e1da", label="room empty"),
                       Patch(facecolor="#f6f5f1", edgecolor="#cfcdc5", label="no data (Pip off)")] + ([Patch(facecolor="none", edgecolor="#8a877d", hatch="////", label="FILLED night, not measured")] if FILLED else []),
              loc="upper center", bbox_to_anchor=(0.5, -0.04), ncol=4, fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(path, dpi=140)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    use_filled = "--filled" in sys.argv
    rows = load(FILLED_LOG if use_filled else LOG)
    tag = "_filled" if use_filled else ""
    span = rows[-1][0] - rows[0][0]
    cut = rows[0][0] + span / 3
    fixed, ef = run(rows, Config())
    ccfg, clusters = calibrated(fixed, cut)
    cal, ec = run(rows, ccfg)
    weekday = lambda t: t.weekday() < 5  # noqa: E731
    day = lambda t: 10 <= t.hour < 17     # noqa: E731
    by_hour = {h: share(cal, lambda t, h=h: t.hour == h)[0] for h in range(24)}
    gens = [g for _, g in cal.values()]
    result = {
        "kind": ("REAL data plus FILLED nights (filled=1, not measured)" if use_filled else "REAL data from Pip in Raya's room") + "; CO2 only",
        "filled_minutes": len(FILLED),
        "minutes_logged": len(rows),
        "first": rows[0][0].strftime("%Y-%m-%d %H:%M"),
        "last": rows[-1][0].strftime("%Y-%m-%d %H:%M"),
        "segments": sum(1 for _ in segments(rows)),
        "learned_air_changes_per_hour": round(ec.leak * 60, 2),
        "leak_samples": len(ec.leak_samples),
        "learned_outdoor_ppm": round(ec.cout),
        "production_clusters_ppm_min (empty, someone)": clusters,
        "thresholds": {"breathing_min": round(ccfg.breathing_min, 2), "empty_max": round(ccfg.empty_max, 2)},
        "production_ppm_min_percentiles": {q: round(percentile(gens, q), 2) for q in (0.1, 0.5, 0.9)},
        "present_share_fixed_thresholds": share(fixed, lambda t: True)[0],
        "present_share_calibrated": share(cal, lambda t: True)[0],
        "daytime_10_17_present_share": {"weekdays": share(cal, lambda t: weekday(t) and day(t)),
                                        "weekends": share(cal, lambda t: not weekday(t) and day(t))},
        "evening_18_23_present_share": {"weekdays": share(cal, lambda t: weekday(t) and 18 <= t.hour < 23),
                                        "weekends": share(cal, lambda t: not weekday(t) and 18 <= t.hour < 23)},
        "present_share_by_hour": by_hour,
        "ground_truth_check": truth_check(cal),
        "raya_account_check (ASSUMED labels from Raya's account, see raya_label)": raya_check(cal, rows, ec.cout),
        "day_night": "from the clock (night 18:30-06:00), NOT the light sensor, which never read dark",
    }
    (OUT / f"summary{tag}.json").write_text(json.dumps(result, indent=1, default=str))
    with (OUT / f"timeline{tag}.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time", "present", "production_ppm_min"])
        for t in sorted(cal):
            p, g = cal[t]
            w.writerow([t.strftime("%Y-%m-%d %H:%M"), "" if p is None else p, round(g, 2)])
    chart(cal, OUT / f"timeline{tag}.png", "Pip, real room: when the air says someone is breathing here (CO2 only)"
          + (". Hatched = FILLED night, not measured" if use_filled else ""))
    print(json.dumps(result, indent=1, default=str))


if __name__ == "__main__":
    main()
