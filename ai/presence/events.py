"""Turn Pip's minute-by-minute answer into a day you can read.

level_rule.py answers one question per minute: someone here, or empty. This file groups those
minutes into blocks, and it is strict about what the air can and cannot say:

  here      CO2 is held above the room's empty level. Someone is breathing in the room.
  asleep    "here", and the air stayed steady for 2 hours or more at night. This is a guess:
            Pip cannot see sleep, only air that nobody is disturbing.
  clearing  CO2 is falling the way it does in a room nobody is breathing in. Either the room
            emptied, or a window or door was opened. The air alone cannot tell which.
  empty     CO2 is back at the room's empty level.
  off       Pip logged nothing. It is shown as "Pip was off" and never guessed.

"How sure" is not a probability. A block is "sure" when any on-limit from 60 to 110 ppm gives
the same answer for it. That is the range that gave the same result on the test days
(see score_level.py).

The numbers in Reading below were set by looking at the same days they are shown on.
They are a reading aid, not a tested result. The tested result is the level rule.

Pure Python. Tests: test_events.py
"""
import statistics
from bisect import bisect_right
from collections import deque
from dataclasses import dataclass, replace
from datetime import datetime, timedelta

from level_rule import GAP_MIN, Rule, empty_level, presence

STUFFY_PPM = 1000            # the same limit Pip's face uses (CO2_STUFFY in the firmware)
SURE_LOW, SURE_HIGH = 60, 110
MIN = timedelta(minutes=1)


@dataclass(frozen=True)
class Reading:
    fall_window: int = 30     # look back this many minutes
    fall_share: float = 0.20  # "falling" = lost this share of its height above the empty level...
    fall_floor: float = 10.0  # ...and at least this many ppm
    fall_min: int = 20        # for this many minutes in a row
    rise_ppm: float = 15.0    # clearing ends when CO2 climbs this far off its low (someone breathing)
    flat_min: int = 30        # or when it stops making new lows for this long (someone holding it up)
    flat_ppm: float = 5.0
    tail_max: int = 120       # a slow, low tail after clearing is still clearing, up to this long
    arrive_back: int = 30     # look this far back from "here" for where the rise began
    arrive_ppm: float = 10.0
    steady_window: int = 30
    steady_ppm: float = 8.0   # "steady" = CO2 moved less than this (standard deviation) in 30 minutes
    sleep_min: int = 120
    sleep_bridge: int = 15    # a short unsteady stretch does not break a night
    night_from: int = 22
    night_to: int = 9


@dataclass
class Block:
    kind: str                 # here | asleep | clearing | empty | off
    start: datetime
    end: datetime             # the first minute after the block
    minutes: int = 0          # minutes Pip logged inside it
    over: float = 0.0         # median ppm above the empty level
    low: float = 0.0
    high: float = 0.0
    first: float = 0.0        # CO2 at the start and at the end
    last: float = 0.0
    agree: float = 1.0        # share of minutes where limits 60 to 110 give the same answer
    outcome: str = ""         # clearing only: emptied | held | cut
    after_off: bool = False   # starts right after a hole: the state restarted from the level
    unusual: str = ""

    @property
    def span(self):
        return int((self.end - self.start).total_seconds() // 60)

    @property
    def sure(self):
        if self.kind == "off":
            return ""
        if self.kind == "clearing":
            return "not sure"
        if self.agree >= 0.9:
            return "sure"
        return "fairly sure" if self.agree >= 0.6 else "not sure"


def _gap(rows, i):
    return i == 0 or (rows[i].t - rows[i - 1].t).total_seconds() / 60 > GAP_MIN


def smooth(rows, width=5):
    """5-minute average, never reaching across a hole."""
    out, win, total = [], deque(), 0.0
    for i, r in enumerate(rows):
        if _gap(rows, i):
            win.clear()
            total = 0.0
        win.append(r)
        total += r.co2
        while (r.t - win[0].t).total_seconds() / 60 >= width:
            total -= win.popleft().co2
        out.append(total / len(win))
    return out


def steady(rows, rd=Reading()):
    """True where CO2 barely moved over the last half hour (and Pip logged nearly all of it)."""
    out, win = [], deque()
    s = s2 = 0.0
    for i, r in enumerate(rows):
        if _gap(rows, i):
            win.clear()
            s = s2 = 0.0
        win.append(r)
        s += r.co2
        s2 += r.co2 * r.co2
        while (r.t - win[0].t).total_seconds() / 60 >= rd.steady_window:
            old = win.popleft()
            s -= old.co2
            s2 -= old.co2 * old.co2
        n = len(win)
        var = max(0.0, s2 / n - (s / n) ** 2)
        out.append(n >= rd.steady_window - 5 and var ** 0.5 < rd.steady_ppm)
    return out


def minute_states(rows, base, here, rd=Reading()):
    """here | clearing | empty for every row. Clearing is found by looking back, so it is for
    reading a finished day, not for the device."""
    n = len(rows)
    sm = smooth(rows)
    state = ["here" if h else "empty" for h in here]
    seg = run_start = 0          # start of the unbroken stretch, and of the current "here" run
    fall_run = 0
    clearing = False
    trough = 0.0
    trough_i = 0
    lows = []                    # the low so far, one entry per clearing minute
    for i in range(n):
        if _gap(rows, i):
            seg = run_start = i
            fall_run, clearing = 0, False
        if not here[i]:
            run_start = i + 1
            fall_run, clearing = 0, False
            continue
        if clearing:
            if sm[i] < trough:
                trough, trough_i = sm[i], i
            lows.append(trough)
            risen = sm[i] >= trough + rd.rise_ppm
            flat = len(lows) > rd.flat_min and lows[-1 - rd.flat_min] - trough < rd.flat_ppm
            if risen or flat:
                back = trough_i + 1 if risen else i - rd.flat_min + 1
                for q in range(max(back, run_start), i + 1):
                    state[q] = "here"
                clearing, fall_run = False, 0
            else:
                state[i] = "clearing"
            continue
        j = i - rd.fall_window
        falling = (j >= seg and (rows[i].t - rows[j].t).total_seconds() / 60 <= rd.fall_window + 5
                   and sm[i] - sm[j] <= -max(rd.fall_floor, rd.fall_share * (sm[j] - base[j])))
        fall_run = fall_run + 1 if falling else 0
        if fall_run >= rd.fall_min:
            first_fall = i - rd.fall_min + 1
            a = max(run_start, seg, first_fall - rd.fall_window)
            peak = max(range(a, first_fall + 1), key=lambda q: (sm[q], q))   # where the fall began
            for q in range(peak, i + 1):
                state[q] = "clearing"
            clearing = True
            trough_i = min(range(peak, i + 1), key=lambda q: sm[q])
            trough = sm[trough_i]
            lows = [trough]
    return state


def mark_asleep(rows, state, rd=Reading()):
    """Relabel long, steady, night-time "here" runs as asleep. Returns a new list."""
    st = steady(rows, rd)
    out = list(state)
    runs, i, n = [], 0, len(rows)
    while i < n:
        if state[i] == "here" and st[i]:
            k = i
            while k + 1 < n and state[k + 1] == "here" and st[k + 1] and not _gap(rows, k + 1):
                k += 1
            runs.append([i, k])
            i = k + 1
        else:
            i += 1
    merged = []
    for a, b in runs:
        if merged:
            pa, pb = merged[-1]
            between = range(pb + 1, a)
            close = (rows[a].t - rows[pb].t).total_seconds() / 60 <= rd.sleep_bridge
            if close and all(state[q] == "here" and not _gap(rows, q) for q in between) and not _gap(rows, a):
                merged[-1][1] = b
                continue
        merged.append([a, b])
    for a, b in merged:
        # the steady flag looks back half an hour, so the quiet began that much earlier
        a0 = a
        while a0 > 0 and a - a0 < rd.steady_window and state[a0 - 1] == "here" and not _gap(rows, a0):
            a0 -= 1
        mins = (rows[b].t - rows[a0].t).total_seconds() / 60
        mid = rows[a0].t + (rows[b].t - rows[a0].t) / 2
        night = mid.hour >= rd.night_from or mid.hour < rd.night_to
        if mins >= rd.sleep_min and night:
            for q in range(a0, b + 1):
                out[q] = "asleep"
    return out


def _runs(rows, state):
    """(first row, last row, state) for every unbroken stretch of one state."""
    out, i, n = [], 0, len(rows)
    while i < n:
        k = i
        while k + 1 < n and state[k + 1] == state[i] and not _gap(rows, k + 1):
            k += 1
        out.append((i, k, state[i]))
        i = k + 1
    return out


def backdate_arrivals(rows, state, agree, rd=Reading()):
    """The rule waits 5 minutes over the limit before it says "here". By then CO2 has been rising
    for a while, and rising CO2 means someone is breathing. Move the start back to where the rise
    began. Those minutes go beyond the tested rule, so they never count as "sure"."""
    sm = smooth(rows)
    out = list(state)
    for i in range(1, len(rows)):
        if state[i] == "here" and state[i - 1] == "empty" and not _gap(rows, i):
            a = i
            while a > 0 and i - a < rd.arrive_back and state[a - 1] == "empty" and not _gap(rows, a):
                a -= 1
            k = min(range(a, i + 1), key=lambda q: (sm[q], -q))        # the last low before the rise
            if sm[i] - sm[k] >= rd.arrive_ppm:
                for q in range(k + 1, i):
                    out[q] = "here"
                    agree[q] = False
    return out


def absorb_tails(rows, state, agree, rd=Reading()):
    """After a fall, CO2 often sits just above the limit for a while before the rule lets go.
    If that stretch is short, never climbs, hangs on a limit Pip is not sure about, and ends in
    "empty", it was the same clearing, not a person."""
    out = list(state)
    runs = _runs(rows, state)
    for (a0, a1, ka), (b0, b1, kb), (c0, c1, kc) in zip(runs, runs[1:], runs[2:]):
        if (ka, kb, kc) != ("clearing", "here", "empty") or _gap(rows, b0) or _gap(rows, c0):
            continue
        mins = (rows[b1].t - rows[b0].t).total_seconds() / 60
        if mins <= rd.tail_max and sum(agree[b0:b1 + 1]) / (b1 - b0 + 1) < 0.6:
            for q in range(b0, b1 + 1):
                out[q] = "clearing"
    return out


def read(rows, rule=Rule(), rd=Reading()):
    """Everything per row: empty level, the rule's answer, the state, and whether limits agree."""
    base = empty_level(rows, rule)
    here = presence(rows, base, rule)
    gap = rule.on_ppm - rule.off_ppm
    lo = presence(rows, base, replace(rule, on_ppm=SURE_LOW, off_ppm=SURE_LOW - gap))
    hi = presence(rows, base, replace(rule, on_ppm=SURE_HIGH, off_ppm=SURE_HIGH - gap))
    agree = [a == b == c for a, b, c in zip(lo, here, hi)]
    state = minute_states(rows, base, here, rd)
    state = backdate_arrivals(rows, state, agree, rd)
    state = absorb_tails(rows, state, agree, rd)
    state = mark_asleep(rows, state, rd)
    return base, here, state, agree


def blocks(rows, base, state, agree):
    """Group the rows into blocks. Holes in the log become "off" blocks."""
    out = []
    for i, k, kind in _runs(rows, state):
        after_off = _gap(rows, i) and i > 0
        if after_off:
            out.append(Block("off", rows[i - 1].t + MIN, rows[i].t))
        co2 = [rows[q].co2 for q in range(i, k + 1)]
        out.append(Block(
            kind=kind, start=rows[i].t, end=rows[k].t + MIN, minutes=k - i + 1,
            over=statistics.median(rows[q].co2 - base[q] for q in range(i, k + 1)),
            low=min(co2), high=max(co2), first=co2[0], last=co2[-1],
            agree=sum(agree[i:k + 1]) / (k - i + 1), after_off=after_off))
    for a, b in zip(out, out[1:] + [None]):
        if a.kind == "clearing":
            touching = b is not None and b.kind != "off" and b.start == a.end
            a.outcome = ("emptied" if b.kind == "empty" else "held") if touching else "cut"
    return out


def mark_unusual(blocks_, min_minutes=60, need_days=3, look_back=28):
    """Flag a block that goes against what the same clock time held on earlier days of the same
    kind (weekday or weekend). Only earlier days are used, and only minutes Pip logged."""
    known = [b for b in blocks_ if b.kind in ("here", "asleep", "empty")]
    if not known:
        return blocks_
    first_day = blocks_[0].start.date()

    starts = [k.start for k in known]

    def someone_share(a, b):
        seen = yes = 0
        i = max(0, bisect_right(starts, a) - 1)
        while i < len(known) and known[i].start < b:
            k = known[i]
            lo, hi = max(a, k.start), min(b, k.end)
            if lo < hi:
                m = int((hi - lo).total_seconds() // 60)
                seen += m
                yes += m if k.kind != "empty" else 0
            i += 1
        return seen, yes

    for b in known:
        if b.minutes < min_minutes:
            continue
        want = b.kind != "empty"
        weekend = b.start.weekday() >= 5
        days = same = 0
        for back in range(1, look_back + 1):
            s = b.start - timedelta(days=back)
            if s.date() < first_day:
                break
            if (s.weekday() >= 5) != weekend:
                continue
            seen, yes = someone_share(s, s + (b.end - b.start))
            if seen >= 0.8 * b.span:
                days += 1
                same += (yes * 2 >= seen) == want
        if days >= need_days and same * 4 <= days:
            other = "empty" if want else "in use"
            kind = "weekend days" if weekend else "weekdays"
            b.unusual = f"This time was {other} on {days - same} of the {days} earlier {kind} Pip logged."
    return blocks_


def stuffy_spells(rows, limit=STUFFY_PPM, min_minutes=5, join=20):
    """Stretches at or over Pip's stuffy limit. Pip can say that it happened, not why.
    Two stretches with a dip of `join` minutes or less between them are one spell.
    A spell never reaches across a hole in the log."""
    found, i, n = [], 0, len(rows)
    while i < n:
        if rows[i].co2 >= limit:
            k = i
            while k + 1 < n and rows[k + 1].co2 >= limit and not _gap(rows, k + 1):
                k += 1
            last = found[-1] if found else None
            near = last and (rows[i].t - rows[last[1]].t).total_seconds() / 60 <= join + 1
            if near and not any(_gap(rows, q) for q in range(last[1] + 1, i + 1)):
                last[1] = k
            else:
                found.append([i, k])
            i = k + 1
        else:
            i += 1
    out = [{"start": rows[i].t, "end": rows[k].t + MIN, "peak": max(r.co2 for r in rows[i:k + 1]), "also": []}
           for i, k in found if (rows[k].t - rows[i].t).total_seconds() / 60 + 1 >= min_minutes]
    for j, s in enumerate(out):
        clock = s["start"].hour * 60 + s["start"].minute
        s["also"] = sorted({e["start"].date() for e in out[:j]
                            if e["start"].weekday() == s["start"].weekday() and e["start"].date() != s["start"].date()
                            and abs(clock - (e["start"].hour * 60 + e["start"].minute)) <= 180})
    return out


def diary(rows, rule=Rule(), rd=Reading()):
    """rows -> (blocks, stuffy spells, per-row extras). The one call the page needs."""
    base, here, state, agree = read(rows, rule, rd)
    bl = mark_unusual(blocks(rows, base, state, agree))
    return bl, stuffy_spells(rows), {"base": base, "here": here, "state": state}


def day_blocks(blocks_, day):
    """The blocks of one calendar day, cut at midnight. Time outside every block (before the log
    starts, after it ends) comes back as "off": no reading means no claim."""
    a = datetime.combine(day, datetime.min.time())
    b = a + timedelta(days=1)
    out, cursor = [], a
    for k in blocks_:
        if k.end <= a or k.start >= b:
            continue
        lo, hi = max(a, k.start), min(b, k.end)
        if lo > cursor:
            out.append((Block("off", cursor, lo), cursor, lo))
        out.append((k, lo, hi))
        cursor = hi
    if cursor < b:
        out.append((Block("off", cursor, b), cursor, b))
    return out


def hm(minutes):
    h, m = divmod(int(minutes), 60)
    return f"{h} h {m:02d} min" if h else f"{m} min"


def when(a, b):
    """'23:16 to 23:58'. Day names are added when the stretch crosses midnight."""
    day = lambda d: f"{d:%a} {d.day} {d:%b} {d:%H:%M}"  # noqa: E731
    if a.date() == b.date():
        return f"{a:%H:%M} to {b:%H:%M}"
    if b == datetime.combine(a.date() + timedelta(days=1), datetime.min.time()):
        return f"{a:%H:%M} to midnight"
    return f"{day(a)} to {day(b)}"


def describe(b):
    """Plain words for one block: what Pip says, the evidence, and how sure it is and why."""
    t = lambda d: d.strftime("%H:%M")  # noqa: E731
    if b.kind == "off":
        return {"title": "Pip was off", "evidence": f"No readings from {when(b.start, b.end)} ({hm(b.span)}).",
                "reading": "Nothing is guessed for this time.", "sure": "", "why": ""}
    over = f"{abs(b.over):.0f} ppm {'above' if b.over >= 0 else 'below'} the room's empty level"
    why = {"sure": f"Any limit from {SURE_LOW} to {SURE_HIGH} ppm gives the same answer.",
           "fairly sure": f"A different limit would change {1 - b.agree:.0%} of this block.",
           "not sure": f"A different limit would change {1 - b.agree:.0%} of this block."}.get(b.sure, "")
    if b.kind == "here":
        d = {"title": "Someone here", "evidence": f"CO2 held about {over} for {hm(b.minutes)}.",
             "reading": "Someone was breathing in the room."}
    elif b.kind == "asleep":
        d = {"title": "Likely asleep",
             "evidence": f"CO2 held about {over} and barely moved for {hm(b.minutes)}.",
             "reading": "A guess. Pip can't see sleep. Steady air at night is its only clue."}
    elif b.kind == "empty":
        d = {"title": "Empty", "evidence": f"CO2 stayed about {over} for {hm(b.minutes)}.",
             "reading": "Nobody was breathing in the room."}
    else:
        d = {"title": "Air clearing",
             "evidence": f"CO2 fell from {b.first:,.0f} to {b.last:,.0f} ppm in {hm(b.minutes)}."}
        if b.outcome == "emptied":
            d["reading"] = (f"The room probably emptied around {t(b.start)}. "
                            f"The air took until {t(b.end)} to show it.")
        elif b.outcome == "held":
            d["reading"] = ("Then it stopped falling. A window or door opened, or the room was empty "
                            "for a while. The air alone can't tell which.")
        else:
            d["reading"] = "The log stops before the air settled, so Pip can't say how this ended."
        why = "Falling CO2 has two causes, and the air alone can't tell them apart."
    if b.after_off and b.kind != "clearing":
        why = (why + " " if why else "") + "Pip had just come back on, so this starts from the level alone."
    d.update(sure=b.sure, why=why)
    return d
