import struct

P = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\US\global.res"
b = open(P, "rb").read()
flen = len(b)

blob_start = struct.unpack_from("<I", b, 8)[0]
N = blob_start // 16
print(f"file_len={flen} blob_start={blob_start} N={N} blob_start%16={blob_start%16}")

# read index
rec = []
for i in range(N):
    d0, ident, off, pad = struct.unpack_from("<IIII", b, i*16)
    rec.append((d0, ident, off, pad))

print("slot0:", rec[0])
print("slot1:", rec[1])
print("slot2:", rec[2])
print("slotN-1:", rec[-1])
print("tail4 =", b[-4:].hex(), "as u32 =", struct.unpack_from("<I", b, flen-4)[0])

# pads all zero?
nz = sum(1 for r in rec if r[3] != 0)
print("nonzero pad count:", nz)

# idents sorted ascending unsigned?
asc = all(rec[i][1] <= rec[i+1][1] for i in range(N-1))
print("idents ascending (unsigned):", asc)
# any ident >= 0x80000000 ?
print("max ident = 0x%08x" % max(r[1] for r in rec))

# DISASM MODEL for slot k (found at index mid=k):
#   length_units = rec[k+1].d0 >> 1
#   blob_ptr = base + (rec[k].off >> 1)*2 + 4    ; (off>>1)*2 == off & ~1
#   then copies length_units UTF-16 units, appends NUL
# So byte offset of payload = (rec[k].off & ~1) + 4 ; payload bytes = length_units*2
# Let's test on several slots: read payload, check it's plausible UTF-16, and
# check relationship between rec[k].d0 and rec[k+1].d0 / off deltas.

def dump(k):
    d0,ident,off,pad = rec[k]
    nd0 = rec[k+1][0] if k+1 < N else None
    len_units = (nd0 >> 1) if nd0 is not None else None
    payload_off = (off & ~1) + 4
    s = ""
    if len_units is not None:
        raw = b[payload_off: payload_off + len_units*2]
        try:
            s = raw.decode("utf-16-le", "replace")
        except Exception as e:
            s = repr(e)
    print(f"k={k} d0={d0} ident=0x{ident:08x} off={off} pad={pad} nextd0={nd0} "
          f"len_units={len_units} payload_off={payload_off} -> {s!r}")
    # what is at off..off+4 (the 'entry header' text.lua claims = size u32)?
    hdr = struct.unpack_from("<I", b, off)[0]
    print(f"      u32@off={hdr}  off-blob_start={off-blob_start}  "
          f"d0/2={d0/2} (d0-? )  rec[k].d0 vs len: d0>>1={d0>>1}")

for k in [0,1,2,3,100,11560,N-2]:
    dump(k)

# Hypothesis A: rec[k].d0 == 2*(utf16 units of slot k INCLUDING terminator)?
# Hypothesis B: rec[k].d0 relates to rec[k-1] (length of previous)?
# Test: compare rec[k+1].d0>>1 (=len used for slot k) vs actual span via offsets.
print("\n-- span vs length-from-next-d0 --")
# sort by off to get spans
order = sorted(range(N), key=lambda i: rec[i][2])
pos = {idx:p for p,idx in enumerate(order)}
eof4 = flen - 4
bad = 0
for k in range(N):
    d0,ident,off,pad = rec[k]
    p = pos[k]
    next_off = rec[order[p+1]][2] if p+1 < len(order) else eof4
    span = next_off - off            # bytes from off to next entry
    len_units = (rec[k+1][0] >> 1) if k+1 < N else None
    # text.lua model: header u32 @off = size = utf16_with_term_bytes + 4
    hdr = struct.unpack_from("<I", b, off)[0]
    # disasm payload bytes = len_units*2 ; payload starts at (off&~1)+4
    if len_units is not None:
        # expected: payload (len_units units, NO terminator copied from file) then code adds NUL
        # span should = 4 (header) + (units_incl_term)*2 ; relationship?
        if k < 6 or k == N-2:
            print(f"k={k} off={off} span={span} hdr@off={hdr} "
                  f"len_units(from k+1 d0>>1)={len_units} "
                  f"rec[k].d0={d0} rec[k].d0>>1={d0>>1} "
                  f"hdr==rec? d0==span? {d0==span} hdr==d0? {hdr==d0}")
