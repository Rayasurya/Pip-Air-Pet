"""Exact-match check: the C++ LeftBehind (v5) against the Python reference on whole SIMULATED fleets.

For each seed, generate the fleet (10 earlier parkings + the test parking), write one CSV, run both, and
compare per parking: first alarm second, learned counts, and the saved learned state bytes.

  tools/.venv/bin/python tools/left_behind/parity.py 60001-60040
Used by tools/left_behind/parity.sh.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "ai", "vehicle"))
sys.path.insert(0, HERE)
import cabin_sim  # noqa: E402
import leftbehind_ref as L  # noqa: E402


def write_csv(seeds, path, n_hist):
    fleets = {}
    with open(path, "w") as f:
        f.write("fleet,t_s,ignition,co2_ppm,temp_c,nominal_volume_m3\n")
        for s in seeds:
            hist, test = cabin_sim.generate_fleet(s, n_hist)
            fleets[s] = hist + [test]
            vol = 8 if test["meta"]["vehicle"] == "van" else 3
            for p in fleets[s]:
                for t, g, c, tc in zip(p["t"], p["ign"], p["co2"], p["temp"]):
                    f.write(f"{s},{int(t)},{int(g)},{int(c)},{tc:.2f},{vol}\n")
    return fleets


def python_side(fleets):
    rows = []
    for s, parks in fleets.items():
        vol = 8.0 if parks[-1]["meta"]["vehicle"] == "van" else 3.0
        lb = L.LeftBehind(L.Config(cabinVolumeM3=vol))
        for k, p in enumerate(parks):
            first = L.run_parking(lb, p, learn=False)  # learning is done by the next parking's ignition-on
            lb.learn_from_parking()
            blob = lb.learned.to_bytes()
            lb.learned = L.Learned.from_bytes(blob)  # same save/load round trip as the C++ side
            rows.append((s, k, first, len(lb.learned.base), len(lb.learned.closed), len(lb.learned.cracked), blob.hex()))
    return rows


def main():
    spec = sys.argv[1] if len(sys.argv) > 1 else "60001-60030"
    a, _, b = spec.partition("-")
    seeds = range(int(a), int(b or a) + 1)
    n_hist = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    out = os.path.join(ROOT, "build", "parity")
    os.makedirs(out, exist_ok=True)
    fleets = write_csv(seeds, os.path.join(out, "lb_in.csv"), n_hist)
    exe = os.path.join(ROOT, "build", "left_behind_parity")
    lib = os.environ.get("LB_LIB", os.path.join(ROOT, "firmware", "lib", "pet"))
    subprocess.run(["clang++", "-std=c++17", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-I" + lib,
                    os.path.join(HERE, "parity.cpp"), os.path.join(lib, "leftbehind.cpp"), "-o", exe], check=True)
    cpp = subprocess.run([exe, os.path.join(out, "lb_in.csv")], check=True, capture_output=True, text=True).stdout.strip().splitlines()[1:]
    cpp_rows = [tuple(x if i == 6 else int(x) for i, x in enumerate(line.split(","))) for line in cpp]
    py_rows = python_side(fleets)
    # python first-check is relative to nothing: run_parking returns absolute t within the parking, like C++
    bad = [(a, b) for a, b in zip(py_rows, cpp_rows) if a != b]
    n_alarm = sum(1 for r in py_rows if r[2] >= 0)
    print(f"{len(py_rows)} parkings in {len(list(seeds))} fleets, {n_alarm} with an alarm, {sum(1 for r in py_rows if r[3])} with learned state")
    if len(py_rows) != len(cpp_rows):
        print("DIFFERENT: row counts", len(py_rows), len(cpp_rows))
        return 1
    if bad:
        print("DIFFERENT on", len(bad), "parkings. First:")
        for a, b in bad[:3]:
            print("  py ", a[:6], "\n  cpp", b[:6], "\n  state equal:", a[6] == b[6])
        return 1
    print("MATCH: alarms, learned counts and saved state bytes are identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
