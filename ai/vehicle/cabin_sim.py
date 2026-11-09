"""SIMULATED parked-car cabin for the left-behind detector.

Everything here is a SIMULATION built on stated assumptions. Nothing is measured in a real car.
One scenario = a trip, then ignition off, doors open, adults leave, doors close, then a long park.
Output is what the CO2/temperature sensor would report every 5 s, plus the hidden truth.

Assumptions (all SIMULATED, ranges are drawn per scenario):
  Cabin volume          car 2.5-3.5 m3, school van 6-10 m3
  Air changes per hour  parked and closed 0.3-1.5 (a measured study found under 1/h with windows closed)
                        window cracked 3-6, driving 8-30 (vents), driving on recirculation 4-8
  Doors open            fast flush, 30-80 air changes per hour, open for 20-90 s (hard cases 10-25 s)
  Outdoor CO2           420-480 ppm
  CO2 per person        adult 15-19 L/h, infant 2.5-4.5, toddler 5-7, dog 6-12 (assumed ranges)
  Sun heating           cabin temperature moves toward a target with a time constant of 10-20 min,
                        so it is mostly there after 30-60 min. Sunny: target 55-70 C.
                        Mild: ambient (15-35 C) plus 3-10 C.
  Sensor                first-order lag 60 s, noise sd 5-10 ppm (white), constant offset +-30 ppm,
                        reading rounded to 1 ppm. Temperature: same lag, noise 0.1 C, offset +-0.5 C.
  Not modelled          sensor failure above 60 C, sensor self-heating, wind, people moving in and out
                        after the doors close, engine heat, air conditioner drying.

Fleet mode (v5, generate_fleet): one vehicle, many parkings. All SIMULATED assumptions, every one a guess.
generate(seed) above is unchanged: one random vehicle per seed, no history. Fleet mode keeps the
vehicle fixed and plays several days, then one TEST parking.
  Fixed per vehicle (drawn once from the seed, inside the ranges above): volume, sensor offset
    (+-30 ppm), sensor noise sd (5-10 ppm), a closed-leak mean (0.3-1.5 /h), a cracked-leak mean
    (3-6 /h), and a window habit (share of trips with the window cracked, 10-40%).
  Varies by day (parking):
    leak rate = vehicle mean x uniform(0.75, 1.25) for the same window state ("about +-25%"), then
      clipped to the old range (0.3-1.5 closed, 3-6 cracked), so v4's assumed ranges stay true and the
      comparison with v4 is fair. Wind, fan state and door seals are not modelled.
    outdoor CO2 = uniform(420, 480) for the day, plus a linear drift of uniform(-10, +10) ppm over the
      whole run (about 2 h), so it is not constant within a day either.
    window habit: each history trip is cracked with the vehicle's habit probability. The TEST parking
      takes its window state from plan(seed), so the cells stay balanced.
    trip and adults: drawn as in generate(). History empties are "hard" (4 adults, long drive, brief door
      opening) only 30% of the time, not 50%, so the learner is not handed extra informative parkings.
    history length: an empty parking lasts uniform(60, 90) min (a school van parks for hours). The test
      parking lasts 90 min, as before.
  History parkings are mostly empty. About 10% hold a dog, toddler or infant who is COLLECTED IN TIME:
    the adults return uniform(5, 60) min after ignition off and the series ends. Some are too short to
    alarm, and some (cracked window) are long enough that no alarm has fired. They are the poisoning
    cases the learner has to survive. Nobody tells the device which parkings these are.
  The learner never sees the hidden truth (leak, outdoor, offset) in this file; only sensor readings.
  Not modelled: a different driver changing the habit, seasonal change in leak, a window left open wide,
    a fan left on, a sensor swap, anything that changes the vehicle between days, power loss.

Run:  tools/.venv/bin/python ai/vehicle/cabin_sim.py --seeds 1-200 --out build/vehicle/dev
"""
import argparse
import json
import math
import os

import numpy as np

STEP = 5.0  # seconds between sensor readings
PARK_MIN = 90  # minutes simulated after ignition off
OCCUPANTS = ["none", "none", "infant", "toddler", "dog"]  # 40% empty, 20% each other


def plan(seed):
    """Deterministic category mix: over any 80 consecutive seeds every combination is balanced."""
    return {
        "occupant": OCCUPANTS[seed % 5],
        "cracked": (seed // 5) % 4 == 0,  # 25% window cracked
        "van": (seed // 20) % 4 == 0,  # 25% school van
    }


def _step_co2(c, outdoor, gen_ppm_h, ach, dt_h):
    """Exact solution of dC/dt = g - ach*(C - outdoor) over one step. Stable at any ach."""
    ach = max(ach, 1e-6)
    c_inf = outdoor + gen_ppm_h / ach
    return c_inf + (c - c_inf) * math.exp(-ach * dt_h)


def generate(seed):
    rng = np.random.default_rng(seed)
    p = plan(seed)
    occ = p["occupant"]
    van = p["van"]
    cracked = p["cracked"]
    volume = rng.uniform(6, 10) if van else rng.uniform(2.5, 3.5)
    outdoor = rng.uniform(420, 480)
    ach_closed = rng.uniform(3, 6) if cracked else rng.uniform(0.3, 1.5)
    ach_open = rng.uniform(30, 80)
    # Hard empty case: long drive, 4 adults, recirculation, doors opened only briefly.
    hard = occ == "none" and rng.random() < 0.5
    if hard:
        n_adults = 4
        ach_drive = rng.uniform(4, 8)
        trip_min = rng.uniform(30, 60)
        door_open_s = rng.uniform(10, 25)
    else:
        n_adults = int(rng.integers(1, 5))
        ach_drive = rng.uniform(8, 30)
        trip_min = rng.uniform(5, 40)
        door_open_s = rng.uniform(20, 90)
    adult_lh = rng.uniform(15, 19, size=n_adults)
    g_occ = {"none": 0.0, "infant": rng.uniform(2.5, 4.5), "toddler": rng.uniform(5, 7),
             "dog": rng.uniform(6, 12)}[occ]
    leave_s = rng.uniform(10, 20)  # adults are out this long after ignition off

    # Temperature
    ambient = rng.uniform(15, 35)
    sunny = rng.random() < 0.5
    t_target = rng.uniform(55, 70) if sunny else ambient + rng.uniform(3, 10)
    tau_heat = rng.uniform(10, 20) * 60.0
    t_drive = rng.uniform(21, 25)  # air conditioner or heater keeps it comfortable while driving

    noise_sd = rng.uniform(5, 10)
    offset = rng.uniform(-30, 30)
    t_offset = rng.uniform(-0.5, 0.5)
    P = {"volume": volume, "outdoor": outdoor, "ach_closed": ach_closed, "ach_open": ach_open,
         "hard": hard, "n_adults": n_adults, "ach_drive": ach_drive, "trip_min": trip_min,
         "door_open_s": door_open_s, "adult_lh": adult_lh, "g_occ": g_occ, "leave_s": leave_s,
         "ambient": ambient, "sunny": sunny, "t_target": t_target, "tau_heat": tau_heat,
         "t_drive": t_drive, "noise_sd": noise_sd, "offset": offset, "t_offset": t_offset,
         "occupant": occ, "cracked": cracked, "van": van, "park_min": PARK_MIN, "outdoor_drift": 0.0}
    return _render(seed, P, rng)


def _render(seed, P, rng):
    """Physics and sensor for one parking, from drawn parameters. Shared by generate() and fleet mode.
    The order of the random draws here (sensor noise only) is part of the seed contract of generate()."""
    volume, outdoor0, ach_closed, ach_open = P["volume"], P["outdoor"], P["ach_closed"], P["ach_open"]
    hard, n_adults, ach_drive, trip_min = P["hard"], P["n_adults"], P["ach_drive"], P["trip_min"]
    door_open_s, adult_lh, g_occ, leave_s = P["door_open_s"], P["adult_lh"], P["g_occ"], P["leave_s"]
    ambient, sunny, t_target, tau_heat = P["ambient"], P["sunny"], P["t_target"], P["tau_heat"]
    t_drive, noise_sd, offset, t_offset = P["t_drive"], P["noise_sd"], P["offset"], P["t_offset"]
    occ, cracked, van = P["occupant"], P["cracked"], P["van"]
    drift = P["outdoor_drift"]
    outdoor = outdoor0
    sensor_tau = 60.0
    t_noise = 0.1

    n_trip = int(trip_min * 60 / STEP)
    n_park = int(P["park_min"] * 60 / STEP)
    n = n_trip + n_park
    t_off = n_trip * STEP
    t_close = t_off + door_open_s

    t = np.arange(n) * STEP
    ign = (t < t_off).astype(np.int8)
    true_co2 = np.empty(n)
    true_temp = np.empty(n)
    c = outdoor
    temp = ambient
    dt_h = STEP / 3600.0
    adults_lh = float(adult_lh.sum())
    vol_l = volume * 1000.0
    for i in range(n):
        ts = t[i]
        if drift:  # fleet mode only: outdoor CO2 moves slowly during the run
            outdoor = outdoor0 + drift * (i / n)
        if ts < t_off:
            gen_l = adults_lh + g_occ
            ach = ach_drive
            temp_goal, tau_t = t_drive, 300.0
        else:
            since = ts - t_off
            gen_l = (adults_lh if since < leave_s else 0.0) + g_occ
            ach = ach_open if ts < t_close else ach_closed
            temp_goal, tau_t = t_target, tau_heat
            if ts < t_close:  # open doors also mix the air with the outside
                temp_goal, tau_t = ambient + 0.3 * (t_target - ambient), 120.0
        gen_ppm_h = gen_l / vol_l * 1e6
        c = _step_co2(c, outdoor, gen_ppm_h, ach, dt_h)
        temp = temp_goal + (temp - temp_goal) * math.exp(-STEP / tau_t)
        true_co2[i] = c
        true_temp[i] = temp

    a = 1.0 - math.exp(-STEP / sensor_tau)
    y = true_co2[0]
    yt = true_temp[0]
    s_co2 = np.empty(n)
    s_temp = np.empty(n)
    for i in range(n):
        y += a * (true_co2[i] - y)
        yt += a * (true_temp[i] - yt)
        s_co2[i] = y
        s_temp[i] = yt
    s_co2 = np.round(s_co2 + offset + rng.normal(0, noise_sd, n))
    s_temp = np.round(s_temp + t_offset + rng.normal(0, t_noise, n), 2)

    hot = np.nonzero(true_temp >= 60.0)[0]
    meta = {
        "seed": seed, "occupant": occ, "variant": "cracked" if cracked else "closed",
        "vehicle": "van" if van else "car", "hard_empty": bool(hard), "volume_m3": round(volume, 2),
        "ach_closed": round(ach_closed, 2), "outdoor_ppm": round(outdoor0, 1), "adults": n_adults,
        "occupant_lh": round(g_occ, 2), "sunny": bool(sunny), "t_off_s": t_off,
        "t_close_s": t_close, "t_cabin_60c_s": float(t[hot[0]]) if len(hot) else None,
        "co2_at_off_ppm": round(float(true_co2[n_trip - 1]), 0), "max_true_temp_c": round(float(true_temp.max()), 1),
    }
    return {"t": t, "ign": ign, "co2": s_co2, "temp": s_temp, "true_co2": true_co2,
            "true_temp": true_temp, "meta": meta}


# ---- Fleet mode (v5): one vehicle, many parkings. The assumptions are in the module docstring. ----
HISTORY_DEFAULT = 10
LEAK_CLOSED = (0.3, 1.5)
LEAK_CRACKED = (3.0, 6.0)
LEAK_DAILY_SPREAD = 0.25
OUTDOOR_RANGE = (420.0, 480.0)
OUTDOOR_DRIFT_PPM = 10.0
HISTORY_HARD_SHARE = 0.3
HISTORY_OCCUPIED_SHARE = 0.10
FLEET_STREAM = 5150


def vehicle(seed):
    """Fixed properties of the vehicle that seed `seed` parks, forever."""
    rng = np.random.default_rng([seed, FLEET_STREAM, 100000])
    van = plan(seed)["van"]
    return {"van": van, "volume": rng.uniform(6, 10) if van else rng.uniform(2.5, 3.5),
            "offset": rng.uniform(-30, 30), "noise_sd": rng.uniform(5, 10),
            "t_offset": rng.uniform(-0.5, 0.5),
            "ach_closed_mean": rng.uniform(*LEAK_CLOSED), "ach_cracked_mean": rng.uniform(*LEAK_CRACKED),
            "crack_habit": rng.uniform(0.1, 0.4)}


def _daily_leak(rng, mean, lo, hi):
    return float(min(hi, max(lo, mean * rng.uniform(1 - LEAK_DAILY_SPREAD, 1 + LEAK_DAILY_SPREAD))))


def _fleet_parking(seed, veh, day, occupant, cracked, history):
    rng = np.random.default_rng([seed, FLEET_STREAM, day])
    cracked = bool(cracked) if cracked is not None else bool(rng.random() < veh["crack_habit"])
    outdoor = rng.uniform(*OUTDOOR_RANGE)
    drift = rng.uniform(-OUTDOOR_DRIFT_PPM, OUTDOOR_DRIFT_PPM)
    ach_closed = _daily_leak(rng, veh["ach_cracked_mean"], *LEAK_CRACKED) if cracked else \
        _daily_leak(rng, veh["ach_closed_mean"], *LEAK_CLOSED)
    ach_open = rng.uniform(30, 80)
    hard = occupant == "none" and rng.random() < (HISTORY_HARD_SHARE if history else 0.5)
    if hard:
        n_adults, ach_drive = 4, rng.uniform(4, 8)
        trip_min, door_open_s = rng.uniform(30, 60), rng.uniform(10, 25)
    else:
        n_adults, ach_drive = int(rng.integers(1, 5)), rng.uniform(8, 30)
        trip_min, door_open_s = rng.uniform(5, 40), rng.uniform(20, 90)
    adult_lh = rng.uniform(15, 19, size=n_adults)
    g_occ = {"none": 0.0, "infant": rng.uniform(2.5, 4.5), "toddler": rng.uniform(5, 7),
             "dog": rng.uniform(6, 12)}[occupant]
    leave_s = rng.uniform(10, 20)
    ambient = rng.uniform(15, 35)
    sunny = rng.random() < 0.5
    t_target = rng.uniform(55, 70) if sunny else ambient + rng.uniform(3, 10)
    tau_heat = rng.uniform(10, 20) * 60.0
    t_drive = rng.uniform(21, 25)
    if not history:
        park_min = PARK_MIN
    elif occupant == "none":
        park_min = rng.uniform(60, 90)
    else:
        park_min = rng.uniform(5, 60) + 3  # collected this long after the doors close (plus door time)
    P = {"volume": veh["volume"], "outdoor": outdoor, "ach_closed": ach_closed, "ach_open": ach_open,
         "hard": hard, "n_adults": n_adults, "ach_drive": ach_drive, "trip_min": trip_min,
         "door_open_s": door_open_s, "adult_lh": adult_lh, "g_occ": g_occ, "leave_s": leave_s,
         "ambient": ambient, "sunny": sunny, "t_target": t_target, "tau_heat": tau_heat,
         "t_drive": t_drive, "noise_sd": veh["noise_sd"], "offset": veh["offset"],
         "t_offset": veh["t_offset"], "occupant": occupant, "cracked": cracked, "van": veh["van"],
         "park_min": park_min, "outdoor_drift": drift}
    out = _render(seed * 1000 + day, P, rng)
    out["meta"]["fleet_seed"] = seed
    out["meta"]["day"] = day
    out["meta"]["ach_true"] = round(ach_closed, 3)
    out["meta"]["drift_ppm"] = round(drift, 1)
    out["meta"]["offset_ppm"] = round(veh["offset"], 1)
    out["meta"]["park_min"] = round(park_min, 1)
    return out


def generate_fleet(seed, n_history=HISTORY_DEFAULT):
    """The test parking of vehicle `seed` and its n_history earlier parkings (oldest first).
    The test parking's occupant and window state come from plan(seed). Returns (history, test)."""
    veh = vehicle(seed)
    p = plan(seed)
    hist = []
    for k in range(n_history, 0, -1):  # day k = k days before the test parking
        r = np.random.default_rng([seed, FLEET_STREAM, 200000 + k])
        occ = "none"
        if r.random() < HISTORY_OCCUPIED_SHARE:
            occ = ["infant", "toddler", "dog"][int(r.integers(0, 3))]
        hist.append(_fleet_parking(seed, veh, k, occ, None, True))
    test = _fleet_parking(seed, veh, 0, p["occupant"], p["cracked"], False)
    return hist, test


def parse_seeds(text):
    a, _, b = text.partition("-")
    return range(int(a), int(b or a) + 1)


def write_csv(seeds, out_prefix):
    os.makedirs(os.path.dirname(out_prefix) or ".", exist_ok=True)
    metas = []
    with open(out_prefix + ".csv", "w") as f:
        f.write("scenario,t_s,ignition,co2_ppm,temp_c,nominal_volume_m3\n")
        for seed in seeds:
            s = generate(seed)
            metas.append(s["meta"])
            nominal = 8 if s["meta"]["vehicle"] == "van" else 3  # what a user would set, not the true volume
            for t, g, c, tc in zip(s["t"], s["ign"], s["co2"], s["temp"]):
                f.write(f"{seed},{int(t)},{int(g)},{int(c)},{tc:.2f},{nominal}\n")
    with open(out_prefix + "_meta.json", "w") as f:
        json.dump(metas, f)
    return metas


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seeds", default="1-200", help="for example 1-200")
    ap.add_argument("--out", default="build/vehicle/dev", help="writes <out>.csv and <out>_meta.json")
    args = ap.parse_args()
    m = write_csv(parse_seeds(args.seeds), args.out)
    print(f"SIMULATED: wrote {len(m)} scenarios to {args.out}.csv")
