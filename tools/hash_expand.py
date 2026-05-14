"""Brute-force fill in missing resource names via template expansion.

Speed trick: the hash is left-fold associative — hash(A + B) depends on
hash(A) and B alone. So for every template "PREFIX<N>SUFFIX" we compute
hash(PREFIX), then for each N step through "0".."99999" incrementally,
then fold SUFFIX once per N. That eliminates redundant prefix work and
makes the whole sweep tractable in pure Python.
"""
import os, sys, struct, itertools
sys.path.insert(0, os.path.dirname(__file__))
from sacred_hash import sacred_hash, MOD, MUL
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

# --- incremental hash primitive: extend(h, s) ---
def extend(h, s):
    """Append chars of s to running hash h (faithful 32-bit semantics)."""
    for c in s:
        oc = ord(c)
        if 0x61 <= oc <= 0x7A: oc -= 0x20
        prod = (h * MUL) & 0xFFFFFFFF
        ss = (oc + prod) & 0xFFFFFFFF
        si = ss - 0x100000000 if ss >= 0x80000000 else ss
        if si >= 0: r =  si % MOD
        else:       r = -((-si) % MOD)
        h = r & 0xFFFFFFFF
    return h

def final(h):
    return h & 0x7FFFFFFF

# --- load global.res ---
GR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\us\global.res"
data = open(GR, "rb").read()
text_blob_start = struct.unpack_from("<I", data, 8)[0]
n_slots = text_blob_start // 16
slots = []
for i in range(n_slots):
    _, ident, off, _ = struct.unpack_from("<IIII", data, i*16)
    if text_blob_start <= off < len(data):
        slots.append((ident, off))
id_to_off = {ident: off for ident, off in slots}
target_ids = {i for i in id_to_off if i > 1_000_000}
print(f"target hashes to crack: {len(target_ids):,}", flush=True)

slots_by_off = sorted(slots, key=lambda x: x[1])
off_to_idx = {off: i for i, (_, off) in enumerate(slots_by_off)}
def text_for(h):
    off = id_to_off[h]
    i = off_to_idx[off]
    nxt = slots_by_off[i+1][1] if i+1 < len(slots_by_off) else len(data) - 4
    raw = data[off+4 : nxt+4]
    while len(raw) >= 2 and raw[-2:] == b"\x00\x00":
        raw = raw[:-2]
    return raw.decode("utf-16-le", errors="replace")

found = {}

def try_template(prefix, n_range, suffixes):
    """Try prefix + str(n) + suffix for n in range, every suffix.
    Uses incremental hashing: hash(prefix) computed once."""
    h_pre = extend(0, prefix)
    for n in n_range:
        h_n = extend(h_pre, str(n))
        for suf in suffixes:
            h_full = final(extend(h_n, suf))
            if h_full in target_ids and h_full not in found:
                found[h_full] = prefix + str(n) + suf

# --- Pattern A: DQ<N>_<VERB>_NPC_ZIEL<M>[_OFFEN/_SIEG] ---
verbs = ["TOETE", "BRINGE", "ESKORTIERE", "BEFREIE", "HOLE", "FINDE",
         "VERTEIDIGE", "BESUCHE", "JAGE", "GIB", "SPRICH", "FUEHRE",
         "BEGLEITE", "BESCHUETZE", "ZERSTOERE", "GEWINNE", "RETTE"]
for n in range(1, 80):
    for verb in verbs:
        # inner: ZIEL<M> for M in 0..120
        h_pre = extend(0, f"DQ{n}_{verb}_NPC_ZIEL")
        for m in range(0, 120):
            h_m = extend(h_pre, str(m))
            for suf in ("", "_OFFEN", "_SIEG", "_HEADER", "_LOG"):
                h = final(extend(h_m, suf))
                if h in target_ids and h not in found:
                    found[h] = f"DQ{n}_{verb}_NPC_ZIEL{m}{suf}"
print(f"after DQ_ZIEL: {len(found):,}", flush=True)

# --- Pattern B: DQ_<N>_LOG_<KIND>, DQ_<N>_<KIND>, RB/NQ variants ---
log_kinds = ["OFFEN", "SIEG", "ZIEL", "HEADER", "TITLE", "INFO", "INTRO",
             "TEXT", "PRE", "POST", "ABBRUCH", "FAIL", "WARN", "DIALOG",
             "PRENPC", "QOFFEN", "QSIEG", "NPC_ZIEL"]
suffixes_b = []
for k in log_kinds:
    suffixes_b.extend([f"_LOG_{k}", f"_{k}"])
for fam in ("DQ", "RB", "NQ", "Q"):
    try_template(f"{fam}_", range(0, 30000), suffixes_b)
    print(f"  ...{fam}_<N>: {len(found):,}", flush=True)

# --- Pattern C: NQ_<REGION><N>_... ---
regions = ["UW","OW","DW","RB","BLDH","OWBL","OWBM","OWHE","OWWB",
           "DWBM","DWHE","C","B","Q","X","Y","Z","VW","PS","SF","WD"]
for reg in regions:
    try_template(f"NQ_{reg}", range(0, 30000), suffixes_b)
    print(f"  ...NQ_{reg}<N>: {len(found):,}", flush=True)

# --- Pattern D: ITEM_*, RAR_*, HERO_*, etc. — sweep with verb suffixes ---
generic_prefixes = ["ITEM_", "RAR_", "HERO_", "MENU_", "ARMOR_", "WEAPON_",
                    "POTION_", "SPELL_", "RUNE_", "QUEST_", "TEXT_",
                    "MSG_", "DIALOG_", "BTN_"]
generic_suffixes = ["NAME","DESC","TITLE","DESCRIPTION","TEXT","SHORT","LONG"]
for pre in generic_prefixes:
    h_pre = extend(0, pre)
    # try just plain suffixes
    for suf in generic_suffixes:
        h = final(extend(h_pre, suf))
        if h in target_ids and h not in found:
            found[h] = pre + suf

# --- Pattern E: CITY_<NAME>_<N> uses pre-discovered city tokens ---
# We can re-scrape the binary tree once for short city/region tokens.
# Skip if expensive — the templates above cover the bulk.

print(f"\n=== {len(found):,} / {len(target_ids):,} names cracked "
      f"({100.0*len(found)/len(target_ids):.1f} %) ===", flush=True)

# Dump 30 random samples
import random
random.seed(7)
print("\n--- 30 cracked samples ---")
for h, name in random.sample(list(found.items()), min(30, len(found))):
    t = text_for(h)[:80]
    print(f"  {name:<48} -> {t!r}")

OUT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\hash_names.csv"
with open(OUT, "w", encoding="utf-8") as f:
    f.write("hash,name,text\n")
    for h in sorted(found):
        t = text_for(h).replace("\r"," ").replace("\n"," | ")
        f.write(f"{h},{found[h]},{t!r}\n")
print(f"wrote {OUT}", flush=True)
