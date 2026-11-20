#!/usr/bin/env python3
"""Extract the game's assets from your own ROM into an asset pack.

The ROM catalogue (tools/rom_catalog.py, built from the ROM access logs)
says which parts of the ROM are graphics (DMA'd to video memory), palettes
(to colour memory) and sound (sent to the sound CPU by $01:DF25). This tool
copies exactly those parts out of the ROM you give it:

  python3 tools/extract_assets.py ROM [--catalog scratch/romlog/catalog.json]
                                      [--out scratch/assets]

Output (in --out, never in the repository):
  vram/<snes address>.bin    graphics, one file per DMA source
  cgram/<snes address>.bin   palettes
  apu/<snes address>.bin     sound driver, samples and songs
  data/<purpose>/<snes>.bin  data the code reads (pictures, tilemaps, tables),
                             grouped by what the reading code does with it
  manifest.json              per file: kind, SNES address, ROM offset,
                             length, DMA target, SHA-1; and the ROM's SHA-1

Nothing from the ROM is in this tool: it takes the ROM's bytes at the
catalogue's offsets. The ROM must be the one the catalogue was made from
(Mario Paint, Europe); its SHA-1 is checked.
"""
import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
ROM_SHA1 = "6d93da7773b2c121bc4e7a3edc38f29f546455e2"   # Mario Paint (Europe)
KINDS = ("vram", "cgram", "apu", "data")

# What the code that reads a data region does with it (from the C
# translations); regions read by other code are extracted as "other".
PURPOSE = {
    "01EC4B": "picture",     # coloring-book pictures, copied whole to the canvas ($7E:A000)
    "00C490": "tilemap",     # screen layouts, rows of tile words to $7E:2100
    "00C3DE": "tilemap",     # the canvas screen's layout
    "00C414": "tilemap",
    "00DE8E": "tilemap",     # to $7E:28C0
    "00DE46": "tilemap",
    "01E93A": "tilemap",     # music editor parts
    "00A25B": "tool",        # tool switch: per-tool graphics and colours
    "00A22D": "tool",
    "00B66C": "cursor",      # stamp editor / eraser cursors
    "00B6F4": "cursor",
    "01D259": "palette",     # the wave effect's palettes ($09AF)
    "01962C": "animation",   # animation scripts
    "01972D": "animation",
    "01F91E": "steps",       # $F825 step data
    "01F84F": "steps",
    "00FD83": "music",       # music editor data
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--catalog", default=os.path.join(ROOT, "scratch", "romlog", "catalog.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "scratch", "assets"))
    a = ap.parse_args()

    rom = open(a.rom, "rb").read()
    sha = hashlib.sha1(rom).hexdigest()
    if sha != ROM_SHA1:
        sys.exit(f"extract_assets: this ROM's SHA-1 is {sha}, the catalogue was made from {ROM_SHA1}")
    cat = json.load(open(a.catalog))
    if cat.get("rom_size") not in (None, len(rom)):
        sys.exit("extract_assets: the catalogue is for a ROM of another size")

    files, total = [], {k: 0 for k in KINDS}
    for r in cat["regions"]:
        if r["kind"] not in KINDS:
            continue
        off, length, snes = r["offset"], r["length"], r["snes"]
        if r["kind"] == "apu" and 0 < int(snes, 16) & 0x7FFF <= 2:
            # An upload set starts at bank:$8000 ($CC = $8000); its first
            # bytes may have been read by other code too, so the catalogue
            # can start the region a byte or two later.
            back = int(snes, 16) & 0x7FFF
            off, length, snes = off - back, length + back, f"{int(snes, 16) - back:06X}"
        blob = rom[off:off + length]
        sub = r["kind"]
        if sub == "data":
            sub = os.path.join("data", PURPOSE.get(r.get("reader", ""), "other"))
        rel = os.path.join(sub, f"{snes}.bin")
        path = os.path.join(a.out, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(blob)
        files.append({"file": rel.replace(os.sep, "/"), "kind": r["kind"], "snes": snes,
                      "rom_offset": off, "length": length, "target": r.get("target"),
                      "reader": r.get("reader"), "purpose": PURPOSE.get(r.get("reader", "")) if r["kind"] == "data" else None,
                      "sha1": hashlib.sha1(blob).hexdigest()})
        total[r["kind"]] += length
    with open(os.path.join(a.out, "manifest.json"), "w") as f:
        json.dump({"rom_sha1": sha, "files": files}, f, indent=1)
    print(f"extract_assets: {len(files)} files in {a.out}: " +
          ", ".join(f"{k} {total[k]} bytes" for k in KINDS))


if __name__ == "__main__":
    main()
