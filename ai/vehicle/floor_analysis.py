"""SIMULATED physical-floor analysis for CO2-only left-behind detection.

Question: is the ~47 min median for an infant behind a cracked window a weak algorithm, or close to
the limit of CO2-only sensing with this sensor?

Method: an ORACLE detector gets information a real device never has: the true air-change rate (ach)
and the true outdoor CO2. After the doors close it tests, on the sensor curve, two models:

  H0 "pure decay":   cabin CO2 = outdoor + a*exp(-ach*t)           with a >= 0
  H1 "decay+source": cabin CO2 = outdoor + S + (a-S)*exp(-ach*t)   with any a, S  (S = G/(ach*V))

Both pass through the known 60 s sensor lag (so the fit is exact for the lag), plus a free sensor
start state. The sensor offset is unknown but bounded (+-30 ppm), so under H0 the asymptote must
sit within 30 ppm of outdoor. Statistic: likelihood ratio (RSS0 - RSS1)/sigma^2 from exact
constrained least squares, recomputed on every 5 s reading from 5 min after the doors close.
Alarm = first time the statistic passes a threshold. The threshold is the largest value seen on
>=400 EMPTY cars of the same variant, so those cars give ZERO false alarms by construction. A second,
untouched group of empty cars checks it.

Cars here include the hard empty cases (4 adults, long drive, brief door opening, high CO2 at
ignition off). Seeds 9001+ (never used for tuning or the final sets 1001-1600, 2001-2600).

Run:  tools/.venv/bin/python ai/vehicle/floor_analysis.py
"""
import json
import math
import os
from multiprocessing import Pool

import numpy as np

import cabin_sim

HERE = os.path.dirname(os.path.abspath(__file__))
STEP = cabin_sim.STEP
SENSOR_TAU = 60.0
ALPHA = 1.0 - math.exp(-STEP / SENSOR_TAU)
RHO = 1.0 - ALPHA
OFFSET_BOUND = 30.0
MIN_SAMPLES = 60  # 5 min after the doors close
N_CAL = 450  # empty cars per variant used to set the threshold
N_INFANT = 300
SEED_START, SEED_END = 9001, 16000


def classify(seed):
    p = cabin_sim.plan(seed)
    return p["occupant"], ("cracked" if p["cracked"] else "closed")


def fit_rss(y, lam_per_h, cmax):
    """Exact constrained least squares at every prefix. Returns (RSS under H1, RSS under H0)."""
    n = len(y)
    k = np.arange(n)
    drive = np.exp(-lam_per_h / 3600.0 * k * STEP)
    g = np.empty(n)  # sensor response to exp(-ach*t) from zero state
    st = 0.0
    for j in range(n):
        st += ALPHA * (drive[j] - st)
        g[j] = st
    X = np.stack([np.ones(n), RHO ** (k + 1), g], axis=1)  # coefficients: c, y0 term, A
    XtX = np.cumsum(X[:, :, None] * X[:, None, :], axis=0)
    Xty = np.cumsum(X * y[:, None], axis=0)
    yy = np.cumsum(y * y)
    eye = 1e-9 * np.eye(3)

    def rss(beta):
        return yy - 2 * np.einsum("ni,ni->n", beta, Xty) + np.einsum("ni,nij,nj->n", beta, XtX, beta)

    def solve(free, fixed):
        beta = np.zeros((n, 3))
        for idx, v in fixed.items():
            beta[:, idx] = v
        fi = list(free)
        A = XtX[:, fi][:, :, fi] + eye[: len(fi), : len(fi)]
        b = Xty[:, fi].copy()
        for idx, v in fixed.items():
            b -= XtX[:, fi, idx] * v
        beta[:, fi] = np.linalg.solve(A, b[:, :, None])[:, :, 0]
        return beta

    b1 = solve([0, 1, 2], {})
    r1 = rss(b1)
    # H0: c <= cmax (asymptote within the offset bound of outdoor), A >= 0 (decay from above).
    # Enumerate which bounds bind; the best feasible candidate is the constrained optimum.
    cand = np.full(n, np.inf)
    ok = (b1[:, 0] <= cmax) & (b1[:, 2] >= 0)
    cand = np.where(ok, np.minimum(cand, r1), cand)
    b = solve([1, 2], {0: cmax})
    cand = np.where(b[:, 2] >= 0, np.minimum(cand, rss(b)), cand)
    b = solve([0, 1], {2: 0.0})
    cand = np.where(b[:, 0] <= cmax, np.minimum(cand, rss(b)), cand)
    b = solve([1], {0: cmax, 2: 0.0})
    cand = np.minimum(cand, rss(b))
    return r1, cand


def lr_from(r1, r0, n):
    nn = np.arange(n) + 1
    sig2 = np.maximum(r1, 1e-9) / np.maximum(nn - 3, 1)
    lr = np.maximum(r0 - r1, 0.0) / sig2
    lr[:MIN_SAMPLES] = 0.0
    return lr


def lam_grid(variant):
    return np.linspace(3.0, 6.0, 10) if variant == "cracked" else np.linspace(0.3, 1.5, 10)


def oracle_curve(seed, mode="exact"):
    """(minutes since close, LR statistic).
    exact:  knows the true ach and true outdoor CO2 (the oracle asked for).
    ranges: knows only the assumed ach range and outdoor range (420-480). Profiles over ach."""
    s = cabin_sim.generate(seed)
    m = s["meta"]
    i0 = int(math.ceil(m["t_close_s"] / STEP))
    y = s["co2"][i0:].astype(float)
    n = len(y)
    if mode == "exact":
        r1, r0 = fit_rss(y, m["ach_closed"], m["outdoor_ppm"] + OFFSET_BOUND)
    else:
        variant = classify(seed)[1]
        r1 = np.full(n, np.inf)
        r0 = np.full(n, np.inf)
        for lam in lam_grid(variant):
            a1, a0 = fit_rss(y, lam, 480.0 + OFFSET_BOUND)
            r1 = np.minimum(r1, a1)
            r0 = np.minimum(r0, a0)
    return (np.arange(n) * STEP / 60.0), lr_from(r1, r0, n)


def job(args):
    seed, mode = args
    mins, lr = oracle_curve(seed, mode)
    return seed, mins, lr


def first_cross(mins, lr, thr):
    idx = np.nonzero(lr > thr)[0]
    return float(mins[idx[0]]) if len(idx) else None


def summarize(times, n_total):
    """times: minutes to first alarm, or None if never. Percentiles count a miss as infinitely late."""
    arr = np.sort(np.array([t if t is not None else np.inf for t in times]))
    n = len(arr)
    med_idx = (n - 1) // 2
    med = float(arr[med_idx]) if n % 2 else float(arr[n // 2 - 1] if not np.isfinite(arr[n // 2]) else (arr[n // 2 - 1] + arr[n // 2]) / 2)
    p90 = float(arr[int(math.ceil(0.9 * n)) - 1])
    out = {"n": n_total,
           "found_at_all_pct": round(100 * float(np.mean(np.isfinite(arr))), 1),
           "median_min": round(med, 1) if math.isfinite(med) else None,
           "p90_min": round(p90, 1) if math.isfinite(p90) else None}
    for lim in (10, 20, 30):
        out[f"within_{lim}_min_pct"] = round(100 * float(np.mean(arr <= lim)), 1)
    return out


def real_detector(groups):
    """Run the shipped C++ detector on the same seeds, for a like-for-like comparison."""
    import evaluate
    seeds = sorted(s for v in groups.values() for s in v)
    prefix = os.path.join(os.environ.get("FLOOR_TMP", "/private/tmp"), "floor_real")
    metas = {m["seed"]: m for m in cabin_sim.write_csv(seeds, prefix)}
    res = evaluate.run_harness(prefix + ".csv")
    out = {}
    for variant in ("cracked", "closed"):
        inf = groups[("infant", variant)]
        times = []
        for s in inf:
            chk = res[s][0]
            times.append((chk - metas[s]["t_close_s"]) / 60.0 if chk >= 0 else None)
        emp = groups[("none", variant)]
        fa = sum(1 for s in emp if res[s][0] >= 0)
        out[variant] = {"infant_detection": summarize(times, len(inf)), "empty_cars": len(emp), "empty_false_alarms": fa}
    return out


def main():
    groups = {("none", "cracked"): [], ("none", "closed"): [], ("infant", "cracked"): [], ("infant", "closed"): []}
    for seed in range(SEED_START, SEED_END + 1):
        key = classify(seed)
        if key in groups:
            groups[key].append(seed)
    need = {("none", "cracked"): N_CAL + 150, ("none", "closed"): N_CAL + 150,
            ("infant", "cracked"): N_INFANT, ("infant", "closed"): N_INFANT}
    for key in groups:
        groups[key] = groups[key][: need[key]]
        assert len(groups[key]) >= (N_CAL if key[0] == "none" else N_INFANT), (key, len(groups[key]))
    allseeds = [s for v in groups.values() for s in v]
    hard = {s: cabin_sim.generate(s)["meta"]["hard_empty"] for k, v in groups.items() if k[0] == "none" for s in v}

    out = {"label": "SIMULATED", "seeds_scanned": [SEED_START, SEED_END],
           "detector_clock": "minutes after the doors close (same clock as evaluate.py)",
           "min_wait_min": MIN_SAMPLES * STEP / 60.0,
           "oracles": {
               "exact": {"knows": ["true air-change rate", "true outdoor CO2", "sensor lag 60 s"],
                         "does_not_know": ["sensor offset (bounded +-30 ppm)", "CO2 at door close", "noise sd (estimated from fit)"]},
               "ranges": {"knows": ["only the assumed ranges: ach 3-6 (cracked) or 0.3-1.5 (closed), outdoor 420-480", "sensor lag 60 s"],
                          "does_not_know": ["the true ach", "the true outdoor CO2", "sensor offset", "CO2 at door close"]}},
           "results": {}}
    with Pool() as pool:
        for mode in ("exact", "ranges"):
            res = {seed: (mins, lr) for seed, mins, lr in pool.imap_unordered(job, [(s, mode) for s in allseeds], chunksize=4)}
            out["results"][mode] = {}
            for variant in ("cracked", "closed"):
                emp = groups[("none", variant)]
                cal, held = emp[:N_CAL], emp[N_CAL:]
                thr = max(float(res[s][1].max()) for s in cal) * 1.0000001
                held_fa = [s for s in held if res[s][1].max() > thr]
                inf = groups[("infant", variant)]
                thr_all = max(float(res[s][1].max()) for s in emp) * 1.0000001
                times = [first_cross(res[s][0], res[s][1], thr_all) for s in inf]
                entry = {
                    "threshold_set_on_calibration_empties": round(thr, 2),
                    "calibration_empty_cars": len(cal),
                    "calibration_hard_empty": sum(hard[s] for s in cal),
                    "calibration_false_alarms": 0,
                    "heldout_empty_cars": len(held),
                    "heldout_false_alarms_at_calibration_threshold": len(held_fa),
                    "threshold_covering_all_empties": round(thr_all, 2),
                    "empty_cars_total_zero_false_alarms": len(emp),
                    "infant_detection": summarize(times, len(inf)),
                }
                for veh in ("car", "van"):
                    sub = [t for s, t in zip(inf, times) if cabin_sim.plan(s)["van"] == (veh == "van")]
                    entry[f"infant_{veh}_detection"] = summarize(sub, len(sub))
                out["results"][mode][variant] = entry
    out["real_detector_same_seeds"] = real_detector(groups)
    out["real_detector_for_comparison"] = {
        "source": "ai/vehicle/results_final_v2.json and LEFT_BEHIND.md, final_v2 seeds 2001-2600 (different seeds, compare the pattern)",
        "cracked_infant_median_min": 45.9, "cracked_infant_found_pct": 77, "cracked_infant_within_10_min_pct": 10,
        "cracked_infant_within_20_min_pct": 30,
        "closed_car_infant_median_min": 6.4, "closed_car_infant_within_10_min_pct": 93.9}
    out["analytic"] = analytic_examples()
    path = os.path.join(HERE, "floor_results.json")
    with open(path, "w") as f:
        json.dump(out, f, indent=2)
    for mode in out["results"]:
        for v, e in out["results"][mode].items():
            print(mode, v, "thr", e["threshold_covering_all_empties"], "heldout FA", e["heldout_false_alarms_at_calibration_threshold"], e["infant_detection"])


def analytic_examples():
    rows = []
    for name, g_lh, vol, ach in [("infant, small car, window cracked (typical)", 3.5, 3.0, 4.5),
                                 ("infant, small car, wide crack", 3.5, 3.0, 6.0),
                                 ("infant, small car, narrow crack", 2.5, 3.5, 3.0),
                                 ("infant, closed car", 3.5, 3.0, 0.9)]:
        g_ppm_h = g_lh / (vol * 1000.0) * 1e6
        rows.append({"case": name, "G_L_per_h": g_lh, "V_m3": vol, "ach_per_h": ach,
                     "steady_excess_ppm": round(g_ppm_h / ach, 0), "time_constant_min": round(60.0 / ach, 1),
                     "initial_rise_ppm_per_min": round(g_ppm_h / 60.0, 1)})
    return {"steady_excess_formula": "G / (ach * V), G in L/h, V in L, result as ppm after x1e6",
            "time_constant_formula": "1 / ach", "examples": rows}


if __name__ == "__main__":
    main()
