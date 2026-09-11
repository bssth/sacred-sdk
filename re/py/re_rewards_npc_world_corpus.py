"""re_rewards_npc_world_corpus.py -- corpus evidence for the wave-2 R4 cluster
(rewards, hero ops, NPC creation, world and cut-scene ops).

Walks FunkCode.bin + StartCode.bin of the canonical sources (one file per
md5-distinct blob by default), decodes every record of the requested tags with
funkcode_disasm.tile_payload(tag=...), and attributes FunkCode records to the
Vectoren.bin section that contains them (section name + owning quest id).

Read-only on bin/. Nothing here is imported by the SDK.

CLI
    python re_rewards_npc_world_corpus.py --tag 0x7d --forms
    python re_rewards_npc_world_corpus.py --tag 0x7d --examples 5 [--grep RE]
    python re_rewards_npc_world_corpus.py --tag 0x01 --opstats
    python re_rewards_npc_world_corpus.py --tag 0x01 --has-op 0x60 --examples 8
    python re_rewards_npc_world_corpus.py --section dq_belohnung
    --sources SPEC (funkcode_sources spec, default: md5-distinct canonical)
    --files FunkCode.bin,StartCode.bin

API
    iter_records(tags, sources=None, files=...) -> Rec namedtuples
    Rec(src, file, idx, off, tag, size, payload, ops, section, quest)
    ops = [(op, form, value, status)]
"""
import os, sys, re, bisect, argparse, collections, hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import funkcode_sources as FS
import funkcode_disasm as D

try:
    import vectoren as V
except Exception:          # pragma: no cover
    V = None

Rec = collections.namedtuple(
    "Rec", "src file idx off tag size payload ops section quest")

_REC_CACHE = {}
_SEC_CACHE = {}


def distinct_sources(spec=None, fname="FunkCode.bin"):
    """Sources whose `fname` content is md5-distinct (first wins)."""
    srcs = FS.resolve(spec or "all")
    seen, out = set(), []
    for s in srcs:
        if not s.exists(fname):
            continue
        h = hashlib.md5(s.read(fname)).hexdigest()
        if h in seen:
            continue
        seen.add(h)
        out.append(s)
    return out


def records(src, fname):
    key = (src.key, fname)
    if key not in _REC_CACHE:
        buf = src.read(fname)
        _REC_CACHE[key] = [(i, off, tag, size, pl)
                           for i, (off, tag, size, pl) in enumerate(D.walk_records(buf))]
    return _REC_CACHE[key]


def _sections(src):
    if V is None:
        return None
    if src.key not in _SEC_CACHE:
        try:
            secs = [s for s in V.sections(src) if s.length > 0]
        except Exception:
            secs = []
        secs.sort(key=lambda s: s.start)
        _SEC_CACHE[src.key] = (secs, [s.start for s in secs])
    return _SEC_CACHE[src.key]


def section_of(src, off):
    t = _sections(src)
    if not t:
        return None
    secs, starts = t
    i = bisect.bisect_right(starts, off) - 1
    if i < 0:
        return None
    s = secs[i]
    return s if s.start <= off < s.start + s.length else None


def decode(payload, tag):
    t = D.tile_payload(payload, tag=tag)
    return [(o.op, o.form, o.value, o.status) for o in t.ops]


def iter_records(tags, sources=None, files=("FunkCode.bin", "StartCode.bin")):
    tags = set(tags) if tags is not None else None
    for fname in files:
        for src in (sources or distinct_sources(None, fname)):
            if not src.exists(fname):
                continue
            for idx, off, tag, size, pl in records(src, fname):
                if tags is not None and tag not in tags:
                    continue
                sec = section_of(src, off) if fname == "FunkCode.bin" else None
                yield Rec(src.key, fname, idx, off, tag, size, pl, decode(pl, tag),
                          sec.name if sec else None, sec.quest if sec else None)


def fmt_val(form, v):
    if v is None:
        return ""
    if isinstance(v, tuple) and len(v) == 2 and v[0] in ("u32", "var", "symref", "name", "xyz", "raw"):
        if v[0] == "u32":
            return str(v[1])
        if v[0] == "xyz":
            return "xyz%s" % (v[1],)
        return "%s:%r" % (v[0], v[1])
    return repr(v)


def fmt_ops(ops):
    out = []
    for op, form, v, st in ops:
        if st == "end":
            continue
        if op is None:
            out.append("%s=%s" % (form, fmt_val(form, v)))
        else:
            s = fmt_val(form, v)
            out.append(("%02x" % op) + (" " + s if s else ""))
        if st not in ("ok", "end"):
            out.append("<%s>" % st)
    return " | ".join(out)


def signature(ops):
    return " ".join(("%02x" % op) if op is not None else form
                    for op, form, v, st in ops if st != "end")


def fmt_rec(r):
    loc = "%s %s #%d @0x%06x" % (r.src, r.file.split(".")[0], r.idx, r.off)
    sec = ("  [%s q=%s]" % (r.section, r.quest)) if r.section else ""
    return "%s tag 0x%02x%s\n      %s" % (loc, r.tag, sec, fmt_ops(r.ops))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", action="append", default=[])
    ap.add_argument("--sources", default=None)
    ap.add_argument("--files", default="FunkCode.bin,StartCode.bin")
    ap.add_argument("--forms", action="store_true", help="histogram of op signatures")
    ap.add_argument("--opstats", action="store_true", help="per-op record counts")
    ap.add_argument("--values", default=None, help="value histogram for this op (hex)")
    ap.add_argument("--has-op", default=None)
    ap.add_argument("--grep", default=None, help="regex on the formatted record")
    ap.add_argument("--examples", type=int, default=0)
    ap.add_argument("--section", default=None, help="dump a named section's records")
    ap.add_argument("--top", type=int, default=40)
    a = ap.parse_args(argv)
    files = tuple(f.strip() for f in a.files.split(",") if f.strip())
    srcs = None
    if a.sources:
        srcs = FS.resolve(a.sources)

    if a.section:
        for src in (srcs or distinct_sources()):
            try:
                secs = V.sections_named(src, a.section)
            except Exception:
                secs = []
            for s in secs:
                print("== %s section #%d %s {%d,%d} quest %s" % (src.key, s.index, s.name,
                                                                 s.start, s.length, s.quest))
                for idx, off, tag, size, pl in records(src, "FunkCode.bin"):
                    if s.start <= off < s.start + s.length:
                        r = Rec(src.key, "FunkCode.bin", idx, off, tag, size, pl, decode(pl, tag),
                                s.name, s.quest)
                        print("  " + fmt_rec(r))
        return 0

    tags = [int(t, 0) for t in a.tag] or None
    has = int(a.has_op, 0) if a.has_op else None
    rx = re.compile(a.grep) if a.grep else None
    recs = []
    for r in iter_records(tags, srcs, files):
        if has is not None and not any(op == has for op, f, v, st in r.ops):
            continue
        if rx and not rx.search(fmt_rec(r)):
            continue
        recs.append(r)
    print("# %d records" % len(recs))
    if a.forms:
        c = collections.Counter(signature(r.ops) for r in recs)
        for sig, n in c.most_common(a.top):
            print("%7d  %s" % (n, sig))
    if a.opstats:
        c = collections.Counter()
        for r in recs:
            for op in set(op for op, f, v, st in r.ops if op is not None and st == "ok"):
                c[op] += 1
        for op, n in sorted(c.items()):
            print("  op 0x%02x  %6d records  (%s)" % (op, n, D.GRAMMAR[op].form))
    if a.values:
        vop = int(a.values, 0)
        c = collections.Counter()
        for r in recs:
            for op, f, v, st in r.ops:
                if op == vop and st == "ok":
                    c[fmt_val(f, v)] += 1
        for val, n in c.most_common(a.top):
            print("%7d  %s" % (n, val))
    for r in recs[:a.examples]:
        print(fmt_rec(r))
    return 0


if __name__ == "__main__":
    sys.exit(main())
