import struct
d = open(r'E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe', 'rb').read()
e = struct.unpack('<I', d[0x3c:0x40])[0]
nsec = struct.unpack('<H', d[e+6:e+8])[0]
opt = struct.unpack('<H', d[e+0x14:e+0x16])[0]
sec = e + 0x18 + opt
ib = struct.unpack('<I', d[e+0x34:e+0x38])[0]
secs = []
for i in range(nsec):
    o = sec + i*40
    vsz, va, rsz, rptr = struct.unpack('<IIII', d[o+8:o+24])
    secs.append((va, vsz, rsz, rptr))

def v2f(va):
    r = va - ib
    for va0, vsz, rsz, rptr in secs:
        if va0 <= r < va0 + max(vsz, rsz):
            return rptr + (r - va0)
    return None

start = v2f(0x004a5980)
buf = d[start:start+0x1200]
for i in range(len(buf)-5):
    if buf[i] == 0xE8:
        tgt = (0x004a5980 + i + 5 + struct.unpack('<i', buf[i+1:i+5])[0]) & 0xffffffff
        if tgt == 0x00603e30:
            ctx = buf[max(0,i-16):i]
            print('call getData@0x%08x' % (0x004a5980+i))
            print('  preceding bytes:', ' '.join('%02x' % x for x in ctx))
            # look for mov ecx, [imm32]  = 8B 0D xx xx xx xx   or  mov ecx, imm32 = B9 xx xx xx xx
            for j in range(i-16, i):
                if buf[j] == 0x8B and buf[j+1] == 0x0D:
                    g = struct.unpack('<I', buf[j+2:j+6])[0]
                    print('  mov ecx,[0x%08x]  (om global ptr)' % g)
                if buf[j] == 0xB9:
                    g = struct.unpack('<I', buf[j+1:j+5])[0]
                    print('  mov ecx, 0x%08x  (om immediate)' % g)
                if buf[j] == 0xA1:
                    g = struct.unpack('<I', buf[j+1:j+5])[0]
                    print('  mov eax,[0x%08x] (nearby global)' % g)
            break
