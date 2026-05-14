"""Find raw 4-byte LE references to a VA in the .text section.
Bypasses Ghidra — direct scan of decrypted binary for the address as immediate."""
import sys, struct
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
data = open(EXE, "rb").read()

# PE header
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

def raw_to_va(raw_off):
    for name, vaddr, vsize, raddr in secs:
        if raddr <= raw_off < raddr + vsize:
            return 0x00400000 + vaddr + (raw_off - raddr)
    return None

# Find raw offset of .text
text_sec = next((s for s in secs if s[0] == ".text"), None)
text_start, text_end = text_sec[3], text_sec[3] + text_sec[2]

targets_hex = sys.argv[1:]
for hex_str in targets_hex:
    va = int(hex_str, 16)
    needle = struct.pack("<I", va)
    print(f"\n=== references to VA 0x{va:08x} in .text ===")
    hits = []
    off = text_start
    while True:
        i = data.find(needle, off, text_end)
        if i < 0: break
        hits.append(i)
        off = i + 1
    print(f"  {len(hits)} occurrence(s)")
    for h in hits[:8]:
        ref_va = raw_to_va(h)
        # Show 16 bytes before for instruction context
        ctx = data[max(text_start, h-8): h+8]
        print(f"  @ raw 0x{h:08x} (va 0x{ref_va:08x})  ctx: {ctx.hex()}")
