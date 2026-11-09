"""Run the real C++ RoomAir on SIMULATED rooms and score it.

  python evaluate.py --parity            C++ vs Python mirror on 3 rooms
  python evaluate.py --split dev         seeds 1-60, used for tuning, can be repeated
  python evaluate.py --split final       seeds 501-800, run ONCE (refuses to overwrite results_final.json)

Every number is SIMULATED. Nothing here is measured in a real room.
"""
import argparse, json, os, struct, subprocess, sys
import numpy as np
import room_sim as sim
from roomair_ref import RoomAirRef, risk_for

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BIN = os.path.join(ROOT, "build", "roomair_eval")
WARMUP = sim.DAY          # first day is not scored (outdoor and leak are still being learned)
EXHALED = 38000.0
LEAK_DEFAULT = 2.0
COLS = ["valid", "steady", "occupied", "leak_learned", "outdoor", "excess", "reb", "fresh", "low", "mid", "high", "risk", "leak"]


def run_cpp(rooms, tag):
    os.makedirs(os.path.join(ROOT, "build", "room"), exist_ok=True)
    fin = os.path.join(ROOT, "build", "room", f"in_{tag}.bin")
    fout = os.path.join(ROOT, "build", "room", f"out_{tag}.bin")
    with open(fin, "wb") as f:
        for r in rooms:
            co2 = r["co2_meas"].astype("<f4")
            f.write(struct.pack("<ifff", len(co2), r["volume_cfg"], r["g_cfg"], LEAK_DEFAULT))
            f.write(co2.tobytes())
    subprocess.run([BIN, fin, fout], check=True)
    flat = np.fromfile(fout, dtype="<f4").reshape(-1, 13)
    outs, i = [], 0
    for r in rooms:
        n = len(r["co2_meas"])
        outs.append({c: flat[i:i + n, k].astype(float) for k, c in enumerate(COLS)})
        i += n
    os.remove(fin); os.remove(fout)
    return outs


def parity():
    """Everything except the particle filter must match exactly. The filter uses different random numbers in C++ and
    numpy, so it is checked statistically: the two should give the same kind of answer against the truth."""
    rooms = [sim.make_room(k, 1) for k in sim.TYPES]
    outs = run_cpp(rooms, "parity")
    ok = True
    for r, o in zip(rooms, outs):
        ref = RoomAirRef(r["volume_cfg"], r["g_cfg"], EXHALED, LEAK_DEFAULT)
        rows = []
        for v in r["co2_meas"].astype("<f4"):
            ref.add_minute(float(v))
            rows.append(ref.row())
        rows = np.array(rows, dtype=np.float64)
        cpp = np.stack([o[c] for c in COLS], axis=1)
        exact = [0, 1, 2, 3, 4, 5, 6, 11, 12]          # flags, outdoor, excess, rebreathed, risk, leak
        fresh_steady = cpp[:, 1] > 0                    # fresh air is the steady-state formula only while steady
        e1 = np.abs(rows[:, exact] - cpp[:, exact]) / (1 + np.abs(rows[:, exact]))
        e2 = np.abs(rows[fresh_steady, 7] - cpp[fresh_steady, 7]) / (1 + np.abs(rows[fresh_steady, 7]))
        n = r["people"]; m = (n >= 1) & (np.arange(len(n)) >= WARMUP)
        err_c = np.median(np.abs(cpp[m, 9] - n[m])); err_p = np.median(np.abs(rows[m, 9] - n[m]))
        cov_c = np.mean((n[m] >= cpp[m, 8] - .5) & (n[m] <= cpp[m, 10] + .5)); cov_p = np.mean((n[m] >= rows[m, 8] - .5) & (n[m] <= rows[m, 10] + .5))
        gap = np.median(np.abs(cpp[m, 9] - rows[m, 9]))
        tol = 0.25 * max(np.median(n[m]), 1)
        good = e1.max() < 1e-3 and e2.max() < 1e-3 and abs(err_c - err_p) <= tol and abs(cov_c - cov_p) <= 0.15
        ok &= bool(good)
        print(f"{r['kind']:10s} exact cols worst diff {e1.max():.1e}, steady fresh air {e2.max():.1e} | filter: median abs people error C++ {err_c:.2f} vs py {err_p:.2f}, "
              f"coverage {cov_c:.2f} vs {cov_p:.2f}, median |mid diff| {gap:.2f} -> {'ok' if good else 'DIFFERENT'}")
    print("PARITY OK" if ok else "PARITY DIFFERENT")
    return 0 if ok else 1


def lam_bin(l):
    return np.where(l < 1.5, "lam<1.5", np.where(l < 4.0, "lam1.5-4", "lam>4"))


def acc_room(room, o, store):
    t = np.arange(len(room["co2_meas"]))
    sc = t >= WARMUP
    n = room["people"]; lam = room["lam"]; kind = room["kind"]
    # truth-based steady: true CO2 within 10 percent of its steady value for the current people and air change rate
    css_ex = n * room["g_true"] * 3600e6 / (lam * room["volume"] * 1000.0)
    ex_true = room["co2_true"] - room["outdoor"]
    truth_steady = (n > 0) & (np.abs(ex_true - css_ex) <= 0.1 * css_ex)
    occ_truth = (n >= 1) & sc
    dev_steady = (o["steady"] > 0) & (o["occupied"] > 0)
    # fresh air
    m = dev_steady & occ_truth
    err = np.abs(o["fresh"] - room["true_fresh"]) / room["true_fresh"] * 100
    S = store.setdefault(kind, {})
    def add(key, arr):
        S.setdefault(key, []).append(np.asarray(arr))
    ma = occ_truth & (o["fresh"] > 0)
    add("fa_all_err", err[ma]); add("fa_all_n", np.array([ma.sum()]))
    add("fa_err", err[m]); add("fa_lam", lam[m]); add("fa_truth_steady", truth_steady[m])
    add("occ_minutes", np.array([occ_truth.sum()])); add("occ_dev_steady", np.array([(dev_steady & occ_truth).sum()]))
    add("occ_truth_steady", np.array([(truth_steady & sc).sum()]))
    add("truth_steady_dev_steady", np.array([(truth_steady & dev_steady & sc).sum()]))
    # headcount (scored where the truth has people)
    mid, lo, hi = o["mid"], o["low"], o["high"]
    add("hc_abs", np.abs(mid - n)[occ_truth]); add("hc_rel", (np.abs(mid - n) / np.maximum(n, 1) * 100)[occ_truth])
    add("hc_in", ((n >= lo - 0.5) & (n <= hi + 0.5))[occ_truth]); add("hc_width", (hi - lo)[occ_truth])
    add("hc_lam", lam[occ_truth]); add("hc_learned", o["leak_learned"][occ_truth]); add("hc_n", n[occ_truth])
    # empty rooms: truth empty for the last 30 minutes
    empty_run = np.convolve((n > 0).astype(float), np.ones(30), mode="full")[:len(n)] == 0
    em = empty_run & sc
    add("empty_mid", mid[em]); add("empty_dev_occ", o["occupied"][em] > 0); add("empty_mid_if_occ", mid[em & (o["occupied"] > 0)])
    # headcount while the leak is still the default (includes day 1)
    dm = (n >= 1) & (o["leak_learned"] == 0)
    add("hcd_abs", np.abs(mid - n)[dm]); add("hcd_in", ((n >= lo - 0.5) & (n <= hi + 0.5))[dm]); add("hcd_width", (hi - lo)[dm])
    # risk
    risk_true = np.array([risk_for(max(0.0, x) / EXHALED * 100) for x in ex_true[occ_truth]])
    rd = o["risk"][occ_truth]
    add("risk_exact", rd == risk_true); add("risk_within1", np.abs(rd - risk_true) <= 1)
    add("risk_true", risk_true); add("risk_dev", rd)


def cat(S, key):
    return np.concatenate(S[key]) if len(S[key]) else np.array([])


def pct(x, q=50):
    return None if len(x) == 0 else round(float(np.percentile(x, q)), 2)


def summarize(S):
    out = {}
    lam = cat(S, "fa_lam"); err = cat(S, "fa_err"); ts = cat(S, "fa_truth_steady").astype(bool)
    occ = int(cat(S, "occ_minutes").sum())
    out["fresh_air"] = {
        "minutes_scored_device_steady": int(len(err)),
        "median_abs_pct_error_device_steady": pct(err),
        "p90_abs_pct_error_device_steady": pct(err, 90),
        "median_abs_pct_error_all_occupied_minutes": pct(cat(S, "fa_all_err")),
        "minutes_all_occupied_with_an_estimate": int(cat(S, "fa_all_n").sum()),
        "median_abs_pct_error_device_and_truth_steady": pct(err[ts]),
        "by_true_air_change_rate": {b: {"minutes": int((lam_bin(lam) == b).sum()), "median_abs_pct_error": pct(err[lam_bin(lam) == b])}
                                    for b in ["lam<1.5", "lam1.5-4", "lam>4"]},
    }
    dsteady = int(cat(S, "occ_dev_steady").sum()); tst = int(cat(S, "occ_truth_steady").sum())
    both = int(cat(S, "truth_steady_dev_steady").sum())
    out["steady_flag"] = {
        "occupied_minutes": occ,
        "pct_of_occupied_minutes_flagged_steady": round(100 * dsteady / max(occ, 1), 1),
        "pct_of_flagged_steady_that_are_truly_near_steady": round(100 * float(ts.mean()), 1) if len(ts) else None,
        "pct_of_truly_near_steady_minutes_flagged_steady": round(100 * both / max(tst, 1), 1),
    }
    hl = cat(S, "hc_lam"); ha = cat(S, "hc_abs"); hr = cat(S, "hc_rel"); hi_ = cat(S, "hc_in").astype(bool)
    hw = cat(S, "hc_width"); hlr = cat(S, "hc_learned").astype(bool); hn = cat(S, "hc_n")
    def hc(mask):
        return {"minutes": int(mask.sum()), "median_abs_error_people": pct(ha[mask]), "median_abs_pct_error": pct(hr[mask]),
                "pct_truth_inside_range": round(100 * float(hi_[mask].mean()), 1) if mask.any() else None,
                "median_range_width": pct(hw[mask]), "median_true_people": pct(hn[mask])}
    out["headcount"] = {"all_occupied_minutes": hc(np.ones(len(ha), bool)),
                        "leak_learned_yet": hc(hlr), "leak_still_default": hc(~hlr),
                        "by_true_air_change_rate": {b: hc(lam_bin(hl) == b) for b in ["lam<1.5", "lam1.5-4", "lam>4"]}}
    em = cat(S, "empty_mid"); eo = cat(S, "empty_dev_occ").astype(bool); emo = cat(S, "empty_mid_if_occ")
    out["empty_room"] = {"minutes_truly_empty_30min": int(len(em)), "mean_headcount_mid": round(float(em.mean()), 3),
                         "pct_minutes_mid_at_least_1": round(100 * float((em >= 1).mean()), 2),
                         "pct_minutes_device_says_occupied": round(100 * float(eo.mean()), 2),
                         "mean_headcount_mid_when_device_says_occupied": round(float(emo.mean()), 2) if len(emo) else None}
    da = cat(S, "hcd_abs")
    out["headcount_before_any_leak_learned"] = {"minutes": int(len(da)), "median_abs_error_people": pct(da),
        "pct_truth_inside_range": round(100 * float(cat(S, "hcd_in").mean()), 1) if len(da) else None,
        "median_range_width": pct(cat(S, "hcd_width"))}
    out["risk"] = {"minutes": int(len(cat(S, "risk_exact"))),
                   "pct_exact_agreement": round(100 * float(cat(S, "risk_exact").mean()), 1),
                   "pct_within_one_level": round(100 * float(cat(S, "risk_within1").mean()), 1),
                   "truth_level_share_pct": {str(k): round(100 * float((cat(S, "risk_true") == k).mean()), 1) for k in range(4)}}
    return out


def evaluate(seeds, name):
    store = {}
    for s in seeds:
        rooms = [sim.make_room(k, s) for k in sim.TYPES]
        outs = run_cpp(rooms, f"{name}_{s}")
        for r, o in zip(rooms, outs):
            acc_room(r, o, store)
    res = {"label": "ALL NUMBERS SIMULATED", "split": name, "seeds": [int(seeds[0]), int(seeds[-1])], "rooms_per_type": len(seeds),
           "days_per_room": sim.DAYS, "scored_from_day": 2, "by_room_type": {k: summarize(store[k]) for k in sim.TYPES}}
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--parity", action="store_true")
    ap.add_argument("--split", choices=["dev", "final", "final_v2"])
    a = ap.parse_args()
    if not os.path.exists(BIN):
        sys.exit("build first: tools/roomair/run.sh")
    if a.parity:
        sys.exit(parity())
    if not a.split:
        ap.error("pick --parity or --split")
    path = os.path.join(HERE, f"results_{a.split}.json")
    if a.split != "dev" and os.path.exists(path):
        sys.exit(f"{os.path.basename(path)} exists. The final seeds are run ONCE. Not overwriting.")
    seeds = {"dev": range(1, 61), "final": range(501, 801), "final_v2": range(1501, 1801)}[a.split]
    seeds = list(seeds)
    res = evaluate(seeds, a.split)
    json.dump(res, open(path, "w"), indent=2)
    print("wrote", path)
    for k, v in res["by_room_type"].items():
        print(k, "fresh-air median err %:", v["fresh_air"]["median_abs_pct_error_device_steady"],
              "| headcount med abs err:", v["headcount"]["all_occupied_minutes"]["median_abs_error_people"],
              "in-range %:", v["headcount"]["all_occupied_minutes"]["pct_truth_inside_range"],
              "| steady %:", v["steady_flag"]["pct_of_occupied_minutes_flagged_steady"],
              "| risk exact %:", v["risk"]["pct_exact_agreement"])


if __name__ == "__main__":
    main()
