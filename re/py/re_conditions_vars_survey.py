#!/usr/bin/env python3
"""re_conditions_vars_survey.py -- wave-2 R3 corpus survey (conditions, variables,
counters, timers, triggers).

Every number quoted in sdk/.claude/knowledge/quests/RE_conditions_vars.md comes
from one of the views below.  Read-only: it only reads bin/**.

Record numbering: `#N` = 0-based index in funkcode_disasm.walk_records() order,
`@0x..` = file offset of the record header (same convention as the shards).
Operands are tiled with funkcode_disasm.tile_payload(payload, tag=tag), i.e. the
FUN_00472bc0 grammar.

Views (all take --sources SPEC, default 'canonical' = one file per distinct
FunkCode.bin; --file FunkCode.bin|StartCode.bin|both):

  --conditions      op histogram of IF (0x3a) / ELSEIF (0x42) predicate streams,
                    per-op value forms, first examples, ops that FUN_004987b0
                    does not switch on (its default group)
  --vars            operand shapes of 0x41/0x43/0x44/0x45/0x4b/0x4c/0x69
  --hooks           every 0x87 SetOnKill / 0x88 SetOnCollect / 0x89 SetDrop
  --triggers        0x04 SetBaseTrigger / 0x05 DelBaseTrigger shapes + names
  --timers          0x38 SetTimer / 0x39 DelTimer / 0x7a SolveTime
  --pool            0x85 GetPoolPosition shapes
  --interp          string operands containing 'VAR(' by tag/op
  --grep BYTES      every record whose payload contains BYTES (latin-1), decoded
  --tag HEX         dump every record of one tag (with --limit)
  --questcode       decode QuestCode.bin of the given sources
  --selftest        a few fixed facts (record numbering etc.)
"""
from __future__ import print_function

import argparse
import bisect
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import funkcode_sources as FS   # noqa: E402
import funkcode_disasm as FD    # noqa: E402

try:
    import vectoren as V        # noqa: E402
except Exception:               # pragma: no cover
    V = None

# ops FUN_004987b0 has a case for (jump table 0x499aac, index table 0x499b2c,
# switch on op-0x3a over 0..0x66; everything else lands on 0x4997b9)
PRED_CASES = {
    0x3a: 0x49881f, 0x3e: 0x4994c1, 0x40: 0x4989b8, 0x47: 0x498cd3,
    0x48: 0x499100, 0x49: 0x4992a3, 0x4a: 0x4993a0, 0x4e: 0x498c88,
    0x4f: 0x498ca1, 0x52: 0x498da8, 0x6d: 0x498f6a, 0x6e: 0x499045,
    0x6f: 0x498edb, 0x77: 0x49953b, 0x78: 0x498cba, 0x7a: 0x499756,
    0x7d: 0x498df6, 0x81: 0x4995f0, 0x82: 0x49969c, 0x8f: 0x498e94,
    0x91: 0x498c18, 0x93: 0x498c3c, 0x95: 0x49981a, 0x96: 0x498b2b,
    0x97: 0x498b42, 0x98: 0x498b59, 0x99: 0x498b70, 0x9a: 0x498b87,
    0x9b: 0x498b9e, 0x9c: 0x498bdb, 0xa0: 0x498c2a,
}


# ------------------------------------------------------------------ helpers --
def sources(spec):
    return FS.resolve(spec, default="canonical")


def files_for(which):
    if which == "both":
        return ("FunkCode.bin", "StartCode.bin")
    return (which,)


_CACHE = {}


def records(src, fname="FunkCode.bin"):
    key = (src.key, fname)
    if key not in _CACHE:
        if not src.exists(fname):
            _CACHE[key] = []
        else:
            buf = src.read(fname)
            _CACHE[key] = [(i, off, tag, size, bytes(pl))
                           for i, (off, tag, size, pl) in enumerate(FD.walk_records(buf))]
    return _CACHE[key]


def tile(tag, payload):
    return FD.tile_payload(payload, tag=tag)


def render_ops(tag, payload):
    t = tile(tag, payload)
    parts = []
    for o in t.ops:
        if o.status == "end":
            break
        if o.op is None:
            parts.append("%s=%r" % (o.label, o.value))
        elif o.value is None:
            parts.append("%02x" % o.op)
        else:
            parts.append("%02x:%s" % (o.op, _short(o.value)))
        if o.status not in ("ok", "end"):
            parts.append("<%s>" % o.status)
    if t.leftover:
        parts.append("LEFTOVER%r" % (t.leftover,))
    return " ".join(parts)


def _short(v):
    if isinstance(v, tuple):
        return "(" + ",".join(_short(x) for x in v) + ")"
    if isinstance(v, bytes):
        return v.hex()
    if isinstance(v, int):
        return str(v if v < 0x80000000 else v - 0x100000000)
    return repr(v)


def shape(tag, payload):
    t = tile(tag, payload)
    return "-".join("%02x" % o.op if o.op is not None else o.label
                    for o in t.ops if o.status != "end")


class SectionMap(object):
    """offset -> Vectoren.bin section name (FunkCode.bin only)."""

    def __init__(self, src):
        self.ok = False
        if V is None:
            return
        try:
            secs = [s for s in V.sections(src) if s.length > 0]
        except Exception:
            return
        secs.sort(key=lambda s: s.start)
        self.starts = [s.start for s in secs]
        self.secs = secs
        self.ok = True

    def name(self, off):
        if not self.ok:
            return "?"
        i = bisect.bisect_right(self.starts, off) - 1
        while i >= 0:
            s = self.secs[i]
            if s.start <= off < s.start + s.length:
                return "%s(q%d)" % (s.name, s.quest)
            if s.start + s.length <= off and i < len(self.secs) - 1:
                break
            i -= 1
        return "-"


_SM = {}


def secmap(src):
    if src.key not in _SM:
        _SM[src.key] = SectionMap(src)
    return _SM[src.key]


def where(src, fname, i, off):
    sec = secmap(src).name(off) if fname == "FunkCode.bin" else "StartCode"
    return "%s %s #%d @0x%06x [%s]" % (src.key, fname.split(".")[0], i, off, sec)


# -------------------------------------------------------------------- views --
def view_conditions(srcs, fnames, nex):
    per_op = collections.Counter()
    per_op_rec = collections.Counter()
    per_tag_op = collections.Counter()
    forms = collections.defaultdict(collections.Counter)
    examples = collections.defaultdict(list)
    recs = collections.Counter()
    default_ops = collections.Counter()
    n_ops_hist = collections.Counter()
    fails = 0
    for src in srcs:
        for fname in fnames:
            for i, off, tag, size, pl in records(src, fname):
                if tag not in (0x3a, 0x42):
                    continue
                recs[tag] += 1
                t = tile(tag, pl)
                seen = set()
                n = 0
                for o in t.ops:
                    if o.status == "end":
                        break
                    if o.status != "ok":
                        fails += 1
                        break
                    n += 1
                    per_op[o.op] += 1
                    per_tag_op[(tag, o.op)] += 1
                    if o.op not in PRED_CASES:
                        default_ops[o.op] += 1
                    seen.add(o.op)
                    v = o.value
                    if o.op in (0x48, 0x49, 0x4a, 0x6d, 0x6e):
                        i32, name, name2 = v
                        fam = "LOC_RG*" if name.upper().startswith("LOC_RG") else "var"
                        forms[o.op]["%s i32=%s%s" % (fam, "neg+var" if name2 else ("<0" if i32 < 0 else "n"), "")] += 1
                        if o.op in (0x49, 0x4a):
                            forms[o.op]["bit=%d" % i32] += 0  # keep key space small
                    elif o.op == 0x47:
                        forms[o.op]["asciiz"] += 1
                    elif o.op in (0x96, 0x97, 0x98, 0x99, 0x9a, 0x91, 0xa0, 0x4e, 0x4f, 0x78):
                        forms[o.op]["flag"] += 1
                    elif o.op in (0x9b, 0x9c):
                        forms[o.op]["bit=%d" % v] += 1
                    elif o.op == 0x93:
                        forms[o.op]["u16=%d" % v] += 1
                    elif o.op == 0x6f:
                        forms[o.op]["u32=%d name=%s" % v] += 1
                    elif o.op == 0x7a:
                        forms[o.op]["u32=%d" % v[0]] += 1
                    elif isinstance(v, str):
                        forms[o.op]["str"] += 1
                    if len(examples[o.op]) < nex:
                        examples[o.op].append("%s  %s" % (where(src, fname, i, off), render_ops(tag, pl)))
                n_ops_hist[n] += 1
                for op in seen:
                    per_op_rec[op] += 1
    print("# IF/ELSEIF records: %s; operand-tiling failures: %d" % (dict((hex(k), v) for k, v in recs.items()), fails))
    print("# predicates per record: %s" % dict(sorted(n_ops_hist.items())))
    print("| op | case VA | occurrences | records | in 0x3a | in 0x42 |")
    print("|---|---|---:|---:|---:|---:|")
    for op, c in per_op.most_common():
        print("| 0x%02x | %s | %d | %d | %d | %d |" % (
            op, ("0x%06x" % PRED_CASES[op]) if op in PRED_CASES else "default 0x4997b9",
            c, per_op_rec[op], per_tag_op[(0x3a, op)], per_tag_op[(0x42, op)]))
    print()
    print("# ops outside the FUN_004987b0 switch (default group):", dict((hex(k), v) for k, v in default_ops.items()))
    print()
    for op in sorted(forms):
        top = forms[op].most_common(12)
        print("0x%02x forms: %s" % (op, "; ".join("%s x%d" % kv for kv in top if kv[1])))
    print()
    for op in sorted(examples):
        print("== 0x%02x" % op)
        for e in examples[op]:
            print("   " + e)


def view_values(srcs, fnames, tagset, opfilter, nex):
    """generic: value histogram of one op inside given tags"""
    vals = collections.Counter()
    ex = collections.defaultdict(list)
    for src in srcs:
        for fname in fnames:
            for i, off, tag, size, pl in records(src, fname):
                if tag not in tagset:
                    continue
                for o in tile(tag, pl).ops:
                    if o.status != "ok":
                        break
                    if o.op == opfilter:
                        k = _short(o.value)
                        vals[k] += 1
                        if len(ex[k]) < nex:
                            ex[k].append(where(src, fname, i, off) + "  " + render_ops(tag, pl))
    for k, c in vals.most_common(60):
        print("%6d  %s" % (c, k))
        for e in ex[k]:
            print("          " + e)


def view_shapes(srcs, fnames, tags, nex, names=False):
    for tg in tags:
        sh = collections.Counter()
        ex = collections.defaultdict(list)
        nm = collections.Counter()
        total = 0
        for src in srcs:
            for fname in fnames:
                for i, off, tag, size, pl in records(src, fname):
                    if tag != tg:
                        continue
                    total += 1
                    s = shape(tag, pl)
                    sh[s] += 1
                    if len(ex[s]) < nex:
                        ex[s].append(where(src, fname, i, off) + "  " + render_ops(tag, pl))
                    if names:
                        for o in tile(tag, pl).ops:
                            if o.status == "ok" and o.op == 0x01:
                                nm[o.value] += 1
                                break
        print("### tag 0x%02x: %d records, %d shapes" % (tg, total, len(sh)))
        for s, c in sh.most_common(25):
            print("  %6d  %s" % (c, s))
            for e in ex[s]:
                print("            " + e)
        if names:
            print("  distinct first-name operands: %d; top: %s" % (
                len(nm), ", ".join("%s x%d" % kv for kv in nm.most_common(25))))
        print()


def view_dump(srcs, fnames, tags, limit, grep=None):
    n = 0
    for src in srcs:
        for fname in fnames:
            for i, off, tag, size, pl in records(src, fname):
                if tags and tag not in tags:
                    continue
                if grep is not None and grep not in pl:
                    continue
                print("%s tag=0x%02x size=%d  %s" % (where(src, fname, i, off), tag, size, render_ops(tag, pl)))
                n += 1
                if limit and n >= limit:
                    return


def view_interp(srcs, fnames, nex):
    c = collections.Counter()
    ex = collections.defaultdict(list)
    for src in srcs:
        for fname in fnames:
            for i, off, tag, size, pl in records(src, fname):
                if b"VAR(" not in pl.upper():
                    continue
                for o in tile(tag, pl).ops:
                    if o.status != "ok":
                        break
                    vals = o.value if isinstance(o.value, tuple) else (o.value,)
                    for v in vals:
                        if isinstance(v, str) and "VAR(" in v.upper():
                            k = "tag 0x%02x op %s (%s)" % (tag, ("0x%02x" % o.op) if o.op is not None else o.label, o.form)
                            c[k] += 1
                            if len(ex[k]) < nex:
                                ex[k].append(where(src, fname, i, off) + "  " + repr(v))
    for k, n in c.most_common():
        print("%6d  %s" % (n, k))
        for e in ex[k]:
            print("          " + e)


def view_questcode(srcs):
    for src in srcs:
        if not src.exists("QuestCode.bin"):
            continue
        buf = src.read("QuestCode.bin")
        print("# %s QuestCode.bin %d bytes: %s" % (src.key, len(buf), buf.hex()))
        for i, (off, tag, size, pl) in enumerate(FD.walk_records(buf)):
            print("   #%d @0x%x tag=0x%02x  %s" % (i, off, tag, render_ops(tag, bytes(pl))))


def selftest():
    ok = True
    src = FS.get("base:VAMPIRELADY")
    recs = records(src)
    # AUDIT Severity 6: base:VAMPIRELADY #24377 @0x083be9 : 3a 00 0d | 00 47 "DQ_2600"
    i, off, tag, size, pl = recs[24377]
    c1 = (off == 0x083be9 and tag == 0x3a and size == 13 and pl[1] == 0x47)
    print("record numbering (#24377 @0x083be9 = 3a 47 'DQ_2600'):", "OK" if c1 else "FAIL")
    ok &= c1
    c2 = all(t in PRED_CASES for t in (0x47, 0x48, 0x49, 0x4a, 0x6d, 0x6e, 0x96, 0x9a, 0xa0))
    print("PRED_CASES sanity:", "OK" if c2 else "FAIL")
    ok &= c2
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sources", default="canonical")
    ap.add_argument("--file", default="FunkCode.bin", help="FunkCode.bin | StartCode.bin | both")
    ap.add_argument("--examples", type=int, default=3)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--conditions", action="store_true")
    ap.add_argument("--values", nargs=2, metavar=("TAGS", "OP"),
                    help="value histogram of OP (hex) inside TAGS (hex,comma)")
    ap.add_argument("--vars", action="store_true")
    ap.add_argument("--hooks", action="store_true")
    ap.add_argument("--triggers", action="store_true")
    ap.add_argument("--timers", action="store_true")
    ap.add_argument("--pool", action="store_true")
    ap.add_argument("--interp", action="store_true")
    ap.add_argument("--grep")
    ap.add_argument("--tag")
    ap.add_argument("--questcode", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    srcs = sources(a.sources)
    fnames = files_for(a.file)
    if a.selftest:
        return selftest()
    if a.conditions:
        view_conditions(srcs, fnames, a.examples)
    if a.values:
        tags = set(int(x, 16) for x in a.values[0].split(","))
        view_values(srcs, fnames, tags, int(a.values[1], 16), a.examples)
    if a.vars:
        view_shapes(srcs, fnames, (0x41, 0x43, 0x44, 0x45, 0x4b, 0x4c, 0x69), a.examples, names=True)
    if a.hooks:
        view_dump(srcs, fnames, (0x87, 0x88, 0x89), a.limit)
    if a.triggers:
        view_shapes(srcs, fnames, (0x04, 0x05), a.examples, names=True)
    if a.timers:
        view_shapes(srcs, fnames, (0x38, 0x39, 0x7a), a.examples, names=True)
    if a.pool:
        view_shapes(srcs, fnames, (0x85,), a.examples, names=True)
    if a.interp:
        view_interp(srcs, fnames, a.examples)
    if a.grep:
        view_dump(srcs, fnames, None, a.limit, grep=a.grep.encode("latin1"))
    if a.tag:
        view_dump(srcs, fnames, set(int(x, 16) for x in a.tag.split(",")), a.limit)
    if a.questcode:
        view_questcode(srcs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
