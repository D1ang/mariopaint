# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow SemVer once the first one is tagged.

## [Unreleased]

### Added
- `--headless` and `--record out.mp4`: run with no window, audio or pacing, and
  optionally pipe frames to ffmpeg.
- Needs snesrecomp #1-#5 (headless/record, mouse pointer steering, lockstep
  validation, LakeSnes mouse-button bump, autogen) and LakeSnes #1, all merged.
- `tools/canvas_tour.py`: drives the title click and every canvas bottom-bar
  button headlessly, on the recompiled build or the genuine ROM (`--real`).
- `tools/conformance.py`: lockstep-validates every recompiled function the tour
  reaches against the genuine ROM and reports a pass count, with a committed
  baseline that fails the run on regression.
- `tools/first_diff.py`: finds the first frame an intercepted function makes
  WRAM diverge.
- `MP_TIMED`, `MP_VALIDATE`, `MP_PROFILE` in `main.c` (timed-recomp intercepts,
  lockstep validation, call profile).
- `tools/intercept_bisect.py`: finds which intercepted functions break the
  tour when run together (callers reading registers a hand-port left wrong).
- `tools/gen.py`: profiles the genuine ROM through the tour and lifts every
  reached routine with snesrecomp's `autogen.py` into `gen/mp_gen.c`
  (gitignored; built in when present, used with `MP_GEN=1`; not yet validated).
- `docs/harness.md`, `CHANGELOG.md`, `ROADMAP.md`, `CONTRIBUTING.md`.

### Changed
- snesrecomp submodule moved from the `feat/autogen` branch to `main` (b73fdcd),
  so the pin can't disappear with a branch again (#1, reported by
  [@jason-oliveira](https://github.com/jason-oliveira)).
- Default mode is now timed recomp: the genuine ROM in LakeSnes's timed frame
  loop, with every function that passes conformance (`k_verified` in `main.c`)
  running natively. The previous native-driven boot chain is `MP_NATIVE=1`.
- `$01:D9E1` (mouse read) is now a straight translation of `$01:D9E1-$01:DB30`:
  serial clocking from `$4016`, two's-complement displacements, the ROM's
  `$04CA` layout and double-click logic, and the `$4212` auto-read wait.
- `$00:8B48` (cursor move) translated from the ROM: two's-complement deltas,
  `$1B26`/`$1B28` both written.
- Cursor locking to the host pointer moved out of the mouse reader into
  snesrecomp (`recomp_input_set_mouse_cursor_addr`).
- Root screenshots moved to `docs/screenshots/`.

### Fixed
- Frame waits in the timed loop: a framed native routine now really hands each
  frame to the ROM's frame driver. Until now only conformance.py's check did
  that; in the game the C frame driver presented the frame itself, at the wrong
  moment, so sprites were now and then uploaded while the picture was being
  drawn (snesrecomp: `func_table.c`). The resumed routine also returns in its
  own bank, not the frame driver's.
- The top bar's handlers in the pattern bar (`$00:E817`, `$00:E87A`) and the
  exit of the mouse speed screen (`$00:CA76`) in C; `$00:C900` now returns
  when that exit drops its return address. New tour scenarios reach these and
  the buttons `$00:987B` and `$00:99E1`.
- Animation Land and the special stamp: the path screens (`$00:C460`,
  `$00:C4D6` and what they lead to), the other main states of `$00:87A8`, the
  stamp's save and load screens (`$00:DB59`-`$00:DD6D`, `$00:DEB3`) and the
  playback's buttons (`$00:BCDB`) in C. New tour scenarios cover them.
- Title screen: `$01:833F`, the letter click test `$01:8D35` and the ten letter
  handlers retranslated from the ROM (the old guesses presented a frame
  themselves); `$01:88E3` loads A before `$8B6E`.
- Pen dab `$00:B05A` retranslated; `$00:96CD`, `$00:BBD4` and `$00:DA7B` added.
- `$01:8000` passes conformance but stays on the ROM: native, the title screen
  now and then shows a frame without its picture (`NOT_TIMED`).
- conformance.py no longer skips checks silently: snesrecomp's limits on
  routines and nesting depth raised, RTL routines entered by jump get the right
  return size, and a soft reset no longer fails the call it cut short.
- The mingw-w64 build failed to link (`undefined reference to SDL_main`):
  `main.c` now includes `<SDL.h>`.
- APU command-queue pushes (`$01:D308/D328/D348/D368`, `$01:D2BF`) returned in
  8-bit mode: the ROM wraps them in PHP/PLP, the hand-ports did `SEP #$30` and
  left callers running at the wrong width. They also return the advanced
  index in A now, as the ROM does.
- Eraser bomb icon (`$00:81CA`) used tile bytes in the wrong order; it and
  `$00:823C` now read their frames from the ROM tables.
- `$01:DE2D` (APU command sender) did not clear the port after a command was
  echoed.
- `$01:E747` and `$01:D9E1` now wait out the auto-joypad read (`$4212`) as the
  ROM does, instead of reading `$4218` mid-read in the timed loop.
- Left and right mouse buttons were swapped in LakeSnes's mouse packet, so a
  left click reached the genuine ROM as a right click and the title screen
  could not be started in real-frame mode.
- The old mouse reader set both buttons' bits on a left click and treated
  displacements as sign-magnitude after the ROM had already converted them.

### Removed
- `smk_config.ini` from the repo (per-machine menu settings; now gitignored).

## Before this file

History up to September 2026 (boot chain, title screen, canvas drawing,
interpreter fallback, real-frame reference captures, stack pointer and title
handover fixes) is in `git log`.
