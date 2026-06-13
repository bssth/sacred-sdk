import struct
from capstone import *
p=r'E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe'
d=open(p,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]
ohsz=struct.unpack_from('<H',d,pe+20)[0]
sect=pe+24+ohsz
secs=[]
for i in range(nsec):
    o=sect+i*40
    nm=d[o:o+8].rstrip(b'\0').decode('latin1')
    vsz,va,rsz,ra=struct.unpack_from('<IIII',d,o+8)
    secs.append((nm,va,vsz,ra,rsz))
def v2f(v):
    rva=v-0x400000
    for nm,va,vsz,ra,rsz in secs:
        if va<=rva<va+max(vsz,rsz): return ra+(rva-va)
    return None
md=Cs(CS_ARCH_X86,CS_MODE_32)
md.detail=True
def dis(va,n,label):
    print('---',label,'---')
    f=v2f(va)
    for ins in md.disasm(d[f:f+n],va):
        print('%x  %s  %s'%(ins.address,ins.mnemonic,ins.op_str))

dis(0x6224b0,0x80,'FUN_006224b0 (pos builder)')

txt=[s for s in secs if s[0]=='.text'][0]
nm,va,vsz,ra,rsz=txt
seg=d[ra:ra+rsz]
base=0x400000+va

for tgt,lbl in [(0x00AD3560,'cWorld@AD3560'),(0x00AD5C40,'cObjMgr@AD5C40')]:
    print('=== refs to %s %#x (writes/mov ecx) ==='%(lbl,tgt))
    tb=struct.pack('<I',tgt)
    i=0
    seen=0
    while seen<60:
        j=seg.find(tb,i)
        if j<0: break
        i=j+1
        # decode the instruction containing the imm (try a few start offsets)
        for back in range(1,8):
            st=j-back
            if st<0: continue
            try:
                ins=next(md.disasm(seg[st:st+12],base+st))
            except StopIteration:
                continue
            if ins.address<=base+j<ins.address+ins.size and ins.size>back:
                m=ins.mnemonic
                if m in ('mov','cmp','test','lea','push') and ('%x'%tgt in ins.op_str or ('0x%x'%tgt in ins.op_str)):
                    print('%x  %s  %s'%(ins.address,m,ins.op_str))
                    seen+=1
                break
