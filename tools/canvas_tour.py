"""Drive the recomp headlessly into the canvas and click a list of points.

Each action parks the scripted pointer (snesrecomp "@x:@y" mouse steps), clicks,
waits, and screenshots, so one run shows what every toolbar button does.
Same script works on the genuine ROM with --real (MP_REALFRAME=1), which is the
ground truth to compare against.

  py tools/canvas_tour.py <rom> [--real] [--out DIR] [--scenario NAME] [x,y ...]

With no points, clicks every bottom-bar slot. --scenario picks one of
SCENARIOS (canvas, draw, stamps, coloring, saveload, music, animation, menu, view, gnat, gnatpause). Screens land in DIR (default
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
    # Drawing: each pencil size, colours from the top bar, strokes on the
    # canvas, the spray, the eraser sizes and the paint can.
    "draw": [(47, 212), ("drag", 40, 60, 120, 90), (70, 15), (62, 212), ("drag", 60, 150, 200, 70),
             (130, 15), (78, 212), ("drag", 30, 100, 220, 160), (112, 212), ("drag", 140, 60, 180, 150),
             (213, 212), (175, 212), (200, 15), (93, 212), (128, 112)],
    # Stamping: the stamp tool, every flip mode (slot 23), stamps placed,
    # colours changed between them.
    "stamps": [(93, 212), (60, 60), (23, 212), (100, 60), (23, 212), (140, 60), (23, 212), (180, 60),
               (40, 15), (60, 150), (160, 15), (120, 150), (220, 15), (180, 150)],
    # The coloring book (menu, third choice): its four pages, a colour and a
    # fill on each, back out with the dog.
    "coloring": [MODE_BAR, (129, 212), (138, 164), (128, 112), (46, 212), (128, 112),
                 (72, 212), (128, 112), (96, 212), (128, 112), (119, 212), (128, 112), (223, 212),
                 # leave the picture menu (x $08-$16): $EBB5 returns
                 (15, 212), (128, 112)],
    # The title screen: each logo letter starts its own little scene (the
    # title states of $01:8308); a while to watch each, then Mario.
    "title": ["notitle"] + [pt for x in (62, 86, 107, 118, 130, 144, 163, 174, 190, 203)
                            for pt in ((x, 72), ("wait", 240))] + [(128, 168)],
    # The special start: B+Select+A held on controller 2 at the title
    # ($01:8CBF sets $0565 = 1), with a saved game: the saved animation
    # plays as an attract loop ($01:9372).
    "specialstart": ["sram", "notitle", ("pad2", "A080", 120), ("wait", 290)] + [("wait", 300)] * 5,
    # The attract loop's bottom buttons: the song on and off (x $40-$4E).
    # $BBD8 hands a click to the ROM ($BCDB), so this one is checked with
    # $BBD8 left out (conformance.py INNER).
    "specialclick": ["sram", "notitle", ("pad2", "A080", 120), ("wait", 290), (70, 210), ("wait", 300),
                     (70, 210), ("wait", 300), ("wait", 300)],
    # The save/load menu opened and left by its exit button ($CF9B ->
    # $CF50: the canvas back, the previous state).
    "menuexit": [MODE_BAR, (129, 212), ("wait", 120), (12, 210), ("wait", 300), (100, 100), ("wait", 100)],
    # The title left alone: after $800 idle frames the demo ($0F:C00E)
    # plays ($0F:C000 feeds its recorded mouse input). The run ends while
    # it plays: its end jumps to the soft reset ($0082B8), which the
    # validator cannot compare.
    "demo": ["notitle", ("wait", 2200)] + [("wait", 300)] * 5 + [("wait", 290)],
    # The title clicked through another way: the random pick of $01:E20C at
    # start ($0547) lands on the other sound bank and the red-framed canvas.
    "title2": ["notitle"] + [(x, y) for y in (40, 64) for x in (40, 72, 104, 136, 168, 200, 224)]
              + [(128, 120), (60, 150), (200, 150), ("wait", 600), (128, 112), (47, 212),
                 ("drag", 40, 60, 200, 150)],
    # The music editor's sample songs (bottom bar), each played a while,
    # the time signatures and back.
    "music2": [MODE_BAR, (102, 212), (128, 112), (46, 212), ("wait", 300), (148, 212), ("wait", 600),
               (166, 212), ("wait", 600), (190, 212), ("wait", 600), (82, 212), (98, 212),
               (128, 212), (222, 212)],
    # Animation Land with 4 frames: a stroke in each, then the player bar
    # (bottom right) and its buttons.
    "animation2": [MODE_BAR, (80, 212), (80, 56), (128, 112), ("drag", 40, 40, 100, 90), (180, 60),
                   ("drag", 150, 40, 220, 90), (60, 150), ("drag", 40, 130, 100, 180), (180, 150),
                   ("drag", 150, 130, 220, 180), (232, 212), ("wait", 300), (36, 212), ("wait", 300),
                   (56, 212), ("wait", 300), (76, 212), ("wait", 300), (96, 212), ("wait", 300)],
    # Gnat Attack through its waves: the gnat counts $7F:05A0..05A3 cleared
    # again and again (each time the game ends a step; five steps a wave),
    # up to the boss, game over and the restart.
    "gnatwaves": [MODE_BAR, (216, 212), ("wait", 200), (128, 112), ("wait", 200)] + [
        pt for _ in range(16)
        for pt in (("poke", "7F05A0=00"), ("poke", "7F05A1=00"), ("poke", "7F05A2=00"),
                   ("poke", "7F05A3=00"), ("wait", 240))],
    # The real stamp tool (bottom bar 168-183), its page arrows (16-31), then
    # the stamp row (184-199): each stamp of the top row picked and placed.
    "stamps2": [(175, 212), (100, 100), (23, 212), (150, 100), (23, 212), (60, 160), (23, 212),
                (200, 160), (192, 212)] + [
        pt for i, x in enumerate(range(12, 240, 32))
        for pt in ((x, 8), (40 + i * 16, 60 + (i % 4) * 30))],
    # The copy tool (bottom bar 168-183, tool 6, $9DFB): a drawing, an area
    # dragged out, copies laid down, the flip/turn mode stepped (16-31).
    "copy": [(78, 212), ("drag", 40, 60, 120, 110), ("drag", 60, 110, 110, 50), (175, 212),
             ("drag", 30, 40, 130, 130), ("wait", 60), (170, 140), ("wait", 60), (23, 212),
             (120, 170), (23, 212), (200, 90), ("wait", 60)],
    # Booting with a saved game (the save check $00:D6D3 finds it valid),
    # then loading it from the menu.
    "savedboot": ["sram", MODE_BAR, (129, 212), (138, 123), (128, 112), (128, 190), ("wait", 2700),
                  (128, 190), (128, 112)],
    # The paint can's flood fill ($B3FF): a closed square, then tool 5 set
    # by a poke ($7E:04D0; no button found that sets it) and a click inside
    # it and one outside.
    "fill": [(62, 212), ("drag", 60, 60, 160, 60), ("drag", 160, 60, 160, 140),
             ("drag", 160, 140, 60, 140), ("drag", 60, 140, 60, 60), ("poke", "7E04D0=05"),
             (110, 100), ("wait", 400), ("poke", "7E04D0=05"), (200, 170), ("wait", 400)],
    # More fills (the fill tool, $9DAB/$B3FF): one fill seldom gets a call
    # without an interrupt in it, so several, over the outlines and the
    # areas they leave.
    "fill2": [(62, 212), ("drag", 60, 60, 160, 60), ("drag", 160, 60, 160, 140),
              ("drag", 160, 140, 60, 140), ("drag", 60, 140, 60, 60)] +
             [pt for x, y in ((110, 100), (40, 40), (110, 100), (180, 150), (60, 60), (100, 180))
              for pt in (("poke", "7E04D0=05"), (x, y), ("wait", 300))],
    # The shapes from the size panel (bottom bar 128-143, then slot n: $AE =
    # (n-1)/4+1): ellipse ($AB8A), rectangle ($AB26), line ($AAFB).
    "shapes": [(135, 212), (168, 212), ("drag", 80, 70, 150, 130), ("wait", 100),
               (135, 212), (104, 212), ("drag", 60, 60, 120, 160), ("wait", 100),
               (135, 212), (200, 212), ("drag", 150, 50, 220, 100), ("wait", 100),
               (135, 212), (40, 212), ("drag", 30, 150, 200, 60), ("wait", 100)],
    # Save (the robot fills SRAM; START when it is done), then load it back.
    "saveload": [(47, 212), ("drag", 40, 60, 200, 150), MODE_BAR, (129, 212), (138, 82), (128, 112),
                 (128, 190), ("wait", 2700), (128, 190), (138, 123), (128, 112), (128, 190),
                 ("wait", 2700), (128, 190), (128, 112), (235, 212), (23, 212), (128, 112),
                 ("wait", 2700), (128, 190), (128, 112)],
    # Gnat Attack's pause menu (right button): resume (the cursor starts on
    # the middle choice), swat a little, pause again and quit (lowest choice).
    "gnatpause": [MODE_BAR, (216, 212), (128, 112), (100, 100), (128, 112, "R"), (128, 112),
                  (150, 80), (128, 112, "R"), (128, 136), (128, 112), (60, 60)],
    # Bottom-bar handlers the other scenarios reach only under their
    # dispatcher ($95E7), where they cannot be checked on their own:
    # conformance.py checks this one with the handlers alone. The canvas
    # slot 152-167 ($995B), the size panel's slot 8-23 ($9A9E), the mode
    # bar's slot 16-31 ($CAD7).
    "bars": [(158, 212), ("wait", 60), (128, 112)],
    "bars2": [(135, 212), (15, 212), ("wait", 60), (128, 112)],
    "bars3": [MODE_BAR, (23, 212), ("wait", 120), (128, 112), ("wait", 60)],
    # The mode bar's buttons, each from the canvas, checked alone (as bars).
    "modes": [MODE_BAR, (55, 212), ("wait", 300), (128, 112)],
    "modes2": [MODE_BAR, (102, 212), ("wait", 200), (128, 112)],
    "modes3": [MODE_BAR, (129, 212), ("wait", 200), (128, 112)],
    "modes4": [MODE_BAR, (150, 212), ("wait", 200), (128, 112)],
    "modes5": [MODE_BAR, (168, 212), ("wait", 200), (128, 112)],
    "modes6": [MODE_BAR, (192, 212), ("wait", 100), (128, 112), ("wait", 60)],
    "modes7": [MODE_BAR, (216, 212), ("wait", 200), (128, 112)],
    # Bar mode 8 (from the animation player's 64-79), its buttons, checked
    # alone like the bars above.
    "patbar": [MODE_BAR, (80, 212), (80, 56), (128, 112), ("drag", 40, 40, 100, 90), (232, 212),
               ("wait", 100), (76, 212), ("wait", 200), (40, 212), ("wait", 60), (112, 212), ("wait", 60),
               (200, 212), ("wait", 60), (216, 212), ("wait", 60), (15, 212), ("wait", 60)],
    # The stamp editor's top bar ($9598 table $95C2): a page, and the
    # picture effects (x 120 $A55B, 144 $A637, 152 $A6F7, 168 $A792,
    # 184 $A8A8, 192 $A8FF, 208 $A95D, 224 $A9BE, 128 $A617), one each,
    # checked alone.
    "stampfx": [(192, 212), (40, 8), ("wait", 30), (120, 8), ("wait", 120)],
    "stampfx2": [(192, 212), (144, 8), ("wait", 600)],
    "stampfx3": [(192, 212), (152, 8), ("wait", 900)],
    "stampfx4": [(192, 212), (168, 8), ("wait", 350)],
    "stampfx5": [(192, 212), (184, 8), ("wait", 150)],
    "stampfx6": [(192, 212), (192, 8), ("wait", 150)],
    "stampfx7": [(192, 212), (208, 8), ("wait", 100)],
    "stampfx8": [(192, 212), (224, 8), ("wait", 300)],
    "stampfx9": [(192, 212), (128, 8), ("wait", 900)],
    # The wave again with its callers left out, for the routines in it.
    "waveinner": None,
    "waveinner2": None,
    # The rocket again with its callers left out, for the routines in it.
    "rocketinner": None,
    # The music editor's bar (music2's clicks), its buttons checked alone.
    "musicbar": None,
    # Into the music editor and out again ($EF6E), checked alone.
    "musicexit": [MODE_BAR, (102, 212), ("wait", 200), (128, 112), (15, 212), ("wait", 300), (128, 112),
                  ("drag", 40, 60, 120, 90), (128, 112)],
    # Gnat Attack again (gnatwaves, gnat, gnatpause) with its game loop and phases not
    # under check, so the routines inside them are checked one by one.
    "gnatinner": None,
    "gnatinner2": None,
    "gnatinner3": None,
    "gnatinner4": None,
    # Gnat Attack's boss: the wave count set to the last wave, the gnats of
    # that wave cleared, then swats at the boss as it moves.
    "gnatboss": [MODE_BAR, (216, 212), ("wait", 200), (128, 112), ("wait", 200), ("poke", "7F0016=04"),
                 ("poke", "7F05A0=00"), ("poke", "7F05A1=00"), ("poke", "7F05A2=00"), ("poke", "7F05A3=00"),
                 ("wait", 120)] + [
        # swats around where the boss hangs (lives kept up); the hit count
        # set near the end so it is beaten and its last steps run
        pt for i in range(42) for pt in ((("poke", "7F0018=05"),) + ((("poke", "7F00C9=13"),) if i == 24 else ()) +
                                         ((80 + (i * 29) % 100, 58 + (i % 4) * 8), ("wait", 25)))] + [("wait", 100)] +
        # the level's tally ($B4F4, $200 frames) and the next level's start
        [("wait", 200)] * 4,
    "gnatbossinner": None,
    "drawinner": None,
    "drawinner2": None,
    "shapesinner": None,
    "shapesinner2": None,
    "modesinner": None,
    "copyinner": None,
    "gnatbossinner2": None,
    # The title clicks again, checked with the title states alone (their
    # dispatcher $01:8308 not under check).
    "titlestates": None,
}


SCENARIOS["titlestates"] = SCENARIOS["title"]
SCENARIOS["musicbar"] = SCENARIOS["music2"]
SCENARIOS["rocketinner"] = SCENARIOS["stampfx8"]
SCENARIOS["waveinner"] = SCENARIOS["stampfx9"]
SCENARIOS["waveinner2"] = SCENARIOS["stampfx9"]
SCENARIOS["gnatinner"] = SCENARIOS["gnatwaves"]
SCENARIOS["gnatinner2"] = SCENARIOS["gnat"]
SCENARIOS["gnatinner3"] = SCENARIOS["gnatpause"]
SCENARIOS["gnatinner4"] = SCENARIOS["gnatpause"]
SCENARIOS["gnatbossinner"] = SCENARIOS["gnatboss"]
SCENARIOS["drawinner"] = SCENARIOS["draw"]
SCENARIOS["drawinner2"] = SCENARIOS["draw"]
SCENARIOS["shapesinner"] = SCENARIOS["shapes"]
SCENARIOS["shapesinner2"] = SCENARIOS["shapes"]
SCENARIOS["modesinner"] = SCENARIOS["modes"]
SCENARIOS["copyinner"] = SCENARIOS["copy"]
SCENARIOS["gnatbossinner2"] = SCENARIOS["gnatboss"]

SRAM_FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scratch", "sram", "saved.srm")


def ensure_sram(rom):
    """The battery RAM of a finished save: made once by the genuine ROM
    (draw, save, wait for the robot) and kept in scratch/ (game-made data,
    not committed)."""
    if os.path.exists(SRAM_FIXTURE):
        return SRAM_FIXTURE
    os.makedirs(os.path.dirname(SRAM_FIXTURE), exist_ok=True)
    pts = SCENARIOS["saveload"]
    run(rom, pts, os.path.join(os.path.dirname(SRAM_FIXTURE), "make"), real=True,
        extra_env={"SNESRECOMP_SRAM_DUMP": os.path.abspath(SRAM_FIXTURE)})
    return SRAM_FIXTURE


def run(rom, points, out, real=False, extra_env=None, title=True):
    """title=False skips the click through the title screen: the points then
    act on the title itself (from frame 300)."""
    sram = None
    if points and points[0] == "sram":           # start from a saved game
        points, sram = points[1:], ensure_sram(rom)
    if points and points[0] == "notitle":        # a scenario on the title screen itself
        points, title = points[1:], False
    script, shots = (list(TITLE) if title else []), []
    pokes, pad2 = [], []
    f = CANVAS_AT if title else 300
    for pt in points:
        if pt[0] == "poke":                      # ("poke", "BBAAAA=VV"): set a byte now, no shot
            pokes.append(f"{f}:{pt[1]}")
            continue
        if pt[0] == "pad2":                      # ("pad2", "MASK", frames): hold controller 2's
            pad2.append(f"{f}:{pt[1]}:{pt[2]}")  # buttons ($4218 bits, hex) from now, no shot
            continue
        if pt[0] == "wait":                      # ("wait", frames): hands off, then a shot
            script.append(f"{f}:@128:@112:0:{pt[1]}")
            f += pt[1]
            shots.append(f - 5)
            continue
        if pt[0] == "drag":                      # ("drag", x1, y1, x2, y2): a held stroke
            _, x1, y1, x2, y2 = pt
            script.append(f"{f}:@{x1}:@{y1}:0:30")
            n = 12
            for k in range(n + 1):
                x = x1 + (x2 - x1) * k // n
                y = y1 + (y2 - y1) * k // n
                script.append(f"{f + 30 + 3 * k}:@{x}:@{y}:L:3")
            end = f + 30 + 3 * (n + 1)
            script.append(f"{end}:@{x2}:@{y2}:0:{STEP - 30 - 3 * (n + 1)}")
        else:
            x, y, *btn = pt
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
    if pokes:
        env["SNESRECOMP_POKE"] = " ".join(pokes)
    if pad2:
        env["SNESRECOMP_PAD2_SCRIPT"] = " ".join(pad2)
    if sram:
        env["SNESRECOMP_SRAM_LOAD"] = os.path.abspath(sram)
    if real:
        env["MP_REALFRAME"] = "1"
    env.update(extra_env or {})
    r = subprocess.run([os.path.abspath(EXE), "--headless", os.path.abspath(rom)],
                       cwd=out, env=env, capture_output=True, text=True)
    with open(os.path.join(out, "run.log"), "w") as f:
        f.write(r.stdout + r.stderr)
    pngs = sorted(glob.glob(os.path.join(out, "screenshot_*.png")))
    snaps = sorted(glob.glob(os.path.join(out, "s_f*.snap")))
    points = [p for p in points if p not in ("notitle", "sram") and p[0] not in ("poke", "pad2")]
    names = ["00_canvas"] + [
        (f"{i + 1:02d}_wait_{p[1]}" if p[0] == "wait" else
         f"{i + 1:02d}_drag_{p[1]}_{p[2]}_{p[3]}_{p[4]}" if p[0] == "drag" else
         f"{i + 1:02d}_{p[0]}_{p[1]}" + ("_" + p[2] if len(p) > 2 else ""))
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
