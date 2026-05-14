"""Modify text(s) in `scripts/<lang>/global.res` and write the result back.

Patch 1 in our DLL reads the file from disk every time Sacred queries
BINARY 107, so editing this file (with the game NOT running) and restarting
gives you a live text mod.

Usage:

    python globalres_modify.py --by-name DQ1_TOETE_NPC_ZIEL18 --to "Custom Mob Name"
    python globalres_modify.py --by-hash 2123824365 --to "Custom Mob Name"
    python globalres_modify.py --list                       # show first 30 entries
    python globalres_modify.py --grep "Undead Mage"         # find by text

Backups: writes `global.res.bak` once on first modify, then overwrites
`global.res` in-place. Restore by copying `.bak` back.

NB: edits are LENGTH-PRESERVING only by default — if the new text differs in
byte-length from the original, the entire text blob shifts and we'd have to
re-write all `offset` slots. Tool DOES support length-changing edits and
re-computes the offsets correctly, but you'll get a warning. Sacred handles
shifted offsets fine because we wrote a clean parser of the format already.
"""
import argparse, csv, os, struct, sys, shutil
sys.path.insert(0, os.path.dirname(__file__))
from sacred_hash import sacred_hash, hash_for_id
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

GAME    = r"E:\SteamLibrary\steamapps\common\Sacred Gold"
VANILLA = os.path.join(GAME, "scripts", "us", "global.res")
# Default: write to custom/ override (vanilla file stays untouched).
CUSTOM_DIR = os.path.join(GAME, "custom", "scripts", "us")
GR     = os.path.join(CUSTOM_DIR, "global.res")
# If custom override doesn't exist yet, clone vanilla first.
if not os.path.exists(GR):
    os.makedirs(CUSTOM_DIR, exist_ok=True)
    if os.path.exists(VANILLA):
        shutil.copyfile(VANILLA, GR)
        print(f"[init] cloned vanilla -> {GR}")
BAK    = GR + ".bak"

def load():
    with open(GR, "rb") as f: data = f.read()
    assert data[:4] == b"SZ\x00\x00"
    blob_start = struct.unpack_from("<I", data, 8)[0]
    return bytearray(data), blob_start

def parse_index(data, blob_start):
    """Return list of (slot_idx, id, off) for all valid index slots."""
    out = []
    for i in range(blob_start // 16):
        raw_len, ident, off, pad = struct.unpack_from("<IIII", data, i*16)
        if blob_start <= off < len(data):
            out.append((i, ident, off, raw_len, pad))
    return out

def text_of(data, off, next_off):
    raw = bytes(data[off+4:next_off+4])
    while len(raw) >= 2 and raw[-2:] == b"\x00\x00":
        raw = raw[:-2]
    return raw.decode("utf-16-le", errors="replace")

def show_list(data, blob_start, n=30):
    rows = parse_index(data, blob_start)
    rows.sort(key=lambda r: r[2])
    print(f"{'slot':>5}  {'id':>10}  {'offset':>10}  {'text (first 60 chars)':<60}")
    for k in range(min(n, len(rows))):
        slot_i, ident, off, _, _ = rows[k]
        nxt = rows[k+1][2] if k+1 < len(rows) else len(data) - 4
        t = text_of(data, off, nxt)[:60]
        print(f"  {slot_i:>3}  {ident:>10}  {off:>#08x}  {t!r}")

def grep(data, blob_start, needle):
    rows = parse_index(data, blob_start)
    rows.sort(key=lambda r: r[2])
    nl = needle.lower()
    hits = 0
    for k in range(len(rows)):
        slot_i, ident, off, _, _ = rows[k]
        nxt = rows[k+1][2] if k+1 < len(rows) else len(data) - 4
        t = text_of(data, off, nxt)
        if nl in t.lower():
            print(f"  id={ident:>10}  text={t[:120]!r}")
            hits += 1
    print(f"\n{hits} match(es)")

def modify(target_id, new_text):
    """Replace the text at the given id with new_text (UTF-16 LE).
    Updates all subsequent offsets if length changes."""
    data, blob_start = load()
    rows = parse_index(data, blob_start)
    rows.sort(key=lambda r: r[2])

    # find target slot index in offset-order
    target_k = -1
    for k, (slot_i, ident, off, _, _) in enumerate(rows):
        if ident == target_id:
            target_k = k
            target_off = off
            target_slot = slot_i
            break
    if target_k < 0:
        print(f"id {target_id} not found"); return 1

    nxt_off = rows[target_k+1][2] if target_k+1 < len(rows) else len(data) - 4
    old_raw_size = (nxt_off + 4) - (target_off + 4)
    # Build new payload: UTF-16 LE + \0\0 terminator
    new_raw = new_text.encode("utf-16-le") + b"\x00\x00"
    # Pad to multiple-of-2 (already is) — but match original padding style:
    # most entries end with \0\0 only; ours does too.
    new_raw_size = len(new_raw)

    print(f"target id={target_id} slot={target_slot} off={target_off:#x}")
    old_text = text_of(data, target_off, nxt_off)
    print(f"  old: {old_text!r} ({old_raw_size} bytes)")
    print(f"  new: {new_text!r}  ({new_raw_size} bytes)")
    delta = new_raw_size - old_raw_size

    if not os.path.exists(BAK):
        shutil.copyfile(GR, BAK)
        print(f"  wrote backup -> {BAK}")

    # Construct new data:
    head = bytes(data[: target_off + 4])
    tail = bytes(data[target_off + 4 + old_raw_size:])
    new_data = bytearray(head + new_raw + tail)

    # Adjust all subsequent slot offsets by `delta`.
    if delta != 0:
        for k in range(target_k+1, len(rows)):
            slot_i, ident, off, raw_len, pad = rows[k]
            slot_base = slot_i * 16
            new_off = off + delta
            struct.pack_into("<I", new_data, slot_base + 8, new_off)
        print(f"  shifted {len(rows) - target_k - 1} subsequent offsets by {delta:+}")

    with open(GR, "wb") as f:
        f.write(new_data)
    print(f"  wrote {GR}  ({len(new_data):,} bytes)")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Modify Sacred global.res")
    ap.add_argument("--list", action="store_true", help="list first 30 entries")
    ap.add_argument("--grep", metavar="TEXT", help="find by text content")
    ap.add_argument("--by-name", metavar="NAME", help="symbolic name; hashed via sacred_hash")
    ap.add_argument("--by-id",   type=int, metavar="N",
                    help="numeric id (decimal); will be stringified+hashed like Sacred does")
    ap.add_argument("--by-hash", type=int, metavar="H", help="raw hash key")
    ap.add_argument("--to",      metavar="TEXT", help="replacement text")
    args = ap.parse_args()

    if args.list:
        data, blob_start = load()
        show_list(data, blob_start, 30); return
    if args.grep:
        data, blob_start = load()
        grep(data, blob_start, args.grep); return

    if not args.to:
        ap.error("--to is required for modify mode")
    if args.by_name:
        target = sacred_hash(args.by_name)
        print(f"by-name '{args.by_name}' -> hash {target}")
    elif args.by_id is not None:
        target = hash_for_id(args.by_id)
        print(f"by-id {args.by_id} -> hash {target}")
    elif args.by_hash is not None:
        target = args.by_hash
    else:
        ap.error("specify --by-name / --by-id / --by-hash")

    sys.exit(modify(target, args.to))

if __name__ == "__main__":
    main()
