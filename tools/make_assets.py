#!/usr/bin/env python3
"""One command from your ROM to images and sounds.

  python3 tools/make_assets.py ROM [--out scratch/assets] [--zip assets.zip]
                                   [--skip-logs]

1. plays every tour scenario on the genuine ROM (the launcher with
   MP_REALFRAME, as conformance.py's oracle runs) and as the launcher plays
   it (translated code on), with the ROM access logger on
   (SNESRECOMP_ROMLOG) -> scratch/romlog/<scenario>[.timed].bin(.assets)
2. builds the ROM catalogue from those logs (tools/rom_catalog.py)
3. copies the assets out of the ROM into the pack (tools/extract_assets.py)
4. converts the pack to PNG and WAV (tools/convert_assets.py)
5. with --zip, packs the result into one zip file

--skip-logs reuses the logs already in scratch/romlog. The launcher is the
one canvas_tour.py runs (build/Debug/mp_launcher.exe, or --exe).
"""
import argparse
import os
import subprocess
import sys
import zipfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import canvas_tour  # noqa: E402

# Scenarios that only repeat another one's clicks for the conformance check.
REPEATS = {"titlestates", "musicbar", "gnatinner", "gnatinner2", "gnatinner3", "gnatinner4",
           "rocketinner", "waveinner", "waveinner2"}


def step(msg):
    print(f"make_assets: {msg}", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--out", default=os.path.join(ROOT, "scratch", "assets"))
    ap.add_argument("--zip")
    ap.add_argument("--exe")
    ap.add_argument("--skip-logs", action="store_true")
    ap.add_argument("--jobs", type=int, default=2)
    a = ap.parse_args()
    rom = os.path.abspath(a.rom)
    logdir = os.path.join(ROOT, "scratch", "romlog")
    os.makedirs(logdir, exist_ok=True)
    if a.exe:
        canvas_tour.EXE = a.exe

    if not a.skip_logs:
        names = [n for n, v in canvas_tour.SCENARIOS.items() if v is not None and n not in REPEATS]
        step(f"playing {len(names)} scenarios on the ROM with the access logger")

        def run(job):
            # Each scenario twice: on the genuine ROM, and as the launcher
            # plays it (translated code on), whose C code reads some bytes
            # the ROM's code does not (a word where the ROM reads a byte).
            name, real = job
            log = os.path.join(logdir, f"{name}.bin" if real else f"{name}.timed.bin")
            canvas_tour.run(rom, canvas_tour.SCENARIOS[name],
                            os.path.join(ROOT, "scratch", "romlog_runs", name + ("" if real else ".timed")),
                            real=real, extra_env={"SNESRECOMP_ROMLOG": log})
            return name, os.path.exists(log + ".assets")
        with ThreadPoolExecutor(a.jobs) as pool:
            bad = [n for n, ok in pool.map(run, [(n, r) for n in names for r in (True, False)]) if not ok]
        if bad:
            sys.exit(f"make_assets: no log from {', '.join(bad)} (is the launcher built with romlog?)")

    py = sys.executable
    logs = sorted(os.path.join(logdir, f) for f in os.listdir(logdir) if f.endswith(".bin"))
    step("building the ROM catalogue")
    subprocess.run([py, os.path.join(HERE, "rom_catalog.py")] + logs, check=True)
    step("extracting the asset pack")
    subprocess.run([py, os.path.join(HERE, "extract_assets.py"), rom, "--out", a.out], check=True)
    step("converting to PNG and WAV")
    subprocess.run([py, os.path.join(HERE, "convert_assets.py"), "--pack", a.out, "--logs", logdir], check=True)

    if a.zip:
        n = 0
        with zipfile.ZipFile(a.zip, "w", zipfile.ZIP_DEFLATED) as z:
            for base, _, files in os.walk(a.out):
                for f in files:
                    p = os.path.join(base, f)
                    z.write(p, os.path.relpath(p, a.out))
                    n += 1
        step(f"{n} files in {a.zip}")
    step("done")


if __name__ == "__main__":
    main()
