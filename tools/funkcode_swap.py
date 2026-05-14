# SacredSDK — byte-level swap on a compiled FunkCode.bin.
#
# Sacred's compiled scripts store identifiers as INLINE ASCII strings inside
# the .bin (we verified this empirically — every identifier we looked for
# shows up via `grep -c` on the raw bytes). That means we don't need Sacred's
# compiler to mod a script: pick a same-length identifier and byte-replace it.
# fs_override then redirects Sacred's read of the original .bin to our custom
# copy and the new strings flow through the engine unchanged.
#
# Constraints
# -----------
#   - Replacement string MUST be the same byte length as the original; many
#     references in the .bin are by file offset to the string pool, so size
#     drift would corrupt the file.
#   - Both strings must already exist in the engine's resource registry for
#     `res:`-style references, otherwise the dialog comes up empty.
#   - This tool does NOT understand the bytecode — it's a textual swap. If
#     the same byte sequence appears in two unrelated places (e.g. as raw
#     data and as an identifier), BOTH get rewritten. For the conservative
#     identifier strings below this hasn't been an issue.

import argparse
import os
import sys

# Demo swap set: take Seraphim's class-specific dialogue identifiers and
# point them at other classes' equivalents. When the modded Seraphim hits
# the relevant quest, the engine renders the cross-class string instead.
#
# All entries are 32-character identifiers in the vanilla FunkCode.bin and
# all the targets exist in the same file — verified by `grep -c` before
# committing this list.
DEMO_SWAPS = [
    # 32 chars each.
    ("HQ_3_2_1_sera_NPC_Auftrag_Qstart", "HQ_3_1_4_glad_NPC_Auftrag_Qstart"),
    ("HQ_3_2_1_sera_NPC_Auftrag_Qoffen", "HQ_3_1_4_glad_NPC_Auftrag_Qoffen"),
    ("HQ_3_2_3_sera_NPC_Auftrag_Qstart", "HQ_3_5_3_vamp_NPC_Auftrag_Qstart"),
    ("HQ_3_2_3_sera_NPC_Auftrag_Qoffen", "HQ_3_5_3_vamp_NPC_Auftrag_Qoffen"),
]


def apply_swaps(data: bytes, swaps):
    buf = bytearray(data)
    report = []
    for old, new in swaps:
        assert len(old) == len(new), f"length mismatch {old!r} vs {new!r}"
        ob, nb = old.encode("ascii"), new.encode("ascii")
        n = 0
        start = 0
        while True:
            i = bytes(buf).find(ob, start)
            if i < 0:
                break
            buf[i : i + len(ob)] = nb
            n += 1
            start = i + len(nb)
        report.append((old, new, n))
    return bytes(buf), report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", help="vanilla FunkCode.bin (e.g. bin/TYPE_NPC_SERAPHIM/FunkCode.bin)")
    ap.add_argument("dst", help="output path (drop under <game>/custom/ to be picked up by fs_override)")
    ap.add_argument("--dry-run", action="store_true", help="print counts but don't write")
    args = ap.parse_args()

    with open(args.src, "rb") as f:
        data = f.read()
    out, report = apply_swaps(data, DEMO_SWAPS)

    print(f"input:  {args.src}  ({len(data):,} bytes)")
    print(f"output: {args.dst}")
    for old, new, n in report:
        marker = "OK " if n > 0 else "MISS"
        print(f"  [{marker}]  {old!r:36s} -> {new!r:36s}  x{n}")

    total = sum(r[2] for r in report)
    if total == 0:
        print("\nNo swaps applied — file probably already modded or wrong source.")
        return 1
    if args.dry_run:
        print(f"\n(dry-run) would write {len(out):,} bytes; total replacements: {total}")
        return 0
    os.makedirs(os.path.dirname(args.dst), exist_ok=True)
    with open(args.dst, "wb") as f:
        f.write(out)
    print(f"\nwrote {len(out):,} bytes; total replacements: {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
