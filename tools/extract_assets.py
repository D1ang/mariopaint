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
KINDS = ("vram", "cgram", "apu")


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
        rel = os.path.join(r["kind"], f"{snes}.bin")
        path = os.path.join(a.out, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(blob)
        files.append({"file": rel.replace(os.sep, "/"), "kind": r["kind"], "snes": snes,
                      "rom_offset": off, "length": length, "target": r.get("target"),
                      "sha1": hashlib.sha1(blob).hexdigest()})
        total[r["kind"]] += length
    with open(os.path.join(a.out, "manifest.json"), "w") as f:
        json.dump({"rom_sha1": sha, "files": files}, f, indent=1)
    print(f"extract_assets: {len(files)} files in {a.out}: " +
          ", ".join(f"{k} {total[k]} bytes" for k in KINDS))


if __name__ == "__main__":
    main()
