"""Run every tour scenario on the native build and compare each screenshot
with the genuine ROM's (the oracle shots tools/conformance.py leaves in
scratch/conf/<scenario>/oracle).

  py tools/tour_check.py <rom> [--scenario NAME]

Prints one line per scenario and exits 1 if any shot differs. The native
build runs the routines in main.c's k_verified natively, so this is the
check that they behave in the real game as they did under validation.

A difference is a signal, not proof of a bug: native routines finish
faster than on the SNES (fewer lag frames), so frame-counted animations
may be a step ahead; that is accepted. To tell timing from logic, hand
routines back with MP_INTERP_FUNCS and see what makes the shot match.
"""
import os, sys
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import canvas_tour  # noqa: E402

CONF = os.path.join(HERE, "..", "scratch", "conf")


def check(rom, name):
    oracle = os.path.join(CONF, name, "oracle")
    if not os.path.isdir(oracle):
        return name, None, ["no oracle shots: run tools/conformance.py first"]
    out = os.path.join(HERE, "..", "scratch", "native", name)
    canvas_tour.run(rom, canvas_tour.SCENARIOS[name], out)
    bad = []
    shots = sorted(f for f in os.listdir(oracle) if f.endswith(".png"))
    for f in shots:
        p = os.path.join(out, f)
        if not os.path.exists(p):
            bad.append(f + " (missing)")
            continue
        a = Image.open(os.path.join(oracle, f)).convert("RGB")
        b = Image.open(p).convert("RGB")
        if ImageChops.difference(a, b).getbbox():
            bad.append(f)
    return name, len(shots), bad


def main():
    args = sys.argv[1:]
    rom = [a for a in args if not a.startswith("--")][0]
    names = list(canvas_tour.SCENARIOS)
    if "--scenario" in args:
        names = [args[args.index("--scenario") + 1]]
    with ThreadPoolExecutor(max_workers=min(len(names), os.cpu_count() or 2)) as pool:
        results = list(pool.map(lambda n: check(rom, n), names))
    failed = False
    for name, total, bad in results:
        ok = total is not None and not bad
        failed |= not ok
        shown = f"{total - len(bad)}/{total} identical" if total is not None else "skipped"
        print(f"{name:10s} {shown}" + ("" if ok else "  differ: " + ", ".join(bad)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
