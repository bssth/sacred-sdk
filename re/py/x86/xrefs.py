"""Call/data cross-references over the whole image."""
import sys, struct, collections
import scan
from scan import P, TEXT_VA, TEXT, in_text, gh_func_of, gh_name, string_at, _cached

def _callmap():
    """target -> [call site VAs] for direct E8/E9 rel32."""
    m = collections.defaultdict(list)
    for i in range(len(TEXT) - 5):
        b = TEXT[i]
        if b in (0xE8, 0xE9):
            rel = struct.unpack_from("<i", TEXT, i+1)[0]
            tgt = TEXT_VA + i + 5 + rel
            if in_text(tgt): m[tgt].append(TEXT_VA + i)
    return dict(m)

CALLMAP = _cached("callmap", _callmap)

def _dwordmap():
    """dword value -> [VAs holding it] across .rdata/.data (vtables, tables)."""
    m = collections.defaultdict(list)
    for sname in (".rdata", ".data"):
        s = P.sec(sname)
        if not s: continue
        blob = P.data[s["rawoff"]:s["rawoff"]+s["rawsz"]]
        for off in range(0, len(blob) - 4, 4):
            v = struct.unpack_from("<I", blob, off)[0]
            if in_text(v):
                m[v].append(s["va"] + off)
    return dict(m)

DWORDMAP = _cached("dwordmap", _dwordmap)

def callers(va, named=True):
    out = []
    for site in CALLMAP.get(va, []):
        f = gh_func_of(site)
        out.append((site, f, gh_name(site)))
    return out

def datarefs(va):
    return DWORDMAP.get(va, [])

if __name__ == "__main__":
    for a in sys.argv[1:]:
        va = int(a, 0)
        cs = callers(va)
        print("== %08x %s : %d direct callers ==" % (va, gh_name(va), len(cs)))
        seen = set()
        for site, f, nm in cs:
            if f in seen: continue
            seen.add(f)
            print("   from %08x  in %s" % (site, nm))
        d = datarefs(va)
        if d: print("   data refs (vtable/table slots): " + " ".join("%08x" % x for x in d[:20]))
