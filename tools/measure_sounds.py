#!/usr/bin/env python3
"""Measure the capture that tools/check_sounds.c leaves behind.

Splits sdlaudio.raw into one slot per effect and reports peak and RMS for each,
so a sound that has gone silent is named rather than merely missing.

    SDL_AUDIODRIVER=disk ./build/gavaga_soundcheck
    python3 tools/measure_sounds.py [sdlaudio.raw]

Exits non-zero if any effect is silent, so it can be used as a check.
"""
import math
import struct
import sys

# In GV_SFX_* order, minus GV_SFX_NONE. Keep in step with check_sounds.c.
EFFECTS = [
    ("shot",                "sample"),
    ("enemy explosion",     "sample"),
    ("flagship hit",        "sample"),
    ("flagship explosion",  "synth"),
    ("player explosion",    "sample"),
    ("dive",                "synth"),
    ("tractor beam",        "synth"),
    ("captured",            "sample"),
    ("rescue",              "sample"),
    ("extra life",          "sample"),
    ("stage start",         "sample"),
    ("game over",           "synth"),
    ("perfect stage",       "synth"),
]

FLOOR = 0.02      # below this a slot is silence, not a quiet effect


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "sdlaudio.raw"
    try:
        raw = open(path, "rb").read()
    except FileNotFoundError:
        sys.exit(f"{path} not found - run the soundcheck first, with SDL_AUDIODRIVER=disk")

    n = len(raw) // 4
    if n == 0:
        sys.exit(f"{path} is empty")
    samples = struct.unpack(f"<{n}f", raw[:n * 4])

    # The disk driver does not write in real time, so derive the slot length
    # from the file rather than assuming it matches check_sounds.c's delay.
    slot = n // len(EFFECTS)
    print(f"{n/44100:.1f}s captured, {slot/44100:.2f}s per effect\n")
    print(f"{'effect':20s} {'source':8s} {'peak':>7s} {'rms':>8s}  verdict")

    silent = []
    for i, (name, source) in enumerate(EFFECTS):
        chunk = samples[i * slot:(i + 1) * slot]
        peak = max((abs(v) for v in chunk), default=0.0)
        rms = math.sqrt(sum(v * v for v in chunk) / max(1, len(chunk)))
        ok = peak > FLOOR
        if not ok:
            silent.append(name)
        print(f"{name:20s} {source:8s} {peak:7.3f} {rms:8.4f}  {'audible' if ok else 'SILENT'}")

    print()
    if silent:
        print(f"{len(silent)} silent: {', '.join(silent)}")
        return 1
    print(f"all {len(EFFECTS)} effects audible")
    return 0


if __name__ == "__main__":
    sys.exit(main())
