"""Python reference of the left-behind detector: v4 (as shipped) plus v5 "learns its own vehicle".

This is a line-by-line mirror of firmware/lib/pet/leftbehind.cpp, kept in plain Python floats so the
C++ and Python give the SAME alarms on the same readings (tools/left_behind/parity.sh checks that).
float32 values in the C++ (readings, slopes, thresholds) are rounded the same way here with r().

v5 adds a learner. A device that lives in one vehicle sees many parkings. From a parking that looks
empty it learns (a) the settled CO2 level as its own sensor reads it (outdoor air plus its fixed
offset) and (b) the vehicle's leak rate, from the decay after the adults leave. It keeps the last 10
of each and uses MEDIANS, so a parking learned by mistake cannot move the answer much. The next
parking uses them as tighter priors in the v4 model comparison: the decay rates tried are a narrow
band around the learned leak, and the cap on the pure-decay level comes from the learned baseline.
With too little history it behaves exactly like v4.

All numbers produced with this are SIMULATED.
"""
import math
import struct

import numpy as np

_f32 = np.float32


def r(x):
    """Round to float32 like a C++ float."""
    return float(_f32(x))


def u32(x):
    return x & 0xFFFFFFFF


# ---- configuration (mirrors LBConfig) -----------------------------------------------------------
class Config:
    def __init__(self, **kw):
        c = dict(
            settleMinSec=120, settleMaxSec=600, settleFallPpmMin=8.0, settleSlopeWindowSec=120,
            windowSec=240, minWindowSec=120, evalEverySec=30, productionThresholdPpmMin=3.0,
            zMargin=2.5, confirmCount=3, leakPerHour=0.3, lagSec=60.0, outdoorMinPpm=400.0,
            outdoorMaxPpm=600.0, outdoorDefaultPpm=450.0, levelExcessPpm=100.0, levelDecayFrac=0.8,
            levelZ=0.0, levelOutdoorCapPpm=500.0, levelWindowSec=600, levelSustainSec=300,
            levelRuleOn=True, cabinVolumeM3=3.0, referenceVolumeM3=3.0, levelExcessFloorPpm=60.0,
            modelRuleOn=True, modelStartSec=120, modelMinSamples=60, modelAsymptoteMaxPpm=510.0,
            modelThreshold=20.0, hotC=32.0, dangerC=42.0, hotRiseC10=2.0, fastRiseC10=6.0,
            sensorMaxC=60.0,
            # v5
            learnOn=True, learnMinBase=6, learnMinLeak=2, learnBaseMargin=80.0, learnLeakLo=0.55,
            learnLeakHi=1.8, learnModelThreshold=20.0, learnMaxResidualSd=14.0, learnMaxBaseSe=12.0,
            learnMinSamples=480, learnLeakSplit=2.0, learnBaseMin=390.0, learnBaseMax=510.0,
            learnDetectZ=9.0, learnTailZ=2.5,
            closedLo=0.3, closedHi=1.5, crackedLo=3.0, crackedHi=6.0,
        )
        c.update(kw)
        float_keys = ("settleFallPpmMin", "productionThresholdPpmMin", "zMargin", "leakPerHour",
                      "lagSec", "outdoorMinPpm", "outdoorMaxPpm", "outdoorDefaultPpm", "levelExcessPpm",
                      "levelDecayFrac", "levelZ", "levelOutdoorCapPpm", "cabinVolumeM3",
                      "referenceVolumeM3", "levelExcessFloorPpm", "modelAsymptoteMaxPpm",
                      "modelThreshold", "hotC", "dangerC", "hotRiseC10", "fastRiseC10", "sensorMaxC",
                      "learnBaseMargin", "learnLeakLo", "learnLeakHi", "learnModelThreshold",
                      "learnMaxResidualSd", "learnMaxBaseSe", "learnLeakSplit", "learnBaseMin",
                      "learnBaseMax", "learnDetectZ", "learnTailZ", "closedLo", "closedHi", "crackedLo", "crackedHi")
        for k in float_keys:
            c[k] = r(c[k])
        self.__dict__.update(c)


DRIVING, SETTLING, WATCHING, ALERT = 0, 1, 2, 3
NONE, CHECK, URGENT = 0, 1, 2

K_RING = 128
K_GRID = 7
K_DIM = 2 + K_GRID
K_ACH_V4 = [0.3, 0.5, 0.8, 1.3, 2.2, 3.6, 6.0]
K_TAU = 60.0

K_HIST = 10  # learned values kept per category
K_LAM = 28  # decay rates tried by the learner
LAM_LO, LAM_HI = 0.15, 8.0
LEARN_VERSION = 1


def lam_grid():
    return [LAM_LO * math.pow(LAM_HI / LAM_LO, k / (K_LAM - 1.0)) for k in range(K_LAM)]


LAMS = lam_grid()
DEFAULT_CLOSED = [0.3, 0.75, 1.5]
DEFAULT_CRACKED = [3.0, 4.0, 5.0, 6.0]


def median_f32(vals):
    v = sorted(vals)
    n = len(v)
    if n == 0:
        return 0.0
    if n % 2:
        return v[n // 2]
    return r((v[n // 2 - 1] + v[n // 2]) / 2.0)


def _fit4(S, V, yy, idx, fixed_mask, fixed_val, beta):
    fr = []
    for a in range(4):
        if fixed_mask & (1 << a):
            beta[a] = fixed_val[a]
        else:
            fr.append(a)
    nf = len(fr)

    def Sij(a, b):
        i, j = idx[a], idx[b]
        return S[i][j] if i <= j else S[j][i]

    if nf:
        M = [[0.0] * 5 for _ in range(4)]
        for a in range(nf):
            rr = V[idx[fr[a]]]
            for b in range(4):
                if fixed_mask & (1 << b):
                    rr -= Sij(fr[a], b) * fixed_val[b]
            for b in range(nf):
                M[a][b] = Sij(fr[a], fr[b]) + (1e-9 if a == b else 0.0)
            M[a][nf] = rr
        for c in range(nf):
            d = M[c][c]
            for rw in range(c + 1, nf):
                f = M[rw][c] / d
                for k in range(c, nf + 1):
                    M[rw][k] -= f * M[c][k]
        for c in range(nf - 1, -1, -1):
            rr = M[c][nf]
            for k in range(c + 1, nf):
                rr -= M[c][k] * beta[fr[k]]
            beta[fr[c]] = rr / M[c][c]
    lin = 0.0
    quad = 0.0
    for a in range(4):
        lin += beta[a] * V[idx[a]]
        for b in range(4):
            quad += beta[a] * beta[b] * Sij(a, b)
    return yy - 2 * lin + quad


def _solve_spd(M, n):
    """Gaussian elimination on an augmented n x (n+1) matrix, no pivoting. Returns solution list."""
    for c in range(n):
        d = M[c][c]
        for rw in range(c + 1, n):
            f = M[rw][c] / d
            for k in range(c, n + 1):
                M[rw][k] -= f * M[c][k]
    x = [0.0] * n
    for c in range(n - 1, -1, -1):
        rr = M[c][n]
        for k in range(c + 1, n):
            rr -= M[c][k] * x[k]
        x[c] = rr / M[c][c]
    return x


class Learned:
    """The learning state: last K_HIST accepted values for three categories."""

    def __init__(self):
        self.base = []
        self.closed = []
        self.cracked = []

    @staticmethod
    def _push(lst, v):
        lst.append(r(v))
        if len(lst) > K_HIST:
            lst.pop(0)

    def add_base(self, v):
        self._push(self.base, v)

    def add_leak(self, lam, split):
        self._push(self.closed if lam < split else self.cracked, lam)

    # Plain bytes. Layout: u8 version, u8 nBase, u8 nClosed, u8 nCracked, then 3 x 10 float32
    # (oldest first, unused slots zero), then u16 checksum (sum of all earlier bytes), little-endian.
    def to_bytes(self):
        out = bytearray([LEARN_VERSION, len(self.base), len(self.closed), len(self.cracked)])
        for lst in (self.base, self.closed, self.cracked):
            vals = lst + [0.0] * (K_HIST - len(lst))
            out += struct.pack("<%df" % K_HIST, *vals)
        out += struct.pack("<H", sum(out) & 0xFFFF)
        return bytes(out)

    @classmethod
    def from_bytes(cls, b, base_lo=300.0, base_hi=700.0):
        size = 4 + 3 * K_HIST * 4 + 2
        if len(b) != size or b[0] != LEARN_VERSION:
            return None
        if struct.unpack("<H", b[-2:])[0] != (sum(b[:-2]) & 0xFFFF):
            return None
        n = [b[1], b[2], b[3]]
        if any(x > K_HIST for x in n):
            return None
        o = cls()
        off = 4
        for lst, cnt, (lo, hi) in zip((o.base, o.closed, o.cracked), n,
                                      ((base_lo, base_hi), (0.05, 3.0), (1.0, 12.0))):
            vals = struct.unpack_from("<%df" % K_HIST, b, off)
            off += K_HIST * 4
            for v in vals[:cnt]:
                if not (lo <= v <= hi):  # also rejects NaN
                    return None
                lst.append(v)
        return o


class LeftBehind:
    def __init__(self, cfg=None):
        self.cfg = cfg or Config()
        self.learned = Learned()
        self.parkLearn = False
        self.everAlerted = False
        self.ring = []
        self.lOn = False
        self._reset()

    # ---- state ----------------------------------------------------------------------------
    def _reset(self):
        self.state = DRIVING
        self.level = NONE
        self.dismissed = False
        self.ring.clear()
        self.streak = 0
        self.levelSince = 0
        self.mOn = False
        self.mN = 0
        self.nextModel = 0
        self.modelScore_ = 0.0
        self.outdoor = self.cfg.outdoorDefaultPpm
        self.minAvg = r(1e9)
        self.prod = 0.0
        self.tOff = self.tSettled = self.lastT = self.nextEval = 0
        self.grid = list(K_ACH_V4)
        self.pairOn = [True] * (K_GRID - 1)
        self.asymMax = self.cfg.modelAsymptoteMaxPpm
        self.modelThr = self.cfg.modelThreshold

    def set_ignition(self, on, t):
        self._reset()
        if on:
            self.parkLearn = False
        else:
            self.state = SETTLING
            self.tOff = t
            self.lastT = t
            self._apply_priors()
            self.lOn = False
            self.parkLearn = True
            self.everAlerted = False

    def dismiss(self, t):
        if self.state == DRIVING:
            return
        self.dismissed = True
        self.everAlerted = True
        self.state = WATCHING
        self.level = NONE
        self.streak = 0

    # ---- v5 priors ------------------------------------------------------------------------
    def _apply_priors(self):
        c = self.cfg
        L = self.learned
        haveB = len(L.base) >= c.learnMinBase
        haveC = len(L.closed) >= c.learnMinLeak
        haveK = len(L.cracked) >= c.learnMinLeak
        self.grid = list(K_ACH_V4)
        self.pairOn = [True] * (K_GRID - 1)
        self.asymMax = c.modelAsymptoteMaxPpm
        self.modelThr = c.modelThreshold
        if not c.learnOn or not (haveB or haveC or haveK):
            return
        self.modelThr = c.learnModelThreshold
        if haveB:
            cap = r(median_f32(L.base) + c.learnBaseMargin)
            self.asymMax = cap if cap < c.modelAsymptoteMaxPpm else c.modelAsymptoteMaxPpm
        closed = list(DEFAULT_CLOSED)
        cracked = list(DEFAULT_CRACKED)
        if haveC:
            closed = self._band(median_f32(L.closed), 3, c.closedLo, c.closedHi)
        if haveK:
            cracked = self._band(median_f32(L.cracked), 4, c.crackedLo, c.crackedHi)
        if haveB or haveC or haveK:
            self.grid = closed + cracked
            self.pairOn = [True, True, False, True, True, True]

    def _band(self, m, n, rmin, rmax):
        """n rates spaced evenly in log between lo*m and hi*m, kept inside the assumed range."""
        lo, hi = self.cfg.learnLeakLo * m, self.cfg.learnLeakHi * m
        lo = rmin if lo < rmin else (rmax if lo > rmax else lo)
        hi = rmax if hi > rmax else (rmin if hi < rmin else hi)
        if hi < lo * 1.05:
            hi = lo * 1.05
        return [lo * math.pow(hi / lo, k / (n - 1.0)) for k in range(n)]

    # ---- helpers --------------------------------------------------------------------------
    def _push(self, t, co2, temp):
        self.ring.append((t, co2, temp))
        if len(self.ring) > K_RING:
            self.ring.pop(0)

    def _at(self, back):
        return self.ring[len(self.ring) - 1 - back]

    def _fit(self, now, fromT, windowSec):
        lo = now - windowSec if now > windowSec else 0
        start = lo if lo > fromT else fromT
        st = 0.0
        sy = 0.0
        n = 0
        cnt = len(self.ring)
        for i in range(cnt):
            s = self._at(i)
            if s[0] < start:
                break
            st += (float(s[0]) - float(now)) / 60.0
            sy += s[1]
            n += 1
        if n < 6:
            return None
        mt = st / n
        my = sy / n
        sxx = 0.0
        sxy = 0.0
        for i in range(cnt):
            s = self._at(i)
            if s[0] < start:
                break
            dt = (float(s[0]) - float(now)) / 60.0 - mt
            sxx += dt * dt
            sxy += dt * (s[1] - my)
        if sxx < 1e-6:
            return None
        slope = sxy / sxx
        sse = 0.0
        for i in range(cnt):
            s = self._at(i)
            if s[0] < start:
                break
            dt = (float(s[0]) - float(now)) / 60.0 - mt
            rr = s[1] - (my + slope * dt)
            sse += rr * rr
        se = r(math.sqrt(sse / (n - 2) / sxx)) if n > 2 else 0.0
        return {"n": n, "slope": r(slope), "atNow": r(my + slope * (0.0 - mt)), "se": se}

    def _track_outdoor(self):
        if len(self.ring) < 6:
            return
        s = 0.0
        for i in range(6):
            s = r(s + self._at(i)[1])
        avg = r(s / 6.0)
        if avg < self.minAvg:
            self.minAvg = avg
        o = self.minAvg
        c = self.cfg
        if o < c.outdoorMinPpm:
            o = c.outdoorMinPpm
        if o > c.outdoorMaxPpm:
            o = c.outdoorMaxPpm
        self.outdoor = o

    def _temp_rise10(self, now):
        lo = now - 600 if now > 600 else 0
        old = None
        for i in range(len(self.ring)):
            s = self._at(i)
            if s[0] < lo:
                break
            old = s
        if old is None:
            return 0.0
        span = now - old[0]
        if span < 240:
            return 0.0
        return r(r(r(self._at(0)[2] - old[2]) * 600.0) / r(float(span)))

    # ---- model comparison (v4, with v5 priors) -------------------------------------------
    def _model_add(self, t, co2):
        dt = 5.0
        if not self.mOn:
            self.mOn = True
            self.mT0 = t
            self.mN = 0
            self.mV = [0.0] * K_DIM
            self.mS = [[0.0] * K_DIM for _ in range(K_DIM)]
            self.mYY = 0.0
            self.mDrive = [1.0] * K_GRID
            self.mG = [0.0] * K_GRID
        else:
            dt = float(t - self.mLast)
            for i in range(K_GRID):
                self.mDrive[i] *= math.exp(-self.grid[i] / 3600.0 * dt)
        self.mLast = t
        alpha = 1.0 - math.exp(-dt / K_TAU)
        x = [0.0] * K_DIM
        x[0] = 1.0
        x[1] = math.exp(-(float(t - self.mT0) + 5.0) / K_TAU)
        for i in range(K_GRID):
            self.mG[i] += alpha * (self.mDrive[i] - self.mG[i])
            x[2 + i] = self.mG[i]
        y = co2
        for i in range(K_DIM):
            for j in range(i, K_DIM):
                self.mS[i][j] += x[i] * x[j]
            self.mV[i] += x[i] * y
        self.mYY += y * y
        self.mN += 1

    def _model_score(self):
        c = self.cfg
        if not self.mOn or self.mN < c.modelMinSamples:
            return 0.0
        cmax = float(self.asymMax)
        tiny = 1e-9
        best1 = 1e300
        best0 = 1e300
        S, V, yy = self.mS, self.mV, self.mYY
        for p in range(K_GRID - 1):
            if not self.pairOn[p]:
                continue
            idx = [0, 1, 2 + p, 3 + p]
            b = [0.0] * 4
            zero = [0.0] * 4
            rU = _fit4(S, V, yy, idx, 0, zero, b)
            bU = list(b)
            if (bU[2] >= -tiny and bU[3] >= -tiny) or (bU[2] <= tiny and bU[3] <= tiny):
                r1 = rU
            else:
                r1 = 1e300
                for sgn in (-1, 1):
                    for m in range(4):
                        fm = (4 if m & 1 else 0) | (8 if m & 2 else 0)
                        q = _fit4(S, V, yy, idx, fm, zero, b)
                        if ((fm & 4) or sgn * b[2] >= -tiny) and ((fm & 8) or sgn * b[3] >= -tiny) and q < r1:
                            r1 = q
            if r1 < best1:
                best1 = r1
            if bU[0] <= cmax and bU[2] >= -tiny and bU[3] >= -tiny:
                r0 = rU
            else:
                r0 = 1e300
                fv = [cmax, 0.0, 0.0, 0.0]
                for m in range(8):
                    fm = (1 if m & 1 else 0) | (4 if m & 2 else 0) | (8 if m & 4 else 0)
                    q = _fit4(S, V, yy, idx, fm, fv, b)
                    if ((fm & 1) or b[0] <= cmax + tiny) and ((fm & 4) or b[2] >= -tiny) and \
                            ((fm & 8) or b[3] >= -tiny) and q < r0:
                        r0 = q
            if r0 < best0:
                best0 = r0
        if best1 >= 1e299 or best0 >= 1e299:
            return 0.0
        if best1 < 1e-9:
            best1 = 1e-9
        sig2 = best1 / float(self.mN - 4 if self.mN - 4 > 1 else 1)
        lr = (best0 - best1) / sig2
        return r(lr) if lr > 0 else 0.0

    # ---- v5 learner ---------------------------------------------------------------------
    def _learn_add(self, t, co2):
        dt = 5.0
        if not self.lOn:
            self.lOn = True
            self.lT0 = t
            self.lN = 0
            self.lSx1 = self.lSx1x1 = self.lV0 = self.lV1 = self.lYY = 0.0
            self.lDrive = [1.0] * K_LAM
            self.lG = [0.0] * K_LAM
            self.lSG = [0.0] * K_LAM
            self.lSx1G = [0.0] * K_LAM
            self.lSGG = [0.0] * K_LAM
            self.lVG = [0.0] * K_LAM
        else:
            dt = float(t - self.lLast)
            for k in range(K_LAM):
                self.lDrive[k] *= math.exp(-LAMS[k] / 3600.0 * dt)
        self.lLast = t
        alpha = 1.0 - math.exp(-dt / K_TAU)
        x1 = math.exp(-(float(t - self.lT0) + 5.0) / K_TAU)
        y = co2
        self.lN += 1
        self.lSx1 += x1
        self.lSx1x1 += x1 * x1
        self.lV0 += y
        self.lV1 += x1 * y
        self.lYY += y * y
        for k in range(K_LAM):
            self.lG[k] += alpha * (self.lDrive[k] - self.lG[k])
            g = self.lG[k]
            self.lSG[k] += g
            self.lSx1G[k] += x1 * g
            self.lSGG[k] += g * g
            self.lVG[k] += g * y

    def _learn_solve(self, k):
        """Fit [1, start state, decay_k] with the decay weight >= 0. Returns (rss, c, A, seC, nPar)."""
        n = float(self.lN)
        S = [[n, self.lSx1, self.lSG[k]], [self.lSx1, self.lSx1x1, self.lSx1G[k]],
             [self.lSG[k], self.lSx1G[k], self.lSGG[k]]]
        V = [self.lV0, self.lV1, self.lVG[k]]
        npar = 3
        M = [[S[i][j] + (1e-9 if i == j else 0.0) for j in range(3)] + [V[i]] for i in range(3)]
        beta = _solve_spd(M, 3)
        if beta[2] < 0.0:
            npar = 2
            M = [[S[i][j] + (1e-9 if i == j else 0.0) for j in range(2)] + [V[i]] for i in range(2)]
            beta = _solve_spd(M, 2) + [0.0]
        lin = sum(beta[a] * V[a] for a in range(3))
        quad = 0.0
        for a in range(3):
            for b in range(3):
                quad += beta[a] * beta[b] * S[a][b]
        rss = self.lYY - 2 * lin + quad
        # (X'X)^-1 [0][0] for the active columns, for the standard error of the level
        M = [[S[i][j] + (1e-9 if i == j else 0.0) for j in range(npar)] + [1.0 if i == 0 else 0.0]
             for i in range(npar)]
        inv00 = _solve_spd(M, npar)[0]
        return rss, beta[0], beta[2], inv00, npar

    def learn_from_parking(self):
        """Call when a parking has ended (ignition back on) BEFORE set_ignition(True). Learns from it
        only if it looks empty. Returns True if anything was learned (state changed, save it)."""
        c = self.cfg
        if not c.learnOn or not self.parkLearn:
            return False
        self.parkLearn = False
        if self.everAlerted or self.dismissed or self.level != NONE or not self.lOn:
            return False
        if self.lN < c.learnMinSamples:
            return False
        sols = [self._learn_solve(k) for k in range(K_LAM)]
        kbest = 0
        for k in range(1, K_LAM):
            if sols[k][0] < sols[kbest][0]:
                kbest = k
        rss, base, _, inv00, npar = sols[kbest]
        dof = float(self.lN - npar)
        sig2 = (rss if rss > 0 else 0.0) / dof
        if math.sqrt(sig2) > c.learnMaxResidualSd:
            return False
        if not (c.learnBaseMin <= base <= c.learnBaseMax):
            return False
        tail = self._fit(self.lastT, 0, 600)
        if tail is None or tail["slope"] > c.learnTailZ * tail["se"]:
            return False
        learned = False
        if math.sqrt(sig2 * inv00) <= c.learnMaxBaseSe:
            self.learned.add_base(base)
            learned = True
        if 2 <= kbest < K_LAM - 2:
            d = c.learnDetectZ * sig2
            if sols[kbest - 2][0] - rss >= d and sols[kbest + 2][0] - rss >= d:
                self.learned.add_leak(LAMS[kbest], c.learnLeakSplit)
                learned = True
        return learned

    # ---- main update ---------------------------------------------------------------------
    def update(self, t, co2, temp):
        c = self.cfg
        co2 = r(co2)
        temp = r(temp)
        if self.state == DRIVING:
            return self.state, self.level
        if t <= self.lastT and len(self.ring) > 0:
            return self.state, self.level
        self.lastT = t
        self._push(t, co2, temp)
        self._track_outdoor()
        rise = self._temp_rise10(t)
        if self.dismissed:
            return self.state, self.level
        if self.level == NONE and t >= self.tOff + c.modelStartSec:
            if c.modelRuleOn:
                self._model_add(t, co2)
            if c.learnOn:
                self._learn_add(t, co2)

        if self.state == SETTLING:
            since = t - self.tOff
            done = since >= c.settleMaxSec
            if not done and since >= c.settleMinSec:
                f = self._fit(t, self.tOff, c.settleSlopeWindowSec)
                done = f is not None and f["slope"] > r(-c.settleFallPpmMin)
            if done:
                self.state = WATCHING
                self.tSettled = t
                self.nextEval = t
            return self.state, self.level

        frm = self.tSettled - c.settleSlopeWindowSec if self.tSettled > c.settleSlopeWindowSec else 0
        fromT = frm if frm > self.tOff else self.tOff
        vol = c.cabinVolumeM3 if c.cabinVolumeM3 > 0.5 else c.referenceVolumeM3
        volScale = r(c.referenceVolumeM3 / vol)
        prodThr = r(c.productionThresholdPpmMin * volScale)
        levelThr = r(c.levelExcessPpm * volScale)
        if levelThr < c.levelExcessFloorPpm:
            levelThr = c.levelExcessFloorPpm
        f = self._fit(t, fromT, c.windowSec)
        if f is not None and f["n"] > 0:
            leak = r(c.leakPerHour / 60.0)
            cNow = r(f["atNow"] + r(r(c.lagSec / 60.0) * f["slope"]))
            excess = r(cNow - self.outdoor)
            if excess < 0:
                excess = 0.0
            self.prod = r(f["slope"] + r(leak * excess))

        if self.state == WATCHING and f is not None and t >= self.nextEval:
            self.nextEval = t + c.evalEverySec
            lo_w = u32(t - c.windowSec)
            span = t - (fromT if fromT > lo_w else lo_w)
            enough = span >= c.minWindowSec
            hit = enough and self.prod > prodThr and r(self.prod - r(c.zMargin * f["se"])) > 0
            if hit:
                self.streak += 1
                if self.streak >= c.confirmCount:
                    self.state = ALERT
                    self.level = CHECK
            else:
                self.streak = 0
            if c.modelRuleOn and self.state == WATCHING and t >= self.nextModel:
                self.nextModel = t + c.evalEverySec
                self.modelScore_ = self._model_score()
                if self.modelScore_ > self.modelThr:
                    self.state = ALERT
                    self.level = CHECK
            if c.levelRuleOn and self.state == WATCHING:
                g = self._fit(t, fromT, c.levelWindowSec)
                lo_g = u32(t - c.levelWindowSec)
                gStart = t - (fromT if fromT > lo_g else lo_g)
                holds = False
                if g is not None and gStart + 15 >= c.levelWindowSec:
                    cNow = r(g["atNow"] + r(r(c.lagSec / 60.0) * g["slope"]))
                    base = self.outdoor if self.outdoor < c.levelOutdoorCapPpm else c.levelOutdoorCapPpm
                    ex = r(cNow - base)
                    allow = r(r(-c.levelDecayFrac * r(c.leakPerHour / 60.0)) * ex)
                    holds = ex > levelThr and r(g["slope"] - r(c.levelZ * g["se"])) >= allow
                if not holds:
                    self.levelSince = 0
                else:
                    if self.levelSince == 0:
                        self.levelSince = t
                    if t - self.levelSince >= c.levelSustainSec:
                        self.state = ALERT
                        self.level = CHECK

        if self.state == ALERT:
            self.everAlerted = True
            if self.level != URGENT:
                urgent = temp >= c.dangerC or (temp >= c.hotC and rise >= c.hotRiseC10) or rise >= c.fastRiseC10
                if urgent:
                    self.level = URGENT
        return self.state, self.level


def run_parking(lb, parking, learn=True):
    """Feed one simulated parking (a cabin_sim dict) to lb the way tools/left_behind/eval.cpp does.
    Returns the absolute second of the first Check, or -1."""
    t, ign, co2, temp = parking["t"], parking["ign"], parking["co2"], parking["temp"]
    prev = -1
    first = -1
    for i in range(len(t)):
        g = int(ign[i])
        ti = int(t[i])
        if g != prev:
            if g and learn:
                lb.learn_from_parking()
            lb.set_ignition(g != 0, ti)
            prev = g
        if not g:
            _, level = lb.update(ti, float(co2[i]), float(round(float(temp[i]), 2)))
            if level != NONE and first < 0:
                first = ti
    return first
