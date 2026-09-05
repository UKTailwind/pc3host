#!/usr/bin/env python3
"""Turn the 24-bit BMP saveimage writes - or the binary PPM pc3d's
--snapshot writes - into a PNG, with nothing but the standard library,
so a screen taken through the server can be looked at.

    python3 bmp2png.py screen.bmp screen.png
    python3 bmp2png.py window.ppm window.png
"""
import re
import struct
import sys
import zlib


def png_write(dst, w, h, rgb_rows):
    raw = bytearray()
    for row in rgb_rows:
        raw.append(0)                       # filter: none
        raw += row

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(dst, "wb").write(png)
    print(f"{dst}: {w}x{h}")


def from_ppm(data, dst):
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m:
        sys.exit("not a binary PPM")
    w, h, maxv = int(m.group(1)), int(m.group(2)), int(m.group(3))
    if maxv != 255:
        sys.exit("only 8-bit PPMs are handled")
    body = data[m.end():]
    rows = [body[y * w * 3:(y + 1) * w * 3] for y in range(h)]
    png_write(dst, w, h, rows)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    if data[:2] == b"P6":
        from_ppm(data, dst)
        return
    if data[:2] != b"BM":
        sys.exit("not a BMP or PPM")
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    planes, bpp = struct.unpack_from("<HH", data, 26)
    if bpp != 24:
        sys.exit(f"{bpp} bpp: only 24-bit BMPs are handled")
    topdown = h < 0
    h = abs(h)
    rowbytes = (w * 3 + 3) & ~3
    rgb_rows = []
    rows = range(h) if topdown else range(h - 1, -1, -1)
    for y in rows:
        row = data[off + y * rowbytes: off + y * rowbytes + w * 3]
        out = bytearray()
        for x in range(w):
            b, g, r = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            out += bytes((r, g, b))
        rgb_rows.append(bytes(out))
    png_write(dst, w, h, rgb_rows)


if __name__ == "__main__":
    main()
