"""Probe value-type tags inside record payloads.

Hypothesis from samples:
  payload := flags:u8 (=0x00)  field*
  field   := type:u8 value:(...)

Known types so far:
  0x0b → u32 LE
  0x16 → ASCIIZ
  0x1c → u32 LE (key)
  0x38 → u32 LE
  0x86 → u32 LE
  0x01 → "name follows as cstr" (special marker, introduces a named decl)

This script walks all records, attempts to parse fields, and tabulates which
type bytes are seen, what sizes follow them, and what example values look like.
"""
import os, collections, struct

PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_SERAPHIM\FunkCode.bin"
data = open(PATH, "rb").read()

# Re-parse outer TLV
records = []
off = 0
while off + 3 <= len(data):
    tag = data[off]
    size = (data[off+1] << 8) | data[off+2]
    if size < 3 or off + size > len(data):
        break
    records.append((off, tag, size, data[off+3:off+size]))
    off += size

# Known parsers: pretend u32-LE for "small int types", cstr for 0x16, name-after-0x01.
INT_TYPES = {0x0b, 0x1c, 0x38, 0x86, 0x14, 0x15, 0x4a, 0x12, 0x49, 0x4e}  # guess wider net

def parse_fields(payload):
    """Walk a payload after the flags byte, return list of (type, raw bytes, decoded)."""
    out = []
    if not payload:
        return out
    flags = payload[0]
    p = 1
    while p < len(payload):
        t = payload[p]
        p += 1
        # try u32 LE
        if t in INT_TYPES and p + 4 <= len(payload):
            v = struct.unpack_from("<I", payload, p)[0]
            out.append((t, payload[p:p+4], v))
            p += 4
            continue
        # try cstr for 0x16 or 0x01
        if t in (0x16, 0x01) and p < len(payload):
            end = payload.find(b"\x00", p)
            if 0 < end <= len(payload):
                s = payload[p:end].decode("latin1", "replace")
                out.append((t, payload[p:end+1], s))
                p = end + 1
                continue
        # unknown type → bail out, record as raw remainder
        out.append((t, payload[p:], None))
        break
    return out, flags

# 1) Count "type byte" occurrences across all records, and how often we managed to fully parse the payload.
type_hist = collections.Counter()
type_sizes = collections.defaultdict(collections.Counter)  # type -> Counter of consumed lengths
type_examples = collections.defaultdict(list)
full_parsed = 0
total_with_fields = 0
for off_r, tag, size, payload in records:
    if len(payload) <= 1:
        continue
    total_with_fields += 1
    fields, flags = parse_fields(payload)
    fully_consumed = (sum(1 + len(raw) for _, raw, _ in fields) == len(payload) - 1)
    if fully_consumed:
        full_parsed += 1
    for t, raw, dec in fields:
        type_hist[t] += 1
        type_sizes[t][len(raw)] += 1
        if len(type_examples[t]) < 5 and dec is not None:
            type_examples[t].append((off_r, tag, dec, raw[:8]))

print(f"Records with payload >1: {total_with_fields}, fully parsed: {full_parsed} ({full_parsed/total_with_fields*100:.1f}%)")
print(f"\nType-byte histogram (after flags), top 30:\n")
print(f"  {'type':6}  {'hex':8}  {'count':>8}  sizes(top3)        sample")
for t, c in type_hist.most_common(30):
    sz = ", ".join(f"{s}bx{n}" for s, n in type_sizes[t].most_common(3))
    if type_examples[t]:
        ex_off, ex_tag, ex_val, ex_raw = type_examples[t][0]
        ex_chr = chr(ex_tag) if 32 <= ex_tag < 127 else "?"
        ex_show = repr(ex_val)[:36]
        sample = f"in '{ex_chr}' = {ex_show}"
    else:
        sample = "(raw)"
    print(f"  0x{t:02x}    ({chr(t) if 32<=t<127 else '.'})       {c:>8}  {sz:18}  {sample}")

# 2) Daily-quest template names: dump every cstr inside any 0x1f record
print(f"\n--- All 0x1f records' embedded strings (daily-quest templates) ---")
templates = collections.Counter()
for off_r, tag, size, payload in records:
    if tag != 0x1f:
        continue
    # payload looks like: 00 1c <u32> 16 <cstr>
    if len(payload) >= 8 and payload[1] == 0x1c and payload[6] == 0x16:
        end = payload.find(b"\x00", 7)
        if end > 7:
            name = payload[7:end].decode("latin1", "replace")
            templates[name] += 1
print(f"distinct templates: {len(templates)}, total 0x1f records: {sum(templates.values())}")
for name, c in templates.most_common(40):
    print(f"  {c:>5}  {name}")

# 3) For 0x64 'd' (fixed-19) records, are the three integers (50, X, Y) really invariant on slot 0?
print(f"\n--- 0x64 'd' record field analysis ---")
slot0 = collections.Counter()
slot1 = collections.Counter()
slot2 = collections.Counter()
for off_r, tag, size, payload in records:
    if tag != 0x64 or size != 19:
        continue
    # payload = 00 0b A 0b B 0b C, 16 bytes total
    if len(payload) == 16 and payload[1] == 0x0b and payload[6] == 0x0b and payload[11] == 0x0b:
        a = struct.unpack_from("<I", payload, 2)[0]
        b = struct.unpack_from("<I", payload, 7)[0]
        c = struct.unpack_from("<I", payload, 12)[0]
        slot0[a] += 1
        slot1[b] += 1
        slot2[c] += 1
print(f"slot 0 distinct values: {len(slot0)}, top 8: {slot0.most_common(8)}")
print(f"slot 1 distinct values: {len(slot1)}, top 8: {slot1.most_common(8)}")
print(f"slot 2 distinct values: {len(slot2)}, top 8: {slot2.most_common(8)}")

# 4) Names from 0x43 'C' and 0x69 'i' records (var declarations / inits)
print(f"\n--- Named records (tags 0x43 'C' and 0x69 'i') ---")
named = collections.Counter()
for off_r, tag, size, payload in records:
    if tag not in (0x43, 0x69):
        continue
    if len(payload) >= 3 and payload[1] == 0x01:
        end = payload.find(b"\x00", 2)
        if end > 2:
            n = payload[2:end].decode("latin1", "replace")
            named[n] += 1
print(f"distinct names: {len(named)}")
print(f"first 30 by frequency:")
for n, c in named.most_common(30):
    print(f"  {c:>4}  {n}")
print(f"\nfirst 30 alphabetically:")
for n in sorted(named)[:30]:
    print(f"  {named[n]:>4}  {n}")
