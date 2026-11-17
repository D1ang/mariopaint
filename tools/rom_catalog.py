"""Build a catalogue of what the game reads from the ROM, and how.

Input: the binary ROM access logs written by snesrecomp's romlog.c
(SNESRECOMP_ROMLOG=<file>, one per tour scenario, run on the genuine ROM):

  py tools/rom_catalog.py scratch/romlog/*.bin

Output (addresses, sizes and readers only - never ROM contents):
  scratch/romlog/catalog.json   every region: offset, length, SNES address,
                                kind, readers / DMA target
  docs/ROM_CATALOG.md           summary per kind and the region list

Kinds:
  code      executed by the CPU (opcodes and operands)
  data      read as data by CPU code; grouped per reading routine
  vram/cgram/oam/wram/apu-port/other-dma   read by a DMA, by its target
  hdma      HDMA tables and data
  apu       sent to the sound CPU by $01:DF25 (driver, samples, songs)
  native    read only by translated C code
Bytes no tour touched are counted as unknown: that is what the tours still
have to reach before the ROM can be dropped.
"""
import json
import os
import re
import struct
import sys
from glob import glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
K_FETCH, K_DATA, K_DMA, K_HDMA, K_NATIVE = 1, 2, 4, 8, 16


def load(path):
    with open(path, "rb") as f:
        b = f.read()
    assert b[:4] == b"MPRL", path
    size = struct.unpack_from("<I", b, 4)[0]
    kind = bytearray(b[8:8 + size])
    first = []
    o = 8 + size
    for _ in range(4):
        first.append(list(struct.unpack_from("<%dI" % size, b, o)))
        o += 4 * size
    return size, kind, first


def merge(paths):
    size, kind, first = load(paths[0])
    for p in paths[1:]:
        s2, k2, f2 = load(p)
        assert s2 == size
        for i in range(size):
            if k2[i] & ~kind[i]:
                for k in range(4):
                    if (k2[i] >> k) & 1 and not (kind[i] >> k) & 1:
                        first[k][i] = f2[k][i]
                kind[i] |= k2[i]
    return size, kind, first


def routine_starts():
    """Known routine entry points: translated ones plus every call target
    the conformance tours saw."""
    starts = set()
    for f in glob(os.path.join(ROOT, "src", "recomp", "*.c")):
        starts |= {int(a, 16) for a in re.findall(r"func_table_register\(0x([0-9A-Fa-f]{6})", open(f).read())}
    ex = os.path.join(ROOT, "scratch", "conf", "executed.txt")
    if os.path.exists(ex):
        starts |= {int(l.strip()[:6], 16) for l in open(ex) if l.strip()}
    return sorted(starts)


def owner(pc, starts):
    """The routine a PC lies in: the nearest known entry at or below it in
    the same bank (a guess where routines are not all known yet)."""
    lo, hi, best = 0, len(starts) - 1, None
    while lo <= hi:
        m = (lo + hi) // 2
        if starts[m] <= pc:
            best = starts[m]; lo = m + 1
        else:
            hi = m - 1
    if best is not None and best >> 16 == pc >> 16:
        return best
    return None


def snes(off):
    return ((off >> 15) << 16) | 0x8000 | (off & 0x7FFF)


APU_UPLOAD = 0x01DF25                    # the routine that feeds the sound CPU
DMA_TARGET = {0x18: "vram", 0x19: "vram", 0x22: "cgram", 0x04: "oam", 0x80: "wram"}


def classify(i, kind, first, starts):
    k = kind[i]
    if k & K_FETCH:
        return ("code", None)
    if k & K_DMA:
        reg = first[2][i] >> 16
        if 0x40 <= reg <= 0x43:
            return ("apu-port", None)
        return (DMA_TARGET.get(reg, "other-dma"), None)
    if k & K_HDMA:
        return ("hdma", None)
    if k & K_DATA:
        r = owner(first[1][i], starts)
        if r == APU_UPLOAD:
            return ("apu", None)             # sound program, samples, songs
        return ("data", r if r is not None else first[1][i])
    if k & K_NATIVE:
        return ("native", None)
    return (None, None)


def main(paths):
    size, kind, first = merge(paths)
    starts = routine_starts()
    regions, i = [], 0
    while i < size:
        c = classify(i, kind, first, starts)
        if c[0] is None:
            i += 1
            continue
        j = i + 1
        while j < size and classify(j, kind, first, starts) == c:
            # a DMA run continues only while its target address does too
            j += 1
        r = {"offset": i, "length": j - i, "snes": "%06X" % snes(i), "kind": c[0]}
        if c[0] == "data":
            r["reader"] = "%06X" % c[1]
        if c[0] in ("vram", "cgram"):
            r["target"] = "%04X" % (first[2][i] & 0xFFFF)
        regions.append(r)
        i = j

    total = {}
    for r in regions:
        total[r["kind"]] = total.get(r["kind"], 0) + r["length"]
    unknown = size - sum(total.values())

    out = os.path.join(ROOT, "scratch", "romlog")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "catalog.json"), "w") as f:
        json.dump({"rom_size": size, "regions": regions}, f, indent=1)

    lines = ["# ROM catalogue", "",
             "What the tours read from the ROM (tools/rom_catalog.py over the",
             "SNESRECOMP_ROMLOG logs of %d scenario runs on the genuine ROM)." % len(paths),
             "Addresses and sizes only; the full region list is in",
             "scratch/romlog/catalog.json.", "",
             "| kind | bytes | share |", "|---|---:|---:|"]
    for k in sorted(total, key=lambda k: -total[k]):
        lines.append("| %s | %d | %.1f%% |" % (k, total[k], 100.0 * total[k] / size))
    lines.append("| (not reached by the tours) | %d | %.1f%% |" % (unknown, 100.0 * unknown / size))
    lines += ["", "## Asset regions (DMA targets and CPU-read data)", "",
              "| SNES | ROM offset | bytes | kind | reader / target |", "|---|---|---:|---|---|"]
    for r in regions:
        if r["kind"] == "code" or r["length"] < 16:
            continue
        lines.append("| %s | %06X | %d | %s | %s |" % (
            r["snes"], r["offset"], r["length"], r["kind"],
            r.get("reader", r.get("target", ""))))
    with open(os.path.join(ROOT, "docs", "ROM_CATALOG.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("rom_catalog: %d regions; " % len(regions) +
          ", ".join("%s %d" % (k, total[k]) for k in sorted(total)) +
          ", unknown %d of %d" % (unknown, size))


if __name__ == "__main__":
    paths = sys.argv[1:] or sorted(glob(os.path.join(ROOT, "scratch", "romlog", "*.bin")))
    main(paths)
