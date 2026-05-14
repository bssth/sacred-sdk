"""Print a human-readable "quest card" for any Sacred quest.

Combines:
  1. FunkCode.bin — for the exhaustive list of symbolic name tokens that
     actually exist in scripts (HQ_*, NQ_*, DQ_*).
  2. sacred_hash — to turn each symbolic name into a resource id.
  3. global.res — to resolve each id to its display text.

Usage:
    python quest_dump.py HQ_3_1_4
    python quest_dump.py NQ_5001
    python quest_dump.py NQ_UW9521
    python quest_dump.py DQ_15013
    python quest_dump.py --list-prefixes HQ      # show all distinct prefixes
    python quest_dump.py --grep "Slater"         # find quests by display text

Examples:
    python quest_dump.py NQ_5001
    => === NQ_5001 quest card ===
       LOG_TITLE  : 'Helping the Refugees'
       LOG_OFFEN  : 'The refugees have asked me ...'
       LOG_SIEG   : 'I have helped the refugees ...'
       PRENPC_QOFFEN : 'The camp is situated north of Drakenden ...'
       ...
"""
import sys, os, re, struct, argparse, collections
sys.path.insert(0, os.path.dirname(__file__))
from sacred_hash import sacred_hash, hash_for_id
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

# --- global.res text lookup ---
GR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\us\global.res"
_data = open(GR, "rb").read()
assert _data[:4] == b"SZ\x00\x00"
_blob_start = struct.unpack_from("<I", _data, 8)[0]
_slots = []
for i in range(_blob_start // 16):
    _, ident, off, _z = struct.unpack_from("<IIII", _data, i*16)
    if _blob_start <= off < len(_data):
        _slots.append((ident, off))
_id_to_off = {ident: off for ident, off in _slots}
_slots_by_off = sorted(_slots, key=lambda x: x[1])
_off_to_idx = {off: i for i, (_, off) in enumerate(_slots_by_off)}

def text_for_id(h):
    if h not in _id_to_off: return None
    off = _id_to_off[h]
    i = _off_to_idx[off]
    nxt = _slots_by_off[i+1][1] if i+1 < len(_slots_by_off) else len(_data) - 4
    raw = _data[off+4 : nxt+4]
    while len(raw) >= 2 and raw[-2:] == b"\x00\x00":
        raw = raw[:-2]
    return raw.decode("utf-16-le", errors="replace")

def text_for_name(name):
    return text_for_id(sacred_hash(name))

# --- FunkCode token scraper ---
BIN_ROOT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin"
CLASSES = [
    "TYPE_NPC_SERAPHIM","TYPE_NPC_GLADIATOR","TYPE_NPC_MAGICIAN","TYPE_NPC_ELVE",
    "TYPE_NPC_DARKELVE","TYPE_NPC_DAEMONIN","TYPE_NPC_VAMPIRELADY","TYPE_NPC_ZWERG",
]
QUEST_TOKEN_RE = re.compile(
    rb"\b("
    rb"HQ_\d+(?:_\d+){0,4}(?:_[A-Za-z][A-Za-z0-9_]*)*"
    rb"|"
    rb"NQ_[A-Za-z0-9_]+"
    rb"|"
    rb"DQ\d+_[A-Z0-9_]+"
    rb"|"
    rb"DQ_[A-Za-z0-9_]+"
    rb"|"
    rb"RB_[A-Za-z0-9_]+"
    rb")\b"
)

_token_cache = None
def all_tokens():
    """Return dict: token -> set(classes_where_seen)."""
    global _token_cache
    if _token_cache is not None: return _token_cache
    out = collections.defaultdict(set)
    for cls in CLASSES:
        p = os.path.join(BIN_ROOT, cls, "FunkCode.bin")
        if not os.path.exists(p): continue
        data = open(p, "rb").read()
        for m in QUEST_TOKEN_RE.findall(data):
            tok = m.decode("ascii", "replace")
            out[tok].add(cls.replace("TYPE_NPC_", ""))
    _token_cache = dict(out)
    return _token_cache

# --- Quest card printing ---
# Suffix templates we expect. Hashing prefix+suffix may yield hits even for
# tokens that don't appear literally in FunkCode (Sacred composes them at
# runtime). We try BOTH sources: literal tokens AND templated suffixes.
SUFFIX_TEMPLATES = [
    "_LOG_TITLE", "_LOG_HEADER", "_LOG_OFFEN", "_LOG_SIEG", "_LOG_ZIEL",
    "_LOG_FAIL", "_LOG_ABBRUCH", "_LOG_QSTART", "_LOG_QOFFEN", "_LOG_QSIEG",
    "_LOG_QFAIL", "_LOG_INFO", "_LOG_TEXT", "_LOG_HINWEIS",
    "_TITLE", "_HEADER", "_TEXT",
    "_PRENPC_QOFFEN", "_PRENPC_QSIEG", "_PRENPC_QFAIL", "_PRENPC_PRESTART",
    "_NPC_AUFTRAG_QSTART", "_NPC_AUFTRAG_QOFFEN", "_NPC_AUFTRAG_QSIEG",
    "_NPC_AUFTRAG_QFAIL", "_NPC_AUFTRAG",
    "_AUFTRAGGEBER", "_AUFTRAGGEBER1", "_AUFTRAGGEBER2",
    "_NPC", "_NPC1", "_NPC2",
    "_ZIEL", "_OFFEN", "_SIEG", "_FAIL", "_ABBRUCH",
    "_QOFFEN", "_QSIEG", "_QFAIL", "_QSTART",
    "_DESC", "_NAME",
]
# Per-class variants for HQ quests.
CLASS_TOKENS = ["glad", "sera", "mage", "helf", "vamp", "DEM", "DWA", "delf"]

def dump_card(prefix, exhaustive=False, only_text=True):
    """Print the quest card for one prefix."""
    print(f"\n=== {prefix} ===")

    # 1) Hits from literal FunkCode tokens
    toks = all_tokens()
    literal_hits = [t for t in toks if t.startswith(prefix)]

    # 2) Synthesised hits from prefix + suffix templates
    synth = set()
    for s in SUFFIX_TEMPLATES:
        synth.add(prefix + s)
    for cls in CLASS_TOKENS:
        for s in SUFFIX_TEMPLATES:
            synth.add(f"{prefix}_{cls}{s}")

    candidates = sorted(set(literal_hits) | synth)
    # Dedupe by hash — case-insensitive variants (`_LOG_HEADER` vs `_Log_Header`)
    # all hash to the same id because sacred_hash() applies toupper().
    # Prefer the variant that literally appears in FunkCode.
    by_hash = {}
    in_funkcode = set(literal_hits)
    for tok in candidates:
        h = sacred_hash(tok)
        txt = text_for_id(h)
        if not txt: continue
        prev = by_hash.get(h)
        if prev is None:
            by_hash[h] = (tok, txt)
        else:
            prev_tok = prev[0]
            # Replace if new variant is in FunkCode and old wasn't
            if tok in in_funkcode and prev_tok not in in_funkcode:
                by_hash[h] = (tok, txt)
    resolved = [(tok, h, txt) for h, (tok, txt) in by_hash.items()]

    # Order suffixes logically: title → header → narrative open → mid → win
    # → NPC variants → class variants → everything else.
    def sort_key(triple):
        tok = triple[0]
        sfx = tok[len(prefix):] if tok.startswith(prefix) else tok
        sfx_u = sfx.upper()
        order = [
            ("_LOG_TITLE", 0), ("_TITLE", 0),
            ("_LOG_HEADER", 1), ("_HEADER", 1),
            ("_LOG_QSTART", 2),
            ("_LOG_OFFEN", 3), ("_OFFEN", 3),
            ("_LOG_QOFFEN", 4),
            ("_LOG_SIEG", 5), ("_SIEG", 5), ("_LOG_QSIEG", 5),
            ("_LOG_ZIEL", 6), ("_ZIEL", 6),
            ("_LOG_QFAIL", 7), ("_LOG_FAIL", 7), ("_FAIL", 7),
            ("_PRENPC_PRESTART", 10), ("_PRENPC_QOFFEN", 11),
            ("_PRENPC_QSIEG", 12), ("_PRENPC_QFAIL", 13),
            ("_NPC_AUFTRAG_QSTART", 20), ("_NPC_AUFTRAG_QOFFEN", 21),
            ("_NPC_AUFTRAG_QSIEG", 22), ("_NPC_AUFTRAG_QFAIL", 23),
            ("_AUFTRAGGEBER", 30),
        ]
        for needle, rank in order:
            if needle in sfx_u: return (rank, sfx)
        return (99, sfx)
    resolved.sort(key=sort_key)

    if not resolved:
        print(f"  (no text resolved for prefix '{prefix}')")
        if prefix.startswith("DQ_") and prefix[3:].isdigit():
            nid = int(prefix[3:])
            txt = text_for_id(hash_for_id(nid))
            if txt:
                print(f"  (but as numeric id {nid} -> hash {hash_for_id(nid)}):")
                print(f"     {txt!r}")
        return 0

    print(f"  {len(resolved)} distinct text entries:\n")
    for tok, h, txt in resolved:
        sfx = tok[len(prefix):] if tok.startswith(prefix) else f"({tok})"
        seen = " [F]" if tok in in_funkcode else ""
        snip = txt.strip().replace("\r"," ").replace("\n", " | ")
        if not only_text or snip:
            print(f"  {sfx:<26}{seen}")
            for line in [snip[i:i+100] for i in range(0, len(snip), 100)][:6]:
                print(f"      {line}")

    # Also report which class FunkCode files reference this quest at all
    classes_seen = set()
    for t in literal_hits:
        classes_seen |= toks[t]
    if classes_seen:
        print(f"\n  appears in FunkCode for classes: {', '.join(sorted(classes_seen))}")
    return len(resolved)

def list_prefixes(family):
    toks = all_tokens()
    prefixes = set()
    for t in toks:
        if t.startswith(family + "_"):
            # Heuristically derive a "quest root prefix":
            # HQ_3_1_4_glad_NPC_Auftrag_Qstart -> HQ_3_1_4
            # NQ_5001_LOG_TITLE                 -> NQ_5001
            # NQ_UW9521_LOG_OFFEN2              -> NQ_UW9521
            parts = t.split("_")
            if len(parts) < 2: continue
            if family in ("HQ",):
                # take leading numeric parts (chapter_section_step)
                root = [parts[0]]
                for p in parts[1:]:
                    if p.isdigit(): root.append(p)
                    else: break
                if len(root) >= 3:
                    prefixes.add("_".join(root))
            elif family in ("NQ", "RB"):
                # NQ_<num>... or NQ_<prefix><num>...
                root = parts[0] + "_" + parts[1]
                prefixes.add(root)
            elif family == "DQ":
                if parts[1].isdigit():
                    prefixes.add(parts[0] + "_" + parts[1])
                else:
                    prefixes.add(parts[0] + "_" + parts[1])
    return sorted(prefixes)

def grep_text(needle):
    """Find quests whose resolved text contains `needle`."""
    toks = all_tokens()
    nl = needle.lower()
    hits = []
    for tok in toks:
        t = text_for_name(tok)
        if t and nl in t.lower():
            hits.append((tok, t.strip()))
    return hits

def main():
    ap = argparse.ArgumentParser(description="Sacred quest text dumper")
    ap.add_argument("prefix", nargs="?",
                    help="quest prefix to dump, e.g. HQ_3_1_4, NQ_5001, DQ_15013")
    ap.add_argument("--list-prefixes", metavar="FAMILY",
                    help="list all root prefixes in family (HQ, NQ, RB, DQ)")
    ap.add_argument("--grep", metavar="TEXT",
                    help="find quests whose text contains TEXT")
    args = ap.parse_args()

    if args.list_prefixes:
        ps = list_prefixes(args.list_prefixes)
        print(f"{len(ps)} {args.list_prefixes}-family root prefixes:")
        for p in ps:
            print(f"  {p}")
        return

    if args.grep:
        hits = grep_text(args.grep)
        print(f"{len(hits)} match(es) for '{args.grep}':")
        for tok, t in hits[:80]:
            print(f"  {tok:<48}  {t[:80]!r}")
        if len(hits) > 80:
            print(f"  ... and {len(hits)-80} more")
        return

    if not args.prefix:
        ap.print_help()
        return

    dump_card(args.prefix)

if __name__ == "__main__":
    main()
