"""Decode DefPos.bin per FUN_0046f9b0 loader logic.
Layout hypothesis:
  u32 magic (==1234)
  u32 count1 ; count1 * 100-byte records  (array at obj+0x334, "DefPos")
  u32 count2 ; count2 * 80-byte records   (array at obj+0x755c)
  u32 count3 ; count3 * 76-byte records   (array at DAT_00ab7820)
"""
import struct, sys, collections
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

def load(p):
    return open(p,'rb').read()

def decode(path, dump_n=6):
    d=load(path)
    print(f"\n######## {path}  ({len(d):,} bytes) ########")
    off=0
    magic=struct.unpack_from('<I',d,off)[0]; off+=4
    print(f"magic = {magic} (0x{magic:x})  expect 1234")
    arrays=[]
    for ai,(stride) in enumerate([100,0x50,0x4c]):
        if off+4>len(d):
            print(f"array {ai}: EOF before count"); break
        cnt=struct.unpack_from('<I',d,off)[0]; off+=4
        need=cnt*stride
        print(f"\n=== array {ai}: count={cnt} stride={stride} (0x{stride:x}) bytes={need:,} fileoff_start=0x{off:x} ===")
        if off+need>len(d):
            print(f"  !! count*stride overruns file ({off+need} > {len(d)}) — layout wrong");
            arrays.append((ai,stride,cnt,off,None)); break
        body=d[off:off+need]
        arrays.append((ai,stride,cnt,off,body))
        # show first few records, interpret leading bytes as ascii + ints/floats
        for r in range(min(dump_n,cnt)):
            rec=body[r*stride:(r+1)*stride]
            # leading printable string?
            s=rec.split(b'\x00')[0]
            sp = s.decode('latin1','replace') if all(32<=c<127 for c in s) and len(s)>0 else ''
            # try to find a name field: many sacred structs lead with a fixed char[N]
            print(f"  [{r}] str0={sp!r}")
            print(f"      hex: {rec.hex()}")
            # dump candidate numeric fields at various offsets
            for fo in range(0, min(stride, 64), 4):
                u=struct.unpack_from('<I',rec,fo)[0]
                f=struct.unpack_from('<f',rec,fo)[0]
                si=struct.unpack_from('<i',rec,fo)[0]
                fl = f"{f:.3f}" if (abs(f)>1e-6 and abs(f)<1e9) else ("0" if f==0 else "~")
                print(f"      +0x{fo:02x}: u32={u:<12} i32={si:<12} f32={fl}")
        off+=need
    print(f"\n  final offset 0x{off:x}, file size 0x{len(d):x}, trailing={len(d)-off} bytes")
    return arrays

for p in [r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\DefPos.bin",
          r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_SERAPHIM\DefPos.bin"]:
    decode(p, dump_n=4)
