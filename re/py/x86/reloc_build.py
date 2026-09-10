"""reloc_build.py - port engine VAs from one Sacred build to another.

Usage:
  python reloc_build.py OLD.exe NEW.exe [--extra extra.json] [--out symbols.json] [--sdk <sdk dir>]

OLD must have plain (decrypted) .text, e.g. sdk/Sacred_decrypted.exe. NEW is the
target build (e.g. SacredReborn.exe = ReBorn v4.3.2 on the 2006-09-28 "2.29" build).
Both must map 1:1 (raw offset == RVA), which all known Sacred builds do.

Method (all offline, no disassembler needed):
  1. delta map  - sample OLD .text every STEP bytes with a 48-byte *masked* signature
                  (imm32 in image range, rel32 after E8/E9/0F 8x are wildcards),
                  find it in NEW, record delta. Piecewise-constant runs = same
                  source with local insertions/removals.
  2. code VA    - predict NEW = OLD + delta(run), verify masked signature there,
                  else search +-4 KiB; cross-check by relocating E8 call sites
                  that target the VA and reading the new rel32 (two independent
                  methods must agree). body_sim = masked equality of first 256 B.
  3. data VA    - relocate instructions that embed the VA as imm32 and read the
                  new imm32 (xref voting; works for .bss too).
Symbols come from engine/addresses.h + engine_resolve.cpp kPinned + --extra json
({"NAME": "0xVA", ...}; VAs inside .text are treated as code).
"""
import re, struct, json, bisect, argparse

def pe_text(d):
    e = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, e + 6)[0]
    optsz = struct.unpack_from('<H', d, e + 20)[0]
    base = struct.unpack_from('<I', d, e + 24 + 28)[0]
    for i in range(nsec):
        n, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, e + 24 + optsz + 40 * i)
        if n.rstrip(b'\0') == b'.text':
            assert va == ro, "section must map 1:1"
            return base, (base + va, base + va + vs)
    raise SystemExit("no .text")

class Build:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        self.base, self.text = pe_text(self.d)
    def rd(self, va, n):
        return self.d[va - self.base:va - self.base + n]

VA_LO, VA_HI = 0x400000, 0x1a00000

def mask_of(b):
    m = bytearray([1] * len(b)); i = 0
    while i < len(b):
        if i + 4 <= len(b) and VA_LO <= struct.unpack_from('<I', b, i)[0] < VA_HI:
            m[i:i + 4] = b'\0\0\0\0'; i += 4; continue
        if b[i] in (0xE8, 0xE9) and i + 5 <= len(b):
            m[i + 1:i + 5] = b'\0\0\0\0'; i += 5; continue
        if b[i] == 0x0F and i + 6 <= len(b) and 0x80 <= b[i + 1] <= 0x8F:
            m[i + 2:i + 6] = b'\0\0\0\0'; i += 6; continue
        i += 1
    return m

def meq(hay, pos, nd, mk):
    if pos < 0 or pos + len(nd) > len(hay):
        return False
    return all((not mk[k]) or hay[pos + k] == nd[k] for k in range(len(nd)))

def rx_of(nd, mk):
    return re.compile(b''.join(b'.' if not mk[k] else re.escape(nd[k:k + 1]) for k in range(len(nd))), re.S)

class Reloc:
    def __init__(self, old, new, step=256, N=48):
        self.o, self.n = old, new
        self.runs = self.build_delta_map(step, N)
        self.starts = [r[0] for r in self.runs]

    def build_delta_map(self, STEP, N):
        o, n = self.o, self.n; anchors = []; delta = 0
        for va in range(o.text[0], o.text[1] - N, STEP):
            nd = o.rd(va, N)
            if nd.count(0) > N // 2 or nd.count(0xCC) > N // 2:
                continue
            mk = mask_of(nd)
            if sum(mk) < N * 0.6:
                continue
            if meq(n.d, va + delta - n.base, nd, mk):
                anchors.append((va, va + delta)); continue
            rx = rx_of(nd, mk)
            lo = max(n.text[0], va + delta - 0x40000) - n.base
            hi = min(n.text[1], va + delta + 0x40000) - n.base
            hits = [m.start() + n.base for m in rx.finditer(n.d, lo, hi)]
            if not hits:
                hits = [m.start() + n.base for m in rx.finditer(n.d, n.text[0] - n.base, n.text[1] - n.base)]
            if len(hits) == 1:
                delta = hits[0] - va; anchors.append((va, hits[0]))
            elif len(hits) > 1:
                near = [h for h in hits if abs((h - va) - delta) < 0x2000]
                if len(near) == 1:
                    delta = near[0] - va; anchors.append((va, near[0]))
        runs = []
        for a, b in anchors:
            d = b - a
            if runs and runs[-1][2] == d:
                runs[-1][1] = a
            else:
                runs.append([a, a, d])
        # drop singleton outliers (ambiguous matches that slipped through)
        sm = []
        for i, (a, b, d) in enumerate(runs):
            if a == b and 0 < i < len(runs) - 1 and abs(d - runs[i - 1][2]) > 0x1000 and abs(d - runs[i + 1][2]) > 0x1000:
                continue
            sm.append((a, b, d))
        return sm

    def pred_delta(self, va):
        i = bisect.bisect_right(self.starts, va) - 1
        return self.runs[max(i, 0)][2]

    def sig(self, va, N=48, win=0x4000):
        nd = self.o.rd(va, N); mk = mask_of(nd); cand = va + self.pred_delta(va)
        if meq(self.n.d, cand - self.n.base, nd, mk):
            return cand
        rx = rx_of(nd, mk)
        lo = max(self.n.text[0], cand - win) - self.n.base
        hi = min(self.n.text[1], cand + win) - self.n.base
        hits = [m.start() + self.n.base for m in rx.finditer(self.n.d, lo, hi)]
        return hits[0] if len(hits) == 1 else None

    def calls(self, va, maxref=8):
        votes = {}; cnt = 0; o, n = self.o, self.n
        for m in re.finditer(b'\xe8', o.d[o.text[0] - o.base:o.text[1] - o.base]):
            p = m.start() + o.text[0]
            if p + 5 + struct.unpack_from('<i', o.d, p - o.base + 1)[0] != va:
                continue
            ns = self.sig(p - 16, N=40)
            if ns is None:
                continue
            np_ = ns + 16
            if n.d[np_ - n.base] != 0xE8:
                continue
            tgt = np_ + 5 + struct.unpack_from('<i', n.d, np_ - n.base + 1)[0]
            votes[tgt] = votes.get(tgt, 0) + 1; cnt += 1
            if cnt >= maxref:
                break
        return votes

    def xref(self, va, maxref=6):
        key = struct.pack('<I', va); res = {}; o, n = self.o, self.n
        pos = o.d.find(key, o.text[0] - o.base); cnt = 0
        while pos >= 0 and cnt < maxref and pos < o.text[1] - o.base:
            nv = self.sig(pos - 12 + o.base, N=24)
            if nv is not None:
                v = struct.unpack_from('<I', n.d, nv - n.base + 12)[0]
                res[v] = res.get(v, 0) + 1
            cnt += 1; pos = o.d.find(key, pos + 1)
        return res

    def body_sim(self, va, nv, N=256):
        a = self.o.rd(va, N); b = self.n.rd(nv, N); mk = mask_of(a); tot = sum(mk)
        return sum(1 for k in range(N) if mk[k] and a[k] == b[k]) / tot if tot else 0

    def symbol(self, va):
        if self.o.text[0] <= va < self.o.text[1]:
            sg = self.sig(va); v = self.calls(va)
            c = max(v, key=v.get) if v else None; nv = sg or c
            agree = 'yes' if (sg and c and sg == c) else ('n/a' if not (sg and c) else 'NO')
            return nv, dict(kind='code', sig=sg, calls=c, agree=agree,
                            body_sim=self.body_sim(va, nv) if nv else 0, votes=v)
        r = self.xref(va); nv = max(r, key=r.get) if r else None
        return nv, dict(kind='data', votes=r)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old'); ap.add_argument('new')
    ap.add_argument('--extra'); ap.add_argument('--out'); ap.add_argument('--sdk', default='.')
    a = ap.parse_args()
    old, new = Build(a.old), Build(a.new); R = Reloc(old, new)
    print(f"delta-map runs: {len(R.runs)} (old .text {old.text[0]:#x}-{old.text[1]:#x})")
    syms = {}
    try:
        syms.update({n: int(v, 16) for n, v in re.findall(
            r'constexpr uintptr_t\s+(\w+)\s*=\s*0x([0-9A-Fa-f]+)', open(f'{a.sdk}/engine/addresses.h').read())})
    except FileNotFoundError:
        pass
    try:
        syms.update({n: int(v, 16) for n, v in re.findall(
            r'\{\s*"(\w+)",\s*0x([0-9a-fA-F]+)', open(f'{a.sdk}/engine_resolve.cpp').read())})
    except FileNotFoundError:
        pass
    if a.extra:
        syms.update({k: int(v, 16) for k, v in json.load(open(a.extra)).items() if not k.startswith('_')})
    out = {}
    print(f"{'symbol':28s} {'old':>10s} {'new':>10s} {'delta':>8s}  detail")
    for name, va in syms.items():
        nv, info = R.symbol(va); out[name] = {'old': va, 'new': nv, **info}
        if info['kind'] == 'code':
            det = f"sig/calls agree={info['agree']} body_sim={info['body_sim']:.0%}"
        else:
            det = f"xref votes={info['votes']}"
        print(f"{name:28s} {va:#010x} {(f'{nv:#010x}' if nv else '-'):>10s} {(f'{nv-va:+#x}' if nv else '-'):>8s}  {det}")
    if a.out:
        json.dump({'runs': R.runs, 'symbols': out}, open(a.out, 'w'), indent=1)
        print('wrote', a.out)

if __name__ == '__main__':
    main()
