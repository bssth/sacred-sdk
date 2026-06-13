import sys, collections
sys.path.insert(0, r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools")
from funkcode_disasm import walk_records, disasm_payload
from funkcode_tags import label_for

BIN = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin"
data = open(BIN, "rb").read()
recs = list(walk_records(data))

# Smallest tag=0x03 records that carry a 'res:' DLG_OP — the dialog-text template.
print("=== smallest tag=0x03 (DialogShow_v1) records with res: text ===")
cand = [(o,s,p) for (o,t,s,p) in recs if t == 0x03 and b"res:" in p]
cand.sort(key=lambda x: x[1])
for off, size, payload in cand[:5]:
    print("@0x%06X size=%d  raw=%s" % (off, size, data[off:off+size].hex()))
    for ln in disasm_payload(payload, indent=2, limit=40)[0]:
        print(ln)
    print()

# All dialog-family tags + counts, so we know the full block shape.
print("=== dialog-family record tags (label contains Dialog) ===")
hist = collections.Counter()
for o,t,s,p in recs: hist[t]+=1
for t,c in hist.most_common(60):
    lbl = label_for(t)
    if "ialog" in lbl or t in (0x01,0x03,0x1f,0x56,0x14,0x1a):
        print("  tag=0x%02x [%-16s] x%d" % (t, lbl, c))
