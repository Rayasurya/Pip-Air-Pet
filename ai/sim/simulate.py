"""Simulated room air for Pip: one row per minute, Oct to Dec, Bengaluru.

SIMULATED DATA. Built from published climate averages and indoor-air physics (see
SOURCES.md) plus an invented daily routine. Use it to build and test the AI features
before real readings exist. Retrain on real logs later.

Columns match the device log (see ../LOG_FORMAT.md). Columns starting with `truth_`
are hidden facts the device cannot sense (people, window); only tests may use them.

Run:  ../../tools/.venv/bin/python simulate.py --days 92 --seed 7 --out ../data/sim_room.csv
"""
import argparse
import csv
import math
from dataclasses import dataclass
from datetime import datetime, timedelta

import numpy as np

# ---- Inputs (SOURCES.md) ----
MONTHLY = {  # month: (mean min C, mean max C, rainy days)
    10: (19.8, 28.5, 9.6),
    11: (18.3, 27.4, 4.2),
    12: (16.4, 26.9, 1.3),
}
SUN = {10: (6 + 10 / 60, 18 + 2 / 60), 11: (6 + 20 / 60, 17 + 55 / 60), 12: (6 + 34 / 60, 17 + 56 / 60)}
CO2_OUT = 426 + 20                      # ppm, background + city
ROOM_M3 = 3.5 * 3.2 * 2.9               # 32.5 m3
GEN_AWAKE, GEN_ASLEEP = 0.0050, 0.0035  # L/s of CO2 per person
ACH_SHUT, ACH_DOOR = 0.6, 1.2           # per hour; Indian homes leak more than the sourced 0.16-0.4
ACH_MESH = 1.5                          # window cracked behind a mosquito mesh
PPM_PER_LS_MIN = 60 / (ROOM_M3 * 1000) * 1e6  # ppm rise per minute per 1 L/s


@dataclass
class State:
    co2: float = 600.0
    t_in: float = 24.0
    window: bool = False
    window_until: float = 0.0  # minutes since start
    stuffy_tolerance: float = 1100.0


def outdoor(ts: datetime, rain: bool, rng) -> tuple:
    lo, hi, _ = MONTHLY[ts.month]
    if rain:
        hi -= 3.0
    h = ts.hour + ts.minute / 60
    # Low near 06:00, high near 14:30: a cosine on a 24 h day, skewed by a piecewise phase.
    if 6 <= h <= 14.5:
        k = (h - 6) / 8.5
        frac = 0.5 - 0.5 * math.cos(math.pi * k)
    else:
        hh = h - 14.5 if h > 14.5 else h + 9.5
        frac = 0.5 + 0.5 * math.cos(math.pi * hh / 15.5)
    t_out = lo + (hi - lo) * frac
    rh_out = 87 - 32 * frac + (10 if rain else 0)
    return t_out, min(rh_out, 98)


def daylight(ts: datetime) -> float:
    rise, set_ = SUN[ts.month]
    h = ts.hour + ts.minute / 60
    if h < rise - 0.3 or h > set_ + 0.3:
        return 0.0
    return max(0.0, math.sin(math.pi * (h - rise + 0.3) / (set_ - rise + 0.6)))


def day_plan(day: datetime, rng) -> list:
    """List of (start_hour, end_hour, people, activity) for one day. activity: sleep/work/relax."""
    j = lambda s: rng.normal(0, s)  # noqa: E731
    weekend = day.weekday() >= 5
    wake = 7.5 + j(0.4) + (1.2 if weekend else 0)
    sleep = 24.5 + j(0.5) + (0.5 if weekend else 0)
    plan = [(0, wake, 1, "sleep")]
    if weekend:
        t = wake + 1.5 + j(0.3)
        out_start = 12 + j(1.5)
        plan += [(t, out_start, 1, "relax")]
        if rng.random() < 0.6:  # out in the afternoon
            back = out_start + 3 + abs(j(1.5))
            plan += [(back, sleep - 24 + 24, 1, "relax")]
        else:
            plan += [(out_start + 1, sleep, 1, "relax")]
        if rng.random() < 0.3:  # friends over in the evening
            g = 18 + j(1)
            plan.append((g, g + 2 + abs(j(1)), 3, "relax"))
    else:
        start = wake + 2 + j(0.3)
        lunch = 13 + j(0.3)
        plan += [(start, lunch, 1, "work"), (lunch + 1 + j(0.2), 18.5 + j(0.5), 1, "work")]
        if rng.random() < 0.5:
            plan.append((21 + j(0.4), sleep, 1, "relax"))
        else:
            plan.append((19 + j(0.4), sleep, 1, "relax"))
        if rng.random() < 0.15:  # a long call with a colleague in the room
            c = 15 + j(1)
            plan.append((c, c + 1 + abs(j(0.5)), 2, "work"))
    plan.append((sleep, 24 + wake, 1, "sleep"))  # spills past midnight; clipped below
    return plan


def occupancy(plan: list, h: float) -> tuple:
    people, act = 0, "away"
    for a, b, n, kind in plan:
        if a <= h < b:
            if n >= people:
                people, act = n, kind
    return people, act


def simulate(days: int, seed: int, start: datetime) -> list:
    rng = np.random.default_rng(seed)
    st = State(stuffy_tolerance=rng.normal(1150, 120))
    rows = []
    rain_today = False
    mesh_tonight = False
    plan_today, plan_prev = None, None
    for m in range(days * 24 * 60):
        ts = start + timedelta(minutes=m)
        h = ts.hour + ts.minute / 60
        if ts.hour == 0 and ts.minute == 0 or plan_today is None:
            plan_prev = plan_today
            plan_today = day_plan(ts.replace(hour=0, minute=0), rng)
            rain_today = rng.random() < MONTHLY[ts.month][2] / 30.0
            mesh_tonight = rng.random() < 0.4  # some nights the window stays cracked
            rain_start = rng.uniform(14, 19)       # rain comes as one afternoon/evening spell
            rain_len = rng.uniform(0.5, 3.0)
        people, act = occupancy(plan_today, h)
        if plan_prev and h < 9:  # last night's sleep carries past midnight
            p2, a2 = occupancy([(a - 24, b - 24, n, k) for a, b, n, k in plan_prev], h)
            if p2 > people:
                people, act = p2, a2

        t_out, rh_out = outdoor(ts, rain_today, rng)
        raining_now = rain_today and rain_start <= h <= rain_start + rain_len

        # ---- Window habits ----
        awake_here = people > 0 and act != "sleep"
        if st.window:
            close = m >= st.window_until or raining_now or (18.5 <= h <= 19.5 and rng.random() < 0.05)
            if close:
                st.window = False
        elif awake_here:
            noticed = st.co2 > st.stuffy_tolerance and rng.random() < 0.03
            morning = act == "work" and 9 <= h <= 10 and rng.random() < 0.004
            if (noticed or morning) and not raining_now:
                st.window = True
                st.window_until = m + rng.uniform(10, 50)

        # ---- Air exchange and CO2 ----
        if st.window:
            ach = rng.uniform(3, 10)
        elif act == "sleep" and mesh_tonight:
            ach = ACH_MESH
        elif people == 0:
            ach = ACH_DOOR
        else:
            ach = ACH_SHUT
        gen = people * (GEN_ASLEEP if act == "sleep" else GEN_AWAKE)
        st.co2 += gen * PPM_PER_LS_MIN - (ach / 60.0) * (st.co2 - CO2_OUT)

        # ---- Temperature and humidity ----
        target = t_out + 1.0  # walls hold some warmth
        if act == "work":
            target += 1.0  # laptop
        target += 0.3 * people
        rate = 0.06 if st.window else 0.008
        st.t_in += rate * (target - st.t_in)
        rh_in = rh_out - 2.5 * (st.t_in - t_out) + 2.0 * people + (5 if raining_now else 0)
        rh_in = float(np.clip(rh_in, 30, 92))

        # ---- Light (raw LDR, dark reads high) ----
        light = daylight(ts) * (0.7 if rain_today else 1.0)
        if people > 0 and act != "sleep" and light < 0.3:
            light = max(light, 0.55)  # lamp
        ldr = int(np.clip(4095 * (1 - 0.85 * light) - 200 + rng.normal(0, 40), 0, 4095))

        rows.append({
            "time": ts.strftime("%Y-%m-%d %H:%M"),
            "co2": int(round(st.co2 + rng.normal(0, 10 + 0.01 * st.co2))),
            "temp": round(st.t_in + rng.normal(0, 0.1), 1),
            "hum": round(rh_in + rng.normal(0, 0.5), 1),
            "ldr": ldr,
            "truth_people": people,
            "truth_activity": act,
            "truth_window": int(st.window),
            "truth_outdoor_temp": round(t_out, 1),
        })
    return rows


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", type=int, default=92)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--start", default="2026-10-01")
    ap.add_argument("--out", default="../data/sim_room.csv")
    a = ap.parse_args()
    rows = simulate(a.days, a.seed, datetime.fromisoformat(a.start))
    with open(a.out, "w", newline="") as f:
        f.write("# SIMULATED DATA for Pip (Air Pet). Not real measurements. See ai/sim/SOURCES.md\n")
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    co2 = np.array([r["co2"] for r in rows])
    print(f"{len(rows)} rows -> {a.out}")
    print(f"CO2 ppm: median {np.median(co2):.0f}, 95th pct {np.percentile(co2, 95):.0f}, max {co2.max()}")
    print(f"share of minutes >= 1000 ppm: {(co2 >= 1000).mean():.1%}")


if __name__ == "__main__":
    main()
