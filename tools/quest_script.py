"""For a given quest prefix, find every FunkCode record across all classes
that mentions any string starting with that prefix, and dump those records
with full tag-labeled disassembly.

This produces a "quest script" view — every action the engine performs as
part of one quest, in execution-order across class files.

Usage:
    python quest_script.py HQ_3_1_4
    python quest_script.py NQ_5001
    python quest_script.py DQ_15013
    python quest_script.py NQ_5001 --classes GLADIATOR,SERAPHIM
"""
import os, sys, re, struct, argparse, collections
sys.path.insert(0, os.path.dirname(__file__))
from funkcode_disasm import walk_records, disasm_payload
from funkcode_tags    import label_for as tag_label
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

BIN = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin"
ALL_CLASSES = ["TYPE_NPC_SERAPHIM","TYPE_NPC_GLADIATOR","TYPE_NPC_MAGICIAN","TYPE_NPC_ELVE",
               "TYPE_NPC_DARKELVE","TYPE_NPC_DAEMONIN","TYPE_NPC_VAMPIRELADY","TYPE_NPC_ZWERG"]

def find_records_with_prefix(data, prefix_bytes):
    """Walk records and yield those whose payload contains prefix_bytes."""
    for off, tag, size, payload in walk_records(data):
        if prefix_bytes in payload:
            yield off, tag, size, payload

def hex_dump(payload):
    """Pretty hex dump of payload bytes."""
    out = []
    for i in range(0, len(payload), 16):
        chunk = payload[i:i+16]
        h = " ".join(f"{b:02x}" for b in chunk)
        a = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)
        out.append(f"      {i:04x}: {h:<48s}  {a}")
    return "\n".join(out)

def dump_quest_script(prefix, classes, max_per_class=200, hex_too=False):
    pb = prefix.encode("ascii")
    total_records = 0
    by_tag = collections.Counter()
    print(f"=== Quest script for prefix '{prefix}' ===\n")
    for cls in classes:
        path = os.path.join(BIN, cls, "FunkCode.bin")
        if not os.path.exists(path): continue
        data = open(path, "rb").read()
        recs = list(find_records_with_prefix(data, pb))
        if not recs: continue
        cls_short = cls.replace("TYPE_NPC_", "")
        print(f"\n--- {cls_short}: {len(recs)} matching records ---")
        for off, tag, size, payload in recs[:max_per_class]:
            lbl = tag_label(tag)
            tch = chr(tag) if 32 <= tag <= 126 else "?"
            by_tag[(tag, lbl)] += 1
            total_records += 1
            print(f"\n  {off:08x}  tag=0x{tag:02x}'{tch}' [{lbl}]  size={size}  payload={size-3}B")
            ops, _, end_ip = disasm_payload(payload, indent=6, limit=40)
            for ln in ops:
                print(ln)
            if hex_too:
                print(hex_dump(payload))
        if len(recs) > max_per_class:
            print(f"  ... ({len(recs)-max_per_class} more matching records in this class)")

    print(f"\n=== Summary ===")
    print(f"  Total matching records : {total_records}")
    print(f"  Subsystem tags used    :")
    for (tag, lbl), n in by_tag.most_common(15):
        print(f"    {n:>4}  tag=0x{tag:02x}  [{lbl}]")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("prefix",  help="quest prefix to search (e.g. HQ_3_1_4, NQ_5001, DQ_15013)")
    ap.add_argument("--classes", default="", help="comma-separated classes; empty=all")
    ap.add_argument("--max-per-class", type=int, default=80,
                    help="cap on records dumped per class")
    ap.add_argument("--hex", action="store_true", help="also show raw hex of each payload")
    args = ap.parse_args()
    classes = ALL_CLASSES
    if args.classes:
        classes = [f"TYPE_NPC_{c.strip().upper()}" for c in args.classes.split(",")]
    dump_quest_script(args.prefix, classes, args.max_per_class, args.hex)
