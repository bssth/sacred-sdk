"""Dump every printable ASCII cstring in a window around the cScriptCompiler
constants. Adjacent strings usually live in one .data block — we expect to find
keywords, token names, type markers, error messages from the stripped compiler.
"""
import sys, struct, re
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
data = open(EXE, "rb").read()

# PE → raw_off=va when section vaddr == raw raddr (Sacred has that for .data)
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

def raw_of_va(va):
    rva = va - 0x00400000
    for name, vaddr, vsize, raddr in secs:
        if vaddr <= rva < vaddr + vsize:
            return raddr + (rva - vaddr)
    return None

def cstr_scan(start, end):
    """Yield (raw_off, va, length, text) of every printable cstring in [start,end).
    Strings must be >= 4 printable chars + null terminator."""
    cur = start
    while cur < end:
        # Find next printable byte
        while cur < end and not (0x20 <= data[cur] <= 0x7E):
            cur += 1
        if cur >= end: break
        # Find run end (printable until null)
        run_start = cur
        while cur < end and 0x20 <= data[cur] <= 0x7E:
            cur += 1
        if cur < end and data[cur] == 0 and cur - run_start >= 2:
            s = data[run_start:cur].decode("latin1", "replace")
            va = 0x00400000 + run_start  # approx, only valid when vaddr==raddr
            # Compute real VA
            for name, vaddr, vsize, raddr in secs:
                if raddr <= run_start < raddr + vsize:
                    va = 0x00400000 + vaddr + (run_start - raddr)
                    break
            yield run_start, va, cur - run_start, s
        cur += 1

# Window around the cScriptCompiler constants (va 0x00963000 .. 0x00965500)
window_va_start = 0x009640b0
window_va_end   = 0x00964820
raw_start = raw_of_va(window_va_start)
raw_end   = raw_of_va(window_va_end - 1)
if raw_start is None or raw_end is None:
    print("can't locate window in raw file"); sys.exit(1)
raw_end += 1

print(f"# Window: va 0x{window_va_start:08x}..0x{window_va_end:08x}  "
      f"(raw 0x{raw_start:08x}..0x{raw_end:08x})")
print(f"# Strings >=4 printable chars + NUL\n")
for raw, va, n, s in cstr_scan(raw_start, raw_end):
    print(f"  va=0x{va:08x}  len={n:3}  {s!r}")
