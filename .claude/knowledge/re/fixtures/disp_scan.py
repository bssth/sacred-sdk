import struct,sys,capstone
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
def f2v(f): return tbase+(f-tra)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)

# Find every 4-byte little-endian occurrence of target disp where the preceding
# byte is a ModRM with mod==10 (disp32) and rm != 100 (no SIB) or with SIB.
targets=[int(x,16) for x in sys.argv[1:]]
for t in targets:
    pat=struct.pack('<I',t)
    print("=== disp32 == 0x%x ===" % t)
    i=tra
    end=tra+trsz
    cnt=0
    while True:
        j=d.find(pat,i,end)
        if j==-1: break
        i=j+1
        # candidate: byte at j-1 = modrm or sib; require mod bits = 10 within a few bytes back
        # disassemble a window ending here for context
        win_start=max(tra,j-16)
        best=None
        for s in range(j-8,j+1):
            if s<tra: continue
            try:
                for ins in md.disasm(d[s:j+8], f2v(s)):
                    if ins.address <= f2v(j) < ins.address+ins.size:
                        # does this instruction's bytes contain the disp at j?
                        if (f2v(j) - ins.address) + 4 <= ins.size:
                            best=(ins.address,ins.mnemonic,ins.op_str,ins.bytes.hex())
                        break
            except Exception:
                pass
            if best: break
        if best and ('0x%x'%t in best[2] or struct.pack('<I',t).hex() in best[3]):
            print("  %08x  %-26s %s %s" % (best[0],best[3],best[1],best[2]))
            cnt+=1
    print("  (%d valid)" % cnt)
