# Pip log format

One CSV row per minute. The device log, the simulator and every AI tool use the same columns.

| Column | Meaning |
|--------|---------|
| `time` | `YYYY-MM-DD HH:MM` local time (IST). If the device has no clock yet: `+N`, minutes since power-on. |
| `co2` | CO2 ppm, the average of that minute |
| `temp` | °C |
| `hum` | relative humidity % |
| `ldr` | raw light reading 0-4095 (higher = darker with the default LDR module) |
| `mood` | mood shown at the end of the minute (device only) |
| `pred` | forecast: predicted CO2 change over the next 20 min, ppm (device only) |
| `soon` | 1 if Pip showed "stuffy soon" (device only) |
| `fb` | feedback given that minute: `bad`, `fine` or empty (device only, comfort learning) |
| `mark` | `1` if the person held Pip 3 s that minute ("I feel unwell now", body diary), else empty |
| `press` | outdoor air pressure, hPa, from Open-Meteo over Wi-Fi (hourly; empty if unknown or over 3 h old) |
| `truth_*` | simulator only: hidden facts (people in the room, window open). Never used as model input. |

The simulator's file starts with a `# SIMULATED DATA` line. Tools skip lines starting with `#`.

## Body diary files (device only)

`tools/pull_log.py` also saves these when the board has them:

- **`real_marks_<date>.csv`**: one row per mark. The columns:
  - `time`, `minutes` (how much history there was, up to 1,440)
  - `co2_mean`, `co2_max`, `hours_over_1400`, `co2_mean_last2h`
  - `temp_min`, `temp_max`, `temp_change_6h`
  - `hum_mean`, `dark_min`, `dark_min_last2h`
  - `press_change_24h` (empty if unknown)

  These describe the 24 h before the mark. The board never deletes marks.
- **`real_hours_<date>.csv`**: hourly averages, for months of history after the minute log rolls over. The columns are `hour`, `minutes`, `co2_mean`, `co2_max`, `temp`, `hum`, `dark_min`, `press` and `marks`. The board keeps about 6 months.

The board keeps about 2 weeks of minute rows. Pull at least every two weeks.
