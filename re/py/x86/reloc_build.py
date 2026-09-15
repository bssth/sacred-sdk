"""reloc_build.py - port the SDK's engine VAs from our build to another Sacred build.

Usage:
  python reloc_build.py [OLD.exe NEW.exe] [--extra extra.json] [--out symbols.json] [--sdk <sdk dir>]

OLD defaults to sdk/Sacred_decrypted.exe (plain .text), NEW to SacredReborn.exe
(the 2006-09-28 build ReBorn and Thorium's 2.29/2.30 patches were made on).

The address correspondence itself lives in buildmap.py (delta map, masked
signatures, no extrapolation). On top of it this script resolves each symbol
two independent ways and reports whether they agree:
  code VA   sig    = the masked signature at the VA, found in NEW
            calls  = relocate the E8 call sites that target the VA and read
                     the callee out of NEW's rel32 (votes)
            body_sim = masked equality of the first 256 bytes at both addresses
  data VA   relocate instructions that embed the VA as imm32/disp32 and read
            NEW's operand (votes; works for .bss too)
Symbols come from engine/addresses.h, engine_resolve.cpp kPinned and --extra
({"NAME": "0xVA", ...}).
"""
import re, struct, json, argparse
import buildmap as bm


class Reloc:
    def __init__(self, dm):
        self.dm, self.o, self.n = dm, dm.old, dm.new

    def sig(self, va, N=48):
        """NEW address of OLD code at va, proven by a masked N-byte signature."""
        nv, _ = self.dm.locate_new(va, before=0, after=N, win=0x4000)
        return nv

    def calls(self, va, maxref=8):
        votes, cnt, o, n = {}, 0, self.o, self.n
        for m in re.finditer(b"\xe8", o.text):
            p = m.start() + o.text_lo
            if p + 5 + struct.unpack_from("<i", o.text, m.start() + 1)[0] != va:
                continue
            ns, _ = self.dm.locate_new(p, before=16, after=24, skip=5, win=0x4000)
            if ns is None or n.rd(ns, 1) != b"\xe8":
                continue
            tgt = ns + 5 + struct.unpack_from("<i", n.rd(ns + 1, 4))[0]
            votes[tgt] = votes.get(tgt, 0) + 1
            cnt += 1
            if cnt >= maxref:
                break
        return votes

    def xref(self, va, maxref=6):
        key, res, o = struct.pack("<I", va), {}, self.o
        pos, cnt = o.text.find(key), 0
        while pos >= 0 and cnt < maxref:
            site = pos + o.text_lo
            nv, _ = self.dm.locate_new(site, before=12, after=12, skip=4, win=0x4000)
            if nv is not None:
                v = struct.unpack_from("<I", self.n.rd(nv, 4))[0]
                res[v] = res.get(v, 0) + 1
            cnt += 1
            pos = o.text.find(key, pos + 1)
        return res

    def body_sim(self, va, nv, N=256):
        a, b = self.o.rd(va, N), self.n.rd(nv, N)
        if not a or not b:
            return 0
        mk = bm.mask_of(a)
        tot = sum(mk)
        return sum(1 for k in range(N) if mk[k] and a[k] == b[k]) / tot if tot else 0

    def symbol(self, va):
        if self.o.in_text(va):
            sg = self.sig(va)
            v = self.calls(va)
            c = max(v, key=v.get) if v else None
            nv = sg or c
            agree = "yes" if (sg and c and sg == c) else ("n/a" if not (sg and c) else "NO")
            return nv, dict(kind="code", sig=sg, calls=c, agree=agree,
                            body_sim=self.body_sim(va, nv) if nv else 0, votes=v)
        r = self.xref(va)
        nv = max(r, key=r.get) if r else None
        return nv, dict(kind="data", votes=r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old", nargs="?", default=bm.OURS)
    ap.add_argument("new", nargs="?", default=bm.REBORN)
    ap.add_argument("--extra"); ap.add_argument("--out"); ap.add_argument("--sdk", default=".")
    a = ap.parse_args()
    old, new = bm.Image(a.old), bm.Image(a.new)
    dm = bm.DeltaMap(old, new)
    R = Reloc(dm)
    print(f"delta-map runs: {len(dm.runs)} (old .text {old.text_lo:#x}-{old.text_hi:#x})")
    syms = {}
    try:
        syms.update({n: int(v, 16) for n, v in re.findall(
            r"constexpr uintptr_t\s+(\w+)\s*=\s*0x([0-9A-Fa-f]+)", open(f"{a.sdk}/engine/addresses.h").read())})
    except FileNotFoundError:
        pass
    try:
        syms.update({n: int(v, 16) for n, v in re.findall(
            r'\{\s*"(\w+)",\s*0x([0-9a-fA-F]+)', open(f"{a.sdk}/engine_resolve.cpp").read())})
    except FileNotFoundError:
        pass
    if a.extra:
        syms.update({k: int(v, 16) for k, v in json.load(open(a.extra)).items() if not k.startswith("_")})
    out = {}
    print(f"{'symbol':28s} {'old':>10s} {'new':>10s} {'delta':>8s}  detail")
    for name, va in syms.items():
        nv, info = R.symbol(va)
        out[name] = {"old": va, "new": nv, **info}
        if info["kind"] == "code":
            det = f"sig/calls agree={info['agree']} body_sim={info['body_sim']:.0%}"
        else:
            det = f"xref votes={info['votes']}"
        print(f"{name:28s} {va:#010x} {(f'{nv:#010x}' if nv else '-'):>10s} "
              f"{(f'{nv - va:+#x}' if nv else '-'):>8s}  {det}")
    if a.out:
        with open(a.out, "w") as f:
            json.dump({"runs": dm.runs, "symbols": out}, f, indent=1)
        print("wrote", a.out)


if __name__ == "__main__":
    main()
