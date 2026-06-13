import sys, struct, capstone

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
IMGBASE = 0x00400000

def load():
    d = open(EXE,'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    opt = struct.unpack_from('<H', d, pe+0x14)[0]
    secoff = pe+0x18+opt
    secs=[]
    for i in range(nsec):
        o=secoff+i*0x28
        name=d[o:o+8].rstrip(b'\0').decode('latin1')
        vsz,va,rsz,ptr=struct.unpack_from('<IIII', d, o+8)
        secs.append((name,va,vsz,ptr,rsz))
    return d,secs

def va2off(secs, va):
    rva = va - IMGBASE
    for name,sva,vsz,ptr,rsz in secs:
        if sva <= rva < sva+max(vsz,rsz):
            return ptr + (rva - sva)
    return None

if __name__=='__main__':
    va = int(sys.argv[1],16)
    n  = int(sys.argv[2],0) if len(sys.argv)>2 else 0x80
    d,secs = load()
    off = va2off(secs, va)
    code = d[off:off+n]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(code, va):
        print("%08x  %-22s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
