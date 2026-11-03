#!/usr/bin/env python3
"""Fits an A2L made offline (xcpclient --offline --elf) to a controller built for the PC.

XCPlite's Linux configuration keeps the EPK in calibration segment 0 (address 0x80000000), so the
controller's own calibration segments are numbers 1, 2, ... An offline A2L does not know that: it
has the EPK at 0xFFFFFF00 and numbers the segments from 0. This moves the EPK and shifts every
segment by one, which is what xcpclient --fix-a2l does when it can reach the controller.

    fix_host_a2l.py controllers/<dir>/docs/<name>_host.a2l   (in place)
"""
import re
import sys


def fix(text):
    if "ADDR_EPK 0x80000000" in text:
        return text  # already fitted
    text = text.replace("ADDR_EPK 0xFFFFFF00", "ADDR_EPK 0x80000000")

    # Segment-relative addresses: 0x8000_0000 + (segment << 16) + offset
    def shift(m):
        return f"0x{int(m.group(0), 16) + 0x10000:08X}"

    lines = []
    for line in text.split("\n"):
        if re.match(r"\s*/begin (MEMORY_SEGMENT|INSTANCE|CHARACTERISTIC|AXIS_PTS)\b", line):
            line = re.sub(r"0x8[0-9A-Fa-f]{7}\b", shift, line)
        line = re.sub(r"(/begin SEGMENT )(\d+)", lambda m: m.group(1) + str(int(m.group(2)) + 1),
                      line)
        lines.append(line)
    return "\n".join(lines)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    with open(path, encoding="utf-8") as f:
        text = f.read()
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(fix(text))
    print(f"fix_host_a2l: {path}: EPK in segment 0, calibration segments from 1")


if __name__ == "__main__":
    main()
