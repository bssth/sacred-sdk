"""Locate every byte-level patch ReBorn applied to Sacred.exe.

Strategy:
  - Map .text from each EXE by RVA (both start at vaddr=0x1000, file-offset = vaddr
    because section alignment = file alignment in PE32 for both EXEs).
  - For each RVA where both bytes are valid, compare. Cluster differing bytes
    into 'regions' separated by >= 16 contiguous matching bytes (so neighbour
    patches don't fragment).
  - Emit region summary: VA, size, first/last differing bytes.
  - For the top-N regions, dump a short hex view of (vanilla, reborn) and try
    to attribute each to a known function from our Ghidra symbols by nearest
    function-start address (the renamed cClassName_methodName_xxxxxxxx).

Repeat the same for .rdata and .data.
"""
import struct, os, sys
sys.path.insert(0, os.path.dirname(__file__))
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

A_PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
B_PATH = r"C:\Users\bssth\Downloads\SacredReborn.exe"

def load(path):
    with open(path, "rb") as f: data = f.read()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", data, opt + 28)[0]
    sec_table = opt + opt_size
    secs = {}
    for i in range(nsec):
        off = sec_table + i*40
        name = data[off:off+8].rstrip(b"\x00").decode("latin1")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
        char = struct.unpack_from("<I", data, off + 32)[0]
        secs[name] = {"vaddr": vaddr, "vsize": vsize, "raddr": raddr, "rsize": rsize}
    return data, image_base, secs

A, base_a, sec_a = load(A_PATH)
B, base_b, sec_b = load(B_PATH)
assert base_a == base_b == 0x00400000

def section_bytes(buf, sec, name):
    s = sec[name]
    # use vsize (actual code), pad with 0x00 if rsize > vsize
    return buf[s["raddr"]: s["raddr"] + s["vsize"]], s["vaddr"]

def cluster_regions(diff_offsets, gap=16):
    """Given a sorted list of differing offsets, cluster into [(start,end),...]
    where end is exclusive and any single gap >= `gap` starts a new region."""
    regions = []
    if not diff_offsets: return regions
    start = prev = diff_offsets[0]
    for o in diff_offsets[1:]:
        if o - prev > gap:
            regions.append((start, prev + 1))
            start = o
        prev = o
    regions.append((start, prev + 1))
    return regions

# Load Ghidra-recovered function symbols (one per line from decompiled/ dir)
SYM_DIR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled"
sym_by_va = {}
if os.path.isdir(SYM_DIR):
    for fn in os.listdir(SYM_DIR):
        # filename: 00472bc0_FUN_00472bc0.c   or   006726f0_FUN_006726f0.c
        m = fn.split("_", 1)
        if len(m) < 2 or not m[0].startswith("00"): continue
        try:
            va = int(m[0], 16)
            sym_by_va[va] = fn[:-2]   # strip .c
        except ValueError:
            pass

def nearest_sym(va):
    """Closest known function-start <= va, or None."""
    best = None
    for fva in sym_by_va:
        if fva <= va and (best is None or fva > best):
            best = fva
    if best is None: return None
    return best, sym_by_va[best]

def diff_section(secname):
    if secname not in sec_a or secname not in sec_b:
        print(f"[{secname}] missing in one side")
        return
    sa, va_a = section_bytes(A, sec_a, secname)
    sb, va_b = section_bytes(B, sec_b, secname)
    # Use the smaller common range, anchored at the shared start vaddr.
    if va_a != va_b:
        print(f"[{secname}] vaddrs differ ({va_a:#x} vs {va_b:#x}); skipping")
        return
    n = min(len(sa), len(sb))
    diffs = [i for i in range(n) if sa[i] != sb[i]]
    print(f"\n=== {secname}  (compared {n:,} bytes, "
          f"{len(diffs):,} different = {100*len(diffs)/n:.3f} %) ===")
    regions = cluster_regions(diffs, gap=16)
    print(f"    grouped into {len(regions)} regions (gap >= 16)")
    # sort by size desc
    regions.sort(key=lambda r: r[1] - r[0], reverse=True)
    print(f"    showing top 30 regions:")
    print(f"    {'idx':>4}  {'VA':>10}  {'size':>6}  nearest_sym")
    for i, (s, e) in enumerate(regions[:30]):
        va = va_a + s
        sym = nearest_sym(va)
        sym_s = f"{sym[1]}  (+0x{va-sym[0]:x})" if sym else "(unknown)"
        print(f"    {i:>4}  0x{va:08x}  {e-s:>6}  {sym_s}")
    # Hex dump for top 5
    print("\n    ---- top-5 hex preview ----")
    for i, (s, e) in enumerate(regions[:5]):
        va = va_a + s
        ln = min(e - s, 48)
        ah = " ".join(f"{b:02x}" for b in sa[s:s+ln])
        bh = " ".join(f"{b:02x}" for b in sb[s:s+ln])
        print(f"\n    [{i}] va=0x{va:08x} size={e-s}")
        print(f"        vanilla: {ah}")
        print(f"        reborn : {bh}")
    return regions, va_a, sa, sb

print(f"vanilla: size={len(A):,}  image_base=0x{base_a:08x}")
print(f"reborn : size={len(B):,}  image_base=0x{base_b:08x}")
print(f"ghidra symbols loaded: {len(sym_by_va)}")

r_text  = diff_section(".text")
r_rdata = diff_section(".rdata")
r_data  = diff_section(".data")
