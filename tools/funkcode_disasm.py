"""Sacred FunkCode disassembler — built from the interpreter dispatch table.

Reads `bin\<class>\FunkCode.bin` and emits a textual opcode trace. Per
FUN_00472bc0 analysis (interpreter_parse.py / interpreter_payload.py):

  bytecode := sequence of (opcode:u8 [payload]) records
  opcode kinds:
    - stack ops    : 1 byte total, no payload (group 6 — 69 opcodes)
    - const ops    : 1+N bytes with static N (groups 3,5,8,9,13,19,20,21,30,35,36,39,40)
    - cstr ops     : 1+strlen+1 + ... bytes (variable; e.g. dialog dispatcher 0x01)
    - complex ops  : multi-record bodies (allocation, jumps, etc.)

This first version handles stack ops + const ops cleanly. cstr/complex ops
are tagged with their "cluster" so a human reader can recognise them; full
operand decoding for these is the next iteration.
"""
import sys, os, struct, argparse, re, collections
sys.path.insert(0, os.path.dirname(__file__))
from funkcode_tags import label_for as tag_label
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

# --- Opcode table (extracted from interpreter_payload.py analysis) ---
# Group ids match interpreter_payload.py output. We carry the "kind" (stack,
# const, cstr, complex) and the payload width.

OPCODE_TABLE = {
    # group, kind, width-extra-bytes, label
    # group 1 — DIALOG dispatcher (variable, 2 cstrings each + work)
    0x01: (1, "cstr2",  None, "DLG_OP_a"),
    0x29: (1, "cstr2",  None, "DLG_OP_b"),
    0x63: (1, "cstr2",  None, "DLG_OP_c"),
    0x68: (1, "cstr2",  None, "DLG_OP_d"),
    0x69: (1, "cstr2",  None, "DLG_OP_e"),
    0x6A: (1, "cstr2",  None, "DLG_OP_f"),
    0x9D: (1, "cstr2",  None, "DLG_OP_g"),
    # group 4 — hero/CPOS dialog ops (+5 bytes static? actually variable - has cstr inside)
    0x04: (4, "cstr1+5", 5, "HERO_OP_a"),
    0x0C: (4, "cstr1+5", 5, "HERO_OP_b"),
    0x0D: (4, "cstr1+5", 5, "HERO_OP_c"),
    # Note: `width` is BYTES OF PAYLOAD AFTER THE OPCODE (so total ip advance = 1 + width).
    # group 5 — math/dispatch ops, total advance +1 (no payload bytes)
    0x05: (5, "const",   0, "MATH_a"),
    0x09: (5, "const",   0, "MATH_b"),
    0x41: (5, "const",   0, "MATH_c"),
    0x60: (5, "const",   0, "MATH_d"),
    0x83: (5, "const",   0, "MATH_e"),
    # group 3 — 1 opcode + 2 payload bytes (advance +3 total)
    0x03: (3, "const",   2, "FMT3_a"),
    0x0A: (3, "const",   2, "FMT3_b"),
    0x6B: (3, "const",   2, "FMT3_c"),
    0x6C: (3, "const",   2, "FMT3_d"),
    0x9B: (3, "const",   2, "FMT3_e"),
    0x9C: (3, "const",   2, "FMT3_f"),
    # group 8/13 — 1 opcode + 4 payload (advance +5 total) — u32 payload
    0x11: (8, "const",   4, "U32_a"),
    0x36: (8, "const",   4, "U32_b"),
    0x75: (8, "const",   4, "U32_c"),
    0x1C: (13,"const",   4, "U32_d"),
    0x53: (13,"const",   4, "U32_e"),
    0x54: (13,"const",   4, "U32_f"),
    0x55: (13,"const",   4, "U32_g"),
    0x56: (13,"const",   4, "U32_h"),
    0x57: (13,"const",   4, "U32_i"),
    0x86: (13,"const",   4, "U32_j"),
    0x7E: (35,"const",   4, "U32_k"),
    0x7F: (35,"const",   4, "U32_l"),
    0x8C: (35,"const",   4, "U32_m"),
    0x90: (35,"const",   4, "U32_n"),
    0x84: (36,"const",   4, "U32_o"),
    # group 9/19/20 — 1 opcode + 8 payload (advance +9 total) — u32 pair
    0x15: (9, "const",   8, "U32PAIR_a"),
    0x33: (19,"const",   8, "U32PAIR_b"),
    0x87: (19,"const",   8, "U32PAIR_c"),
    0x88: (19,"const",   8, "U32PAIR_d"),
    0x89: (19,"const",   8, "U32PAIR_e"),
    0x34: (20,"const",   8, "U32PAIR_f"),
    0x79: (20,"const",   8, "U32PAIR_g"),
    # group 16 — 1 opcode + 3 payload (advance +4 total)
    0x1F: (16,"const",   3, "C3_a"),
    0x9F: (40,"const",   3, "C3_b"),
    # group 18 — 1 opcode + 1 payload (advance +2 total)
    0x28: (18,"const",   1, "C1_a"),
    # group 39 — 1 opcode + 2 payload (advance +3 total)
    0x93: (39,"const",   2, "FMT3_g"),
    # group 7 — 0x0b/0x38/0x5f: read u32. If u32 == -0x12d687 magic, additionally cstring follows.
    # For disassembler we treat as fixed-width u32 (4 bytes) — magic-cstring case will misalign on those records (rare).
    0x0B: (7, "const",   4, "U32_qid_a"),
    0x38: (7, "const",   4, "U32_qid_b"),
    0x5F: (7, "const",   4, "U32_qid_c"),
    # group 30 — +1 (cstr-extra)
    0x47: (30,"const",   1, "STR_LOOKUP_a"),
    0x52: (30,"const",   1, "STR_LOOKUP_b"),
    0x7D: (30,"const",   1, "STR_LOOKUP_c"),
    0x8F: (30,"const",   1, "STR_LOOKUP_d"),
    # group 28 — +1 variable-length cstring lookup
    0x3E: (28,"cstr1+1", 1, "VAR_LOOKUP_a"),
    0x77: (28,"cstr1+1", 1, "VAR_LOOKUP_b"),
    0x81: (28,"cstr1+1", 1, "VAR_LOOKUP_c"),
    0x82: (28,"cstr1+1", 1, "VAR_LOOKUP_d"),
    # group 29 — +1 var lookup (res dictionary)
    0x40: (29,"cstr1+1", 1, "RES_LOOKUP_a"),
    # 0x95: cstr1 only (single string, optional "res:" prefix)
    0x95: (29,"cstr1",   0, "ResLookup_95"),
    # opcode 0x00: dispatcher's `default:` case — returns 0 (END signal to caller)
    0x00: (0, "halt",    0, "END"),
    # group 22 — bare break (1-line)
    0x37: (22,"halt",    0, "HALT"),
    0x4B: (22,"halt",    0, "BREAK"),
    # group 24 — 0x3a reads 2 cstrings (potential "res:" prefix on first)
    0x3A: (24,"cstr2",   0, "ResLookup_3a"),
    # group 23 — 0x71 default path just advances ip (joined body, inner branch unreachable for 0x71)
    0x71: (23,"const",   0, "InternalGoto_71"),
    # group 33 — 0x6f: u32 + cstr1 + cstr2
    0x6F: (33,"u32+cstr2", 4, "ResLookup_6f"),
    # group 34 — 0x7a: u32 + cstr1 (single cstring, may start with "res:")
    0x7A: (34,"u32+cstr1", 4, "ResLookup_7a"),
    # group 14 — 0x1d (resource lookup w/ cstring)
    0x1D: (14,"cstr1",   0, "RES_LOOKUP_C"),
    # group 15 — 0x1e (string-name reference)
    0x1E: (15,"cstr1",   0, "STR_REF"),
    # group 10 — 0x16: cstring + appends magic 0x12341234 to vector (block-start marker)
    0x16: (10,"cstr1",   0, "BlockMarker"),
    # (group 7 entries 0x0B/0x38/0x5F already defined above as U32_qid_*)
    # group 38 — 0x92 (+1)
    0x92: (38,"cstr1+1", 1, "CMD_92"),
    # group 32 — 0x67 (152 lines)
    0x67: (32,"complex", None, "VAR_OP_67"),
    # group 26 — 0x3c: u32 (with optional cstr if u32 == -2 magic; we read u32 only for safe alignment)
    0x3C: (26,"const",   4, "HERO_REF"),
    # group 21 — 0x35
    0x35: (21,"const",   0, "CMD_35"),
    # group 12 — 0x19: u32 triple (1 opcode + 12 bytes = 3 u32s, total advance +13)
    0x19: (12,"const",   12,"U32_TRIPLE"),
    # group 25 — 0x3b, 0x73
    0x3B: (25,"complex", None, "CMD_3B"),
    0x73: (25,"complex", None, "CMD_73"),
    # group 31 — u32 + cstr1 (text emission with id)
    0x48: (31,"u32+cstr1", 4, "EMIT_a"),
    0x49: (31,"u32+cstr1", 4, "EMIT_b"),
    0x4A: (31,"u32+cstr1", 4, "EMIT_c"),
    0x5D: (31,"u32+cstr1", 4, "EMIT_d"),
    0x5E: (31,"u32+cstr1", 4, "EMIT_e"),
    0x6D: (31,"u32+cstr1", 4, "EMIT_f"),
    0x6E: (31,"u32+cstr1", 4, "EMIT_g"),
    # group 2 — 0x02: u32 (+5 total)
    0x02: (2, "const",   4, "U32_TRG"),
    # group 17 — 0x20, 0x2a, 0x4d: u32 triple xyz coords (+13 total)
    0x20: (17,"const",  12, "XYZ_a"),
    0x2A: (17,"const",  12, "XYZ_b"),
    0x4D: (17,"const",  12, "XYZ_c"),
    # 0x3d: u32 quad (rect / 4-int packet, +17 total)
    0x3D: (27,"const",  16, "U32_QUAD"),
    # 0x3b, 0x73: u32 (+5 total) — single integer parameter
    0x3B: (25,"const",   4, "U32_3b"),
    0x73: (25,"const",   4, "U32_73"),
    # group 32 — 0x67: cstring (variable lookup)
    0x67: (32,"cstr1",   0, "VAR_LOOKUP_67"),
    # group 37 — 0x8b: u8 (+2 total)
    0x8B: (37,"const",   1, "U8_8b"),
    # group 6 — bare stack ops (no payload, +1)
}
STACK_OPS = {0x06,0x07,0x08,0x0e,0x0f,0x10,0x12,0x13,0x14,0x1a,0x1b,0x1f,
             0x23,0x24,0x25,0x26,0x27,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,
             0x39,0x3f,0x42,0x43,0x44,0x45,0x46,0x4c,0x4e,0x4f,0x50,0x51,
             0x58,0x59,0x5a,0x5b,0x5c,0x61,0x62,0x64,0x65,0x66,0x70,0x72,0x74,
             0x78,0x7b,0x7c,0x80,0x85,0x8a,0x8d,0x8e,0x91,0x96,0x97,0x98,0x99,
             0x9a,0x9e,0xa0,0xa1}
for op in STACK_OPS:
    OPCODE_TABLE[op] = (6, "stack", 0, f"STACK_{op:02X}")

def disasm_one(data, ip):
    """Decode one opcode at `data[ip]` and return (label, payload_repr, next_ip).
    Returns (None, error_string, ip+1) on unknown / malformed."""
    if ip >= len(data):
        return None, "(EOF)", ip + 1
    op = data[ip]
    info = OPCODE_TABLE.get(op)
    if info is None:
        return None, f"UNK_{op:02X}", ip + 1
    group, kind, width, label = info

    if kind == "stack":
        return label, "", ip + 1
    if kind == "halt":
        return label, "", ip + 1
    if kind == "const":
        if width is None or ip + 1 + width > len(data):
            return label, f"(truncated, +{width})", min(len(data), ip + 1 + (width or 1))
        payload = data[ip+1: ip+1+width]
        if width == 4:
            v = int.from_bytes(payload, "little")
            pr = f"u32={v}"
        elif width == 8:
            v1 = int.from_bytes(payload[:4], "little")
            v2 = int.from_bytes(payload[4:], "little")
            pr = f"u32a={v1} u32b={v2}"
        elif width == 12:
            v1 = int.from_bytes(payload[0:4], "little")
            v2 = int.from_bytes(payload[4:8], "little")
            v3 = int.from_bytes(payload[8:12], "little")
            pr = f"x={v1} y={v2} z={v3}"
        elif width == 16:
            v1 = int.from_bytes(payload[0:4], "little")
            v2 = int.from_bytes(payload[4:8], "little")
            v3 = int.from_bytes(payload[8:12], "little")
            v4 = int.from_bytes(payload[12:16], "little")
            pr = f"a={v1} b={v2} c={v3} d={v4}"
        elif width == 2:
            v = int.from_bytes(payload, "little")
            pr = f"u16={v}"
        elif width == 1:
            pr = f"u8={payload[0]}"
        else:
            pr = " ".join(f"{b:02x}" for b in payload)
        return label, pr, ip + 1 + width
    if kind.startswith("cstr") or kind.startswith("u32+cstr"):
        # `u32+cstrN` first reads a 4-byte u32 right after the opcode,
        # then reads N cstrings.  `cstrN` reads N cstrings only.
        cur = ip + 1
        u32_val = None
        if kind.startswith("u32+cstr"):
            if cur + 4 > len(data):
                return label, "(u32 truncated)", len(data)
            u32_val = int.from_bytes(data[cur:cur+4], "little")
            cur += 4
        strs = []
        if kind in ("cstr2", "u32+cstr2"):
            n_str = 2
        else:
            n_str = 1
        for _ in range(n_str):
            end = data.find(0, cur)
            if end < 0:
                strs.append(data[cur:cur+32].decode("latin1","replace") + "...")
                cur = len(data)
                break
            s = data[cur:end].decode("latin1","replace")
            strs.append(s)
            cur = end + 1
        # static extra width if specified
        if kind.endswith("+1"): cur += 1
        elif kind.endswith("+5"): cur += 5
        prefix = f"{u32_val} | " if u32_val is not None else ""
        payload = prefix + ", ".join(repr(s) for s in strs)
        return label, payload, cur
    if kind == "complex":
        # We can't size complex opcodes from this analysis yet — just emit the
        # label and advance by 1 byte so we eventually resync on the next
        # opcode. This will produce misalignments inside complex bodies but is
        # the only safe action without sub-record decoding.
        return label, "(complex; size unknown — synthesised +1)", ip + 1
    return None, "(unimplemented)", ip + 1

def walk_records(buf):
    """Top-level record walker per docs/05-funkcode-grammar.md:
       [tag:u8][size:u16 BE][payload of size-3 bytes]
       Stops cleanly when a malformed record is hit."""
    off = 0
    while off + 3 <= len(buf):
        tag = buf[off]
        size = (buf[off+1] << 8) | buf[off+2]
        if size < 3 or off + size > len(buf):
            return
        yield off, tag, size, buf[off+3: off+size]
        off += size

def disasm_payload(payload, indent=4, limit=None):
    """Disassemble one record's payload as a sequence of opcodes.

    payload[0] is a `flags` byte that is NOT interpreted (per FUN_00491170
    caller setting ip=4, which is record-relative; payload begins at record
    offset 3, so payload-relative ip = 1).
    """
    histogram = collections.Counter()
    if payload:
        # Show flags byte separately
        out_flags = f"{' ' * indent}flags=0x{payload[0]:02x}"
    else:
        out_flags = ""
    ip = 1   # skip flags byte
    n = 0
    out = []
    if out_flags:
        out.append(out_flags)
    while ip < len(payload):
        if limit is not None and n >= limit: break
        op = payload[ip]
        label, pr, next_ip = disasm_one(payload, ip)
        histogram[label or f"UNK_{op:02X}"] += 1
        sp = " " * indent
        out.append(f"{sp}+{ip:04x}  {op:02x}  {label or 'UNK':<18} {pr}")
        if next_ip <= ip:
            break
        ip = next_ip
        n += 1
    return out, histogram, ip

def disasm_file(path, limit_records=20, ops_per_record=200, dump_payload=True):
    data = open(path, "rb").read()
    print(f"# {path}")
    print(f"# {len(data):,} bytes")
    global_hist = collections.Counter()
    record_tag_hist = collections.Counter()

    for i, (off, tag, size, payload) in enumerate(walk_records(data)):
        record_tag_hist[tag] += 1
        if i >= limit_records:
            continue
        # decode the tag character (records use ASCII printable tags mostly)
        tag_ch = chr(tag) if 32 <= tag <= 126 else "?"
        label = tag_label(tag)
        print(f"\n{off:08x}  RECORD tag=0x{tag:02x} '{tag_ch}'  [{label}]  size={size}  payload={size-3}B")
        if not dump_payload: continue
        ops, hist, end_ip = disasm_payload(payload, indent=4, limit=ops_per_record)
        for ln in ops:
            print(ln)
        if end_ip < len(payload):
            print(f"    ... ({len(payload) - end_ip} more bytes of payload not shown)")
        for k, v in hist.items():
            global_hist[k] += v

    print(f"\n# === record-tag histogram (full file) ===")
    for t, c in record_tag_hist.most_common(30):
        tch = chr(t) if 32 <= t <= 126 else "?"
        print(f"#   tag=0x{t:02x} '{tch}'  [{tag_label(t):<22}]  : {c:,}")
    print(f"#")
    print(f"# === opcode histogram (limited window) ===")
    for lbl, c in global_hist.most_common(25):
        print(f"#   {c:>6}  {lbl}")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("file", help="path to FunkCode.bin")
    ap.add_argument("--records", type=int, default=12,
                    help="how many records to dump in full")
    ap.add_argument("--ops",     type=int, default=200,
                    help="max ops per record")
    args = ap.parse_args()
    disasm_file(args.file, limit_records=args.records, ops_per_record=args.ops)
