#!/usr/bin/env python3
# Scan all FunkCode.bin for tag 0x56 (SetIcon / FUN_004a1a50) records and
# decode the embedded opcode-substream's opcode 0x0b operand = the icon
# sprite-id value written to DlgNPC entry+0x48 (then mapped by FUN_004090d0).
import struct, collections, glob, os

def walk(d):
    off = 0; recs = []
    while off + 3 <= len(d):
        tag = d[off]; size = (d[off+1] << 8) | d[off+2]
        if size < 3 or off + size > len(d):
            break
        recs.append((off, tag, size, d[off+3:off+size])); off += size
    return recs

# opcode-substream widths (FUN_00472bc0): 0x0b/0x38/0x5f take a 4-byte dword
W4  = {0x02, 0x11, 0x36, 0x75, 0x0b, 0x38, 0x5f}
W4P = {0x04, 0x0c, 0x0d, 0x2a}
W2  = {0x03, 0x0a, 0x6b, 0x6c, 0x93, 0x9b, 0x9c}
W8  = {0x15}
W12 = {0x19}
WS  = {0x01, 0x29, 0x63, 0x68, 0x69, 0x6a, 0x9d, 0x16, 0x05, 0x09}
NUL = b"\x00"

def op_b_values(pl):
    vals = []; off = 1; n = len(pl)
    while off < n:
        op = pl[off]
        if op == 0:
            break
        if op in W4:
            if off + 5 > n: break
            v = struct.unpack_from("<i", pl, off + 1)[0]
            if op == 0x0b:
                vals.append(v)
            off += 5
        elif op in W4P:
            if off + 5 > n: break
            v = struct.unpack_from("<i", pl, off + 1)[0]
            if v == -2:
                e = pl.find(NUL, off + 5); e = n if e < 0 else e; off = e + 1
            else:
                off += 5
        elif op in W2:
            if off + 3 > n: break
            off += 3
        elif op in W8:
            if off + 9 > n: break
            off += 9
        elif op in W12:
            if off + 13 > n: break
            off += 13
        elif op in WS:
            e = pl.find(NUL, off + 1); e = n if e < 0 else e; off = e + 1
        else:
            off += 1
    return vals

base = r"E:/SteamLibrary/steamapps/common/Sacred Gold/bin"
files = glob.glob(base + "/**/FunkCode.bin", recursive=True)
print("FunkCode.bin files:", len(files))
agg = collections.Counter()
total56 = 0
empty = 0
samples = []
for f in files:
    d = open(f, "rb").read()
    for off, tag, size, pl in walk(d):
        if tag == 0x56:
            total56 += 1
            vs = op_b_values(pl)
            if not vs:
                empty += 1
            for v in vs:
                agg[v] += 1
            if len(samples) < 12 and vs:
                samples.append((os.path.relpath(f, base).replace("\\", "/"),
                                hex(off), vs, pl[:28].hex()))
print("total tag-0x56 records:", total56, " (no op0xb:", empty, ")")
print("op0xb value distribution  (hex / signed / count):")
for v, c in agg.most_common(50):
    print("  0x%08x  %12d  x%d" % (v & 0xffffffff, v, c))
print("samples (file, off, op0xb vals, first28B payload hex):")
for s in samples:
    print("  ", s)
