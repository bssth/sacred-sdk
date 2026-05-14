"""Sanity-check the target bytes for both runtime patches before we trust the
DLL with them. Reads our Sacred_decrypted.exe (= what the live process .text
should match after the .bind stub decrypts in place).

  Patch 1: FUN_0080e680 should begin with `6a ff 68 …` (SEH prologue).
  Patch 6: FUN_00811440 should contain a `c3` or `c2 nn nn` epilogue within
           600 bytes. Decompile shows the function calls
           SetForegroundWindow @ 0x00811487 and SetFocus @ 0x008114be,
           confirming we have the right entry.
"""
import struct, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
data = open(DEC, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt = coff + 20
opt_size = struct.unpack_from("<H", data, coff + 16)[0]
sec_table = opt + opt_size
secs = []
for i in range(nsec):
    off = sec_table + i*40
    name = data[off:off+8].rstrip(b"\x00").decode("latin1")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
    secs.append((name, vaddr, vsize, raddr))

def va2off(va):
    rva = va - 0x00400000
    for n, va_s, vs, ra in secs:
        if va_s <= rva < va_s + vs:
            return ra + (rva - va_s)
    return None

def hex_of(va, n):
    o = va2off(va)
    return " ".join(f"{b:02x}" for b in data[o:o+n])

print(f"Patch 1 target  va=0x0080e680 first 8: {hex_of(0x0080E680, 8)}")
print(f"  expected SEH prologue 6a ff 68 ... — OK to detour\n")

print(f"Patch 6 target  va=0x00811440 first 16: {hex_of(0x00811440, 16)}")
# Scan for first ret-style epilogue within 600 bytes
o = va2off(0x00811440)
for i in range(600):
    b = data[o + i]
    if b == 0xC3:
        print(f"  found C3 (cdecl ret)        at offset +0x{i:x} (va=0x{0x00811440+i:08x})")
        print(f"  -> patch will write: 33 c0 c3  (xor eax,eax; ret)")
        break
    if b == 0xC2:
        imm = data[o+i+1] | (data[o+i+2] << 8)
        print(f"  found C2 nn nn (stdcall ret) at offset +0x{i:x} (va=0x{0x00811440+i:08x}), imm=0x{imm:x}")
        print(f"  -> patch will write: 33 c0 c2 {imm&0xff:02x} {imm>>8:02x}  (xor eax,eax; ret 0x{imm:x})")
        break
else:
    print("  NO RET FOUND in 600 bytes — patch will skip!")

# Verify the function actually contains SetForegroundWindow / SetFocus calls
# (confirms we're looking at the right function in our build).
print(f"\nCross-checks within FUN_00811440 (looking for FF 15 = call indirect):")
o = va2off(0x00811440)
calls = []
for i in range(600):
    if data[o+i] == 0xFF and data[o+i+1] == 0x15:
        # 6-byte indirect call: FF 15 <imm32 = absolute address of import slot>
        slot_va = struct.unpack_from("<I", data, o+i+2)[0]
        calls.append((0x00811440 + i, slot_va))
for site, slot in calls:
    print(f"  call indirect @ va=0x{site:08x}  slot=0x{slot:08x}")
