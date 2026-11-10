"""Pip's room diary: one page that turns the CO2 log into days you can read.

Pip's face is the glance. This page is the read. It runs on the Mac, from the log, not on the chip.

Run from air-pet:

    tools/.venv/bin/python ai/presence/room_diary.py
        Your own log (data/pip/log_all.csv), served on this Mac only at http://127.0.0.1:8765
        "Right" and "Wrong?" are saved to data/pip/presence/ground_truth.csv (private).

    tools/.venv/bin/python ai/presence/room_diary.py --demo
        The simulated room. No real data. Checks are not saved.

    tools/.venv/bin/python ai/presence/room_diary.py --demo --export ai/presence/out/room-diary-demo.html
        One file you can open offline or send to someone.

    Add --simple to any of these for the short view: a drawn room, one sentence, and a day you
    can play. It is the page to show someone who has never seen Pip. See diary_simple.py.

What the page will not do:
  - It never fills in time when Pip was off. Those hours say "Pip was off".
  - It never says who was in the room, or how many people.
  - A real-room page holds that home's hours. It is only ever written under data/ (private).

The reading itself is in events.py. The tested rule is in level_rule.py. Tests: test_room_diary.py
"""
import argparse
import csv
import html
import json
import sys
import webbrowser
from datetime import datetime, time, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import diary_simple
from events import SURE_HIGH, SURE_LOW, STUFFY_PPM, day_blocks, describe, diary, when
from level_rule import Rule, load

ROOT = Path(__file__).resolve().parents[2]
SIM = ROOT / "ai" / "data" / "sim_room.csv"
REAL = ROOT / "data" / "pip" / "log_all.csv"
PRIVATE = ROOT / "data"
CHECKS = PRIVATE / "pip" / "presence" / "ground_truth.csv"
FIELDS = ["start", "end", "state", "pip_said", "note", "saved_at"]
STATES = ("in", "asleep", "out")                      # what a person says was true
SAID = ("here", "asleep", "clearing", "empty")        # what Pip said
STAMP = "%Y-%m-%d %H:%M"
DEMO_LOAD_DAYS, DEMO_SHOW_DAYS = 21, 14               # a week for the empty level to settle, then 2 weeks


# ---------- the data the page draws ----------

def day_label(d):
    return f"{d:%a} {d.day} {d:%b}"


def runs(flags):
    """[False, True, True, False] -> [[1, 3]]: minute ranges where the flag is on."""
    out, start = [], None
    for i, f in enumerate(list(flags) + [False]):
        if f and start is None:
            start = i
        elif not f and start is not None:
            out.append([start, i])
            start = None
    return out


def sim_truth(path=SIM):
    """What the simulator knows and a real room never does: who was really in, and the window."""
    out = {}
    with open(path) as f:
        for x in csv.DictReader(ln for ln in f if not ln.startswith("#")):
            out[x["time"]] = (int(x["truth_people"]) > 0, x["truth_window"] not in ("0", ""))
    return out


def payload(rows, *, simulated=False, live=False, checks=(), truth=None, last_days=None, rule=Rule()):
    """Everything the page needs, as plain lists and words. No drawing happens here."""
    bl, spells, extra = diary(rows, rule)
    co2, base = {}, {}
    for r, b in zip(rows, extra["base"]):
        day = r.t.date()
        co2.setdefault(day, [None] * 1440)[r.t.hour * 60 + r.t.minute] = round(r.co2)
        base.setdefault(day, [None] * 24)[r.t.hour] = round(b)
    first, last = rows[0].t.date(), rows[-1].t.date()
    dates = [first + timedelta(days=i) for i in range((last - first).days + 1)]
    if last_days:
        dates = dates[-last_days:]
    days = []
    for day in dates:
        midnight = datetime.combine(day, time.min)

        def minute(t):
            return int((t - midnight).total_seconds() // 60)

        blocks_ = []
        for b, lo, hi in day_blocks(bl, day):
            cont = ""
            if b.kind != "off":
                if b.start < lo and b.end > hi:
                    cont = f"Part of a longer stretch: {when(b.start, b.end)}."
                elif b.start < lo:
                    cont = f"Began the day before, at {b.start:%H:%M}."
                elif b.end > hi:
                    cont = f"Carries on into the next day, until {b.end:%H:%M}."
            blocks_.append({"k": b.kind, "a": minute(lo), "b": minute(hi),
                            "s": b.start.strftime(STAMP), "e": b.end.strftime(STAMP),
                            **describe(b), "unusual": b.unusual, "cont": cont})
        stuffy, also = [], set()
        for s in spells:
            lo, hi = max(midnight, s["start"]), min(midnight + timedelta(days=1), s["end"])
            if lo < hi:
                stuffy.append({"a": minute(lo), "b": minute(hi), "peak": round(s["peak"])})
                if s["start"] >= midnight:            # a spell is compared on the day it began
                    also.update(s["also"])
        one = {"date": day.isoformat(), "label": day_label(day), "weekend": day.weekday() >= 5,
               "blocks": blocks_, "co2": co2.get(day, [None] * 1440), "base": base.get(day, [None] * 24),
               "spells": stuffy, "also": [day_label(d) for d in sorted(also)],
               "logged": sum(v is not None for v in co2.get(day, []))}
        if truth is not None:
            keys = [(midnight + timedelta(minutes=m)).strftime(STAMP) for m in range(1440)]
            one["truth"] = {"in": runs(truth.get(k, (False, False))[0] for k in keys),
                            "window": runs(truth.get(k, (False, False))[1] for k in keys)}
        days.append(one)
    start = max(range(len(days)), key=lambda i: (days[i]["logged"] >= 1080, i))
    shown = [d for d in dates if d in co2]
    return {"days": days, "start": start, "simulated": simulated, "live": live, "checks": list(checks),
            "rule": {"on": rule.on_ppm, "on_min": rule.on_min, "sure_low": SURE_LOW, "sure_high": SURE_HIGH},
            "stuffy": STUFFY_PPM,
            "span": f"{day_label(dates[0])} to {day_label(dates[-1])} {dates[-1].year}",
            "days_logged": len(shown), "minutes": sum(d["logged"] for d in days)}


# ---------- checks: what a person says was true ----------

def clean_check(d):
    """One check from the page, made safe to store. Raises ValueError when it isn't one."""
    if not isinstance(d, dict):
        raise ValueError("not a check")
    a = datetime.strptime(str(d.get("start", "")), STAMP)
    b = datetime.strptime(str(d.get("end", "")), STAMP)
    if not a < b or b - a > timedelta(days=2):
        raise ValueError("the end must come after the start, within 2 days")
    if d.get("state") not in STATES or d.get("pip_said") not in SAID:
        raise ValueError("unknown state")
    note = " ".join(str(d.get("note", "")).split())[:160]
    if note[:1] in ("=", "+", "-", "@"):          # a note is words, never a spreadsheet formula
        note = "'" + note
    return {"start": a.strftime(STAMP), "end": b.strftime(STAMP), "state": d["state"],
            "pip_said": d["pip_said"], "note": note}


def load_checks(path=CHECKS):
    if not Path(path).exists():
        return []
    with open(path, newline="") as f:
        return [{k: x.get(k, "") or "" for k in FIELDS} for x in csv.DictReader(f)]


def _write(path, rows):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)


def save_check(check, path=CHECKS, now=None):
    """Add one check. A second check for the very same stretch replaces the first."""
    c = clean_check(check)
    c["saved_at"] = (now or datetime.now()).strftime(STAMP)
    rows = [r for r in load_checks(path) if (r["start"], r["end"]) != (c["start"], c["end"])]
    _write(path, rows + [c])
    return load_checks(path)


def undo_check(check, path=CHECKS):
    key = (str(check.get("start", "")), str(check.get("end", "")))
    _write(path, [r for r in load_checks(path) if (r["start"], r["end"]) != key])
    return load_checks(path)


# ---------- the page ----------

CSS = r"""
:root{color-scheme:light;--page:#f9f9f7;--surface:#fcfcfb;--ink:#0b0b0b;--ink2:#52514e;--muted:#898781;
--grid:#e1e0d9;--axis:#c3c2b7;--border:rgba(11,11,11,.12);--wash:rgba(11,11,11,.05);
--here:#eb6834;--asleep:#2a78d6;--clearing:#1baf7a;--empty:#dddcd4;--stuffy:#d03b3b;--on-fill:#ffffff}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){color-scheme:dark;--page:#0d0d0d;--surface:#1a1a19;
--ink:#f0efec;--ink2:#c3c2b7;--muted:#898781;--grid:#2c2c2a;--axis:#4a4a46;--border:rgba(255,255,255,.14);
--wash:rgba(255,255,255,.07);--here:#d95926;--asleep:#3987e5;--clearing:#199e70;--empty:#3d3d39;--stuffy:#e66767}}
:root[data-theme="dark"]{color-scheme:dark;--page:#0d0d0d;--surface:#1a1a19;--ink:#f0efec;--ink2:#c3c2b7;
--muted:#898781;--grid:#2c2c2a;--axis:#4a4a46;--border:rgba(255,255,255,.14);--wash:rgba(255,255,255,.07);
--here:#d95926;--asleep:#3987e5;--clearing:#199e70;--empty:#3d3d39;--stuffy:#e66767}
*{box-sizing:border-box}
body{margin:0;background:var(--page);color:var(--ink);font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
.wrap{max-width:1040px;margin:0 auto;padding:28px 16px 64px}
h1{font-size:28px;line-height:1.2;margin:0 0 6px;letter-spacing:-.01em}
h2{font-size:17px;margin:0 0 4px}
p{margin:0}
.sub{color:var(--ink2);max-width:66ch}
.muted{color:var(--ink2);font-size:13.5px}
.tags{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0 0}
.tag{display:inline-block;border:1px solid var(--axis);border-radius:999px;padding:2px 10px;font-size:12.5px;
font-weight:600;color:var(--ink);background:var(--surface)}
.tag.loud{background:var(--ink);color:var(--surface);border-color:var(--ink)}
a.tag{text-decoration:none;transition:background .18s,border-color .18s}
a.tag:hover{background:var(--wash);border-color:var(--ink2)}
a.tag:focus-visible{outline:2px solid var(--asleep);outline-offset:2px}
#tally{margin-top:12px;color:var(--ink2)}
.card{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:18px;margin-top:18px}
.legend{display:flex;flex-wrap:wrap;gap:6px 18px;margin-top:18px;font-size:13.5px;color:var(--ink2)}
.key{display:inline-flex;align-items:center;gap:7px}
.sw{width:14px;height:14px;border-radius:4px;flex:none}
.sw.line{height:4px;border-radius:2px}
.k-here{background:var(--here)}.k-asleep{background:var(--asleep)}.k-clearing{background:var(--clearing)}
.k-empty{background:var(--empty)}
.k-off{background:repeating-linear-gradient(135deg,transparent 0 3px,var(--axis) 3px 4px);border:1px solid var(--axis)}
.k-stuffy{background:var(--stuffy)}.k-truth{background:var(--ink)}

/* every day */
.rows{margin-top:14px}
.row{display:grid;grid-template-columns:88px 1fr;gap:10px;align-items:center;padding:3px 8px;border:1px solid transparent;
border-radius:9px;cursor:pointer;transition:background .18s,border-color .18s}
.row:hover{background:var(--wash)}
.row.on{background:var(--wash);border-color:var(--ink2)}
.row:focus-visible{outline:2px solid var(--asleep);outline-offset:1px}
.row.axis{cursor:default;padding-bottom:0}.row.axis:hover{background:none}
.lab{font-size:13px;color:var(--ink2);font-variant-numeric:tabular-nums;white-space:nowrap}
.row.on .lab{color:var(--ink);font-weight:600}
.hours{position:relative;height:16px;font-size:11.5px;color:var(--muted);font-variant-numeric:tabular-nums}
.hours span{position:absolute;transform:translateX(-50%)}
.hours span:first-child{transform:none}.hours span:last-child{transform:translateX(-100%)}
.tracks{display:grid;gap:2px}
.track{position:relative;height:4px}
.strip{position:relative;height:16px;border-radius:5px;overflow:hidden}
.strip.big{height:30px;border-radius:7px}
.seg{position:absolute;top:0;bottom:0;transition:filter .15s,opacity .15s;overflow:hidden}
.seg:not(.k-off){box-shadow:inset -1px 0 0 var(--surface),inset 1px 0 0 var(--surface)}
.seg:hover,.seg.hot{filter:brightness(1.12) saturate(1.15)}
.seglab{position:absolute;left:8px;top:50%;transform:translateY(-50%);font-size:12px;font-weight:600;white-space:nowrap;
color:var(--on-fill);pointer-events:none}
.k-empty .seglab,.k-off .seglab{color:var(--ink2)}
.bar{position:absolute;top:0;bottom:0;border-radius:2px;min-width:3px}

/* one day */
.dayhead{display:flex;flex-wrap:wrap;align-items:center;gap:10px}
.dayhead h2{font-size:20px;margin:0 6px 0 0}
.btn{font:inherit;font-size:13.5px;font-weight:600;line-height:1;padding:8px 14px;border-radius:999px;border:1px solid var(--axis);
background:var(--surface);color:var(--ink);cursor:pointer;transition:background .18s,color .18s,border-color .18s,opacity .18s}
.btn:hover{background:var(--wash)}
.btn:active,.btn[aria-pressed="true"]{background:var(--ink);color:var(--surface);border-color:var(--ink)}
.btn:disabled{opacity:.35;cursor:default}
.btn:focus-visible,.inp:focus-visible{outline:2px solid var(--asleep);outline-offset:2px}
.btn.quiet{font-weight:500;color:var(--ink2)}
.sum{display:flex;flex-wrap:wrap;gap:6px 16px;margin:10px 0 2px;font-size:13.5px;color:var(--ink2)}
.sum b{color:var(--ink);font-weight:600}
#dayview{transition:opacity .16s}
#dayview.swap{opacity:0}
.chart{position:relative;margin-top:12px}
.chart svg{display:block;width:100%;overflow:visible}
.chart text{font-size:11.5px;fill:var(--muted);font-variant-numeric:tabular-nums}
.chart text.lab2{fill:var(--ink2)}
.under{margin-top:4px}
.stuffynote{margin-top:10px;font-size:13.5px;color:var(--ink2)}
#tip{position:fixed;z-index:5;pointer-events:none;background:var(--surface);border:1px solid var(--axis);border-radius:9px;
padding:7px 10px;font-size:12.5px;line-height:1.4;color:var(--ink2);max-width:260px;opacity:0;transition:opacity .12s}
#tip.show{opacity:1}
#tip b{display:block;color:var(--ink);font-size:14px}

/* the list */
.list{display:grid;gap:10px;margin-top:16px}
.blk{border:1px solid var(--border);border-radius:12px;padding:12px 14px;background:var(--surface);
transition:border-color .2s,background .2s}
.blk.hot{border-color:var(--ink2);background:var(--wash)}
.blkhead{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;margin-bottom:6px}
.time{font-weight:600;font-variant-numeric:tabular-nums}
.badge{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--axis);border-radius:999px;padding:1px 10px 1px 7px;
font-size:12.5px;font-weight:600}
.badge .sw{width:10px;height:10px;border-radius:3px}
.blk p{max-width:74ch}
.blk .rd{color:var(--ink2)}
.blk .why{margin-top:6px;font-size:13.5px;color:var(--ink2)}
.blk .why b{color:var(--ink);font-weight:600}
.blk .note{margin-top:6px;font-size:13.5px;color:var(--ink2)}
.act{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-top:10px;transition:opacity .14s}
.act.swap{opacity:0}
.act .ask{font-size:13.5px;color:var(--ink2);margin-right:2px}
.fix{display:grid;grid-template-rows:0fr;transition:grid-template-rows .22s ease}
.fix.open{grid-template-rows:1fr}
.fix>div{overflow:hidden}
.fixin{display:grid;gap:10px;padding-top:12px}
.fixrow{display:flex;flex-wrap:wrap;align-items:center;gap:8px;font-size:13.5px;color:var(--ink2)}
.inp{font:inherit;font-size:13.5px;color:var(--ink);background:var(--surface);border:1px solid var(--axis);border-radius:9px;
padding:7px 10px;appearance:none;-webkit-appearance:none;transition:border-color .18s}
.inp:hover{border-color:var(--ink2)}
.inp.t{width:68px;text-align:center;font-variant-numeric:tabular-nums}
.inp.n{flex:1;min-width:180px}
.err{color:var(--stuffy);font-size:13px;min-height:1em}
.said{font-size:13.5px}
.said b{font-weight:600}
.notes ul{margin:8px 0 0;padding-left:18px;color:var(--ink2);max-width:76ch}
.notes li{margin:4px 0}
@media (max-width:560px){.row{grid-template-columns:62px 1fr;padding:3px 4px}.lab{font-size:12px}.card{padding:14px}
h1{font-size:24px}.seglab{display:none}}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
"""

JS = r"""
const D = JSON.parse(document.getElementById('data').textContent);
const KIND = {here:'Someone here', asleep:'Likely asleep', clearing:'Air clearing', empty:'Empty', off:'Pip was off'};
const TRUE = {in:'I was here, awake', asleep:'I was asleep', out:'The room was empty'};
const SAYS = {here:'in', asleep:'asleep', empty:'out'};
let checks = D.checks.slice();
let cur = D.start;
const $ = (s, root) => (root || document).querySelector(s);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const svg = (tag, attrs) => { const e = document.createElementNS('http://www.w3.org/2000/svg', tag); for (const k in attrs) e.setAttribute(k, attrs[k]); return e; };
const two = n => String(n).padStart(2, '0');
const hhmm = m => two(Math.floor(m / 60)) + ':' + two(m % 60);
const dur = m => m >= 60 ? Math.floor(m / 60) + ' h ' + two(m % 60) + ' min' : m + ' min';
const num = n => Math.round(n).toLocaleString('en-US');
const pct = m => (m / 14.4) + '%';

/* tooltip */
const tipBox = $('#tip');
function tip(e, strong, lines) {
  tipBox.textContent = '';
  tipBox.append(el('b', null, strong));
  lines.forEach(t => tipBox.append(el('div', null, t)));
  tipBox.classList.add('show');
  const w = tipBox.offsetWidth, h = tipBox.offsetHeight;
  let x = e.clientX + 14, y = e.clientY - h - 12;
  if (x + w > innerWidth - 8) x = e.clientX - w - 14;
  if (y < 8) y = e.clientY + 16;
  tipBox.style.left = Math.max(8, x) + 'px';
  tipBox.style.top = y + 'px';
}
const hideTip = () => tipBox.classList.remove('show');

/* shared pieces */
function hours() {
  const h = el('div', 'hours');
  [0, 6, 12, 18, 24].forEach(x => { const s = el('span', null, two(x) + (innerWidth < 560 ? '' : ':00')); s.style.left = (x / 24 * 100) + '%'; h.append(s); });
  return h;
}
function tracks(d, big) {
  const box = el('div', 'tracks');
  const top = el('div', 'track');
  d.spells.forEach(s => {
    const b = el('div', 'bar k-stuffy'); b.style.left = pct(s.a); b.style.width = pct(s.b - s.a);
    b.addEventListener('pointermove', e => tip(e, 'Stuffy air', [hhmm(s.a) + ' to ' + hhmm(s.b), num(s.peak) + ' ppm at the peak']));
    b.addEventListener('pointerleave', hideTip);
    top.append(b);
  });
  box.append(top);
  const strip = el('div', 'strip' + (big ? ' big' : ''));
  d.blocks.forEach((b, j) => {
    const s = el('div', 'seg k-' + b.k);
    s.style.left = pct(b.a); s.style.width = pct(b.b - b.a); s.dataset.j = j;
    s.addEventListener('pointermove', e => tip(e, KIND[b.k], [hhmm(b.a) + ' to ' + hhmm(b.b), dur(b.b - b.a) + (b.sure ? ' · ' + b.sure : '')]));
    s.addEventListener('pointerleave', hideTip);
    strip.append(s);
  });
  box.append(strip);
  if (d.truth) {
    const t = el('div', 'track');
    d.truth.in.forEach(r => {
      const b = el('div', 'bar k-truth'); b.style.left = pct(r[0]); b.style.width = pct(r[1] - r[0]);
      b.addEventListener('pointermove', e => tip(e, "Simulator's truth", ['Someone was in the room', hhmm(r[0]) + ' to ' + hhmm(r[1])]));
      b.addEventListener('pointerleave', hideTip);
      t.append(b);
    });
    box.append(t);
  }
  return box;
}

/* legend */
(function () {
  const L = $('#legend');
  const key = (cls, text) => { const k = el('span', 'key'); k.append(el('span', 'sw ' + cls), el('span', null, text)); L.append(k); };
  ['here', 'asleep', 'clearing', 'empty', 'off'].forEach(k => key('k-' + k, KIND[k]));
  key('line k-stuffy', 'Stuffy air, ' + num(D.stuffy) + ' ppm or more');
  if (D.simulated) key('line k-truth', "Simulator's truth: someone was in");
})();

/* every day */
function drawAll() {
  const box = $('#days'); box.textContent = '';
  const axis = el('div', 'row axis'); axis.append(el('span', 'lab'), hours()); box.append(axis);
  D.days.forEach((d, i) => {
    const row = el('div', 'row'); row.tabIndex = 0; row.setAttribute('role', 'button'); row.dataset.i = i;
    row.setAttribute('aria-label', 'Open ' + d.label);
    row.append(el('span', 'lab', d.label), tracks(d, false));
    row.addEventListener('click', () => pick(i));
    row.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(i); } });
    box.append(row);
  });
}
function pick(i, instant) {
  if (i < 0 || i >= D.days.length) return;
  cur = i;
  document.querySelectorAll('#days .row').forEach(r => r.classList.toggle('on', +r.dataset.i === i));
  const box = $('#dayview');
  if (instant) return drawDay();
  box.classList.add('swap');
  setTimeout(() => { drawDay(); box.classList.remove('swap'); }, 170);
}

/* one day */
function drawDay() {
  hideTip();
  const d = D.days[cur], box = $('#dayview'); box.textContent = '';
  const head = el('div', 'dayhead');
  const prev = el('button', 'btn quiet', 'Earlier day'), next = el('button', 'btn quiet', 'Later day');
  prev.disabled = cur === 0; next.disabled = cur === D.days.length - 1;
  prev.addEventListener('click', () => pick(cur - 1)); next.addEventListener('click', () => pick(cur + 1));
  head.append(el('h2', null, d.label), prev, next);
  box.append(head);

  const tot = {};
  d.blocks.forEach(b => tot[b.k] = (tot[b.k] || 0) + b.b - b.a);
  const sum = el('div', 'sum');
  ['here', 'asleep', 'clearing', 'empty', 'off'].forEach(k => {
    if (!tot[k]) return;
    const s = el('span', 'key'); s.append(el('span', 'sw k-' + k), el('span', null, KIND[k] + ' '), el('b', null, dur(tot[k]))); sum.append(s);
  });
  box.append(sum);

  const chart = el('div', 'chart'); box.append(chart);
  const under = el('div', 'under'); under.append(tracks(d, true)); box.append(under);
  drawChart(chart, under, d);
  under.querySelectorAll('.seg').forEach(s => {
    const b = d.blocks[+s.dataset.j];
    if (s.getBoundingClientRect().width > KIND[b.k].length * 7.4 + 16) s.append(el('span', 'seglab', KIND[b.k]));
    s.style.cursor = 'pointer';
    s.addEventListener('pointerenter', () => hot(s.dataset.j, true));
    s.addEventListener('pointerleave', () => hot(s.dataset.j, false));
    s.addEventListener('click', () => { const c = $('.blk[data-j="' + s.dataset.j + '"]'); if (c) c.scrollIntoView({behavior: 'smooth', block: 'center'}); });
  });

  if (d.spells.length) {
    const worst = d.spells.reduce((a, s) => s.peak > a.peak ? s : a);
    const also = d.also;
    const list = a => a.length < 2 ? a.join('') : a.slice(0, -1).join(', ') + ' and ' + a[a.length - 1];
    let t = d.spells.length === 1
      ? 'Stuffy air from ' + hhmm(worst.a) + ' to ' + hhmm(worst.b) + '. It peaked at ' + num(worst.peak) + ' ppm.'
      : 'Stuffy air for ' + dur(d.spells.reduce((n, s) => n + s.b - s.a, 0)) + ', in ' + d.spells.length + ' spells. The highest was '
        + num(worst.peak) + ' ppm, in the spell from ' + hhmm(worst.a) + ' to ' + hhmm(worst.b) + '.';
    if (also.length) t += ' It was also stuffy around ' + (d.spells.length === 1 ? 'this time' : 'these times') + ' on ' + list(also) + '.';
    t += ' Pip can say that it happened, not why.';
    const p = el('p', 'stuffynote'); p.append(el('span', 'tag', 'Stuffy'), document.createTextNode(' ' + t)); box.append(p);
  }

  const list = el('div', 'list');
  d.blocks.forEach((b, j) => list.append(card(d, b, j)));
  box.append(list);
}
function hot(j, on) {
  document.querySelectorAll('#dayview [data-j="' + j + '"]').forEach(n => n.classList.toggle('hot', on));
}

function drawChart(holder, under, d) {
  const W = Math.max(300, holder.clientWidth), H = 262, ml = 46, mr = W < 520 ? 68 : 92, mt = 26, mb = 24;
  const pw = W - ml - mr, ph = H - mt - mb;
  under.style.paddingLeft = ml + 'px'; under.style.paddingRight = mr + 'px';
  const vals = d.co2.filter(v => v != null);
  const bases = d.base.filter(v => v != null);
  const lowest = Math.min(vals.length ? Math.min(...vals) : 400, bases.length ? Math.min(...bases) : 400);
  const top = Math.max(vals.length ? Math.max(...vals) : 600, 700);
  const step = top - lowest > 1600 ? 500 : top - lowest > 700 ? 200 : 100;
  const y0 = Math.floor((lowest - 15) / step) * step, y1 = Math.ceil((top + 20) / step) * step;
  const X = m => ml + m / 1440 * pw, Y = v => mt + (1 - (v - y0) / (y1 - y0)) * ph;
  const s = svg('svg', {viewBox: '0 0 ' + W + ' ' + H, height: H, role: 'img',
    'aria-label': 'CO2 through ' + d.label + '. The list below holds the same reading in words.'});
  const defs = svg('defs', {}), pat = svg('pattern', {id: 'hatch', width: 6, height: 6, patternUnits: 'userSpaceOnUse', patternTransform: 'rotate(45)'});
  pat.append(svg('line', {x1: 0, y1: 0, x2: 0, y2: 6, stroke: 'var(--axis)', 'stroke-width': 1.2})); defs.append(pat); s.append(defs);

  d.blocks.forEach((b, j) => {
    if (b.k === 'empty') return;
    const r = svg('rect', {x: X(b.a), y: mt, width: Math.max(0, X(b.b) - X(b.a)), height: ph,
      fill: b.k === 'off' ? 'url(#hatch)' : 'var(--' + b.k + ')', opacity: b.k === 'off' ? .55 : .13, 'data-j': j});
    s.append(r);
  });
  for (let v = y0; v <= y1; v += step) {
    s.append(svg('line', {x1: ml, x2: ml + pw, y1: Y(v), y2: Y(v), stroke: 'var(--grid)', 'stroke-width': 1}));
    const t = svg('text', {x: ml - 8, y: Y(v) + 4, 'text-anchor': 'end'}); t.textContent = num(v); s.append(t);
  }
  const unit = svg('text', {x: ml, y: 12, class: 'lab2'}); unit.textContent = 'CO2 in the room, ppm'; s.append(unit);
  [0, 3, 6, 9, 12, 15, 18, 21, 24].forEach(h => {
    s.append(svg('line', {x1: X(h * 60), x2: X(h * 60), y1: mt + ph, y2: mt + ph + 4, stroke: 'var(--axis)', 'stroke-width': 1}));
    if (h % 6 === 0 || W > 700) { const t = svg('text', {x: X(h * 60), y: H - 6, 'text-anchor': h === 0 ? 'start' : h === 24 ? 'end' : 'middle'}); t.textContent = two(h) + (W < 520 ? '' : ':00'); s.append(t); }
  });
  s.append(svg('line', {x1: ml, x2: ml + pw, y1: mt + ph, y2: mt + ph, stroke: 'var(--axis)', 'stroke-width': 1}));

  /* the two levels the rule uses: the room's empty level, and the limit above it */
  const stepLine = (add, dash) => {
    let p = '', pen = false;
    d.base.forEach((b, h) => {
      if (b == null) { pen = false; return; }
      p += (pen ? 'L' : 'M') + X(h * 60).toFixed(1) + ' ' + Y(b + add).toFixed(1) + 'L' + X(h * 60 + 60).toFixed(1) + ' ' + Y(b + add).toFixed(1);
      pen = true;
    });
    const a = {d: p, fill: 'none', stroke: 'var(--ink2)', 'stroke-width': 1, opacity: .8};
    if (dash) a['stroke-dasharray'] = '5 4';
    s.append(svg('path', a));
  };
  let lastBase = null; d.base.forEach(b => { if (b != null) lastBase = b; });
  const side = (v, text, nudge) => { const t = svg('text', {x: ml + pw + 8, y: Y(v) + 4 + (nudge || 0), class: 'lab2'}); t.textContent = text; s.append(t); };
  if (lastBase != null) {
    stepLine(0, false); stepLine(D.rule.on, true);
    const gap = Y(lastBase) - Y(lastBase + D.rule.on);
    side(lastBase, 'empty level');
    side(lastBase + D.rule.on, 'here limit', gap < 14 ? gap - 14 : 0);
  }
  if (y1 >= D.stuffy && (lastBase == null || Y(lastBase + D.rule.on) - Y(D.stuffy) > 28)) {
    s.append(svg('line', {x1: ml, x2: ml + pw, y1: Y(D.stuffy), y2: Y(D.stuffy), stroke: 'var(--stuffy)', 'stroke-width': 1, 'stroke-dasharray': '2 4'}));
    side(D.stuffy, 'stuffy');
  }

  let path = '', pen = false;
  d.co2.forEach((v, m) => { if (v == null) { pen = false; return; } path += (pen ? 'L' : 'M') + X(m).toFixed(1) + ' ' + Y(v).toFixed(1); pen = true; });
  s.append(svg('path', {d: path, fill: 'none', stroke: 'var(--ink)', 'stroke-width': 1.8, 'stroke-linejoin': 'round', 'stroke-linecap': 'round'}));
  d.spells.filter(sp => sp.peak === Math.max(...d.spells.map(x => x.peak))).slice(0, 1).forEach(sp => {
    let at = sp.a; for (let m = sp.a; m < sp.b; m++) if (d.co2[m] != null && d.co2[m] >= (d.co2[at] || 0)) at = m;
    s.append(svg('circle', {cx: X(at), cy: Y(d.co2[at]), r: 4, fill: 'var(--stuffy)', stroke: 'var(--surface)', 'stroke-width': 2}));
    const t = svg('text', {x: X(at) + (at > 1200 ? -8 : 8), y: Math.max(mt + 10, Y(d.co2[at]) - 7), 'text-anchor': at > 1200 ? 'end' : 'start', class: 'lab2'});
    t.textContent = num(d.co2[at]) + ' ppm'; s.append(t);
  });

  const cross = svg('line', {y1: mt, y2: mt + ph, stroke: 'var(--ink2)', 'stroke-width': 1, opacity: 0});
  const dot = svg('circle', {r: 4, fill: 'var(--ink)', stroke: 'var(--surface)', 'stroke-width': 2, opacity: 0});
  const hit = svg('rect', {x: ml, y: mt, width: pw, height: ph, fill: 'transparent'});
  s.append(cross, dot, hit);
  hit.addEventListener('pointermove', e => {
    const box = s.getBoundingClientRect();
    let m = Math.round((e.clientX - box.left - ml) / pw * 1440); m = Math.max(0, Math.min(1439, m));
    let near = null; for (let k = 0; k <= 4 && near == null; k++) { if (d.co2[m - k] != null) near = m - k; else if (d.co2[m + k] != null) near = m + k; }
    const blk = d.blocks.find(b => m >= b.a && m < b.b);
    cross.setAttribute('x1', X(m)); cross.setAttribute('x2', X(m)); cross.setAttribute('opacity', .5);
    if (near == null) { dot.setAttribute('opacity', 0); tip(e, 'No reading', [hhmm(m), blk ? KIND[blk.k] : '']); return; }
    const v = d.co2[near], b = d.base[Math.floor(near / 60)];
    dot.setAttribute('cx', X(near)); dot.setAttribute('cy', Y(v)); dot.setAttribute('opacity', 1);
    const lines = [hhmm(near) + (blk ? ' · ' + KIND[blk.k] : '')];
    if (b != null) lines.push(num(Math.abs(v - b)) + ' ppm ' + (v >= b ? 'above' : 'below') + ' the empty level');
    tip(e, num(v) + ' ppm', lines);
  });
  hit.addEventListener('pointerleave', () => { cross.setAttribute('opacity', 0); dot.setAttribute('opacity', 0); hideTip(); });
  holder.textContent = ''; holder.append(s);
}

/* the list, with Right and Wrong? */
function verdict(c) {
  if (c.pip_said === 'clearing') return 'none';
  if ((c.state !== 'out') !== (c.pip_said !== 'empty')) return 'wrong';
  if (c.state !== 'out' && (c.state === 'asleep') !== (c.pip_said === 'asleep')) return 'half';
  return 'right';
}
const VERDICT = {right: 'Pip was right.', wrong: 'Pip was wrong.', none: 'Pip made no call here.',
  half: 'Pip was right that someone was there, and wrong about sleep.'};
function tally() {
  const n = checks.length, t = $('#tally');
  if (!n) { t.textContent = 'Nothing checked yet. Use Right and Wrong? on any block below. Every check becomes a label Pip can be scored against.'; return; }
  const v = checks.map(verdict), called = v.filter(x => x !== 'none').length;
  const right = v.filter(x => x === 'right' || x === 'half').length, half = v.filter(x => x === 'half').length;
  let s = 'You checked ' + n + (n === 1 ? ' block. ' : ' blocks. ');
  if (called) s += 'Pip had someone-or-nobody right on ' + right + ' of ' + called + '. ';
  if (half) s += 'It got the sleep wrong on ' + half + ' of those. ';
  if (n - called) s += (n - called) + (n - called === 1 ? ' was' : ' were') + ' air clearing, where Pip makes no call.';
  t.textContent = s.trim();
}
function badge(k) { const b = el('span', 'badge'); b.append(el('span', 'sw k-' + k), el('span', null, KIND[k])); return b; }
function card(d, b, j) {
  const c = el('article', 'blk'); c.dataset.j = j;
  c.addEventListener('pointerenter', () => hot(j, true)); c.addEventListener('pointerleave', () => hot(j, false));
  const head = el('div', 'blkhead');
  head.append(el('span', 'time', hhmm(b.a) + ' to ' + hhmm(b.b)), badge(b.k), el('span', 'muted', dur(b.b - b.a)));
  c.append(head);
  c.append(el('p', null, b.evidence), el('p', 'rd', b.reading));
  if (b.sure) { const w = el('p', 'why'); w.append(el('b', null, 'How sure: ' + b.sure + '. '), document.createTextNode(b.why)); c.append(w); }
  if (b.cont) c.append(el('p', 'note', b.cont));
  if (b.unusual) { const u = el('p', 'note'); u.append(el('span', 'tag', 'Unusual'), document.createTextNode(' ' + b.unusual)); c.append(u); }
  if (b.k !== 'off') { const a = el('div'); c.append(a); actions(a, d, b); }
  return c;
}
function actions(holder, d, b) {
  holder.textContent = '';
  const act = el('div', 'act'); holder.append(act);
  const mine = checks.filter(c => c.start >= b.s && c.start < b.e);
  if (mine.length) {
    mine.forEach(c => {
      const line = el('span', 'said');
      line.append(el('b', null, 'You said: ' + TRUE[c.state] + '. '),
        document.createTextNode(c.start.slice(11) + ' to ' + c.end.slice(11) + '. ' + VERDICT[verdict(c)] + (c.note ? ' Note: ' + c.note : '')));
      const u = el('button', 'btn quiet', 'Undo'); u.addEventListener('click', () => send('undo', c, holder, d, b));
      act.append(line, u);
    });
    return;
  }
  const day0 = d.date + ' ';
  const whole = state => ({start: b.s, end: b.e, state: state, pip_said: b.k, note: ''});
  if (b.k === 'clearing') {
    act.append(el('span', 'ask', "Pip can't tell. Which was it?"));
    [['in', 'I was here'], ['out', 'The room was empty']].forEach(o => {
      const x = el('button', 'btn', o[1]); x.addEventListener('click', () => send('check', whole(o[0]), holder, d, b)); act.append(x);
    });
    return;
  }
  const right = el('button', 'btn', 'Right'), wrong = el('button', 'btn', 'Wrong?');
  wrong.setAttribute('aria-expanded', 'false');
  right.addEventListener('click', () => send('check', whole(SAYS[b.k]), holder, d, b));
  act.append(right, wrong);

  const fix = el('div', 'fix'), inner = el('div'), box = el('div', 'fixin'); fix.append(inner); inner.append(box); holder.append(fix);
  let chosen = null;
  const r1 = el('div', 'fixrow'); r1.append(el('span', null, 'What was true?'));
  const opts = Object.keys(TRUE).filter(s => s !== SAYS[b.k]).map(s => {
    const x = el('button', 'btn', TRUE[s]); x.setAttribute('aria-pressed', 'false');
    x.addEventListener('click', () => { chosen = s; opts.forEach(o => o.setAttribute('aria-pressed', String(o === x))); });
    r1.append(x); return x;
  });
  const from = el('input', 'inp t'), to = el('input', 'inp t'), note = el('input', 'inp n');
  from.value = hhmm(b.a); to.value = hhmm(b.b); from.setAttribute('aria-label', 'From'); to.setAttribute('aria-label', 'To');
  [from, to].forEach(i => { i.type = 'text'; i.inputMode = 'numeric'; i.maxLength = 5; i.autocomplete = 'off'; });
  note.type = 'text'; note.maxLength = 120; note.placeholder = 'Note, if you want one'; note.setAttribute('aria-label', 'Note');
  const r2 = el('div', 'fixrow'); r2.append(el('span', null, 'From'), from, el('span', null, 'to'), to, note);
  const err = el('div', 'err'); err.setAttribute('role', 'alert');
  const save = el('button', 'btn', 'Save'), cancel = el('button', 'btn quiet', 'Cancel');
  const r3 = el('div', 'fixrow'); r3.append(save, cancel, err);
  box.append(r1, r2, r3);
  const open = on => { fix.classList.toggle('open', on); wrong.setAttribute('aria-expanded', String(on)); wrong.setAttribute('aria-pressed', String(on)); fix.querySelectorAll('button,input').forEach(n => n.tabIndex = on ? 0 : -1); };
  open(false);
  wrong.addEventListener('click', () => open(!fix.classList.contains('open')));
  cancel.addEventListener('click', () => open(false));
  const mins = v => { const m = /^([01]\d|2[0-4]):([0-5]\d)$/.exec(v.trim()); return m ? +m[1] * 60 + +m[2] : null; };
  const stamp = m => { if (m < 1440) return day0 + hhmm(m); const n = new Date(d.date + 'T12:00:00'); n.setDate(n.getDate() + 1);
    return n.getFullYear() + '-' + two(n.getMonth() + 1) + '-' + two(n.getDate()) + ' 00:00'; };
  save.addEventListener('click', () => {
    const a = mins(from.value), z = mins(to.value);
    if (!chosen) { err.textContent = 'Pick what was true.'; return; }
    if (a == null || z == null || z > 1440) { err.textContent = 'Write times like 07:30.'; return; }
    if (a >= z || a < b.a || z > b.b) { err.textContent = 'Keep the times inside ' + hhmm(b.a) + ' to ' + hhmm(b.b) + '.'; return; }
    err.textContent = '';
    send('check', {start: stamp(a), end: stamp(z), state: chosen, pip_said: b.k, note: note.value}, holder, d, b);
  });
}
async function send(what, c, holder, d, b) {
  const act = $('.act', holder); if (act) act.classList.add('swap');
  try {
    if (D.live) {
      const r = await fetch(what, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(c)});
      if (!r.ok) throw new Error(await r.text());
      checks = (await r.json()).checks;
    } else if (what === 'check') {
      checks = checks.filter(x => !(x.start === c.start && x.end === c.end)).concat([c]);
    } else {
      checks = checks.filter(x => !(x.start === c.start && x.end === c.end));
    }
  } catch (e) {
    if (act) act.classList.remove('swap');
    alertLine(holder, 'That did not save. Is room_diary.py still running?');
    return;
  }
  setTimeout(() => { actions(holder, d, b); const n = $('.act', holder); n.classList.add('swap'); requestAnimationFrame(() => requestAnimationFrame(() => n.classList.remove('swap'))); tally(); }, 150);
}
function alertLine(holder, text) { let e = $('.err', holder); if (!e) { e = el('div', 'err'); holder.append(e); } e.textContent = text; }

document.addEventListener('keydown', e => {
  if (e.target.closest('input,button')) return;
  if (e.key === 'ArrowLeft') pick(cur - 1); if (e.key === 'ArrowRight') pick(cur + 1);
});
let again; addEventListener('resize', () => { clearTimeout(again); again = setTimeout(drawDay, 150); });
drawAll(); pick(cur, true); tally();
"""

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>Pip's room diary__TITLE__</title>
<style>__CSS__</style></head>
<body><main class="wrap">
<header>
<h1>Pip's room diary</h1>
<p class="sub">One room, read from its air. Pip logs CO2 once a minute. This page turns that log into days you can read, and says how sure each reading is.</p>
<div class="tags">__TAGS__</div>
<p id="tally"></p>
</header>
<div class="legend" id="legend"></div>
<section class="card">
<h2>Every day Pip logged</h2>
<p class="muted">Each row is one day, midnight to midnight. Pick a day to read it.</p>
<div class="rows" id="days"></div>
</section>
<section class="card"><div id="dayview"></div></section>
<section class="card notes">
<h2>What this page can and can't say</h2>
<ul>__NOTES__</ul>
</section>
<div id="tip" role="status"></div>
</main>
<script id="data" type="application/json">__DATA__</script>
<script>__JS__</script>
</body></html>
"""


def page(data, back=None, back_label=None):
    """back: an address for a link back (to Pip's story, or to the short view), for a hosted copy."""
    rule = data["rule"]
    tags = [f'<span class="tag">{data["span"]}</span>',
            f'<span class="tag">{data["minutes"]:,} minutes logged</span>']
    notes = [
        f"<b>Someone here</b> is the tested rule: CO2 stayed at least {rule['on']:.0f} ppm above the room's empty "
        f"level for {rule['on_min']} minutes. The empty level is the room's own low point over the last 7 days.",
        "<b>Likely asleep</b> and <b>air clearing</b> are reading aids on top of that rule. Their settings were tuned "
        "on the same days they are shown on, so they are not a tested result.",
        f"<b>How sure</b> is not a probability. Sure means any limit from {rule['sure_low']} to {rule['sure_high']} ppm "
        "gives the same answer for that block.",
        "<b>Pip was off</b> means no readings. Nothing is guessed for that time.",
        "Pip can't tell who is in the room, or how many people. It reads breathing, not faces.",
    ]
    if data["simulated"]:
        tags.insert(0, '<span class="tag loud">Simulated room</span>')
        notes.append("This room is <b>simulated</b>. The thin dark line under each day is the simulator's truth: when "
                     "someone was really in. A real room has no such line.")
        notes.append("The simulated sensor is noisier than the real one, so the air never looks steady and "
                     "<b>likely asleep</b> never shows here. Those nights read as someone here.")
    if data["live"]:
        notes.append("Right and Wrong? are saved on this Mac, in data/pip/presence/ground_truth.csv. "
                     "score_level.py scores the rule against them.")
    else:
        tags.append('<span class="tag">Checks on this page are not saved</span>')
        notes.append("This is a demo or a saved copy, so Right and Wrong? work but are forgotten when you close the page.")
    if back:
        label = html.escape(back_label or diary_simple.BACK_LABEL, quote=False)
        tags.insert(0, f'<a class="tag" href="{html.escape(back, quote=True)}">{label}</a>')
    blob = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    return (PAGE.replace("__TITLE__", " (simulated)" if data["simulated"] else "")
            .replace("__TAGS__", "".join(tags)).replace("__NOTES__", "".join(f"<li>{n}</li>" for n in notes))
            .replace("__CSS__", CSS).replace("__JS__", JS).replace("__DATA__", blob))


def build(args, live):
    """Read the log again and make the page. Called on every load, so a new pull shows up on refresh."""
    if args.demo:
        rows = load(SIM)
        stop = rows[0].t + timedelta(days=DEMO_LOAD_DAYS)
        rows = [r for r in rows if r.t < stop]
        data = payload(rows, simulated=True, live=False, truth=sim_truth(), last_days=args.days or DEMO_SHOW_DAYS)
    else:
        data = payload(load(args.log), live=live, checks=load_checks(args.checks) if live else (),
                       last_days=args.days)
    back, label = getattr(args, "back", None), getattr(args, "back_label", None)
    if getattr(args, "simple", False):
        return diary_simple.page(data, back=back, back_label=label, full=getattr(args, "full", None),
                                 fonts=getattr(args, "fonts", None), theme=getattr(args, "theme", None))
    return page(data, back=back, back_label=label)


# ---------- serving it, on this Mac only ----------

def handler(args):
    class Diary(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _ok_caller(self):
            """Only this Mac's own browser, on this page's own address. Another site can't post here."""
            port = self.server.server_port
            local = {f"127.0.0.1:{port}", f"localhost:{port}"}
            origin = self.headers.get("Origin")
            return self.headers.get("Host") in local and (origin is None or origin in {f"http://{h}" for h in local})

        def _send(self, code, kind, body):
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if not self._ok_caller():
                return self._send(403, "text/plain", b"This page only answers on this Mac.")
            if self.path.split("?")[0] != "/":
                return self._send(404, "text/plain", b"Not here.")
            self._send(200, "text/html; charset=utf-8", build(args, live=not args.demo).encode())

        def do_POST(self):
            what = self.path.strip("/")
            size = int(self.headers.get("Content-Length") or 0)
            if (not self._ok_caller() or args.demo or what not in ("check", "undo") or not 0 < size <= 4096
                    or self.headers.get("Content-Type", "").split(";")[0] != "application/json"):
                return self._send(403, "text/plain", b"Not allowed.")
            try:
                body = json.loads(self.rfile.read(size))
                rows = save_check(body, args.checks) if what == "check" else undo_check(body, args.checks)
            except (ValueError, TypeError, AttributeError) as e:
                return self._send(400, "text/plain", str(e).encode())
            self._send(200, "application/json", json.dumps({"checks": rows}).encode())

    return Diary


def main():
    ap = argparse.ArgumentParser(description="Pip's room diary. See the top of this file.")
    ap.add_argument("--demo", action="store_true", help="use the simulated room (no real data)")
    ap.add_argument("--log", type=Path, default=REAL, help="a real Pip log")
    ap.add_argument("--checks", type=Path, default=CHECKS, help="where Right and Wrong? are saved")
    ap.add_argument("--days", type=int, help="show only the last N days")
    ap.add_argument("--export", type=Path, help="write one HTML file and stop")
    ap.add_argument("--simple", action="store_true", help="the short view: a drawn room and a day you can play")
    ap.add_argument("--back", help="add a \"Back to Pip's story\" link to this address (for a hosted copy)")
    ap.add_argument("--back-label", help="other words for that link")
    ap.add_argument("--full", help="with --simple: add a link to the full diary at this address")
    ap.add_argument("--fonts", help="with --simple: a folder with the Geist fonts, for a copy hosted next to Pip's story")
    ap.add_argument("--theme", choices=("light", "dark"), help="with --simple: fix the look (default: the reader's system)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true", help="don't open the browser")
    args = ap.parse_args()
    if not args.demo and not args.log.exists():
        sys.exit(f"No log at {args.log}. Pull one with tools/pull_log.py, or try --demo.")
    if args.export:
        out = args.export.resolve()
        if not args.demo and ROOT in out.parents and PRIVATE not in out.parents:
            sys.exit("A real-room page holds that home's hours. Write it under data/ (private), or outside this repo.")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(build(args, live=False))
        print(f"Wrote {out}" + ("" if args.demo else "  (real room: keep this file private)"))
        return
    server = ThreadingHTTPServer(("127.0.0.1", args.port), handler(args))
    url = f"http://127.0.0.1:{args.port}"
    print(f"Room diary at {url}  (this Mac only). Ctrl+C to stop.")
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
