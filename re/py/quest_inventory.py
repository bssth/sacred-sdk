"""Inventory of quest-related names in FunkCode.bin.

Scans the selected script sources and extracts every name matching the
quest-name patterns. Reports per-source counts and distinct names.

    python quest_inventory.py                    # the 8 base per-class dirs (legacy)
    python quest_inventory.py --sources all      # base + addon + net
    python quest_inventory.py --sources canonical
"""
import os, re, sys, collections, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import funkcode_sources as fs
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

# legacy names
ROOT = fs.BIN_ROOT
CLASSES = list(fs.ALL_CLASSES)

# Patterns of interest
PATTERNS = [
    ("MainQuest",      re.compile(rb"\bHQ_\d+(?:_\d+){0,4}(?:_[a-z]+)?(?:_[A-Za-z_]+)?\b")),
    ("NamedQuest",     re.compile(rb"\bNQ_[A-Za-z_0-9]+\b")),
    ("DailyQuestVar",  re.compile(rb"\bDQ_[A-Za-z_0-9]+\b")),
    ("DailyTemplate",  re.compile(rb"\bDQ\d+_[A-Z_]+\b")),
    ("GuildQuest",     re.compile(rb"\bGQ_[A-Za-z_0-9]+\b")),
    ("RuneBook",       re.compile(rb"\bRB_[0-9]+[A-Za-z_0-9]*\b")),
    ("Region",         re.compile(rb"\bRG\d+\b")),
    ("ResRef",         re.compile(rb"\bres:[A-Za-z0-9_]+\b")),
    ("TPTarget",       re.compile(rb"\btptarget_[a-z]+_\d+\b")),
    ("RType",          re.compile(rb"\bRTYPE_NPC_[A-Z]+\b")),
    ("Belohnung",      re.compile(rb"\bBelohnung_[A-Za-z]+\b")),
]


def run(sources):
    print(f"{'source':22}  {'size':>10}", *[f"{n[:14]:>14}" for n, _ in PATTERNS])
    totals = {n: collections.Counter() for n, _ in PATTERNS}
    per_source = {n: collections.defaultdict(set) for n, _ in PATTERNS}

    for s in sources:
        data = s.read()
        row = [f"{s.key:22}", f"{len(data):>10}"]
        for name, rx in PATTERNS:
            hits = rx.findall(data)
            for h in hits:
                t = h.decode("latin1", "replace")
                totals[name][t] += 1
                per_source[name][t].add(s.key)
            row.append(f"{len(hits):>14}")
        print("  ".join(row))

    print(f"\n=== distinct names per pattern (across selected sources) ===")
    for name, _ in PATTERNS:
        c = totals[name]
        print(f"\n--- {name}: {len(c)} distinct ---")
        for x, cnt in c.most_common(10):
            print(f"  {cnt:>5}  {x}")
        if len(c) > 10:
            rest = list(c.keys())[10:18]
            print(f"  ... and {len(c)-10} more (e.g. {', '.join(rest)})")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--sources", default="base-classes",
                    help="source spec (default: base-classes = legacy behaviour)")
    args = ap.parse_args()
    run(fs.resolve(args.sources, default="base-classes"))
