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
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=True
code=d[tra:tra+trsz]
# find every instruction whose mem operand disp == one of the targets (any base/index)
targets=[int(x,16) for x in sys.argv[1:]]
res={t:[] for t in targets}
for ins in md.disasm(code,tbase):
    for op in ins.operands:
        if op.type==capstone.x86.X86_OP_MEM:
            disp=op.mem.disp & 0xffffffff
            sd=op.mem.disp
            for t in targets:
                if sd==t:
                    res[t].append((ins.address,ins.mnemonic,ins.op_str))
for t in targets:
    print("=== disp 0x%x : %d ===" % (t,len(res[t])))
    for a,m,o in res[t]:
        print("  %08x  %s %s" % (a,m,o))
