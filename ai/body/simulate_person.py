"""SIMULATED people who press Pip's head when a headache starts, for testing the pattern report.

The room air comes from ../sim/simulate.py (Bengaluru, Oct-Dec). Outdoor air pressure is
invented in a realistic shape: about 912 hPa at 920 m, the normal twice-daily swing of about
1.5 hPa, and a weather dip of 3-8 hPa every week or two. Nothing here is real data.

Each person has a hidden rule (the "truth") that the report must find, or must not invent.
"Common" rules are met in 13-23% of waking hours in this simulated room; "rare" ones in 2-3%.
  pressure        : attacks 3x more likely after pressure fell 2+ hPa over the 3-24 h before (common)
  stale           : attacks 3x more likely after 15+ min above 1,400 ppm CO2 in the 3-24 h before (common)
  pressure_rare   : attacks 4x more likely after a 4+ hPa fall (rare)
  stale_rare      : attacks 3x more likely after 2+ hours above 1,400 ppm (rare)
  none            : attacks at random (the report must find nothing)
  early_sign      : attacks at random, but the person dims the lights 30-90 min before pressing,
                    because light already hurts (a warning sign, not a cause)
"""
import math
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sim"))
from simulate import simulate  # noqa: E402

# name: (factor, threshold, risk multiplier)
RULES = {
    "pressure": ("drop", -2.0, 3.0),
    "stale": ("stale", 0.25, 3.0),
    "pressure_rare": ("drop", -4.0, 4.0),
    "stale_rare": ("stale", 2.0, 3.0),
    "none": (None, 0, 1.0),
    "early_sign": (None, 0, 1.0),
}
PERSONAS = tuple(RULES)
ATTACKS_PER_MONTH = 6.0        # an episodic migraine pattern; varies a lot between people
AWAKE = (7, 23)                # a press only happens while awake


def outdoor_pressure(n_minutes, rng):
    t = np.arange(n_minutes) / 60.0                       # hours
    tide = 1.5 * np.cos(2 * np.pi * (t - 10) / 12.0)       # peaks near 10:00 and 22:00
    dips = np.zeros(n_minutes)
    h = rng.uniform(48, 200)
    while h < t[-1]:
        depth, width = rng.uniform(3, 8), rng.uniform(18, 40)
        dips -= depth * np.exp(-0.5 * ((t - h) / width) ** 2)
        h += rng.uniform(150, 360)                         # a dip every 6-15 days
    drift = np.cumsum(rng.normal(0, 0.01, n_minutes))
    return 912.0 + tide + dips + drift - drift.mean()


def make_world(seed, days=90, seasons=1):
    """`seasons` > 1 joins separate Oct-Dec seasons (2026, 2027, ...) to stand in for longer use.
    The room simulator only has Bengaluru climate data for Oct-Dec."""
    parts = []
    for k in range(seasons):
        rows = simulate(days, seed + 7919 * k, datetime(2026 + k, 10, 1))
        df = pd.DataFrame(rows)[["time", "co2", "temp", "hum", "ldr"]]
        df["time"] = pd.to_datetime(df["time"])
        df["press"] = np.round(outdoor_pressure(len(df), np.random.default_rng(seed + 1000 + 7919 * k)), 1)
        parts.append(df)
    return pd.concat(parts, ignore_index=True)


def window_values(df):
    """Per minute: pressure change and stale hours over the 3-24 h before (the cause window)."""
    co2 = df["co2"].to_numpy()
    press = df["press"].to_numpy()
    stale = np.concatenate([[0], np.cumsum(co2 > 1400)])
    n = len(df)
    i = np.arange(n)
    a, b = np.clip(i - 24 * 60, 0, n), np.clip(i - 3 * 60, 0, n)
    stale_h = (stale[b] - stale[a]) / 60.0
    drop = press[np.clip(b - 1, 0, n - 1)] - press[a]
    return stale_h, drop


def make_marks(df, persona, seed, rate=ATTACKS_PER_MONTH):
    """Returns (marks, df). For early_sign, the lights in df are dimmed before each press."""
    rng = np.random.default_rng(seed * 7 + PERSONAS.index(persona))
    stale_h, drop = window_values(df)
    base = rate / (30 * 24 * 60)
    mult = np.ones(len(df))
    factor, threshold, risk = RULES[persona]
    if factor == "drop":
        mult[drop <= threshold] = risk
    elif factor == "stale":
        mult[stale_h >= threshold] = risk
    hours = df["time"].dt.hour.to_numpy()
    awake = (hours >= AWAKE[0]) & (hours < AWAKE[1])
    # Fewer awake minutes than all minutes: scale so the overall rate stays `rate`.
    p = base * mult * awake * (len(df) / awake.sum())
    p[: 24 * 60] = 0                                      # no history yet on day one
    marks, last = [], -10 ** 9
    df = df.copy()
    for i in np.flatnonzero(rng.random(len(df)) < p):
        if i - last < 24 * 60:                            # one attack a day at most
            continue
        last = i
        marks.append(df["time"].iloc[i])
        if persona == "early_sign":
            dim = int(rng.uniform(30, 90))
            lo = max(0, i - dim)
            df.loc[df.index[lo:i], "ldr"] = np.maximum(df["ldr"].iloc[lo:i], 3700)  # curtains drawn
    return marks, df


if __name__ == "__main__":
    w = make_world(1, 30)
    for persona in PERSONAS:
        m, _ = make_marks(w, persona, 1)
        print(persona, len(m), [str(x) for x in m[:3]])
    print("pressure range", w["press"].min(), w["press"].max(), "co2 max", w["co2"].max(),
          "math check", math.isfinite(w["press"].mean()))
