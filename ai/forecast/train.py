"""Trains and evaluates Pip's "stuffy soon" forecast. SIMULATED DATA until real logs exist.

Compares three ways to predict CO2 20 minutes ahead:
  trend   - straight line through the last 10 minutes (no learning)
  linear  - linear regression on the features (learned, tiny)
  mlp     - small neural network 12 -> 16 -> 8 -> 1 (learned)
Ships the simplest one that is clearly better. Writes a report and a C header.

Run: ../../tools/.venv/bin/python train.py --data ../data/sim_room.csv
"""
import argparse
import csv
import json
from datetime import datetime
from pathlib import Path

import numpy as np
from sklearn.linear_model import Ridge
from sklearn.neural_network import MLPRegressor

from features import HISTORY, HORIZON, N_FEATURES, NAMES, features_at, smooth_co2

LIMIT = 1000        # ppm, the stuffy limit (cfg::CO2_STUFFY)
MIN_LEAD = 5        # a warning counts only if it came at least this many minutes early
MATCH_WINDOW = 25   # an alert is "right" if CO2 crosses within this many minutes


def load(path):
    rows = list(csv.DictReader(l for l in open(path) if not l.startswith("#")))
    times = [datetime.strptime(r["time"], "%Y-%m-%d %H:%M") for r in rows]
    co2 = smooth_co2([float(r["co2"]) for r in rows])
    ldr = np.array([float(r["ldr"]) for r in rows])
    temp = np.array([float(r["temp"]) for r in rows])
    hum = np.array([float(r["hum"]) for r in rows])
    return times, co2, ldr, temp, hum


def build(times, co2, ldr, temp, hum, idx):
    X = np.stack([features_at(co2, ldr, temp, hum, t, times[t].hour * 60 + times[t].minute,
                              times[t].weekday() >= 5) for t in idx])
    y = np.array([co2[t + HORIZON] - co2[t] for t in idx], dtype=np.float32)
    return X, y


def crossings(co2, idx_set):
    """Minutes where smoothed CO2 rises through the limit after 15 calm minutes below it."""
    ev = []
    for t in sorted(idx_set):
        if co2[t] >= LIMIT and co2[t - 1] < LIMIT and np.all(co2[t - 15:t] < LIMIT):
            ev.append(t)
    return ev


def alert_metrics(co2, idx, pred_delta):
    alert = (co2[idx] < LIMIT) & (co2[idx] + pred_delta >= LIMIT)
    alert_at = {t for t, a in zip(idx, alert) if a}
    events = crossings(co2, set(idx))
    leads, warned = [], 0
    for e in events:
        lead, t = 0, e - 1
        while t in alert_at and lead < 30:
            lead += 1
            t -= 1
        leads.append(lead)
        warned += lead >= MIN_LEAD
    # Alert episodes: runs of consecutive alert minutes.
    episodes, t_prev = [], None
    for t in sorted(alert_at):
        if t_prev is None or t != t_prev + 1:
            episodes.append(t)
        t_prev = t
    ev = np.array(events)
    right = sum(1 for s in episodes if len(ev) and np.any((ev > s) & (ev <= s + MATCH_WINDOW)))
    return {
        "stuffy_spells": len(events),
        "caught_5min_early_pct": round(100 * warned / max(1, len(events)), 1),
        "median_warning_min": float(np.median(leads)) if leads else 0.0,
        "alerts": len(episodes),
        "alerts_right_pct": round(100 * right / max(1, len(episodes)), 1),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="../data/sim_room.csv")
    ap.add_argument("--test-days", type=int, default=30)
    ap.add_argument("--header", default="../../firmware/lib/pet/forecast_model.h")
    ap.add_argument("--report", default="REPORT.md")
    a = ap.parse_args()

    times, co2, ldr, temp, hum = load(a.data)
    n = len(co2)
    split = n - a.test_days * 1440
    train_idx = np.arange(HISTORY, split - HORIZON)
    test_idx = np.arange(split + HISTORY, n - HORIZON)
    Xtr, ytr = build(times, co2, ldr, temp, hum, train_idx)
    Xte, yte = build(times, co2, ldr, temp, hum, test_idx)

    preds = {"trend": Xte[:, NAMES.index("rise_10m")] * 100.0 * (HORIZON / 10.0)}
    lin = Ridge(alpha=1.0).fit(Xtr, ytr)
    preds["linear"] = lin.predict(Xte)
    mlp = MLPRegressor(hidden_layer_sizes=(16, 8), activation="relu", alpha=1e-3, learning_rate_init=3e-3,
                       max_iter=300, early_stopping=True, random_state=0).fit(Xtr, ytr)
    preds["mlp"] = mlp.predict(Xte)

    near = (co2[test_idx] >= 700) & (co2[test_idx] < LIMIT)  # where a warning matters
    results = {}
    for name, p in preds.items():
        err = np.abs(p - yte)
        results[name] = {"mae_ppm_all": round(float(err.mean()), 1),
                         "mae_ppm_700_1000": round(float(err[near].mean()), 1),
                         **alert_metrics(co2, test_idx, p)}

    # Pick: the simplest model that beats the trend line on both catch rate and accuracy.
    def score(r):
        return r["caught_5min_early_pct"] + r["alerts_right_pct"]
    choice = "trend"
    for cand in ["linear", "mlp"]:
        if score(results[cand]) > score(results[choice]) + 5 and \
                results[cand]["mae_ppm_700_1000"] < results[choice]["mae_ppm_700_1000"]:
            choice = cand

    write_report(a.report, results, choice, n, a.test_days)
    write_header(a.header, choice, lin, mlp, Xte[:8], parity_series(a.data, times))
    print(json.dumps(results, indent=1))
    print("chosen:", choice)


def write_report(path, results, choice, n, test_days):
    lines = [
        "# Forecast report (SIMULATED DATA)",
        "",
        "> All numbers below come from the room simulator, not real measurements. They show the pipeline",
        "> works and which method to ship. Re-run on real logs before claiming anything about real rooms.",
        "",
        f"Data: {n // 1440} simulated days. Trained on the first {n // 1440 - test_days}, tested on the last {test_days} (never seen in training).",
        f"Task: predict CO2 {HORIZON} min ahead. Alert when the prediction crosses {LIMIT} ppm.",
        "",
        "| Method | Error, all (ppm) | Error near the limit (ppm) | Stuffy spells | Caught ≥5 min early | Median warning (min) | Alerts | Alerts that were right |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for k, r in results.items():
        lines.append(f"| {k} | {r['mae_ppm_all']} | {r['mae_ppm_700_1000']} | {r['stuffy_spells']} | "
                     f"{r['caught_5min_early_pct']}% | {r['median_warning_min']} | {r['alerts']} | {r['alerts_right_pct']}% |")
    lines += ["", f"**Shipped: `{choice}`**: the simplest method that clearly beats the straight-line trend."]
    Path(path).write_text("\n".join(lines) + "\n")


def cfloat(v):
    s = f"{v:.7g}"
    return (s if any(ch in s for ch in ".en") else s + ".0") + "f"


def arr(name, a):
    flat = ", ".join(cfloat(v) for v in np.asarray(a, dtype=np.float64).ravel())
    return f"static const float {name}[{np.asarray(a).size}] = {{{flat}}};\n"


def parity_series(path, times):
    """60 raw minutes from the middle of the data, and the features Python computes at the end."""
    rows = list(csv.DictReader(l for l in open(path) if not l.startswith("#")))
    i0 = len(rows) // 2
    seg = rows[i0:i0 + 60]
    raw = np.array([float(r["co2"]) for r in seg])
    ldr = np.array([float(r["ldr"]) for r in seg])
    temp = np.array([float(r["temp"]) for r in seg])
    hum = np.array([float(r["hum"]) for r in seg])
    co2 = smooth_co2(raw)
    ts = times[i0 + 59]
    feats = features_at(co2, ldr, temp, hum, 59, ts.hour * 60 + ts.minute, ts.weekday() >= 5)
    return {"raw": raw, "ldr": ldr, "temp": temp, "hum": hum, "feats": feats,
            "minute": ts.hour * 60 + ts.minute, "weekend": int(ts.weekday() >= 5)}


def write_header(path, choice, lin, mlp, samples, series):
    out = ["// GENERATED by ai/forecast/train.py. Do not edit. Trained on SIMULATED data.\n",
           "#pragma once\n\n",
           f"#define FORECAST_KIND_{choice.upper()} 1\n",
           f"static const int kForecastFeatures = {N_FEATURES};\n"]
    if choice == "linear":
        out += [arr("kLinW", lin.coef_), f"static const float kLinB = {cfloat(float(lin.intercept_))};\n"]
    if choice == "mlp":
        (w1, w2, w3), (b1, b2, b3) = mlp.coefs_, mlp.intercepts_
        out += [arr("kW1", w1), arr("kB1", b1), arr("kW2", w2), arr("kB2", b2), arr("kW3", w3), arr("kB3", b3)]
    # Parity vectors: the C++ code must reproduce these predictions.
    model = {"linear": lin, "mlp": mlp}.get(choice)
    expect = model.predict(samples) if model is not None else samples[:, NAMES.index("rise_10m")] * 200.0
    out += [f"static const int kParityN = {len(samples)};\n", arr("kParityX", samples), arr("kParityY", expect)]
    out += ["// Feature parity: 60 raw minutes in, features at the last minute out.\n",
            arr("kSeriesCo2", series["raw"]), arr("kSeriesLdr", series["ldr"]),
            arr("kSeriesTemp", series["temp"]), arr("kSeriesHum", series["hum"]),
            arr("kSeriesFeats", series["feats"]),
            f"static const int kSeriesMinute = {series['minute']};\n",
            f"static const int kSeriesWeekend = {series['weekend']};\n"]
    Path(path).write_text("".join(out))


if __name__ == "__main__":
    main()
