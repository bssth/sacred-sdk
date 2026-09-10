"""Correlate CreateNPC flag opcodes with creature types across all vanilla scripts."""
import npc_records as npcrec, collections, hashlib, os, re, sys

HDR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\ports\data\creature_types.h"
NAME, BAND = {}, {}
row = re.compile(r'^\s*\{\s*(\d+),\s*0x[0-9a-fA-F]+,\s*CreatureBand::(\w+),\s*"([^"]*)"')
for line in open(HDR, encoding="utf-8", errors="replace"):
    m = row.match(line)
    if m:
        NAME[int(m.group(1))] = m.group(3)
        BAND[int(m.group(1))] = m.group(2)

def load():
    """Yield (source, offset, fields) for every unique CreateNPC record."""
    seen = set()
    for path in npcrec.all_bins():
        if os.path.basename(path).lower() not in ("funkcode.bin", "startcode.bin"): continue
        buf = open(path, "rb").read()
        h = hashlib.md5(buf).hexdigest()
        if h in seen: continue
        seen.add(h)
        src = os.path.relpath(path, npcrec.ROOT)
        for off, tag, pl in npcrec.records(buf, 0x01):
            f, ok = npcrec.decode(pl)
            if ok: yield src, off, f

def analyse():
    recs = list(load())
    print("unique CreateNPC records: %d\n" % len(recs))
    flag_types  = collections.defaultdict(collections.Counter)
    flag_bands  = collections.defaultdict(collections.Counter)
    flag_count  = collections.Counter()
    flag_co     = collections.defaultdict(collections.Counter)
    type_flags  = collections.defaultdict(collections.Counter)
    for src, off, f in recs:
        typ = next((v for op, k, v in f if op == 0x02 and k == "i32"), None)
        flags = [op for op, k, _ in f if k == "flag"]
        for fl in flags:
            flag_count[fl] += 1
            if typ is not None:
                flag_types[fl][typ] += 1
                flag_bands[fl][BAND.get(typ, "?")] += 1
                type_flags[typ][fl] += 1
            for other in flags:
                if other != fl: flag_co[fl][other] += 1
    print("%-6s %7s  %-42s  %s" % ("flag", "count", "band mix", "top creature types"))
    for fl, n in sorted(flag_count.items(), key=lambda kv: -kv[1]):
        bands = ", ".join("%s:%d" % (b, c) for b, c in flag_bands[fl].most_common(4))
        tops  = ", ".join("%s(%d)" % (NAME.get(t, str(t)), c) for t, c in flag_types[fl].most_common(4))
        print("0x%02x  %7d  %-42s  %s" % (fl, n, bands[:42], tops[:100]))
    print("\n--- co-occurrence (top partners per flag) ---")
    for fl, n in sorted(flag_count.items(), key=lambda kv: -kv[1]):
        if n < 20: continue
        co = ", ".join("0x%02x(%d)" % (o, c) for o, c in flag_co[fl].most_common(6))
        print("0x%02x: %s" % (fl, co))

if __name__ == "__main__":
    analyse()
