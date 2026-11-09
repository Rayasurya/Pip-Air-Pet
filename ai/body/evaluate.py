"""Does the pattern report find hidden patterns, and avoid inventing them? SIMULATED people only.

Seeds 1-10 are for development. Seeds 101-130 are kept fresh for the final test (--final).
For each simulated person: which factors came out as "pattern", "hint" or "early sign".

How much data: 6 attacks a month throughout. The simulated room only has Oct-Dec climate, so
6 and 12 months of use are separate Oct-Dec seasons joined together (2 and 4 of them).

Run:  tools/.venv/bin/python ai/body/evaluate.py            (development seeds)
      tools/.venv/bin/python ai/body/evaluate.py --final    (fresh seeds, run once)
"""
import argparse
import json
import sys
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import patterns as P  # noqa: E402
import simulate_person as S  # noqa: E402

TRUE_FACTOR = {"pressure": "press_change", "pressure_rare": "press_change",
               "stale": "stale_hours", "stale_rare": "stale_hours"}


SCENARIOS = {"3 months": 1, "6 months": 2, "12 months": 4}


def one(args):
    seed, seasons = args
    world = S.make_world(seed, 90, seasons)
    out = {}
    for persona in S.PERSONAS:
        marks, df = S.make_marks(world, persona, seed)
        findings, used = P.analyze(df.set_index("time"), marks, seed=seed)
        out[persona] = {"marks": used, "found": {f.name: f.verdict for f in findings if f.verdict != "none"}}
    return seed, seasons, out


def summarize(results):
    table = {}
    for persona in S.PERSONAS:
        runs = [r[persona] for r in results]
        enough = [r for r in runs if r["marks"] >= P.MIN_MARKS]
        truth = TRUE_FACTOR.get(persona)
        row = {"runs": len(runs), "runs_with_enough_marks": len(enough),
               "median_marks": sorted(r["marks"] for r in runs)[len(runs) // 2]}
        if truth:
            row["true_factor_pattern"] = sum(r["found"].get(truth) == "pattern" for r in enough)
            row["true_factor_pattern_or_hint"] = sum(r["found"].get(truth) in ("pattern", "hint") for r in enough)
        row["any_false_hint"] = sum(any(v == "hint" and k != truth for k, v in r["found"].items()) for r in enough)
        row["any_false_pattern"] = sum(any(v == "pattern" and k != truth for k, v in r["found"].items()) for r in enough)
        if persona == "early_sign":
            row["dark_last2h_early_sign"] = sum(r["found"].get("dark_last2h") == "early_sign" for r in enough)
            row["called_light_a_cause"] = sum(r["found"].get("dark_minutes") == "pattern" for r in enough)
        row["false_patterns_by_factor"] = dict(Counter(k for r in enough for k, v in r["found"].items()
                                                       if v == "pattern" and k != truth))
        table[persona] = row
    return table


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--final", action="store_true")
    a = ap.parse_args()
    seeds = range(101, 131) if a.final else range(1, 11)
    out = {}
    with ProcessPoolExecutor() as ex:
        for label, seasons in SCENARIOS.items():
            res = [r for _, _, r in ex.map(one, [(s, seasons) for s in seeds])]
            out[label] = summarize(res)
            print(label, json.dumps(out[label]))
    name = "final_results.json" if a.final else "dev_results.json"
    (HERE / name).write_text(json.dumps({"note": "SIMULATED people", "seeds": [min(seeds), max(seeds)], **out}, indent=1))


if __name__ == "__main__":
    main()
