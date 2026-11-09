"""Quick checks for the body-diary pattern report. Run: tools/.venv/bin/python ai/body/test_patterns.py"""
import sys
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
import patterns as P  # noqa: E402


def room(days=60, seed=0):
    rng = np.random.default_rng(seed)
    t = pd.date_range("2026-10-01", periods=days * 1440, freq="1min")
    hour = t.hour.to_numpy()
    df = pd.DataFrame({
        "co2": 700 + rng.normal(0, 30, len(t)), "temp": 25 + rng.normal(0, 0.2, len(t)),
        "hum": 60 + rng.normal(0, 1, len(t)), "ldr": np.where((hour < 6) | (hour >= 19), 3800, 1500),
        "press": 912 + rng.normal(0, 0.1, len(t)),
    }, index=t)
    return df


def test_not_enough_marks():
    df = room(30)
    marks = [pd.Timestamp("2026-10-05 16:00"), pd.Timestamp("2026-10-12 16:00")]
    f, used = P.analyze(df, marks)
    text = P.report(f, used, len(marks))
    assert "Not enough yet" in text and used == 2


def test_finds_a_strong_planted_pattern_and_no_false_ones():
    df = room(60, seed=1)
    marks = []
    for day in range(3, 58, 4):                    # 14 marks, each after an afternoon of stale air
        t = pd.Timestamp("2026-10-01 18:00") + pd.Timedelta(days=day)
        df.loc[t - pd.Timedelta(hours=10): t - pd.Timedelta(hours=5), "co2"] = 1800
        marks.append(t)
    f, used = P.analyze(df, marks)
    verdicts = {x.name: x.verdict for x in f}
    assert verdicts["stale_hours"] == "pattern", verdicts
    assert verdicts["temp_mean"] == "none" and verdicts["press_change"] == "none", verdicts


def test_early_sign_is_not_called_a_cause():
    df = room(60, seed=2)
    marks = []
    for day in range(3, 58, 4):
        t = pd.Timestamp("2026-10-01 15:00") + pd.Timedelta(days=day)
        df.loc[t - pd.Timedelta(minutes=60): t, "ldr"] = 3900     # curtains drawn just before
        marks.append(t)
    f, used = P.analyze(df, marks)
    verdicts = {x.name: x.verdict for x in f}
    assert verdicts["dark_last2h"] == "early_sign", verdicts
    assert verdicts["dark_minutes"] != "pattern", verdicts
    text = P.report(f, used, len(marks))
    assert "Possible early signs, not causes" in text


def test_wording_never_claims_a_trigger():
    df = room(60, seed=3)
    marks = [pd.Timestamp("2026-10-01 18:00") + pd.Timedelta(days=d) for d in range(3, 58, 4)]
    text = P.report(*P.analyze(df, marks), len(marks)).lower()
    assert "trigger" not in text and "diagnos" not in text
    assert "not medical advice" in text


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
