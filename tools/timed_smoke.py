#!/usr/bin/env python3
"""Run every tour scenario in the default timed mode (k_verified native, as
the shipped launcher runs) and report scenarios that hang, that end with
fewer screenshots than the ROM's run, or that show a blank screen where the
ROM's run shows a picture. Conformance checks routines one call at
a time; this checks that the native routines together still play.

  python3 tools/timed_smoke.py ROM [scenario ...]
"""
import os, sys, subprocess, shutil
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(__file__))
import canvas_tour
from PIL import Image

LIMIT = 240          # seconds: a scenario runs in well under a minute


def run(rom, name):
    out = os.path.join(canvas_tour.HERE, "..", "scratch", "smoke", name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    hung = []
    real_run = subprocess.run

    def limited(args, cwd=None, env=None, **kw):
        try:
            return real_run(args, cwd=cwd, env=env, capture_output=True, text=True, timeout=LIMIT)
        except subprocess.TimeoutExpired:
            hung.append(True)
            return subprocess.CompletedProcess(args, -9, "", "")
    canvas_tour.subprocess.run = limited
    try:
        canvas_tour.run(rom, canvas_tour.SCENARIOS[name], out, real=False)
    finally:
        canvas_tour.subprocess.run = real_run
    want = len([p for p in canvas_tour.SCENARIOS[name] if p not in ("notitle", "sram") and p[0] != "poke"]) + 1
    shots = sorted(f for f in os.listdir(out) if f.endswith(".png"))
    # A blank screen where the ROM's run shows a picture: the native code
    # broke the display (black music editor, for instance).
    oracle = os.path.join(canvas_tour.HERE, "..", "scratch", "conf", name, "oracle")
    blank = []
    for f in shots:
        o = os.path.join(oracle, f)
        if os.path.exists(o) and colours(os.path.join(out, f)) == 1 and colours(o) > 1:
            blank.append(f)
    return name, bool(hung), len(shots), want, blank


def colours(path):
    return len(set(Image.open(path).convert("RGB").get_flattened_data()))


def main():
    rom = sys.argv[1]
    names = sys.argv[2:] or [n for n, v in canvas_tour.SCENARIOS.items() if v is not None]
    bad = 0
    with ThreadPoolExecutor(2) as pool:
        for name, hung, got, want, blank in pool.map(lambda n: run(rom, n), names):
            flag = "HANG" if hung else ("SHORT" if got < want else ("BLANK" if blank else "ok"))
            bad += flag != "ok"
            print(f"{name:14s} {flag:5s} {got}/{want} shots" + (f", blank: {' '.join(blank[:4])}" if blank else ""),
                  flush=True)
    print(f"timed smoke: {len(names) - bad}/{len(names)} scenarios play through")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
