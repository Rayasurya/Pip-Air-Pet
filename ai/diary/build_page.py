"""Air diary, step 3: one self-contained HTML page (charts + diary + "ask Pip"). Opens offline.

Usage (from air-pet/ai):
  ../tools/.venv/bin/python diary/build_page.py data/sim_room.csv --out diary/out/air-diary.html
"""
import argparse
import html
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from diary import answer, template_diary  # noqa: E402
from stats import compute  # noqa: E402

W, H, PAD = 640, 220, 36


def bar_chart(days):
    vals = [d["stuffy_min"] / 60 for d in days]
    top = max(max(vals), 1)
    bw = (W - 2 * PAD) / len(vals)
    bars = []
    for i, (d, v) in enumerate(zip(days, vals)):
        h = (H - 2 * PAD) * v / top
        x = PAD + i * bw
        bars.append(f'<rect x="{x + 1:.1f}" y="{H - PAD - h:.1f}" width="{bw - 2:.1f}" height="{h:.1f}" rx="2" class="bar">'
                    f'<title>{d["weekday"][:3]} {d["date"]}: {v:.1f} h</title></rect>')
    labels = "".join(f'<text x="{PAD + i * bw + bw / 2:.1f}" y="{H - PAD + 14}" class="tick">{d["date"][8:]}</text>'
                     for i, d in enumerate(days) if i % 5 == 0)
    axis = (f'<text x="{PAD - 6}" y="{PAD + 4}" class="tick end">{top:.0f}h</text>'
            f'<text x="{PAD - 6}" y="{H - PAD}" class="tick end">0</text>')
    return f'<svg viewBox="0 0 {W} {H}" role="img" aria-label="Stuffy hours per day">{"".join(bars)}{labels}{axis}</svg>'


def line_chart(by_hour, limit):
    lo, hi = 400, max(max(v for v in by_hour if v), limit) + 100
    def y(v): return H - PAD - (H - 2 * PAD) * (v - lo) / (hi - lo)
    def x(h): return PAD + (W - 2 * PAD) * h / 23
    pts = " ".join(f"{x(h):.1f},{y(v):.1f}" for h, v in enumerate(by_hour) if v)
    ticks = "".join(f'<text x="{x(h):.1f}" y="{H - PAD + 14}" class="tick">{h:02d}</text>' for h in range(0, 24, 3))
    return (f'<svg viewBox="0 0 {W} {H}" role="img" aria-label="Average CO2 by hour of day">'
            f'<line x1="{PAD}" x2="{W - PAD}" y1="{y(limit):.1f}" y2="{y(limit):.1f}" class="limit"/>'
            f'<text x="{W - PAD}" y="{y(limit) - 6:.1f}" class="tick end">stuffy limit {limit:.0f}</text>'
            f'<polyline points="{pts}" class="line"/>{ticks}'
            f'<text x="{PAD - 6}" y="{y(hi - 100) + 4:.1f}" class="tick end">{hi - 100:.0f}</text>'
            f'<text x="{PAD - 6}" y="{y(lo) + 4:.1f}" class="tick end">{lo}</text></svg>')


def md_to_html(md):
    out = []
    for line in md.strip().splitlines():
        line = html.escape(line)
        line = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", line)
        if line.startswith("&gt; "):
            continue  # the simulated note is shown as the page badge instead
        elif line.startswith("### "):
            out.append(f"<h3>{line[4:]}</h3>")
        elif line.startswith("## "):
            out.append(f"<h2>{line[3:]}</h2>")
        elif line.startswith("- "):
            out.append(f"<li>{line[2:]}</li>")
        elif line:
            out.append(f"<p>{line}</p>")
    joined = "\n".join(out)
    return re.sub(r"((?:<li>.*</li>\n?)+)", r"<ul>\1</ul>", joined)


PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Pip air diary</title>
<style>
:root {{ --bg:#fbfbf9; --fg:#1c1f23; --muted:#5f6670; --card:#ffffff; --line:#d9dde2; --accent:#2f80c8; --warn:#c8692f; --badge:#fff3e6; }}
@media (prefers-color-scheme: dark) {{ :root {{ --bg:#14171a; --fg:#e9ecef; --muted:#9aa3ad; --card:#1c2024; --line:#2e353c; --accent:#6ec8ff; --warn:#f0a36b; --badge:#3a2a1c; }} }}
* {{ box-sizing:border-box; }}
body {{ margin:0; background:var(--bg); color:var(--fg); font:16px/1.55 -apple-system, system-ui, sans-serif; }}
main {{ max-width:720px; margin:0 auto; padding:24px 16px 64px; }}
h1 {{ font-size:26px; margin:0 0 6px; }}
h2 {{ font-size:19px; margin:20px 0 8px; }} h3 {{ font-size:16px; margin:18px 0 6px; }}
.badge {{ display:inline-block; background:var(--badge); color:var(--warn); border:1px solid var(--warn); border-radius:999px; padding:2px 12px; font-size:13px; font-weight:600; }}
.card {{ background:var(--card); border:1px solid var(--line); border-radius:12px; padding:16px; margin:16px 0; }}
.muted {{ color:var(--muted); font-size:14px; }}
svg {{ width:100%; height:auto; display:block; }}
.bar {{ fill:var(--accent); }} .line {{ fill:none; stroke:var(--accent); stroke-width:2.5; }}
.limit {{ stroke:var(--warn); stroke-dasharray:5 4; stroke-width:1.5; }}
.tick {{ fill:var(--muted); font-size:11px; text-anchor:middle; }} .end {{ text-anchor:end; }}
.ask button {{ font:inherit; font-size:14px; padding:6px 12px; margin:4px 4px 0 0; border-radius:999px; border:1px solid var(--line); background:var(--bg); color:var(--fg); cursor:pointer; transition:background .2s, color .2s; }}
.ask button[aria-pressed="true"] {{ background:var(--accent); color:#fff; border-color:var(--accent); }}
#answer {{ margin-top:12px; min-height:3em; transition:opacity .25s; }}
ul {{ padding-left:20px; }}
</style></head><body><main>
<h1>Pip's air diary</h1>
{badge}
<p class="muted">{range_text}</p>
<div class="card">{diary}</div>
<div class="card"><h3>Stuffy hours per day</h3>{bars}
<p class="muted">Hours with CO2 at or over the stuffy limit.</p></div>
<div class="card"><h3>Average CO2 by hour of day</h3>{line}</div>
<div class="card ask"><h3>Ask Pip: why were you unhappy on...</h3>{buttons}<p id="answer" class="muted">Pick a day.</p>
<p class="muted">Answers are built from the same numbers as the charts.</p></div>
</main><script>
const answers = {answers};
document.querySelectorAll('.ask button').forEach(b => b.addEventListener('click', () => {{
  document.querySelectorAll('.ask button').forEach(x => x.setAttribute('aria-pressed', x === b));
  const a = document.getElementById('answer'); a.style.opacity = 0;
  setTimeout(() => {{ a.textContent = answers[b.dataset.day]; a.style.opacity = 1; }}, 150);
}}));
</script></body></html>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--out", default="diary/out/air-diary.html")
    a = ap.parse_args()
    s = compute(a.log)
    week = s["days"][-7:]
    answers = {d["date"]: answer(s, f"why on {d['date']}") for d in week}
    buttons = "".join(f'<button type="button" data-day="{d["date"]}" aria-pressed="false">{d["weekday"][:3]} {d["date"][5:]}</button>' for d in week)
    page = PAGE.format(
        badge='<span class="badge">Simulated data, not real measurements</span>' if s["simulated"] else "",
        range_text=f"Log covers {s['range'][0]} to {s['range'][1]}. Diary written by the template writer (no AI).",
        diary=md_to_html(template_diary(s)),
        bars=bar_chart(s["days"][-30:]),
        line=line_chart(s["co2_by_hour"], s["limits"]["co2"]),
        buttons=buttons,
        answers=json.dumps(answers),
    )
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
