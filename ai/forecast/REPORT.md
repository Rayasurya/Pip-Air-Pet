# Forecast report (SIMULATED DATA)

> All numbers below come from the room simulator, not real measurements. They show the pipeline
> works and which method to ship. Re-run on real logs before claiming anything about real rooms.

Data: 92 simulated days. Trained on the first 62, tested on the last 30 (never seen in training).
Task: predict CO2 20 min ahead. Alert when the prediction crosses 1000 ppm.

| Method | Error, all (ppm) | Error near the limit (ppm) | Stuffy spells | Caught ≥5 min early | Median warning (min) | Alerts | Alerts that were right |
|---|---|---|---|---|---|---|---|
| trend | 79.0 | 66.2 | 127 | 78.7% | 12.0 | 445 | 52.4% |
| linear | 70.6 | 54.8 | 127 | 18.1% | 1.0 | 215 | 90.2% |
| mlp | 47.6 | 27.6 | 127 | 87.4% | 15.0 | 264 | 69.3% |

**Shipped: `mlp`**: the simplest method that clearly beats the straight-line trend.
