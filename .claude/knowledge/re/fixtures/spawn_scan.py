import struct, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

K=53.66563034057617
KX,KY=2796,2261
WX=(KX+0.5)*K   # ~150070
WY=(KY+0.5)*K   # ~121366
print(f"targets: Kompass=({KX},{KY}) world~=({WX:.1f},{WY:.1f})")

TARGS = {
 "kompass":   (KX,KY,4),
 "world":     (round(WX),round(WY),200),
 "world>>6":  (round(WX)>>6, round(WY)>>6, 4),     # /64
 "world*64":  (round(WX*64), round(WY*64), 4000),  # <<6 stored small? unlikely
 "kompass<<6":(KX<<6, KY<<6, 64),                  # DAT<<6 sector encoding
}

def near(v,t,tol): return abs(v-t)<=tol

def scan(path):
    d=open(path,'rb').read()
    print(f"\n#### {path} ({len(d)} bytes)")
    n=len(d)
    fmts=[('<I',4),('<i',4),('<H',2),('<h',2),('<f',4)]
    for fc,sz in fmts:
        step=1
        for off in range(0, n-8, 1):
            try: vx=struct.unpack_from(fc,d,off)[0]
            except struct.error: continue
            if isinstance(vx,float):
                if vx!=vx or abs(vx)>1e9: continue
                vx_=vx
            else: vx_=vx
            for name,(tx,ty,tol) in TARGS.items():
                if isinstance(vx_,float):
                    if not near(vx_,tx,max(tol,1)): continue
                else:
                    if not near(vx_,tx,tol): continue
                # check a Y nearby (next field at off+sz, or off+4, off+2)
                for dy in (sz,4,2,8,12,16,sz*2):
                    if off+dy+sz>n: continue
                    try: vy=struct.unpack_from(fc,d,off+dy)[0]
                    except struct.error: continue
                    if isinstance(vy,float) and (vy!=vy or abs(vy)>1e9): continue
                    if near(vy,ty,max(tol,1) if isinstance(vy,float) else tol):
                        print(f"  HIT {fc} enc={name} off=0x{off:x} X={vx_} Y={vy} (dy={dy})")

for p in [r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\DefPos.bin",
          r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\StartCode.bin",
          r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\Addon\TYPE_NPC_VAMPIRELADY\DefPos.bin",
          r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\Addon\TYPE_NPC_VAMPIRELADY\StartCode.bin"]:
    try: scan(p)
    except FileNotFoundError: print("missing",p)
