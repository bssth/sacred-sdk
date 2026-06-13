import struct,capstone
p=r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
d=open(p,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]
opt=pe+24; ohsz=struct.unpack_from('<H',d,pe+20)[0]; sect=opt+ohsz
secs=[]
for i in range(nsec):
    o=sect+i*40
    nm=d[o:o+8].rstrip(b'\0').decode('latin1')
    vsz,va,rsz,ra=struct.unpack_from('<IIII',d,o+8); secs.append((nm,va,vsz,ra,rsz))
def v2f(v):
    rva=v-0x400000
    for nm,va,vsz,ra,rsz in secs:
        if va<=rva<va+max(vsz,rsz): return ra+(rva-va)
def f2v(off):
    for nm,va,vsz,ra,rsz in secs:
        if ra<=off<ra+rsz: return 0x400000+va+(off-ra)
# Parse import table: find IAT entry VA for CreateWindowExA / ChangeDisplaySettingsA / GetSystemMetrics
ddir=opt+96
imprva,impsz=struct.unpack_from('<II',d,ddir+8)  # import dir = entry 1
io=v2f(0x400000+imprva)
want={b'CreateWindowExA':None,b'ChangeDisplaySettingsA':None,b'GetSystemMetrics':None,b'ShowWindow':None}
k=io
while True:
    oft,ts,fc,nameRVA,fthunk=struct.unpack_from('<IIIII',d,k)
    if nameRVA==0: break
    thunk=oft if oft else fthunk
    to=v2f(0x400000+thunk); fo=v2f(0x400000+fthunk)
    j=0
    while True:
        ent=struct.unpack_from('<I',d,to+j*4)[0]
        if ent==0: break
        if not (ent & 0x80000000):
            no=v2f(0x400000+ent)
            nm=d[no+2:d.find(b'\0',no+2)]
            if nm in want:
                want[nm]=f2v(fo+j*4)  # VA of IAT slot
        j+=1
    k+=20
print("IAT slots:",{kk.decode():hex(vv) if vv else None for kk,vv in want.items()})
# now scan .text for `call dword ptr [IATslot]` (FF15 <le32>)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for nm,va,vsz,ra,rsz in secs:
    if nm!='.text': continue
    base=0x400000+va
    code=d[ra:ra+rsz]
    for fn,slot in want.items():
        if not slot: continue
        pat=b'\xff\x15'+struct.pack('<I',slot)
        i=code.find(pat)
        while i>=0:
            print(f"{fn.decode():22} called @ {base+i:#x}")
            i=code.find(pat,i+1)
