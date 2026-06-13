"""FunkCode.bin v2 walker — size is u16 BIG-ENDIAN.

Record framing:
    [tag:u8][size:u16 BE][payload: size-3 bytes]

The payload itself frequently contains nested records using the same framing,
which is why a non-recursive walker only sees the outermost layer.
"""
import os, struct, collections, sys

ROOT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin"
PATH_SER = os.path.join(ROOT, "TYPE_NPC_SERAPHIM", "FunkCode.bin")
PATH_GLAD = os.path.join(ROOT, "TYPE_NPC_GLADIATOR", "FunkCode.bin")
data = open(PATH_SER, "rb").read()

def safe_cstr(b, off, maxlen=128):
    end = b.find(b"\x00", off, off + maxlen)
    if end < 0:
        return None, off + maxlen
    return b[off:end].decode("latin1", "replace"), end + 1

def parse_records(buf, base=0, max_depth=4, depth=0, max_records=None):
    """Yield (offset, depth, tag, size, payload) by walking sequential records."""
    off = 0
    n = 0
    while off + 3 <= len(buf):
        tag = buf[off]
        size = (buf[off+1] << 8) | buf[off+2]   # BE u16
        if size < 3 or off + size > len(buf):
            # try to bail
            return
        payload = buf[off+3 : off+size]
        yield (base + off, depth, tag, size, payload)
        # recurse if payload looks like more records
        if depth < max_depth and len(payload) >= 3:
            # Heuristic: child records work only if recursion produces ≥ 1 well-formed inner record
            # and consumes all/most of payload.
            inner = list(_try_inner(payload, base + off + 3, depth + 1, max_depth))
            if inner:
                yield from inner
        off += size
        n += 1
        if max_records and n >= max_records:
            return

def _try_inner(buf, base, depth, max_depth):
    """Attempt to parse buf as nested records; only yield if it cleanly tiles the buffer."""
    off = 0
    out = []
    while off + 3 <= len(buf):
        tag = buf[off]
        size = (buf[off+1] << 8) | buf[off+2]
        if size < 3 or off + size > len(buf):
            return  # not a clean nested
        out.append((base + off, depth, tag, size, buf[off+3:off+size]))
        off += size
    if off == len(buf) and len(out) >= 2:   # require clean coverage
        yield from out

# Top-level pass
top = list(parse_records(data, max_depth=0))
print(f"TOP-LEVEL records: {len(top)}")
print(f"last record ends at offset {top[-1][0] + top[-1][3]:#x} / file size {len(data):#x}")

# Tag histogram at top level
tag_hist = collections.Counter(t for _, _, t, _, _ in top)
print(f"\nTop-level tag histogram (top 20):")
for t, c in tag_hist.most_common(20):
    ch = chr(t) if 32 <= t < 127 else "?"
    print(f"  0x{t:02x} '{ch}'  {c:>7}")

# Dump first 30 top-level records
print(f"\nFirst 30 top-level records:")
for off, _, tag, size, payload in top[:30]:
    ch = chr(tag) if 32 <= tag < 127 else "?"
    name = None
    # if payload starts with 0x01 then a cstr, dump it
    if payload and payload[0] == 0x01:
        n, _ = safe_cstr(payload, 1)
        name = n
    elif payload[:5].isascii() and all(32 <= b < 127 or b == 0 for b in payload[:32]):
        n, _ = safe_cstr(payload, 0)
        name = n
    head = " ".join(f"{x:02x}" for x in payload[:24])
    label = f"@ {off:08x}  tag=0x{tag:02x}'{ch}'  size={size:>5}"
    if name:
        label += f"  name='{name}'"
    label += f"  [{head}]"
    print(label)

# Try a recursive walk now
print(f"\n--- RECURSIVE walk ---")
rec = list(parse_records(data, max_depth=3))
print(f"Total events (depth <= 3): {len(rec)}")
tag_hist2 = collections.Counter(t for _, _, t, _, _ in rec)
print(f"All-depth tag histogram (top 25):")
for t, c in tag_hist2.most_common(25):
    ch = chr(t) if 32 <= t < 127 else "?"
    print(f"  0x{t:02x} '{ch}'  {c:>7}")

# Extract every cstr that immediately follows an 0x01 marker inside payloads
print(f"\n--- NAMES found (subtag 0x01 + cstr) — first 80 unique ---")
names = []
seen = set()
for off, depth, tag, size, payload in rec:
    if payload and payload[0] == 0x01:
        n, after = safe_cstr(payload, 1)
        if n and 2 <= len(n) <= 80 and all(c.isalnum() or c == '_' or c == '.' for c in n):
            if n not in seen:
                seen.add(n)
                names.append((off, depth, tag, n))
print(f"Distinct names: {len(seen)}")
for off, depth, tag, name in names[:80]:
    ch = chr(tag) if 32 <= tag < 127 else "?"
    print(f"  @ {off:08x}  d={depth}  tag=0x{tag:02x}'{ch}'  '{name}'")

# Tag character frequency among ASCII tags (gives a hint at "what are the language opcodes")
ascii_tags = {t: c for t, c in tag_hist2.items() if 32 <= t < 127}
print(f"\nASCII tags (these are likely AST node kinds): {sorted(chr(t) for t in ascii_tags)}")

# Per-class divergent region dump
GLAD = open(PATH_GLAD, "rb").read()
n = min(len(data), len(GLAD))
i = 0
while i < n and data[i] == GLAD[i]:
    i += 1
j = 0
while j < n and data[len(data)-1-j] == GLAD[len(GLAD)-1-j]:
    j += 1
print(f"\nPer-class divergent region: SERAPHIM {i:#x}..{len(data)-j:#x} ({len(data)-j-i} bytes)")
print(f"                            GLADIATOR {i:#x}..{len(GLAD)-j:#x} ({len(GLAD)-j-i} bytes)")
print(f"\nFirst 64 bytes of per-class region in SERAPHIM @ {i:#x}:")
for k in range(0, 64, 16):
    h = " ".join(f"{x:02x}" for x in data[i+k:i+k+16])
    a = "".join(chr(x) if 32<=x<127 else "." for x in data[i+k:i+k+16])
    print(f"  {i+k:08x}  {h:<48}  {a}")
print(f"\nFirst 64 bytes of per-class region in GLADIATOR @ {i:#x}:")
for k in range(0, 64, 16):
    h = " ".join(f"{x:02x}" for x in GLAD[i+k:i+k+16])
    a = "".join(chr(x) if 32<=x<127 else "." for x in GLAD[i+k:i+k+16])
    print(f"  {i+k:08x}  {h:<48}  {a}")
