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
text=[s for s in secs if s[0]=='.text'][0]
_,tva,tvsz,tra,trsz=text
tbase=0x400000+tva
QM=0x00AACF80
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=True
code=d[tra:tra+trsz]
# offsets of interest relative to qm
targets=[int(x,16) for x in sys.argv[1:]] if len(sys.argv)>1 else [0x358,0x35c,0x755c,0x7560]
abs_targets={QM+t:t for t in targets}
# Also catch: instruction with mem disp == QM (base register loads of qm) -- print context
hits=[]
prev=[]
for ins in md.disasm(code,tbase):
    rec=(ins.address,ins.mnemonic,ins.op_str)
    found=None
    for op in ins.operands:
        if op.type==capstone.x86.X86_OP_IMM and op.imm in abs_targets:
            found=op.imm
        if op.type==capstone.x86.X86_OP_MEM and op.mem.disp in abs_targets and op.mem.base==0:
            found=op.mem.disp
    if found is not None:
        hits.append((found,rec))
for off in abs_targets:
    o=abs_targets[off]
    rel=[h for h in hits if h[0]==off]
    print("=== qm+0x%x  (abs 0x%08x) : %d hits ===" % (o,off,len(rel)))
    for _,(a,m,oo) in rel:
        print("  %08x  %s %s" % (a,m,oo))
