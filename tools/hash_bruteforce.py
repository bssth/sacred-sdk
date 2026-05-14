"""Bruteforce the hash function that maps symbolic res:NAME tokens to the
32-bit ids that global.res indexes by.

Strategy:
  1. Load all 23 123 ids from global.res.
  2. Collect candidate symbolic names from:
        a. FunkCode.bin -- 982 declared symbols (C/i records), plus the
           full pre-resolved "res:..." cstr values.
        b. Quest-name patterns from quest_inventory's enumeration.
  3. For each (hash function, input variant) pair, hash every candidate
     name and count how many hashes hit the id set.
  4. Rank by hit-count. The winner is, at minimum, an indicator of which
     hash family is in use; usually it's an exact match to a single hash.
"""
import os, sys, struct, binascii, zlib, re, collections, importlib.util
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

SDK = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools"

# Import the globalres_resolve module to reuse its id parser
spec = importlib.util.spec_from_file_location("gr", os.path.join(SDK, "globalres_resolve.py"))
gr = importlib.util.module_from_spec(spec)
# globalres_resolve prints when imported; silence stdout for that brief moment
import io as _io
_saved = sys.stdout
sys.stdout = _io.StringIO()
spec.loader.exec_module(gr)
sys.stdout = _saved

ID_SET = set(gr.all_ids().keys())
print(f"global.res has {len(ID_SET)} distinct ids")
print(f"  min={min(ID_SET)}  max={max(ID_SET)}")
print(f"  low (<= 65536): {sum(1 for i in ID_SET if i <= 65536)}")
print(f"  high (> 1e6)  : {sum(1 for i in ID_SET if i > 1_000_000)}")

# Collect candidate names from all FunkCode.bin files
ROOT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin"
CLASSES = ["TYPE_NPC_SERAPHIM","TYPE_NPC_GLADIATOR","TYPE_NPC_MAGICIAN","TYPE_NPC_ELVE",
           "TYPE_NPC_DARKELVE","TYPE_NPC_DAEMONIN","TYPE_NPC_VAMPIRELADY","TYPE_NPC_ZWERG"]

# Pull every cstr from FunkCode.bin that looks like a quest/symbol name.
NAME_RX = re.compile(rb"[A-Za-z_][A-Za-z0-9_:.\-]{3,80}")
names = set()
for cls in CLASSES:
    p = os.path.join(ROOT, cls, "FunkCode.bin")
    if not os.path.exists(p): continue
    data = open(p, "rb").read()
    for m in NAME_RX.findall(data):
        s = m.decode("latin1", "ignore")
        names.add(s)

print(f"\ncollected {len(names)} candidate names from FunkCode.bin")

# Also collect from res: references specifically (strip "res:" prefix)
res_names = {n[4:] for n in names if n.startswith("res:")}
print(f"  of which {len(res_names)} are 'res:NAME' (after prefix strip)")

# --- hash function zoo ---
def djb2(b, h0=5381):
    h = h0
    for c in b:
        h = ((h * 33) + c) & 0xFFFFFFFF
    return h

def djb2_xor(b, h0=5381):
    h = h0
    for c in b:
        h = ((h * 33) ^ c) & 0xFFFFFFFF
    return h

def sdbm(b):
    h = 0
    for c in b:
        h = (c + (h << 6) + (h << 16) - h) & 0xFFFFFFFF
    return h

def fnv1a32(b, h0=0x811c9dc5):
    h = h0
    for c in b:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h

def fnv1_32(b, h0=0x811c9dc5):
    h = h0
    for c in b:
        h = ((h * 0x01000193) ^ c) & 0xFFFFFFFF
    return h

def lose_hash(b):
    # MSVC/STL "loose hash" sometimes
    h = 0
    for c in b: h = (h + c) & 0xFFFFFFFF
    return h

def crc32_std(b):
    return binascii.crc32(b) & 0xFFFFFFFF

def adler32_std(b):
    return zlib.adler32(b) & 0xFFFFFFFF

def hash_lookup3_jenkins(b):
    # Bob Jenkins lookup3 (simplified for short keys)
    a = b_ = c = 0xdeadbeef + len(b)
    i = 0
    while len(b) - i >= 12:
        a += int.from_bytes(b[i:i+4], "little")
        b_ += int.from_bytes(b[i+4:i+8], "little")
        c += int.from_bytes(b[i+8:i+12], "little")
        a &= 0xFFFFFFFF; b_ &= 0xFFFFFFFF; c &= 0xFFFFFFFF
        # mix
        a = (a - c) & 0xFFFFFFFF; a ^= ((c << 4) | (c >> 28)) & 0xFFFFFFFF; c = (c + b_) & 0xFFFFFFFF
        b_ = (b_ - a) & 0xFFFFFFFF; b_ ^= ((a << 6) | (a >> 26)) & 0xFFFFFFFF; a = (a + c) & 0xFFFFFFFF
        c = (c - b_) & 0xFFFFFFFF; c ^= ((b_ << 8) | (b_ >> 24)) & 0xFFFFFFFF; b_ = (b_ + a) & 0xFFFFFFFF
        a = (a - c) & 0xFFFFFFFF; a ^= ((c << 16) | (c >> 16)) & 0xFFFFFFFF; c = (c + b_) & 0xFFFFFFFF
        b_ = (b_ - a) & 0xFFFFFFFF; b_ ^= ((a << 19) | (a >> 13)) & 0xFFFFFFFF; a = (a + c) & 0xFFFFFFFF
        c = (c - b_) & 0xFFFFFFFF; c ^= ((b_ <<  4) | (b_ >> 28)) & 0xFFFFFFFF; b_ = (b_ + a) & 0xFFFFFFFF
        i += 12
    # remaining bytes mixed cheaply
    rem = b[i:]
    for j, x in enumerate(rem):
        c = (c + (x << (8*(j % 4)))) & 0xFFFFFFFF
    return c

def murmur3_32(b, seed=0):
    c1 = 0xcc9e2d51; c2 = 0x1b873593
    r1 = 15; r2 = 13; m = 5; n = 0xe6546b64
    h = seed
    i = 0
    while i + 4 <= len(b):
        k = int.from_bytes(b[i:i+4], "little")
        k = (k * c1) & 0xFFFFFFFF
        k = ((k << r1) | (k >> (32 - r1))) & 0xFFFFFFFF
        k = (k * c2) & 0xFFFFFFFF
        h ^= k
        h = ((h << r2) | (h >> (32 - r2))) & 0xFFFFFFFF
        h = (h * m + n) & 0xFFFFFFFF
        i += 4
    rem = b[i:]
    k = 0
    if len(rem) >= 3: k ^= rem[2] << 16
    if len(rem) >= 2: k ^= rem[1] << 8
    if len(rem) >= 1:
        k ^= rem[0]
        k = (k * c1) & 0xFFFFFFFF
        k = ((k << r1) | (k >> (32 - r1))) & 0xFFFFFFFF
        k = (k * c2) & 0xFFFFFFFF
        h ^= k
    h ^= len(b)
    h ^= h >> 16
    h = (h * 0x85ebca6b) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 0xc2b2ae35) & 0xFFFFFFFF
    h ^= h >> 16
    return h

HASHES = [
    ("djb2",        djb2),
    ("djb2_xor",    djb2_xor),
    ("sdbm",        sdbm),
    ("fnv1a32",     fnv1a32),
    ("fnv1_32",     fnv1_32),
    ("crc32",       crc32_std),
    ("adler32",     adler32_std),
    ("jenkins",     hash_lookup3_jenkins),
    ("murmur3_32",  murmur3_32),
    ("loose_sum",   lose_hash),
]

# Variants of each name to try
def variants(name):
    yield ("as-is",          name.encode("latin1", "ignore"))
    yield ("lower",          name.lower().encode("latin1", "ignore"))
    yield ("upper",          name.upper().encode("latin1", "ignore"))
    yield ("nul",            name.encode("latin1", "ignore") + b"\x00")
    yield ("upper-nul",      name.upper().encode("latin1", "ignore") + b"\x00")
    yield ("res-prefix",     ("res:" + name).encode("latin1", "ignore"))
    yield ("res-prefix-nul", ("res:" + name).encode("latin1", "ignore") + b"\x00")

# Run the bruteforce
print(f"\nrunning {len(HASHES)} x 7 variants over {len(names)} names...")
results = []   # (hits, hash_name, variant, sample_hits)
for hname, hfn in HASHES:
    for vname in ["as-is","lower","upper","nul","upper-nul","res-prefix","res-prefix-nul"]:
        hits = []
        for n in names:
            for _vn, b in variants(n):
                if _vn != vname: continue
                if hfn(b) in ID_SET:
                    hits.append(n)
                break
        results.append((len(hits), hname, vname, hits[:5]))

# rank
results.sort(reverse=True)
print(f"\n=== top 20 (hits / hash / variant / sample names) ===")
for hits, hname, vname, sample in results[:20]:
    print(f"  {hits:>5}  {hname:<14} {vname:<16}  sample: {sample[:3]}")

# detailed look at the winner — if it has > 100 hits, demonstrate resolution
if results[0][0] > 50:
    hits, hname, vname, sample = results[0]
    hfn = dict(HASHES)[hname]
    print(f"\n=== WINNER: {hname} / {vname} -> {hits} hits ===")
    # find sample names AND their resolved ids
    matched = []
    for n in names:
        for _vn, b in variants(n):
            if _vn != vname: continue
            h = hfn(b)
            if h in ID_SET:
                matched.append((n, h))
            break
        if len(matched) >= 20: break
    print(f"first 20 resolved (name -> id -> text):")
    for n, h in matched[:20]:
        t = gr.get_text(h)
        if t is None: t = "(no text resolved)"
        short = t.replace("\n", " ")[:120]
        print(f"  {n!r:<60} -> id={h:>11} -> text={short!r}")
