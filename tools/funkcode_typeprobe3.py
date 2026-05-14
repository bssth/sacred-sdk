"""Type inference v3 — conservative: only adopt types with >=95% confidence
on >=200 hits, with width restricted to {u8, u16, u32, cstr, pstr}. No u64 (too rare
in this era), no false-positive u8 from misaligned zero-bytes.

After convergence we dump:
  - the conservative dictionary
  - per-unresolved-type samples (10 records each), so a human can eyeball the rest
  - per-tag breakdown showing which tags have unresolved types and how often
"""
import os, struct, collections

PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_SERAPHIM\FunkCode.bin"
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

# Conservative seed: only types we have very strong manual evidence for
KNOWN = {
    0x0b: "u32",
    0x1c: "u32",
    0x38: "u32",
    0x49: "u32",
    0x4a: "u32",
    0x53: "u32",
    0x86: "u32",
    0x14: "u32",
    0x15: "u32",
    0x28: "u16",
    0x16: "cstr",
    0x01: "cstr",
}

# Encodings to try
ENCODINGS = ("u8", "u16", "u32", "cstr")

def consume(payload, p, kind):
    if kind == "u8":  return p + 1 if p + 1 <= len(payload) else None
    if kind == "u16": return p + 2 if p + 2 <= len(payload) else None
    if kind == "u32": return p + 4 if p + 4 <= len(payload) else None
    if kind == "cstr":
        end = payload.find(b"\x00", p)
        return (end + 1) if end >= 0 else None
    return None

def try_parse(payload, known):
    if not payload:
        return True, 0
    p = 1
    while p < len(payload):
        t = payload[p]
        if t not in known:
            return False, p
        nxt = consume(payload, p + 1, known[t])
        if nxt is None:
            return False, p
        p = nxt
    return True, p

def infer(records, known):
    cand = collections.defaultdict(collections.Counter)
    for off_r, _, _, payload in records:
        if not payload: continue
        ok, stop = try_parse(payload, known)
        if ok: continue
        t = payload[stop]
        for kind in ENCODINGS:
            nxt = consume(payload, stop + 1, kind)
            if nxt is None: continue
            # require rest of record from nxt fully parses
            ok2, _ = try_parse_from(payload, nxt, known)
            if ok2:
                cand[t][kind] += 1
    return cand

def try_parse_from(payload, p, known):
    while p < len(payload):
        t = payload[p]
        if t not in known: return False, p
        nxt = consume(payload, p + 1, known[t])
        if nxt is None: return False, p
        p = nxt
    return True, p

def coverage(records, known):
    full = 0
    bytes_consumed = 0
    bytes_total = 0
    for _, _, _, p in records:
        if not p:
            full += 1
            continue
        ok, stop = try_parse(p, known)
        bytes_total += len(p)
        if ok:
            full += 1
            bytes_consumed += len(p)
        else:
            bytes_consumed += stop
    return full, bytes_consumed, bytes_total

# Iterate with strict threshold
THRESHOLD = 0.90
MIN_HITS  = 100
print(f"=== Type inference v3 (conservative) ===")
print(f"Seed: {len(KNOWN)} types, threshold {THRESHOLD*100:.0f}% on >={MIN_HITS} hits, no u64\n")

for round_no in range(1, 50):
    full, bc, bt = coverage(records, KNOWN)
    print(f"R{round_no:2d}  full={full:>6}/{len(records)} ({full/len(records)*100:5.1f}%)  "
          f"bytes={bc:>7}/{bt} ({bc/bt*100:5.1f}%)  dict={len(KNOWN)}")
    cand = infer(records, KNOWN)
    if not cand: break
    by_hits = sorted(cand.items(), key=lambda kv: -sum(kv[1].values()))
    picked = False
    for t, c in by_hits:
        if t in KNOWN: continue
        total = sum(c.values())
        if total < MIN_HITS: continue
        best, best_n = c.most_common(1)[0]
        ratio = best_n / total
        if ratio >= THRESHOLD:
            KNOWN[t] = best
            print(f"      LEARN 0x{t:02x} = {best} ({best_n}/{total}={ratio*100:.0f}%)")
            picked = True
            break
    if not picked: break

# Final
full, bc, bt = coverage(records, KNOWN)
print(f"\n=== FINAL CONSERVATIVE DICT ===")
print(f"Records fully parsed: {full}/{len(records)} ({full/len(records)*100:.2f}%)")
print(f"Field bytes parsed:   {bc}/{bt} ({bc/bt*100:.2f}%)")
print(f"\n{len(KNOWN)} types known:")
for t in sorted(KNOWN):
    print(f"  0x{t:02x}: {KNOWN[t]}")

# Unresolved samples
unresolved = collections.Counter()
samples = collections.defaultdict(list)
for off_r, tag, size, p in records:
    if not p: continue
    ok, stop = try_parse(p, KNOWN)
    if not ok and stop < len(p):
        unr = p[stop]
        unresolved[unr] += 1
        if len(samples[unr]) < 4:
            ctx_before = " ".join(f"{b:02x}" for b in p[max(0,stop-6):stop])
            ctx_at = " ".join(f"{b:02x}" for b in p[stop:stop+16])
            samples[unr].append((off_r, tag, len(p), stop, ctx_before, ctx_at))

print(f"\n=== UNRESOLVED TYPES (top 15 with samples) ===")
for t, c in unresolved.most_common(15):
    print(f"\n--- 0x{t:02x}  stalls={c} ---")
    for off_r, tag, sz, stop, before, at in samples[t][:4]:
        ch = chr(tag) if 32<=tag<127 else "?"
        print(f"  in tag 0x{tag:02x}'{ch}' @ {off_r:#08x} size={sz}, stop_at_payload_offset={stop}")
        print(f"    before: {before}")
        print(f"    at:     {at}")

# Per-tag unresolved
print(f"\n=== PER-TAG UNRESOLVED COUNTS (top 15) ===")
tag_unr = collections.Counter()
tag_total = collections.Counter()
for _, tag, _, p in records:
    tag_total[tag] += 1
    if not p: continue
    ok, _ = try_parse(p, KNOWN)
    if not ok: tag_unr[tag] += 1
for t in sorted(tag_unr, key=lambda k: -tag_unr[k])[:15]:
    ch = chr(t) if 32<=t<127 else "?"
    print(f"  0x{t:02x} '{ch}': {tag_unr[t]}/{tag_total[t]} unresolved ({tag_unr[t]/tag_total[t]*100:.0f}%)")
