"""Verify the seven 2.29 patch sites exist in our decrypted vanilla Sacred.exe.

Reference: 2007 Thorium SacredVault unofficial-patch source (Sacred-Patch
Source.txt). All addresses are virtual addresses with image_base=0x00400000.
"""
import struct, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
with open(DEC, "rb") as f: data = f.read()

# Parse PE to compute va->raw offset map
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt = coff + 20
opt_size = struct.unpack_from("<H", data, coff + 16)[0]
sec_table = opt + opt_size
sections = []
for i in range(nsec):
    off = sec_table + i*40
    name = data[off:off+8].rstrip(b"\x00").decode("latin1")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, vaddr, vsize, raddr, rsize))

def va_to_raw(va):
    rva = va - 0x00400000
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + vsize:
            return raddr + (rva - vaddr)
    return None

def hex_at(va, n):
    off = va_to_raw(va)
    if off is None: return None
    return data[off:off+n]

def disp(va, n, label):
    bs = hex_at(va, n)
    if bs is None:
        print(f"  [{label}] va=0x{va:08x}: out of bounds")
        return
    print(f"  [{label}] va=0x{va:08x}: " + " ".join(f"{b:02x}" for b in bs))

print("=== verifying the seven 2.29-patch anchor sites ===\n")

print("Change 1: load global.res from disk (replace block @ 0x0080DC09..0x0080DCCB)")
disp(0x0080DC09, 32, "preamble")
disp(0x0080DCBE, 16, "end-of-block")
# Should look like a real function body, not zeros.

print("\nChange 2: NOP cheat-code area @ 0x0061561B + free zone 0x006156B8..0x00615FA7")
disp(0x0061561B, 6, "patch site (6 NOPs)")
disp(0x006156B8, 16, "free-zone start (must look like real code)")
disp(0x00615FA0, 8, "free-zone end")

print("\nChange 4: Chat-Crash fix anchor @ 0x0084AE81 (replace with jmp+nop)")
disp(0x0084AE81, 16, "patch site")
disp(0x006156DE, 16, "new IsBadReadPtr wrapper lives here")

print("\nChange 5: Version display call @ 0x00758ED0 (push 0x20; push 0x0095D014)")
disp(0x00758ED0, 16, "version-push site")

print("\nChange 6: Debugger-freeze fix — NOP 2 bytes @ 0x00810B0F (kill force-focus)")
disp(0x00810B0F, 8, "force-focus instruction")
# The patch NOPs exactly 2 bytes here. If the original is e.g. "EB nn" (short jmp)
# or "90 90" already, that's diagnostic.

print("\nChange 7: hardcoded Balance.bin path @ 0x00856A8A -> 0x00615717")
disp(0x00856A8A, 16, "patch site (replace with jmp 0x00615717; nop)")
disp(0x00615717, 16, "free area for trampoline")
disp(0x0094C4D4, 32, "expected compare string at 0x0094C4D4")
disp(0x009E1AFC, 24, "hardcoded path '.\\Bin\\Balance.bin' lives here in 2.29")

# Also: confirm FUN_0080e680 (PE BINARY loader) is indeed the function the
# patch hijacks (its caller is at 0x0080DC09).
print("\nReference cross-checks:")
disp(0x0080E680, 16, "FUN_0080e680 prologue (PE BINARY chained-XOR loader)")
disp(0x006156DE, 8, "old code at 0x006156DE (Change 4 trampoline overwrites this)")
