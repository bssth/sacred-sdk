"""Verify the Sacred resource-name hash discovered in FUN_0080e780.

Algorithm (from decompiled FUN_0080e780 @ va:0x0080e780):
    h = 0
    for c in name:
        h = (toupper(c) + h * 113) % 999999991
    return h & 0x7FFFFFFF

The char transform is FUN_0084bce6, which is MSVC toupper() in default-C-locale
fast path: c-0x20 if c in 'a'..'z', else c (full Unicode path falls through to
the CRT, but ASCII names dominate).

This script:
  1. Loads global.res and decodes (id, text) pairs via the existing resolver.
  2. Sweeps the source tree (scripts/, bin/) for plausible name candidates
     (uppercase ASCII identifiers, length >= 3).
  3. Hashes every candidate and counts hits vs. ids that exist in global.res.
"""
import os, re, sys, struct
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

MOD = 0x3b9ac9f7   # 999_999_991, a prime
MUL = 113

def sacred_hash(name: str) -> int:
    """Faithful 32-bit reproduction of FUN_0080e780's inner loop.

      uVar6 = (int)(iVar4 + uVar6 * 0x71) % 0x3b9ac9f7;

    uVar6 is uint; iVar4 is int (toupper of a char, always small positive).
    The multiplication truncates to 32-bit unsigned, the cast (int) is signed
    reinterpret, and C signed `%` keeps the sign of the dividend. The result
    is then reassigned to uVar6 (uint) — reinterpreting any negative remainder
    as a huge unsigned value.
    """
    h = 0
    for c in name:
        oc = ord(c)
        if 0x61 <= oc <= 0x7A:
            oc -= 0x20
        prod = (h * MUL) & 0xFFFFFFFF        # uint32 wrap
        s    = (oc + prod) & 0xFFFFFFFF       # uint32 sum
        si   = s - 0x100000000 if s >= 0x80000000 else s   # (int) cast
        # C signed %: trunc toward zero, sign follows dividend
        if si >= 0:
            r = si % MOD
        else:
            r = -((-si) % MOD)
        h = r & 0xFFFFFFFF                    # back to uint32
    return h & 0x7FFFFFFF


# Math-mode hash (for comparison, in case I'm wrong about overflow)
def sacred_hash_math(name: str) -> int:
    h = 0
    for c in name:
        oc = ord(c)
        if 0x61 <= oc <= 0x7A:
            oc -= 0x20
        h = (oc + h * MUL) % MOD
    return h & 0x7FFFFFFF

# Load global.res ids
GR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\us\global.res"
data = open(GR, "rb").read()
assert data[:4] == b"SZ\x00\x00"
text_blob_start = struct.unpack_from("<I", data, 8)[0]
n_slots = text_blob_start // 16
slots = []
for i in range(n_slots):
    raw_len, ident, off, pad = struct.unpack_from("<IIII", data, i*16)
    if text_blob_start <= off < len(data):
        slots.append((ident, off))
ids_set = set(i for i, _ in slots)
print(f"global.res: {len(ids_set)} distinct ids loaded")

big_ids = {i for i in ids_set if i > 1_000_000}
small_ids = ids_set - big_ids
print(f"  -> small (<1M, looks numeric)   : {len(small_ids)}")
print(f"  -> big   (>=1M, hash candidates): {len(big_ids)}")

# Gather candidate names by scraping ASCII strings out of binary game files +
# any source-style text. The game uses uppercase symbolic names like
# ITEM_QUEST_FOO_DESC, RAR_..., HERO_..., MENU_..., etc.
ROOTS = [
    r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin",
    r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts",
]
NAME_RE = re.compile(rb"[A-Z_][A-Z0-9_]{2,63}")

candidates = set()
for root in ROOTS:
    for dp, _, fns in os.walk(root):
        for fn in fns:
            p = os.path.join(dp, fn)
            try:
                with open(p, "rb") as f:
                    buf = f.read()
            except Exception:
                continue
            for m in NAME_RE.findall(buf):
                # skip pure numeric or trivially short
                s = m.decode("ascii", errors="ignore")
                if len(s) >= 3 and not s.isdigit():
                    candidates.add(s)

print(f"\nharvested {len(candidates):,} unique uppercase candidate names")

# Hash and count hits
for fn_name, fn in [("32-bit faithful", sacred_hash),
                    ("pure-math",       sacred_hash_math)]:
    hash2name = {}
    hits_big, hits_small = 0, 0
    for name in candidates:
        h = fn(name)
        if h in big_ids:   hits_big += 1
        if h in small_ids: hits_small += 1
        hash2name[h] = name
    print(f"\n=== HIT REPORT ({fn_name}) ===")
    print(f"  hits into BIG ids   : {hits_big:>6} / {len(big_ids):>6}")
    print(f"  hits into SMALL ids : {hits_small:>6} / {len(small_ids):>6}")
    print(f"  distinct hashes prod: {len(hash2name):>6}")

# Pick the better variant for the sample dump
hash2name = {sacred_hash(name): name for name in candidates}

# Show samples
print(f"\n--- 20 random name->id hits (big ids) ---")
import random
random.seed(13)
hits = [(name, sacred_hash(name)) for name in candidates if sacred_hash(name) in big_ids]
for name, h in random.sample(hits, min(20, len(hits))):
    # fetch text
    pos = None
    for ident, off in slots:
        if ident == h:
            pos = off + 4; break
    text = ""
    if pos is not None:
        # find end as next slot offset
        next_off = min((o for i, o in slots if o > pos - 4), default=len(data) - 4)
        raw = data[pos: next_off + 4]
        while len(raw) >= 2 and raw[-2:] == b"\x00\x00":
            raw = raw[:-2]
        text = raw.decode("utf-16-le", errors="replace")[:80]
    print(f"  {name:<40} -> hash={h:<10} text={text!r}")
