# Who reads/writes the quest-book table pointer 0xAAC738 (stride-0x88 entries,
# indexed by quest-slot byte +0x01)? Writers = the quest book loader (issue #1).
import struct, capstone

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
IMGBASE = 0x00400000
ADDR = 0xAAC738

d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 0x14)[0]
secoff = pe + 0x18 + opt
secs = []
for i in range(nsec):
    o = secoff + i * 0x28
    name = d[o:o + 8].rstrip(b'\0').decode('latin1')
    vsz, va, rsz, ptr = struct.unpack_from('<IIII', d, o + 8)
    secs.append((name, va, vsz, ptr, rsz))

text = [s for s in secs if s[0] == '.text'][0]
tname, tva, tvsz, tptr, trsz = text

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

pat = struct.pack('<I', ADDR)
hits = []
i = tptr
while True:
    i = d.find(pat, i, tptr + trsz)
    if i < 0:
        break
    hits.append(i)
    i += 1

print("imm32 0x%08X occurrences in .text: %d" % (ADDR, len(hits)))
for h in hits:
    # disasm a window starting a bit before so the instruction containing the
    # imm decodes; find the instruction whose bytes cover offset h
    found = None
    for lead in range(1, 8):
        va = IMGBASE + tva + (h - lead - tptr)
        for ins in md.disasm(d[h - lead:h - lead + 16], va):
            if ins.address <= IMGBASE + tva + (h - tptr) < ins.address + ins.size:
                found = ins
                break
        if found:
            break
    if found:
        kind = 'WRITE' if (found.mnemonic.startswith('mov') and found.op_str.startswith('dword ptr [0x')) else 'read '
        print("  0x%08X  %-6s %-8s %s" % (found.address, kind, found.mnemonic, found.op_str))
    else:
        print("  raw offset 0x%X (VA~0x%08X) — could not decode" % (h, IMGBASE + tva + (h - tptr)))
