#!/usr/bin/env python3
"""Probe a Windows PE executable: bitness, subsystems, imports, x87/SSE usage.

The point of this script for the Luxor 3 port is to answer "is the original
binary 32-bit x86 (x87/LSU + possibly SSE2) or 64-bit?", which decides whether
a 32-bit compatibility path would be needed.  Since the port re-implements the
game natively, the answer mainly tells us which math semantics to mirror
(x87 extended precision vs SSE) and confirms we are not shipping the .exe.

Usage: python3 pe_probe.py <path-to-exe>
"""
import struct, sys, hashlib, datetime

MACH = {0x14c: "I386 (x86, 32-bit)", 0x8664: "AMD64 (x86-64)",
        0x1c0: "ARM", 0xaa64: "ARM64", 0x1c2: "THUMB", 0x1c4: "ARMNT"}
DD = ["EXPORT", "IMPORT", "RESOURCE", "EXCEPTION", "SECURITY", "BASERELOC",
      "DEBUG", "ARCH", "GLOBALPTR", "TLS", "LOADCONFIG", "BOUNDIMPORT",
      "IAT", "DELAYIMPORT", "CLR", "RESERVED"]


def main(path):
    d = open(path, 'rb').read()
    print(f"file        : {path}")
    print(f"size        : {len(d)} bytes ({len(d)/1048576:.2f} MiB)")
    print(f"sha256      : {hashlib.sha256(d).hexdigest()}")
    if d[:2] != b'MZ':
        print("not an MZ executable"); return 1
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    if d[pe:pe+4] != b'PE\0\0':
        print("no PE signature"); return 1
    mach = struct.unpack_from('<H', d, pe+4)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    ts = struct.unpack_from('<I', d, pe+8)[0]
    optsz = struct.unpack_from('<H', d, pe+20)[0]
    chars = struct.unpack_from('<H', d, pe+22)[0]
    oh = pe + 24
    magic = struct.unpack_from('<H', d, oh)[0]

    print(f"machine     : 0x{mach:04x}  {MACH.get(mach,'?')}")
    print(f"timestamp   : {datetime.datetime.utcfromtimestamp(ts).isoformat()}Z")
    print(f"characteristics: 0x{chars:04x} {'DLL' if chars & 0x2000 else 'EXE'}")
    print(f"opt magic   : 0x{magic:x}  "
          f"{'PE32 (32-bit)' if magic == 0x10b else ('PE32+ (64-bit)' if magic == 0x20b else '?')}")
    print(f"linker      : {d[oh+2]}.{d[oh+3]}")
    print(f"os version  : {struct.unpack_from('<H', d, oh+40)[0]}."
          f"{struct.unpack_from('<H', d, oh+42)[0]}")

    if magic == 0x10b:
        entry = struct.unpack_from('<I', d, oh+16)[0]
        imagebase = struct.unpack_from('<I', d, oh+28)[0]
        subsys = struct.unpack_from('<H', d, oh+68)[0]
        dllchar = struct.unpack_from('<H', d, oh+70)[0]
        ndd = struct.unpack_from('<I', d, oh+92)[0]; ddoff = oh + 96
        print(f"entry rva   : 0x{entry:x}   imagebase 0x{imagebase:x}")
    else:
        entry = struct.unpack_from('<I', d, oh+16)[0]
        imagebase = struct.unpack_from('<Q', d, oh+24)[0]
        subsys = struct.unpack_from('<H', d, oh+68)[0]
        dllchar = struct.unpack_from('<H', d, oh+70)[0]
        ndd = struct.unpack_from('<I', d, oh+108)[0]; ddoff = oh + 112
        print(f"entry rva   : 0x{entry:x}   imagebase 0x{imagebase:x}")
    print(f"subsystem   : {subsys} "
          f"{ {1:'NATIVE',2:'WINDOWS_GUI',3:'WINDOWS_CUI'}.get(subsys,'?') }")
    flags = [n for b, n in [(0x0040,'ASLR'), (0x0100,'NX'), (0x0400,'NO_SEH'),
                            (0x0020,'HIGH_ENTROPY_VA')] if dllchar & b]
    print(f"dll chars   : 0x{dllchar:x} {','.join(flags) if flags else '(none)'}")

    sect_off = oh + optsz
    secs = []
    print("\nsections:")
    for i in range(nsec):
        s = sect_off + i*40
        name = d[s:s+8].rstrip(b'\x00').decode('latin1')
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', d, s+8)
        sc = struct.unpack_from('<I', d, s+36)[0]
        secs.append((name, vaddr, vsize, rawptr, rawsize))
        fl = []
        if sc & 0x20: fl.append("CODE")
        if sc & 0x20000000: fl.append("X")
        if sc & 0x40000000: fl.append("R")
        if sc & 0x80000000: fl.append("W")
        print(f"  {name:8s} vaddr=0x{vaddr:<8x} vsize={vsize:<9d} raw=0x{rawptr:<8x} {','.join(fl)}")

    def rva2off(rva):
        for name, vaddr, vsize, rawptr, rawsize in secs:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        return None

    print("\ndata directories:")
    for i in range(min(ndd, 16)):
        rva, size = struct.unpack_from('<II', d, ddoff + i*8)
        if rva or size:
            print(f"  {DD[i] if i < len(DD) else i:12s} rva=0x{rva:x} size={size}")

    print("\nimports:")
    imp_rva = struct.unpack_from('<I', d, ddoff + 8)[0]
    if imp_rva:
        off = rva2off(imp_rva); i = 0
        step = 4 if magic == 0x10b else 8
        ordflag = 1 << (31 if magic == 0x10b else 63)
        while True:
            e = off + i*20
            oft, _, _, nm, ft = struct.unpack_from('<IIIII', d, e)
            if oft == 0 and nm == 0: break
            noff = rva2off(nm)
            dll = d[noff:d.index(b'\x00', noff)].decode('latin1') if noff else '?'
            thunk = oft or ft
            toff = rva2off(thunk)
            funcs, j = [], 0
            if toff:
                while True:
                    v = struct.unpack_from('<Q' if step == 8 else '<I', d, toff + j*step)[0]
                    if v == 0: break
                    if v & ordflag:
                        funcs.append(f"ord#{v & 0xffff}")
                    else:
                        hn = rva2off(v)
                        if hn:
                            funcs.append(d[hn+2:d.index(b'\x00', hn+2)].decode('latin1'))
                    j += 1
            print(f"  {dll} ({len(funcs)})")
            for f in funcs:
                print(f"      {f}")
            i += 1

    # overlay
    last = max((s[3]+s[4]) for s in secs)
    print(f"\noverlay     : {len(d)-last} bytes past 0x{last:x}")
    if len(d) > last:
        print(f"  first bytes: {d[last:last+24]}")
        if d[last:last+2] == b'PK':
            print("  -> Authenticode signature (PKCS#7) present")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
