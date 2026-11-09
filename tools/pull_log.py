"""Downloads Pip's minute log from the board over USB and saves it as CSV.

Usage (board plugged in, serial monitor closed):
  tools/.venv/bin/python tools/pull_log.py            # auto-finds the port
  tools/.venv/bin/python tools/pull_log.py --port /dev/cu.usbserial-0001
Saves to ai/data/real_log_<date>.csv, plus real_marks_<date>.csv (body diary marks) and
real_hours_<date>.csv (hourly record), when the board has them. Real data, so no SIMULATED header.
Pull at least every two weeks: the board keeps about two weeks of minutes (marks are never dropped).
"""
import argparse
import glob
import sys
import time
from datetime import datetime
from pathlib import Path

import serial  # pyserial


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.SLAB_USBtoUART*") + glob.glob("/dev/cu.wchusbserial*"))
    if not ports:
        sys.exit("No board found. Plug it in, close the serial monitor, or pass --port.")
    return ports[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--out")
    a = ap.parse_args()
    port = a.port or find_port()
    out = Path(a.out or Path(__file__).resolve().parent.parent / "ai" / "data" / f"real_log_{datetime.now():%Y%m%d_%H%M}.csv")
    with serial.Serial(port, 115200, timeout=2) as s:
        time.sleep(2.0)  # the board may restart when the port opens
        s.reset_input_buffer()
        s.write(b"dump\n")
        parts, current, deadline = {"LOG": [], "MARKS": [], "HOURS": []}, None, time.time() + 300
        while time.time() < deadline:
            raw = s.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("---") and line.endswith(" START---"):
                current = line[3:-9]
            elif line.startswith("---") and line.endswith(" END---"):
                if line[3:-7] == "HOURS":
                    break
                current = None
            elif current in parts and line:
                parts[current].append(line)
    lines = parts["LOG"]
    if not lines:
        sys.exit("Got no log. Is the firmware up to date? Try typing dump in the serial monitor.")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    print(f"{len(lines) - 1} minutes saved to {out}")
    for name, rows in (("marks", parts["MARKS"]), ("hours", parts["HOURS"])):
        if len(rows) > 1:
            extra = out.with_name(out.name.replace("real_log_", f"real_{name}_"))
            extra.write_text("\n".join(rows) + "\n")
            print(f"{len(rows) - 1} {name} saved to {extra}")


if __name__ == "__main__":
    main()
