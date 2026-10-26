#!/usr/bin/env python3
"""Inventory the Luxor 3 asset tree: counts, sizes, image dimensions, formats.

Everything the port needs to know before it can load a texture atlas:
  * file counts and byte totals per extension
  * PNG colour type / bit depth / interlace
  * PNG and JPEG dimensions
  * which map folders carry background / mask / track files

Usage: python3 asset_inventory.py <game_dir>
"""
import os, struct, sys, collections


def png_header(path):
    with open(path, 'rb') as f:
        head = f.read(33)
    if len(head) < 29 or head[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    w, h, bd, ct, comp, filt, inter = struct.unpack('>IIBBBBB', head[16:29])
    return w, h, bd, ct, inter


def jpeg_size(path):
    with open(path, 'rb') as f:
        d = f.read()
    i = 2
    while i < len(d) - 9:
        if d[i] != 0xFF:
            i += 1; continue
        m = d[i+1]
        if m in (0xC0,0xC1,0xC2,0xC3,0xC5,0xC6,0xC7,0xC9,0xCA,0xCB,0xCD,0xCE,0xCF):
            h, w = struct.unpack('>HH', d[i+5:i+9])
            return w, h
        if m in (0xD8, 0xD9) or 0xD0 <= m <= 0xD7:
            i += 2; continue
        i += 2 + struct.unpack('>H', d[i+2:i+4])[0]
    return None


def main(root):
    ext = collections.Counter(); size = collections.Counter()
    dims = collections.Counter(); ctypes = collections.Counter(); inter = collections.Counter()
    total = 0; count = 0
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            e = os.path.splitext(f)[1].lower() or '(none)'
            try: s = os.path.getsize(p)
            except OSError: s = 0
            ext[e] += 1; size[e] += s; total += s; count += 1
            if e == '.png':
                h = png_header(p)
                if h:
                    w, ht, bd, ct, it = h
                    dims[('png', w, ht)] += 1
                    ctypes[(bd, ct)] += 1
                    inter[it] += 1
            elif e in ('.jpg', '.jpeg'):
                h = jpeg_size(p)
                if h: dims[('jpg', h[0], h[1])] += 1

    print(f"files: {count}   total: {total/1048576:.1f} MiB")
    print("\nby extension:")
    for e, n in ext.most_common():
        print(f"  {e:10s} {n:6d}  {size[e]/1024:10.1f} KiB")
    ct_name = {0: 'Gray', 2: 'RGB', 3: 'Palette', 4: 'Gray+Alpha', 6: 'RGBA'}
    print("\nPNG bit depth / colour type:")
    for (bd, ct), n in ctypes.most_common():
        print(f"  bd={bd} {ct_name.get(ct, ct):11s} {n:6d}")
    print("\nPNG interlace (0=none, 1=adam7):")
    for k, n in inter.most_common():
        print(f"  {k}: {n}")
    print("\nmost common image sizes:")
    for (kind, w, h), n in dims.most_common(20):
        print(f"  {kind} {w:5d}x{h:<5d} {n:6d}")
    print("\nmap folder contents:")
    maps = os.path.join(root, 'data', 'maps')
    if os.path.isdir(maps):
        for m in sorted(os.listdir(maps)):
            d = os.path.join(maps, m)
            if not os.path.isdir(d): continue
            names = sorted(os.listdir(d))
            print(f"  {m:18s} {' '.join(names)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
