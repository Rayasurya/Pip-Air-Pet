"""Plots a few simulated days to sanity-check the simulator. Output is labelled SIMULATED."""
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import csv

path, start_day, n_days, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
rows = [r for r in csv.DictReader(l for l in open(path) if not l.startswith("#"))]
i0 = next(i for i, r in enumerate(rows) if r["time"].startswith(start_day))
rows = rows[i0:i0 + n_days * 1440]
x = np.arange(len(rows)) / 60.0
co2 = np.array([int(r["co2"]) for r in rows]); temp = np.array([float(r["temp"]) for r in rows])
hum = np.array([float(r["hum"]) for r in rows]); win = np.array([int(r["truth_window"]) for r in rows])
ppl = np.array([int(r["truth_people"]) for r in rows]); ldr = np.array([int(r["ldr"]) for r in rows])
fig, ax = plt.subplots(4, 1, figsize=(12, 8), sharex=True)
ax[0].plot(x, co2, lw=0.8); ax[0].axhline(1000, ls="--", c="orange"); ax[0].axhline(1500, ls="--", c="red")
ax[0].fill_between(x, 400, 400 + 300 * win, color="green", alpha=0.25, label="window open (hidden truth)")
ax[0].set_ylabel("CO2 ppm"); ax[0].legend(loc="upper right", fontsize=8)
ax[1].plot(x, temp, c="tab:red", lw=0.8); ax[1].set_ylabel("temp C")
ax[2].plot(x, hum, c="tab:blue", lw=0.8); ax[2].set_ylabel("RH %")
ax[3].step(x, ppl, c="k", lw=0.8, label="people (hidden)"); ax[3].plot(x, ldr / 4095 * 3, c="gold", lw=0.6, label="LDR (dark=high)")
ax[3].set_ylabel("people"); ax[3].legend(loc="upper right", fontsize=8); ax[3].set_xlabel(f"hours from {start_day} 00:00")
fig.suptitle(f"SIMULATED room data, {n_days} days from {start_day} (not real measurements)", color="crimson")
fig.tight_layout(); fig.savefig(out, dpi=90)
