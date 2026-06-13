# Quest-book parser: strings + fn start + head disasm
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

def va2off(va):
    rva = va - IMGBASE
    for name, sva, vsz, ptr, rsz in secs:
        if sva <= rva < sva + max(vsz, rsz):
            return ptr + (rva - sva)
    return None

def off2va(off):
    for name, sva, vsz, ptr, rsz in secs:
        if ptr <= off < ptr + rsz:
            return IMGBASE + sva + (off - ptr)
    return None

def cstr(va, maxlen=200):
    o = va2off(va)
    if o is None: return None
    s = d[o:o + maxlen].split(b'\0')[0]
    try:
        return s.decode('latin1')
    except Exception:
        return repr(s)

for sva in (0x94d578, 0x94ce28):
    print("str 0x%08X: %r" % (sva, cstr(sva)))

# function start containing 0x45CA9D
off = va2off(0x45CA9D)
o = off
start = None
while off - o < 0x6000:
    o -= 1
    if d[o:o + 3] == b'\x6a\xff\x68' and d[o - 1] in (0xCC, 0xC3, 0x90, 0x00):
        start = off2va(o); break
    if d[o] == 0xCC and d[o - 1] == 0xCC:
        start = off2va(o + 1); break
print("parser fn start:", hex(start) if start else "???")

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
if start:
    o = va2off(start)
    n = 0x300
    for ins in md.disasm(d[o:o + n], start):
        note = ''
        for tok in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').split():
            if tok.startswith('0x'):
                v = int(tok, 16)
                if 0x00870000 <= v <= 0x009F0000:
                    s = cstr(v, 80)
                    if s and len(s) >= 2 and all(32 <= ord(c) < 127 for c in s):
                        note = '   ; "%s"' % s
        print("0x%08X  %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

# also: find callers of the parser fn (E8 rel32)
if start:
    text = [s for s in secs if s[0] == '.text'][0]
    tptr, trsz, tva = text[3], text[4], text[1]
    i = tptr
    end = tptr + trsz - 5
    print()
    print("callers of parser fn 0x%08X:" % start)
    while i < end:
        if d[i] == 0xE8:
            rel = struct.unpack_from('<i', d, i + 1)[0]
            src_va = IMGBASE + tva + (i - tptr)
            if src_va + 5 + rel == start:
                print("  call @ 0x%08X" % src_va)
        i += 1
