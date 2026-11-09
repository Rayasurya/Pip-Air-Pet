"""Forecast features. MUST match firmware/lib/pet/forecast.cpp exactly (a parity test checks it).

Input: per-minute series (CO2 ppm, LDR raw, temp C, humidity %) plus the clock.
The device keeps the last HISTORY minutes and computes the same numbers.
"""
import math

import numpy as np

HISTORY = 31          # minutes of history needed (t-30 .. t)
HORIZON = 20          # predict CO2 this many minutes ahead
N_FEATURES = 12
NAMES = [
    "co2_level", "rise_5m", "rise_10m", "rise_30m", "accel", "tod_sin", "tod_cos",
    "weekend", "light", "light_change", "temp_change", "hum_change",
]


def features_at(co2, ldr, temp, hum, t, minute_of_day, weekend):
    """Features for minute index t. Arrays are per-minute and already smoothed."""
    c = co2
    return np.array([
        (c[t] - 800.0) / 300.0,
        (c[t] - c[t - 5]) / 50.0,
        (c[t] - c[t - 10]) / 100.0,
        (c[t] - c[t - 30]) / 200.0,
        ((c[t] - c[t - 5]) - (c[t - 5] - c[t - 10])) / 30.0,
        math.sin(2 * math.pi * minute_of_day / 1440.0),
        math.cos(2 * math.pi * minute_of_day / 1440.0),
        1.0 if weekend else 0.0,
        (ldr[t] - 2000.0) / 2000.0,
        (ldr[t] - ldr[t - 10]) / 1000.0,
        (temp[t] - temp[t - 10]) / 1.0,
        (hum[t] - hum[t - 10]) / 5.0,
    ], dtype=np.float32)


def smooth_co2(raw):
    """Median of the last 3 minutes, then a light EMA. Uses only past values (the device
    cannot see the future), the same as the device's minute filter."""
    raw = np.asarray(raw, dtype=np.float64)
    med = raw.copy()
    med[2:] = np.median(np.stack([raw[:-2], raw[1:-1], raw[2:]]), axis=0)
    out = np.empty_like(med)
    acc = med[0]
    for i, v in enumerate(med):
        acc = 0.5 * acc + 0.5 * v
        out[i] = acc
    return out
