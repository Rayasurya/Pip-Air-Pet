"""Body-diary pattern report: what was the room's air like before each "I feel unwell" mark,
compared with the same hours on ordinary days?

Method (case-crossover, time-stratified): each mark is compared with the same clock time on the
other days of the same calendar month. Each person is their own control,
so slow differences between people, seasons and weekdays mostly cancel.

Windows:
  cause window : 3 to 24 h before the mark (things that might set a headache off)
  last 2 h     : kept apart. Migraine warning signs (light sensitivity, for example) can start
                 hours before the pain, so a change here is reported as a possible early sign.

Wording rule: "pattern worth testing", never "trigger" or "cause". This is a wellness tool.

Run on real data (after tools/pull_log.py):
  tools/.venv/bin/python ai/body/patterns.py --log ai/data/real_log_*.csv --marks ai/data/real_marks_*.csv
"""
import argparse
import glob
from dataclasses import dataclass

import numpy as np
import pandas as pd

MIN_MARKS = 8              # below this: timeline only (a design choice, not a research number)
PERMUTATIONS = 4000
DARK_LEVEL, DARK_IS_HIGH = 3000, True   # same as the firmware's config.h

# name, plain-language label, unit, window ("cause" or "last2h")
FACTORS = [
    ("stale_hours", "hours of stale air (CO2 above 1,400 ppm)", "h", "cause"),
    ("co2_mean", "average CO2", "ppm", "cause"),
    ("temp_mean", "average room temperature", "C", "cause"),
    ("hum_mean", "average humidity", "%", "cause"),
    ("press_change", "change in outdoor air pressure", "hPa", "cause"),
    ("dark_minutes", "minutes in the dark", "min", "cause"),
    ("dark_last2h", "minutes in the dark in the last 2 hours", "min", "last2h"),
    ("co2_last2h", "average CO2 in the last 2 hours", "ppm", "last2h"),
]


@dataclass
class Finding:
    name: str
    label: str
    unit: str
    window: str
    n: int
    hits: int              # marks where the value was past the usual level (the median of its own ordinary days)
    base_share: float      # how often ordinary days were past that same level (what chance would give)
    mean_diff: float       # mark minus its ordinary days, averaged (for the plain-language size)
    rank: float            # average position of the mark among its ordinary days, 0..1 (0.5 = no difference)
    p: float
    verdict: str = "none"  # "pattern", "hint", "early_sign", "none"


def load(log_paths, marks_paths):
    df = pd.concat([pd.read_csv(p, comment="#") for p in log_paths])
    df = df[~df["time"].astype(str).str.startswith("+")]          # rows before the clock was set
    df["time"] = pd.to_datetime(df["time"])
    df = df.drop_duplicates("time").sort_values("time").set_index("time")
    df = df.resample("1min").mean(numeric_only=True)               # gaps become empty minutes
    marks = []
    for p in marks_paths:
        m = pd.read_csv(p, comment="#")
        marks += [t for t in m["time"].astype(str) if not t.startswith("+")]
    marks = sorted(set(pd.to_datetime(marks)))
    return df, marks


class Windows:
    """Fast window averages over the minute log, ignoring missing minutes."""

    def __init__(self, df):
        self.t0 = df.index[0]
        self.n = len(df)
        dark = (df["ldr"] >= DARK_LEVEL) if DARK_IS_HIGH else (df["ldr"] <= DARK_LEVEL)
        cols = {
            "co2": df["co2"], "temp": df["temp"], "hum": df["hum"],
            "stale": (df["co2"] > 1400).astype(float).where(df["co2"].notna()),
            "dark": dark.astype(float).where(df["ldr"].notna()),
        }
        self.sums, self.counts = {}, {}
        for k, s in cols.items():
            v = s.to_numpy(dtype=float)
            ok = ~np.isnan(v)
            self.sums[k] = np.concatenate([[0], np.cumsum(np.where(ok, v, 0))])
            self.counts[k] = np.concatenate([[0], np.cumsum(ok)])
        self.press = df["press"].to_numpy(dtype=float) if "press" in df else np.full(self.n, np.nan)

    def idx(self, t):
        return int((t - self.t0) / pd.Timedelta(minutes=1))

    def mean(self, k, a, b):
        a, b = max(0, a), min(self.n, b)
        c = self.counts[k][b] - self.counts[k][a]
        if b <= a or c < 0.6 * (b - a):                             # too much missing: unknown
            return np.nan
        return (self.sums[k][b] - self.sums[k][a]) / c

    def press_at(self, i, search=30):
        for j in range(i, max(-1, i - search), -1):                # nearest earlier reading
            if 0 <= j < self.n and not np.isnan(self.press[j]):
                return self.press[j]
        return np.nan

    def features(self, t):
        i = self.idx(t)
        a, b = i - 24 * 60, i - 3 * 60
        return {
            "stale_hours": self.mean("stale", a, b) * (b - a) / 60.0,
            "co2_mean": self.mean("co2", a, b),
            "temp_mean": self.mean("temp", a, b),
            "hum_mean": self.mean("hum", a, b),
            "press_change": self.press_at(b) - self.press_at(a),
            "dark_minutes": self.mean("dark", a, b) * (b - a),
            "dark_last2h": self.mean("dark", i - 120, i) * 120,
            "co2_last2h": self.mean("co2", i - 120, i),
        }


def referents(t, start, end):
    """Same clock time on every other day of the same calendar month (time-stratified design).
    Days with other marks stay in: leaving them out biases the comparison when marks are frequent."""
    out = []
    d = t.replace(day=1)
    while d.month == t.month:
        r = d.replace(hour=t.hour, minute=t.minute)
        if r.date() != t.date() and start + pd.Timedelta(hours=24) <= r <= end:
            out.append(r)
        d += pd.Timedelta(days=1)
    return out


def analyze(df, marks, seed=0):
    df = df.resample("1min").mean(numeric_only=True)   # a full minute grid; gaps stay empty
    w = Windows(df)
    start, end = df.index[0], df.index[-1]
    sets = []                                                     # per mark: (case features, [referent features])
    for t in marks:
        if t < start + pd.Timedelta(hours=24) or t > end:
            continue
        refs = referents(t, start, end)
        if len(refs) >= 5:
            sets.append((w.features(t), [w.features(r) for r in refs]))
    rng = np.random.default_rng(seed)
    findings = []
    for name, label, unit, window in FACTORS:
        groups = []
        for case, refs in sets:
            vals = [case[name]] + [r[name] for r in refs]
            vals = np.array([v for v in vals if not np.isnan(v)]) if not np.isnan(case[name]) else None
            if vals is not None and len(vals) >= 6:
                groups.append(vals)                               # index 0 = the mark
        if len(groups) < MIN_MARKS:
            findings.append(Finding(name, label, unit, window, len(groups), 0, float("nan"), float("nan"), float("nan"),
                                    float("nan")))
            continue
        diffs = np.array([g[0] - g[1:].mean() for g in groups])
        # Rank of each day among its group (ties shared), scaled 0..1. Ranks suit lopsided measures
        # such as "hours of stale air", which is zero most of the time.
        ranks = [pd.Series(g).rank().to_numpy() for g in groups]
        scaled = [(r - 1) / (len(r) - 1) for r in ranks]
        obs = abs(np.mean([s[0] for s in scaled]) - 0.5)
        # Conditional permutation: in each group any day could have been "the mark day".
        lens = np.array([len(s) for s in scaled])
        flat = np.concatenate(scaled)
        offsets = np.concatenate([[0], np.cumsum(lens)[:-1]])
        picks = offsets + (rng.random((PERMUTATIONS, len(lens))) * lens).astype(int)
        perm = np.abs(flat[picks].mean(axis=1) - 0.5)
        p = (np.sum(perm >= obs - 1e-12) + 1) / (PERMUTATIONS + 1)
        rank = float(np.mean([s[0] for s in scaled]))
        up = rank > 0.5
        hits, base = 0, []
        for g in groups:
            usual = np.median(g[1:])
            hits += bool(g[0] > usual) if up else bool(g[0] < usual)
            base.append(np.mean(g[1:] > usual) if up else np.mean(g[1:] < usual))
        findings.append(Finding(name, label, unit, window, len(groups), hits, float(np.mean(base)),
                                float(diffs.mean()), rank, float(p)))
    # Verdicts. Holm correction across the cause factors, so 6 tries don't make chance look real.
    cause = sorted([f for f in findings if f.window == "cause" and not np.isnan(f.p)], key=lambda f: f.p)
    for k, f in enumerate(cause):
        if f.p * (len(cause) - k) < 0.05:
            f.verdict = "pattern"
        else:
            break
    for f in cause:
        if f.verdict == "none" and f.p < 0.05:
            f.verdict = "hint"
    for f in findings:
        if f.window == "last2h" and not np.isnan(f.p) and f.p * 2 < 0.05:
            f.verdict = "early_sign"
    return findings, len(sets)


def report(findings, n_used, n_marks, simulated=False):
    lines = ["# What the air was like before your marks", ""]
    if simulated:
        lines += ["**SIMULATED DATA.** A made-up person, for testing this report.", ""]
    if n_used < MIN_MARKS:
        lines += [f"{n_marks} marks so far ({n_used} with enough air history). Not enough yet: patterns need at least "
                  f"{MIN_MARKS}. Keep pressing when it happens; nothing is concluded until then."]
        return "\n".join(lines) + "\n"
    lines += [f"Based on {n_used} marks. Each one is compared with the same time of day on the other days of the "
              "same month.", ""]
    nice = {"pattern": [], "hint": [], "early_sign": []}
    for f in findings:
        if f.verdict in nice:
            nice[f.verdict].append(f)

    def line(f):
        direction = "higher" if f.rank > 0.5 else "lower"
        return (f"- **{f.label}** tends to be {direction} before your marks ({abs(f.mean_diff):.1f} {f.unit} on average). "
                f"It was {direction} than usual before {f.hits} of {f.n} marks ({100 * f.hits / f.n:.0f}%), "
                f"against {100 * f.base_share:.0f}% of ordinary days. "
                f"Chance of a difference this big with no real link: {f.p:.3f}.")

    if nice["pattern"]:
        lines += ["## Patterns worth testing", ""] + [line(f) for f in nice["pattern"]]
        lines += ["", "A pattern is not proof. To test one, change it on purpose for two weeks "
                  "(for example, keep CO2 under 1,000 ppm by airing the room) and see if marks drop.", ""]
    if nice["hint"]:
        lines += ["## Weak hints (keep watching)", ""] + [line(f) for f in nice["hint"]]
        lines += ["", "These could be chance. Several things were checked, so one weak hint is expected now and then.", ""]
    if nice["early_sign"]:
        lines += ["## Possible early signs, not causes", ""] + [line(f) for f in nice["early_sign"]]
        lines += ["", "These changed in the 2 hours just before the marks. A migraine can start hours before the pain "
                  "(light sensitivity, for example), so this is more likely the headache starting than its cause.", ""]
    if not any(nice.values()):
        lines += ["No pattern stands out. The air before your marks looks like the air on ordinary days. That is a "
                  "real answer too: the air in this room may not be what sets them off.", ""]
    lines += ["Pip only measures the room's air, light and (from the internet) outdoor air pressure. Sleep, food, "
              "stress and hormones also matter and are not seen here. Not medical advice."]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", nargs="+", required=True)
    ap.add_argument("--marks", nargs="+", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    logs = [p for g in a.log for p in glob.glob(g)]
    mk = [p for g in a.marks for p in glob.glob(g)]
    df, marks = load(logs, mk)
    findings, used = analyze(df, marks)
    text = report(findings, used, len(marks))
    if a.out:
        open(a.out, "w").write(text)
    print(text)


if __name__ == "__main__":
    main()
