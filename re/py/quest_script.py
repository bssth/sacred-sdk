"""For a given quest prefix, find every FunkCode record across all script
sources that belongs to that quest, and dump those records with full
tag-labeled disassembly.

This produces a "quest script" view — every action the engine performs as
part of one quest, in execution order, per source file.

Sources (2026-09-10): the corpus is no longer just the 8 base per-class
directories.  `--sources` accepts anything funkcode_sources.resolve()
understands:

    base-classes (default, = the old behaviour)   all
    base | addon | classes | net | canonical
    base:VAMPIRELADY | addon:NetScript | TYPE_NPC_ELVE | ELVE

Matching: by default a record belongs to the quest when one of its *tokens*
canonicalises to the quest id (quest_index.classify), so `HQ_5` no longer
swallows `HQ_5_0_1` and `DQ_1502` no longer swallows `DQ_15024`.  Pass
--substring for the old raw byte-substring behaviour (useful for arbitrary
non-quest prefixes such as `LOC_RG1`).

Usage:
    python quest_script.py HQ_3_1_4
    python quest_script.py NQ_5001 --classes GLADIATOR,SERAPHIM
    python quest_script.py HQ_1_1  --sources addon:SERAPHIM
    python quest_script.py DQ_15024 --sources canonical
"""
import os, sys, re, struct, bisect, argparse, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from funkcode_disasm import walk_records, disasm_payload
from funkcode_tags    import label_for as tag_label
import funkcode_sources as fs
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

# legacy constants — unchanged values, other scripts import them
BIN = fs.BIN_ROOT
ALL_CLASSES = list(fs.ALL_CLASSES)

_classify = None
def _quest_classify(token):
    global _classify
    if _classify is None:
        import quest_index
        _classify = quest_index.classify
    return _classify(token)

TOKEN_RE = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{2,79}")


_data_cache = {}
def _data_of(source):
    """FunkCode.bin bytes for a Source, read once per process."""
    if source.key not in _data_cache:
        _data_cache[source.key] = source.read()
    return _data_cache[source.key]


_record_cache = {}
def records_of(data, cache_key=None):
    """walk_records(data) as a list, memoised per source key.

    Bulk generators (quest_script_book.py) ask for hundreds of prefixes from
    the same file; re-walking 125 000 records each time dominated the runtime.
    """
    if cache_key is None:
        return list(walk_records(data))
    if cache_key not in _record_cache:
        _record_cache[cache_key] = list(walk_records(data))
    return _record_cache[cache_key]


def find_records_with_prefix(data, prefix_bytes, cache_key=None):
    """Legacy: walk records and yield those whose payload contains the bytes."""
    for off, tag, size, payload in records_of(data, cache_key):
        if prefix_bytes in payload:
            yield off, tag, size, payload


_upper_cache = {}
def _upper_of(data, cache_key):
    """Uppercased copy of the file, memoised — the prefilter searches it so
    matching is case-insensitive (`nq_7601_dlg_offen` == `NQ_7601_...`)."""
    if cache_key is None:
        return data.upper()
    if cache_key not in _upper_cache:
        _upper_cache[cache_key] = data.upper()
    return _upper_cache[cache_key]


def find_records_for_quest(data, qid, cache_key=None):
    """Yield the records that own quest `qid` (token-canonical matching).

    A record that owns the quest must contain the id's bytes somewhere, so
    the file is searched for those byte patterns first (one memmem sweep per
    pattern, C speed) and each hit is mapped to its record via bisect; the
    token check then runs only on those few records.  Verified to return
    exactly the same records as the full per-record scan used by
    quest_index.SourceScan.
    """
    want = qid.upper()
    # prefilter patterns.  The id itself covers almost everything; daily
    # quests additionally use the shape DQ_START_<n> / DQ_OFFEN_<n> /
    # DQ_SIEG_<n>, which does NOT contain "DQ_<n>", so the bare "_<n>" is
    # added for them.
    pats = [want.encode("latin1")]
    m = re.match(r"^DQ_(\d+)$", want)
    if m:
        pats.append(("_" + m.group(1)).encode("latin1"))

    records = records_of(data, cache_key)
    starts = _starts_of(records, cache_key)
    up = _upper_of(data, cache_key)
    cand = set()
    for pat in pats:
        pos = up.find(pat)
        while pos >= 0:
            i = bisect.bisect_right(starts, pos) - 1
            if i >= 0:
                off, tag, size, payload = records[i]
                if off + 3 <= pos < off + size:
                    cand.add(i)
            pos = up.find(pat, pos + 1)

    for i in sorted(cand):
        off, tag, size, payload = records[i]
        for m in TOKEN_RE.finditer(payload):
            hit = _quest_classify(m.group().decode("latin1"))
            if hit and hit[1].upper() == want:
                yield off, tag, size, payload
                break


_starts_cache = {}
def _starts_of(records, cache_key):
    if cache_key is None:
        return [r[0] for r in records]
    if cache_key not in _starts_cache:
        _starts_cache[cache_key] = [r[0] for r in records]
    return _starts_cache[cache_key]


def hex_dump(payload, indent=6):
    """Pretty hex dump of payload bytes."""
    out = []
    sp = " " * indent
    for i in range(0, len(payload), 16):
        chunk = payload[i:i+16]
        h = " ".join(f"{b:02x}" for b in chunk)
        a = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)
        out.append(f"{sp}{i:04x}: {h:<48s}  {a}")
    return "\n".join(out)


def dump_record(off, tag, size, payload, indent=2, ops_limit=40, hex_too=False,
                marker="", index=None, emit=print):
    """Emit ONE record: header line + tag-labelled opcode disassembly.

    This is the shard body format — quest_shards.py calls it directly so the
    per-quest files and the big book stay byte-identical in style.
    """
    lbl = tag_label(tag)
    tch = chr(tag) if 32 <= tag <= 126 else "?"
    sp = " " * indent
    idx = f"#{index} " if index is not None else ""
    emit(f"\n{sp}{marker}{idx}{off:08x}  tag=0x{tag:02x}'{tch}' [{lbl}]  "
         f"size={size}  payload={size-3}B")
    ops, _, end_ip = disasm_payload(payload, indent=indent + 4, limit=ops_limit)
    for ln in ops:
        emit(ln)
    if hex_too:
        emit(hex_dump(payload, indent=indent + 4))
    return lbl


def dump_quest_script(prefix, classes=None, max_per_class=200, hex_too=False,
                      substring=False, sources=None):
    """Print the record dump for one quest across the given sources.

    `classes` keeps the legacy meaning (list of TYPE_NPC_* dir names / class
    names, base campaign); `sources` is the new, wider spec.  Either may be
    given; `sources` wins when both are set.
    """
    spec = sources if sources else (classes if classes else "base-classes")
    srcs = fs.resolve(spec, default="base-classes")
    pb = prefix.encode("ascii")
    total_records = 0
    by_tag = collections.Counter()
    print(f"=== Quest script for prefix '{prefix}' ===\n")
    for s in srcs:
        data = _data_of(s)
        if substring:
            recs = list(find_records_with_prefix(data, pb, s.key))
        else:
            recs = list(find_records_for_quest(data, prefix, s.key))
            if not recs:                       # unknown/non-quest prefix
                recs = list(find_records_with_prefix(data, pb, s.key))
        if not recs:
            continue
        print(f"\n--- {s.key}: {len(recs)} matching records ---")
        for off, tag, size, payload in recs[:max_per_class]:
            by_tag[(tag, tag_label(tag))] += 1
            total_records += 1
            dump_record(off, tag, size, payload, indent=2, ops_limit=40,
                        hex_too=hex_too)
        if len(recs) > max_per_class:
            print(f"  ... ({len(recs)-max_per_class} more matching records in this source)")

    print(f"\n=== Summary ===")
    print(f"  Total matching records : {total_records}")
    print(f"  Subsystem tags used    :")
    for (tag, lbl), n in by_tag.most_common(15):
        print(f"    {n:>4}  tag=0x{tag:02x}  [{lbl}]")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("prefix",  help="quest prefix to search (e.g. HQ_3_1_4, NQ_5001, DQ_15013)")
    ap.add_argument("--classes", default="", help="legacy: comma-separated base classes; empty=all")
    ap.add_argument("--sources", default="", help="source spec (see funkcode_sources)")
    ap.add_argument("--max-per-class", type=int, default=80,
                    help="cap on records dumped per source")
    ap.add_argument("--hex", action="store_true", help="also show raw hex of each payload")
    ap.add_argument("--substring", action="store_true",
                    help="legacy raw substring matching instead of token matching")
    args = ap.parse_args()
    dump_quest_script(args.prefix,
                      classes=[c.strip() for c in args.classes.split(",") if c.strip()] or None,
                      max_per_class=args.max_per_class, hex_too=args.hex,
                      substring=args.substring,
                      sources=args.sources or None)
