import struct,capstone,sys
p=r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
d=open(p,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]
opt=pe+24; ohsz=struct.unpack_from('<H',d,pe+20)[0]; sect=opt+ohsz
secs=[]
for i in range(nsec):
    o=sect+i*40
    nmn=d[o:o+8].rstrip(b'\0').decode('latin1')
    vsz,va,rsz,ra=struct.unpack_from('<IIII',d,o+8); secs.append((nmn,va,vsz,ra,rsz))
def v2f(v):
    rva=v-0x400000
    for nm,va,vsz,ra,rsz in secs:
        if va<=rva<va+max(vsz,rsz): return ra+(rva-va)
    return None
def f2v(f):
    for nm,va,vsz,ra,rsz in secs:
        if ra<=f<ra+rsz: return 0x400000+va+(f-ra)
    return None
text=[s for s in secs if s[0]=='.text'][0]
_,tva,tvsz,tra,trsz=text
tbase=0x400000+tva
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=True

def find_str(s):
    b=s.encode() if isinstance(s,str) else s
    out=[]
    i=d.find(b)
    while i!=-1:
        out.append((i,f2v(i)))
        i=d.find(b,i+1)
    return out

def xref_imm(val):
    # find instructions referencing immediate val (e.g. push addr / mov reg,addr / lea)
    out=[]
    code=d[tra:tra+trsz]
    for ins in md.disasm(code,tbase):
        for op in ins.operands:
            if op.type==capstone.x86.X86_OP_IMM and op.imm==val:
                out.append((ins.address,ins.mnemonic,ins.op_str)); break
            if op.type==capstone.x86.X86_OP_MEM and op.mem.disp==val and op.mem.base==0 and op.mem.index==0:
                out.append((ins.address,ins.mnemonic,ins.op_str)); break
    return out

def xref_call(target):
    out=[]
    code=d[tra:tra+trsz]
    for ins in md.disasm(code,tbase):
        if ins.mnemonic=='call':
            for op in ins.operands:
                if op.type==capstone.x86.X86_OP_IMM and op.imm==target:
                    out.append(ins.address)
    return out

if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='str':
        for s in sys.argv[2:]:
            print("== %r ==" % s)
            for off,va in find_str(s):
                print("  off=0x%x va=0x%x" % (off, va or -1))
    elif cmd=='imm':
        for a in sys.argv[2:]:
            v=int(a,16)
            print("== imm/disp 0x%x ==" % v)
            for addr,m,o in xref_imm(v):
                print("  %08x  %s %s" % (addr,m,o))
    elif cmd=='call':
        for a in sys.argv[2:]:
            t=int(a,16)
            print("== callers of 0x%x ==" % t)
            for addr in xref_call(t):
                print("  %08x" % addr)
