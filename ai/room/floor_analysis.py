"""How close is room mode to the physical limit? (SIMULATED rooms, seeds 901-1000, never used for tuning RoomAir.)

Two oracles that see only the sensor series (plus what a device could be told):
  range oracle: knows the room volume and the per-person CO2 rate as configured, a prior on the sensor
                offset around the cheap outdoor estimate, and that the air-change rate is somewhere in 0.3 to 12 per hour.
                It does NOT know the air-change rate. A particle filter over people, air-change rate and
                sensor offset, with the 60 s sensor lag in the model.
  exact oracle: the same filter, but it is told the TRUE air-change rate every minute.
Also scores the real RoomAir (v1) on the same rooms.

  python floor_analysis.py [--seeds 901 1000] [--particles 800]
"""
import argparse, json, os
from multiprocessing import Pool
import numpy as np
import room_sim as sim
import evaluate as E
from roomair_ref import PfRef, RoomAirRef

def pf_run(co2, vol, gen, lam_true=None, npart=800, seed=0, params=None):
    pf = PfRef(vol, gen, None, npart, seed, params)
    N = len(co2)
    out = np.zeros((4, N))
    for t in range(N):
        out[:, t] = pf.step(float(co2[t]), None if lam_true is None else lam_true[t])
    return out


def job(args):
    kind, seed, npart = args
    r = sim.make_room(kind, seed)
    o1 = E.run_cpp([r], f"fa_{kind}_{seed}")[0]
    out = {"v2": o1}
    ref = RoomAirRef(r["volume_cfg"], r["g_cfg"], 38000.0, 2.0)     # v1 from the Python mirror (its leak learning matched the C++ exactly)
    rows = []
    for v in r["co2_meas"].astype("<f4"):
        ref.add_minute(float(v)); rows.append(ref.row(v1=True))
    rows = np.array(rows)
    o_v1 = {c: rows[:, k] for k, c in enumerate(E.COLS)}
    out["v1"] = o_v1
    for name, lt in (("range", None), ("exact", r["lam"])):
        lo, mi, hi, fr = pf_run(r["co2_meas"], r["volume_cfg"], r["g_cfg"], lt, npart, seed * 7 + 1)
        o = dict(o1); o.update(low=lo, mid=mi, high=hi, fresh=fr)
        # fresh air is available whenever the room is occupied for the oracles
        o["fresh"] = np.where(o1["occupied"] > 0, fr, 0.0)
        out[name] = o
    return kind, seed, r, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", nargs=2, type=int, default=[901, 1000])
    ap.add_argument("--particles", type=int, default=2000)
    a = ap.parse_args()
    seeds = range(a.seeds[0], a.seeds[1] + 1)
    jobs = [(k, s, a.particles) for s in seeds for k in sim.TYPES]
    stores = {m: {} for m in ("v1", "v2", "range", "exact")}
    with Pool(9) as p:
        for kind, seed, r, out in p.imap_unordered(job, jobs, chunksize=1):
            for m in stores:
                E.acc_room(r, out[m], stores[m])
    res = {"label": "ALL NUMBERS SIMULATED", "seeds": list(a.seeds), "particles": a.particles}
    for m in stores:
        res[m] = {k: E.summarize(stores[m][k]) for k in sim.TYPES}
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "floor_analysis.json")
    json.dump(res, open(path, "w"), indent=2)
    for m in stores:
        for k in sim.TYPES:
            v = res[m][k]; h = v["headcount"]["all_occupied_minutes"]; f = v["fresh_air"]
            print(f"{m:6s} {k:10s} HC abs err {h['median_abs_error_people']:6.2f} in-range {h['pct_truth_inside_range']:5.1f}% width {h['median_range_width']:6.2f} | FA steady {f['median_abs_pct_error_device_steady']} all {f['median_abs_pct_error_all_occupied_minutes']}")


if __name__ == "__main__":
    main()
