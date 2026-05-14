"""Compare Balance.bin variants and profile their structure."""
import os, hashlib, struct, re, collections

paths = {
    "steam":    r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\Balance.bin",
    "vanilla":  r"D:\dev\TODO\SacredUtils-master\SacredUtils\Resources\game\balance\BalanceVanilla.bin",
    "veteran":  r"D:\dev\TODO\SacredUtils-master\SacredUtils\Resources\game\balance\BalanceVeteran.bin",
}

blobs = {k: open(p, "rb").read() for k, p in paths.items()}

print("=== SIZES & HASHES ===")
for k, b in blobs.items():
    print(f"  {k:8} {len(b):8} bytes  sha256={hashlib.sha256(b).hexdigest()[:16]}")

print("\n=== HEADER (first 64 bytes hex) ===")
for k, b in blobs.items():
    hex_ = " ".join(f"{x:02x}" for x in b[:64])
    print(f"  {k:8}: {hex_}")

print("\n=== TAIL (last 32 bytes hex) ===")
for k, b in blobs.items():
    hex_ = " ".join(f"{x:02x}" for x in b[-32:])
    print(f"  {k:8}: {hex_}")

print("\n=== PAIRWISE DIFFS ===")
def diff_ranges(a, b, max_runs=20):
    """Find aligned-position differing ranges (only if same length)."""
    if len(a) != len(b):
        return None
    runs = []
    i = 0
    while i < len(a):
        if a[i] != b[i]:
            j = i
            while j < len(a) and a[j] != b[j]:
                j += 1
            runs.append((i, j - i))
            i = j
        else:
            i += 1
    return runs

pairs = [("steam","vanilla"), ("steam","veteran"), ("vanilla","veteran")]
for x, y in pairs:
    a, b = blobs[x], blobs[y]
    print(f"\n  {x} vs {y}:")
    if len(a) != len(b):
        print(f"    sizes differ: {len(a)} vs {len(b)}, delta {len(b)-len(a):+d}")
        continue
    runs = diff_ranges(a, b)
    print(f"    same size {len(a)} bytes, {len(runs)} differing runs, total diff bytes = {sum(r[1] for r in runs)}")
    for off, ln in runs[:8]:
        ah = " ".join(f"{x:02x}" for x in a[off:off+min(ln,16)])
        bh = " ".join(f"{x:02x}" for x in b[off:off+min(ln,16)])
        print(f"      @ {off:#08x} len {ln:>5}  A={ah}")
        print(f"      {' '*16}            B={bh}")
    if len(runs) > 8:
        print(f"      ... and {len(runs)-8} more")

print("\n=== STRING SCAN (steam Balance.bin) ===")
data = blobs["steam"]
strs = re.findall(rb"[\x20-\x7e]{4,}", data)
print(f"  total ascii runs: {len(strs)}")
# Find offsets of all-caps tokens (likely enum keys we already saw)
caps = [s for s in strs if re.fullmatch(rb"[A-Z][A-Z0-9_]{3,}", s)]
print(f"  CAPS tokens: {len(caps)}")
for s in caps[:30]:
    off = data.find(s)
    print(f"    @ {off:#08x}  {s.decode()}")

print("\n=== POSSIBLE RECORD GRID ===")
# Try to find a repeating period. Print byte histogram around top of file.
# Heuristic: look for repeating low-byte patterns.
def period_guess(b, max_p=128):
    best = []
    for p in range(8, max_p):
        # sample 64 windows
        matches = 0
        for k in range(64):
            i = 256 + k * p
            if i + p >= len(b):
                break
            if b[i:i+4] != b'\x00\x00\x00\x00' and b[i] == b[i + p] and b[i+1] == b[i+1+p]:
                matches += 1
        best.append((matches, p))
    best.sort(reverse=True)
    return best[:6]

for k, b in blobs.items():
    g = period_guess(b)
    print(f"  {k}: top period guesses (matches,period) = {g}")
