import struct
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
# VA of "FULLSCREEN" string = 0xa1d9c0 ; find push imm32 of that VA in .text
target=struct.pack('<I',0xa1d9c0)
i=d.find(target)
while i>=0:
    va=f2v(i)
    if va and 0x401000<=va<0x900000:
        print(f"ref to FULLSCREEN str data@{va:#x} (file {i:#x})")
    i=d.find(target,i+1)
# also "Settings.cfg" VA 0xa1ae30
t2=struct.pack('<I',0xa1ae30)
i=d.find(t2)
while i>=0:
    va=f2v(i)
    if va: print(f"ref to Settings.cfg str @{va:#x}")
    i=d.find(t2,i+1)
