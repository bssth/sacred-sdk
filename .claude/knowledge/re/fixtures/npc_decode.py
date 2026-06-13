import sys, struct

BIN = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin"
d = open(BIN,'rb').read()

# field-type -> (name,width or 'cstr')
INT4 = {0x02,0x04,0x0b,0x11,0x14,0x15,0x1c,0x1d,0x1f,0x38,0x49,0x4a,0x53,0x6b,0x6d,0x75,0x86}
INT2 = {0x0a,0x28,0x69,0x93}
INT1 = {0x07,0x08,0x17,0x39,0x45,0x48,0x50,0xe0}
CSTR = {0x01,0x05,0x09,0x16,0x1e,0x2a,0x30,0x32,0x33,0x40,0x41,0x47,0x4d,0x52,0x5f,0x67,0x6f,0x79,0x7c,0x7d,0x8f,0x60}

# CreateNPC switch (FUN_00482510) field-byte -> meaning
CN = {
 1:"name/instance-id(cstr,unique)", 2:"Type(creature class id)", 3:"level(local_63c)",
 4:"posX/Y/sector(coord triple @0xa860/64/68)",0xc:"pos(alt)",0xd:"pos(alt)",0x2a:"pos(alt)",
 5:"name2(acStack_214)", 8:"side=ALLY?(local_6c8=1)",
 9:"link to existing dialog NPC(by name)",10:"link dialog NPC(by id)",
 0x11:"group/team id(pTStack_640)",0x12:"flag|=1(awake/active)",0x13:"flag bit2",
 0x14:"flag&=~..(clear)",0x15:"DefPos rect(0xa880..0xa88c)",0x1a:"local_6e0|=1",
 0x1f:"posX only(0xa860)",0x20:"pos negate",0x23:"6e0|=0x10",0x24:"6e0|=4",0x25:"6e0|=8",
 0x2b:"side: NEUTRAL? (6c8=0,EBX flag8)",0x2e:"side (6c8=0,EBX 0x10)",
 0x2f:"side ENEMY? (EBX flag 0x40)",0x31:"side (EBX 0x40)",
 0x30:"side (EBX 0x20)",0x32:"side (EBX 0x80)",0x33:"DefPos? (0xa880/0xa884)",
 0x36:"scale/float(local_698)",0x39:"clear bVar7",0x65:"clear bVar8",
 0x42:"side (EBX 0x400)",0x43:"EBX 0x800",0x44:"EBX 0x1000",0x45:"EBX|=0x2000",
 0x46:"EBX 0x4000",0x4b:"bVar8=1(EBP)",0x37:"bVar7=1",0x4c:"EBX 0x8000",
 0x50:"EBX|=0x10000",0x54:"local_6a0(0xa860)",0x60:"name from DAT(template)",
 99:"name3(acStack_114)+local_6ac",100:"6e0|=0x20",0x67:"local_694(loot/drop)",
 0x6b:"pTStack_6e4(stationary/sleep)",0x70:"6e0|=0x400",0x71:"6e0|=0x800",
 0x72:"6e0|=0x1000",0x73:"local_650(0xa880)",0x74:"EBX|=0x8000000",
 0x75:"local_690(0xa880)",0x79:"0xa860/0xa864 floatpair",0x7b:"local_6dc[3]=1",
 0x85:"EBX|=0x2000000",0x8a:"EBX|=0x10000000",0xe:"side(6c8=0)",
 0x8d:"local_6d8[7]=1(special spawn)",0x90:"local_688(0xa860 anim?)",
 0x9d:"pvStack_574(0xa880)",0xa1:"local_6dc[2]=1(0x200000 invuln?)",
}

def parse_payload(p):
    """p = bytes after the 3-byte record header (incl flags byte at p[0])"""
    out=[]
    off=1  # skip flags
    while off < len(p):
        t=p[off]; off+=1
        if t in CSTR:
            e=p.find(b'\0',off)
            if e<0: e=len(p)
            out.append((t,'str',p[off:e].decode('latin1','replace'))); off=e+1
        elif t in INT4:
            if off+4>len(p): break
            v=struct.unpack_from('<i',p,off)[0]; off+=4
            out.append((t,'i32',v))
        elif t in INT2:
            if off+2>len(p): break
            v=struct.unpack_from('<H',p,off)[0]; off+=2
            out.append((t,'u16',v))
        elif t in INT1:
            if off+1>len(p): break
            out.append((t,'u8',p[off])); off+=1
        else:
            out.append((t,'?stop',p[off:off+16].hex())); break
    return out

recs=[]
off=0
while off+3<=len(d):
    tag=d[off]; size=(d[off+1]<<8)|d[off+2]
    if size<3 or off+size>len(d): break
    recs.append((off,tag,size,d[off+3:off+size]))
    off+=size

cn=[r for r in recs if r[1]==0x01]
print(f"total records={len(recs)}  CreateNPC(0x01)={len(cn)}")
n=int(sys.argv[1]) if len(sys.argv)>1 else 8
shown=0
for (o,tag,size,pl) in cn:
    flds=parse_payload(pl)
    # heuristic: only show ones that parsed cleanly and have a Type-ish field
    print(f"\n--- rec @0x{o:06x} size={size} flags=0x{pl[0]:02x} rawpl={pl[:40].hex()}")
    for (t,k,v) in flds:
        lbl = CN.get(t, f"(field 0x{t:02x})")
        print(f"   type=0x{t:02x} {k:5} = {v!r:30}  <- {lbl}")
    shown+=1
    if shown>=n: break
