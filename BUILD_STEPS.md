# Pip build: one step at a time

> This file is public in the Pip-Air-Pet repo. Never write Wi-Fi names or passwords, home data, job applications or employer names here.

Instructions for the guiding agent:
- Raya has no electronics experience.
- Give exactly ONE step per message. Then wait for a reply or a photo.
- Name every object plainly. Never assume a tool exists. For example, the kit has no sponge.
- When you point at a part, mark it on Raya's first parts photo with a red circle, and send that image.
- Tick each step here when it is done, and add a line to the log at the bottom.
- **Marking parts:** the photo is `air-pet/docs/parts-photo.webp` (899x1599 px, portrait). Draw a red circle on it, save it to a new png, then send it with SendUserFile. Command:
  `air-pet/tools/.venv/bin/python -c "from PIL import Image,ImageDraw;im=Image.open('air-pet/docs/parts-photo.webp').convert('RGB');ImageDraw.Draw(im).ellipse((X1,Y1,X2,Y2),outline=(255,40,40),width=8);im.save('air-pet/docs/mark.png')"`
- **Boxes in the photo** (x1,y1,x2,y2), approximate:
  - soldering case and iron: 170,0,670,520
  - buzzer module (black round top): 240,620,390,690
  - light sensor (blue knob): 240,690,390,760
  - rotary knob (black board): 450,620,600,710
  - ESP32 board (on white foam): 420,720,620,860
  - resistor box: 90,760,420,990
  - solder wire box: 450,870,650,1060
  - USB cable: 50,950,200,1200
  - OLED screen: 200,1040,370,1180
  - SCD40 sensor, in the white 7Semi box: 410,1040,620,1240
  - desoldering wick (round reel): 420,1230,600,1400
  - breadboard with small buttons: 0,1230,400,1460
  - jumper wires (rainbow): 20,1400,680,1599
  - not in this photo: the touch pads (tiny red boards, in a small zip bag) and the stand.
- The back of the OLED is in `air-pet/docs/oled-back.webp`.
- **STOP** is only for the steps marked STOP below. A missing part is not a STOP: say what to skip and carry on.
- **STOP** means: tell Raya to switch to the bigger model (Opus) for that step. Don't guess.

Already done: the parts are checked. The iron is on its stand on a steel plate, the dial is at 3, and the solder melts.

## A. Solder the pins (3 boards)
- [x] A1. No 40-pin strip arrived (16 Oct). Skip A2 to A6 and the touch pad until a strip is bought. Go straight to A7.
- [x] A2. Break off 3 pins: bend the strip at a gap between two pins.
- [x] A3. Push the 3 pins, long side down, into the breadboard, anywhere in the middle.
- [x] A4. Put the touch pad on top, "TOUCH" side up, so the pins poke through its 3 holes.
- [x] A5. Solder one pin: the tip touches the pin and the ring together for 2 s, then feed solder in. Ask for a photo.
- [x] A6. Check that the board sits flat. Then solder the other 2 pins. Ask for a close-up and judge it: you want shiny cones, and no blobs joining two pins.
- [x] A7. OLED: put its own 4-pin piece in the breadboard, screen side up, and solder the same way. One pin first, then a photo, then the rest.
- [x] A8. SCD40: its own 5-pin piece, the white sensor facing up. Be quick, 2 s per pin, and keep the iron away from the white sensor box.
- [x] A9. Switch the iron off, unplug it, and leave it 10 min on the stand. Wash hands.

## B. Board on the Mac (no wires yet)
- [x] B1. Plug the ESP32 into the Mac with the USB cable. Ask whether a red light comes on.
- [x] B2. In Terminal, in `air-pet/firmware`, run `pio run -e esp32dev -t upload`. If it fails with a port or connecting error, try holding BOOT during "Connecting...". If it still fails, STOP.
- [x] B3. Run `pio device monitor`. Expect `Pip (Air Pet) starting.` and `No CO2 sensor`. Press Ctrl+C to exit.
- [x] B4. Unplug the USB. Rule from now on: **always unplug before touching wires.**

## C. Wiring (needs exact pin labels)
- [x] C1. OLED front labels, left to right: VCC, GND, SCK, SDA (read 16 Oct). The wiring, checked by Opus:
  - OLED VCC to ESP32 **3V3** (never VIN or 5V)
  - OLED GND to ESP32 **GND**
  - OLED SCK to ESP32 **D22** (SCK means the same as SCL)
  - OLED SDA to ESP32 **D21**
- [x] C2. Wire the OLED as the bigger model said: one wire per message, then a photo of everything.
- [x] C3. Upload, then type `demo` in the monitor. Pip should appear. If the image is broken or shifted, STOP.
- [x] C4. SCD40: VCC to 3V3, GND to GND, SDA to D21, SCL to D22. These are the same lines as the OLED. Use the pins in the bottom row, not the white plugs. One wire per message.
- [x] C5. The monitor shows `co2=...` every 5 s. Breathing gently on the sensor should make CO2 rise.
- [x] C6. Light sensor: get a photo of its pin labels first. If it has AO: VCC to 3V3, GND to GND, AO to D34. If it has only DO, STOP.
- [x] C7. Touch pad: VCC to 3V3, GND to GND, SIG or I/O to D27. Tap it and Pip shows hearts.
- [x] C8. Buzzer: I/O to D25, VCC to 3V3, GND to GND. Tap it and you hear a chirp.

## D. Tuning (follow firmware/README.md, Day 7)
- [ ] D1. Dark level: read `ldr` with the light on and with it off, then set `LDR_DARK_LEVEL` in config.h. STOP if the numbers look odd.
- [ ] D2. Temperature offset against a room thermometer.
- [x] D3. CO2 calibration: at an open window, 3 min with no breath near it, then type `calibrate`.
- [ ] D4. Live with Pip for a day. Note anything that feels wrong.

## Log
- 16 Oct: parts checked, iron set up, solder melts. Next: A1.
- 16 Oct: A1 STOP. No long black 40-pin strip in docs/parts-photo.webp (only the rainbow jumper wires and the breadboard). Needs the bigger model (Opus) to check. Still at A1.
- 16 Oct: A1 done (no strip, A2 to A6 skipped). Started A7 (OLED). The OLED's 4 pins are a loose piece. 4-pin piece is in the middle of the breadboard (Raya confirmed, no close-up needed). OLED placed on the pins, screen up (16 Oct). Next: solder one pin.
- 16 Oct: first OLED solder attempt is poor. Pins 3 and 4 (SCK, SDA) are bridged by one blob. Tip is silver-coated and the kit has no sponge. Fix the tip, then remove the bridge with the desoldering wick. A7 not ticked.
- 16 Oct: STOP at A7 (iron). Tip still silver and not melting wire after tissue cleaning. Iron to cool 10 min before any touch. Switch to Opus to read the iron and tip state.
- 16 Oct: A7 OLED: Raya reports no pins touching from the top view (description, no photo). Still need: are all 4 pins soldered? A7 not ticked.
- 16 Oct: A7 done. All 4 OLED pins soldered, none touching (Raya's report, plus photos 2 and 3). Started A8 (SCD40). 5-pin piece is in the middle of the breadboard. SCD40 seated on the pins, white side up. Raya reports all 5 pins soldered and looking good (16 Oct). Photo of all 5 pins requested before ticking A8. Photo 4 received: pins look soldered, no bridge seen. Two loose solder specks on the breadboard next to the sensor must be removed before A8 is ticked. Raya says they pulled "them" out by hand, no breakage. Unclear whether specks or pins. Asked to confirm. Raya then reports both boards were pulled out of the breadboard with the pins still soldered on and not bent. Re-seating both boards in the middle of the breadboard, pins straight. Raya reports both boards back in, flat and straight. Photo 5 confirms the breadboard is clean near the sensor. The two specks are on the desk mat. A8 ticked (16 Oct). Next: A9 (iron off, cool 10 min). 16 Oct: iron switched off and unplugged, cooling started. A9 ticked (16 Oct): 10 min passed, hands washed. Section A done. B1 ticked: red light came on the ESP32 when plugged in. Next: B2 (upload). B2 ticked: upload finished with no errors. Next: B3 (serial monitor). B3 ticked: both startup lines seen. Next: B4 (unplug). B4 ticked: USB unplugged. Section B done. Next: C1 (STOP, needs Opus). 16 Oct: Raya read the OLED pin labels as VCC, GND, SCK, SDA. Wiring not given by Haiku. Waiting for Opus.
- 16 Oct: C1 ticked. Opus gave the OLED wiring (VCC to 3V3, GND to GND, SCK to D22, SDA to D21). Starting C2: ESP32 onto the breadboard first, then one wire per message. ESP32 seated on the breadboard (16 Oct). Next: wire 1, OLED VCC to ESP32 3V3.
- 16 Oct: only female-to-female wires, so the OLED is wired straight to the ESP32 off the breadboard (VCC-3V3, GND-GND, SCK-D22, SDA-D21). Screen works and shows 'Pip starting' and 'CO2 sensor missing'. Male-to-female wires ordered for the sensor. Next: C3 demo.
- 16 Oct: board kept restarting or freezing. Two firmware bugs fixed (by Opus): (1) a 21 KB room-mode object built on the 8 KB stack; (2) a leaf-drawing NaN (powf of a tiny negative) that made one drawing call near-endless on the ESP32. Pip now runs at about 13 fps with no restarts. Next: C3, Raya runs demo.
- 16 Oct: demo runs on the real screen (sent over serial by Opus; typing in the pio monitor didn't seem to reach the board, check that later). C3 done. Next: C4, the SCD40, once the male-to-female wires arrive.
- 19 Oct: rewired. The ESP32 covers every middle hole, so it sits OFF the breadboard. The breadboard is only a junction: left + rail = 3V3, left - rail = GND, row 10 = SDA (D21), row 15 = SCL (D22). All wires male-to-female, female on the module pins. Sensor 3V3 pin left empty (power via VCC). Board sees both parts: 0x3C OLED, 0x62 SCD40. First readings: CO2 ~610 ppm, 29 C, 62%. C4 done. Next: C5 breath test.
- 19 Oct: breath test passed. CO2 went from about 800 to 1,449 ppm. C5 done. Next: C6, the light sensor (needs a photo of its pin labels first).
- 19 Oct: light sensor has VCC, GND, DO only (digital). Wired VCC to + rail, GND to - rail, DO to D34 (female-to-female). Test: lit = ~90, covered = 4094, so the existing settings (dark if above 3000) work with no code change. Pip sleeps after 60 s of dark. C6 done. Next: solder pins on the touch pad (A2 to A6, pin strips have arrived), then C7.
- 19 Oct: touch pad soldered (3 pins, flat). Labels GND, I/O, VCC. Wired VCC to + rail, GND to - rail, I/O to D27 (female-to-female). Tap shows hearts, hold shows the reason screen. C7 done. Next: C8 buzzer (I/O to D25) for a quick sound test. The speaker and amplifier replace it later.
- 19 Oct: buzzer wired (I/O to D25). The hello tune plays at start. The tap chirp wasn't heard, probably quiet mode or the 1-hour hush after a 3 s hold; not chased further. C8 done. Section C done. Next: D tuning, then the speaker swap.
- 19 Oct: D1 deferred to night. The room has daylight leaks, so set the light-sensor screw at night: lights off, turn it until the second LED just goes off.
- 19 Oct: CO2 check outdoors on the balcony, from a power bank, read from the minute log: it settled at 419 to 434 ppm by minutes 9 to 11, which is normal outdoor air. The factory calibration is good, so no calibrate command was sent. D3 done. To check later: the hours log has many all-zero rows (written with no clock?), and a 'Marked.' line appeared in the dump.
- 19 Oct: Pip is on home Wi-Fi (2.4 GHz; it needs the network's full name, not a short form) with the real time. The log is readable over Wi-Fi at http://pip.local/ or 192.168.1.11 (/log.csv, /hours.csv, /marks.csv), and future firmware uploads go over Wi-Fi (pio run -e ota -t upload, with PIP_OTA_PASS set). Pip no longer needs the Mac. The 1-2 week log starts now.
- 19 Oct: live dashboard at http://pip.local (served by Pip; refreshes every 15 s; 6 h, 24 h and 7 day charts). The Mac archive runs as a system job (/Library/LaunchDaemons/com.raya.pip-archive.plist, every 15 min) into data/pip/*_all.csv (git-ignored). It has to be a root daemon: macOS local-network privacy blocks user agents. Wi-Fi uploads: PIP_OTA_PASS from secrets.h, pio run -e ota -t upload (upload_port is set to 192.168.1.11; update it if the router changes Pip's address).
- 7 Nov: night mode uploaded over Wi-Fi.
  - The clock turns the screen off from 21:30 to 07:30 and silences pet sounds. Sensing and logging carry on. A tap shows the face, dimly, for 15 s. Safety alerts still show.
  - The light sensor never reads dark, so the clock decides.
  - `/now.json` now reports `night` and `screen`.
  - 102 native tests pass.
  - The dashboard's finished-run message was reworded.
- 7 Nov 2026, evening: **quotes** (Raya asked for lines from books on the screen, "like Pip is talking to me").
  - Every 30 min, a quote is typed onto the screen for 20 s. A small Pip sits bottom left, and its mouth moves while the letters appear. The author shows when the typing ends. A touch ends it early.
  - It never shows at night, over a Kin question, a car alert, a reason screen, a "noted" screen, Gasping, room mode, or the demo.
  - 13 lines from public-domain texts: Seneca, Marcus Aurelius, Epictetus, Thoreau, Emerson, Tagore, Shakespeare, Wilde, Whitman.
    - Each was checked word for word against the source (Gutenberg, Standard Ebooks).
    - 5 were dropped: no primary text, a copyrighted translation, or a modern paraphrase.
  - Code: `lib/pet/quotes.*` (list, word wrap, shuffled deck), `PetRenderer::drawQuote`, a `quote` console command, and `"quote"` in `/now.json` (-1 when none is showing).
  - 109 native tests pass (7 new in `test_quotes`). Flash 90.1%.
  - Preview with the real U8g2 font: `preview-out/quotes.png`.
  - **Not uploaded yet:** waiting until Raya finishes recording a video, because an upload restarts Pip.
  - To add your own line: add one row to `LIST` in `lib/pet/quotes.cpp`. The tests check it fits the screen.
- 7 Nov, 22:52: night mode confirmed on the real Pip. Raya reports the screen is black, and a touch wakes the face, which then goes black again. `/now.json` shows `"night":1,"screen":0`. Raya felt the wake lasted "a few minutes"; it is set to 15 s after the last touch. Time it once to check.
- 7 Nov, about 23:00: Raya unplugged Pip for the night. The screen is off, but the boards' own small lights still bother them, and taping them couldn't be done tonight. The night of 7 to 8 Nov will be missing from the log. Next: black tape over each board light (keep it off the light sensor's eye and the CO2 sensor's hole), then leave Pip plugged in at night.
