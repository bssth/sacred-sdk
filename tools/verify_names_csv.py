"""Cross-check community names.csv vs global.res by trying every plausible
key transform: raw int, str(int), "ID_<int>", "RES_<int>", hex etc."""
import csv, struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sacred_hash import sacred_hash
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

GR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\us\global.res"
data = open(GR, "rb").read()
text_blob_start = struct.unpack_from("<I", data, 8)[0]
slots = []
for i in range(text_blob_start // 16):
    _, ident, off, _ = struct.unpack_from("<IIII", data, i*16)
    if text_blob_start <= off < len(data):
        slots.append((ident, off))
id_to_off = {ident: off for ident, off in slots}
slots_by_off = sorted(slots, key=lambda x: x[1])
off_to_idx = {off: i for i, (_, off) in enumerate(slots_by_off)}

def text_for(h):
    if h not in id_to_off: return None
    off = id_to_off[h]
    i = off_to_idx[off]
    nxt = slots_by_off[i+1][1] if i+1 < len(slots_by_off) else len(data) - 4
    raw = data[off+4 : nxt+4]
    while len(raw) >= 2 and raw[-2:] == b"\x00\x00":
        raw = raw[:-2]
    return raw.decode("utf-16-le", errors="replace").strip()

# Transforms to try
def transforms(n):
    s = str(n)
    return [
        ("raw_id",       n),
        ("hash:str",     sacred_hash(s)),
        ("hash:id_",     sacred_hash(f"ID_{n}")),
        ("hash:res_",    sacred_hash(f"RES_{n}")),
        ("hash:res:",    sacred_hash(f"res:{n}")),
        ("hash:hex",     sacred_hash(f"{n:X}")),
        ("hash:hex0x",   sacred_hash(f"0x{n:X}")),
        ("hash:NPC_",    sacred_hash(f"NPC_{n}")),
        ("hash:NAME_",   sacred_hash(f"NAME_{n}")),
        ("hash:ID",      sacred_hash(f"ID{n}")),
        ("hash:Name_",   sacred_hash(f"Name_{n}")),
        ("hash:str_pad4",sacred_hash(f"{n:04d}")),
        ("hash:str_pad5",sacred_hash(f"{n:05d}")),
    ]

CSV = r"C:\Users\bssth\Downloads\sacred_modding - names.csv"
counts = {}
samples = {}
with open(CSV, encoding="utf-8") as f:
    reader = csv.DictReader(f)
    for row in reader:
        rid = int(row["ID"])
        name = row["Name"].strip()
        for tag, candidate in transforms(rid):
            actual = text_for(candidate)
            if actual and (actual.lower() == name.lower()):
                counts[tag] = counts.get(tag, 0) + 1
                if tag not in samples:
                    samples[tag] = (rid, name, candidate)

print("which transform matched community names.csv against global.res:")
if not counts:
    print("  no transform matched any of the 823 names")
else:
    for tag, n in sorted(counts.items(), key=lambda x: -x[1]):
        s = samples[tag]
        print(f"  {tag:<15} hits={n:<4} sample: id={s[0]} name={s[1]!r} -> key={s[2]}")
