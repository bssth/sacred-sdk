import sys, collections
import scan, funcinfo
from scan import GH_NAME, GH_MAX, gh_func_of
def brief(va, maxs=8):
    e = gh_func_of(va)
    if e is None: print("%08x : not in a function" % va); return
    st, en, strs, calls, imms = funcinfo.profile(e, cap=(GH_MAX[e]-e+1), quiet=True)
    uniq, seen = [], set()
    for a,v,s in strs:
        if s in seen: continue
        seen.add(s); uniq.append(s)
    print("%08x %-28s size=%-6d strings: %s" % (e, GH_NAME[e], GH_MAX[e]-e+1,
          " | ".join(repr(u[:60]) for u in uniq[:maxs]) or "-"))
if __name__ == "__main__":
    for a in sys.argv[1:]: brief(int(a,0))
