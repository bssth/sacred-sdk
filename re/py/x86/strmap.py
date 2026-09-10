"""Global string <-> function index for Sacred_decrypted.exe."""
import struct, collections, re, os, pickle, sys
import scan
from scan import P, TEXT_VA, TEXT, in_text, gh_func_of, gh_name, GH_NAME, _cached

MIN = 4

def _strings():
    """VA -> text for every plausible ASCIIZ string in .rdata/.data."""
    out = {}
    for sname in (".rdata", ".data"):
        s = P.sec(sname)
        if not s: continue
        blob = P.data[s["rawoff"]:s["rawoff"]+s["rawsz"]]
        base = s["va"]
        i = 0
        n = len(blob)
        while i < n:
            if 32 <= blob[i] < 127 or blob[i] in (9,10,13):
                j = i
                while j < n and (32 <= blob[j] < 127 or blob[j] in (9,10,13)): j += 1
                if j < n and blob[j] == 0 and (j - i) >= MIN:
                    out[base + i] = blob[i:j].decode("latin1")
                i = j + 1
            else:
                i += 1
    return out

STRINGS = _cached("strings", _strings)

def _refs():
    """string VA -> [code VAs referencing it]"""
    m = collections.defaultdict(list)
    sset = STRINGS
    for off in range(0, len(TEXT) - 4):
        v = struct.unpack_from("<I", TEXT, off)[0]
        if v in sset:
            m[v].append(TEXT_VA + off)
    return dict(m)

REFS = _cached("strrefs", _refs)

def func_strings():
    """function entry -> set of strings it references"""
    m = collections.defaultdict(set)
    for sva, sites in REFS.items():
        for site in sites:
            f = gh_func_of(site)
            if f is not None: m[f].add(STRINGS[sva])
    return m

def grep(pattern, limit=60):
    rx = re.compile(pattern, re.I)
    hits = [(va, s) for va, s in STRINGS.items() if rx.search(s)]
    hits.sort(key=lambda kv: kv[1])
    for va, s in hits[:limit]:
        sites = REFS.get(va, [])
        fns = sorted({gh_name(x) for x in sites})
        print("%08x  %-58s  refs=%d  %s" % (va, s[:58], len(sites), " ".join(fns[:4])))
    print("(%d matches, showing %d)" % (len(hits), min(len(hits), limit)))

if __name__ == "__main__":
    if len(sys.argv) > 1: grep(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 60)
    else: print("strings:", len(STRINGS), " referenced:", len(REFS))
