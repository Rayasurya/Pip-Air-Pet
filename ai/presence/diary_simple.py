"""The short view of Pip's room diary: one room, one sentence, and a day you can play.

room_diary.py makes the working page: every reading, each block's evidence, and Right / Wrong?
checks. That page is for the person who owns the room. This one is for someone who has never
seen Pip. It shows the same reading with far fewer words:

  - a drawn room. The dots are CO2: one dot for about every 14 ppm above the room's empty level.
  - Pip on the wall, with its real screen faces (faces/*.png, stills from the firmware preview).
    Here the face follows CO2 only. The real Pip also reads heat and damp.
  - one sentence that says what Pip makes of the room right now.
  - a bar for the day. Press play, or drag it.

Run from air-pet:

    tools/.venv/bin/python ai/presence/room_diary.py --demo --simple
    tools/.venv/bin/python ai/presence/room_diary.py --demo --simple --export ai/presence/out/room-diary-demo.html

It draws from the same payload() as the full page, so the two can never disagree.
Tests: test_diary_simple.py
"""
import base64
import html
import json
import re
from pathlib import Path

FACES = Path(__file__).resolve().parent / "faces"
GASPING_PPM = 1500                       # firmware/lib/pet/config.h, CO2_GASPING
BACK_LABEL = "Back to Pip's story"
SAFE_URL = re.compile(r"^[A-Za-z0-9._~/:\-]+$")


def slim(data):
    """Only what this page draws. The long evidence text stays on the full page."""
    days = []
    for d in data["days"]:
        one = {"label": d["label"], "date": d["date"], "co2": d["co2"], "base": d["base"],
               "blocks": [{"k": b["k"], "a": b["a"], "b": b["b"], "sure": b["sure"]} for b in d["blocks"]],
               "stuffy": sum(s["b"] - s["a"] for s in d["spells"]),
               "peak": max((s["peak"] for s in d["spells"]), default=None)}
        if "truth" in d:
            one["truth"] = d["truth"]["in"]
        days.append(one)
    return {"days": days, "start": data["start"], "simulated": data["simulated"],
            "stuffy": data["stuffy"], "gasping": GASPING_PPM, "rule": data["rule"]}


def face(name):
    return "data:image/png;base64," + base64.b64encode((FACES / f"{name}.png").read_bytes()).decode()


CSS = r"""
:root{color-scheme:light;--bg:#f2f1ec;--surface:#ffffff;--ink:#121316;--ink2:#464a52;--muted:#5c606a;--line:#e0ded6;
--line2:#c8c5bb;--tint:#e8e6df;--accent:#1f63c9;--here:#eb6834;--asleep:#2a78d6;--clearing:#18a572;--empty:#d6d4ca;
--stuffy:#c23030;--sim-bg:#f1ecfb;--sim-ink:#5a2fb0;--sim-line:#d8cbf3;
--room-day:#fbfaf6;--room-night:#161a21;--line-day:#c8c5bb;--line-night:#313945;--ink-day:#121316;--ink-night:#eef1f5;
--sky-day:#cfe4fb;--sky-night:#0e1730;--dot-day:#8aa0b8;--dot-night:#7f93ab;
--font:"Geist Sans",ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;
--pix:"Geist Pixel",ui-monospace,SFMono-Regular,Menlo,monospace;--ease:cubic-bezier(.23,1,.32,1)}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){color-scheme:dark;--bg:#0b0d10;--surface:#13161b;
--ink:#eef1f5;--ink2:#a3acb9;--muted:#8d96a3;--line:#262b33;--line2:#3a414c;--tint:#1a1e24;--accent:#6cc3ff;
--here:#d95926;--asleep:#3987e5;--clearing:#199e70;--empty:#343a43;--stuffy:#e66767;--sim-bg:#221a38;--sim-ink:#cbb9f6;
--sim-line:#46357a;--room-day:#252a33;--room-night:#0b0d10;--line-day:#454d59;--line-night:#2b323c;--ink-day:#eef1f5;
--sky-day:#7fb2e6;--dot-day:#9fb2c8}}
:root[data-theme="dark"]{color-scheme:dark;--bg:#0b0d10;--surface:#13161b;--ink:#eef1f5;--ink2:#a3acb9;--muted:#8d96a3;
--line:#262b33;--line2:#3a414c;--tint:#1a1e24;--accent:#6cc3ff;--here:#d95926;--asleep:#3987e5;--clearing:#199e70;
--empty:#343a43;--stuffy:#e66767;--sim-bg:#221a38;--sim-ink:#cbb9f6;--sim-line:#46357a;--room-day:#252a33;
--room-night:#0b0d10;--line-day:#454d59;--line-night:#2b323c;--ink-day:#eef1f5;--sky-day:#7fb2e6;--dot-day:#9fb2c8}
*,*::before,*::after{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 var(--font);-webkit-font-smoothing:antialiased;overflow-x:clip}
.wrap{max-width:1080px;margin:0 auto;padding:20px 16px 80px}
p,h1,h2{margin:0}
.pix{font-family:var(--pix);font-weight:400;font-variant-numeric:tabular-nums;letter-spacing:0}
:focus-visible{outline:2px solid var(--accent);outline-offset:3px}

.top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px;min-height:40px}
.pill{display:inline-flex;align-items:center;gap:8px;font:inherit;font-size:14px;font-weight:600;line-height:1;
padding:11px 16px;border-radius:999px;border:1px solid var(--line2);background:var(--surface);color:var(--ink);
text-decoration:none;cursor:pointer;transition:background .2s,color .2s,border-color .2s}
.pill:hover{background:var(--tint)}
.pill:active{background:var(--ink);color:var(--surface);border-color:var(--ink)}
.tag{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:600;line-height:1;padding:7px 10px;
border-radius:999px;background:var(--sim-bg);color:var(--sim-ink);border:1px solid var(--sim-line);white-space:nowrap}
.tag::before{content:"";width:7px;height:7px;border-radius:50%;border:1.6px solid currentColor}
h1{margin-top:26px;font-size:clamp(1.9rem,1.15rem + 3vw,3.2rem);line-height:1.05;letter-spacing:-.035em;font-weight:680;
max-width:17ch;text-wrap:balance}
.lede{margin-top:14px;color:var(--ink2);font-size:1.0625rem;max-width:54ch}
h2{font-size:1.25rem;letter-spacing:-.015em;font-weight:650;margin-top:44px}
.sub{color:var(--ink2);font-size:.9375rem;margin-top:4px;max-width:60ch}

/* the stage: the room, and what Pip makes of it */
.stage{margin-top:26px;background:var(--surface);border:1px solid var(--line);border-radius:26px;padding:clamp(14px,2.4vw,26px)}
.now{display:grid;grid-template-columns:minmax(0,1.3fr) minmax(0,1fr);gap:clamp(16px,3.4vw,40px);align-items:center;
transition:opacity .22s}
.now.out{opacity:0}
.scene{position:relative;--room:var(--room-day);--rline:var(--line-day);--rink:var(--ink-day);--sky:var(--sky-day);
--orb:#f2b233;--dot:var(--dot-day)}
.scene[data-sky="night"]{--room:var(--room-night);--rline:var(--line-night);--rink:var(--ink-night);--sky:var(--sky-night);
--orb:#e9edf5;--dot:var(--dot-night)}
.scene svg{display:block;width:100%;height:auto}
.rm{fill:var(--room);stroke:var(--rline);stroke-width:2;transition:fill 1.1s,stroke 1.1s}
.floor,.frame{fill:none;stroke:var(--rline);stroke-width:2;transition:stroke 1.1s}
.sky{fill:var(--sky);transition:fill 1.1s}
.orb{fill:var(--orb);transition:fill 1.1s}
.star{fill:#e9edf5;opacity:0;transition:opacity 1.1s}
.scene[data-sky="night"] .star{opacity:.9}
#dots circle{fill:var(--dot);opacity:0;transition:opacity .7s,fill 1.1s;animation:drift var(--t,7s) ease-in-out infinite alternate}
#dots circle.on{opacity:.74}
@keyframes drift{to{transform:translate(var(--dx,4px),var(--dy,-4px))}}
.pipbody{fill:#0b0d10;stroke:var(--rline);stroke-width:2;transition:stroke 1.1s}
.face{opacity:0;transition:opacity .45s;image-rendering:pixelated}
.face.on{opacity:1}
.piplab{font:600 13px var(--font);fill:var(--rink);opacity:.62;transition:fill 1.1s}
.who{opacity:0;transition:opacity .5s}
.who.on{opacity:1}
#pHere circle,#pHere rect{fill:var(--here)}
#pHere rect{transform-box:fill-box;transform-origin:50% 100%;animation:breathe 3.6s ease-in-out infinite}
@keyframes breathe{50%{transform:scale(1.035,1.02)}}
#pAsleep circle,#pAsleep rect{fill:var(--asleep)}
#pGhost circle,#pGhost rect{fill:none;stroke:var(--clearing);stroke-width:2.6;stroke-dasharray:7 6;vector-effect:non-scaling-stroke}
.mark{font:700 24px var(--font);fill:var(--rink);transition:fill 1.1s}
.hatchline{stroke:var(--rline);stroke-width:2;transition:stroke 1.1s}
#veil{opacity:0;transition:opacity .5s}
#veil.on{opacity:.9}
.ppm{position:absolute;left:5.5%;bottom:4.2%;display:flex;align-items:baseline;gap:8px;color:var(--rink);
transition:color 1.1s,opacity .4s}
.ppm.off{opacity:0}
.ppm b{font-size:clamp(1.2rem,.85rem + 1.7vw,1.9rem);line-height:1}
.ppm span{font-size:.8125rem;opacity:.72}

.read{min-width:0}
.when{display:flex;align-items:baseline;gap:8px;color:var(--muted)}
.when b{font-size:clamp(2.3rem,1.5rem + 2.8vw,3.5rem);line-height:1;color:var(--ink)}
.when .ap{font-size:1.0625rem;font-weight:600;color:var(--ink)}
.when .dn{margin-left:auto;font-size:.875rem;text-align:right}
.fade{transition:opacity .16s}
.fade.out{opacity:0}
.say{display:flex;gap:12px;align-items:flex-start;margin-top:18px;font-size:clamp(1.5rem,1.05rem + 1.7vw,2.15rem);line-height:1.12;
letter-spacing:-.028em;font-weight:660;min-height:1.12em}
.say i{flex:none;width:.6em;height:.6em;border-radius:.19em;margin-top:.27em;background:var(--empty);transition:background .4s}
.why{margin-top:10px;color:var(--ink2);min-height:3.1em}
.chips{display:flex;flex-wrap:wrap;gap:8px;min-height:30px;margin-top:10px}
.chip{display:inline-flex;align-items:center;font-size:12.5px;font-weight:600;line-height:1;padding:7px 11px;border-radius:999px;
border:1px solid var(--line2);color:var(--ink);background:var(--surface);transition:opacity .25s}
.chip.hide{opacity:0}
.chip.bad{border-color:var(--stuffy)}
.real{padding-top:12px;font-size:.9375rem;color:var(--ink2);min-height:3.6em}
.real b{color:var(--ink);font-weight:650}

.ctl{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px 16px;margin-top:clamp(16px,2.6vw,26px)}
.play{min-width:126px;justify-content:center;background:var(--ink);color:var(--surface);border-color:var(--ink)}
.play:hover{background:var(--ink2);border-color:var(--ink2)}
.play:active{background:var(--surface);color:var(--ink);border-color:var(--ink)}
.play svg{width:13px;height:13px;flex:none}
.play path,.play rect{fill:currentColor}
.play .i-play,.play .i-pause{transition:opacity .2s}
.play .i-pause,.play[data-on="1"] .i-play{opacity:0}
.play[data-on="1"] .i-pause{opacity:1}
.switch{display:inline-flex;align-items:center;gap:10px;font:inherit;font-size:14px;font-weight:600;color:var(--ink);
background:none;border:0;padding:8px 2px;cursor:pointer;border-radius:999px}
.switch .box{position:relative;flex:none;width:40px;height:24px;border-radius:999px;background:var(--line2);transition:background .25s}
.switch .box::after{content:"";position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;
background:var(--surface);transition:transform .28s var(--ease)}
.switch[aria-checked="true"] .box{background:var(--ink)}
.switch[aria-checked="true"] .box::after{transform:translateX(16px)}

.track{position:relative;height:46px;margin-top:14px;border-radius:13px;cursor:pointer;touch-action:none;user-select:none;
-webkit-user-select:none}
.segs{position:absolute;inset:0;border-radius:13px;overflow:hidden;background:var(--tint)}
.seg{position:absolute;top:0;bottom:0;box-shadow:inset 1px 0 0 var(--surface),inset -1px 0 0 var(--surface)}
.k-here{background:var(--here)}.k-asleep{background:var(--asleep)}.k-clearing{background:var(--clearing)}
.k-empty{background:var(--empty)}.k-stuffy{background:var(--stuffy)}
.k-off{background:repeating-linear-gradient(135deg,transparent 0 4px,var(--line2) 4px 5.5px);box-shadow:none}
.head{position:absolute;top:-7px;bottom:-7px;left:0;width:0;pointer-events:none}
.head::before{content:"";position:absolute;left:-4px;top:0;bottom:0;width:8px;border-radius:4px;background:var(--ink);
border:2px solid var(--surface)}
.head::after{content:"";position:absolute;left:-11px;top:-11px;width:22px;height:22px;border-radius:50%;background:var(--ink);
border:3px solid var(--surface)}
.fold{display:grid;grid-template-rows:0fr;transition:grid-template-rows .34s var(--ease)}
.fold.open{grid-template-rows:1fr}
.fold>div{overflow:hidden;min-height:0}
.trow{position:relative;height:8px;margin-top:10px;border-radius:4px;background:var(--tint)}
.trow i{position:absolute;top:0;bottom:0;border-radius:4px;background:var(--ink)}
.hrs{position:relative;height:18px;margin-top:10px;font-size:12px;color:var(--muted);font-variant-numeric:tabular-nums}
.hrs span{position:absolute;transform:translateX(-50%);white-space:nowrap}
.hrs span:first-child{transform:none}.hrs span:last-child{transform:translateX(-100%)}
.tcap{padding-top:10px;font-size:.9375rem;color:var(--ink2);max-width:74ch}
.tcap b{color:var(--ink);font-weight:650}
.legend{display:flex;flex-wrap:wrap;gap:6px 18px;margin-top:12px;font-size:13.5px;color:var(--ink2)}
.key{display:inline-flex;align-items:center;gap:7px}
.sw{width:12px;height:12px;border-radius:4px;flex:none}
.sw.k-off{border:1px solid var(--line2)}

/* the day in short */
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin-top:14px;transition:opacity .22s}
.tiles.out{opacity:0}
.tile{background:var(--surface);border:1px solid var(--line);border-radius:18px;padding:14px 16px 16px}
.tile .lab{display:flex;align-items:center;gap:8px;font-size:13.5px;color:var(--ink2)}
.tile b{display:block;margin-top:10px;font-size:1.5rem;line-height:1.1}
.tile small{display:block;margin-top:5px;font-size:12.5px;color:var(--muted)}

/* other days */
.rail{display:grid;grid-template-columns:repeat(7,minmax(0,1fr));gap:10px;margin-top:14px}
.dayc{font:inherit;text-align:left;background:var(--surface);border:1px solid var(--line);border-radius:14px;padding:10px 12px 12px;
cursor:pointer;color:var(--ink);transition:border-color .2s,background .2s,color .2s}
.dayc:hover{background:var(--tint);border-color:var(--line2)}
.dayc:active{background:var(--ink);color:var(--surface);border-color:var(--ink)}
.dayc:active span{color:inherit}
.dayc[aria-pressed="true"]{border-color:var(--ink);box-shadow:inset 0 0 0 1px var(--ink)}
.dayc b{display:block;font-size:14px;font-weight:650}
.dayc span{display:block;font-size:12.5px;color:var(--muted);transition:color .2s}
.mini{position:relative;height:8px;border-radius:4px;overflow:hidden;margin-top:9px;background:var(--tint)}
.mini .seg{box-shadow:none}

/* the day, stretch by stretch */
.more{margin-top:14px}
.more .pill svg{width:12px;height:12px;transition:transform .3s var(--ease)}
.more .pill[aria-expanded="true"] svg{transform:rotate(180deg)}
.rows{display:grid;gap:6px;padding-top:12px}
.mrow{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(0,1fr) 92px;gap:10px;align-items:center;font:inherit;font-size:.9375rem;
text-align:left;background:var(--surface);border:1px solid var(--line);border-radius:12px;padding:10px 14px;color:var(--ink);
cursor:pointer;transition:border-color .2s,background .2s,color .2s}
.mrow:hover{background:var(--tint);border-color:var(--line2)}
.mrow:active{background:var(--ink);color:var(--surface);border-color:var(--ink)}
.mrow:active .dur{color:inherit}
.mrow .t{font-variant-numeric:tabular-nums}
.mrow .dur{text-align:right;color:var(--muted);font-size:13.5px;font-variant-numeric:tabular-nums;transition:color .2s}

/* how to read it */
.keylist{display:grid;gap:10px;margin:14px 0 0;padding:0;list-style:none;max-width:70ch}
.keylist li{display:grid;grid-template-columns:12px minmax(0,1fr);gap:12px;align-items:baseline;color:var(--ink2)}
.keylist .sw{transform:translateY(1px)}
.keylist b{color:var(--ink);font-weight:650}
.fine{margin-top:16px;color:var(--ink2);font-size:.9375rem;max-width:70ch}
.foot{display:flex;flex-wrap:wrap;align-items:center;gap:10px 14px;margin-top:40px}
.foot p{color:var(--ink2);font-size:.9375rem;flex:1 1 260px;max-width:52ch}

@media (max-width:820px){
.now{grid-template-columns:minmax(0,1fr)}
.say{min-height:0}
.rail{grid-auto-flow:column;grid-template-columns:none;grid-auto-columns:108px;overflow-x:auto;scroll-snap-type:x proximity;
margin-inline:-16px;padding:2px 16px 8px;scrollbar-width:none}
.rail::-webkit-scrollbar{display:none}
.dayc{scroll-snap-align:start}
.mrow{grid-template-columns:minmax(0,1fr) auto;row-gap:2px}
.mrow .t{grid-column:1 / -1;font-weight:600}
.stage{border-radius:22px}
}
@media (prefers-reduced-motion:reduce){*,*::before,*::after{transition:none!important;animation:none!important}}
"""

JS = r"""
const D = JSON.parse(document.getElementById('data').textContent);
const NAME = {here: 'Someone here', asleep: 'Likely asleep', clearing: 'Air clearing', empty: 'Empty', off: 'Pip was off'};
const SAY = {here: 'Someone is here.', asleep: 'Someone is likely asleep.', clearing: 'The air is clearing.',
  empty: 'Nobody is here.', off: 'Pip was off.'};
const NS = 'http://www.w3.org/2000/svg';
const DAY_MS = 38000;                      /* one day plays in 38 seconds */
const PPM_PER_DOT = 14, DOTS = 64;
const calm = matchMedia('(prefers-reduced-motion: reduce)').matches;
const $ = id => document.getElementById(id);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const num = n => Math.round(n).toLocaleString('en-US');
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function clock(m) { m = clamp(Math.floor(m), 0, 1439); const h = Math.floor(m / 60); return [(h % 12 || 12) + ':' + String(m % 60).padStart(2, '0'), h < 12 ? 'am' : 'pm']; }
function at(m) { if (m <= 0 || m >= 1440) return 'midnight'; if (m === 720) return 'noon'; const c = clock(m); return c[0] + ' ' + c[1]; }
function span(n) { const h = Math.floor(n / 60), m = n % 60; return h ? (m ? h + ' h ' + m + ' min' : h + ' h') : m + ' min'; }

let day = D.start, pos = 0, playing = false, last = 0, shown = null, truthOn = false, ended = false, drag = false;
const cache = {};

/* the dots: the same places every time, so the room fills the same way */
const dots = [];
(function () {
  let s = 11; const rnd = () => (s = s * 16807 % 2147483647) / 2147483647;
  const g = $('dots');
  while (dots.length < DOTS) {
    const x = 26 + rnd() * 508, y = 26 + rnd() * 236;
    if (x > 32 && x < 184 && y > 36 && y < 156) continue;            /* the window */
    if (x > 378 && x < 530 && y > 38 && y < 152) continue;           /* Pip */
    const c = document.createElementNS(NS, 'circle');
    c.setAttribute('cx', x.toFixed(1)); c.setAttribute('cy', y.toFixed(1)); c.setAttribute('r', (2.6 + rnd() * 1.6).toFixed(1));
    c.style.setProperty('--dx', (rnd() * 14 - 7).toFixed(1) + 'px'); c.style.setProperty('--dy', (rnd() * 14 - 7).toFixed(1) + 'px');
    c.style.setProperty('--t', (5 + rnd() * 5).toFixed(1) + 's'); c.style.animationDelay = (-rnd() * 6).toFixed(1) + 's';
    g.append(c); dots.push(c);
  }
})();

function info(i) {
  if (cache[i]) return cache[i];
  const d = D.days[i], inn = new Uint8Array(1440), kind = new Array(1440), tot = {};
  (d.truth || []).forEach(r => inn.fill(1, r[0], r[1]));
  let right = 0, wrong = 0, none = 0;
  d.blocks.forEach((b, j) => {
    tot[b.k] = (tot[b.k] || 0) + b.b - b.a;
    for (let m = b.a; m < b.b; m++) {
      kind[m] = j;
      if (b.k === 'off') continue;
      if (b.k === 'clearing') none++; else if ((b.k !== 'empty') === !!inn[m]) right++; else wrong++;
    }
  });
  return cache[i] = {inn, kind, tot, right, wrong, none};
}
function reading(d, m) {
  let v = null;
  for (let k = 0; k <= 4 && v == null; k++) { if (d.co2[m - k] != null) v = d.co2[m - k]; else if (d.co2[m + k] != null) v = d.co2[m + k]; }
  const h = Math.floor(m / 60); let b = d.base[h];
  for (let k = 1; k < 24 && b == null; k++) { if (d.base[h - k] != null) b = d.base[h - k]; else if (d.base[h + k] != null) b = d.base[h + k]; }
  return [v, b];
}
function why(k, v, b) {
  if (k === 'off') return 'No readings, so nothing is guessed.';
  if (k === 'asleep') return 'CO2 is high and steady, and it is night. Asleep is a guess.';
  if (k === 'clearing') return "CO2 is falling. A window opened, or the room emptied. The air alone can't tell which.";
  const over = v != null && b != null ? Math.round((v - b) / 10) * 10 : null;
  if (k === 'empty') return over != null && over > 20 ? 'CO2 is only about ' + num(over) + " ppm above this room's empty level." : "CO2 is at this room's empty level.";
  return over != null ? 'CO2 is about ' + num(over) + " ppm above this room's empty level." : "CO2 is well above this room's empty level.";
}
function sure(b) {
  if (b.k === 'off') return '';
  if (b.k === 'clearing') return "Pip can't tell";
  return 'Pip is ' + (b.sure || 'not sure') + (b.k === 'asleep' ? ' someone is here' : '');
}
function real(k, inn) {
  if (k === 'off') return ['Pip was off, so there is nothing to check.', ''];
  const t = inn ? 'In the simulation, someone was in the room.' : 'In the simulation, the room was empty.';
  if (k === 'clearing') return [t, 'Pip made no call.'];
  return [t, (k !== 'empty') === !!inn ? 'Pip is right.' : 'Pip is wrong.'];
}
function swap(node, fn) {
  if (calm) return fn();
  node.classList.add('out');
  setTimeout(() => { fn(); node.classList.remove('out'); }, 160);
}

function render(force) {
  const d = D.days[day], I = info(day), m = clamp(Math.floor(pos), 0, 1439);
  const b = d.blocks[I.kind[m]] || d.blocks[d.blocks.length - 1], k = b.k;
  const rv = reading(d, m), v = k === 'off' ? null : rv[0], base = rv[1];
  const c = clock(m);
  $('clock').textContent = c[0]; $('ap').textContent = c[1];
  $('head').style.left = (pos / 1439 * 100) + '%';
  const tr = $('track');
  tr.setAttribute('aria-valuenow', m); tr.setAttribute('aria-valuetext', c[0] + ' ' + c[1] + ', ' + NAME[k]);

  /* the room */
  const night = m < 360 || m >= 1110, sc = $('scene');
  sc.dataset.sky = night ? 'night' : 'day';
  const t = night ? ((m - 1110 + 1440) % 1440) / 690 : (m - 360) / 750, orb = $('orb');
  orb.setAttribute('cx', (56 + t * 104).toFixed(1)); orb.setAttribute('cy', (150 - Math.sin(Math.PI * t) * 76).toFixed(1));
  orb.setAttribute('opacity', clamp(Math.sin(Math.PI * t) * 5, 0, 1).toFixed(2));
  const n = v != null && base != null ? clamp(Math.round((v - base) / PPM_PER_DOT), 0, DOTS) : 0;
  dots.forEach((dot, i) => dot.classList.toggle('on', i < n));
  const mood = v == null ? '' : v >= D.gasping ? 'gasping' : v >= D.stuffy ? 'stuffy' : 'happy';
  ['happy', 'stuffy', 'gasping'].forEach(f => $('f-' + f).classList.toggle('on', f === mood));
  $('pHere').classList.toggle('on', k === 'here'); $('pAsleep').classList.toggle('on', k === 'asleep');
  $('pGhost').classList.toggle('on', k === 'clearing'); $('veil').classList.toggle('on', k === 'off');
  $('ppm').classList.toggle('off', v == null);
  if (v != null) $('ppmv').textContent = num(v);
  $('alt').textContent = 'A drawing of the room at ' + c[0] + ' ' + c[1] + '. ' + SAY[k] + (v != null ? ' CO2 is ' + num(v) + ' ppm.' : '');

  /* the words */
  const stuffy = v != null && v >= D.stuffy, inn = I.inn[m], rl = real(k, inn);
  const words = () => {
    $('sayt').textContent = SAY[k]; $('sw').style.background = k === 'off' ? 'var(--line2)' : 'var(--' + k + ')';
    $('why').textContent = why(k, v, base);
    const s = sure(b); $('sure').textContent = s || ' '; $('sure').classList.toggle('hide', !s);
  };
  if (shown !== k && !force && shown != null) { swap($('saybox'), words); } else words();
  shown = k;
  $('stuffy').classList.toggle('hide', !stuffy);
  $('realt').textContent = rl[0] + ' '; $('realv').textContent = rl[1];
}

/* playing */
function setPlaying(on, atEnd) {
  playing = on; ended = !!atEnd;
  const p = $('play'); p.dataset.on = on ? 1 : 0;
  $('playt').textContent = on ? 'Pause' : ended ? 'Play again' : 'Play the day';
  if (on) { last = performance.now(); requestAnimationFrame(frame); }
}
function frame(t) {
  if (!playing) return;
  pos += Math.min(120, t - last) * 1440 / DAY_MS; last = t;
  if (pos >= 1439) { pos = 1439; render(); return setPlaying(false, true); }
  render();
  requestAnimationFrame(frame);
}
$('play').addEventListener('click', () => {
  if (playing) return setPlaying(false);
  if (pos >= 1439) pos = 0;
  setPlaying(true);
});

/* dragging the bar */
const track = $('track');
function seek(e) { const r = track.getBoundingClientRect(); pos = clamp((e.clientX - r.left) / r.width, 0, 1) * 1439; ended = false; render(); }
track.addEventListener('pointerdown', e => { drag = true; track.setPointerCapture(e.pointerId); setPlaying(false); seek(e); });
track.addEventListener('pointermove', e => { if (drag) seek(e); });
['pointerup', 'pointercancel'].forEach(n => track.addEventListener(n, () => { drag = false; }));
track.addEventListener('keydown', e => {
  const step = {ArrowLeft: -5, ArrowRight: 5, ArrowDown: -5, ArrowUp: 5, PageDown: -60, PageUp: 60}[e.key];
  if (e.key === 'Home') pos = 0; else if (e.key === 'End') pos = 1439; else if (step) pos = clamp(pos + step * (e.shiftKey ? 12 : 1), 0, 1439); else return;
  e.preventDefault(); setPlaying(false); render();
});

/* what really happened: only a simulation knows */
function truth(on) {
  truthOn = on;
  const s = $('truth'); if (s) s.setAttribute('aria-checked', String(on));
  ['trowf', 'tcapf', 'realf'].forEach(id => { const f = $(id); f.classList.toggle('open', on); f.firstElementChild.inert = !on; });
}
if ($('truth')) $('truth').addEventListener('click', () => truth(!truthOn));

/* one day */
function segs(holder, d) {
  holder.textContent = '';
  d.blocks.forEach(b => { const s = el('i', 'seg k-' + b.k); s.style.left = b.a / 14.4 + '%'; s.style.width = (b.b - b.a) / 14.4 + '%'; holder.append(s); });
}
function drawDay() {
  const d = D.days[day], I = info(day);
  $('dn').textContent = d.label;
  segs($('segs'), d);
  const tr = $('trow'); tr.textContent = '';
  (d.truth || []).forEach(r => { const b = el('i'); b.style.left = r[0] / 14.4 + '%'; b.style.width = (r[1] - r[0]) / 14.4 + '%'; tr.append(b); });
  const cap = $('tcap'); cap.textContent = '';
  if (d.truth) {
    cap.append(document.createTextNode('The line under the bar is when someone was really in the room. On this day Pip was right for '),
      el('b', null, span(I.right)), document.createTextNode(', wrong for '), el('b', null, span(I.wrong)),
      document.createTextNode(', and made no call for '), el('b', null, span(I.none)), document.createTextNode('.'));
  }

  const tiles = $('tiles'); tiles.textContent = '';
  const tile = (cls, label, value, note) => {
    const t = el('div', 'tile'), l = el('div', 'lab'); l.append(el('span', 'sw ' + cls), el('span', null, label));
    t.append(l, el('b', 'pix', value)); if (note) t.append(el('small', null, note)); tiles.append(t);
  };
  [['here', 'Someone here'], ['asleep', 'Likely asleep'], ['clearing', "Pip couldn't tell"], ['empty', 'Empty'], ['off', 'Pip was off']]
    .forEach(x => { if (I.tot[x[0]]) tile('k-' + x[0], x[1], span(I.tot[x[0]]), x[0] === 'clearing' ? 'The air was clearing' : x[0] === 'off' ? 'Nothing is guessed' : ''); });
  if (d.stuffy) tile('k-stuffy', 'Stuffy air', span(d.stuffy), 'Highest: ' + num(d.peak) + ' ppm');

  const rows = $('rows'); rows.textContent = '';
  d.blocks.forEach(b => {
    const r = el('button', 'mrow'); r.type = 'button';
    const name = el('span', 'key'); name.append(el('span', 'sw k-' + b.k), el('span', null, NAME[b.k]));
    r.append(el('span', 't', at(b.a) + ' to ' + at(b.b)), name, el('span', 'dur', span(b.b - b.a)));
    r.setAttribute('aria-label', 'Go to ' + at(b.a) + ', ' + NAME[b.k]);
    r.addEventListener('click', () => { setPlaying(false); pos = b.a; ended = false; render(); $('stage').scrollIntoView({behavior: calm ? 'auto' : 'smooth', block: 'start'}); });
    rows.append(r);
  });
  $('moret').textContent = 'See this day in ' + d.blocks.length + ' parts';
  document.querySelectorAll('.dayc').forEach(c => c.setAttribute('aria-pressed', String(+c.dataset.i === day)));
}
function pick(i) {
  if (i === day && !ended && pos < 1439) { pos = 0; shown = null; render(true); if (!calm) setPlaying(true); return; }
  setPlaying(false);
  const go = () => { day = i; pos = 0; shown = null; drawDay(); render(true); };
  if (calm) return go();
  $('now').classList.add('out'); $('tiles').classList.add('out');
  setTimeout(() => { go(); $('now').classList.remove('out'); $('tiles').classList.remove('out'); setPlaying(true); }, 230);
}

/* the parts that never change */
(function () {
  const rail = $('rail');
  D.days.forEach((d, i) => {
    const c = el('button', 'dayc'); c.type = 'button'; c.dataset.i = i;
    const p = d.label.split(' ');
    const mini = el('span', 'mini'); segs(mini, d);
    c.append(el('b', null, p[0]), el('span', null, p.slice(1).join(' ')), mini);
    c.setAttribute('aria-label', 'Play ' + d.label);
    c.addEventListener('click', () => { pick(i); if (innerWidth < 820) $('stage').scrollIntoView({behavior: calm ? 'auto' : 'smooth', block: 'start'}); });
    rail.append(c);
  });
  const hrs = $('hrs');
  [[0, '12 am'], [6, '6 am'], [12, 'noon'], [18, '6 pm'], [24, '12 am']].forEach(x => { const s = el('span', null, x[1]); s.style.left = x[0] / 24 * 100 + '%'; hrs.append(s); });
  const seen = {}; D.days.forEach(d => d.blocks.forEach(b => seen[b.k] = 1));
  const lg = $('legend');
  ['here', 'asleep', 'clearing', 'empty', 'off'].forEach(k => {
    if (!seen[k]) { const li = $('key-' + k); if (li) li.remove(); return; }
    const x = el('span', 'key'); x.append(el('span', 'sw k-' + k), el('span', null, k === 'clearing' ? "Air clearing: Pip can't tell" : NAME[k])); lg.append(x);
  });
  const more = $('more'), fold = $('rowsf');
  fold.firstElementChild.inert = true;
  more.addEventListener('click', () => { const on = more.getAttribute('aria-expanded') !== 'true'; more.setAttribute('aria-expanded', String(on)); fold.classList.toggle('open', on); fold.firstElementChild.inert = !on; });
})();

/* a link can open on one moment: #d=3&m=615&truth  (day number, minute of the day, and the truth line) */
const H = new URLSearchParams(location.hash.slice(1));
if (H.has('d')) day = clamp(Math.floor(+H.get('d')) || 0, 0, D.days.length - 1);
if (H.has('m')) pos = clamp(+H.get('m') || 0, 0, 1439);
drawDay(); truth(H.has('truth') && !!$('truth')); render(true);
if (H.has('parts')) $('more').click();
if (!calm && !H.has('m')) setTimeout(() => { if (!playing && pos === 0 && !drag) setPlaying(true); }, 700);
"""

PAGE = """<!doctype html>
<html lang="en"__THEME__><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>Pip's room diary__TITLE__</title>
<style>__FONTS____CSS__</style></head>
<body><main class="wrap">
<div class="top">__BACK____TAG__</div>
<h1>One day in a room, read from its air.</h1>
<p class="lede">Pip reads a room's CO2 once a minute. Press play to watch a day go by, or drag the bar.__SIMLEDE__</p>

<section class="stage" id="stage" aria-label="The room, minute by minute">
<div class="now" id="now">
<div class="scene" id="scene" data-sky="night">
<svg viewBox="0 0 560 340" role="img" aria-labelledby="alt"><title id="alt">A drawing of the room.</title>
<defs><clipPath id="win"><rect x="46" y="50" width="124" height="92" rx="8"/></clipPath>
<pattern id="hatch" width="9" height="9" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line class="hatchline" x1="0" y1="0" x2="0" y2="9"/></pattern></defs>
<rect class="rm" x="6" y="6" width="548" height="328" rx="24"/>
<line class="floor" x1="6" y1="276" x2="554" y2="276"/>
<rect class="sky" x="46" y="50" width="124" height="92" rx="8"/>
<g clip-path="url(#win)"><circle class="star" cx="70" cy="72" r="1.8"/><circle class="star" cx="132" cy="64" r="1.5"/><circle class="star" cx="150" cy="104" r="1.8"/><circle class="star" cx="92" cy="118" r="1.4"/>
<circle class="orb" id="orb" r="12" cx="108" cy="96"/></g>
<rect class="frame" x="46" y="50" width="124" height="92" rx="8"/><line class="frame" x1="108" y1="50" x2="108" y2="142"/>
<g id="dots"></g>
<rect class="pipbody" x="392" y="50" width="124" height="74" rx="12"/>
<image class="face" id="f-happy" x="398" y="59" width="112" height="56" href="__HAPPY__"/>
<image class="face" id="f-stuffy" x="398" y="59" width="112" height="56" href="__STUFFY__"/>
<image class="face" id="f-gasping" x="398" y="59" width="112" height="56" href="__GASPING__"/>
<text class="piplab" x="454" y="144" text-anchor="middle">Pip</text>
<g class="who" id="pHere" transform="translate(280 221) scale(1.45)"><circle cx="0" cy="-38" r="14"/><rect x="-18" y="-20" width="36" height="58" rx="16"/></g>
<g class="who" id="pGhost"><g transform="translate(280 221) scale(1.45)"><circle cx="0" cy="-38" r="14"/><rect x="-18" y="-20" width="36" height="58" rx="16"/></g><text class="mark" x="316" y="158">?</text></g>
<g class="who" id="pAsleep"><rect x="226" y="236" width="136" height="40" rx="19"/><circle cx="204" cy="254" r="19"/><text class="mark" x="222" y="214">z</text><text class="mark" x="240" y="194" style="font-size:19px">z</text></g>
<rect id="veil" x="7" y="7" width="546" height="326" rx="23" fill="url(#hatch)"/>
</svg>
<p class="ppm" id="ppm"><b class="pix" id="ppmv">0</b><span>ppm of CO2</span></p>
</div>
<div class="read">
<p class="when"><b class="pix" id="clock">12:00</b><span class="ap" id="ap">am</span><span class="dn" id="dn"></span></p>
<div class="fade" id="saybox">
<p class="say"><i id="sw"></i><span id="sayt"></span></p>
<p class="why" id="why"></p>
<div class="chips"><span class="chip hide" id="sure"></span><span class="chip bad hide" id="stuffy">Stuffy air</span></div>
</div>
<div class="fold" id="realf"><div><p class="real"><span id="realt"></span><b id="realv"></b></p></div></div>
</div>
</div>
<div class="ctl">
<button class="pill play" id="play" type="button" data-on="0"><svg viewBox="0 0 14 14" aria-hidden="true"><path class="i-play" d="M2.5 1.2v11.6a.6.6 0 0 0 .92.5l9-5.8a.6.6 0 0 0 0-1l-9-5.8a.6.6 0 0 0-.92.5z"/><g class="i-pause"><rect x="2" y="1" width="3.7" height="12" rx=".9"/><rect x="8.3" y="1" width="3.7" height="12" rx=".9"/></g></svg><span id="playt">Play the day</span></button>
__SWITCH__
</div>
<div class="track" id="track" role="slider" tabindex="0" aria-label="Time of day" aria-valuemin="0" aria-valuemax="1439" aria-valuenow="0">
<div class="segs" id="segs"></div><div class="head" id="head"></div></div>
<div class="fold" id="trowf"><div><div class="trow" id="trow"></div></div></div>
<div class="hrs" id="hrs"></div>
<div class="fold" id="tcapf"><div><p class="tcap" id="tcap"></p></div></div>
<div class="legend" id="legend"></div>
</section>

<h2>This day in short</h2>
<div class="tiles" id="tiles"></div>
<div class="more"><button class="pill" id="more" type="button" aria-expanded="false" aria-controls="rowsf"><span id="moret">See this day part by part</span><svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 4.2l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
<div class="fold" id="rowsf"><div><div class="rows" id="rows"></div></div></div></div>

<h2>Pick another day</h2>
<p class="sub">__SPAN__</p>
<div class="rail" id="rail"></div>

<h2>How Pip reads the air</h2>
<ul class="keylist">
<li id="key-here"><span class="sw k-here"></span><span><b>Someone here.</b> CO2 has stayed at least __ON__ ppm above the room's empty level for __ONMIN__ minutes. This is the tested rule.</span></li>
<li id="key-asleep"><span class="sw k-asleep"></span><span><b>Likely asleep.</b> High, steady CO2 at night. A guess, not a tested result.</span></li>
<li id="key-clearing"><span class="sw k-clearing"></span><span><b>Air clearing.</b> CO2 is falling. A window opened, or the room emptied. The air alone can't tell which, so Pip makes no call.</span></li>
<li id="key-empty"><span class="sw k-empty"></span><span><b>Empty.</b> CO2 is back near the room's empty level.</span></li>
<li id="key-off"><span class="sw k-off"></span><span><b>Pip was off.</b> No readings. Nothing is guessed for that time.</span></li>
</ul>
<p class="fine">The empty level is the lowest the room's CO2 usually gets. Pip works it out from the last 7 days. Each dot in the drawing is about 14 ppm above it.</p>
<p class="fine">"Pip is sure" means the answer stays the same when the limit moves from __LOW__ to __HIGH__ ppm. It is not a probability. Pip can't tell who is in the room, or how many people.</p>
__SIMNOTE__
<div class="foot">__FULL__</div>
</main>
<script id="data" type="application/json">__DATA__</script>
<script>__JS__</script>
</body></html>
"""

SWITCH = ('<button class="switch" id="truth" type="button" role="switch" aria-checked="false">'
          '<span class="box"></span><span>Show what really happened</span></button>')


def page(data, back=None, back_label=None, full=None, fonts=None, theme=None):
    """back: an address for a link back to Pip's story, for a copy hosted next to that page.
    full: an address for the full diary. fonts: a folder that holds Geist-Variable.woff2 and
    GeistPixel-Square.woff2 (Pip's story page has them). Without it the page uses system fonts.
    theme: "light" or "dark" to fix the look. Without it the page follows the reader's system."""
    if theme not in (None, "light", "dark"):
        raise ValueError("theme is light or dark")
    rule, sim = data["rule"], data["simulated"]
    font_css = ""
    if fonts:
        if not SAFE_URL.match(fonts):
            raise ValueError("fonts must be a plain address, like ../assets/fonts/")
        base = fonts.rstrip("/") + "/"
        font_css = (f'@font-face{{font-family:"Geist Sans";src:url("{base}Geist-Variable.woff2") format("woff2");'
                    'font-weight:100 900;font-display:swap}'
                    f'@font-face{{font-family:"Geist Pixel";src:url("{base}GeistPixel-Square.woff2") format("woff2");'
                    'font-display:swap}')
    def esc(text):
        return html.escape(text, quote=True)

    back_html = "<span></span>"
    if back:
        back_html = f'<a class="pill" href="{esc(back)}">{html.escape(back_label or BACK_LABEL, quote=False)}</a>'
    full_html = ""
    if full:
        full_html = ("<p>This is the short view. The full diary shows every reading, the evidence for each part of the day, "
                     "and buttons to mark Pip right or wrong.</p>"
                     f'<a class="pill" href="{esc(full)}">Open the full diary</a>')
    span = data.get("span", "")
    count = len(data["days"])
    out = (PAGE.replace("__TITLE__", " (simulated)" if sim else "")
           .replace("__THEME__", f' data-theme="{theme}"' if theme else "")
           .replace("__FONTS__", font_css).replace("__CSS__", CSS)
           .replace("__BACK__", back_html)
           .replace("__TAG__", '<span class="tag">Simulated room</span>' if sim else "")
           .replace("__SIMLEDE__", " This room is simulated." if sim else "")
           .replace("__SWITCH__", SWITCH if sim else "")
           .replace("__SPAN__", f"{count} days, {span}. Each bar is one day, midnight to midnight.")
           .replace("__ON__", f"{rule['on']:.0f}").replace("__ONMIN__", str(rule["on_min"]))
           .replace("__LOW__", str(rule["sure_low"])).replace("__HIGH__", str(rule["sure_high"]))
           .replace("__SIMNOTE__",
                    '<p class="fine">This room is simulated, so the page can show what really happened. A real room has no '
                    'such answer. The simulated sensor is also noisier than the real one, so "likely asleep" never shows '
                    'here. Those nights read as someone here.</p>' if sim else "")
           .replace("__FULL__", full_html)
           .replace("__HAPPY__", face("happy")).replace("__STUFFY__", face("stuffy")).replace("__GASPING__", face("gasping")))
    blob = json.dumps(slim(data), separators=(",", ":")).replace("</", "<\\/")
    return out.replace("__JS__", JS).replace("__DATA__", blob)
