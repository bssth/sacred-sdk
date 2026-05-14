"""Inventory of quest-related names in FunkCode.bin.

Scans all known classes' FunkCode.bin and extracts every cstr (type 0x16 or
0x01 payload) matching quest-name patterns. Reports counts, distinct names,
and a per-class breakdown.
"""
import os, re, collections, struct

ROOT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin"
CLASSES = ["TYPE_NPC_SERAPHIM","TYPE_NPC_GLADIATOR","TYPE_NPC_MAGICIAN","TYPE_NPC_ELVE",
           "TYPE_NPC_DARKELVE","TYPE_NPC_DAEMONIN","TYPE_NPC_VAMPIRELADY","TYPE_NPC_ZWERG"]

# Patterns of interest
PATTERNS = [
    ("MainQuest",      re.compile(rb"\bHQ_\d+(?:_\d+){0,4}(?:_[a-z]+)?(?:_[A-Za-z_]+)?\b")),
    ("NamedQuest",     re.compile(rb"\bNQ_[A-Za-z_0-9]+\b")),
    ("DailyQuestVar",  re.compile(rb"\bDQ_[A-Za-z_0-9]+\b")),
    ("DailyTemplate",  re.compile(rb"\bDQ\d+_[A-Z_]+\b")),
    ("Region",         re.compile(rb"\bRG\d+\b")),
    ("ResRef",         re.compile(rb"\bres:[A-Za-z0-9_]+\b")),
    ("TPTarget",       re.compile(rb"\btptarget_[a-z]+_\d+\b")),
    ("RType",          re.compile(rb"\bRTYPE_NPC_[A-Z]+\b")),
    ("Belohnung",      re.compile(rb"\bBelohnung_[A-Za-z]+\b")),
]

print(f"{'class':12}  {'size':>10}", *[f"{n[:14]:>14}" for n,_ in PATTERNS])
totals = {n: collections.Counter() for n,_ in PATTERNS}
sample_per_pattern = {n: [] for n,_ in PATTERNS}

for cls in CLASSES:
    p = os.path.join(ROOT, cls, "FunkCode.bin")
    if not os.path.exists(p):
        continue
    data = open(p, "rb").read()
    row = [cls.replace("TYPE_NPC_",""), f"{len(data):>10}"]
    for name, rx in PATTERNS:
        hits = rx.findall(data)
        for h in hits:
            totals[name][h.decode("latin1", "replace")] += 1
            if len(sample_per_pattern[name]) < 10 and h not in [s.encode() for s in sample_per_pattern[name]]:
                sample_per_pattern[name].append(h.decode("latin1", "replace"))
        row.append(f"{len(hits):>14}")
    print("  ".join(row))

print(f"\n=== distinct names per pattern (across all classes) ===")
for name, _ in PATTERNS:
    c = totals[name]
    print(f"\n--- {name}: {len(c)} distinct ---")
    # show top-10 by count and 10 samples
    for x, cnt in c.most_common(10):
        print(f"  {cnt:>5}  {x}")
    if len(c) > 10:
        print(f"  ... and {len(c)-10} more (e.g. {', '.join(list(c.keys())[10:18])})")
