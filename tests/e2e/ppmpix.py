#!/usr/bin/env python3
"""ppmpix.py FILE.ppm SCALE x,y ... - the colour of each MODE 2 pixel (x, y)
in a pc3d --snapshot of a 640x480 raster, as RRGGBB, one line each.  A
MODE 2 pixel is 2x2 in the raster and SCALE x SCALE in the window."""
import sys

data = open(sys.argv[1], "rb").read()
head = data.split(b"\n", 3)
if head[0] != b"P6":
    sys.exit("not a binary PPM")
w, h = map(int, head[1].split())
pix = head[3]
s = int(sys.argv[2])
for arg in sys.argv[3:]:
    x, y = map(int, arg.split(","))
    i = ((2 * y * s) * w + 2 * x * s) * 3
    print("%d,%d %02X%02X%02X" % (x, y, pix[i], pix[i + 1], pix[i + 2]))
