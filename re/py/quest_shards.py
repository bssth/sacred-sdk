"""Write one small, self-contained file per quest so later passes never have
to open a 4 MB FunkCode.bin again.

    sdk/.claude/knowledge/quests/shards/<campaign>/<id>.txt

Each shard holds the tag-labelled record disassembly of that quest in the
BASELINE source (base:VAMPIRELADY for base-campaign quests, addon:SERAPHIM
for addon-only ones, base:NetScript for net-only ones), plus — when the
quest is class-specific — the differing classes' own records appended under
a clear header.

The record body is produced by quest_script.dump_record(), so shards and the
big book stay identical in style.

    python quest_shards.py                     # all quests, all sources
    python quest_shards.py --quest HQ_3_1_4    # just one (prints path)
    python quest_shards.py --categories HQ,NQ  # subset
"""
import os, sys, io, json, time, argparse, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import funkcode_sources as fs
import quest_index as qi
import quest_script as qs
from funkcode_tags import label_for as tag_label

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

SHARD_ROOT = os.path.join(qi.OUT_DIR, "shards")

# how many records may sit between two owned records before the run is split
DEFAULT_GAP = 24
# hard cap on records written per shard section (context included)
DEFAULT_MAX_RECORDS = 500
# categories that are machinery, not a playable quest: owned records only,
# no context expansion (their owners are scattered over the whole file)
NO_CONTEXT_CATEGORIES = {"VAR", "DQSLOT", "DQTPL", "WW"}


def clusters(indices, gap=DEFAULT_GAP):
    """Group sorted record indices into runs; a run is expanded to its full
    record range so the quest's intervening logic is included."""
    out = []
    for i in indices:
        if out and i - out[-1][-1] <= gap:
            out[-1].append(i)
        else:
            out.append([i])
    return out


def _text(token):
    t = qi._text_for(token)
    if not t:
        return None
    return t.strip().replace("\r", " ").replace("\n", " | ")


def write_shard(q, scans, root=SHARD_ROOT, gap=DEFAULT_GAP,
                max_records=DEFAULT_MAX_RECORDS, with_text=True,
                ops_limit=40):
    """Write one quest's shard file, return its path (relative to GAME_ROOT)."""
    campaign = q["campaign"]
    d = os.path.join(root, campaign)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, q["id"] + ".txt")

    baseline = q["baselineSource"]
    scan = scans[baseline]
    owned = scan.record_indices(q["id"])
    no_ctx = q["category"] in NO_CONTEXT_CATEGORIES

    buf = io.StringIO()
    def emit(line=""):
        buf.write(line + "\n")

    # ------------------------------------------------------------- header --
    emit("# %s — %s" % (q["id"], q.get("title") or "(no questbook title resolved)"))
    emit("#")
    emit("# category        : %s" % q["category"])
    emit("# campaign        : %s" % campaign)
    emit("# baseline source : %s   (%d owned records)" % (baseline, len(owned)))
    if q["classSpecific"]:
        bd = q.get("baseClassDiff") or {}
        bits = []
        if bd.get("differing"):
            bits.append("payload differs in: " + ", ".join(bd["differing"]))
        if bd.get("missing"):
            bits.append("absent in: " + ", ".join(bd["missing"]))
        emit("# class-specific  : YES — %s" % ("; ".join(bits) or "see index"))
    else:
        emit("# class-specific  : no (identical owned-record payloads in every class that has it)")
    emit("# sources         : %s" % ", ".join(
        "%s(%d)" % (k, n) for k, n in q["recordsBySource"].items()))
    emit("# tokens          : %s" % ", ".join(q["tokens"]))
    emit("# regenerate      : python sdk/re/py/quest_shards.py --quest %s" % q["id"])
    emit("#")
    emit("# FORMAT")
    emit("#   '>>' = record OWNS this quest (its payload names one of the tokens above)")
    emit("#   ' .' = neighbouring record, included as context because it lies inside")
    emit("#          an owned span (gap <= %d records). Context records may belong to" % gap)
    emit("#          another quest — check the strings before attributing them.")
    emit("#   header line: #<record index> <file offset> tag=0xNN'c' [Subsystem] size=..")
    emit("#   body: FunkCode opcode disassembly of the payload (funkcode_disasm)")
    if no_ctx:
        emit("#   NOTE: category %s -> owned records only, no context expansion." % q["category"])
    emit("#")

    # ------------------------------------------------------------ strings --
    if with_text:
        rows = []
        for t in q["tokens"]:
            txt = _text(t)
            if txt:
                rows.append((t, txt))
        if rows:
            emit("# ---- resolved global.res strings (scripts/us/global.res) ----")
            for t, txt in rows:
                emit("#   %-42s %s" % (t, txt[:300]))
            emit("#")

    # ----------------------------------------------------------- baseline --
    written = 0
    cls = clusters(owned, gap) if not no_ctx else [[i] for i in owned]
    emit("=== BASELINE %s — %d owned records in %d cluster(s) ==="
         % (baseline, len(owned), len(cls)))
    tag_hist = collections.Counter()
    for ci, run in enumerate(cls, 1):
        lo, hi = run[0], run[-1]
        span = range(lo, hi + 1) if not no_ctx else run
        ownset = set(run)
        emit("\n--- cluster %d: records %d..%d (%d records, %d owned) ---"
             % (ci, lo, hi, len(list(span)), len(run)))
        for i in span:
            if written >= max_records:
                emit("\n... record cap %d reached; %d owned records not shown "
                     "(raise --max-records or use quest_script.py)"
                     % (max_records, len([x for x in owned if x > i])))
                break
            off, tag, size, payload = scan.records[i]
            tag_hist[(tag, tag_label(tag))] += 1
            qs.dump_record(off, tag, size, payload, indent=1, ops_limit=ops_limit,
                           marker=">> " if i in ownset else " . ", index=i, emit=emit)
            written += 1
        if written >= max_records:
            break

    emit("\n=== subsystem tags in this shard ===")
    for (tag, lbl), n in tag_hist.most_common():
        emit("  %4d  tag=0x%02x  [%s]" % (n, tag, lbl))

    # ------------------------------------------------------- class diffs --
    bd = q.get("baseClassDiff") or {}
    for cname in bd.get("differing", []):
        key = "base:" + cname
        if key not in scans:
            continue
        sc = scans[key]
        idxs = sc.record_indices(q["id"])
        emit("\n\n=== CLASS DIFF %s — %d owned records "
             "(payload md5 %s vs baseline %s) ==="
             % (key, len(idxs), q["payloadMd5"].get(key), q["payloadMd5"].get(baseline)))
        emit("=== only this class's OWNED records are shown (no context) ===")
        for n, i in enumerate(idxs):
            if n >= max_records // 2:
                emit("\n... %d more owned records not shown" % (len(idxs) - n))
                break
            off, tag, size, payload = sc.records[i]
            qs.dump_record(off, tag, size, payload, indent=1, ops_limit=ops_limit,
                           marker=">> ", index=i, emit=emit)

    ad = q.get("addonClassDiff") or {}
    for cname in ad.get("differing", []):
        key = "addon:" + cname
        if key not in scans:
            continue
        sc = scans[key]
        idxs = sc.record_indices(q["id"])
        emit("\n\n=== ADDON CLASS DIFF %s — %d owned records ===" % (key, len(idxs)))
        for n, i in enumerate(idxs):
            if n >= max_records // 2:
                emit("\n... %d more owned records not shown" % (len(idxs) - n))
                break
            off, tag, size, payload = sc.records[i]
            qs.dump_record(off, tag, size, payload, indent=1, ops_limit=ops_limit,
                           marker=">> ", index=i, emit=emit)

    with open(path, "w", encoding="utf-8") as fh:
        fh.write(buf.getvalue())
    return os.path.relpath(path, fs.GAME_ROOT).replace("\\", "/")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default="all")
    ap.add_argument("--root", default=SHARD_ROOT)
    ap.add_argument("--quest", help="only this quest id")
    ap.add_argument("--categories", help="comma-separated category filter")
    ap.add_argument("--gap", type=int, default=DEFAULT_GAP)
    ap.add_argument("--max-records", type=int, default=DEFAULT_MAX_RECORDS)
    ap.add_argument("--ops", type=int, default=40, help="max opcodes per record")
    ap.add_argument("--no-text", action="store_true", help="skip global.res strings")
    ap.add_argument("--index", default=qi.JSON_OUT,
                    help="also rewrite the index JSON with shardFile paths")
    args = ap.parse_args()

    t0 = time.time()
    sources = fs.resolve(args.sources, default="all")
    meta, quests, scans = qi.build_index(sources, with_titles=not args.no_text)

    cats = set(c.strip().upper() for c in args.categories.split(",")) if args.categories else None
    n = 0
    for q in quests:
        if args.quest and q["id"].upper() != args.quest.upper():
            continue
        if cats and q["category"] not in cats:
            continue
        q["shardFile"] = write_shard(q, scans, root=args.root, gap=args.gap,
                                     max_records=args.max_records,
                                     with_text=not args.no_text, ops_limit=args.ops)
        n += 1
        if args.quest:
            print(q["shardFile"])

    if not args.quest and not cats:
        qi.write_json(meta, quests, args.index)
        qi.write_md(meta, quests, qi.MD_OUT)
    print("wrote %d shards under %s in %.1fs" % (n, args.root, time.time() - t0))


if __name__ == "__main__":
    main()
