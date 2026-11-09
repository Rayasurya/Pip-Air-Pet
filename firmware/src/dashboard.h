// Pip's live dashboard, served at http://pip.local/ on the home network.
// One self-contained page: no internet, no libraries. It reads /now.json every 15 s and the
// minute log (/log.csv, /log_old.csv) every 5 min, and draws the charts as inline SVG.
#pragma once

static const char kDashboardHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pip live air</title>
<style>
:root{--bg:#f6f5f1;--card:#fff;--ink:#1d1d1b;--mute:#6b6a64;--line:#e3e1da;--co2:#2f6f5e;--t:#b4562d;--h:#3c64a8;--warn:#b8860b;--bad:#b23a3a}
@media (prefers-color-scheme:dark){:root{--bg:#161614;--card:#20201d;--ink:#ecebe6;--mute:#9d9b93;--line:#33322e;--co2:#6cc3a8;--t:#e08a5f;--h:#86a8e0;--warn:#e0b44a;--bad:#e07070}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 -apple-system,system-ui,sans-serif}
main{max-width:860px;margin:0 auto;padding:20px 16px 40px}
header{display:flex;justify-content:space-between;align-items:baseline;gap:12px;flex-wrap:wrap}
h1{font-size:22px;margin:0}.sub{color:var(--mute);font-size:13px}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin:16px 0}
.tile{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}
.tile .k{color:var(--mute);font-size:13px}.tile .v{font-size:30px;font-weight:600;margin-top:2px}
.tile .v small{font-size:14px;font-weight:400;color:var(--mute)}.tag{display:inline-block;margin-top:6px;font-size:12px;padding:2px 8px;border-radius:99px;background:var(--line)}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-top:12px}
.card h2{font-size:14px;margin:0 0 6px;color:var(--mute);font-weight:500}
svg{width:100%;height:150px;display:block}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:99px;overflow:hidden}
.seg button{border:0;background:transparent;color:var(--ink);padding:6px 14px;font:inherit;cursor:pointer;transition:background .2s}
.seg button.on{background:var(--ink);color:var(--bg)}
.err{color:var(--bad)}
.bar{height:12px;border-radius:99px;background:var(--line);overflow:hidden;margin:10px 0 6px}
.bar i{display:block;height:100%;background:var(--co2);border-radius:99px;transition:width .6s ease}
.prog{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;margin-top:8px}
.prog b{display:block;font-size:18px}
</style></head><body><main>
<header><div><h1>Pip <span id="mood" class="sub"></span></h1><div class="sub" id="upd">Connecting…</div></div>
<div class="seg" id="range"><button data-h="6">6 h</button><button data-h="24" class="on">24 h</button><button data-h="168">7 days</button></div></header>
<div class="tiles">
<div class="tile"><div class="k">CO2</div><div class="v" id="co2">–</div><span class="tag" id="co2tag">–</span></div>
<div class="tile"><div class="k">Temperature</div><div class="v" id="temp">–</div></div>
<div class="tile"><div class="k">Humidity</div><div class="v" id="hum">–</div></div>
<div class="tile"><div class="k">Light</div><div class="v" id="light">–</div></div>
</div>
<div class="card" id="pc"><h2>The 2-week real-data run</h2>
<div class="sub" id="pmsg">–</div><div class="bar"><i id="pbar" style="width:0%"></i></div>
<div class="prog"><div><span class="sub">Running for</span><b id="pdays">–</b></div>
<div><span class="sub">First check-in (7 days)</span><b id="pmid">–</b></div>
<div><span class="sub">Goal (14 days)</span><b id="pend">–</b></div>
<div><span class="sub">Rows on Pip now</span><b id="prows">–</b></div>
<div><span class="sub">Coverage, last 24 h</span><b id="pcov">–</b></div></div></div>
<div class="card"><h2>CO2 (ppm). The dashed line is 1,000, where rooms start to feel stuffy.</h2><svg id="c1"></svg></div>
<div class="card"><h2>Temperature (°C)</h2><svg id="c2"></svg></div>
<div class="card"><h2>Humidity (%)</h2><svg id="c3"></svg></div>
<p class="sub">Real readings from Pip's sensor, one row a minute. This page works only on the home Wi-Fi.</p>
</main><script>
const $=id=>document.getElementById(id);let rows=[],hours=24;
function band(c){return c<800?['Fresh','var(--co2)']:c<1000?['Okay','var(--co2)']:c<1500?['Stuffy, open a window','var(--warn)']:['Very stale','var(--bad)']}
async function now(){try{const r=await(await fetch('/now.json',{cache:'no-store'})).json();
$('co2').innerHTML=r.co2+' <small>ppm</small>';const b=band(r.co2);$('co2tag').textContent=b[0];$('co2tag').style.color=b[1];
$('temp').innerHTML=r.temp.toFixed(1)+' <small>°C</small>';$('hum').innerHTML=Math.round(r.hum)+' <small>%</small>';
$('light').textContent=r.dark?'Dark':'Light';$('mood').textContent='· feeling '+r.mood+(r.night?' · night mode, screen '+(r.screen?'on':'off'):'');
$('upd').textContent='Updated '+new Date().toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'})+(r.clock?'':' · no clock yet');$('upd').className='sub'}
catch(e){$('upd').textContent='Can\'t reach Pip. Is it powered and on Wi-Fi?';$('upd').className='sub err'}}
function parse(t){return t.split('\n').map(l=>l.split(',')).filter(c=>/^\d{4}-\d\d-\d\d \d\d:\d\d$/.test(c[0])&&+c[1]>0)
.map(c=>({t:new Date(c[0].replace(' ','T')).getTime(),co2:+c[1],temp:+c[2],hum:+c[3]}))}
async function log(){try{const [a,b]=await Promise.all(['/log_old.csv','/log.csv'].map(u=>fetch(u,{cache:'no-store'}).then(r=>r.ok?r.text():'').catch(()=>'')));
rows=parse(a).concat(parse(b)).sort((x,y)=>x.t-y.t);draw()}catch(e){}}
function chart(id,key,col,ref){const s=$(id),W=s.clientWidth||600,H=150,P=28,end=Date.now(),start=end-hours*3600e3;
const d=rows.filter(r=>r.t>=start);if(d.length<2){s.innerHTML='<text x="8" y="80" fill="var(--mute)" font-size="13">Not enough data yet for this range.</text>';return}
let lo=Math.min(...d.map(r=>r[key])),hi=Math.max(...d.map(r=>r[key]));if(ref){lo=Math.min(lo,ref);hi=Math.max(hi,ref)}
const pad=(hi-lo)*.1||1;lo-=pad;hi+=pad;const x=t=>P+(t-start)/(end-start)*(W-P-6),y=v=>H-18-(v-lo)/(hi-lo)*(H-26);
let path='',prev=null;for(const r of d){path+=(prev&&r.t-prev>5*60e3?'M':(path?'L':'M'))+x(r.t).toFixed(1)+' '+y(r[key]).toFixed(1);prev=r.t}
const fmt=t=>{const dt=new Date(t);return hours>48?dt.toLocaleDateString([], {weekday:'short'}):dt.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'})};
let g='';for(const v of [lo+pad,hi-pad])g+=`<text x="0" y="${y(v)+4}" fill="var(--mute)" font-size="11">${Math.round(v)}</text>`;
for(let i=0;i<=3;i++){const t=start+i*(end-start)/3;g+=`<text x="${Math.min(x(t),W-40)}" y="${H-2}" fill="var(--mute)" font-size="11">${fmt(t)}</text>`}
if(ref)g+=`<line x1="${P}" x2="${W-6}" y1="${y(ref)}" y2="${y(ref)}" stroke="var(--warn)" stroke-dasharray="4 4"/>`;
s.setAttribute('viewBox',`0 0 ${W} ${H}`);s.innerHTML=g+`<path d="${path}" fill="none" stroke="${col}" stroke-width="2" stroke-linejoin="round"/>`}
const START=new Date('2026-10-19T13:20').getTime(),DAY=864e5,GOAL=14;
function progress(){const n=Date.now(),d=(n-START)/DAY,p=Math.min(100,d/GOAL*100);
const f=t=>new Date(t).toLocaleDateString([], {day:'numeric',month:'short'});
$('pbar').style.width=p.toFixed(1)+'%';$('pdays').textContent=d<1?Math.floor(d*24)+' h':d.toFixed(1)+' days';
$('pmid').textContent=f(START+7*DAY)+(d>=7?' ✓':'');$('pend').textContent=f(START+GOAL*DAY)+(d>=GOAL?' ✓':'');
$('prows').textContent=rows.length.toLocaleString();const day=rows.filter(r=>r.t>n-DAY).length;
$('pcov').textContent=Math.min(100,Math.round(day/Math.min(1440,(n-START)/6e4)*100))+'%';
$('pmsg').textContent=d>=GOAL?'Done: the two weeks are complete. Pip keeps logging.':d>=7?'Halfway. A first look is possible now; the full run ends '+f(START+GOAL*DAY)+'.':
'Keep Pip powered and on the wall. '+Math.ceil(7-d)+' days to the first check-in.'}
function draw(){progress();chart('c1','co2','var(--co2)',1000);chart('c2','temp','var(--t)');chart('c3','hum','var(--h)')}
$('range').onclick=e=>{const h=e.target.dataset.h;if(!h)return;hours=+h;for(const b of $('range').children)b.classList.toggle('on',b===e.target);draw()};
addEventListener('resize',draw);now();log();setInterval(now,15000);setInterval(log,300000);
</script></body></html>)HTML";
