#!/usr/bin/env python3
"""Convert an Xvfb -fbdir screen file (XWD, 16 or 32 bpp TrueColor) to PNG.

Usage: python3 -I xgrab.py /tmp/xvfb/Xvfb_screen0 out.png
"""
import struct
import sys
from PIL import Image


def main():
    d = open(sys.argv[1], "rb").read()
    h = struct.unpack(">25I", d[:100])
    hsize, w, ht, bpp, bpl, ncolors = h[0], h[4], h[5], h[11], h[12], h[19]
    rmask, gmask, bmask = h[13], h[14], h[15]
    big = h[7] == 1  # byte_order MSBFirst
    off = hsize + ncolors * 12
    if bpp == 16:
        raw = Image.frombuffer("RGB", (w, ht), d[off:off + bpl * ht], "raw",
                               "BGR;16" if not big else "BGR;16B", bpl, 1)
    else:
        raw = Image.frombuffer("RGBX", (w, ht), d[off:off + bpl * ht], "raw",
                               "BGRX" if not big else "XRGB", bpl, 1).convert("RGB")
    raw.save(sys.argv[2])


if __name__ == "__main__":
    main()
