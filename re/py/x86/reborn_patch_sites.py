"""reborn_patch_sites.py - list every in-place patch ReBorn applied to vanilla code (old VA -> new VA, disasm old/new),
plus the HD layout globals table (0xa1efd0..0xa1f0fc) and the .rsrc-tail init code that fills it.
Needs sdk/re/reborn_symbols.json from reloc_build.py (delta map). Output: sdk/re/reborn_patch_sites.txt
Usage: python reborn_patch_sites.py   (paths hard-coded to the game folder)
"""
import re, struct, json, bisect, capstone, sys, os
CS=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
S=os.path.dirname(os.path.abspath(__file__))
G="E:/SteamLibrary/steamapps/common/Sacred Gold/"
dec=open(G+"sdk/Sacred_decrypted.exe","rb").read(); reb=open(G+"SacredReborn.exe","rb").read()
B=0x400000; NT=(0x401000,0x401000+0x48cf82)
RSRC_VA=0x1430000; RSRC_RAW=0x622000
def rva(v): return v-RSRC_VA+RSRC_RAW if v>=RSRC_VA else v-B
runs=json.load(open(G+"sdk/re/reborn_symbols.json"))["runs"]; rstarts=[a+d for a,b,d in runs]
def new2old(nv):
    i=bisect.bisect_right(rstarts,nv)-1; return nv-runs[i][2] if i>=0 else None
def dis(data,va,n=10):
    out=[]
    for ins in CS.disasm(data,va):
        out.append(f"{ins.address:#x}: {ins.mnemonic} {ins.op_str}")
        if len(out)>=n: break
    return "; ".join(out)
VA_LO,VA_HI=0x400000,0x1a00000
def mask_of(b):
    m=bytearray([1]*len(b)); i=0
    while i<len(b):
        if i+4<=len(b) and VA_LO<=struct.unpack_from("<I",b,i)[0]<VA_HI: m[i:i+4]=b"\0\0\0\0"; i+=4; continue
        if b[i] in (0xE8,0xE9) and i+5<=len(b): m[i+1:i+5]=b"\0\0\0\0"; i+=5; continue
        if b[i]==0x0F and i+6<=len(b) and 0x80<=b[i+1]<=0x8F: m[i+2:i+6]=b"\0\0\0\0"; i+=6; continue
        i+=1
    return m
clusters=[]
for a,b_,d in runs:
    if b_-a<0x400: continue
    lo=a+0x40; hi=b_-0x40
    o=dec[lo-B:hi-B]; n=reb[lo+d-B:hi+d-B]; mk=mask_of(o); cur=None
    for k in range(len(o)):
        if mk[k] and o[k]!=n[k]:
            if cur and k-cur[1]<=24: cur[1]=k
            else:
                if cur: clusters.append(cur)
                cur=[k,k,lo,d]
    if cur: clusters.append(cur)
sig=[]
for s,e,lo,d in clusters:
    L=e-s+1; ov=lo+s; nv=ov+d; nb=reb[nv-B:nv-B+L+2]
    if L>=5 or nb.count(0x90)>=3: sig.append((ov,nv,L))
with open(G+"sdk/re/reborn_patch_sites.txt","w") as f:
    for ov,nv,L in sig:
        ob=dec[ov-B-6:ov-B+L+10]; nb=reb[nv-B-6:nv-B+L+10]
        f.write(f"old {ov:#x} new {nv:#x} len={L}\n   OLD: {dis(ob,ov-6,8)}\n   NEW: {dis(nb,nv-6,8)}\n")
print("clusters:",len(sig),"-> reborn_clusters.txt")
# new globals referenced anywhere in reborn .text+rdata cave that are NOT referenced in old
glob={}
for i in range(NT[0]-B,NT[1]-B-4):
    v=struct.unpack_from("<I",reb,i)[0]
    if 0xa1efd0<=v<0xa1f100: glob[v]=glob.get(v,0)+1
cave=reb[0x8e4700-B:0x8e4960-B]
for i in range(len(cave)-4):
    v=struct.unpack_from("<I",cave,i)[0]
    if 0xa1efd0<=v<0xa1f100: glob[v]=glob.get(v,0)+1
print("HD layout globals (0xa1efd0-0xa1f100) ref counts:",{hex(k):c for k,c in sorted(glob.items())})
# refs into rsrc cave main code from anywhere (validated E8/E9/68/FF15/FF25 + data pointers)
tgt=(0x16c0000,0x16c4400)
refs=[]
for i in range(0,len(reb)-5):
    v=struct.unpack_from("<I",reb,i+1)[0] if reb[i] in (0xE8,0xE9,0x68) else None
    if reb[i] in (0xE8,0xE9):
        t=(i+B if i<0x622000 else i-RSRC_RAW+RSRC_VA)+5+struct.unpack_from("<i",reb,i+1)[0]
        if tgt[0]<=t<tgt[1]: refs.append((i,"rel",t))
    elif reb[i]==0x68 and v and tgt[0]<=v<tgt[1]: refs.append((i,"push",v))
    v2=struct.unpack_from("<I",reb,i)[0]
    if tgt[0]<=v2<tgt[1] and i%4==0 and i>=0x48e000 and i<0x622000: refs.append((i,"dataptr",v2))
print("refs into rsrc main cave:",len(refs))
for i,k,t in refs[:40]:
    va=i+B if i<0x622000 else i-RSRC_RAW+RSRC_VA
    print(f"   {va:#x} [{k}] -> {t:#x}  old~{new2old(va) and hex(new2old(va)) if va<NT[1] else ''}   {dis(reb[i:i+16],va,3) if k!='dataptr' else ''}")
# functions in rsrc main cave: prologues
code=reb[rva(tgt[0]):rva(tgt[1])]
fns=[tgt[0]+m.start() for m in re.finditer(rb"\x55\x8b\xec",code)]
print("rsrc cave function prologues (55 8b ec):",len(fns),[hex(x) for x in fns[:40]])
# IAT calls in rsrc main cave
apis={}
for m in re.finditer(rb"\xff\x15",code):
    v=struct.unpack_from("<I",code,m.start()+2)[0]
    if 0x88e000<=v<0x88e800: apis[v]=apis.get(v,0)+1
# import names
def imports(d):
    e=struct.unpack_from('<I',d,0x3c)[0]; o=e+24; nsec=struct.unpack_from('<H',d,e+6)[0]; optsz=struct.unpack_from('<H',d,e+20)[0]
    secs=[struct.unpack_from('<8sIIII',d,o+optsz+40*i) for i in range(nsec)]
    def r2o(r):
        for n,vs,va,rs,ro in secs:
            if va<=r<va+max(vs,rs): return r-va+ro
    irva=struct.unpack_from('<I',d,o+96+8)[0]; p=r2o(irva); names={}
    while True:
        ilt,ts,fc,name,iat=struct.unpack_from('<IIIII',d,p)
        if name==0: break
        dll=d[r2o(name):].split(b'\0')[0].decode(); q=r2o(ilt); slot=iat
        while True:
            v=struct.unpack_from('<I',d,q)[0]
            if not v: break
            nm=f"ord{v&0xffff}" if v&0x80000000 else d[r2o(v)+2:].split(b'\0')[0].decode()
            names[0x400000+slot]=dll+"!"+nm; q+=4; slot+=4
        p+=20
    return names
names=imports(reb)
print("IAT calls from rsrc main cave:",{names.get(v,hex(v)):c for v,c in apis.items()})
# string refs from rsrc main cave
strs=set()
for i in range(len(code)-4):
    v=struct.unpack_from("<I",code,i)[0]
    if 0x88e000<=v<0x1030000 or 0x16c0000<=v<0x16dd000:
        s=reb[rva(v):rva(v)+48].split(b"\0")[0]
        if len(s)>=4 and all(32<=c<127 for c in s): strs.add(s.decode())
        w=reb[rva(v):rva(v)+96]
        try:
            ws=w.decode("utf-16le").split("\0")[0]
            if len(ws)>=4 and all(32<=ord(c)<127 for c in ws): strs.add("W:"+ws)
        except: pass
print("string refs from rsrc main cave:",sorted(strs)[:80])
# calls from rsrc cave into .text
back={}
for m in re.finditer(rb"\xe8",code):
    p=m.start(); va=tgt[0]+p; t=va+5+struct.unpack_from("<i",code,p+1)[0]
    if NT[0]<=t<NT[1]: back[t]=back.get(t,0)+1
print("calls into .text:",{f"{t:#x}(old {new2old(t):#x})":c for t,c in sorted(back.items())})

# ---- HD init code disassembly (globals writer) ----
lo,hi=0x16be000,0x16dd000; blob=reb[rva(lo):rva(hi)]
print("== FF15 IAT calls in rsrc tail:")
for m in re.finditer(rb"\xff\x15",blob):
    v=struct.unpack_from("<I",blob,m.start()+2)[0]
    if v in names: print(f"   {lo+m.start():#x}: call [{names[v]}]")
print("== E8 calls into .text from rsrc tail (validated by following disasm):")
for m in re.finditer(rb"\xe8",blob):
    p=m.start(); va=lo+p; t=va+5+struct.unpack_from("<i",blob,p+1)[0]
    if 0x401000<=t<0x88df82:
        nxt=list(CS.disasm(blob[p:p+16],va))
        if len(nxt)>=2 and nxt[0].mnemonic=="call": print(f"   {va:#x}: call {t:#x} (old {new2old(t):#x})  then: {nxt[1].mnemonic} {nxt[1].op_str}")
print("== disasm around global-writer 0x16c8500-0x16c8620")
for ins in CS.disasm(reb[rva(0x16c8500):rva(0x16c8620)],0x16c8500): print(f"   {ins.address:#x}: {ins.mnemonic} {ins.op_str}")
print("== disasm around 0x16c9540-0x16c9640")
for ins in CS.disasm(reb[rva(0x16c9540):rva(0x16c9640)],0x16c9540): print(f"   {ins.address:#x}: {ins.mnemonic} {ins.op_str}")
# any absolute refs (imm32) from rsrc tail to .data 0xa1efd0-0xa1f100 -> list unique with addresses (writers)
print("== rsrc-tail refs to HD globals:")
for i in range(len(blob)-4):
    v=struct.unpack_from("<I",blob,i)[0]
    if 0xa1efd0<=v<0xa1f100 or v in (0xa1ad20,0xa1ad24):
        ins=list(CS.disasm(blob[max(0,i-3):i+8],lo+max(0,i-3)))
        print(f"   {lo+i:#x} -> {v:#x}   {'; '.join(f'{x.mnemonic} {x.op_str}' for x in ins[:2])}")
