"""Take the live `seen_hashes.csv` produced by text_logger and join it
against our (1) global.res text and (2) recovered hash_names.csv to produce
`seen_named.csv` with as much human-readable context as possible.

Run after playing for a while; flush is every 5 s so the file grows
continuously.
"""
import csv, os, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

SEEN   = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\logs\seen_hashes.csv"
NAMES  = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\hash_names.csv"
GR     = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\us\global.res"
OUT    = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\logs\seen_named.csv"

if not os.path.exists(SEEN):
    print(f"no seen_hashes.csv yet at {SEEN} — play the game first")
    sys.exit(0)

# --- load global.res text by id (id is the hashed value) ---
data = open(GR, "rb").read()
assert data[:4] == b"SZ\x00\x00"
blob_start = struct.unpack_from("<I", data, 8)[0]
slots = []
for i in range(blob_start // 16):
    _, ident, off, _ = struct.unpack_from("<IIII", data, i*16)
    if blob_start <= off < len(data):
        slots.append((ident, off))
id_to_off = {ident: off for ident, off in slots}
slots_by_off = sorted(slots, key=lambda x: x[1])
off_to_idx = {off: i for i, (_, off) in enumerate(slots_by_off)}
def text_for(h):
    if h not in id_to_off: return None
    off = id_to_off[h]
    i = off_to_idx[off]
    nxt = slots_by_off[i+1][1] if i+1 < len(slots_by_off) else len(data) - 4
    raw = data[off+4:nxt+4]
    while len(raw) >= 2 and raw[-2:] == b"\x00\x00":
        raw = raw[:-2]
    return raw.decode("utf-16-le", errors="replace")

# --- load hash_names.csv ---
name_by_hash = {}
if os.path.exists(NAMES):
    with open(NAMES, encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            try:
                name_by_hash[int(row["hash"])] = row["name"]
            except Exception:
                pass

# --- read seen hashes ---
seen = []
with open(SEEN, encoding="utf-8") as f:
    r = csv.DictReader(f)
    for row in r:
        try: seen.append(int(row["hash"]))
        except Exception: pass

uniq = list(dict.fromkeys(seen))  # dedupe, preserve order
print(f"seen file        : {SEEN}")
print(f"  total entries  : {len(seen)}")
print(f"  unique hashes  : {len(uniq)}")
print(f"  in global.res  : {sum(1 for h in uniq if h in id_to_off)}")
print(f"  with name      : {sum(1 for h in uniq if h in name_by_hash)}")

# Write join
with open(OUT, "w", encoding="utf-8", newline="") as f:
    w = csv.writer(f)
    w.writerow(["hash", "name", "text", "in_globalres"])
    for h in uniq:
        nm = name_by_hash.get(h, "")
        t  = text_for(h) or ""
        w.writerow([h, nm, t.replace("\r"," ").replace("\n"," | "),
                    "yes" if h in id_to_off else "no"])
print(f"\nwrote {OUT}")

# Show 20 random samples that have name AND text
import random
random.seed(1)
named = [h for h in uniq if h in name_by_hash and text_for(h)]
print(f"\n--- 20 random samples (named + with text) ---")
for h in random.sample(named, min(20, len(named))):
    nm = name_by_hash[h]
    tx = text_for(h)[:60]
    print(f"  {h:>10}  {nm:<40}  {tx!r}")

# Show 15 unnamed unique hashes (these are interesting RE targets)
print(f"\n--- 15 random unnamed unique hashes (best targets for future RE) ---")
unnamed = [h for h in uniq if h not in name_by_hash and h in id_to_off]
for h in random.sample(unnamed, min(15, len(unnamed))):
    tx = text_for(h)[:60]
    print(f"  {h:>10}  (unknown)                                  {tx!r}")
