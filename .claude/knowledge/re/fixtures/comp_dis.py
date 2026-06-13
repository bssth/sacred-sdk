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

D,SECS = load()

def va2off(va):
    rva = va - IMGBASE
    for name,sva,vsz,ptr,rsz in SECS:
        if sva <= rva < sva+max(vsz,rsz):
            return ptr + (rva - sva)
    return None

def off2va(off):
    for name,sva,vsz,ptr,rsz in SECS:
        if ptr <= off < ptr+rsz:
            return IMGBASE + sva + (off - ptr)
    return None

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True

def dis(va, n=0x80, stop_ret=False):
    off = va2off(va)
    code = D[off:off+n]
    out=[]
    for ins in MD.disasm(code, va):
        out.append(ins)
        if stop_ret and ins.mnemonic in ('ret','retn') :
            break
    return out

def show(va, n=0x80, stop_ret=False):
    for ins in dis(va,n,stop_ret):
        print("%08x  %-20s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))

def callers(target, scan_secs=('.text',)):
    """find E8 rel32 calls to target across code sections"""
    res=[]
    for name,sva,vsz,ptr,rsz in SECS:
        if name not in scan_secs: continue
        base=ptr; size=rsz
        seg=D[base:base+size]
        i=0
        while True:
            j=seg.find(b'\xe8', i)
            if j<0: break
            rel=struct.unpack_from('<i', seg, j+1)[0]
            src_va = off2va(base+j)
            if src_va is None:
                i=j+1; continue
            dst = src_va+5+rel
            if dst==target:
                res.append(src_va)
            i=j+1
    return res

def refs_to_const(val, scan_secs=('.text',)):
    """find places that load immediate == val (push/mov/cmp)"""
    needle=struct.pack('<I', val & 0xffffffff)
    res=[]
    for name,sva,vsz,ptr,rsz in SECS:
        if name not in scan_secs: continue
        seg=D[ptr:ptr+rsz]
        i=0
        while True:
            j=seg.find(needle, i)
            if j<0: break
            res.append(off2va(ptr+j))
            i=j+1
    return res

if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='dis':
        show(int(sys.argv[2],16), int(sys.argv[3],0) if len(sys.argv)>3 else 0x80)
    elif cmd=='disr':
        show(int(sys.argv[2],16), int(sys.argv[3],0) if len(sys.argv)>3 else 0x400, True)
    elif cmd=='callers':
        for v in callers(int(sys.argv[2],16)):
            print("%08x"%v)
    elif cmd=='const':
        for v in refs_to_const(int(sys.argv[2],16)):
            print("%08x"%v)
