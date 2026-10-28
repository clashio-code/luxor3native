#!/usr/bin/env python3
"""List executable strings near a given offset in Luxor3.exe (static only)."""
import sys
p = "/sessions/blissful-festive-archimedes/mnt/Luxor 3/Luxor 3/Luxor3.exe"
data = open(p,'rb').read()
strs=[]; cur=bytearray(); start=0
for i,b in enumerate(data):
    if 32 <= b < 127:
        if not cur: start=i
        cur.append(b)
    else:
        if len(cur)>=4: strs.append((start, cur.decode('ascii')))
        cur=bytearray()
needle = sys.argv[1]
before = int(sys.argv[2]) if len(sys.argv)>2 else 900
after  = int(sys.argv[3]) if len(sys.argv)>3 else 900
for o,s in strs:
    if needle in s:
        print(f'=== match @0x{o:X}: {s!r}')
        for oo,ss in strs:
            if o-before <= oo <= o+after and oo!=o:
                print(f'  @0x{oo:X}  {ss!r}')
