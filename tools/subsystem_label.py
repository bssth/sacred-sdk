"""For every subsystem function in walker_dispatch_table.txt, extract:
  - opcodes it handles (via `if (rv == N)` chains)
  - string refs found (especially `s_*` symbol literals — reveals purpose)
  - distinctive helpers called (especially named ones like cKernel_*)
  - first 3 FUN calls
Then resolve string symbols against the live decrypted EXE for actual text.
"""
import os, re, sys, struct, collections
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC_DIR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled"
EXE     = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
TBL     = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\logs\walker_dispatch_table.txt"

# ----- live-EXE string resolver -----
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

def cstr_at(va, maxlen=180):
    off = va2raw(va)
    if off is None: return None
    end = data.find(b"\x00", off, off + maxlen)
    if end < 0: end = off + maxlen
    return data[off:end].decode("latin1", "replace")

# ----- walker dispatch table -----
tbl = open(TBL, encoding="utf-8").read()
tag_to_fun = {}
# Format: `  0x01      5 * FUN_00482510`
for m in re.finditer(r"^\s*(0x[0-9a-fA-F]+|\d+)\s+\d+\s+\*\s+(FUN_[0-9a-f]{8})", tbl, re.M):
    raw = m.group(1)
    tag = int(raw, 16) if raw.startswith("0x") else int(raw)
    fun = m.group(2)
    tag_to_fun[tag] = fun

print(f"loaded {len(tag_to_fun)} tag → fun mappings\n")

# ----- per-fn analysis -----
opcode_re   = re.compile(r"^\s*(?:else\s+)?if\s*\(\s*(\w+)\s*==\s*(0x[0-9a-fA-F]+|\d+)\s*\)\s*\{", re.M)
fun_call_re = re.compile(r"\b(FUN_[0-9a-f]{8}|c[A-Z][A-Za-z0-9_]+_[a-z][A-Za-z0-9_]*_[0-9a-f]{8})\(")
str_use_re  = re.compile(r"(s_[A-Za-z_][A-Za-z0-9_]*_[0-9a-f]{8})")

def analyze(fun):
    va = fun[4:]
    p = os.path.join(DEC_DIR, f"{va}_FUN_{va}.c")
    if not os.path.exists(p):
        return None
    text = open(p, encoding="utf-8", errors="replace").read()
    # opcodes (only consider variables assigned from FUN_00472bc0)
    rv_match = re.search(r"(\w+)\s*=\s*FUN_00472bc0\s*\(", text)
    rv = rv_match.group(1) if rv_match else None
    opcs = set()
    if rv:
        for m in opcode_re.finditer(text):
            if m.group(1) == rv:
                v = m.group(2)
                opcs.add(int(v, 16) if v.startswith("0x") else int(v))
    # FUN calls + strs (over the whole body)
    funs = list(dict.fromkeys(fun_call_re.findall(text)))
    strs_sym = list(dict.fromkeys(str_use_re.findall(text)))
    # resolve each s_* symbol to its actual text
    str_texts = []
    for s in strs_sym:
        va_s = s.rsplit("_", 1)[1]
        try:
            t = cstr_at(int(va_s, 16))
            if t: str_texts.append(t)
        except ValueError:
            pass
    return {
        "opcodes": sorted(opcs),
        "funs":    funs[:8],
        "strs":    str_texts[:6],
        "loc":     text.count("\n"),
    }

# ----- emit table -----
print(f"{'tag':>5}  {'subsystem':<22}  {'lines':>5}  {'opcodes':<28}  identifier-strings (sample)")
print("-" * 130)
all_funs = sorted(tag_to_fun.items())
identified = 0
for tag, fun in all_funs:
    info = analyze(fun)
    if info is None:
        print(f"  {tag:>3}  {fun:<22}  (no decompile yet)")
        continue
    opcs = ",".join(f"0x{o:x}" for o in info['opcodes']) if info['opcodes'] else "(no FUN_00472bc0 loop)"
    sample = info['strs'][0] if info['strs'] else ""
    print(f"  0x{tag:02x}  {fun:<22}  {info['loc']:>5}  {opcs[:26]:<28}  {sample[:60]!r}")
    if info['strs']:
        identified += 1

print(f"\n{identified} / {len(tag_to_fun)} subsystems have at least one identifier string")
