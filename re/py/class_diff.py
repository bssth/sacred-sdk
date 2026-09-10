"""RECORD-GRANULAR DIFF of FunkCode.bin (and the sibling blobs) across the
8 playable classes, the base/addon campaigns and the net scripts.

Why a second tool next to quest_index.py: quest_index compares only the
records a quest *owns* (records whose payload mentions one of its tokens).
That answers "does quest X differ", but it cannot answer "how much of the
file differs at all", because a record with no quest token in it is owned by
nobody.  This tool aligns the two files as SEQUENCES OF RECORDS
(difflib.SequenceMatcher over per-record md5 of `tag || payload`) and reports
every record that is inserted / deleted / replaced, then attributes those
records back to quest ids with quest_index.classify.

    python class_diff.py                      # base classes vs base:VAMPIRELADY
    python class_diff.py --file StartCode.bin
    python class_diff.py --pairs addon        # addon vs base, same class
    python class_diff.py --pairs net          # net vs SP
    python class_diff.py --json out.json
"""
import os, sys, json, hashlib, difflib, argparse, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import funkcode_sources as fs
from funkcode_disasm import walk_records
from funkcode_tags import label_for as tag_label
import quest_index as qi

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass


def rec_list(source, name="FunkCode.bin"):
    """[(off, tag, size, payload)] for one blob, or None when absent."""
    if not source.exists(name):
        return None
    return list(walk_records(source.read(name)))


def rec_keys(records):
    """Per-record identity: md5(tag || payload).  Position-independent so the
    aligner can see an inserted block as an insert, not as 'everything after
    here changed'."""
    out = []
    for off, tag, size, payload in records:
        h = hashlib.md5()
        h.update(bytes([tag]))
        h.update(payload)
        out.append(h.hexdigest())
    return out


def quests_in(payload, tag):
    """Quest ids mentioned by one record's payload (same classifier the index
    uses, so the two tools agree on ownership)."""
    hits = set()
    for m in qi.TOKEN_RE.finditer(payload):
        r = qi.classify(m.group().decode("latin1"))
        if r:
            hits.add(r[1])
    return hits


def diff_pair(a_src, b_src, name="FunkCode.bin"):
    """Align b against a.  Returns a dict of counts plus the changed blocks."""
    ra, rb = rec_list(a_src, name), rec_list(b_src, name)
    if ra is None or rb is None:
        return None
    ka, kb = rec_keys(ra), rec_keys(rb)
    sm = difflib.SequenceMatcher(None, ka, kb, autojunk=False)
    equal = ins = dele = 0
    blocks = []
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal":
            equal += (i2 - i1)
            continue
        na, nb = i2 - i1, j2 - j1
        dele += na
        ins += nb
        qa, qb = set(), set()
        tags = collections.Counter()
        for i in range(i1, i2):
            off, tag, size, payload = ra[i]
            qa |= quests_in(payload, tag)
            tags[tag_label(tag)] += 1
        for j in range(j1, j2):
            off, tag, size, payload = rb[j]
            qb |= quests_in(payload, tag)
            tags[tag_label(tag)] += 1
        blocks.append({"op": op, "aRange": [i1, i2], "bRange": [j1, j2],
                       "aRecords": na, "bRecords": nb,
                       "quests": sorted(qa | qb),
                       "tags": tags.most_common(6)})
    return {"a": a_src.key, "b": b_src.key, "file": name,
            "aRecords": len(ra), "bRecords": len(rb),
            "equalRecords": equal, "aOnly": dele, "bOnly": ins,
            "changedBlocks": len(blocks), "blocks": blocks}


def pair_list(mode, name):
    if mode == "classes":
        base = fs.get(fs.BASELINE)
        return [(base, fs.get("base:" + c)) for c in fs.CLASS_NAMES
                if "base:" + c != fs.BASELINE]
    if mode == "addon":
        return [(fs.get("base:" + c), fs.get("addon:" + c)) for c in fs.CLASS_NAMES]
    if mode == "net":
        return [(fs.get("base:GLADIATOR"), fs.get("base:NetScript")),
                (fs.get("base:GLADIATOR"), fs.get("base:NetScriptCamp")),
                (fs.get("addon:SERAPHIM"), fs.get("addon:NetScript")),
                (fs.get("addon:SERAPHIM"), fs.get("addon:NetScriptCamp")),
                (fs.get("base:NetScript"), fs.get("addon:NetScript"))]
    raise SystemExit("unknown --pairs %r" % mode)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", default="FunkCode.bin")
    ap.add_argument("--pairs", default="classes",
                    choices=["classes", "addon", "net"])
    ap.add_argument("--json")
    ap.add_argument("--blocks", type=int, default=0,
                    help="print the N largest changed blocks per pair")
    args = ap.parse_args()

    results = []
    for a, b in pair_list(args.pairs, args.file):
        r = diff_pair(a, b, args.file)
        if r is None:
            print("SKIP %s -> %s (%s missing)" % (a.key, b.key, args.file))
            continue
        results.append(r)
        pct = 100.0 * r["equalRecords"] / max(1, r["aRecords"])
        print("%-22s -> %-22s  a=%6d b=%6d  equal=%6d (%.3f%%)  a-only=%4d b-only=%4d  blocks=%d"
              % (r["a"], r["b"], r["aRecords"], r["bRecords"],
                 r["equalRecords"], pct, r["aOnly"], r["bOnly"], r["changedBlocks"]))
        qs = collections.Counter()
        for blk in r["blocks"]:
            for q in blk["quests"]:
                qs[q] += blk["aRecords"] + blk["bRecords"]
        if qs:
            print("      quests touched: " + ", ".join(
                "%s(%d)" % (k, v) for k, v in qs.most_common(20)))
        for blk in sorted(r["blocks"], key=lambda x: -(x["aRecords"] + x["bRecords"]))[:args.blocks]:
            print("      %-7s a[%d:%d]=%d b[%d:%d]=%d %s tags=%s"
                  % (blk["op"], blk["aRange"][0], blk["aRange"][1], blk["aRecords"],
                     blk["bRange"][0], blk["bRange"][1], blk["bRecords"],
                     ",".join(blk["quests"][:6]) or "-", blk["tags"][:3]))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(results, fh, indent=1)
        print("wrote %s" % args.json)


if __name__ == "__main__":
    main()
