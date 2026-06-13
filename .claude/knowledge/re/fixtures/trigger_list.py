# Writers/readers of the named-trigger list head 0xAB44AC and cache 0xAB44B0.
import struct, capstone

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
IMGBASE = 0x00400000

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

for addr in (0xAB44AC, 0xAB44B0):
    print("=== refs to 0x%08X ===" % addr)
    pat = struct.pack('<I', addr)
    j = tptr
    while True:
        j = d.find(pat, j, tptr + trsz)
        if j < 0:
            break
        ref_va = IMGBASE + tva + (j - tptr)
        # decode the instruction containing the imm
        found = None
        for lead in range(1, 8):
            start = j - lead
            va = IMGBASE + tva + (start - tptr)
            for ins in md.disasm(d[start:start + 16], va):
                if ins.address <= ref_va < ins.address + ins.size:
                    found = ins
                break
            if found:
                break
        if found:
            iswrite = found.op_str.startswith('dword ptr [0x') and found.mnemonic == 'mov'
            print("  0x%08X  %-5s  %-8s %s" % (found.address, 'WRITE' if iswrite else '', found.mnemonic, found.op_str))
        else:
            print("  ~0x%08X (undecoded)" % ref_va)
        j += 1
