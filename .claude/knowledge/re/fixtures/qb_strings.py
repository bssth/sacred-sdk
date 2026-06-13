# Dump the .rdata string cluster used by the quest-script parser
import struct

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

def va2off(va):
    rva = va - IMGBASE
    for name, sva, vsz, ptr, rsz in secs:
        if sva <= rva < sva + max(vsz, rsz):
            return ptr + (rva - sva)
    return None

lo, hi = 0x94CC00, 0x94E300
o = va2off(lo)
end = va2off(hi)
cur = []
start = o
out = []
i = o
while i < end:
    b = d[i]
    if 32 <= b < 127:
        if not cur:
            start = i
        cur.append(chr(b))
    else:
        if len(cur) >= 3:
            va = lo + (start - o)
            out.append((va, ''.join(cur)))
        cur = []
    i += 1
for va, s in out:
    print("0x%08X  %s" % (va, s))
