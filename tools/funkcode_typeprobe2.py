"""Type inference v2 — adds Pascal-string encoding + better diagnostics.

Encodings tried per type:
  u8/u16/u32/u64       fixed-width LE int
  cstr                 ASCIIZ
  pstr                 length-prefixed (u8 len + bytes)

For each round, the algorithm picks the encoding whose "consistency score" is
highest (next byte after the value lands on a known type or at record end).
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

# Seed types (high confidence from manual analysis + first inference round)
KNOWN = {
    0x0b: "u32", 0x1c: "u32", 0x38: "u32", 0x49: "u32", 0x4a: "u32",
    0x53: "u32", 0x86: "u32", 0x14: "u32", 0x15: "u32",
    0x28: "u16",
    0x16: "cstr", 0x01: "cstr",
}

def consume(payload, p, kind):
    """Try to read one value of `kind` starting at p. Return new p or None."""
    if kind == "u8":
        if p + 1 > len(payload): return None
        return p + 1
    if kind == "u16":
        if p + 2 > len(payload): return None
        return p + 2
    if kind == "u32":
        if p + 4 > len(payload): return None
        return p + 4
    if kind == "u64":
        if p + 8 > len(payload): return None
        return p + 8
    if kind == "cstr":
        end = payload.find(b"\x00", p)
        if end < 0: return None
        return end + 1
    if kind == "pstr":
        if p + 1 > len(payload): return None
        ln = payload[p]
        if p + 1 + ln > len(payload): return None
        # plausibility: most bytes printable
        sample = payload[p+1:p+1+ln]
        if not sample: return None
        printable = sum(1 for b in sample if 32 <= b < 127)
        if printable / len(sample) < 0.7: return None
        return p + 1 + ln
    return None

def try_parse(payload, known):
    """Parse using known dict. Return (ok, stop_idx, fields_count, bytes_consumed)."""
    if not payload:
        return True, 0, 0, 0
    p = 1  # skip flags
    n = 0
    while p < len(payload):
        t = payload[p]
        if t not in known:
            return False, p, n, p
        nxt = consume(payload, p + 1, known[t])
        if nxt is None:
            return False, p, n, p
        p = nxt
        n += 1
    return True, p, n, p

def infer_one_round(records, known):
    """For each unknown type, score each encoding."""
    cand = collections.defaultdict(lambda: collections.Counter())
    for off_r, tag, size, payload in records:
        if not payload:
            continue
        ok, stop, _, _ = try_parse(payload, known)
        if ok:
            continue
        t = payload[stop]
        for kind in ("u8", "u16", "u32", "u64", "cstr", "pstr"):
            nxt = consume(payload, stop + 1, kind)
            if nxt is None:
                continue
            # consistency: rest of record from nxt onward parses cleanly under current dict
            rest_ok = True
            p = nxt
            while p < len(payload):
                tt = payload[p]
                if tt not in known:
                    rest_ok = False
                    break
                np = consume(payload, p + 1, known[tt])
                if np is None:
                    rest_ok = False
                    break
                p = np
            if rest_ok:
                cand[t][kind] += 1
    return cand

def coverage(records, known):
    full = 0
    bytes_consumed = 0
    bytes_total = 0
    for off_r, tag, size, p in records:
        if not p:
            full += 1
            continue
        ok, stop, _, consumed = try_parse(p, known)
        if ok:
            full += 1
            bytes_consumed += len(p)
        else:
            bytes_consumed += stop  # everything up to the stall is good
        bytes_total += len(p)
    return full, bytes_consumed, bytes_total

# Iterate
print(f"=== Type inference v2 ===")
print(f"Seed: {len(KNOWN)} types\n")

for round_no in range(1, 60):
    full, bc, bt = coverage(records, KNOWN)
    print(f"--- Round {round_no} ---  fully-parsed: {full}/{len(records)} ({full/len(records)*100:.1f}%)  "
          f"field-bytes parsed: {bc}/{bt} ({bc/bt*100:.1f}%)")
    cand = infer_one_round(records, KNOWN)
    if not cand:
        print("  Nothing left to infer.")
        break
    # rank by total support
    by_hits = sorted(cand.items(), key=lambda kv: -sum(kv[1].values()))
    # show top 10
    for t, c in by_hits[:8]:
        total = sum(c.values())
        top3 = ", ".join(f"{k}={v}" for k, v in c.most_common(3))
        print(f"  0x{t:02x}  hits={total:>6}  {top3}")
    # pick best confident new type
    picked = False
    for t, c in by_hits:
        if t in KNOWN:
            continue
        total = sum(c.values())
        if total < 20:
            continue
        best, best_n = c.most_common(1)[0]
        ratio = best_n / total
        if ratio >= 0.70:
            KNOWN[t] = best
            print(f"  >> LEARN 0x{t:02x} = {best} ({best_n}/{total}={ratio*100:.0f}%)")
            picked = True
            break
    if not picked:
        # relaxed
        for t, c in by_hits:
            if t in KNOWN:
                continue
            total = sum(c.values())
            if total < 50:
                continue
            best, best_n = c.most_common(1)[0]
            ratio = best_n / total
            if ratio >= 0.55:
                KNOWN[t] = best
                print(f"  >> LEARN-relaxed 0x{t:02x} = {best} ({best_n}/{total}={ratio*100:.0f}%)")
                picked = True
                break
    if not picked:
        print("  No confident pick possible. Stopping.")
        break

# Final report
full, bc, bt = coverage(records, KNOWN)
print(f"\n=== FINAL ===")
print(f"Records fully parsed: {full}/{len(records)} ({full/len(records)*100:.2f}%)")
print(f"Field bytes parsed:   {bc}/{bt} ({bc/bt*100:.2f}%)")
print(f"\nLearned type dictionary ({len(KNOWN)} entries):")
for t in sorted(KNOWN):
    print(f"  0x{t:02x}: {KNOWN[t]}")

# What's still unresolved?
unresolved = collections.Counter()
for off_r, tag, size, p in records:
    if not p: continue
    ok, stop, _, _ = try_parse(p, KNOWN)
    if not ok and stop < len(p):
        unresolved[p[stop]] += 1
print(f"\nRemaining unresolved type bytes (top 20):")
for t, c in unresolved.most_common(20):
    print(f"  0x{t:02x}: stalls={c}")
