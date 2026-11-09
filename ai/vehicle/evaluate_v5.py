"""SIMULATED fleet evaluation of v4 against v5 ("learns its own vehicle"), using the Python reference.

Each seed is one vehicle. It gets N earlier parkings (mostly empty, about 10% with a dog or child who
was collected in time) and then one TEST parking whose occupant and window state come from
cabin_sim.plan(seed). v4 sees only the test parking. v5 first feeds the earlier parkings to its
learner (exactly as the firmware would: learn when ignition comes back on, only if no alarm).

  tools/.venv/bin/python ai/vehicle/evaluate_v5.py dev            # tuning seeds 40001-40400
  tools/.venv/bin/python ai/vehicle/evaluate_v5.py stress         # empty-only false-alarm check, 41001-
  tools/.venv/bin/python ai/vehicle/evaluate_v5.py final_cpp      # ONCE, seeds 100001-102400, writes results_final_v5.json

Seed ranges used by earlier work (do not reuse): 1-200, 1001-1600, 2001-2600, 3001-3600, 4001-4600,
5001-7000, 7001-13000, 9001-16000, 20001-20600. This file uses 40001-60000 for tuning (dev: infants and
empties picked from it by plan(seed)), 70001+ for the oracle floors (floor_v5.py), and 100001-102400
for the final run, which is made once and never used for tuning. Detection time is
measured from the moment the doors close. All numbers are SIMULATED.
"""
import json
import os
import sys
from multiprocessing import Pool

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "left_behind"))
import cabin_sim  # noqa: E402
import leftbehind_ref as L  # noqa: E402
from evaluate import summarize  # noqa: E402

SETS = {"dev": (range(40001, 40401), "results_dev_v5.json"),
        "stress": (range(200001, 226001), None),
        "final": (range(100001, 102401), "results_final_v5.json")}
FALSE_ALARM_WINDOW_S = 60 * 60

# name -> (history parkings used, config overrides). "v5" with 0 history must equal v4.
VARIANTS = {"v4": (0, {"learnOn": False}), "v5_h0": (0, {}), "v5_h3": (3, {}), "v5_h10": (10, {})}


def _cfg(meta, overrides):
    return L.Config(cabinVolumeM3=8.0 if meta["vehicle"] == "van" else 3.0, **overrides)


def run_seed(args):
    seed, variants, empty_only = args
    nh = max(n for n, _ in variants.values())
    hist, test = cabin_sim.generate_fleet(seed, nh)
    if empty_only and test["meta"]["occupant"] != "none":
        return None
    close = test["meta"]["t_close_s"]
    out = {}
    for name, (n, over) in variants.items():
        lb = L.LeftBehind(_cfg(test["meta"], over))
        h_alarms = 0
        for p in (hist[len(hist) - n:] if n else []):
            if L.run_parking(lb, p) >= 0:
                h_alarms += 1
        first = L.run_parking(lb, test)
        out[name] = {"detect_s": (first - close) if first >= 0 else None, "any_check": first >= 0,
                     "history_alarms": h_alarms,
                     "learned": [len(lb.learned.base), len(lb.learned.closed), len(lb.learned.cracked)],
                     "history_occupied_learned_risk": sum(1 for p in hist[len(hist) - n:] if p["meta"]["occupant"] != "none") if n else 0}
    m = test["meta"]
    return {"seed": seed, "occupant": m["occupant"], "variant": m["variant"], "vehicle": m["vehicle"],
            "hard_empty": m["hard_empty"], "t_close_s": close, "t_cabin_60c_s": m["t_cabin_60c_s"],
            "results": out}


# ---- fast engine: the real C++ detector (tools/left_behind/fleet_eval.cpp) -------------------------
import struct  # noqa: E402
import subprocess  # noqa: E402

CPP_LIB = os.environ.get("LB_LIB", os.path.join(ROOT, "firmware", "lib", "pet"))
CPP_EXE = os.path.join(ROOT, "build", "fleet_eval" + os.environ.get("LB_EXE_SUFFIX", ""))
CPP_VARIANTS = "off:0,on:0,on:3,on:10"
CPP_NAMES = {"off:0": "v4", "on:0": "v5_h0", "on:3": "v5_h3", "on:10": "v5_h10"}


def build_cpp():
    if os.environ.get("LB_SKIP_BUILD"):
        return
    os.makedirs(os.path.dirname(CPP_EXE), exist_ok=True)
    subprocess.run(["clang++", "-std=c++17", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-I" + CPP_LIB,
                    os.path.join(ROOT, "tools", "left_behind", "fleet_eval.cpp"),
                    os.path.join(CPP_LIB, "leftbehind.cpp"), "-o", CPP_EXE], check=True)


def pack_fleet(seed, parks, vol):
    b = bytearray(struct.pack("<ifi", seed, vol, len(parks)))
    for p in parks:
        n = len(p["t"])
        b += struct.pack("<i", n)
        b += p["t"].astype("<i4").tobytes() + p["ign"].astype("u1").tobytes()
        b += p["co2"].astype("<i2").tobytes() + np.round(p["temp"] * 100).astype("<i2").tobytes()
    return bytes(b)


CACHE = os.path.join(ROOT, "build", "fleet_cache")


def _fleet_blob(seed):
    """Packed fleet (10 earlier parkings + test parking) and what the metrics need. Cached on disk: the
    simulation is the slow part, the detector is fast."""
    import pickle
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"{seed}.pkl")
    if os.path.exists(path):
        with open(path, "rb") as f:
            return pickle.load(f)
    hist, test = cabin_sim.generate_fleet(seed, 10)
    vol = 8.0 if test["meta"]["vehicle"] == "van" else 3.0
    item = (pack_fleet(seed, hist + [test], vol), [p["meta"]["occupant"] for p in hist], test["meta"])
    with open(path + ".tmp", "wb") as f:
        pickle.dump(item, f)
    os.replace(path + ".tmp", path)
    return item


def run_seed_cpp(args):
    chunk, variants_spec, empty_only, env = args
    blob = bytearray()
    metas = {}
    for seed in chunk:
        b, hist_occ, tmeta = _fleet_blob(seed)
        if empty_only and tmeta["occupant"] != "none":
            continue
        blob += b
        metas[seed] = (hist_occ, tmeta)
    if not blob:
        return []
    e = dict(os.environ)
    e.update({f"LB_{k}": str(v) for k, v in env.items()})
    out = subprocess.run([CPP_EXE, variants_spec], input=bytes(blob), capture_output=True, check=True, env=e).stdout.decode().splitlines()
    by = {}
    for line in out:
        sid, var, first, mask, nb, nc, nk = line.split(",")
        by.setdefault(int(sid), {})[var] = (int(first), int(mask), [int(nb), int(nc), int(nk)])
    rows = []
    for seed, (hist, m) in metas.items():
        hist = [{"meta": {"occupant": o}} for o in hist]
        close = m["t_close_s"]
        res = {}
        for var, (first, mask, learned) in by[seed].items():
            n = int(var.split(":")[1])
            h_idx = range(len(hist) - n, len(hist)) if var.startswith("on") or n else range(0)
            # alarms in history parkings that were truly empty (a false alarm there is a defect)
            bad = sum(1 for k in h_idx if (mask >> k) & 1 and hist[k]["meta"]["occupant"] == "none")
            ok = sum(1 for k in h_idx if (mask >> k) & 1 and hist[k]["meta"]["occupant"] != "none")
            res[CPP_NAMES.get(var, var)] = {"detect_s": (first - close) if first >= 0 else None, "any_check": first >= 0,
                                           "history_alarms": bad + ok, "history_false_alarms": bad, "learned": learned}
        rows.append({"seed": seed, "occupant": m["occupant"], "variant": m["variant"], "vehicle": m["vehicle"],
                     "hard_empty": m["hard_empty"], "t_close_s": close, "t_cabin_60c_s": m["t_cabin_60c_s"], "results": res})
    return rows


def evaluate_cpp(seeds, variants_spec=CPP_VARIANTS, empty_only=False, env=None, per_chunk=8):
    seeds = list(seeds)
    if empty_only:
        seeds = [s for s in seeds if cabin_sim.plan(s)["occupant"] == "none"]
    chunks = [seeds[i:i + per_chunk] for i in range(0, len(seeds), per_chunk)]
    with Pool() as pool:
        out = pool.map(run_seed_cpp, [(c, variants_spec, empty_only, env or {}) for c in chunks])
    rows = [r for part in out for r in part]
    rows.sort(key=lambda r: r["seed"])
    return rows


def cell(rows, name):
    sel = [{"detect_s": r["results"][name]["detect_s"], "urgent_s": None, "after_60c": False} for r in rows]
    s = summarize(sel)
    s.pop("urgent_count", None)
    s.pop("after_cabin_60c", None)
    det = [x["detect_s"] for x in sel if x["detect_s"] is not None]
    s["rate_within_30min"] = round(sum(1 for d in det if d <= 1800) / len(sel), 3) if sel else None
    return s


def report(rows, names):
    res = {"label": "SIMULATED", "scenarios": len(rows), "by_cell": {}, "empty": {}}
    occ = [r for r in rows if r["occupant"] != "none"]
    emp = [r for r in rows if r["occupant"] == "none"]
    for o in ("infant", "toddler", "dog"):
        for var in ("closed", "cracked"):
            for veh in ("car", "van"):
                sel = [r for r in occ if r["occupant"] == o and r["variant"] == var and r["vehicle"] == veh]
                res["by_cell"][f"{o}/{var}/{veh}"] = {nm: cell(sel, nm) for nm in names}
    for key, sel in (("all", emp), ("car", [r for r in emp if r["vehicle"] == "car"]),
                     ("van", [r for r in emp if r["vehicle"] == "van"]),
                     ("hard", [r for r in emp if r["hard_empty"]])):
        res["empty"][key] = {nm: {"n": len(sel),
                                  "false_alarms_within_60min": sum(1 for r in sel if r["results"][nm]["detect_s"] is not None
                                                                   and r["results"][nm]["detect_s"] <= FALSE_ALARM_WINDOW_S),
                                  "any_alarm_in_90min_run": sum(1 for r in sel if r["results"][nm]["any_check"])}
                             for nm in names}
    res["empty_false_alarm_seeds"] = {nm: [r["seed"] for r in emp if r["results"][nm]["any_check"]] for nm in names}
    # History: how often an empty-vehicle history parking raised an alarm itself, and poisoning exposure.
    res["history"] = {nm: {"parkings_with_alarm_total": sum(r["results"][nm]["history_alarms"] for r in rows),
                           "avg_learned_base": round(float(np.mean([r["results"][nm]["learned"][0] for r in rows])), 2),
                           "avg_learned_closed": round(float(np.mean([r["results"][nm]["learned"][1] for r in rows])), 2),
                           "avg_learned_cracked": round(float(np.mean([r["results"][nm]["learned"][2] for r in rows])), 2)}
                      for nm in names}
    return res


def show(res, names):
    print(f"{res['scenarios']} SIMULATED fleets")
    for k, v in res["by_cell"].items():
        if not k.startswith("infant") and not os.environ.get("ALL"):
            continue
        for nm in names:
            c = v[nm]
            print(f"  {k:22s} {nm:7s} n={c['n']:3d} 10/20/30min {c['rate_within_10min']}/{c['rate_within_20min']}/"
                  f"{c['rate_within_30min']} median {c['median_min']} found {c['detected']}")
    for k, v in res["empty"].items():
        print("  empty", k, {nm: (x["false_alarms_within_60min"], x["any_alarm_in_90min_run"], x["n"]) for nm, x in v.items()})
    print("  FA seeds", res["empty_false_alarm_seeds"])
    print("  history", res["history"])


def evaluate(which, variants=None, empty_only=False, seeds=None):
    variants = variants or VARIANTS
    rng_seeds, fname = SETS[which]
    seeds = list(seeds or rng_seeds)
    with Pool() as pool:
        rows = [r for r in pool.imap_unordered(run_seed, [(s, variants, empty_only) for s in seeds], chunksize=2)
                if r is not None]
    rows.sort(key=lambda r: r["seed"])
    return rows


def sweep(configs, n_inf=300, n_closed=150, n_empty=3000, hist="on:10"):
    """Tuning helper (dev seeds only: 40001-49000). Runs the real C++ engine once per config (env overrides)."""
    plan = cabin_sim.plan
    inf_c = [s for s in range(40001, 49000) if plan(s)["occupant"] == "infant" and plan(s)["cracked"] and not plan(s)["van"]][:n_inf]
    inf_c += [s for s in range(40001, 90000) if plan(s)["occupant"] == "infant" and plan(s)["cracked"] and plan(s)["van"]][:n_inf // 2]
    inf_k = [s for s in range(40001, 49000) if plan(s)["occupant"] == "infant" and not plan(s)["cracked"]][:n_closed]
    emp = [s for s in range(41001, 60000) if plan(s)["occupant"] == "none"][:n_empty]
    build_cpp()
    v4_fa = set()
    for i, c in enumerate([None] + list(configs)):
        spec = "off:0" if c is None else hist
        rows = evaluate_cpp(inf_c + inf_k + emp, spec, env=c or {})
        nm = CPP_NAMES.get(spec, spec)
        out = {}
        for tag, sel in (("inf-cracked-car", [r for r in rows if r["occupant"] == "infant" and r["variant"] == "cracked" and r["vehicle"] == "car"]),
                         ("inf-cracked-van", [r for r in rows if r["occupant"] == "infant" and r["variant"] == "cracked" and r["vehicle"] == "van"]),
                         ("inf-closed", [r for r in rows if r["occupant"] == "infant" and r["variant"] == "closed"])):
            cc = cell(sel, nm)
            out[tag] = (cc["n"], cc["rate_within_10min"], cc["rate_within_20min"], cc["rate_within_30min"], cc["median_min"], cc["detected"])
        e = [r for r in rows if r["occupant"] == "none"]
        fa = [r["seed"] for r in e if r["results"][nm]["any_check"]]
        if c is None:
            v4_fa = set(fa)
        new = [x for x in fa if x not in v4_fa]
        hfa = sum(r["results"][nm]["history_false_alarms"] for r in e)
        print(nm, c, "\n   ", out, "\n    empty FA (test parking)", len(fa), "of", len(e), "NEW vs v4:", new, "| alarms in empty history parkings (v4 rules included):", hfa, flush=True)


def config_snapshot():
    c = L.Config()
    keys = [k for k in c.__dict__ if k.startswith("learn") or k in ("closedLo", "closedHi", "crackedLo", "crackedHi", "modelThreshold")]
    return {k: c.__dict__[k] for k in keys}


def stress_run():
    """Empty vehicles only (seeds 200001-226000, 10,400 of them): false alarms for v4 and v5 with 3 and 10
    history parkings. Run after tuning is frozen; not used to tune."""
    build_cpp()
    names = list(CPP_NAMES.values())
    rows = evaluate_cpp(SETS["stress"][0], empty_only=True)
    out = {"empty_n": len(rows), "hard_n": sum(1 for r in rows if r["hard_empty"])}
    for nm in names:
        w = [r["seed"] for r in rows if r["results"][nm]["detect_s"] is not None and r["results"][nm]["detect_s"] <= FALSE_ALARM_WINDOW_S]
        a = [r["seed"] for r in rows if r["results"][nm]["any_check"]]
        out[nm] = {"false_alarms_within_60min": len(w), "any_alarm_in_90min_run": len(a), "seeds_60": w[:10],
                   "history_false_alarms_in_empty_parkings": sum(r["results"][nm]["history_false_alarms"] for r in rows)}
    print(json.dumps(out, indent=1))
    with open(os.path.join(HERE, "stress_v5.json"), "w") as f:
        json.dump({"label": "SIMULATED", "seeds": [200001, 226000], **out}, f, indent=1)
    return out


def final_run(force=False):
    """The one-time run on fresh seeds. Uses the real C++ engine (the Python reference matches it, see parity.sh)."""
    seeds, fname = SETS["final"]
    path = os.path.join(HERE, fname)
    if os.path.exists(path) and not force:
        raise SystemExit(f"{fname} already exists. The final set is run once. Use --force only if the code is frozen.")
    build_cpp()
    names = list(CPP_NAMES.values())
    rows = evaluate_cpp(seeds)
    res = report(rows, names)
    res.update({"set": "final_v5", "seeds": [seeds[0], seeds[-1]], "engine": "C++ LeftBehind via tools/left_behind/fleet_eval.cpp",
                "history_parkings": {"v4": 0, "v5_h0": 0, "v5_h3": 3, "v5_h10": 10},
                "config": config_snapshot(),
                "note": "SIMULATED. One test parking per vehicle seed after N earlier parkings of the same vehicle. v5_h0 equals v4 by construction."})
    # false alarms in the 60 min window plus any alarm in the 90 min run, and empties whose history held a dog/child
    emp = [r for r in rows if r["occupant"] == "none"]
    res["empty_false_alarms_in_empty_history_parkings"] = {nm: sum(r["results"][nm]["history_false_alarms"] for r in emp) for nm in names}
    res["empty_n"] = len(emp)
    with open(path, "w") as f:
        json.dump(res, f, indent=1)
    show(res, names)
    return res


if __name__ == "__main__":
    if sys.argv[1:2] == ["sweep"]:
        sweep(json.loads(sys.argv[2]), *[int(x) for x in sys.argv[3:6]])
        raise SystemExit
    which = sys.argv[1] if len(sys.argv) > 1 else "dev"
    if which == "final_cpp":
        final_run("--force" in sys.argv)
        raise SystemExit
    if which == "stress":
        stress_run()
        raise SystemExit
    names = list(VARIANTS)
    rows = evaluate(which, empty_only=(which == "stress"))
    res = report(rows, names)
    show(res, names)
    if which == "final":
        raise SystemExit("run:  evaluate_v5.py final_cpp  (once)")
    # sanity: v5 with no history must equal v4 exactly
    diff = [r["seed"] for r in rows if r["results"]["v4"]["detect_s"] != r["results"]["v5_h0"]["detect_s"]]
    print("v5_h0 differs from v4 on seeds:", diff)
