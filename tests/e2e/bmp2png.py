#!/usr/bin/env python3
"""Turn a BMP saveimage writes - 24-bit, or with a palette and RLE4 or
RLE8 compression, as it writes now - or the binary PPM pc3d's
--snapshot writes, into a PNG, with nothing but the standard library,
so a screen taken through the server can be looked at.

    python3 bmp2png.py screen.bmp screen.png
    python3 bmp2png.py window.ppm window.png

--same compares two pictures by their pixels rather than their bytes:
any of those BMPs, or an 8-bit RGB PNG such as SAVE PNG writes.  It
exits 0 when they are the same picture.

    python3 bmp2png.py --same SCREEN_x.PNG ref.bmp
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


def from_ppm(data):
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m:
        sys.exit("not a binary PPM")
    w, h, maxv = int(m.group(1)), int(m.group(2)), int(m.group(3))
    if maxv != 255:
        sys.exit("only 8-bit PPMs are handled")
    body = data[m.end():]
    return w, h, [bytes(body[y * w * 3:(y + 1) * w * 3]) for y in range(h)]


def from_bmp(data):
    """Rows top first, each w x (r, g, b) bytes."""
    off = struct.unpack_from("<I", data, 10)[0]
    hsz, w, h, planes, bpp, comp = struct.unpack_from("<IiiHHI", data, 14)
    ncol = struct.unpack_from("<I", data, 46)[0] if hsz >= 40 else 0
    topdown = h < 0
    h = abs(h)
    pal = []
    if bpp <= 8:
        for i in range(ncol or (1 << bpp)):
            b, g, r = data[14 + hsz + 4 * i: 17 + hsz + 4 * i]
            pal.append(bytes((r, g, b)))
    rows = [[b"\0\0\0"] * w for _ in range(h)]   # bottom first, as stored
    if comp == 0:
        rb = ((w * bpp + 31) // 32) * 4
        for y in range(h):
            row = data[off + y * rb: off + (y + 1) * rb]
            for x in range(w):
                if bpp == 24:
                    rows[y][x] = bytes((row[3 * x + 2], row[3 * x + 1], row[3 * x]))
                elif bpp == 8:
                    rows[y][x] = pal[row[x]]
                elif bpp == 4:
                    rows[y][x] = pal[(row[x // 2] >> (0 if x & 1 else 4)) & 15]
                elif bpp == 1:
                    rows[y][x] = pal[(row[x // 8] >> (7 - (x & 7))) & 1]
                else:
                    sys.exit(f"{bpp} bpp is not handled")
    elif comp in (1, 2):                     # BI_RLE8, BI_RLE4
        p, x, y = off, 0, 0
        while p + 1 < len(data):
            a, b = data[p], data[p + 1]
            p += 2
            if a:                            # a run
                for k in range(a):
                    v = b if comp == 1 else (b >> 4 if k % 2 == 0 else b & 15)
                    rows[y][x] = pal[v]
                    x += 1
            elif b == 0:                     # end of the row
                x, y = 0, y + 1
            elif b == 1:                     # end of the picture
                break
            elif b == 2:                     # a move
                x, y = x + data[p], y + data[p + 1]
                p += 2
            else:                            # b pixels as they are
                n = b if comp == 1 else (b + 1) // 2
                for k in range(b):
                    v = data[p + k] if comp == 1 else \
                        (data[p + k // 2] >> 4 if k % 2 == 0 else data[p + k // 2] & 15)
                    rows[y][x] = pal[v]
                    x += 1
                p += n + (n & 1)
    else:
        sys.exit(f"compression {comp} is not handled")
    if not topdown:
        rows.reverse()
    return w, h, [b"".join(r) for r in rows]


def from_png(data):
    """8-bit RGB, not interlaced: what SAVE PNG writes."""
    p, idat, w, h = 8, b"", 0, 0
    while p < len(data):
        n = struct.unpack_from(">I", data, p)[0]
        kind, body = data[p + 4:p + 8], data[p + 8:p + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if (depth, ctype, inter) != (8, 2, 0):
                sys.exit("only 8-bit RGB PNGs are handled")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        p += 12 + n
    raw = zlib.decompress(idat)
    prev, rows, n = bytearray(3 * w), [], 3 * w
    for y in range(h):
        f = raw[y * (n + 1)]
        line = bytearray(raw[y * (n + 1) + 1:(y + 1) * (n + 1)])
        for i in range(n):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows


def read_picture(path):
    data = open(path, "rb").read()
    if data[:2] == b"P6":
        return from_ppm(data)
    if data[:2] == b"BM":
        return from_bmp(data)
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return from_png(data)
    sys.exit(f"{path}: not a BMP, PNG or PPM")


def main():
    if sys.argv[1] == "--same":
        a, b = read_picture(sys.argv[2]), read_picture(sys.argv[3])
        if a != b:
            print(f"{sys.argv[2]} and {sys.argv[3]} are different pictures")
            sys.exit(1)
        return
    w, h, rows = read_picture(sys.argv[1])
    png_write(sys.argv[2], w, h, rows)


if __name__ == "__main__":
    main()
