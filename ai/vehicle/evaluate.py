"""Generate SIMULATED scenarios, run the real C++ LeftBehind on them, and write results.

  Dev seeds 1-200 are for tuning. Run as often as needed:
      tools/.venv/bin/python ai/vehicle/evaluate.py dev
  Final seeds 1001-1600 are run ONCE at the end and never used for tuning. The script refuses to
  overwrite results_final.json unless you pass --force (do not, unless the code is frozen).
      tools/.venv/bin/python ai/vehicle/evaluate.py final

  final_v2 (seeds 2001-2600) is the one-time run for the v2 level rule. final stays as the v1 record.

Detection time is measured from the moment the doors close. All numbers are SIMULATED.
"""
import json
import os
import subprocess
import sys
from collections import defaultdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import cabin_sim  # noqa: E402

SETS = {"dev": (range(1, 201), "results_dev.json"), "final": (range(1001, 1601), "results_final.json"),
        "final_v2": (range(2001, 2601), "results_final_v2.json"),
        "final_v3": (range(3001, 3601), "results_final_v3.json"),
        # 4001-4600 were used as extra tuning seeds for v3, so v4 uses untouched seeds 20001-20600.
        "final_v4": (range(20001, 20601), "results_final_v4.json")}
FALSE_ALARM_WINDOW_S = 60 * 60


def run_harness(csv_path):
    out = subprocess.run([os.path.join(ROOT, "tools/left_behind/run.sh"), csv_path], check=True,
                         capture_output=True, text=True, cwd=ROOT).stdout.strip().splitlines()
    res = {}
    for line in out[1:]:
        sid, chk, urg, spec = (int(x) for x in line.split(","))
        res[sid] = (chk, urg, spec)
    return res


def pct(a, q):
    return round(float(np.percentile(a, q)) / 60.0, 1) if len(a) else None


def summarize(rows):
    """rows: list of dicts with detect_s (None if missed) and urgent_s."""
    n = len(rows)
    det = [r["detect_s"] for r in rows if r["detect_s"] is not None]
    out = {"n": n, "detected": len(det)}
    for m in (5, 10, 20):
        out[f"rate_within_{m}min"] = round(sum(1 for d in det if d <= m * 60) / n, 3) if n else None
    out["median_min"] = pct(det, 50)
    out["p95_min"] = pct(det, 95)
    urg = [r["urgent_s"] for r in rows if r["urgent_s"] is not None]
    out["urgent_count"] = len(urg)
    out["after_cabin_60c"] = sum(1 for r in rows if r["after_60c"])
    return out


def evaluate(name, force=False):
    seeds, fname = SETS[name]
    path = os.path.join(HERE, fname)
    if name.startswith("final") and os.path.exists(path) and not force:
        sys.exit(f"{fname} already exists. The final set is run once. Use --force only if code is frozen.")
    prefix = os.path.join(ROOT, "build", "vehicle", name)
    metas = cabin_sim.write_csv(seeds, prefix)
    res = run_harness(prefix + ".csv")
    rows = []
    for m in metas:
        chk, urg, _ = res[m["seed"]]
        close = m["t_close_s"]
        t60 = m["t_cabin_60c_s"]
        d = chk - close if chk >= 0 else None
        rows.append({**m, "detect_s": d, "urgent_s": (urg - close) if urg >= 0 else None,
                     "after_60c": bool(chk >= 0 and t60 is not None and chk >= t60),
                     "any_check": chk >= 0})
    occupied = [r for r in rows if r["occupant"] != "none"]
    empty = [r for r in rows if r["occupant"] == "none"]
    result = {"label": "SIMULATED", "set": name, "seeds": [seeds[0], seeds[-1]], "scenarios": len(rows),
              "by_occupant": {}, "by_occupant_variant_vehicle": {}, "by_variant": {}, "by_vehicle": {}}
    for occ in ("infant", "toddler", "dog"):
        sel = [r for r in occupied if r["occupant"] == occ]
        result["by_occupant"][occ] = summarize(sel)
        for var in ("closed", "cracked"):
            for veh in ("car", "van"):
                cell = [r for r in sel if r["variant"] == var and r["vehicle"] == veh]
                result["by_occupant_variant_vehicle"][f"{occ}/{var}/{veh}"] = summarize(cell)
        for var in ("closed", "cracked"):
            result["by_variant"][f"{occ}/{var}"] = summarize([r for r in sel if r["variant"] == var])
        for veh in ("car", "van"):
            result["by_vehicle"][f"{occ}/{veh}"] = summarize([r for r in sel if r["vehicle"] == veh])
    result["all_occupied_closed"] = summarize([r for r in occupied if r["variant"] == "closed"])
    result["all_occupied_cracked"] = summarize([r for r in occupied if r["variant"] == "cracked"])
    result["all_occupied_car"] = summarize([r for r in occupied if r["vehicle"] == "car"])
    result["all_occupied_van"] = summarize([r for r in occupied if r["vehicle"] == "van"])

    def fa(sel):
        w = [r for r in sel if r["detect_s"] is not None and r["detect_s"] <= FALSE_ALARM_WINDOW_S]
        return {"n": len(sel), "false_alarms_within_60min": len(w),
                "rate": round(len(w) / len(sel), 4) if sel else None,
                "any_alarm_in_90min_run": sum(1 for r in sel if r["any_check"])}
    result["empty_all"] = fa(empty)
    result["empty_car"] = fa([r for r in empty if r["vehicle"] == "car"])
    result["empty_van"] = fa([r for r in empty if r["vehicle"] == "van"])
    result["empty_hard"] = fa([r for r in empty if r["hard_empty"]])
    result["empty_easy"] = fa([r for r in empty if not r["hard_empty"]])
    result["empty_false_alarm_seeds"] = [r["seed"] for r in empty if r["any_check"]]
    inf_closed_car = [r for r in occupied if r["occupant"] == "infant" and r["variant"] == "closed"
                      and r["vehicle"] == "car"]
    result["targets"] = {
        "empty_false_alarm_under_1pct": (result["empty_all"]["rate"] or 0) < 0.01,
        "infant_closed_car_10min_at_least_95pct": summarize(inf_closed_car)["rate_within_10min"] is not None
        and summarize(inf_closed_car)["rate_within_10min"] >= 0.95,
        "infant_closed_car_10min_rate": summarize(inf_closed_car)["rate_within_10min"],
        "infant_closed_car_n": len(inf_closed_car),
    }
    result["misses_by_cell"] = {k: v["n"] - v["detected"] for k, v in
                                result["by_occupant_variant_vehicle"].items()}
    result["alerts_after_cabin_passed_60c"] = sum(1 for r in occupied if r["after_60c"])
    result["occupied_alerts_total"] = sum(1 for r in occupied if r["any_check"])
    with open(path, "w") as f:
        json.dump(result, f, indent=1)
    return result


def brief(r):
    print(f"{r['set']}: {r['scenarios']} SIMULATED scenarios")
    for k, v in r["by_occupant"].items():
        print(f"  {k:8s} n={v['n']:3d} 5/10/20min {v['rate_within_5min']}/{v['rate_within_10min']}/"
              f"{v['rate_within_20min']} median {v['median_min']} p95 {v['p95_min']}")
    for k in ("all_occupied_closed", "all_occupied_cracked", "all_occupied_car", "all_occupied_van"):
        v = r[k]
        print(f"  {k:22s} n={v['n']:3d} 5/10/20 {v['rate_within_5min']}/{v['rate_within_10min']}/"
              f"{v['rate_within_20min']} median {v['median_min']} p95 {v['p95_min']}")
    print("  empty", r["empty_all"], "hard", r["empty_hard"]["false_alarms_within_60min"], "/", r["empty_hard"]["n"])
    print("  FA seeds", r["empty_false_alarm_seeds"])
    print("  targets", r["targets"])
    print("  misses", {k: v for k, v in r["misses_by_cell"].items() if v})
    print("  alerts after 60C", r["alerts_after_cabin_passed_60c"], "of", r["occupied_alerts_total"])


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "dev"
    if which not in SETS:
        sys.exit("usage: evaluate.py dev|final|final_v2|final_v3|final_v4 [--force]")
    brief(evaluate(which, "--force" in sys.argv))
