"""SIMULATED oracle floors on FLEET parkings, with a new row: "learned priors".

Same method as floor_analysis.py (likelihood ratio of "pure decay" against "decay plus a constant
source", threshold = the largest score seen on empty vehicles, so those give zero false alarms by
construction, and a held-out group of empties checks it). Three oracles, all on the same fleet test
parkings (cabin_sim.generate_fleet), so they can sit next to v4 and v5:

  ranges   knows only the assumed ranges (leak 3-6 or 0.3-1.5 per hour, level cap 510 ppm).
  learned  knows exactly what a PERFECT learner could know from many earlier parkings of this vehicle:
           its mean leak for this window state (the day's value still varies +-25%, so it tries
           0.75x to 1.25x of the mean, kept inside the old range), and its sensor offset. It does not
           know the day's outdoor CO2 (420-480, drifting +-10), so the cap on the pure-decay level is
           offset + 480 + 10.
  exact    knows the day's true leak and the day's outdoor CO2 (a real device never does).

All oracles also know whether the window is cracked, as in floor_analysis.py. Seeds 70001 and up are
used only here (and never for tuning). Run:  tools/.venv/bin/python ai/vehicle/floor_v5.py
Writes ai/vehicle/floor_results_v5.json.
"""
import json
import math
import os
from multiprocessing import Pool

import numpy as np

import cabin_sim
import floor_analysis as fa

HERE = os.path.dirname(os.path.abspath(__file__))
STEP = cabin_sim.STEP
SEED_START = 70001
N_CAL, N_HELD = 450, 150
N_INF_CAR, N_INF_VAN = 200, 100
MODES = ("ranges", "learned", "exact")


def curve(args):
    seed, mode = args
    hist, test = cabin_sim.generate_fleet(seed, 0)
    veh = cabin_sim.vehicle(seed)
    m = test["meta"]
    variant = m["variant"]
    i0 = int(math.ceil(m["t_close_s"] / STEP))
    y = test["co2"][i0:].astype(float)
    n = len(y)
    if mode == "exact":
        r1, r0 = fa.fit_rss(y, m["ach_true"], m["outdoor_ppm"] + fa.OFFSET_BOUND)
    else:
        if mode == "ranges":
            lams, cmax = fa.lam_grid(variant), 480.0 + fa.OFFSET_BOUND
        else:
            mean = veh["ach_cracked_mean"] if variant == "cracked" else veh["ach_closed_mean"]
            lo, hi = cabin_sim.LEAK_CRACKED if variant == "cracked" else cabin_sim.LEAK_CLOSED
            lams = np.linspace(max(lo, 0.75 * mean), min(hi, 1.25 * mean), 10)
            cmax = veh["offset"] + 480.0 + cabin_sim.OUTDOOR_DRIFT_PPM
        r1 = np.full(n, np.inf)
        r0 = np.full(n, np.inf)
        for lam in lams:
            a1, a0 = fa.fit_rss(y, lam, cmax)
            r1 = np.minimum(r1, a1)
            r0 = np.minimum(r0, a0)
    return seed, (np.arange(n) * STEP / 60.0), fa.lr_from(r1, r0, n)


def pick():
    groups = {("none", "cracked"): [], ("none", "closed"): [], ("infant", "cracked"): {"car": [], "van": []},
              ("infant", "closed"): {"car": [], "van": []}}
    need_empty = N_CAL + N_HELD
    for seed in range(SEED_START, SEED_START + 40000):
        p = cabin_sim.plan(seed)
        var = "cracked" if p["cracked"] else "closed"
        veh = "van" if p["van"] else "car"
        if p["occupant"] == "none":
            g = groups[("none", var)]
            if len(g) < need_empty:
                g.append(seed)
        elif p["occupant"] == "infant":
            g = groups[("infant", var)][veh]
            if len(g) < (N_INF_VAN if veh == "van" else N_INF_CAR):
                g.append(seed)
    return groups


def main():
    groups = pick()
    allseeds = []
    for k, v in groups.items():
        allseeds += v if isinstance(v, list) else v["car"] + v["van"]
    out = {"label": "SIMULATED", "seeds_from": SEED_START,
           "oracles": {"ranges": "assumed ranges only", "learned": "vehicle's mean leak and offset known exactly, day's outdoor and leak spread unknown",
                       "exact": "true daily leak and true outdoor CO2"},
           "results": {}}
    with Pool() as pool:
        for mode in MODES:
            res = {s: (mi, lr) for s, mi, lr in pool.imap_unordered(curve, [(s, mode) for s in allseeds], chunksize=4)}
            out["results"][mode] = {}
            for variant in ("cracked", "closed"):
                emp = groups[("none", variant)]
                cal, held = emp[:N_CAL], emp[N_CAL:]
                thr = max(float(res[s][1].max()) for s in cal) * 1.0000001
                held_fa = sum(1 for s in held if res[s][1].max() > thr)
                thr_all = max(float(res[s][1].max()) for s in emp) * 1.0000001
                entry = {"threshold_covering_all_empties": round(thr_all, 2),
                         "heldout_empty_cars": len(held), "heldout_false_alarms_at_calibration_threshold": held_fa,
                         "empty_cars_total_zero_false_alarms": len(emp)}
                allinf = []
                for veh in ("car", "van"):
                    inf = groups[("infant", variant)][veh]
                    times = [fa.first_cross(res[s][0], res[s][1], thr_all) for s in inf]
                    entry[f"infant_{veh}_detection"] = fa.summarize(times, len(inf))
                    allinf += times
                entry["infant_all_detection"] = fa.summarize(allinf, len(allinf))
                out["results"][mode][variant] = entry
            print(mode, {v: (e["infant_car_detection"]["within_20_min_pct"], e["infant_van_detection"]["within_20_min_pct"]) for v, e in out["results"][mode].items()}, flush=True)
    with open(os.path.join(HERE, "floor_results_v5.json"), "w") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
