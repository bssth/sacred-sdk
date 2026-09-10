"""Authoritative CreateNPC (tag 0x01) record decoder.

Field widths taken from the interpreter FUN_00472bc0 switch(local_114).
"""
import os, struct, collections

ROOT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin"

CSTR = {0x01,0x29,0x63,0x68,0x69,0x6a,0x9d, 0x05,0x09,0x41,0x60,0x83,
        0x3e,0x77,0x81,0x82, 0x40,0x95, 0x47,0x52,0x7d,0x8f,
        0x16,0x1e,0x3a,0x67}
U16   = {0x03,0x0a,0x6b,0x6c,0x9b,0x9c,0x93}
I32   = {0x02,0x11,0x36,0x75, 0x1c,0x53,0x54,0x55,0x56,0x57,0x86, 0x1d,
         0x6f, 0x7e,0x7f,0x8c,0x90, 0x84}
I32CS = {0x0b,0x38,0x5f}          # i32, and if i32 == -0x12d687 a cstr follows
POS   = {0x04,0x0c,0x0d}          # i32, and if i32 == -2 a cstr follows
U32CS = {0x7a}                    # i32 + cstr
W9    = {0x15, 0x33,0x87,0x88,0x89, 0x34,0x79}
W13   = {0x19, 0x35, 0x20, 0x2a, 0x4d}
W17   = {0x3d}
W2    = {0x28, 0x8b}
W4    = {0x9f}
FLAG  = {0x06,0x07,0x08,0x0e,0x0f,0x10,0x12,0x13,0x14,0x1a,0x1b,0x23,0x24,0x25,
         0x26,0x27,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x39,0x3f,0x42,0x43,
         0x44,0x45,0x46,0x4c,0x4e,0x4f,0x50,0x51,0x58,0x59,0x5a,0x5b,0x5c,0x61,
         0x62,0x64,0x65,0x66,0x70,0x72,0x74,0x78,0x7b,0x7c,0x80,0x85,0x8a,0x8d,
         0x8e,0x91,0x96,0x97,0x98,0x99,0x9a,0x9e,0xa0,0xa1,
         0x37,0x4b,0x71,0x92}

def _cstr(p, o):
    e = p.find(b"\0", o)
    if e < 0: return None, len(p)
    return p[o:e].decode("latin1", "replace"), e + 1

def decode(payload):
    """payload = bytes after the 3-byte record header. Returns (fields, ok)."""
    out = []
    o = 1                                   # skip the 1 flags byte
    n = len(payload)
    while o < n:
        op = payload[o]
        if op == 0:
            out.append((op, "end", None)); o += 1; break
        if op in FLAG:
            out.append((op, "flag", None)); o += 1
        elif op in CSTR:
            s, o2 = _cstr(payload, o+1)
            if s is None: return out, False
            out.append((op, "str", s)); o = o2
        elif op in U16:
            if o+3 > n: return out, False
            out.append((op, "u16", struct.unpack_from("<H", payload, o+1)[0])); o += 3
        elif op in I32:
            if o+5 > n: return out, False
            out.append((op, "i32", struct.unpack_from("<i", payload, o+1)[0])); o += 5
        elif op in POS:
            if o+5 > n: return out, False
            v = struct.unpack_from("<i", payload, o+1)[0]
            if v == -2:
                s, o2 = _cstr(payload, o+5)
                if s is None: return out, False
                out.append((op, "pos", s)); o = o2
            else:
                if o+13 > n: return out, False
                y, z = struct.unpack_from("<ii", payload, o+5)
                out.append((op, "xyz", (v, y, z))); o += 13
        elif op in I32CS:
            if o+5 > n: return out, False
            v = struct.unpack_from("<i", payload, o+1)[0]
            if v == -0x12d687:
                s, o2 = _cstr(payload, o+5)
                if s is None: return out, False
                out.append((op, "ref", s)); o = o2
            else:
                out.append((op, "i32", v)); o += 5
        elif op in U32CS:
            if o+5 > n: return out, False
            v = struct.unpack_from("<I", payload, o+1)[0]
            s, o2 = _cstr(payload, o+5)
            if s is None: return out, False
            out.append((op, "u32str", (v, s))); o = o2
        elif op == 0x1f:
            # value slot: either a symref token  9F EF BE ED FE <name>
            # (FUN_00453970) or a bare i32.
            if o+6 <= n and payload[o+1] == 0x9f and struct.unpack_from("<I", payload, o+2)[0] == 0xFEEDBEEF:
                s2, o2 = _cstr(payload, o+6)
                if s2 is None: return out, False
                out.append((op, "symref", s2)); o = o2
            else:
                if o+5 > n: return out, False
                out.append((op, "i32", struct.unpack_from("<i", payload, o+1)[0])); o += 5
        elif op in W9:  out.append((op, "w9",  payload[o+1:o+9]));  o += 9
        elif op in W13: out.append((op, "w13", payload[o+1:o+13])); o += 13
        elif op in W17: out.append((op, "w17", payload[o+1:o+17])); o += 17
        elif op in W2:  out.append((op, "u8",  payload[o+1]));      o += 2
        elif op in W4:  out.append((op, "w4",  payload[o+1:o+4]));  o += 4
        else:
            out.append((op, "UNKNOWN", None))
            return out, False
        if o > n: return out, False
    return out, True

def records(buf, tag_filter=None):
    off = 0
    while off + 3 <= len(buf):
        tag = buf[off]
        size = (buf[off+1] << 8) | buf[off+2]
        if size < 3 or off + size > len(buf): return
        if tag_filter is None or tag == tag_filter:
            yield off, tag, buf[off+3:off+size]
        off += size

def all_bins():
    for dirpath, _, files in os.walk(ROOT):
        for f in files:
            if f.lower().endswith(".bin"):
                yield os.path.join(dirpath, f)
