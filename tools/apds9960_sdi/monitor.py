"""Read a complete APDS-9960 diagnostic cycle from the WCH-Link SDI port."""

import argparse
from contextlib import nullcontext
import math
from pathlib import Path
import re
import time

import serial


SAMPLE = re.compile(
    r"SAMPLE n=(\d+) status=([0-9A-F]{2}) p=(\d+) c=(\d+) "
    r"r=(\d+) g=(\d+) b=(\d+)"
)
DONE = re.compile(r"DONE stage=complete errors=(\d+) fifo=([01]) stop=(\d+)")
SAMPLE_COUNT = 100


def capture(port, seconds, log):
    deadline = time.monotonic() + seconds
    pending = bytearray()
    active = False
    samples = {}
    with serial.Serial(port, 115200, timeout=0.2) as connection:
        while time.monotonic() < deadline:
            pending.extend(connection.read_until(size=128))
            if b"\n" not in pending:
                if len(pending) > 128:
                    pending.clear()
                continue
            line = pending.decode("ascii", errors="replace").strip()
            pending.clear()
            if log is not None:
                print(line, file=log, flush=True)

            if line.startswith("APDS9960 sdi=1 "):
                active = True
                samples.clear()
            if not active or not line:
                continue

            match = SAMPLE.fullmatch(line)
            if match:
                index = int(match[1])
                samples[index] = (int(match[2], 16), *map(int, match.groups()[2:]))
                continue

            print(line, flush=True)
            if line.startswith("DONE stage="):
                return summarize(line, samples)

    print(f"No complete diagnostic cycle in {seconds:g} seconds.")
    return 2


def summarize(line, samples):
    result = DONE.fullmatch(line)
    if result is None:
        print("FAIL at the stage reported above.")
        return 1

    print(f"Valid samples {len(samples)}/{SAMPLE_COUNT}")
    if samples:
        for label, values in zip(
            ("STATUS", "Proximity", "Clear", "Red", "Green", "Blue"),
            zip(*samples.values()),
        ):
            if label == "STATUS":
                status = ", ".join(f"0x{value:02X}" for value in sorted(set(values)))
                print(f"{label} {status}")
            else:
                print(f"{label} {min(values)}..{max(values)}")

    complete = samples.keys() == set(range(1, SAMPLE_COUNT + 1))
    if not complete or result[1] != "0" or result[3] != "0":
        print("FAIL or incomplete proximity/RGBC acquisition.")
        return 1
    print("PASS for I2C, register readback, and valid proximity/RGBC reads.")
    if result[2] == "0":
        print("FAIL for gesture FIFO.")
        return 1
    print("PASS for raw gesture FIFO.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--seconds", type=float, default=65)
    parser.add_argument("--log", type=Path, help="Save the raw SDI lines.")
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error("--seconds must be positive")

    try:
        output = args.log.open("w", encoding="utf-8") if args.log else nullcontext()
        with output as log:
            return capture(args.port, args.seconds, log)
    except OSError as exc:
        parser.exit(2, f"{exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
