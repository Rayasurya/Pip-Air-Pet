"""Copies Pip's new log rows into one growing archive on the Mac.

Pip keeps only about 10 days of minute rows. This script fetches them over the home Wi-Fi and
merges them into data/pip/*_all.csv, keyed by timestamp, so history is never lost. Safe to run
any number of times. launchd runs it every 15 minutes (tools/pip-archive.plist).

Run by hand:  tools/.venv/bin/python tools/archive_pip.py
"""
import csv
import io
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

HOSTS = ["pip.local", "192.168.1.11"]
FILES = {"log": ["/log_old.csv", "/log.csv"], "hours": ["/hours_old.csv", "/hours.csv"], "marks": ["/marks.csv"]}
LAST_ERROR = ""
OUT = Path(__file__).resolve().parents[1] / "data" / "pip"


def fetch(path):
    for host in HOSTS:
        try:
            with urllib.request.urlopen(f"http://{host}{path}", timeout=20) as r:
                return r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return ""  # Pip is up, the file just doesn't exist yet
            continue
        except Exception as e:
            global LAST_ERROR
            LAST_ERROR = f"{host}: {e}"
            continue
    return None


def rows_with_time(text):
    """Rows whose first column is a real date (rows written before the clock was set are skipped)."""
    header, rows = None, []
    for row in csv.reader(io.StringIO(text)):
        if not row:
            continue
        if row[0] == "time":
            header = row
        elif len(row[0]) == 16 and row[0][4] == "-" and row[0][10] == " ":
            rows.append(row)
    return header, rows


def merge(name, paths):
    header, fresh, reached = None, [], False
    for p in paths:
        text = fetch(p)
        if text is None:
            continue
        reached = True
        h, rows = rows_with_time(text)
        header = header or h
        fresh += rows
    if not reached:
        return None
    dest = OUT / f"{name}_all.csv"
    known = {}
    if dest.exists():
        with dest.open() as f:
            r = csv.reader(f)
            header = next(r, header) or header
            for row in r:
                if row:
                    known[row[0]] = row
    added = 0
    for row in fresh:
        if row[0] not in known:
            added += 1
        known[row[0]] = row
    if header is None:
        return 0
    tmp = dest.with_suffix(".tmp")
    with tmp.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for k in sorted(known):
            w.writerow(known[k])
    tmp.replace(dest)
    return added


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y-%m-%d %H:%M")
    results = {name: merge(name, paths) for name, paths in FILES.items()}
    line = f"{stamp} " + " ".join(f"{k}={'unreachable' if v is None else '+' + str(v)}" for k, v in results.items())
    if LAST_ERROR and any(v is None for v in results.values()):
        line += f"  ({LAST_ERROR})"
    with (OUT / "archive.log").open("a") as f:
        f.write(line + "\n")
    print(line)
    return 0 if all(v is not None for v in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
