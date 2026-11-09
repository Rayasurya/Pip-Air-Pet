"""SIMULATED shared rooms for room mode. Every number here is SIMULATED, none is measured.

One minute per step. Three room types: classroom, clinic waiting room, meeting room.
The true CO2 follows the well-mixed mass balance with piecewise-constant people and air-change rate,
analytic over each minute. The sensor then adds a 60 s first-order lag, noise, an offset, and rounding.
"""
import numpy as np

DAY = 1440
DAYS = 7
TYPES = ["classroom", "clinic", "meeting"]

# Configured values (what the installer types in): the generating rate the device assumes per person.
CONFIG_G = {"classroom": 0.0035, "clinic": 0.0050, "meeting": 0.0050}


def _lambda_series(rng, occupied, breaks, n_min, closed):
    """Air changes per hour per minute. Closed rooms leak at `closed`; windows or a fan raise it (0.5 to 8)."""
    lam = np.full(n_min, closed)
    t = 0
    while t < n_min:
        seg = int(rng.integers(30, 181))
        r = rng.random()
        if r < 0.55:
            val = closed
        elif r < 0.80:
            val = rng.uniform(2.5, 4.5)       # fan
        else:
            val = rng.uniform(4.0, 8.0)       # windows open
        lam[t:t + seg] = np.clip(val, 0.5, 8.0)
        t += seg
    # during breaks, windows are often opened
    for (a, b) in breaks:
        if rng.random() < 0.5:
            lam[a:b] = np.clip(rng.uniform(4.0, 8.0), 0.5, 8.0)
    return lam


def make_room(kind, seed):
    rng = np.random.default_rng([seed, TYPES.index(kind) + 100])
    N = DAYS * DAY
    people = np.zeros(N)
    breaks = []
    if kind == "classroom":
        vol = rng.uniform(150, 250)
        g_true = rng.uniform(0.0030, 0.0040)
        cls = int(rng.integers(25, 46))
        for d in range(5):  # Mon-Fri
            base = d * DAY
            absent = int(rng.integers(0, 4))
            size = cls - absent
            start = base + 8 * 60 + 25 + int(rng.integers(-5, 6))
            # trickle in over 8 min, out over 6 min. Lessons 8:30-10:30, 10:50-12:30, 13:15-15:30 ; breaks between
            lesson = [(start, start + 125), (start + 145, start + 245), (start + 290, start + 425)]
            for i, (a, b) in enumerate(lesson):
                people[a:b] = size
            for i in range(2):
                gap = (lesson[i][1], lesson[i + 1][0])
                breaks.append(gap)
                if rng.random() < 0.25:     # some breaks only half the class leaves
                    people[gap[0]:gap[1]] = size // 2
            people[start - 8:start] = np.linspace(0, size, 8)
            people[lesson[-1][1]:lesson[-1][1] + 6] = np.linspace(size, 0, 6)
        closed = rng.uniform(0.5, 2.5)
        sens_noise = rng.uniform(5, 10)
    elif kind == "clinic":
        vol = rng.uniform(60, 120)
        g_true = rng.uniform(0.0045, 0.0055)
        for d in range(6):  # Mon-Sat (Sat morning)
            base = d * DAY
            open_, close = base + 8 * 60, base + (13 if d == 5 else 17) * 60
            level = rng.uniform(5, 30)
            n = level
            for t in range(open_, close):
                if (t - open_) % 60 == 0:
                    level = float(np.clip(level + rng.normal(0, 6), 3, 30))
                if (t - open_) % 5 == 0:
                    n = float(np.clip(n + 0.3 * (level - n) + rng.normal(0, 2), 0, 30))
                people[t] = n
            people[open_:open_ + 10] *= np.linspace(0.2, 1, 10)
            people[close:close + 15] = np.linspace(people[close - 1], 0, 15)
            breaks.append((base + 12 * 60 + 30, base + 13 * 60))
        people = np.round(people)
        closed = rng.uniform(0.5, 3.0)
        sens_noise = rng.uniform(5, 10)
    else:
        vol = rng.uniform(30, 60)
        g_true = rng.uniform(0.0045, 0.0055)
        for d in range(5):
            base = d * DAY
            t = base + 9 * 60 + int(rng.integers(0, 60))
            for _ in range(int(rng.integers(3, 6))):
                dur = int(rng.integers(30, 91))
                size = int(rng.integers(2, 13))
                if t + dur + 10 > base + 18 * 60:
                    break
                people[t:t + dur] = size
                people[t - 3:t] = np.linspace(0, size, 3)
                people[t + dur:t + dur + 3] = np.linspace(size, 0, 3)
                t += dur + int(rng.integers(10, 90))
        closed = rng.uniform(0.5, 2.5)
        sens_noise = rng.uniform(5, 10)
    people = np.round(people)
    lam = _lambda_series(rng, people > 0, breaks, N, closed)
    out = float(rng.uniform(420, 470))
    offset = float(rng.uniform(-30, 30))
    # configured volume is a little wrong in real life: +-10 percent
    vol_cfg = vol * float(rng.uniform(0.9, 1.1))
    # true CO2 by the exact one-minute solution
    c = np.empty(N)
    cur = out
    for t in range(N):
        l = lam[t] / 60.0
        prod = people[t] * g_true * 60.0 / (vol * 1000.0) * 1e6   # ppm per minute
        e = np.exp(-l)
        cur = out + (cur - out) * e + prod * (1 - e) / l
        c[t] = cur
    # sensor: 60 s lag, noise, offset, rounding
    k = 1 - np.exp(-1.0)
    m = np.empty(N)
    s = out
    for t in range(N):
        s += (c[t] - s) * k
        m[t] = s
    meas = np.round(m + offset + rng.normal(0, sens_noise, N))
    true_q = np.where(people > 0, lam * vol * 1000.0 / 3600.0 / np.maximum(people, 1), np.nan)  # L/s per person
    return dict(kind=kind, seed=seed, volume=vol, volume_cfg=vol_cfg, g_true=g_true, g_cfg=CONFIG_G[kind],
                outdoor=out, offset=offset, noise=sens_noise, co2_true=c, co2_meas=meas,
                people=people, lam=lam, true_fresh=true_q)


if __name__ == "__main__":
    for k in TYPES:
        r = make_room(k, 1)
        print(k, round(r["volume"]), "m3, people max", r["people"].max(), "meas range",
              r["co2_meas"].min(), r["co2_meas"].max())
