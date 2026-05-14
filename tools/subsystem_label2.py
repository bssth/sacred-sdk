"""Second-pass subsystem identification: also look at distinctive helper FUN
calls (cKernel_*, cObjectManager_*, c<Class>_<method>_*) and at any string
literal in `.data` referenced via DAT_XXXXXXXX (not just s_*).
"""
import os, re, sys, struct, collections
sys.path.insert(0, os.path.dirname(__file__))
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC_DIR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled"
EXE     = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
TBL     = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\logs\walker_dispatch_table.txt"

# Live-EXE string resolver
data = open(EXE, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt_size = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
sec_table = opt + opt_size
secs = []
for i in range(nsec):
    o = sec_table + i*40
    name = data[o:o+8].rstrip(b"\x00").decode("latin1")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o+8)
    secs.append((name, vaddr, vsize, raddr))

def va2raw(va):
    rva = va - 0x00400000
    for n, vaddr, vsize, raddr in secs:
        if vaddr <= rva < vaddr + vsize:
            return raddr + (rva - vaddr)
    return None

def cstr_at(va, maxlen=200):
    off = va2raw(va)
    if off is None: return None
    end = data.find(b"\x00", off, off + maxlen)
    if end < 0: end = off + maxlen
    s = data[off:end].decode("latin1", "replace")
    # filter to printable+meaningful
    if len([c for c in s if 32 <= ord(c) <= 126]) >= len(s) * 0.7 and len(s) >= 3:
        return s
    return None

# tag → fun
tbl = open(TBL, encoding="utf-8").read()
tag_to_fun = {}
for m in re.finditer(r"^\s*(0x[0-9a-fA-F]+|\d+)\s+\d+\s+\*\s+(FUN_[0-9a-f]{8})", tbl, re.M):
    raw = m.group(1)
    tag = int(raw, 16) if raw.startswith("0x") else int(raw)
    tag_to_fun[tag] = m.group(2)

# Per-fn analysis
named_fun_re = re.compile(r"\b(c[A-Z][A-Za-z0-9_]+_[a-z][A-Za-z0-9_]*_[0-9a-f]{8})\(")
dat_ref_re   = re.compile(r"&?(DAT_00[0-9a-f]{6})")
str_use_re   = re.compile(r"(s_[A-Za-z_][A-Za-z0-9_]*_[0-9a-f]{8})")

print(f"{'tag':>5}  {'fun':<22}  candidate name signals")
print("-" * 130)
for tag, fun in sorted(tag_to_fun.items()):
    va = fun[4:]
    p = os.path.join(DEC_DIR, f"{va}_FUN_{va}.c")
    if not os.path.exists(p): continue
    text = open(p, encoding="utf-8", errors="replace").read()

    # collect named (cClass_*) calls
    named_calls = list(dict.fromkeys(named_fun_re.findall(text)))
    # collect every DAT_ ref + try to resolve to a string
    dat_strs = []
    for m in dat_ref_re.finditer(text):
        dr = m.group(1)
        try:
            va_dr = int(dr.split("_")[1], 16)
            s = cstr_at(va_dr)
            if s and s not in dat_strs:
                dat_strs.append(s)
        except Exception:
            pass
    # collect s_ refs
    ss = list(dict.fromkeys(str_use_re.findall(text)))
    s_strs = []
    for s in ss:
        try:
            va_s = int(s.rsplit("_", 1)[1], 16)
            t = cstr_at(va_s)
            if t and t not in s_strs:
                s_strs.append(t)
        except Exception:
            pass
    all_strs = (s_strs + dat_strs)[:6]
    name_calls_short = ", ".join(named_calls[:4]) if named_calls else "-"
    strs_short = ", ".join(repr(s)[:30] for s in all_strs)
    print(f"  0x{tag:02x}  {fun}  {name_calls_short[:60]:<60}  {strs_short[:60]}")
