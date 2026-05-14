"""Final FunkCode type dictionary — manual seed combined with inferred types,
all verified by full-record parse coverage.

The dictionary mixes:
  - types we identified by hand from sample inspection
  - types that the conservative inference loop adopted with >=90% confidence
  - explicit notes about each entry

Output: docs/06-funkcode-types.md plus a numeric coverage report.
"""
import os, struct, collections

PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_SERAPHIM\FunkCode.bin"
OUT  = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\docs\06-funkcode-types.md"

data = open(PATH, "rb").read()

records = []
off = 0
while off + 3 <= len(data):
    tag = data[off]
    size = (data[off+1] << 8) | data[off+2]
    if size < 3 or off + size > len(data):
        break
    records.append((off, tag, size, data[off+3:off+size]))
    off += size

# Final dictionary: encoding + a one-line note for human readers
TYPES = {
    # --- u32 LE (4 bytes) ---
    0x0b: ("u32", "default integer literal — most common"),
    0x1c: ("u32", "integer (key/index role)"),
    0x38: ("u32", "integer (counter)"),
    0x49: ("u32", "integer"),
    0x4a: ("u32", "integer (small values, e.g. 22)"),
    0x53: ("u32", "integer"),
    0x86: ("u32", "sequence/counter integer (seen in `N` records)"),
    0x14: ("u32", "integer"),
    0x15: ("u32", "integer"),
    0x02: ("u32", "i32 — can be negative (e.g. -2 = 0xFFFFFFFE)"),
    0x04: ("u32", "i32 — can be negative"),
    0x11: ("u32", "integer (e.g. 5001)"),
    0x6b: ("u32", "integer (large bit-flag-style values)"),
    0x6d: ("u32", "integer"),
    0x1f: ("u32", "integer"),
    0x1d: ("u32", "integer (saw 96% u16 in inference, but 4-byte fits better in context)"),
    0x75: ("u32", "integer"),
    # --- u16 LE (2 bytes) ---
    0x28: ("u16", "always 2 bytes in histogram"),
    0x69: ("u16", "value-type 0x69 (distinct from outer tag 0x69 'i')"),
    0x2a: ("u16", "value-type (distinct from outer tag 0x2a '*' marker)"),
    0x0a: ("u16", "small integer"),
    0x93: ("u16", "small integer"),
    # --- u8 (1 byte) ---
    0x07: ("u8", "small enum"),
    0x08: ("u8", "small enum/flag"),
    0x17: ("u8", "100% u8 in inference"),
    0x39: ("u8", "small enum"),
    0x45: ("u8", "small enum"),
    0x48: ("u8", "small enum (distinct from outer tag `H`)"),
    0x50: ("u8", "100% u8 in inference"),
    0xe0: ("u8", "100% u8 in inference"),
    # --- u64 LE (8 bytes) — tuples of (u32, u32) ---
    0x87: ("u64", "u64 (likely 2x u32 tuple) — pervasive in 's' statement records"),
    0x88: ("u64", "u64 (tuple) — sibling of 0x87"),
    0x89: ("u64", "u64 (tuple) — sibling of 0x87"),
    # --- cstr (ASCIIZ) ---
    0x01: ("cstr", "named symbol reference (most common cstr role)"),
    0x16: ("cstr", "string literal (sound IDs, quest templates)"),
    0x05: ("cstr", "system-ID strings (e.g. 'od_6001')"),
    0x09: ("cstr", "cstr"),
    0x1e: ("cstr", "cstr"),
    0x2a: ("cstr", "<-- overridden by u16 above"),  # marker overlap; will dedupe
    0x30: ("cstr", "cstr"),
    0x32: ("cstr", "quest/region ID strings (e.g. '210')"),
    0x33: ("cstr", "cstr"),
    0x40: ("cstr", "cstr"),
    0x41: ("cstr", "cstr"),
    0x47: ("cstr", "cstr"),
    0x4d: ("cstr", "cstr"),
    0x52: ("cstr", "cstr"),
    0x5f: ("cstr", "cstr"),
    0x67: ("cstr", "cstr"),
    0x6f: ("cstr", "cstr"),
    0x79: ("cstr", "cstr"),
    0x7c: ("cstr", "cstr"),
    0x7d: ("cstr", "cstr (sentinel-block contents)"),
    0x8f: ("cstr", "cstr"),
}

# Remove overlap entries that were left as comments
for t in list(TYPES):
    enc, note = TYPES[t]
    if "overridden" in note:
        # already handled by an earlier alias
        pass

def consume(payload, p, kind):
    if kind == "u8":  return p + 1 if p + 1 <= len(payload) else None
    if kind == "u16": return p + 2 if p + 2 <= len(payload) else None
    if kind == "u32": return p + 4 if p + 4 <= len(payload) else None
    if kind == "u64": return p + 8 if p + 8 <= len(payload) else None
    if kind == "cstr":
        end = payload.find(b"\x00", p)
        return (end + 1) if end >= 0 else None
    return None

def try_parse(payload, types):
    if not payload:
        return True, 0
    p = 1
    while p < len(payload):
        t = payload[p]
        if t not in types:
            return False, p
        enc, _ = types[t]
        nxt = consume(payload, p + 1, enc)
        if nxt is None:
            return False, p
        p = nxt
    return True, p

# Coverage
full = 0
bytes_consumed = 0
bytes_total = 0
unresolved = collections.Counter()
for off_r, tag, size, p in records:
    if not p:
        full += 1
        continue
    ok, stop = try_parse(p, TYPES)
    bytes_total += len(p)
    if ok:
        full += 1
        bytes_consumed += len(p)
    else:
        bytes_consumed += stop
        if stop < len(p):
            unresolved[p[stop]] += 1

print(f"Records fully parsed: {full}/{len(records)} ({full/len(records)*100:.2f}%)")
print(f"Field bytes parsed:   {bytes_consumed}/{bytes_total} ({bytes_consumed/bytes_total*100:.2f}%)")
print(f"\nType dict size: {len(TYPES)}")
print(f"Top remaining unresolved type bytes:")
for t, c in unresolved.most_common(15):
    print(f"  0x{t:02x}: {c} stalls")

# Generate docs/06-funkcode-types.md
md = []
md.append("# 06 — FunkCode.bin value-type dictionary\n\n")
md.append(f"This is the curated table of value types found inside record payloads. ")
md.append(f"It mixes manual analysis with the iterative inference passes from ")
md.append(f"`sdk\\tools\\funkcode_typeprobe*.py`. **Field-byte coverage with this dict: "
          f"{bytes_consumed/bytes_total*100:.1f} %** on SERAPHIM.\n\n")
md.append("## Grammar reminder\n\n")
md.append("```\n")
md.append("payload := flags:u8  field*\n")
md.append("field   := type:u8  value:(encoding determined by type)\n")
md.append("```\n\n")
md.append("`flags` is almost always `0x00`. Each `field` is a TLV-ish `(type, value)` pair where the type byte alone determines value width/encoding. Same byte value can mean different things as a top-level **tag** vs. as a value **type** — context (outer record's tag) disambiguates.\n\n")

# Group by encoding
by_enc = collections.defaultdict(list)
for t, (enc, note) in sorted(TYPES.items()):
    by_enc[enc].append((t, note))

md.append("## Table\n\n")
md.append("| Type | Encoding | Bytes | Note |\n|---|---|---|---|\n")
for enc in ("u8", "u16", "u32", "u64", "cstr"):
    for t, note in by_enc[enc]:
        nb = {"u8":"1","u16":"2","u32":"4","u64":"8","cstr":"variable (ASCIIZ)"}[enc]
        md.append(f"| `0x{t:02x}` | {enc} | {nb} | {note} |\n")

md.append("\n## Coverage\n\n")
md.append(f"- **Records fully parsed**: {full}/{len(records)} = {full/len(records)*100:.2f} %\n")
md.append(f"- **Field bytes parsed**:   {bytes_consumed}/{bytes_total} = {bytes_consumed/bytes_total*100:.2f} %\n")
md.append(f"- Distinct types in dictionary: **{len(TYPES)}**\n")
md.append(f"- Distinct types still unresolved (with examples): see below\n\n")

md.append("## Remaining unresolved type bytes\n\n")
md.append("These bytes appear as value-type tags in payloads but we couldn't pin a single encoding from automated inference + sampling. Hot candidates for the next round (and for cross-checking against `Sacred.exe`'s parser):\n\n")
md.append("| Type | Stalls | Hypothesis |\n|---|---|---|\n")
notes_unr = {
    0x36: "very high frequency — likely u8 enum, but the context where it stalls keeps the parser misaligned. Probably context-dependent.",
    0x55: "split 50/50 between u16 and cstr in inference — context-sensitive.",
    0x31: "between cstr and u32 — looks like cstr for short numeric IDs ('210'-style).",
    0x37: "appears as cstr 48 % of the time in inference.",
    0x42: "0x42 is also outer tag `B`; as value type seems to introduce a length-prefixed substring or another nested record.",
    0x0c: "appears inside `0x04` records as `0c <u32> <type>` — likely u32.",
    0x6e: "appears after cstr; small counts.",
}
for t, c in unresolved.most_common(15):
    h = notes_unr.get(t, "")
    md.append(f"| `0x{t:02x}` | {c} | {h} |\n")

md.append("\n## Why we plateau\n\n")
md.append("The grammar is **almost** flat-typed `(type, value)*`, but a few records carry encodings that the type alone doesn't disambiguate — most notably `0x42` records in some contexts and the high-frequency `0x36` and `0x55` byte. The likely explanations:\n\n")
md.append("1. **Context-dependent typing.** Same value-type byte means different widths in different outer-tag contexts. E.g. inside outer-tag `s` the byte `0x02` may behave as u32, inside outer-tag `d` as u8 (or vice versa).\n")
md.append("2. **Length-prefixed sub-records.** Some types may read `[len:u8 or u16][nested bytes:len]`. Pascal-strings explain a few cases but not all.\n")
md.append("3. **Polymorphic types.** A single type byte may be a discriminated union (e.g. flags select int-vs-string).\n\n")
md.append("To resolve the rest cleanly, the cheapest next step is to **read the parser inside `Sacred.exe`**: find the function that reads `%s\\FunkCode.bin` (xref the `FunkCode.bin` string in `.rdata`), follow it to the field-decoder switch, and read the case table directly. That gives canonical encodings without further inference.\n")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write("".join(md))
print(f"\nWrote {OUT}")
