"""Drive the recomp headlessly into the canvas and click a list of points.

Each action parks the scripted pointer (snesrecomp "@x:@y" mouse steps), clicks,
waits, and screenshots, so one run shows what every toolbar button does.
Same script works on the genuine ROM with --real (MP_REALFRAME=1), which is the
ground truth to compare against.

  py tools/canvas_tour.py <rom> [--real] [--out DIR] [--scenario NAME] [x,y ...]

With no points, clicks every bottom-bar slot. --scenario picks one of
SCENARIOS (canvas, music, animation, menu, view, gnat, gnatpause). Screens land in DIR (default
scratch/tour[_real]) as NN_x_y.png, plus a .snap (WRAM/VRAM/CGRAM) per shot.
"""
import os, subprocess, sys, glob, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "..", "build", "Debug", "mp_launcher.exe")

# Title: park the cursor on Mario's walking line (y=$0794/2) and click until he's hit.
TITLE = ["100:@128:@168:0:300"] + [
    st for f in range(400, 1400, 4) for st in (f"{f}:@128:@168:L:2", f"{f + 2}:@128:@168:0:2")]
CANVAS_AT = 1500     # canvas is up well before this
STEP = 90            # frames per action: move 30, click, settle, shoot

# Bottom-bar slot centres (SNES px, y=212) — the real ROM's layout.
BOTTOM = [(x, 212) for x in (23, 47, 62, 78, 93, 112, 135, 158, 175, 192, 213, 235)]

# The arrow at the right of the bottom bar swaps in the mode bar: animation
# (80), music (102), save/load/colouring menu (129), full view (192) and
# Gnat Attack (216). Each scenario opens one mode from the canvas and plays
# with it; every step is one click STEP frames after the previous one.
MODE_BAR = (235, 212)
SCENARIOS = {
    "canvas": BOTTOM,
    "music": [MODE_BAR, (102, 212), (128, 112), (60, 80), (80, 92), (100, 70),
              (45, 15), (140, 86), (160, 98), (180, 64), (61, 172), (128, 200),
              (29, 172), (91, 172), (216, 182)],
    "animation": [MODE_BAR, (80, 212), (128, 112), (128, 112), (100, 90), (150, 120),
                  (60, 60), (200, 150), (16, 212)],
    "menu": [MODE_BAR, (129, 212), (150, 52), (128, 112), (150, 85), (128, 112),
             (150, 125), (128, 112)],
    "view": [MODE_BAR, (192, 212), (128, 112), (128, 112)],
    "gnat": [MODE_BAR, (216, 212), (128, 112), (100, 100), (150, 80), (60, 150),
             (200, 120), (128, 60), (90, 170), (170, 40), (128, 128)],
    # Gnat Attack's pause menu (right button): resume (the cursor starts on
    # the middle choice), swat a little, pause again and quit (lowest choice).
    "gnatpause": [MODE_BAR, (216, 212), (128, 112), (100, 100), (128, 112, "R"), (128, 112),
                  (150, 80), (128, 112, "R"), (128, 136), (128, 112), (60, 60)],
}


def run(rom, points, out, real=False, extra_env=None):
    script, shots = list(TITLE), []
    f = CANVAS_AT
    for x, y, *btn in points:
        b = btn[0] if btn else "L"
        script.append(f"{f}:@{x}:@{y}:0:30")
        script.append(f"{f + 30}:@{x}:@{y}:{b}:3")
        script.append(f"{f + 33}:@{x}:@{y}:0:{STEP - 33}")
        shots.append(f + STEP - 5)
        f += STEP
    os.makedirs(out, exist_ok=True)
    for p in glob.glob(os.path.join(out, "*")):
        os.remove(p)
    env = dict(os.environ,
               SNESRECOMP_MOUSE_SCRIPT=" ".join(script),
               SNESRECOMP_SHOT_FRAMES=",".join(map(str, [CANVAS_AT - 5] + shots)),
               SNESRECOMP_EXIT_FRAME=str(f + 5),
               SNESRECOMP_DUMP_PREFIX="s")
    if real:
        env["MP_REALFRAME"] = "1"
    env.update(extra_env or {})
    r = subprocess.run([os.path.abspath(EXE), "--headless", os.path.abspath(rom)],
                       cwd=out, env=env, capture_output=True, text=True)
    with open(os.path.join(out, "run.log"), "w") as f:
        f.write(r.stdout + r.stderr)
    pngs = sorted(glob.glob(os.path.join(out, "screenshot_*.png")))
    snaps = sorted(glob.glob(os.path.join(out, "s_f*.snap")))
    names = ["00_canvas"] + [f"{i + 1:02d}_{p[0]}_{p[1]}" + ("_" + p[2] if len(p) > 2 else "")
                             for i, p in enumerate(points)]
    for name, png, snap in zip(names, pngs, snaps):
        shutil.move(png, os.path.join(out, name + ".png"))
        shutil.move(snap, os.path.join(out, name + ".snap"))
    for t in glob.glob(os.path.join(out, "s_f*.ppu.txt")):
        os.remove(t)
    if len(pngs) != len(names):
        print(r.stderr[-2000:])
    print(f"{len(pngs)}/{len(names)} shots in {out}")
    return r


if __name__ == "__main__":
    args = sys.argv[1:]
    real = "--real" in args
    scenario = None
    if "--scenario" in args:
        i = args.index("--scenario"); scenario = args[i + 1]; del args[i:i + 2]
    out = None
    if "--out" in args:
        i = args.index("--out"); out = args[i + 1]; del args[i:i + 2]
    args = [a for a in args if a != "--real"]
    rom, pts = args[0], [tuple(map(int, a.split(","))) for a in args[1:]]
    if scenario:
        pts = SCENARIOS[scenario]
    out = out or os.path.join(HERE, "..", "scratch",
                              ("tour_real" if real else "tour") + (f"_{scenario}" if scenario else ""))
    run(rom, pts or BOTTOM, out, real)
