"""Run: tools/.venv/bin/python ai/presence/test_diary_simple.py   (from air-pet)

The short view of the room diary. Made-up rooms and the simulated room only.
What the page does when you play, drag and pick a day was checked in a real browser
(play, pause, keys, drag, the truth switch, a phone-sized screen, reduced motion).
This file checks what can be checked without one.
"""
import argparse
import json
import re
import sys
import threading
import urllib.request
from datetime import datetime, timedelta
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import diary_simple as ds  # noqa: E402
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


def blob(page):
    return json.loads(page.split('<script id="data" type="application/json">')[1].split("</script>")[0])


def chrome(page):
    """The page without its data: the part a person wrote."""
    head, rest = page.split('<script id="data" type="application/json">')
    return head + rest.split("</script>", 1)[1]


# 1. The short page draws from the same reading as the full page, block for block.
rows = room((600, 430), (120, 650), ("off", 400), (600, 430))
full = rd.payload(rows)
small = ds.slim(full)
assert len(small["days"]) == len(full["days"]) and small["start"] == full["start"]
for a, b in zip(small["days"], full["days"]):
    assert [(x["k"], x["a"], x["b"]) for x in a["blocks"]] == [(x["k"], x["a"], x["b"]) for x in b["blocks"]]
    assert a["blocks"][0]["a"] == 0 and a["blocks"][-1]["b"] == 1440
    assert a["co2"] == b["co2"] and a["base"] == b["base"]
    assert all(set(x) == {"k", "a", "b", "sure"} for x in a["blocks"]), "the long evidence text stays on the full page"
    assert "truth" not in a, "a real room has no truth line"
#    time Pip did not log is "off", with no CO2 value to draw
off = [x for d in small["days"] for x in d["blocks"] if x["k"] == "off"]
assert off and all(x["sure"] == "" for x in off)
for d in small["days"]:
    for x in d["blocks"]:
        if x["k"] == "off":
            assert not any(v is not None for v in d["co2"][x["a"]:x["b"]])

# 2. A real room's page: no simulated tag, and no truth switch, because nothing knows the truth.
page = ds.page(full)
assert "Simulated room" not in page and 'id="truth"' not in page and "(simulated)" not in page
assert blob(page) == json.loads(json.dumps(small))

# 3. The simulated room is always marked, and it can show what really happened.
sim = load(rd.SIM)
sim = [r for r in sim if r.t < sim[0].t + timedelta(days=3)]
data = rd.payload(sim, simulated=True, truth=rd.sim_truth())
page = ds.page(data)
assert '<span class="tag">Simulated room</span>' in page and "(simulated)" in page
assert "This room is simulated." in page and 'role="switch"' in page
got = blob(page)
assert got["simulated"] and all(d["truth"] for d in got["days"])
assert got["stuffy"] == 1000 and got["gasping"] == 1500, "Pip's own limits (firmware/lib/pet/config.h)"

# 4. Words every state needs, and the limits of what Pip can say.
for words in ("Someone is here.", "Someone is likely asleep.", "The air is clearing.", "Nobody is here.", "Pip was off.",
              "nothing is guessed", "Asleep is a guess", "The air alone can't tell which", "Pip made no call",
              "This is the tested rule", "A guess, not a tested result", "It is not a probability",
              "Pip can't tell who is in the room, or how many people"):
    assert words in page, words
for key in ("key-here", "key-asleep", "key-clearing", "key-empty", "key-off"):
    assert f'id="{key}"' in page

# 5. The house rules for anything on screen.
c = chrome(page)
assert chr(0x2014) not in c, "no em dashes"
for native in ("<select", "<input", 'type="range"', 'type="time"', "<details"):
    assert native not in c, f"no OS-native controls: {native}"
for edge in ("border-left", "border-right", "border-top", "border-bottom", "border-inline", "border-block"):
    assert edge not in c, "no single-side borders"
assert "drop-shadow" not in c and "text-shadow" not in c
for shadow in re.findall(r"box-shadow:([^;}]+)", c):
    assert shadow.strip() == "none" or all(part.strip().startswith("inset") for part in shadow.split("),")), shadow
assert "prefers-reduced-motion" in c and 'role="slider"' in c and "aria-valuetext" in c
assert "ground_truth" not in c and "log_all" not in c, "nothing about private files"
assert "</script" not in page.split('<script id="data" type="application/json">')[1].split("</script>")[0]

# 6. Pip's real faces are inside the page, so one file is the whole thing.
for name in ("happy", "stuffy", "gasping"):
    raw = (ds.FACES / f"{name}.png").read_bytes()
    assert raw[:8] == b"\x89PNG\r\n\x1a\n" and len(raw) < 1000
    assert ds.face(name) in page
assert "http://" not in c.replace("http://www.w3.org/2000/svg", "") and "https://" not in c, "it loads nothing from the web"

# 7. Links and looks for a hosted copy. Every address is escaped or refused.
assert "<a " not in page, "a plain copy links nowhere"
hosted = ds.page(data, back='../"><script>', full="full/", fonts="../assets/fonts/", theme="light")
assert 'href="../&quot;&gt;&lt;script&gt;">Back to Pip\'s story</a>' in hosted
assert '<a class="pill" href="full/">Open the full diary</a>' in hosted
assert 'url("../assets/fonts/Geist-Variable.woff2")' in hosted and '<html lang="en" data-theme="light">' in hosted
assert "Back to the short view" in rd.page(data, back="../", back_label="Back to the short view")
for bad in ('x");}body{display:none', "a b", "'"):
    try:
        ds.page(data, fonts=bad)
    except ValueError:
        continue
    raise AssertionError(f"should have been refused: {bad}")
try:
    ds.page(data, theme="pink")
    raise AssertionError("theme should be light or dark")
except ValueError:
    pass

# 8. room_diary.py --simple builds this page, as a file and on its own server.
args = argparse.Namespace(demo=True, simple=True, days=2, log=rd.SIM, checks=None, back=None, back_label=None,
                          full=None, fonts=None, theme=None)
built = rd.build(args, live=False)
assert "One day in a room" in built and len(blob(built)["days"]) == 2
assert "One day in a room" not in rd.build(argparse.Namespace(**{**vars(args), "simple": False}), live=False)
server = ThreadingHTTPServer(("127.0.0.1", 0), rd.handler(args))
threading.Thread(target=server.serve_forever, daemon=True).start()
with urllib.request.urlopen(f"http://127.0.0.1:{server.server_port}/", timeout=20) as r:
    assert r.status == 200 and "One day in a room" in r.read().decode()
server.shutdown()

print("short diary tests passed")
