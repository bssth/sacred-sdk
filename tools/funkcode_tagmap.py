"""Build a tag map for FunkCode.bin.

Outputs Markdown to docs/04-funkcode-tags.md with:
  - Tag table: counts, size distribution, %ascii, name-detection, bigram context.
  - Three sample records per tag (smallest / median / largest).

Schema corrections from v1:
  - Subtag `0x01` lives at payload[1], not payload[0]. Payload[0] is a leading flags/version byte
    that appears to be 0x00 almost always.
  - "Marker" tags have payload <= 3 bytes (just a u8/u16 inside).
  - "Fixed-record" tags always have one size (variance == 0).
"""
import os, struct, collections, statistics

PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_SERAPHIM\FunkCode.bin"
OUT  = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\docs\04-funkcode-tags.md"

data = open(PATH, "rb").read()

# --- parse ---
records = []
off = 0
while off + 3 <= len(data):
    tag = data[off]
    size = (data[off+1] << 8) | data[off+2]
    if size < 3 or off + size > len(data):
        break
    records.append((off, tag, size, data[off+3:off+size]))
    off += size

N = len(records)
print(f"Parsed {N} records, coverage {off:#x}/{len(data):#x}")

by_tag = collections.defaultdict(list)
for r in records:
    by_tag[r[1]].append(r)

# bigrams
pred = collections.defaultdict(collections.Counter)
succ = collections.defaultdict(collections.Counter)
for i, (_, tag, _, _) in enumerate(records):
    if i > 0:
        pred[tag][records[i-1][1]] += 1
    if i + 1 < N:
        succ[tag][records[i+1][1]] += 1

def char(t):
    return chr(t) if 32 <= t < 127 else "?"

def hexs(b, n=24):
    return " ".join(f"{x:02x}" for x in b[:n])

def asciis(b, n=24):
    return "".join(chr(x) if 32 <= x < 127 else "." for x in b[:n])

def looks_like_name(s):
    if not 2 <= len(s) <= 80:
        return False
    return all(c.isalnum() or c in "_.:" for c in s)

def payload_shape(samples):
    """Return per-tag aggregate features."""
    has_name_at_1 = 0
    has_name_at_0 = 0
    ascii_count = 0
    head_byte = collections.Counter()
    for _, _, _, p in samples:
        head_byte[p[0] if p else None] += 1
        if not p:
            continue
        # ascii-ish
        printable = sum(1 for x in p if 32 <= x < 127)
        if printable / len(p) >= 0.5:
            ascii_count += 1
        # cstr after subtag 0x01 at offset 1
        if len(p) >= 3 and p[1] == 0x01:
            end = p.find(b"\x00", 2)
            if 1 < end <= min(80, len(p)):
                name = p[2:end].decode("latin1", "replace")
                if looks_like_name(name):
                    has_name_at_1 += 1
        # cstr immediately at offset 0 (no subtag)
        if 32 <= p[0] < 127:
            end = p.find(b"\x00", 0)
            if 1 < end <= min(80, len(p)):
                name = p[:end].decode("latin1", "replace")
                if looks_like_name(name):
                    has_name_at_0 += 1
    return has_name_at_1, has_name_at_0, ascii_count, head_byte

def categorize(sizes, count):
    if len(set(sizes)) == 1:
        s = sizes[0]
        if s <= 4:
            return "marker"
        if s <= 6:
            return "tiny-fixed"
        return f"fixed-{s}"
    if max(sizes) - min(sizes) <= 4:
        return "narrow"
    return "variable"

# --- markdown ---
md = []
md.append("# 04 — FunkCode.bin tag map\n")
md.append(f"Auto-generated from `bin\\TYPE_NPC_SERAPHIM\\FunkCode.bin` ({N} top-level records).\n")
md.append("Re-run via `python sdk\\tools\\funkcode_tagmap.py`.\n\n")
md.append("## Schema reminders\n")
md.append("- Framing: `[tag:u8][size:u16 BE][payload : size-3]`.\n")
md.append("- payload[0] is a flags/version byte (almost always 0x00).\n")
md.append("- When payload[1] == 0x01, a null-terminated symbol name follows at payload[2..].\n")
md.append("- Tail bytes after the name often encode `(type:u8, value:u32 LE)` pairs; `type=0x0b` is "
          "the only one we've confirmed so far (u32 integer).\n\n")
md.append("## Tag table\n")
md.append("Sorted by frequency.\n\n")
md.append("| Tag | Char | Count | % | Sizes (min..max, med) | Cat | %ascii | %named | Top preds | Top succs |\n")
md.append("|---|---|---|---|---|---|---|---|---|---|\n")

table_rows = []
for tag, recs in sorted(by_tag.items(), key=lambda kv: -len(kv[1])):
    sizes = [r[2] for r in recs]
    named1, named0, asc, head_byte = payload_shape(recs)
    cnt = len(recs)
    pct = cnt / N * 100
    named_pct = max(named1, named0) / cnt * 100
    asc_pct = asc / cnt * 100
    cat = categorize(sizes, cnt)
    pred_top = ", ".join(f"`{chr(t) if 32<=t<127 else '.'}`={c}" for t, c in pred[tag].most_common(3))
    succ_top = ", ".join(f"`{chr(t) if 32<=t<127 else '.'}`={c}" for t, c in succ[tag].most_common(3))
    table_rows.append((tag, recs, cnt, pct, sizes, cat, asc_pct, named_pct, pred_top, succ_top))
    md.append(
        f"| `0x{tag:02x}` | `{char(tag)}` | {cnt} | {pct:.2f}% | "
        f"{min(sizes)}..{max(sizes)}, {int(statistics.median(sizes))} | {cat} | "
        f"{asc_pct:.0f}% | {named_pct:.0f}% | {pred_top} | {succ_top} |\n"
    )

# --- category summary ---
md.append("\n## By category\n\n")
cats = collections.defaultdict(list)
for tag, _, cnt, _, sizes, cat, *_ in table_rows:
    cats[cat].append((tag, cnt, sizes))

for cat in sorted(cats):
    md.append(f"### {cat}\n")
    for tag, cnt, sizes in sorted(cats[cat], key=lambda x: -x[1]):
        md.append(f"- `0x{tag:02x} {char(tag)}` ×{cnt}, size={min(sizes)}..{max(sizes)}\n")
    md.append("\n")

# --- deterministic chain detection ---
md.append("\n## Deterministic bigram chains (>= 95% next-token agreement)\n\n")
md.append("These tag-to-tag transitions happen >=95% of the time at the source side. "
          "They mark fixed-shape sub-records embedded in the stream.\n\n")
md.append("| From | To | hits | of total `from` |\n|---|---|---|---|\n")
chain_rows = []
for tag in sorted(by_tag):
    total = len(by_tag[tag])
    if total < 10:
        continue
    for nxt, cnt in succ[tag].most_common(1):
        if cnt / total >= 0.95:
            chain_rows.append((tag, nxt, cnt, total))
            md.append(f"| `0x{tag:02x} {char(tag)}` | `0x{nxt:02x} {char(nxt)}` | {cnt} | {cnt}/{total} ({cnt/total*100:.0f}%) |\n")

# --- per-tag samples ---
md.append("\n## Per-tag samples\n\n")
md.append("Three records per tag: smallest, median, largest size in file. "
          "If `payload[1] == 0x01` we decode the embedded symbol name.\n\n")

for tag, recs, cnt, pct, sizes, cat, *_ in table_rows:
    recs_sorted = sorted(recs, key=lambda r: r[2])
    picks = []
    if recs_sorted: picks.append(("min", recs_sorted[0]))
    if len(recs_sorted) >= 3: picks.append(("med", recs_sorted[len(recs_sorted)//2]))
    if len(recs_sorted) >= 2 and recs_sorted[-1] is not recs_sorted[0]:
        picks.append(("max", recs_sorted[-1]))
    md.append(f"### `0x{tag:02x}` `{char(tag)}` — {cnt} records ({pct:.2f}%), cat={cat}\n\n")
    for kind, (off_r, _, size, payload) in picks:
        name = ""
        if len(payload) >= 3 and payload[1] == 0x01:
            end = payload.find(b"\x00", 2)
            if 1 < end <= min(80, len(payload)):
                try:
                    n = payload[2:end].decode("latin1")
                    if looks_like_name(n):
                        name = f" name=`{n}`"
                except Exception:
                    pass
        md.append(f"- **{kind}** @ `{off_r:#08x}` size={size}{name}  \n")
        md.append(f"  hex: `{hexs(payload, 40)}`  \n")
        md.append(f"  ascii: `{asciis(payload, 40)}`\n")
    md.append("\n")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write("".join(md))
print(f"Wrote {OUT}")
print(f"Distinct tags: {len(by_tag)}")
