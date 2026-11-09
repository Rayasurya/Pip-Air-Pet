"""Python mirror of firmware/lib/pet/roomair.cpp. Same formulas, same constants, same order of steps.
Used to check the C++ (parity) and to show the maths in plain code."""
import math

K_BUF, K_VALID_MIN, K_FIT_MIN, K_STEADY_MIN = 40, 20, 10, 20
K_LAG_MIN = 1.0
K_OCCUPIED_EXCESS = 80.0
K_STEADY_FRAC, K_STEADY_FLOOR = 0.05, 20.0
K_FRESH_CAP = 99.0
K_OUT_MIN, K_OUT_MAX, K_OUT_DEFAULT, K_OUT_WARMUP, K_DAYS = 400.0, 500.0, 420.0, 120, 7
K_DECAY_WIN, K_DECAY_SKIP = 24, 4
K_DECAY_MIN_EXCESS, K_DECAY_START_EXCESS, K_DECAY_MIN_POINTS = 30.0, 100.0, 8
K_LEAK_MIN, K_LEAK_MAX = 0.2, 15.0
K_RISK = (0.5, 1.0, 2.0)


def risk_for(p):
    if p < K_RISK[0]: return 0
    if p < K_RISK[1]: return 1
    if p < K_RISK[2]: return 2
    return 3


class RoomAirRef:
    def __init__(self, volume=180.0, gen=0.0050, exhaled=38000.0, leak_default=2.0, max_people=None):
        self.V, self.G, self.X = volume, gen, exhaled
        self.buf = []            # newest last, at most K_BUF
        self.minutes = 0
        self.day_min = []
        self.cur_day_min = 1e9
        self.day_minute = 0
        self.leak_learned = False
        self.leak = leak_default
        self.leak_at = 0
        self.pf = PfRef(volume, gen, max_people, seed=1)
        self.pf_out = (0.0, 0.0, 0.0, 0.0)

    def _at(self, ago):
        return self.buf[-1 - ago]

    def _fit(self, m):
        ys = self.buf[-m:]
        sx = sum(range(m)); sy = sum(ys)
        sxx = sum(i * i for i in range(m)); sxy = sum(i * y for i, y in enumerate(ys))
        d = m * sxx - sx * sx
        slope = (m * sxy - sx * sy) / d
        b = (sy - slope * sx) / m
        end = b + slope * (m - 1)
        ss = sum((y - (b + slope * i)) ** 2 for i, y in enumerate(ys))
        return slope, end, math.sqrt(ss / (m - 2))

    def outdoor(self):
        if self.minutes < K_OUT_WARMUP: return K_OUT_DEFAULT
        m = min([self.cur_day_min] + self.day_min)
        if m > 1e8: return K_OUT_DEFAULT
        return min(max(m, K_OUT_MIN), K_OUT_MAX)

    def add_minute(self, co2):
        v = min(max(float(co2), 0.0), 40000.0)
        self.buf.append(v)
        if len(self.buf) > K_BUF: self.buf.pop(0)
        self.minutes += 1
        if len(self.buf) >= 5:
            s = sum(self.buf[-5:]) / 5
            if s < self.cur_day_min: self.cur_day_min = s
        self.day_minute += 1
        if self.day_minute >= 1440:
            self.day_min.append(self.cur_day_min)
            if len(self.day_min) > K_DAYS: self.day_min.pop(0)
            self.cur_day_min = 1e9
            self.day_minute = 0
        self._learn_decay()
        self.pf_out = self.pf.step(v)

    def _learn_decay(self):
        if len(self.buf) < K_DECAY_WIN: return
        cout = self.outdoor()
        e = []
        for i in range(K_DECAY_SKIP, K_DECAY_WIN):
            ex = self._at(K_DECAY_WIN - 1 - i) - cout
            if ex < K_DECAY_MIN_EXCESS: break
            e.append(ex)
        n = len(e)
        if n < K_DECAY_MIN_POINTS or e[0] < K_DECAY_START_EXCESS: return
        b = n // 3
        m1 = sum(e[:b]) / b; m2 = sum(e[b:2 * b]) / b; m3 = sum(e[n - b:]) / b
        if not (m1 > m2 > m3): return
        s1, s2 = math.log(m2 / m1), math.log(m3 / m2)
        if s2 > 0.8 * s1: return
        sw = swx = swy = swxx = swxy = 0.0
        for i, ei in enumerate(e):
            w = ei * ei; y = math.log(ei)
            sw += w; swx += w * i; swy += w * y; swxx += w * i * i; swxy += w * i * y
        slope = (sw * swxy - swx * swy) / (sw * swxx - swx * swx)
        lam = -slope * 60.0
        if not (K_LEAK_MIN <= lam <= K_LEAK_MAX): return
        if self.leak_learned and self.minutes - self.leak_at <= 3: self.leak = 0.7 * self.leak + 0.3 * lam
        else: self.leak = lam
        self.leak_learned = True
        self.leak_at = self.minutes

    def report(self, v1=False):
        """Returns (valid, steady, occupied, leak_learned, outdoor, excess, rebreathed_pct, fresh, low, mid, high, risk, leak)."""
        cout = self.outdoor()
        r = dict(valid=0, steady=0, occupied=0, leak_learned=int(self.leak_learned), outdoor=cout, excess=0.0,
                 reb=0.0, fresh=0.0, low=0.0, mid=0.0, high=0.0, risk=0, leak=self.leak)
        if len(self.buf) < K_VALID_MIN: return r
        r["valid"] = 1
        slope10, end10, res10 = self._fit(K_FIT_MIN)
        corr = end10 + K_LAG_MIN * slope10
        excess = max(0.0, corr - cout)
        r["excess"] = excess
        r["reb"] = 100.0 * excess / self.X
        r["risk"] = risk_for(r["reb"])
        r["occupied"] = int(excess >= K_OCCUPIED_EXCESS)
        if r["occupied"]:
            r["fresh"] = min(self.G / (excess * 1e-6), K_FRESH_CAP)
            s20, _, _ = self._fit(K_STEADY_MIN)
            lim = max(K_STEADY_FRAC * excess, K_STEADY_FLOOR)
            r["steady"] = int(abs(s20) * K_STEADY_MIN <= lim)
        if v1:   # the first version (mass balance with one learned air-change rate); kept to compare against v2
            vL = self.V * 1000.0
            age = self.minutes - self.leak_at
            if not self.leak_learned: lo, hi = 0.7, 1.4
            elif age < 90: lo, hi = 0.8, 1.25
            elif age < 360: lo, hi = 0.6, 1.7
            else: lo, hi = 0.4, 2.5
            sig = max(res10, 5.0) * math.sqrt(12.0 / (K_FIT_MIN * (K_FIT_MIN ** 2 - 1.0)))
            per_ppm = vL * 1e-6 / self.G
            people = lambda lam: max(0.0, per_ppm * (slope10 / 60.0 + lam / 3600.0 * (corr - cout)))
            noise = 2.0 * per_ppm * (sig / 60.0) + per_ppm * (self.leak * hi / 3600.0) * 15.0
            r["mid"] = people(self.leak); r["low"] = max(0.0, people(self.leak * lo) - noise); r["high"] = people(self.leak * hi) + noise
            return r
        # headcount and (while CO2 is moving) fresh air come from the particle filter (see PfRef below)
        r["low"], r["mid"], r["high"] = self.pf_out[0], self.pf_out[1], self.pf_out[2]
        if r["occupied"] and not r["steady"]: r["fresh"] = self.pf_out[3]
        return r

    def row(self, v1=False):
        r = self.report(v1)
        return [r["valid"], r["steady"], r["occupied"], r["leak_learned"], r["outdoor"], r["excess"], r["reb"],
                r["fresh"], r["low"], r["mid"], r["high"], r["risk"], r["leak"]]


# ---------------------------------------------------------------------------------------------
# v2: particle filter over people, air-change rate and sensor offset. Mirror of the C++ filter.
# The random numbers differ from the C++ (numpy vs xorshift) so parity with the C++ is statistical,
# not bit for bit. Same model, same constants.
import numpy as np

PF_N = 400
PF_REF_PPM = 440.0          # fixed reference for outdoor CO2 plus sensor offset
PF_B_INIT_MEAN, PF_B_INIT_SD, PF_B_WALK = 0.0, 35.0, 0.2
PF_SIG_Y = 12.0             # assumed measurement noise (ppm)
PF_PJ_N, PF_N_WALK_ABS, PF_N_WALK_REL = 0.03, 0.3, 0.03
PF_PJ_L, PF_L_WALK = 0.03, 0.03
PF_LAM_PRIOR = (0.5, 8.0)   # air changes per hour, prior for jumps and start
PF_LAM_CLIP = (0.3, 12.0)
PF_LAG_K = 1 - math.exp(-1.0)
PF_QLOW, PF_QHIGH = 0.05, 0.95
PF_ESS_FRAC = 0.5


class PfRef:
    def __init__(self, volume, gen, max_people=None, n=PF_N, seed=1, params=None):
        self.p = dict(sig_y=PF_SIG_Y, pj_n=PF_PJ_N, n_abs=PF_N_WALK_ABS, n_rel=PF_N_WALK_REL, pj_l=PF_PJ_L, l_walk=PF_L_WALK,
                      b_walk=PF_B_WALK, b_sd=PF_B_INIT_SD, ess=PF_ESS_FRAC)
        if params: self.p.update(params)
        self.V, self.G = volume, gen
        self.nmax = max_people if max_people else 0.25 * volume
        self.gppm = gen * 60.0 / (volume * 1000.0) * 1e6
        self.N = n
        self.rng = np.random.default_rng(seed)
        self.n = np.zeros(n); self.E = np.zeros(n); self.s = np.zeros(n)
        self.L = self._lam_prior(n)
        self.b = self._g(n) * self.p["b_sd"] + PF_B_INIT_MEAN
        self.w = np.full(n, 1.0 / n)
        self.lam_known = None

    def _g(self, k):    # unit variance, sum of 4 uniforms (cheap on the ESP32)
        return (self.rng.random((4, k)).sum(axis=0) - 2.0) * 1.7320508

    def _lam_prior(self, k):
        lo, hi = math.log(PF_LAM_PRIOR[0]), math.log(PF_LAM_PRIOR[1])
        return lo + self.rng.random(k) * (hi - lo)

    def step(self, co2, lam_known=None):
        p, N, r = self.p, self.N, self.rng
        jump = r.random(N) < p["pj_n"]
        pick = r.random(N); uni = r.random(N) * self.nmax
        walk = self.n + p["n_abs"] * self._g(N) + p["n_rel"] * self.n * self._g(N)
        self.n = np.clip(np.where(jump, np.where(pick < 0.5, 0.0, uni), walk), 0.0, self.nmax)
        if lam_known is None:
            jl = r.random(N) < p["pj_l"]
            self.L = np.clip(np.where(jl, self._lam_prior(N), self.L + p["l_walk"] * self._g(N)),
                             math.log(PF_LAM_CLIP[0]), math.log(PF_LAM_CLIP[1]))
            lam_h = np.exp(self.L)
        else:
            lam_h = np.full(N, float(lam_known))
        lam = lam_h / 60.0
        self.b = self.b + p["b_walk"] * self._g(N)
        e = np.exp(-lam)
        self.E = self.E * e + self.n * self.gppm * (1 - e) / lam
        self.s = self.s + (self.E - self.s) * PF_LAG_K
        ll = -0.5 * ((co2 - PF_REF_PPM - self.b - self.s) / p["sig_y"]) ** 2
        self.w = self.w * np.exp(ll - ll.max())
        self.w /= self.w.sum()
        o = np.argsort(self.n); cw = np.cumsum(self.w[o]); ns = self.n[o]
        lo = ns[min(np.searchsorted(cw, PF_QLOW), N - 1)]; mi = ns[min(np.searchsorted(cw, 0.5), N - 1)]
        hi = ns[min(np.searchsorted(cw, PF_QHIGH), N - 1)]
        q = lam_h * self.V * 1000.0 / 3600.0 / np.maximum(self.n, 0.5)
        oq = np.argsort(q); cq = np.cumsum(self.w[oq])
        fr = min(q[oq][min(np.searchsorted(cq, 0.5), N - 1)], K_FRESH_CAP)
        if 1.0 / np.sum(self.w ** 2) < N * p["ess"]:
            idx = np.minimum(np.searchsorted(np.cumsum(self.w), (r.random() + np.arange(N)) / N), N - 1)
            self.n, self.E, self.s, self.L, self.b = self.n[idx], self.E[idx], self.s[idx], self.L[idx], self.b[idx]
            self.w = np.full(N, 1.0 / N)
        return lo, mi, hi, fr
