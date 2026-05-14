"""First-pass analysis of FunkCode.bin (per-class script bytecode)."""
import os, struct, re, collections, hashlib

ROOT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin"

paths_base = {
    "SERAPHIM":   os.path.join(ROOT, "TYPE_NPC_SERAPHIM", "FunkCode.bin"),
    "GLADIATOR":  os.path.join(ROOT, "TYPE_NPC_GLADIATOR", "FunkCode.bin"),
    "MAGICIAN":   os.path.join(ROOT, "TYPE_NPC_MAGICIAN", "FunkCode.bin"),
    "ELVE":       os.path.join(ROOT, "TYPE_NPC_ELVE", "FunkCode.bin"),
    "DARKELVE":   os.path.join(ROOT, "TYPE_NPC_DARKELVE", "FunkCode.bin"),
    "DAEMONIN":   os.path.join(ROOT, "TYPE_NPC_DAEMONIN", "FunkCode.bin"),
    "VAMPIRE":    os.path.join(ROOT, "TYPE_NPC_VAMPIRELADY", "FunkCode.bin"),
    "ZWERG":      os.path.join(ROOT, "TYPE_NPC_ZWERG", "FunkCode.bin"),
}
addon = os.path.join(ROOT, "Addon", "TYPE_NPC_SERAPHIM", "FunkCode.bin")

blobs = {k: open(p, "rb").read() for k, p in paths_base.items()}
addon_blob = open(addon, "rb").read()

print("=== SIZES ===")
for k, b in blobs.items():
    print(f"  base {k:10} {len(b):>9}")
print(f"  addon-master      {len(addon_blob):>9}")

# Header dump (first 128 bytes) for SERAPHIM
print("\n=== HEAD (SERAPHIM base, 128B) ===")
for i in range(0, 128, 16):
    hex_ = " ".join(f"{x:02x}" for x in blobs["SERAPHIM"][i:i+16])
    ascii_ = "".join(chr(x) if 32 <= x < 127 else "." for x in blobs["SERAPHIM"][i:i+16])
    print(f"  {i:04x}  {hex_:<48}  {ascii_}")

# Byte histogram (top 10)
print("\n=== BYTE HISTOGRAM TOP-10 (SERAPHIM base) ===")
h = collections.Counter(blobs["SERAPHIM"])
total = len(blobs["SERAPHIM"])
for b, c in h.most_common(10):
    print(f"  0x{b:02x} ({chr(b) if 32<=b<127 else '?'})  {c:>9}  {c/total*100:5.2f}%")

# How many distinct byte values appear?
distinct = sum(1 for v in h.values() if v > 0)
print(f"  distinct byte values: {distinct}/256")

# Try common longest-prefix between classes — identifies shared header/code region
print("\n=== LONGEST COMMON PREFIX between classes ===")
ref = blobs["SERAPHIM"]
for k, b in blobs.items():
    if k == "SERAPHIM": continue
    n = min(len(ref), len(b))
    i = 0
    while i < n and ref[i] == b[i]:
        i += 1
    print(f"  SERAPHIM vs {k:10}: {i:>9} bytes ({i/n*100:5.2f}%)")

# And longest common suffix
print("\n=== LONGEST COMMON SUFFIX ===")
for k, b in blobs.items():
    if k == "SERAPHIM": continue
    n = min(len(ref), len(b))
    i = 0
    while i < n and ref[len(ref) - 1 - i] == b[len(b) - 1 - i]:
        i += 1
    print(f"  SERAPHIM vs {k:10}: {i:>9} bytes")

# Strings inside SERAPHIM (printable runs ≥ 4 chars)
print("\n=== STRINGS (SERAPHIM base, len>=4, first 60 unique) ===")
data = blobs["SERAPHIM"]
strs = re.findall(rb"[\x20-\x7e]{4,}", data)
uniq = []
seen = set()
for s in strs:
    if s not in seen:
        seen.add(s)
        uniq.append(s)
print(f"  total ascii runs={len(strs)}, unique={len(uniq)}")
for s in uniq[:60]:
    off = data.find(s)
    print(f"    @ {off:#08x}  ({len(s):3d})  {s.decode('latin1')}")

# Anything that looks like a known token from the exe enum list?
print("\n=== TOKEN HITS (cross-referenced with exe enum-style tokens) ===")
# Sample tokens we already know from exe strings dump
known_tokens = [
    b"CL_DEMON", b"CL_DRAGON", b"CL_ELF", b"CL_GOBLIN", b"CL_ENERGY",
    b"CHANCE4BLOCK", b"BONUS_B", b"BONUS_M", b"BONUS_R", b"BONUS_W",
    b"CHEATS", b"AMULET", b"BOW_CH", b"BOW_GS", b"AUTOTRACKENEMY",
    b"AUTOSAVE", b"BLACKSMITH", b"CHICKEN", b"BRACERS",
    b"UI_REGION_NORTHKERN", b"UI_REGION_LAVA",
]
for t in known_tokens:
    if t in data:
        off = data.find(t)
        # show 16 bytes context before
        before = data[max(0,off-16):off]
        print(f"  HIT '{t.decode()}' @ {off:#08x}  preceded by: {' '.join(f'{x:02x}' for x in before)}")
    else:
        pass  # silent miss

# Look for table-of-offsets pattern at start: u32 count, then count*u32 offsets pointing into file
print("\n=== TABLE-OF-OFFSETS hypothesis at start ===")
for k in list(blobs.keys())[:2]:
    b = blobs[k]
    cnt = struct.unpack_from("<I", b, 0)[0]
    print(f"  {k}: first u32 = {cnt} (0x{cnt:x})")
    if 0 < cnt < 200_000:
        ptr_start = 4
        # first few offsets
        ptrs = struct.unpack_from(f"<{min(cnt, 8)}I", b, ptr_start)
        print(f"    first {len(ptrs)} u32s as offsets: {[hex(p) for p in ptrs]}")
        # plausibility: all ptrs increasing and inside file?
        if cnt < 1_000_000 and ptr_start + cnt*4 < len(b):
            all_ptrs = struct.unpack_from(f"<{cnt}I", b, ptr_start)
            mono = all(all_ptrs[i] <= all_ptrs[i+1] for i in range(len(all_ptrs)-1))
            in_range = all(p < len(b) for p in all_ptrs)
            print(f"    monotonic={mono}, all in-file={in_range}, last={hex(all_ptrs[-1]) if all_ptrs else 'n/a'}")

# Compare SERAPHIM with Addon master — is base = addon + extras?
print("\n=== BASE vs ADDON MASTER (SERAPHIM) ===")
a, c = blobs["SERAPHIM"], addon_blob
print(f"  size base={len(a)}, addon={len(c)}, delta={len(a)-len(c)}")
# Longest common prefix
n = min(len(a), len(c))
i = 0
while i < n and a[i] == c[i]:
    i += 1
print(f"  longest common prefix: {i}")
# Longest common suffix
i = 0
while i < n and a[len(a)-1-i] == c[len(c)-1-i]:
    i += 1
print(f"  longest common suffix: {i}")

# Repeated 8-byte ngram count (proxy for instruction patterns or strings)
print("\n=== TOP REPEATED 4-BYTE NGRAMS (SERAPHIM, top 12) ===")
ng = collections.Counter()
for i in range(0, len(data) - 4, 1):
    ng[data[i:i+4]] += 1
for k, v in ng.most_common(12):
    h = " ".join(f"{x:02x}" for x in k)
    a = "".join(chr(x) if 32<=x<127 else "." for x in k)
    print(f"  {h}   [{a}]   ×{v}")
