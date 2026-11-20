#!/usr/bin/env python3
"""Convert the asset pack (tools/extract_assets.py) into images and sounds.

  python3 tools/convert_assets.py [--pack scratch/assets] [--logs scratch/romlog]

Reads only the pack and the ROM access logs' side files (<log>.assets, made
by snesrecomp's romlog.c), and writes next to the pack:

  png/vram_<snes>.png     each graphics block as a tile sheet (16 tiles a
                          row), pixels as palette indices shown in grey;
                          its bits per pixel from what the game showed from
                          the VRAM it went to (BG mode, layers, sprites)
  png/palettes.png        the palette block, 16 colours a row
  palettes.json           the same colours as #RRGGBB
  wav/<snes>/sample_NN.wav  the sound samples of each sound upload set
                          (BRR decoded, 16-bit mono at 32 kHz, played once
                          up to the loop point and the loop once more)
  aram/<snes>.bin         the sound CPU's memory as each upload set leaves it
  converted.json          what was made from which file, and how

The bits per pixel and the sample directory come from the logs; a block the
game never showed is written as 4bpp and marked "guessed" in converted.json.
"""
import argparse
import json
import os
import struct
import wave
import zlib
from glob import glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")


# ---- logs -------------------------------------------------------------------

def read_logs(logdir):
    """VRAM use per word (bit 0 2bpp BG, 1 4bpp BG, 2 8bpp BG, 3 sprites) and
    the sample directory pages, merged over all runs."""
    use = [0] * 0x8000
    dirs = set()
    for path in glob(os.path.join(logdir, "*.assets")):
        for line in open(path):
            f = line.split()
            if f[0] == "vuse":
                w, n, bits = int(f[1], 16), int(f[2], 16), int(f[3])
                for i in range(n):
                    use[(w + i) & 0x7FFF] |= bits
            elif f[0] == "dir":
                dirs.add(int(f[1], 16))
    return use, sorted(dirs)


def bpp_for(use, dest_word, length):
    """Majority vote over the VRAM words a block went to."""
    votes = {2: 0, 4: 0, 8: 0}
    for i in range(length // 2):
        b = use[(dest_word + i) & 0x7FFF]
        if b & 1: votes[2] += 1
        if b & 2: votes[4] += 1
        if b & 4: votes[8] += 1
        if b & 8: votes[4] += 1                 # sprites are 4bpp
    best = max(votes, key=votes.get)
    return (best, False) if votes[best] else (4, True)


# ---- PNG (no dependencies) ---------------------------------------------------

def write_png(path, width, height, rows, palette):
    """8-bit indexed PNG; rows: list of bytes, palette: list of (r, g, b)."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    plte = b"".join(bytes(c) for c in palette)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b"PLTE", plte))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def write_png_rgb(path, width, height, rows):
    """24-bit RGB PNG; rows: list of bytes (3 per pixel)."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def decode_tile(data, at, bpp):
    """One 8x8 SNES planar tile -> 64 palette indices."""
    px = [0] * 64
    for y in range(8):
        for plane in range(bpp):
            pair, bit = divmod(plane, 2)
            b = data[at + pair * 16 + y * 2 + bit] if at + pair * 16 + y * 2 + bit < len(data) else 0
            for x in range(8):
                if b & (0x80 >> x):
                    px[y * 8 + x] |= 1 << plane
    return px


def tiles_png(path, data, bpp):
    size = bpp * 8
    n = len(data) // size
    cols = 16
    rows_t = (n + cols - 1) // cols
    w, h = cols * 8, max(rows_t, 1) * 8
    img = [bytearray(w) for _ in range(h)]
    for t in range(n):
        px = decode_tile(data, t * size, bpp)
        ox, oy = (t % cols) * 8, (t // cols) * 8
        for i, v in enumerate(px):
            img[oy + i // 8][ox + i % 8] = v
    levels = 1 << bpp
    grey = [(v * 255 // (levels - 1),) * 3 for v in range(levels)] + [(0, 0, 0)] * (256 - levels)
    write_png(path, w, h, img, grey)
    return n


def bgr555(w):
    r, g, b = w & 0x1F, (w >> 5) & 0x1F, (w >> 10) & 0x1F
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


# ---- sound -------------------------------------------------------------------

def upload_to_aram(data):
    """The $01:DF25 upload format: blocks of [u16 size][u16 dest][bytes],
    ended by size 0 (dest = where the sound CPU starts)."""
    aram = bytearray(0x10000)
    at, blocks, start = 0, 0, None
    while at + 4 <= len(data):
        size, dest = struct.unpack_from("<HH", data, at)
        at += 4
        if size == 0:
            start = dest
            break
        chunk = data[at:at + size]
        for i, b in enumerate(chunk):
            aram[(dest + i) & 0xFFFF] = b
        at += size
        blocks += 1
    return aram, blocks, start


def brr_decode(aram, at, limit=0x10000):
    """Decode BRR blocks from at until the end flag; -> (samples, loop index)."""
    out, p1, p2 = [], 0, 0
    for _ in range(limit // 9):
        if at + 9 > 0x10000:
            break
        hdr = aram[at]
        shift, filt, end = hdr >> 4, (hdr >> 2) & 3, hdr & 1
        for i in range(16):
            byte = aram[at + 1 + i // 2]
            n = (byte >> 4) if i % 2 == 0 else (byte & 0xF)
            if n >= 8:
                n -= 16
            s = (n << shift) >> 1 if shift <= 12 else (-2048 if n < 0 else 0)
            if filt == 1:
                s += p1 + ((-p1) >> 4)
            elif filt == 2:
                s += (p1 << 1) + ((-((p1 << 1) + p1)) >> 5) - p2 + (p2 >> 4)
            elif filt == 3:
                s += (p1 << 1) + ((-(p1 + (p1 << 2) + (p1 << 3))) >> 6) - p2 + (((p2 << 1) + p2) >> 4)
            s = max(-32768, min(32767, s))           # clamp to 16 bits,
            s = ((s << 1) & 0xFFFF)                    # then the DSP keeps 15:
            s = (s - 0x10000 if s & 0x8000 else s) >> 1
            p2, p1 = p1, s
            out.append(s * 2)
        at += 9
        if end:
            break
    return out


def write_wav(path, samples, rate=32000):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))


def samples_of(aram, dirs, max_n=128):
    """Sample directory: 4 bytes per entry (start, loop start)."""
    found = []
    for d in dirs:
        base = d
        for i in range(max_n):
            start, loop = struct.unpack_from("<HH", aram, (base + i * 4) & 0xFFFF)
            if start == 0 or start == 0xFFFF or start < 0x0200:
                break
            found.append((i, start, loop))
    return found


# ---- main --------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pack", default=os.path.join(ROOT, "scratch", "assets"))
    ap.add_argument("--logs", default=os.path.join(ROOT, "scratch", "romlog"))
    a = ap.parse_args()
    man = json.load(open(os.path.join(a.pack, "manifest.json")))
    use, dirs = read_logs(a.logs)
    for sub in ("png", "wav", "aram"):
        os.makedirs(os.path.join(a.pack, sub), exist_ok=True)
    done = []
    for f in man["files"]:
        data = open(os.path.join(a.pack, f["file"]), "rb").read()
        if f["kind"] == "vram":
            dest = int(f["target"] or "0", 16)
            bpp, guessed = bpp_for(use, dest, len(data))
            out = os.path.join("png", f"vram_{f['snes']}.png")
            n = tiles_png(os.path.join(a.pack, out), data, bpp)
            done.append({"from": f["file"], "to": out, "bpp": bpp, "tiles": n, "guessed": guessed})
        elif f["kind"] == "cgram":
            cols = [bgr555(struct.unpack_from("<H", data, i)[0]) for i in range(0, len(data) - 1, 2)]
            rows = (len(cols) + 15) // 16
            img = [bytearray(16 * 8 * 3) for _ in range(rows * 8)]
            for i, c in enumerate(cols):
                for y in range(8):
                    for x in range(8):
                        at = ((i % 16) * 8 + x) * 3
                        img[(i // 16) * 8 + y][at:at + 3] = bytes(c)
            write_png_rgb(os.path.join(a.pack, "png", "palettes.png"), 128, rows * 8, img)
            json.dump(["#%02X%02X%02X" % c for c in cols],
                      open(os.path.join(a.pack, "palettes.json"), "w"))
            done.append({"from": f["file"], "to": "png/palettes.png", "colours": len(cols)})
        elif f["kind"] == "apu":
            aram, blocks, start = upload_to_aram(data)
            open(os.path.join(a.pack, "aram", f"{f['snes']}.bin"), "wb").write(aram)
            wdir = os.path.join("wav", f["snes"])
            os.makedirs(os.path.join(a.pack, wdir), exist_ok=True)
            made = 0
            for i, s0, loop in samples_of(aram, dirs):
                pcm = brr_decode(aram, s0)
                if len(pcm) < 16:
                    continue
                if s0 <= loop < 0x10000:
                    pcm += brr_decode(aram, loop)
                write_wav(os.path.join(a.pack, wdir, f"sample_{i:02d}.wav"), pcm)
                made += 1
            done.append({"from": f["file"], "to": wdir, "upload_blocks": blocks,
                         "start": None if start is None else f"{start:04X}", "samples": made,
                         "directory": [f"{d:04X}" for d in dirs]})
    json.dump(done, open(os.path.join(a.pack, "converted.json"), "w"), indent=1)
    g = sum(1 for d in done if d.get("tiles") is not None)
    print(f"convert_assets: {g} tile sheets ({sum(1 for d in done if d.get('guessed'))} with guessed bpp), "
          f"palettes, {sum(d.get('samples', 0) for d in done)} samples from "
          f"{sum(1 for d in done if 'samples' in d)} sound sets -> {a.pack}")


if __name__ == "__main__":
    main()
