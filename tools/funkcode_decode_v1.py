"""Walk FunkCode.bin as a stream of tagged records and pretty-print them.

Hypothesized record layout (from header of SERAPHIM base):
    +0  u8   tag
    +1  u16  total_size  (includes header, LE)
    +3  ...  payload (total_size - 3 bytes)

Two tags seen so far:
    0x43 ('C') -> payload: u8 subtag, cstr name, 5-byte trailer  -> "declare const"
    0x69 ('i') -> payload: u8 subtag, cstr name, 10-byte trailer -> "init var with value"

Trailer format (so far):
    [0x0b][u32 LE value]      -> "type 11, integer value"

This is a first-pass walker. It will dump record types and stop after N records or on
parse failure.
"""
import os, struct, sys, collections

PATH = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_SERAPHIM\FunkCode.bin"
data = open(PATH, "rb").read()

def safe_cstr(b, off, maxlen=128):
    end = b.find(b"\x00", off, off + maxlen)
    if end < 0:
        return None, off + maxlen
    return b[off:end].decode("latin1", "replace"), end + 1

records = []
errors = 0
off = 0
LIMIT = 80   # how many records to dump in detail
N = 0
tag_hist = collections.Counter()
size_hist = collections.Counter()

while off + 3 <= len(data):
    tag = data[off]
    size = struct.unpack_from("<H", data, off + 1)[0]
    if size < 4 or off + size > len(data):
        errors += 1
        # try to resync by advancing 1 byte
        off += 1
        if errors > 5: break
        continue
    payload = data[off + 3 : off + size]
    rec = {"off": off, "tag": tag, "size": size, "payload": payload}

    # quick decode for tag C / i
    if tag in (0x43, 0x69) and len(payload) >= 2:
        sub = payload[0]
        name, after = safe_cstr(payload, 1)
        if name is not None:
            tail = payload[after:]
            rec["sub"] = sub
            rec["name"] = name
            rec["tail"] = tail
    records.append(rec)
    tag_hist[tag] += 1
    size_hist[size] += 1
    off += size
    N += 1

print(f"Parsed {N} records, last offset {off}/{len(data)} ({off/len(data)*100:.2f}%), errors {errors}")
print(f"\nTag histogram (top 20):")
for t, c in tag_hist.most_common(20):
    ch = chr(t) if 32 <= t < 127 else "?"
    print(f"  0x{t:02x} '{ch}'  {c:>7}")

print(f"\nSize histogram (top 12):")
for s, c in size_hist.most_common(12):
    print(f"  size={s:>5}  {c:>7}")

print(f"\nFirst {min(LIMIT, len(records))} records:")
for rec in records[:LIMIT]:
    s = f"  @ {rec['off']:08x}  tag=0x{rec['tag']:02x}'{chr(rec['tag']) if 32<=rec['tag']<127 else '?'}'  size={rec['size']:>5}"
    if "name" in rec:
        tailhex = " ".join(f"{x:02x}" for x in rec["tail"][:16])
        s += f"  sub=0x{rec['sub']:02x}  name='{rec['name']}'  tail=[{tailhex}]"
    else:
        # dump first 24 bytes of payload
        ph = " ".join(f"{x:02x}" for x in rec["payload"][:24])
        s += f"  payload[:24]={ph}"
    print(s)

# Now: how far do "named" records (tag C/i) go before something else kicks in?
named = [r for r in records if "name" in r]
print(f"\nNamed records: {len(named)} of {len(records)} total")
if named:
    print(f"  last named record at offset {named[-1]['off']:#x}, name='{named[-1]['name']}'")

# Show first non-C/i record (a likely structural divider)
non_ci = next((r for r in records if r["tag"] not in (0x43, 0x69)), None)
if non_ci:
    print(f"\nFirst non-C/non-i record:")
    print(f"  @ {non_ci['off']:#x}  tag=0x{non_ci['tag']:02x}  size={non_ci['size']}  payload[:32]={' '.join(f'{x:02x}' for x in non_ci['payload'][:32])}")

# Per-class divergence point: find offset where SERAPHIM and GLADIATOR first differ.
GLAD = open(r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_GLADIATOR\FunkCode.bin", "rb").read()
n = min(len(data), len(GLAD))
i = 0
while i < n and data[i] == GLAD[i]:
    i += 1
print(f"\nFirst divergence SERAPHIM vs GLADIATOR: offset {i:#x} ({i})")
# Find the record containing that offset
for r in records:
    if r["off"] <= i < r["off"] + r["size"]:
        print(f"  divergence falls inside record @ {r['off']:#x} tag=0x{r['tag']:02x} size={r['size']}")
        if "name" in r:
            print(f"  record name = '{r['name']}'")
        break

# Find re-convergence point — common suffix
i_s = 0
while i_s < n and data[len(data)-1-i_s] == GLAD[len(GLAD)-1-i_s]:
    i_s += 1
print(f"Common suffix length: {i_s} (re-convergence at offset {len(data)-i_s:#x} in SERAPHIM)")
print(f"Per-class divergent region in SERAPHIM: {i:#x} .. {len(data)-i_s:#x}  ({len(data)-i_s - i} bytes)")
