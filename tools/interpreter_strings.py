"""Pull every DAT_XXXXXXXX / s_NAME_XXXXXXXX referenced in the interpreter
decompile out of the live Sacred_decrypted.exe so we know the actual text
behind each magic constant."""
import struct, sys, re, os
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
DEC = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled\00472bc0_FUN_00472bc0.c"

data = open(EXE, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt_size = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
sec_table = opt + opt_size
secs = []
for i in range(nsec):
    o = sec_table + i * 40
    name = data[o:o+8].rstrip(b"\x00").decode("latin1")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
    secs.append((name, vaddr, vsize, raddr, rsize))

def va2raw(va):
    rva = va - 0x00400000
    for n, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + vsize:
            return raddr + (rva - vaddr)
    return None

def read_cstr(va, maxlen=128):
    off = va2raw(va)
    if off is None: return None
    end = data.find(b"\x00", off, off + maxlen)
    if end < 0: end = off + maxlen
    s = data[off:end].decode("latin1", errors="replace")
    return s

src = open(DEC, encoding="utf-8", errors="replace").read()
# Find all references: DAT_XXXXXXXX or s_FOO_XXXXXXXX
refs = re.findall(r"(DAT_[0-9a-f]{8}|s_[A-Za-z_][A-Za-z0-9_]*_[0-9a-f]{8})", src)
uniq = sorted(set(refs))
print(f"{len(uniq)} unique data refs in FUN_00472bc0\n")
print(f"{'symbol':<40}  va         text")
print("-" * 100)
for r in uniq:
    # extract VA from last 8 hex chars
    va_hex = r[-8:]
    try: va = int(va_hex, 16)
    except ValueError: continue
    txt = read_cstr(va)
    print(f"{r:<40}  0x{va:08x}  {txt!r}")
