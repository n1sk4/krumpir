#!/usr/bin/env python3
"""Fits an A2L made offline (xcpclient --offline --elf) to a controller built for the PC.

XCPlite's Linux configuration keeps the EPK in calibration segment 0 (address 0x80000000), so the
controller's own calibration segments are numbers 1, 2, ... An offline A2L does not know that: it
has the EPK at 0xFFFFFF00 and numbers the segments from 0. This moves the EPK and shifts every
segment by one, which is what xcpclient --fix-a2l does when it can reach the controller.

    fix_host_a2l.py controllers/<dir>/docs/<name>_host.a2l   (in place)
    fix_host_a2l.py --selftest

Exit status: 0 ok, 1 the self-check failed, 2 usage.
"""
import argparse
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


def selftest():
    a2l = ("ADDR_EPK 0xFFFFFF00\n"
           "/begin MEMORY_SEGMENT par \"\" DATA FLASH INTERN 0x80000000 0x100\n"
           "/begin SEGMENT 0 2 0 0 0\n"
           "/begin CHARACTERISTIC kp \"\" VALUE 0x80000004 R 0 1 0 1\n"
           "x 0x80000004\n")
    out = fix(a2l)
    assert "ADDR_EPK 0x80000000" in out
    assert "INTERN 0x80010000 0x100" in out and "VALUE 0x80010004" in out
    assert "/begin SEGMENT 1 2" in out
    assert out.endswith("x 0x80000004\n")  # only the address lines move
    assert fix(out) == out  # a fitted A2L stays as it is
    print("fix_host_a2l: selftest ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("a2l", nargs="?", help="the offline A2L, fitted in place")
    p.add_argument("--selftest", action="store_true", help="check the fitting and exit")
    a = p.parse_args()
    if a.selftest:
        selftest()
        return 0
    if not a.a2l:
        p.error("an A2L file is needed")
    with open(a.a2l, encoding="utf-8") as f:
        text = f.read()
    with open(a.a2l, "w", encoding="utf-8", newline="") as f:
        f.write(fix(text))
    print(f"fix_host_a2l: {a.a2l}: EPK in segment 0, calibration segments from 1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
