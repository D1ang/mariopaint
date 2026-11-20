#!/usr/bin/env python3
"""Run every tour scenario in the default timed mode (k_verified native, as
the shipped launcher runs) and report scenarios that hang or that end with
fewer screenshots than the ROM's run. Conformance checks routines one call at
a time; this checks that the native routines together still play.

  python3 tools/timed_smoke.py ROM [scenario ...]
"""
import os, sys, subprocess, shutil
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(__file__))
import canvas_tour

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
    got = len([f for f in os.listdir(out) if f.endswith(".png")])
    return name, bool(hung), got, want


def main():
    rom = sys.argv[1]
    names = sys.argv[2:] or [n for n, v in canvas_tour.SCENARIOS.items() if v is not None]
    bad = 0
    with ThreadPoolExecutor(2) as pool:
        for name, hung, got, want in pool.map(lambda n: run(rom, n), names):
            flag = "HANG" if hung else ("SHORT" if got < want else "ok")
            bad += flag != "ok"
            print(f"{name:14s} {flag:5s} {got}/{want} shots", flush=True)
    print(f"timed smoke: {len(names) - bad}/{len(names)} scenarios play through")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
