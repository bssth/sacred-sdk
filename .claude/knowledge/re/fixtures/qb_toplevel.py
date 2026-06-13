# Find ALL references (imm32 + call/jmp rel32) to the quest parser 0x45A370
# and to its file-open helper 0x65AEA0; also locate " * scripts/ready" xref.
import struct, capstone

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
IMGBASE = 0x00400000
TARGETS = [0x0045A370]
STR_REFS = [0x94E29C, 0x94E2C0, 0x94E2DC, 0x94E0F0]  # " * scripts/ready", " * scripts/questregions", "bin\\sgqp.bin", "bin\\MultiStart.bin"

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

# rel32 call/jmp
for tgt in TARGETS:
    print("refs to 0x%08X:" % tgt)
    i = tptr
    end = tptr + trsz - 5
    while i < end:
        if d[i] in (0xE8, 0xE9):
            rel = struct.unpack_from('<i', d, i + 1)[0]
            src = IMGBASE + tva + (i - tptr)
            if src + 5 + rel == tgt:
                print("  %s @ 0x%08X" % ('call' if d[i] == 0xE8 else 'jmp ', src))
        i += 1
    # imm32
    pat = struct.pack('<I', tgt)
    j = tptr
    while True:
        j = d.find(pat, j, tptr + trsz)
        if j < 0: break
        print("  imm32 @ ~0x%08X" % (IMGBASE + tva + (j - tptr)))
        j += 1

for sva in STR_REFS:
    pat = struct.pack('<I', sva)
    print("refs to str 0x%08X:" % sva)
    j = tptr
    while True:
        j = d.find(pat, j, tptr + trsz)
        if j < 0: break
        print("  imm32 @ ~0x%08X" % (IMGBASE + tva + (j - tptr)))
        j += 1
