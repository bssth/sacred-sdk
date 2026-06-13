import struct, sys
sys.stdout.reconfigure(encoding='utf-8')

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
BASE = 0x400000
with open(EXE,'rb') as f:
    data = f.read()

def va2off(va): return va - BASE

TBL = 0x00964268
STRIDE = 0x38
N = 0x18
print("=== raw dump of table region @0x%08X ===" % TBL)
o = va2off(TBL)
# dump 0x18 entries of 0x38 bytes
for i in range(N):
    raw = data[o+i*STRIDE : o+(i+1)*STRIDE]
    hexs = ' '.join('%02X'%b for b in raw)
    asci = ''.join(chr(b) if 32<=b<127 else '.' for b in raw)
    print("[%2d] @%08X" % (i, TBL+i*STRIDE))
    for k in range(0, STRIDE, 16):
        h = ' '.join('%02X'%b for b in raw[k:k+16])
        a = ''.join(chr(b) if 32<=b<127 else '.' for b in raw[k:k+16])
        print("    +%02X  %-48s  %s" % (k, h, a))
