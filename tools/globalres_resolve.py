"""global.res resolver (final).

Layout determined empirically:
  +0x00  16-byte index records, each:
            u32 raw_len      (in record 0 this slot is the magic "SZ\\0\\0";
                              for later records this field describes the
                              *previous* entry's text size — useless to us)
            u32 id           (numeric "res:NNN" identifier, or hashed-name)
            u32 offset       (points 4 bytes BEFORE the actual UTF-16 text)
            u32 zero
  text blob follows; texts are packed back-to-back, no separators.

  To read entry's text:
      actual_start = offset + 4
      actual_end   = next_valid_entry.offset + 4   (or file_end)
      text         = utf-16-le decode of [actual_start : actual_end]
"""
import struct, os, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

P = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\us\global.res"
data = open(P, "rb").read()
assert data[:4] == b"SZ\x00\x00"

text_blob_start = struct.unpack_from("<I", data, 8)[0]
n_slots = text_blob_start // 16
print(f"file size      : {len(data):>10} bytes")
print(f"text blob @    : 0x{text_blob_start:08x}")
print(f"index slots    : {n_slots} (some are junk; filtering)")

# Collect well-formed index slots (offset must point inside text blob).
slots = []
for i in range(n_slots):
    base = i * 16
    raw_len, ident, off, pad = struct.unpack_from("<IIII", data, base)
    if text_blob_start <= off < len(data):
        slots.append((ident, off))
print(f"valid index entries: {len(slots)}")

# Sort by offset to compute text lengths via consecutive-offset diff.
slots_by_off = sorted(slots, key=lambda x: x[1])
ids = {}              # id -> (text_start, text_end)
for k in range(len(slots_by_off)):
    ident, off = slots_by_off[k]
    next_off  = slots_by_off[k+1][1] if k+1 < len(slots_by_off) else (len(data) - 4)
    start = off + 4
    end   = next_off + 4
    if start < end <= len(data):
        # Multiple slots may share an id (rare); keep the one with the larger
        # text length so we don't lose data.
        prev = ids.get(ident)
        if not prev or (end - start) > (prev[1] - prev[0]):
            ids[ident] = (start, end)

def get_text(ident):
    if ident not in ids: return None
    a, b = ids[ident]
    raw = data[a:b]
    while len(raw) >= 2 and raw[-2:] == b"\x00\x00":
        raw = raw[:-2]
    return raw.decode("utf-16-le", errors="replace")

# Sanity dump
print(f"\n--- sanity check: first 8 entries ---")
for k in range(8):
    ident, off = slots_by_off[k]
    t = get_text(ident)
    print(f"  id={ident:<10}  off={off:#08x}  text={t!r}")

# The most-referenced ids from FunkCode quest_inventory output
print(f"\n--- top referenced res:NNN ids ---")
test_ids = [1024, 1037, 1038, 17643, 17631, 17656, 18007, 18237]
for n in test_ids:
    t = get_text(n)
    print(f"  res:{n:<6}  -> {t!r}" if t else f"  res:{n:<6}  (not in index)")

# A handful of huge-id (presumably symbolic-hash) entries
print(f"\n--- 8 spread huge-id (symbolic) samples ---")
import random
random.seed(7)
big_ids = [i for i in ids if i > 1_000_000]
for ident in random.sample(big_ids, 8):
    t = get_text(ident)
    if t and t.strip():
        print(f"  res:{ident:<10}  -> {t[:140]!r}")

# Side effect: expose ids dict for other tools to import.
def all_ids(): return dict(ids)
