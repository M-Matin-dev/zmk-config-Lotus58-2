#!/usr/bin/env python3
"""Check and summarise a Lotus58 battery log saved from PuTTY.

Usage:
    python3 tools/check_battery_log.py putty.log [--interval MIN] [--quick-test]

Reads the last full dump in the file (from the "boot,time_s,..." header to
"# end"), prints what happened in each boot, and flags problems: missed voltage
samples, a boot with no readings, a log that never ran on battery.

--interval   minutes between samples the firmware was built with (default 15)
--quick-test also check the quick-test steps from docs/battery-test.md
"""

import argparse
import csv
import sys
from collections import defaultdict

HEADER = "boot,time_s,event"


def read_last_dump(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = [ln.strip() for ln in f]
    starts = [i for i, ln in enumerate(lines) if ln.startswith(HEADER)]
    if not starts:
        sys.exit("No log found: the file has no 'boot,time_s,event' header line.")
    body = []
    complete = False
    for ln in lines[starts[-1] + 1:]:
        if ln.startswith("# end"):
            complete = True
            break
        if ln and not ln.startswith("#"):
            body.append(ln)
    head = [ln for ln in lines[:starts[-1]] if ln.startswith("# ")][-4:]
    cols = lines[starts[-1]].split(",")
    rows = [dict(zip(cols, r)) for r in csv.reader(body)]
    return head, rows, complete


def hm(seconds):
    return f"{seconds // 3600}h{(seconds % 3600) // 60:02d}m"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--interval", type=int, default=15)
    ap.add_argument("--quick-test", action="store_true")
    a = ap.parse_args()

    head, rows, complete = read_last_dump(a.log)
    problems = []
    for h in head:
        print(h)
    if not complete:
        problems.append("the dump is cut off (no '# end' line); press d to print it again")

    boots = defaultdict(list)
    for r in rows:
        boots[int(r["boot"])].append(r)

    max_gap = a.interval * 60 * 1.5 + 30
    longest = None  # (seconds on battery, boot)
    for b, rs in sorted(boots.items()):
        volts = [r for r in rs if r["event"] == "volt"]
        bat = [r for r in volts if r["detail"].startswith("on_battery")]
        events = defaultdict(int)
        for r in rs:
            events[r["event"]] += 1
        last_t = int(rs[-1]["time_s"])
        print(f"\nboot {b}: {len(rs)} records, last record at {hm(last_t)}")
        for r in rs:
            if r["event"] == "usb":
                print(f"  {hm(int(r['time_s']))}  usb {r['detail']}")
        if volts:
            print(f"  volt: {len(volts)} readings ({len(bat)} on battery), "
                  f"{volts[0]['mv']} -> {volts[-1]['mv']} mV, "
                  f"max spread {max(int(v['spread_mv']) for v in volts)} mV")
        else:
            problems.append(f"boot {b}: no voltage readings")
        downs = [r["detail"] for r in rs if r["event"] == "link_down"]
        if events["link_up"] or downs:
            print(f"  link: {events['link_up']} up, {len(downs)} down {sorted(set(downs))}")
        rb = [int(r["pct"]) for r in rs if r["event"] == "rbatt"]
        if rb:
            print(f"  rbatt: {len(rb)} values, {rb.count(0)} were 0, last {rb[-1]}")
        bogus = [r for r in rs if r["event"] == "rbatt" and "not a half" in r["detail"]]
        if bogus:
            print(f"  rbatt: {len(bogus)} came from a computer link dropping, not the right half")
        hd = [r["detail"] for r in rs if r["event"] == "host_down"]
        if events["host_up"] or hd:
            print(f"  host: {events['host_up']} up, {len(hd)} down {sorted(set(hd))}")

        # Gaps between scheduled readings (USB-triggered readings also count).
        for prev, cur in zip(volts, volts[1:]):
            gap = int(cur["time_s"]) - int(prev["time_s"])
            if gap > max_gap:
                problems.append(f"boot {b}: no reading for {hm(gap)} after {hm(int(prev['time_s']))}")
        if bat:
            span = int(bat[-1]["time_s"]) - int(bat[0]["time_s"])
            if longest is None or span > longest[0]:
                longest = (span, b, bat)

    if longest:
        span, b, bat = longest
        print(f"\nlongest run on battery: boot {b}, {hm(span)} between first and last "
              f"reading, {bat[0]['mv']} -> {bat[-1]['mv']} mV")
        ended_on_usb = any(r["event"] == "usb" and int(r["time_s"]) > int(bat[-1]["time_s"])
                           for r in boots[b])
        if not ended_on_usb:
            print("  that boot ends with no USB event: it lost power (switched off or the "
                  "battery ran out) within one interval of the last reading")
    else:
        problems.append("no readings on battery at all")

    if a.quick_test:
        quick_checks(boots, a.interval, problems)

    print()
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print("  - " + p)
        sys.exit(1)
    print("OK: no problems found")


def quick_checks(boots, interval, problems):
    """Quick test: a boot that starts on USB and is unplugged (the run), more
    boots from switching off and on, the last one started on battery and
    then plugged in to read the log."""
    order = sorted(boots)
    run = next((b for b in order
                if any(r["event"] == "usb" and r["detail"] == "unplugged" for r in boots[b])), None)
    if run is None:
        problems.append("quick test: no boot has a 'usb unplugged' record")
        return
    later = [b for b in order if b > run]
    if not later:
        problems.append("quick test: no boot after the run (switch off and on again)")
        return
    bat = [r for b in [run] + later for r in boots[b]
           if r["event"] == "volt" and r["detail"].startswith("on_battery")]
    if len(bat) < 5:
        problems.append(f"quick test: only {len(bat)} readings on battery "
                        f"(expected about one every {interval} min for ~10 min)")
    early = [r for r in boots[later[-1]] if r["event"] == "volt"]
    if early and not early[0]["detail"].startswith("on_battery"):
        problems.append(f"quick test: boot {later[-1]} started on USB; switch it on before plugging in")
    if not all("+load" in r["detail"] for r in bat):
        problems.append("quick test: some readings were taken without the extra load")


if __name__ == "__main__":
    main()
