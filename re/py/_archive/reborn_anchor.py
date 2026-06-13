"""Find where ReBorn's .text diverges from vanilla. If they only added a few
insertions, we expect long runs of identical code separated by inserted blocks.

Approach:
  - Bidirectional LCS-style longest common runs via simple shifting.
  - For each k-byte sliding window of vanilla .text, search for its position in
    reborn .text; if found, that anchors a chunk. Repeat with a smaller k for
    holes. Report the alignment summary.

If they actually rewrote ~95% of bytes, the anchors will be tiny. If they
shifted by N bytes, almost everything will line up with a constant shift.
"""
import struct, os, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

A_PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
B_PATH = r"C:\Users\bssth\Downloads\SacredReborn.exe"

def load(path):
    with open(path, "rb") as f: data = f.read()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    opt = coff + 20
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    sec_table = opt + opt_size
    for i in range(nsec):
        off = sec_table + i*40
        name = data[off:off+8].rstrip(b"\x00").decode("latin1")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
        if name == ".text":
            return data[raddr: raddr + vsize], vaddr
    return None, None

text_a, va_a = load(A_PATH)
text_b, va_b = load(B_PATH)
assert va_a == va_b
print(f".text vanilla: {len(text_a):,} bytes  @ va=0x{va_a:08x}")
print(f".text reborn : {len(text_b):,} bytes  @ va=0x{va_b:08x}")

# Where does the first divergence start?
n = min(len(text_a), len(text_b))
first = next((i for i in range(n) if text_a[i] != text_b[i]), n)
print(f"first differing byte at offset 0x{first:x} (va=0x{va_a+first:08x})")

# Last identical tail?
last_same = 0
for k in range(min(len(text_a), len(text_b))):
    if text_a[-1-k] != text_b[-1-k]:
        last_same = k
        break
print(f"identical tail length: {last_same} bytes")

# Try the constant-shift hypothesis: scan a 64-byte signature from vanilla at
# va_a+0x100000 and see if it appears in reborn .text, at what offset.
SIG_LEN = 64
for sig_off in (0x100000, 0x200000, 0x300000, 0x400000, 0x10000, 0x50000):
    if sig_off + SIG_LEN > len(text_a): continue
    sig = text_a[sig_off: sig_off + SIG_LEN]
    pos = text_b.find(sig)
    print(f"  sig @ va=0x{va_a+sig_off:08x} ({SIG_LEN}B) -> {'NOT FOUND' if pos<0 else f'va=0x{va_b+pos:08x}, delta={pos-sig_off:+}'}")

# Distribution of large-anchor matches: for each 32-byte chunk of vanilla, is
# it present in reborn? if so where? plot delta histogram.
print("\nrunning anchor scan (32-byte windows every 4096 bytes of vanilla)...")
import collections
delta_hist = collections.Counter()
total = 0
found = 0
for sig_off in range(0, len(text_a) - 32, 4096):
    sig = text_a[sig_off: sig_off + 32]
    if sig.count(0) > 24:  # skip mostly-zero windows (padding)
        continue
    pos = text_b.find(sig)
    total += 1
    if pos >= 0:
        delta = pos - sig_off
        found += 1
        delta_hist[delta] += 1

print(f"sampled {total} windows, found {found} ({100*found/max(1,total):.1f} %)")
print(f"top-15 deltas (delta-bytes : count):")
for d, c in delta_hist.most_common(15):
    print(f"   {d:+8}  : {c}")
