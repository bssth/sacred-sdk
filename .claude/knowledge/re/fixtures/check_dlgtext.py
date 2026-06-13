# Verify the dialog text bake: does custom/scripts/us/global.res contain
# CAPMILES_GREET / ROCHEFORD_GREET at sacred_hash(name), and what string?
import struct

GR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\custom\scripts\us\global.res"

def sacred_hash(name):
    MOD, MUL, h = 999999991, 113, 0
    for ch in name:
        oc = ord(ch)
        if 0x61 <= oc <= 0x7a: oc -= 0x20
        prod = (h * MUL) & 0xFFFFFFFF
        s = (oc + prod) & 0xFFFFFFFF
        si = s - 0x100000000 if s >= 0x80000000 else s
        r = (si % MOD) if si >= 0 else -((-si) % MOD)
        if r < 0: r += 0x100000000
        h = r & 0xFFFFFFFF
    return h & 0x7FFFFFFF

blob = open(GR, "rb").read()
blob_start = struct.unpack_from("<I", blob, 8)[0]
n = blob_start // 16
print("global.res: %d slots, blob_start=0x%X, size=%d" % (n, blob_start, len(blob)))

# build ident -> (off, idx)
idents = []
for k in range(n):
    d0, ident, off, pad = struct.unpack_from("<IIII", blob, k*16)
    idents.append((ident, off, k, d0))

last_len_dw = struct.unpack_from("<I", blob, blob_start)[0]

def slot_text(k):
    ident, off, idx, d0 = idents[k]
    two_units = idents[k+1][3] if k < n-1 else last_len_dw
    payload = blob[off+4 : off+4+two_units]
    try:
        return payload.decode("utf-16-le", errors="replace")
    except Exception:
        return repr(payload)

id_to_k = {}
for k,(ident,off,idx,d0) in enumerate(idents):
    id_to_k.setdefault(ident, k)

for name in ("CAPMILES_GREET","CAPMILES_NAME","ROCHEFORD_GREET","ROCHEFORD_NAME",
             "Q1_LAIR_TITLE","Q1_LAIR_S1"):
    h = sacred_hash(name)
    k = id_to_k.get(h)
    if k is None:
        print("  %-16s hash=0x%08X  -> NOT FOUND in global.res" % (name, h))
    else:
        t = slot_text(k)[:80]
        print("  %-16s hash=0x%08X  -> slot#%d  '%s'" % (name, h, k, t))

# sanity: is the index sorted ascending by ident (engine binary-searches)?
asc = all(idents[i][0] <= idents[i+1][0] for i in range(n-1))
print("index sorted ascending by ident:", asc)
