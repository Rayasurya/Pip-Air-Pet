"""Air diary, step 1: plain-code statistics from a Pip log (real or simulated). No AI here.

Everything the diary says comes from these numbers, so the writer can't invent facts.
"""
import csv
from collections import Counter, defaultdict
from datetime import datetime

# Default comfort limits (match firmware config.h). Learned limits can be passed in.
LIMITS = {"co2": 1000, "hot": 31.0, "dry": 30.0, "damp": 75.0}
WINDOW_DROP_PPM = 200      # CO2 falls this much within 10 min = a window was opened
WINDOW_MIN_START = 900


def load(path):
    simulated = False
    lines = []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                simulated = simulated or "SIMULATED" in line
                continue
            lines.append(line)
    rows = list(csv.DictReader(lines))
    if not rows or rows[0]["time"].startswith("+"):
        raise SystemExit("This log has no clock times (Pip had no Wi-Fi). The diary needs dates.")
    for r in rows:
        r["_t"] = datetime.strptime(r["time"], "%Y-%m-%d %H:%M")
        r["_co2"] = float(r["co2"])
        r["_temp"] = float(r["temp"])
        r["_hum"] = float(r["hum"])
    return rows, simulated


def window_openings(rows):
    """Times where CO2 fell sharply, the fingerprint of an opened window."""
    out, last = [], None
    for i in range(len(rows) - 10):
        a, b = rows[i]["_co2"], rows[i + 10]["_co2"]
        if a >= WINDOW_MIN_START and a - b >= WINDOW_DROP_PPM:
            t = rows[i]["_t"]
            if last is None or (t - last).total_seconds() > 1800:
                out.append(t)
                last = t
    return out


def day_stats(rows, lim):
    co2 = [r["_co2"] for r in rows]
    peak_i = max(range(len(rows)), key=lambda i: co2[i])
    stuffy_by_hour = Counter(r["_t"].hour for r in rows if r["_co2"] >= lim["co2"])
    worst_hour = stuffy_by_hour.most_common(1)[0][0] if stuffy_by_hour else None
    t_hi = max(rows, key=lambda r: r["_temp"])
    d = {
        "date": rows[0]["_t"].strftime("%Y-%m-%d"),
        "weekday": rows[0]["_t"].strftime("%A"),
        "minutes": len(rows),
        "stuffy_min": sum(1 for c in co2 if c >= lim["co2"]),
        "hot_min": sum(1 for r in rows if r["_temp"] > lim["hot"]),
        "dry_min": sum(1 for r in rows if r["_hum"] < lim["dry"]),
        "damp_min": sum(1 for r in rows if r["_hum"] > lim["damp"]),
        # Hours, pre-rounded, so every number the diary prints exists here.
        "stuffy_h": round(sum(1 for c in co2 if c >= lim["co2"]) / 60, 1),
        "hot_h": round(sum(1 for r in rows if r["_temp"] > lim["hot"]) / 60, 1),
        "dry_h": round(sum(1 for r in rows if r["_hum"] < lim["dry"]) / 60, 1),
        "damp_h": round(sum(1 for r in rows if r["_hum"] > lim["damp"]) / 60, 1),
        "co2_peak": int(round(co2[peak_i])),
        "co2_peak_time": rows[peak_i]["_t"].strftime("%H:%M"),
        "co2_median": int(round(sorted(co2)[len(co2) // 2])),
        "worst_hour": worst_hour,
        "temp_max": round(t_hi["_temp"], 1),
        "temp_max_time": t_hi["_t"].strftime("%H:%M"),
        "hum_min": round(min(r["_hum"] for r in rows)),
        "hum_max": round(max(r["_hum"] for r in rows)),
        "window_opens": [t.strftime("%H:%M") for t in window_openings(rows)],
    }
    if "soon" in rows[0]:  # device logs only
        d["warnings"] = sum(1 for i, r in enumerate(rows) if r["soon"] == "1" and (i == 0 or rows[i - 1]["soon"] != "1"))
        d["feedback_bad"] = sum(1 for r in rows if r.get("fb") == "bad")
        d["feedback_fine"] = sum(1 for r in rows if r.get("fb") == "fine")
    return d


def compute(path, lim=None, last_days=7):
    lim = dict(LIMITS, **(lim or {}))
    rows, simulated = load(path)
    by_day = defaultdict(list)
    for r in rows:
        by_day[r["_t"].date()].append(r)
    days = [day_stats(by_day[k], lim) for k in sorted(by_day) if len(by_day[k]) >= 1200]  # skip partial days
    week = days[-last_days:]
    hours = Counter()
    for k in sorted(by_day)[-last_days:]:
        for r in by_day[k]:
            if r["_co2"] >= lim["co2"]:
                hours[r["_t"].hour] += 1
    worst2 = max(range(24), key=lambda h: hours[h] + hours[(h + 1) % 24]) if hours else None
    worst_day = max(week, key=lambda d: d["stuffy_min"])
    best_day = min(week, key=lambda d: d["stuffy_min"])
    # Average CO2 by hour of day over the whole log, for the chart.
    by_hour = defaultdict(list)
    for r in rows:
        by_hour[r["_t"].hour].append(r["_co2"])
    return {
        "simulated": simulated,
        "limits": lim,
        "range": [days[0]["date"], days[-1]["date"]],
        "week": {
            "from": week[0]["date"],
            "to": week[-1]["date"],
            "stuffy_hours": round(sum(d["stuffy_min"] for d in week) / 60, 1),
            "stuffy_hours_per_day": round(sum(d["stuffy_min"] for d in week) / 60 / len(week), 1),
            "worst_two_hours": f"{worst2:02d}:00-{(worst2 + 2) % 24:02d}:00" if worst2 is not None else None,
            "worst_day": f"{worst_day['weekday']} {worst_day['date']}",
            "worst_day_stuffy_hours": round(worst_day["stuffy_min"] / 60, 1),
            "best_day": f"{best_day['weekday']} {best_day['date']}",
            "best_day_stuffy_hours": round(best_day["stuffy_min"] / 60, 1),
            "window_opens": sum(len(d["window_opens"]) for d in week),
            "hot_hours": round(sum(d["hot_min"] for d in week) / 60, 1),
            "damp_hours": round(sum(d["damp_min"] for d in week) / 60, 1),
            "dry_hours": round(sum(d["dry_min"] for d in week) / 60, 1),
        },
        "days": days,
        "co2_by_hour": [round(sum(by_hour[h]) / len(by_hour[h])) if by_hour[h] else None for h in range(24)],
    }
