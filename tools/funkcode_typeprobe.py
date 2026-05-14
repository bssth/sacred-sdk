"""Iteratively infer unknown value-type widths in FunkCode.bin.

Algorithm:
  1. Start with a seed of types we know how to parse (mostly 4-byte u32 LE, plus cstr).
  2. Walk all records using current grammar.
  3. When a record is fully parsed: contributes positive evidence.
     When a record stalls at unknown type T: look at distance to the next byte that
     is a known type byte (or record end) — that's a candidate width for T.
  4. For each unknown T, aggregate width candidates across all occurrences.
     The width with the highest "consistency score" wins.
  5. Add that to the known set. Iterate until no new types are learned.
"""
import os, struct, collections

PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_SERAPHIM\FunkCode.bin"
data = open(PATH, "rb").read()

# Parse outer TLV records
records = []
off = 0
while off + 3 <= len(data):
    tag = data[off]
    size = (data[off+1] << 8) | data[off+2]
    if size < 3 or off + size > len(data):
        break
    records.append((off, tag, size, data[off+3:off+size]))
    off += size

# Seed known types: type → encoding
KNOWN = {
    # u32 LE
    0x0b: ("u32", 4), 0x1c: ("u32", 4), 0x38: ("u32", 4),
    0x49: ("u32", 4), 0x4a: ("u32", 4), 0x53: ("u32", 4),
    0x86: ("u32", 4), 0x14: ("u32", 4), 0x15: ("u32", 4),
    0x28: ("u16", 2),                                 # from histogram: always 2-byte
    # variable cstr
    0x16: ("cstr", None), 0x01: ("cstr", None),
}

def parse_one(payload, known):
    """Parse a payload's field stream using `known`. Return (fields, ok, stop_idx)."""
    out = []
    if not payload:
        return out, True, 0
    flags = payload[0]
    i = 1
    while i < len(payload):
        t = payload[i]
        i += 1
        if t in known:
            kind, w = known[t]
            if kind == "u32":
                if i + 4 > len(payload):
                    return out, False, i - 1
                v = struct.unpack_from("<I", payload, i)[0]
                out.append((t, v, "u32"))
                i += 4
            elif kind == "u16":
                if i + 2 > len(payload):
                    return out, False, i - 1
                v = struct.unpack_from("<H", payload, i)[0]
                out.append((t, v, "u16"))
                i += 2
            elif kind == "u8":
                if i + 1 > len(payload):
                    return out, False, i - 1
                v = payload[i]
                out.append((t, v, "u8"))
                i += 1
            elif kind == "cstr":
                end = payload.find(b"\x00", i)
                if end < 0:
                    return out, False, i - 1
                out.append((t, payload[i:end].decode("latin1", "replace"), "cstr"))
                i = end + 1
            else:
                return out, False, i - 1
        else:
            return out, False, i - 1
    return out, True, i

def infer_round(records, known):
    """One round: for each unknown type, collect width candidates and pick best."""
    # candidates: type -> Counter of widths
    cand = collections.defaultdict(collections.Counter)
    # Also track examples
    cand_ex = collections.defaultdict(list)

    for off_r, tag, size, payload in records:
        if not payload:
            continue
        fields, ok, stop = parse_one(payload, known)
        if ok:
            continue
        # stop is the position of the unknown type byte
        if stop >= len(payload):
            continue
        unk = payload[stop]
        # try widths 1, 2, 4, 8 — pick the smallest that lands on a known type or record end
        for w in (1, 2, 4, 8):
            j = stop + 1 + w
            if j > len(payload):
                continue
            if j == len(payload):
                # record ends here — exact fit
                cand[unk][("end", w)] += 1
                if len(cand_ex[unk]) < 3:
                    cand_ex[unk].append((off_r, tag, payload[stop:stop+1+w], "end"))
                break
            nxt = payload[j]
            if nxt in known:
                cand[unk][("known", w)] += 1
                if len(cand_ex[unk]) < 3:
                    cand_ex[unk].append((off_r, tag, payload[stop:stop+1+w], f"next={hex(nxt)}"))
                break
        # also try cstr interpretation
        end = payload.find(b"\x00", stop + 1)
        if end > stop + 1:
            tentative_name = payload[stop+1:end]
            # plausible if printable-mostly
            printable = sum(1 for b in tentative_name if 32 <= b < 127)
            if len(tentative_name) > 0 and printable / len(tentative_name) >= 0.8:
                j = end + 1
                if j == len(payload):
                    cand[unk][("end", "cstr")] += 1
                elif j < len(payload) and payload[j] in known:
                    cand[unk][("known", "cstr")] += 1

    return cand, cand_ex

# Iterate
print(f"=== Seed: {len(KNOWN)} known types ===\n")
round_no = 0
while True:
    round_no += 1
    fully = 0
    for _, _, _, p in records:
        _, ok, _ = parse_one(p, KNOWN)
        if ok:
            fully += 1
    print(f"--- Round {round_no} ---  fully-parsed: {fully}/{len(records)} ({fully/len(records)*100:.1f}%)")

    cand, cand_ex = infer_round(records, KNOWN)
    if not cand:
        print("No more unknown types reachable. Done.\n")
        break

    # For each unknown type, summarize candidates
    learned_this_round = 0
    by_freq = sorted(cand.items(), key=lambda kv: -sum(kv[1].values()))
    for t, c in by_freq[:15]:
        total = sum(c.values())
        best = c.most_common(1)[0]
        print(f"  type 0x{t:02x}  total_hits={total:>6}  best_candidate={best[0]} (x{best[1]})  "
              f"all: {dict(c.most_common(5))}")

    # Adopt the most-frequent unknown whose top candidate exceeds 70% of its hits.
    accepted = False
    for t, c in by_freq:
        total = sum(c.values())
        if total < 20:
            continue
        if t in KNOWN:
            continue
        best, best_n = c.most_common(1)[0]
        ratio = best_n / total
        if ratio >= 0.70:
            tag_meta, width = best
            if width == "cstr":
                KNOWN[t] = ("cstr", None)
            else:
                if width == 4:
                    KNOWN[t] = ("u32", 4)
                elif width == 2:
                    KNOWN[t] = ("u16", 2)
                elif width == 1:
                    KNOWN[t] = ("u8", 1)
                elif width == 8:
                    KNOWN[t] = ("u64", 8)
                else:
                    continue
            print(f"  -> LEARNED 0x{t:02x} = {KNOWN[t]} ({best_n}/{total} = {ratio*100:.0f}%)")
            accepted = True
            learned_this_round += 1
            break  # one type per round to keep evidence clean
    if not accepted:
        # Try a looser threshold for the next iterations
        print("  (no high-confidence type to adopt at >=70%; relaxing threshold)")
        for t, c in by_freq:
            total = sum(c.values())
            if total < 50 or t in KNOWN:
                continue
            best, best_n = c.most_common(1)[0]
            if best_n / total >= 0.55:
                tag_meta, width = best
                if width == "cstr":
                    KNOWN[t] = ("cstr", None)
                elif width in (1, 2, 4, 8):
                    KNOWN[t] = ({1:"u8",2:"u16",4:"u32",8:"u64"}[width], width)
                else:
                    continue
                print(f"  -> RELAXED-LEARNED 0x{t:02x} = {KNOWN[t]} ({best_n}/{total} = {best_n/total*100:.0f}%)")
                accepted = True
                break
    if not accepted:
        print("  No more types learnable. Stopping.")
        break
    if round_no > 30:
        print("  Round budget exceeded.")
        break

print("\n=== FINAL KNOWN TYPE DICTIONARY ===")
for t, enc in sorted(KNOWN.items()):
    print(f"  0x{t:02x}: {enc}")
print(f"\nTotal known: {len(KNOWN)}")

# Final coverage
fully = 0
total_fields = 0
unknown_counter = collections.Counter()
for off_r, tag, size, p in records:
    fields, ok, stop = parse_one(p, KNOWN)
    total_fields += len(fields)
    if ok:
        fully += 1
    else:
        if stop < len(p):
            unknown_counter[p[stop]] += 1
print(f"\nFully-parsed records: {fully}/{len(records)} ({fully/len(records)*100:.1f}%)")
print(f"Top unresolved types ({len(unknown_counter)} distinct):")
for t, c in unknown_counter.most_common(15):
    print(f"  0x{t:02x}  x{c}")
