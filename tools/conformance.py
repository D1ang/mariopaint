"""Conformance harness: does each recompiled function match the ROM, call by call?

1. Oracle: the genuine ROM (MP_REALFRAME=1) runs the canvas tour — title click,
   then every bottom-bar button. MP_PROFILE lists every JSR/JSL target it
   executed, and whether a recompiled body is registered for it.
2. The same tour runs again with all of those under lockstep validation
   (MP_VALIDATE, snesrecomp's recomp_timed_add_validate): on each call the
   native body runs on a saved copy of the machine, the genuine routine then
   runs for real, and WRAM/VRAM/CGRAM/OAM are compared when it returns.
   Up to 3000 calls per function are checked: every call of a per-frame
   routine across the whole tour (the toolkit default of 300 stopped before the
   eraser was clicked and hid its bomb-icon bug), while the SPC upload helper's
   125k calls stay affordable.
   Execution stays genuine throughout, so one wrong function cannot hide or
   cause another's failure, and the run's snapshots must equal the oracle's.

PASS = called at least once, no memory or I/O-register mismatch on any checked call, and no
call skipped (a skipped call - a jump exit, a body that presents a frame itself, or one
that falls back to the interpreter for a callee - was not checked, so the
function is not verified). A body that calls a frame driver (main.c's
k_frame_drivers) is checked frame by frame: it waits at the driver while the
ROM routine runs up to its own driver call, the machines are compared there,
and it resumes after the genuine frame. Such routines are listed with an F
suffix (frames=N in results.txt) and run as framed intercepts. A call where an
interrupt wrote WRAM the native body read (irqdep=N) cannot be compared - the
ROM routine may have acted on the handler's value - and does not count either
way. A routine that returns with M/X
widths different from the ROM fails (its caller would run at the wrong width).
Other register/flag mismatches are reported, not failed:
tools/intercept_bisect.py is the check for callers that read them.

  py tools/conformance.py <rom> [--update]

Runs every scenario in canvas_tour.SCENARIOS (in parallel) and sums each
routine's counts; --scenario NAME checks one. Prints "conformance: P/N pass". Per-function lines go to scratch/conf/results.txt.
tools/conformance_baseline.txt holds the pass count; the run exits 1 if it drops
(--update rewrites it).
"""
import os, sys, re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import canvas_tour  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "scratch", "conf")
BASELINE = os.path.join(HERE, "conformance_baseline.txt")

# Frame drivers: their native bodies present a frame (begin/end_frame), which
# is how the native-driven build paces itself, not a translation of the ROM
# routine. Run natively they would advance the capture clock mid-check.
FRAME_DRIVERS = {"01E2CE", "01E794", "01E7C9"}

# Routines entered by JMP (abs,x) or $01:E393 from a jump table, never by JSR/JSL, so the
# profile never lists them. They return through their dispatcher's caller's
# return address, which the validator already handles, so they can be checked
# like any other routine; they just have to be named here.
# Routines to keep on the original code even though they pass. Empty by
# choice: native routines may finish faster than on the SNES, so fewer lag
# frames happen and a frame-counted animation (colour cycling, the bomb
# fuse, Gnat Attack) can run a step ahead. That is accepted as an
# improvement; conformance (the logic, call by call) is what must match.
# Native bodies that wait for an interrupt-driven variable by frames where
# the ROM spins (a native body cannot take the NMI mid-spin): same end
# state, different timing, so they never run as timed intercepts.
TIMING_SENSITIVE = set()
# Pass under check but break the timed loop when run natively (so not in
# k_verified; tools/timed_smoke.py finds such breakage).
# $01:8000: with the whole title routine native, the title screen now and then
# shows a frame without its picture (letters scenario, after the click at
# (164,72); $8260 and $8308 native under the ROM's $8000 do not).
NOT_TIMED = {"018000"}

JUMP_ENTERED = ["008BEB", "009564", "009598", "0095E7",
                "009760", "009853", "00987B", "0098C3", "0098F8", "009C25", "009DAB", "009DFB",
                # Gnat Attack phases, entered through $01:E393's inline tables
                "01C012", "01AF14", "01AFDB", "01C3C4",
                # title states, entered through $01:8308's JMP ($8310,x)
                "01834B", "0183B6", "0184E2", "018521", "0185A3", "018641",
                "0187F9", "01885F", "0188E3", "018AAF",
                # bottom-bar handlers, entered through $95E7's table
                "009928", "00995B", "00998B", "0099E1", "009A35", "009A9E",
                "009B63", "009B96", "009BBA", "00A4B4", "00C42A", "00C6EF",
                "00CAD7", "00E7D9", "0096A1",
                # the mode bar's buttons (mode 7)
                "00D869", "00EB7A", "00CE22", "00E3F7", "00C9E3", "009734", "0096A6",
                # bar mode 8's buttons
                "00EAA4", "00E9A1", "00EA12", "00E848", "00EA69",
                # the music editor's bar (mode 11)
                "00F9A7", "00FA08", "00FEF9", "00FA65", "00EF6E",
                # the stamp editor's top bar
                "00A509", "00A55B", "00A637", "00A6F7", "00A792", "00A8A8", "00A8FF", "00A95D", "00A9BE", "00A617"]


# Gnat Attack's boss: the phase handlers ($A5F3 enters them through $01:E393)
# and the boss's steps ($B5F4/$B60F jump to them through tables).
BOSS_PHASES = ["01B0CA", "01B153", "01B17B", "01B27D", "01B3FA", "01B481", "01B4F4"]
BOSS_STEPS = ["01B62A", "01B62E", "01B66F", "01B6A1", "01B6AA", "01B70F", "01B784", "01B7A9", "01B7C7",
              "01B7E3", "01B841", "01B8A8", "01B8C8"]
JUMP_ENTERED += BOSS_PHASES + BOSS_STEPS
# Entered by JMP/JML from their callers: the dab at the cursor ($9C25's
# freehand), the masked single-pixel draw ($B051), the window tables
# ($01:D3BA).
JUMP_ENTERED += ["00AAEF", "00B32D", "00E64B", "009CC7"]
# Title state 1, its click test (JMP) and the letters' handlers (JMP
# ($8D71,X)).
JUMP_ENTERED += ["01833F", "018D35", "018D99", "018DAE", "018DC3", "018DD8", "018DFA", "018E0F",
                 "018E8E", "018EA3", "018EC0", "018F04"]
# Of the routines named here, those that return with RTL: the profile does
# not list them, so their return size has to be given.
LONG_ENTERED = {"00E64B", "0096CD", "00B046"}
# The title letters' own callees, so the handlers-alone scenarios check them.
JUMP_ENTERED_CALLEES = ["0096CD", "00B046", "00B05A", "00DA7B", "018B6E", "018BB5", "018C19"]
# Called once at boot, before the profiler's window ($84D5, $01:8328) or
# at the soft reset ($82D5), or through the tool table ($94E5, the empty
# slot): checked wherever a scenario reaches them.
JUMP_ENTERED += ["0084D5", "018328", "0082D5", "0094E5"]
# Main states, entered through JMP ($87B0,X) by $09D6 ($87A8), and the
# sound test's row handler (JMP ($C941,X)).
JUMP_ENTERED += ["00C900", "00D505", "00D03E", "00D2F6", "00C94B", "00CF9B", "00EC88", "00E4B9", "00CC10",
                 "00E27C", "00DE67", "00E388"]
JUMP_ENTERED += JUMP_ENTERED_CALLEES

# Scenarios checked without the routines listed (see check_scenario).
GNAT_LOOP = {"01A505", "01A5F3", "01A60E", "01C012", "01AF14", "01AFDB", "01C3C4",
             "01B0CA", "01B153", "01B17B", "01B27D"}
INNER = {"gnatinner": GNAT_LOOP, "gnatinner2": GNAT_LOOP, "gnatinner3": GNAT_LOOP,
         # and once more without the pause menu, for the routines inside it
         "gnatinner4": GNAT_LOOP | {"01C5F8", "01C622"},
         # the boss phases alone (their dispatcher $A5F3 and the loop off),
         # then the boss's steps alone (their dispatchers off too)
         # The demo: the title loop hands over to it by unwinding the stack
         # (the title's stack point), which cuts one call of the per-frame
         # demo input ($0F:C000) and of $8A75 short; they are checked in
         # the other scenarios. ($0F:C000's playback itself matched the ROM
         # on all 4070 calls of a run with only it under check.)
         "demo": {"0FC000", "008A75"},
         # the routines entered from these, checked with them off
         "drawinner": {"009C25"},
         "drawinner2": {"00B051"},
         "shapesinner": {"00B051"},
         "shapesinner2": {"009C25"},
         "modesinner": {"00D869"},
         "copyinner": {"01D3BA"},
         "specialclick": {"00BBD8"},
         "gnatbossinner": GNAT_LOOP - set(BOSS_PHASES),
         "gnatbossinner2": GNAT_LOOP | {"01B5F4", "01B60F"},
         # the rocket ($A9BE) with the clicks that lead to it
         "rocketinner": {"0087A8", "008BEB", "008BA9", "009598", "00A9BE"},
         # the wave ($A617, $01:CFD5) likewise
         "waveinner": {"0087A8", "008BEB", "008BA9", "009598", "00A617"},
         "waveinner2": {"0087A8", "008BEB", "008BA9", "009598", "00A617", "01CFD5"}}
# Scenarios checked with the jump-entered handlers alone (see check_scenario).
HANDLERS_ALONE = {"bars", "bars2", "bars3", "titlestates",
                  "modes", "modes2", "modes3", "modes4", "modes5", "modes6", "modes7", "patbar", "musicbar",
                  "stampfx", "stampfx2", "stampfx3", "stampfx4", "stampfx5", "stampfx6", "stampfx7", "stampfx8", "stampfx9", "musicexit",
                  "letters"}
DISPATCHERS = {"008BEB", "009564", "009598", "0095E7", "018308"}


def snaps(d):
    return {f: open(os.path.join(d, f), "rb").read() for f in sorted(os.listdir(d)) if f.endswith(".snap")}


def check_scenario(rom, name):
    """Oracle run (profile) then lockstep-validated run of one tour scenario."""
    pts = canvas_tour.SCENARIOS[name]
    oracle = os.path.join(OUT, name, "oracle")
    r = canvas_tour.run(rom, pts, oracle, real=True, extra_env={"MP_PROFILE": "5000"})
    prof = re.findall(r"^PROF (\w{6}) +\d+ +\w\w (JSR|JSL) .*recompiled", r.stderr, re.M)
    executed = set(re.findall(r"^PROF (\w{6}) ", r.stderr, re.M))
    spec = ",".join([a + ("L" if k == "JSL" else "") for a, k in prof if a not in FRAME_DRIVERS] +
                    [a + ("L" if a in LONG_ENTERED else "") for a in JUMP_ENTERED if a not in dict(prof)])
    if name in INNER:
        # The routines inside a loop that is now checked whole: without the
        # loop (and its phases) under check, each is checked on its own.
        spec = ",".join(x for x in spec.split(",") if x.rstrip("LF") not in INNER[name])
    if name in HANDLERS_ALONE:
        # Only the jump-entered handlers: with their dispatcher also under
        # check, a handler runs inside its call and is never checked itself.
        spec = ",".join(a + ("L" if a in LONG_ENTERED else "") for a in JUMP_ENTERED if a not in DISPATCHERS)
    checked = os.path.join(OUT, name, "validated")
    r = canvas_tour.run(rom, pts, checked, real=True, extra_env={"MP_VALIDATE": spec,
                                                                 "SNESRECOMP_VALIDATE_MAX": "3000"})
    if snaps(oracle) != snaps(checked):
        print(f"WARNING: validation changed the {name} run (snapshots differ from the oracle)")
    rows = re.findall(r"^VALIDATE (\w{6}) calls=(\d+) mem_fail=(\d+) reg_fail=(\d+) mx_fail=(\d+) skipped=(\d+) "
                      r"irq=(\d+)(?: frames=(\d+))?(?: irqdep=(\d+))?(.*)$", r.stderr, re.M)
    return dict(prof), executed, rows


def main():
    from concurrent.futures import ThreadPoolExecutor
    args = sys.argv[1:]
    update = "--update" in args
    rom = [a for a in args if not a.startswith("--")][0]
    names = list(canvas_tour.SCENARIOS)
    if "--scenario" in args:
        names = [args[args.index("--scenario") + 1]]

    with ThreadPoolExecutor(max_workers=min(len(names), os.cpu_count() or 2)) as pool:
        outs = list(pool.map(lambda n: check_scenario(rom, n), names))

    # Sum each routine's counts over the scenarios; keep the first failure.
    kind, executed, total = {}, set(), {}
    for name, (prof, exe, rows) in zip(names, outs):
        kind.update(prof)
        executed |= exe
        for addr, calls, mem, reg, mx, skip, irq, frames, irqdep, first in rows:
            t = total.setdefault(addr, [0, 0, 0, 0, 0, 0, "", 0, 0])
            t[8] += int(irqdep or 0)
            for k, v in enumerate((calls, mem, reg, mx, skip, irq)):
                t[k] += int(v)
            t[7] += int(frames or 0)
            if not t[6] and int(mem):
                t[6] = f" [{name}]{first}"
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "executed.txt"), "w") as f:
        f.write("\n".join(sorted(executed)) + "\n")

    passed, verified = 0, []
    with open(os.path.join(OUT, "results.txt"), "w") as f:
        for addr, (calls, mem, reg, mx, skip, irq, first, frames, irqdep) in total.items():
            # Calls an interrupt landed in are compared in full except for what
            # the handler itself wrote (WRAM, video memory, I/O registers).
            ok = calls > 0 and mem == 0 and mx == 0 and skip == 0
            passed += ok
            if ok and addr not in TIMING_SENSITIVE and addr not in NOT_TIMED:
                # F: it waits for frames, so it runs as a framed intercept.
                long_ret = kind.get(addr) == "JSL" or addr in LONG_ENTERED
                verified.append(addr + ("L" if long_ret else "") + ("F" if frames else ""))
            f.write(f"{addr} {'PASS' if ok else 'FAIL'} calls={calls} mem_fail={mem} reg_fail={reg} mx_fail={mx} "
                    f"skipped={skip} irq={irq} frames={frames} irqdep={irqdep}{first}\n")
    rows = total
    # MP_TIMED format: what main.c's k_verified should hold.
    with open(os.path.join(OUT, "verified.txt"), "w") as f:
        f.write(",".join(sorted(verified)) + "\n")
    print(f"conformance: {passed}/{len(rows)} pass over {len(names)} scenarios "
          f"(details: scratch/conf/results.txt)")

    base = int(open(BASELINE).read().split()[0]) if os.path.exists(BASELINE) else 0
    if update or not os.path.exists(BASELINE):
        open(BASELINE, "w").write(f"{passed} {len(rows)}\n")
    elif passed < base:
        print(f"REGRESSION: {passed} < baseline {base}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
