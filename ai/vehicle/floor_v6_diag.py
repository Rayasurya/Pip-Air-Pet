"""SIMULATED diagnosis: which piece of knowledge gives the learned-priors oracle its edge on the
cracked-window van? Ablates the oracle of floor_v5.py on 100 infant cracked vans (seeds 70001+),
threshold set on 300 empty cracked vehicles (zero false alarms by construction).

Results (within 20 min, found by 90 min), on the fleet seeds picked by floor_v5.pick():
  A  full learned-priors oracle (leak band 0.75-1.25 x mean, cap = offset + 490)   77%  (95%)
  B  cap 510 instead (offset not known)                                            65%  (85%)
  C  leak band 0.55-1.8 x mean (what v5 can afford)                                54%  (92%)
  D  B and C together                                                              51%  (83%)
  E  whole cracked range 3-6 (no leak knowledge)                                   54%  (92%)
  F  A plus the closed-state rates in the grid (window state NOT known)            22%  (90%)
So: knowing the window state is worth about 55 points, a tight leak band about 23, the offset about 12.

Run:  tools/.venv/bin/python ai/vehicle/floor_v6_diag.py
"""
import os, sys, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cabin_sim, floor_analysis as fa, floor_v5 as f5
from multiprocessing import Pool
STEP=cabin_sim.STEP
def curve(a):
    seed, mode = a
    hist,test=cabin_sim.generate_fleet(seed,0); veh=cabin_sim.vehicle(seed); m=test['meta']
    i0=int(math.ceil(m['t_close_s']/STEP)); y=test['co2'][i0:].astype(float); n=len(y)
    mean=veh['ach_cracked_mean']; lo,hi=cabin_sim.LEAK_CRACKED
    off=veh['offset']
    if mode=='A': lams=np.linspace(max(lo,.75*mean),min(hi,1.25*mean),10); cmax=off+490
    elif mode=='B': lams=np.linspace(max(lo,.75*mean),min(hi,1.25*mean),10); cmax=510
    elif mode=='C': lams=np.linspace(max(lo,.55*mean),min(hi,1.8*mean),10); cmax=off+490
    elif mode=='D': lams=np.linspace(max(lo,.55*mean),min(hi,1.8*mean),10); cmax=510
    elif mode=='E': lams=np.linspace(3,6,10); cmax=off+490
    elif mode=='F': lams=np.concatenate([np.linspace(.3,1.5,6),np.linspace(max(lo,.75*mean),min(hi,1.25*mean),10)]); cmax=off+490
    r1=np.full(n,np.inf); r0=np.full(n,np.inf)
    for l in lams:
        a1,a0=fa.fit_rss(y,l,cmax); r1=np.minimum(r1,a1); r0=np.minimum(r0,a0)
    return seed,np.arange(n)*STEP/60.0,fa.lr_from(r1,r0,n)
if __name__=='__main__':
    groups=f5.pick()
    emp=groups[('none','cracked')][:300]
    inf=groups[('infant','cracked')]['van']
    with Pool() as P:
        for mode in 'ABCDEF':
            res={s:(mi,lr) for s,mi,lr in P.imap_unordered(curve,[(s,mode) for s in emp+inf],chunksize=4)}
            thr=max(float(res[s][1].max()) for s in emp)*1.0000001
            t=[fa.first_cross(res[s][0],res[s][1],thr) for s in inf]
            print(mode,fa.summarize(t,len(t)),flush=True)
