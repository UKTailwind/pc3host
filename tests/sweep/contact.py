#!/usr/bin/env python3
"""A contact sheet: several 24-bit BMPs (the screens saveimage writes)
tiled into one PNG, 320x240 a tile, four across, so a sweep's screens
can be looked at together.  640x480 screens are halved.

    python3 contact.py out.png a.bmp b.bmp ...

Prints the tile order, since the sheet carries no labels.
"""
import struct
import sys
import zlib

TW, TH, COLS = 320, 240, 4


def read_bmp(path):
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        return None
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        return None
    top = h < 0
    h = abs(h)
    rb = (w * 3 + 3) & ~3
    rows = []
    for y in (range(h) if top else range(h - 1, -1, -1)):
        r = d[off + y * rb: off + y * rb + w * 3]
        rows.append([(r[x * 3 + 2], r[x * 3 + 1], r[x * 3]) for x in range(w)])
    return w, h, rows


def tile(img):
    """the image as a 320x240 tile: halved if 640 wide, cropped/padded"""
    if img is None:
        return [[(40, 0, 0)] * TW for _ in range(TH)]
    w, h, rows = img
    step = 2 if w >= 640 else 1
    out = []
    for ty in range(TH):
        sy = ty * step
        row = []
        for tx in range(TW):
            sx = tx * step
            row.append(rows[sy][sx] if sy < h and sx < w else (0, 0, 0))
        out.append(row)
    return out


def main():
    dst, srcs = sys.argv[1], sys.argv[2:]
    tiles = [tile(read_bmp(s)) for s in srcs]
    n = len(tiles)
    rows_of = (n + COLS - 1) // COLS
    W, H = TW * COLS, TH * rows_of
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        ty, iy = divmod(y, TH)
        for x in range(W):
            tx, ix = divmod(x, TW)
            i = ty * COLS + tx
            if i < n:
                r, g, b = tiles[i][iy][ix]
            else:
                r = g = b = 32
            # a one-pixel grid between tiles
            if ix == 0 or iy == 0:
                r, g, b = 96, 96, 96
            raw += bytes((r, g, b))

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(dst, "wb").write(png)
    for i, s in enumerate(srcs):
        print(f"{i // COLS},{i % COLS}: {s}")


if __name__ == "__main__":
    main()
