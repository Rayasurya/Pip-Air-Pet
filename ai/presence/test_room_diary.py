"""Run: tools/.venv/bin/python ai/presence/test_room_diary.py   (from air-pet)

Made-up rooms and the simulated room only. No real home data is needed or read.
The server test runs on this Mac alone, on a spare port, with a throwaway file for the checks.
"""
import argparse
import json
import sys
import tempfile
import threading
import urllib.error
import urllib.request
from datetime import datetime, timedelta
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import room_diary as rd  # noqa: E402
from level_rule import Row, load  # noqa: E402

T0 = datetime(2026, 1, 5, 9, 0)                      # a Monday, 09:00


def room(*parts, start=T0):
    rows, t = [], start
    for a, b in parts:
        if a == "off":
            t += timedelta(minutes=b)
            continue
        for _ in range(a):
            rows.append(Row(t, float(b)))
            t += timedelta(minutes=1)
    return rows


# 1. The page's data. Every day is a full 24 hours of blocks with no holes and no overlaps,
#    and time Pip did not log is "off" with no CO2 value at all.
rows = room((600, 430), (120, 650), ("off", 400), (600, 430))     # runs into Tuesday
data = rd.payload(rows)
assert [d["date"] for d in data["days"]] == ["2026-01-05", "2026-01-06"]
for d in data["days"]:
    edges = [(b["a"], b["b"]) for b in d["blocks"]]
    assert edges[0][0] == 0 and edges[-1][1] == 1440, edges
    assert all(a[1] == b[0] for a, b in zip(edges, edges[1:])), edges
    for b in d["blocks"]:
        got = [v for v in d["co2"][b["a"]:b["b"]] if v is not None]
        if b["k"] == "off":
            assert not got, "an off block must not carry readings"
            assert b["title"] == "Pip was off" and "Nothing is guessed" in b["reading"]
        else:
            assert got, "a block that says something must stand on readings"
mon = data["days"][0]
assert mon["blocks"][0]["k"] == "off" and mon["blocks"][0]["b"] == 9 * 60, "before the log starts is off"
assert mon["logged"] == 720 and sum(v is not None for v in mon["co2"]) == 720
assert data["minutes"] == len(rows)
assert not data["simulated"] and not data["live"] and data["checks"] == []

# 2. A block that crosses midnight is cut in two, and each half says so.
rows = room((600, 430), (600, 650), start=T0.replace(hour=6))     # here from 16:00 to 02:00
days = rd.payload(rows)["days"]
late, early = days[0]["blocks"][-1], days[1]["blocks"][0]
assert late["k"] == early["k"] == "here" and late["s"] == early["s"] and late["e"] == early["e"]
assert "next day" in late["cont"] and "day before" in early["cont"]

# 3. --days keeps the last days only, but the reading still uses everything before them.
rows = room(*[(1440, 430)] * 5, start=T0.replace(hour=0))
assert [d["date"] for d in rd.payload(rows, last_days=2)["days"]] == ["2026-01-08", "2026-01-09"]

# 4. The simulated room is always marked simulated, shows the simulator's truth, and never saves.
sim = load(rd.SIM)
sim = [r for r in sim if r.t < sim[0].t + timedelta(days=3)]
data = rd.payload(sim, simulated=True, truth=rd.sim_truth())
html = rd.page(data)
assert "Simulated room" in html and "(simulated)" in html
assert "Checks on this page are not saved" in html
assert all("truth" in d for d in data["days"])
assert any(d["truth"]["in"] for d in data["days"])
for bad in (chr(0x2014), "ground_truth.csv", "</script><"):
    assert bad not in html.split('<script id="data"')[0] + html.split("</script>")[-1], bad
assert "<select" not in html and 'type="time"' not in html and "type = 'time'" not in html, "no OS-native controls"
assert "box-shadow:0" not in html and "drop-shadow" not in html
for edge in ("border-left", "border-right", "border-top", "border-bottom"):
    assert edge not in html, "no single-side borders"
assert rd.runs([False, True, True, False, True]) == [[1, 3], [4, 5]]

# 5. A check from the page is cleaned before it is stored.
ok = {"start": "2026-01-05 10:00", "end": "2026-01-05 11:30", "state": "out", "pip_said": "here", "note": " a\nb  c "}
assert rd.clean_check(ok)["note"] == "a b c"
assert rd.clean_check({**ok, "note": "=SUM(A1)"})["note"].startswith("'")
assert len(rd.clean_check({**ok, "note": "x" * 999})["note"]) == 160
for bad in ({**ok, "state": "maybe"}, {**ok, "pip_said": "off"}, {**ok, "end": ok["start"]},
            {**ok, "start": "yesterday"}, {**ok, "end": "2026-01-09 10:00"}, [], "x", None):
    try:
        rd.clean_check(bad)
    except (ValueError, TypeError):
        continue
    raise AssertionError(f"should have been refused: {bad}")

# 6. Saving, saving again for the same stretch, and undo. The file is the format real_pip.py reads.
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "deep" / "ground_truth.csv"
    assert rd.load_checks(path) == []
    rd.save_check(ok, path, now=T0)
    assert path.read_text().splitlines()[0] == "start,end,state,pip_said,note,saved_at"
    rd.save_check({**ok, "state": "asleep"}, path, now=T0)
    got = rd.save_check({**ok, "start": "2026-01-05 12:00", "end": "2026-01-05 13:00"}, path, now=T0)
    assert [(c["start"][11:], c["state"]) for c in got] == [("10:00", "asleep"), ("12:00", "out")]
    assert [c["start"][11:] for c in rd.undo_check(ok, path)] == ["12:00"]

    # 7. The server. It answers this Mac's own page, and refuses everyone else.
    args = argparse.Namespace(demo=False, log=rd.SIM, days=2, checks=path, port=0)
    server = ThreadingHTTPServer(("127.0.0.1", 0), rd.handler(args))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    here = f"http://127.0.0.1:{server.server_port}"

    def call(where, body=None, **headers):
        data_ = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(here + where, data=data_, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return r.status, r.read().decode()
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode()

    json_ = {"Content-Type": "application/json"}
    code, html = call("/")
    assert code == 200 and "Pip's room diary" in html and "Checks on this page are not saved" not in html
    assert call("/ground_truth.csv")[0] == 404 and call("/../level_rule.py")[0] == 404
    code, body = call("/check", {**ok, "start": "2026-01-05 14:00", "end": "2026-01-05 15:00"}, **json_)
    assert code == 200 and len(json.loads(body)["checks"]) == 2, body
    assert call("/check", ok, **json_, Origin="http://evil.example")[0] == 403, "another site must not post"
    assert call("/check", ok, **json_, Host="evil.example")[0] == 403
    assert call("/check", ok, **{"Content-Type": "text/plain"})[0] == 403
    assert call("/check", {**ok, "state": "maybe"}, **json_)[0] == 400
    assert call("/delete", ok, **json_)[0] == 403
    assert len(rd.load_checks(path)) == 2, "refused calls must not touch the file"
    code, body = call("/undo", {"start": "2026-01-05 14:00", "end": "2026-01-05 15:00"}, **json_)
    assert code == 200 and len(json.loads(body)["checks"]) == 1
    server.shutdown()

    # 8. In demo mode nothing is ever saved, even if a page tries.
    args = argparse.Namespace(demo=True, log=rd.SIM, days=2, checks=path, port=0)
    server = ThreadingHTTPServer(("127.0.0.1", 0), rd.handler(args))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    here = f"http://127.0.0.1:{server.server_port}"
    assert call("/check", ok, **json_)[0] == 403 and len(rd.load_checks(path)) == 1
    assert "Simulated room" in call("/")[1]
    server.shutdown()

print("room diary tests passed")
