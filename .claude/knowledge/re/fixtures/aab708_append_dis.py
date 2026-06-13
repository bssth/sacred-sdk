# Read the parser's 0xAAB708 entry-append idiom (writes end-ptr at 0x45C33B).
# Goal: the 0x54-byte entry layout + what links the trigger to a quest action.
import struct, capstone
EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
IMGBASE = 0x00400000
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
opt = struct.unpack_from('<H', d, pe + 0x14)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
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
def cstr(va, maxlen=80):
    o = va2off(va)
    if o is None: return None
    s = d[o:o + maxlen].split(b'\0')[0]
    try:
        t = s.decode('latin1')
        if t and len(t) >= 2 and all(32 <= ord(c) < 127 for c in t): return t
    except Exception: return None
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
# regions around the four end-ptr writes
for lo, hi, lbl in [(0x45C2A0, 0x45C470, "append A (0x45C33B)"),
                    (0x45C640, 0x45C800, "append C/D (0x45C69D/0x45C7DC)")]:
    print("\n======== %s ========" % lbl)
    o = va2off(lo)
    for ins in md.disasm(d[o:o + (hi - lo)], lo):
        note = ''
        for tok in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').split():
            if tok.startswith('0x'):
                v = int(tok, 16)
                if 0x00870000 <= v <= 0x009F0000:
                    s = cstr(v)
                    if s: note = '   ; "%s"' % s
        print("0x%08X  %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
