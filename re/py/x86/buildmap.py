"""buildmap.py - address correspondence between two builds of Sacred.exe.

OLD is always our build (sdk/Sacred_decrypted.exe, Steam/GOG "Oct 13 2006").
NEW is another build of the same engine, in practice SacredReborn.exe (the
"Sep 28 2006" build ReBorn and Thorium's 2.29/2.30 patches were made on).

The two .text sections hold the same source compiled twice, so code moves in
piecewise-constant runs: a stretch of functions shifted by one delta, then a
local insertion or removal, then the next delta. DeltaMap samples OLD .text
every STEP bytes with a 48-byte masked signature (absolute addresses and
rel32 displacements wildcarded), finds each sample in NEW and merges equal
deltas into runs.

What changed compared with the first version of this code (reloc_build.py):
  * STEP 64 instead of 256, so short runs are no longer lost. The 256 map had
    a 6 KB hole over the display init at 0x815f00-0x817700.
  * No extrapolation. old2new()/new2old() answer only inside a run's covered
    range; everywhere else they return None and the caller has to use
    locate_*(), which proves the answer with a signature. The old
    pred_delta() silently extended the previous run's delta over any hole.
  * The map is cached per (file size, mtime) pair under _cache/.

Library only. It reads both executables from the local install and never
writes bytes of either anywhere except the cache.
"""
import os, re, struct, bisect, json, hashlib
from pe import PE

GAME   = r"E:\SteamLibrary\steamapps\common\Sacred Gold"
OURS   = os.path.join(GAME, "sdk", "Sacred_decrypted.exe")
REBORN = os.path.join(GAME, "SacredReborn.exe")
CACHE  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_cache")

VA_LO, VA_HI = 0x00400000, 0x01E00000   # anything in here is treated as an absolute address


class Image:
    """A PE image with fast .text access."""
    def __init__(self, path):
        self.path = path
        self.pe = PE(path)
        t = self.pe.sec(".text")
        self.text_lo = t["va"]
        self.text_hi = t["va"] + t["vsize"]
        self.text = self.pe.data[t["rawoff"]:t["rawoff"] + t["vsize"]]

    def in_text(self, va):
        return self.text_lo <= va < self.text_hi

    def rd(self, va, n):
        """Bytes at va from whichever section holds them (None if not file-backed)."""
        if self.text_lo <= va and va + n <= self.text_hi:
            o = va - self.text_lo
            return self.text[o:o + n]
        return self.pe.read(va, n)

    def sec_of(self, va):
        for s in self.pe.sections:
            if s["va"] <= va < s["va"] + max(s["vsize"], s["rawsz"]):
                return s["name"]
        return None


# ---------------------------------------------------------------------------
#  Masked signatures
# ---------------------------------------------------------------------------
def mask_of(b):
    """1 = compare this byte, 0 = wildcard (absolute address / branch displacement)."""
    m = bytearray([1] * len(b))
    i = 0
    while i < len(b):
        if i + 4 <= len(b) and VA_LO <= struct.unpack_from("<I", b, i)[0] < VA_HI:
            m[i:i + 4] = b"\0\0\0\0"; i += 4; continue
        if b[i] in (0xE8, 0xE9) and i + 5 <= len(b):
            m[i + 1:i + 5] = b"\0\0\0\0"; i += 5; continue
        if b[i] == 0x0F and i + 6 <= len(b) and 0x80 <= b[i + 1] <= 0x8F:
            m[i + 2:i + 6] = b"\0\0\0\0"; i += 6; continue
        i += 1
    return m


def sig_regex(b, mk=None):
    mk = mask_of(b) if mk is None else mk
    return re.compile(b"".join(re.escape(b[k:k + 1]) if mk[k] else b"." for k in range(len(b))), re.S)


def masked_eq(a, b, mk):
    return len(a) == len(b) and all((not mk[k]) or a[k] == b[k] for k in range(len(a)))


# ---------------------------------------------------------------------------
#  Delta map
# ---------------------------------------------------------------------------
class DeltaMap:
    def __init__(self, old, new, step=64, N=48, use_cache=True):
        self.old, self.new, self.step, self.N = old, new, step, N
        runs = self._load_cache() if use_cache else None
        if runs is None:
            runs = self._build()
            if use_cache:
                self._save_cache(runs)
        self.runs = [tuple(r) for r in runs]                  # (old_lo, old_hi, delta), sorted by old_lo
        self._old_starts = [r[0] for r in self.runs]
        by_new = sorted(self.runs, key=lambda r: r[0] + r[2])
        self._new_runs = by_new
        self._new_starts = [r[0] + r[2] for r in by_new]

    # -- cache ------------------------------------------------------------
    def _key(self):
        h = hashlib.sha1()
        for p in (self.old.path, self.new.path):
            st = os.stat(p)
            h.update(f"{os.path.basename(p)}:{st.st_size}:{int(st.st_mtime)}".encode())
        h.update(f"step={self.step}:N={self.N}:v2".encode())
        return h.hexdigest()[:16]

    def _cache_path(self):
        return os.path.join(CACHE, f"deltamap_{self._key()}.json")

    def _load_cache(self):
        try:
            with open(self._cache_path(), encoding="utf-8") as f:
                return json.load(f)["runs"]
        except (OSError, ValueError, KeyError):
            return None

    def _save_cache(self, runs):
        os.makedirs(CACHE, exist_ok=True)
        with open(self._cache_path(), "w", encoding="utf-8") as f:
            json.dump({"old": self.old.path, "new": self.new.path, "runs": runs}, f)

    # -- build ------------------------------------------------------------
    def _build(self):
        o, n, N = self.old, self.new, self.N
        nt, nlo = n.text, n.text_lo
        anchors, delta = [], 0
        for va in range(o.text_lo, o.text_hi - N, self.step):
            nd = o.rd(va, N)
            if nd.count(0) > N // 2 or nd.count(0xCC) > N // 2 or nd.count(0x90) > N // 2:
                continue
            mk = mask_of(nd)
            if sum(mk) < N * 0.6:
                continue
            rx = sig_regex(nd, mk)
            p = va + delta - nlo
            if 0 <= p and rx.match(nt, p):
                anchors.append((va, delta)); continue
            lo, hi = max(0, p - 0x40000), min(len(nt), p + 0x40000)
            hits = [m.start() for m in rx.finditer(nt, lo, hi)]
            if not hits:
                hits = [m.start() for m in rx.finditer(nt)]
            if len(hits) > 1:
                hits = [h for h in hits if abs((h + nlo - va) - delta) < 0x2000] or hits
            if len(hits) == 1:
                delta = hits[0] + nlo - va
                anchors.append((va, delta))
        runs = []
        for va, d in anchors:
            if runs and runs[-1][2] == d:
                runs[-1][1] = va
            else:
                runs.append([va, va, d])
        # A lone anchor whose delta is far from both neighbours is an ambiguous
        # match that slipped through, not a real one-sample run.
        out = []
        for i, (a, b, d) in enumerate(runs):
            if a == b and 0 < i < len(runs) - 1 \
                    and abs(d - runs[i - 1][2]) > 0x1000 and abs(d - runs[i + 1][2]) > 0x1000:
                continue
            out.append([a, b, d])
        return out

    # -- queries ----------------------------------------------------------
    def old2new(self, va):
        """NEW address of OLD va, only when va lies inside a run's covered range."""
        i = bisect.bisect_right(self._old_starts, va) - 1
        if i < 0:
            return None
        lo, hi, d = self.runs[i]
        return va + d if va < hi + self.N else None

    def new2old(self, va):
        i = bisect.bisect_right(self._new_starts, va) - 1
        if i < 0:
            return None
        lo, hi, d = self._new_runs[i]
        return va - d if va < hi + d + self.N else None

    def neighbour_deltas(self, old_va=None, new_va=None, k=3):
        if old_va is not None:
            i = bisect.bisect_right(self._old_starts, old_va) - 1
            rr = self.runs
        else:
            i = bisect.bisect_right(self._new_starts, new_va) - 1
            rr = self._new_runs
        ds = []
        for j in range(max(0, i - k + 1), min(len(rr), i + k + 1)):
            if rr[j][2] not in ds:
                ds.append(rr[j][2])
        return ds

    def locate(self, src, dst, src_va, before=24, after=24, skip=0, win=0x3000, deltas=()):
        """Find in `dst` the address matching `src_va` in `src`, proven by context.

        The signature is `before` bytes ending at src_va plus `after` bytes
        starting at src_va+skip; the `skip` bytes in between are allowed to
        differ (that is where a patch sits). Candidates: every delta in
        `deltas` first, then a masked search within +-win of each. Returns
        (dst_va, how) or (None, reason).
        """
        # Mask the whole stretch at once, 3 bytes wider on each side, so an
        # absolute address straddling a window edge is still recognised as one.
        whole_lo = src_va - before - 3
        whole = src.rd(whole_lo, before + skip + after + 6)
        if whole is None:
            return None, "unreadable"
        mwhole = mask_of(whole)
        pre, mpre = whole[3:3 + before], mwhole[3:3 + before]
        post, mpost = whole[3 + before + skip:3 + before + skip + after], \
                      mwhole[3 + before + skip:3 + before + skip + after]

        def ok_at(cand):
            if before and not masked_eq(dst.rd(cand - before, before) or b"", pre, mpre):
                return False
            if after and not masked_eq(dst.rd(cand + skip, after) or b"", post, mpost):
                return False
            return True

        for d in deltas:
            if ok_at(src_va + d):
                return src_va + d, "delta"
        rx = sig_regex(pre, mpre) if before >= after else sig_regex(post, mpost)
        off = before if before >= after else -skip
        found = set()
        for d in deltas or (0,):
            c = src_va + d
            lo = max(dst.text_lo, c - win) - dst.text_lo
            hi = min(dst.text_hi, c + win) - dst.text_lo
            for m in rx.finditer(dst.text, lo, hi):
                cand = m.start() + dst.text_lo + off
                if ok_at(cand):
                    found.add(cand)
        if len(found) == 1:
            return found.pop(), "search"
        return None, ("ambiguous" if found else "not found")

    def locate_old(self, new_va, **kw):
        """OLD (our) address of NEW va. Deltas come from the runs around it."""
        ds = []
        d = self.new2old(new_va)
        if d is not None:
            ds.append(-(new_va - d))
        ds += [-x for x in self.neighbour_deltas(new_va=new_va) if -x not in ds]
        return self.locate(self.new, self.old, new_va, deltas=ds, **kw)

    def locate_new(self, old_va, **kw):
        ds = []
        d = self.old2new(old_va)
        if d is not None:
            ds.append(d - old_va)
        ds += [x for x in self.neighbour_deltas(old_va=old_va) if x not in ds]
        return self.locate(self.old, self.new, old_va, deltas=ds, **kw)


def load(step=64):
    """(ours, reborn, deltamap) with the default local paths."""
    ours, reb = Image(OURS), Image(REBORN)
    return ours, reb, DeltaMap(ours, reb, step=step)


if __name__ == "__main__":
    import time
    t0 = time.time()
    ours, reb, dm = load()
    print(f"runs: {len(dm.runs)}  built/loaded in {time.time() - t0:.1f}s")
    covered = sum(min(hi + dm.N, ours.text_hi) - lo for lo, hi, d in dm.runs)
    print(f"coverage of our .text: {covered / (ours.text_hi - ours.text_lo):.1%}")
    holes = []
    for (a, b, d), (a2, b2, d2) in zip(dm.runs, dm.runs[1:]):
        gap = a2 - (b + dm.N)
        if gap > 0x400:
            holes.append((b + dm.N, a2, gap))
    holes.sort(key=lambda h: -h[2])
    print("largest holes:", ", ".join(f"{lo:#x}-{hi:#x} ({g:#x})" for lo, hi, g in holes[:10]))
    for probe in (0x00816C6F, 0x00813C81, 0x00615F30, 0x0040EB89):
        print(f"  {probe:#010x} -> old2new {dm.old2new(probe) and hex(dm.old2new(probe))}"
              f"  locate_new {dm.locate_new(probe)}")
