# Disassemble the candidate quest-book vector fillers (ecx=0xAAC738 call sites)
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

def cstr(va, maxlen=120):
    o = va2off(va)
    if o is None: return None
    s = d[o:o + maxlen].split(b'\0')[0]
    if not s: return None
    try:
        t = s.decode('latin1')
        if all(32 <= ord(c) < 127 for c in t) and len(t) >= 2:
            return t
    except Exception:
        pass
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def dump(va, n, label):
    print()
    print("=== %s ===" % label)
    o = va2off(va)
    for ins in md.disasm(d[o:o + n], va):
        note = ''
        for tok in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').split():
            if tok.startswith('0x'):
                v = int(tok, 16)
                if 0x00870000 <= v <= 0x009F0000:
                    s = cstr(v)
                    if s:
                        note = '   ; "%s"' % s
        print("0x%08X  %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

dump(0x461400, 0x140, "around 0x4614C1 (pre-conv-pump filler?)")
dump(0x467EC0, 0x100, "around 0x467F24")
dump(0x470020, 0x100, "around 0x47007E")
dump(0x45CA40, 0x100, "around 0x45CA9D")
