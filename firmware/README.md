# Air Pet firmware

Everything here was written and tested before the parts arrived. When they come, you only wire, upload and tune.

## What is where

| Path | What it does |
|------|--------------|
| `lib/pet/config.h` | Every number you might tune: pins, CO2/heat/humidity limits, the dark level, timings. |
| `lib/pet/mood.*` | Turns readings into a mood. Holds a new mood 30 s before showing it. Detects the "window opened" moment. |
| `lib/pet/gesture.*` | Tap (pet), long-press (explain), double-tap (quiet mode). |
| `lib/pet/pet.*` | Draws Pip. Shapes, not images, so moods morph smoothly. |
| `lib/pet/console.*` | Typed test commands (`help`, `co2 1400`, `mood hot`, `demo`...). |
| `lib/pet/raster_gfx.h` | Shape drawing shared by the device and the Mac preview, so both look the same. |
| `src/main.cpp` | The device: OLED, SCD40, LDR, touch, buzzer. |
| `test/test_logic/` | Logic tests that run on the Mac. |
| `../tools/preview/` | Renders every mood as a GIF on the Mac. |

## Commands

Run these from this `firmware` folder.

```bash
pio test -e native
```
Runs the logic tests on the Mac. No board needed.

```bash
../tools/preview/run.sh
```
Renders all moods into `../preview-out/` as GIFs.

```bash
pio run -e esp32dev -t upload
```
Builds and uploads to the board (board plugged in by USB).

```bash
pio device monitor
```
Shows the live log from the board. Press Ctrl+C to exit.

Calibration: `calibrate` (or `calibrate 450`) after Pip has sat in fresh air for 3 minutes. See Day 7.

## Wiring

See `../docs/wiring.svg`: one card per part, colour coded. Some ESP32 boards print pins as `G21` or just `21` instead of `D21`. Same pin.

## Test commands (type in the serial monitor)

Open the monitor with `pio device monitor`, type a command, press Enter.

| Command | What it does |
|---------|--------------|
| `help` | Lists the commands. |
| `scan` | Lists the parts the board can see: the OLED (0x3C) and the SCD40 (0x62). |
| `status` | Current readings, mood and frame rate. |
| `demo` | Plays every mood, 6 s each. Good for filming. |
| `mood hot` | Forces one mood: happy, stuffy, gasping, hot, dry, damp, asleep, relieved, loved. |
| `co2 1400` / `temp 33` / `hum 80` | Fake readings, so the real mood rules run without the sensor. |
| `dark` / `light` | Fake the room light. Sleep still waits 60 s of dark. |
| `pet` / `explain` / `quiet` | Same as tap, long-press and double-tap. |
| `auto` | Back to the real sensors. |

| `dump` / `clearlog` | Print or delete the minute log. To save it on the Mac, close the monitor and run `tools/.venv/bin/python tools/pull_log.py` from the `air-pet` folder. |

**Forecast ("stuffy soon"):** Pip needs 31 minutes of readings before it can forecast. With Wi-Fi set, it uses the trained model, which needs the time of day. Without Wi-Fi, it uses the straight-line method. `status` shows which one is running.

**Wi-Fi (optional, only for the clock):** copy `src/secrets.example.h` to `src/secrets.h` and fill in your Wi-Fi name and password. `secrets.h` is git-ignored. Never commit it.

At power-on the board prints which parts it found, and the OLED shows a short check screen ("CO2 sensor OK / MISSING").

## Day 4: first power-on (parts arrived)

1. **Board only first.** Plug the ESP32 into the Mac with the USB-C to Micro USB cable. Nothing else connected.
2. Run the upload command. If it says no port is found:
   - Try the cable in another port, and make sure it is the data cable from the order.
   - Some boards need you to hold the **BOOT** button while "Connecting..." shows, then release.
   - If the Mac still does not see it, install the CP2102 driver from Silicon Labs (official site only).
3. Open the monitor. You should see `Pip (Air Pet) starting.` and `No CO2 sensor` (nothing is wired yet). That is fine.
4. Unplug USB before wiring anything. **Always unplug before changing wires.**

## Day 5: OLED

1. Do Step 1 of the wiring cards (rails and the SDA and SCL rows), then the OLED card. Read the 4 labels on the OLED pins first.
2. Upload. The check screen appears, then Pip.
3. Type `demo` in the monitor. Pip plays every mood on the real screen, even before the sensors are wired.
4. If the image is shifted, noisy or has a stripe on one edge, your OLED uses the SSD1306 chip. In `platformio.ini`, change `build_flags =` to `build_flags = -DOLED_SSD1306` and upload again.

## Day 7: sensors

1. Wire the SCD40 to the same 4 lines as the OLED (3V3, GND, D21, D22). Wire the LDR module: VCC to 3V3, GND to GND, AO to D34.
2. Open the monitor. Every 5 s you get a line like:
   `co2=640 temp=26.1 hum=58 ldr=1200 dark=0 mood=happy`
3. **Set the dark level:** note `ldr` with the room lit, then with the lights off. In `config.h`, set `LDR_DARK_LEVEL` halfway between. If `ldr` goes **down** when dark, set `LDR_DARK_IS_HIGH = false`.
4. Breathe gently on the SCD40 for 10 s. CO2 should jump. After 30 s the pet turns stuffy.
5. The SCD40 needs a few minutes after power-on to settle. Early readings may be off.
6. **Temperature:** the SCD40 can read 1 to 3 C high because the board warms it. Compare with any room thermometer and set `TEMP_OFFSET_C` in `config.h`.
7. **Altitude** is already set to Bengaluru (920 m) in `config.h`. CO2 readings depend on air pressure.
8. **CO2 calibration (do this once now, then once a month).**
   - Pip turns the SCD40's self-calibration **off** at every boot. Self-calibration assumes the room sees fresh air every week. A closed, lived-in room fools it, and its corrections arrive as sudden jumps that Kin could read as breathing.
   - To calibrate by hand:
     1. Take Pip outdoors into shade, or put it at a wide-open window, still powered by USB or a power bank.
     2. Keep your face and breath away from it for **3 minutes**.
     3. Type `calibrate` in the serial monitor.
   - Pip prints how far it shifted the reading. A shift of tens of ppm is normal.
   - Outdoor air in Bengaluru is about 420–460 ppm, so near a busy road use `calibrate 450`.

## Day 8: touch and buzzer

Touch: VCC to 3V3, GND to GND, SIG to D27. Buzzer: VCC to 3V3, GND to GND, I/O to D25.

- Tap: hearts and a chirp.
- Hold 1 s: the reason screen for 4 s.
- Double-tap: quiet mode on/off (speaker icon top-left when on).

## Tuning (Day 10 to 11)

All limits live in `lib/pet/config.h`. After any change, run `pio test -e native` before uploading. The tests check that the mood rules still behave.
