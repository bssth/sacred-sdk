# Path A: find where the trigger-NAME table 0xAAB708 is APPENDED to.
# The appender writes the end-ptr 0xAAB70C. Learn the 0x54-byte entry layout
# + the registration ABI so we can replicate it for our runtime NPC.
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
text = [s for s in secs if s[0] == '.text'][0]
tname, tva, tvsz, tptr, trsz = text
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

# All refs to 0xAAB708 and 0xAAB70C, classify read vs write, show instruction
for addr in (0xAAB708, 0xAAB70C):
    print("=== refs to 0x%08X ===" % addr)
    pat = struct.pack('<I', addr)
    j = tptr
    while True:
        j = d.find(pat, j, tptr + trsz)
        if j < 0: break
        ref_va = IMGBASE + tva + (j - tptr)
        found = None
        for lead in range(1, 8):
            start = j - lead
            va = IMGBASE + tva + (start - tptr)
            for ins in md.disasm(d[start:start + 16], va):
                if ins.address <= ref_va < ins.address + ins.size:
                    found = ins
                break
            if found: break
        if found:
            w = (found.mnemonic == 'mov' and found.op_str.startswith('dword ptr [0x'))
            print("  0x%08X  %-5s %-8s %s" % (found.address, 'WRITE' if w else '', found.mnemonic, found.op_str))
        j += 1
