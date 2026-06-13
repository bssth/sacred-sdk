import struct, re, glob, os

GAME = r'E:\SteamLibrary\steamapps\common\Sacred Gold'

def parse_globalres(path):
    b = open(path, 'rb').read()
    blob_start = struct.unpack('<I', b[8:12])[0]
    n = blob_start // 16
    d0=[];ident=[];off=[]
    for k in range(n):
        a,bb,c,_ = struct.unpack('<IIII', b[k*16:k*16+16])
        d0.append(a);ident.append(bb);off.append(c)
    last = struct.unpack('<I', b[blob_start:blob_start+4])[0]
    rows=[]
    for k in range(n):
        tu = d0[k+1] if k<n-1 else last
        raw = b[off[k]+4: off[k]+4+tu]
        try:
            s = raw.decode('utf-16-le', 'replace')
        except Exception:
            s = ''
        rows.append((ident[k], s))
    return rows

# locate a global.res (prefer vanilla US)
cands = [
    os.path.join(GAME,'scripts','US','global.res'),
    os.path.join(GAME,'scripts','us','global.res'),
]
cands += glob.glob(os.path.join(GAME,'scripts','*','global.res'))
path = next((c for c in cands if os.path.isfile(c)), None)
print('global.res =', path)
rows = parse_globalres(path)
print('slots =', len(rows))

KEYS = re.compile(r'quest|solved|complet|accompl|auftrag|erledig|gelöst|geloest|abgeschloss|erfüllt', re.I)
hits = [(i, t) for (i, t) in rows if t and KEYS.search(t)]
print('candidate strings (%d):' % len(hits))
for ident, t in hits[:60]:
    tt = t.replace('\x00',' ').strip()
    if len(tt) > 80: tt = tt[:80] + '...'
    print('  ident=0x%08x  %r' % (ident, tt))
