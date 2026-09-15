"""startcode_diff.py -- semantic comparison of the 20 StartCode.bin files.

A byte or record diff of StartCode.bin drowns in false changes: tag 0x27
(trigger binding) and tag 0x28 (DlgNPC declaration, +0x44) carry SECTION
INDICES into the source's own section table (Vectoren.bin), and a class whose
FunkCode.bin has a different number of sections shifts every index after its
class block. The engine re-resolves both by name (startcode.py), so this tool
compares records in canonical form: the disassembly without offsets, with the
section index replaced by the section's name.

Findings and their interpretation: .claude/knowledge/quests/STARTCODE.md.

    python startcode_diff.py --groups                 content groups (md5)
    python startcode_diff.py --classes [--full]       the 8 base class windows,
                                                      each block tagged with the
                                                      classes that carry it
    python startcode_diff.py --pair A B [--full]      aligned diff of two sources
    python startcode_diff.py --presence               base / addon / net regions
Sources: funkcode_sources keys (base:VAMPIRELADY, addon:SERAPHIM, base:NetScript).
"""
from __future__ import print_function

import argparse
import collections
import difflib
import hashlib
import re
import struct
import sys

import funkcode_disasm as fd
import funkcode_sources as fs
import startcode as sc
import vectoren

CLASSES = ["SERAPHIM", "GLADIATOR", "MAGICIAN", "ELVE", "DARKELVE", "DAEMONIN", "VAMPIRELADY", "ZWERG"]
LETTER = dict(zip(CLASSES, "SGMEDAVZ"))
_OFF = re.compile(r"^\+[0-9a-f]{4}\s+")
_CANON = {}


def canon(key):
    """Canonical text of every record of the source's StartCode.bin."""
    if key in _CANON:
        return _CANON[key]
    secs = vectoren.sections(key)

    def sname(i):
        return secs[i].name if 0 <= i < len(secs) else "#%d" % i

    out = []
    for r in sc.records(key):
        p = r.payload
        if r.tag == sc.TAG_DLGNPC:
            e = p[1:81]
            h = struct.unpack_from("<i", e, 0)[0]
            name = e[4:0x44].split(b"\0", 1)[0].decode("latin-1")
            sec, marker, dh = struct.unpack_from("<iiI", e, 0x44)
            s = "DlgNPC %s sec=%s marker=%d h=%d dh=%d" % (name, sname(sec), marker, h, dh)
        elif r.tag == sc.TAG_TRIGBIND:
            ops, clean = sc.operands(p)
            s = "TrigBind " + " ".join("sec=" + sname(v) if o == sc.OP_I32 else repr(v) for o, v in ops)
            s += "" if clean else " ?"
        else:
            ops, _, _ = fd.disasm_payload(p, indent=0, tag=r.tag)
            body = []
            for ln in ops:
                ln = ln.strip()
                if ln.startswith("flags="):
                    if ln != "flags=0x00":
                        body.append(ln)
                    continue
                body.append(re.sub(r"\s{2,}", " ", _OFF.sub("", ln)))
            s = fd.tag_label(r.tag) + " | " + " ; ".join(body)
        out.append(s)
    _CANON[key] = out
    return out


def groups():
    g = collections.OrderedDict()
    for key, s in fs.SOURCES.items():
        if s.exists("StartCode.bin"):
            g.setdefault(hashlib.md5(s.read("StartCode.bin")).hexdigest(), []).append(key)
    return g


def class_windows():
    """(prefix, suffix, {class: records of its window}) over the 8 base classes."""
    C = {c: canon("base:" + c) for c in CLASSES}
    n = min(len(v) for v in C.values())
    pre = next((i for i in range(n) if len({C[c][i] for c in CLASSES}) > 1), n)
    suf = 0
    while all(len(C[c]) - suf - 1 > pre for c in CLASSES) and \
            len({C[c][len(C[c]) - suf - 1] for c in CLASSES}) == 1:
        suf += 1
    return pre, suf, {c: C[c][pre:len(C[c]) - suf] for c in CLASSES}


def presence_regions():
    """Split every record's multiplicity over base (union of the 8 classes),
    addon (one file) and net (NetScript): pattern 'BAN', 'B.N', ... -> Counter."""
    base = collections.Counter()
    for c in CLASSES:
        base |= collections.Counter(canon("base:" + c))
    addon = collections.Counter(canon("addon:SERAPHIM"))
    net = collections.Counter(canon("base:NetScript"))
    reg = collections.defaultdict(collections.Counter)
    for k in set(base) | set(addon) | set(net):
        b, a, n = base[k], addon[k], net[k]
        for pat, take in (("BAN", lambda: min(b, a, n)), ("BA.", lambda: min(b, a)),
                          ("B.N", lambda: min(b, n)), (".AN", lambda: min(a, n)),
                          ("B..", lambda: b), (".A.", lambda: a), ("..N", lambda: n)):
            m = take()
            if m:
                reg[pat][k] += m
                b -= m if "B" in pat else 0
                a -= m if "A" in pat else 0
                n -= m if "N" in pat else 0
    return reg


def _family(name):
    return re.sub(r"\d+", "#", re.sub(r"^(Dialog:|OMO-?\d*)", "", name))[:28]


def summary(lines):
    tags = collections.Counter(l.split(" ", 1)[0] for l in lines)
    fams = collections.Counter(_family(m.group(1)) for l in lines for m in
                               re.finditer(r"(?:NAME|STR|RESNAME|sec=|DlgNPC) ?'?([A-Za-z_][\w:\-]*)", l))
    return tags, fams


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--groups", action="store_true")
    ap.add_argument("--classes", action="store_true")
    ap.add_argument("--pair", nargs=2)
    ap.add_argument("--presence", action="store_true")
    ap.add_argument("--full", action="store_true", help="print every record, not just the block summary")
    a = ap.parse_args(argv)
    if a.groups:
        for h, keys in groups().items():
            print(h[:10], len(canon(keys[0])), "records:", ", ".join(keys))
    if a.classes:
        pre, suf, W = class_windows()
        print("common head %d records, common tail %d records" % (pre, suf))
        pres = collections.defaultdict(set)
        for c in CLASSES:
            for s in W[c]:
                pres[s].add(c)
        for c in CLASSES:
            print("\n## %s: window [%d:%d], %d records" % (c, pre, pre + len(W[c]), len(W[c])))
            prev = None
            for i, s in enumerate(W[c]):
                sig = "".join(LETTER[k] if k in pres[s] else "." for k in CLASSES)
                if sig != prev:
                    print("  -- [%s]" % sig)
                    prev = sig
                if a.full or "." in sig:
                    print("     #%d %s" % (pre + i, s[:220]))
    if a.pair:
        A, B = canon(a.pair[0]), canon(a.pair[1])
        ops = difflib.SequenceMatcher(None, A, B, autojunk=False).get_opcodes()
        print("%s %d vs %s %d: %d equal" % (a.pair[0], len(A), a.pair[1], len(B),
                                          sum(i2 - i1 for t, i1, i2, _, _ in ops if t == "equal")))
        for t, i1, i2, j1, j2 in ops:
            if t == "equal":
                continue
            print("\n== %s A[%d:%d] -> B[%d:%d]" % (t, i1, i2, j1, j2))
            for side, L, lo, hi in (("A", A, i1, i2), ("B", B, j1, j2)):
                if hi > lo:
                    tags, fams = summary(L[lo:hi])
                    print("  %s tags: %s" % (side, ", ".join("%s %d" % kv for kv in tags.most_common(8))))
                    print("  %s names: %s" % (side, ", ".join("%s %d" % kv for kv in fams.most_common(12))))
                    if a.full or hi - lo <= 12:
                        for k in range(lo, hi):
                            print("    %s#%d %s" % (side, k, L[k][:220]))
    if a.presence:
        reg = presence_regions()
        for pat in ("BAN", "BA.", "B.N", ".AN", "B..", ".A.", "..N"):
            lines = list(reg[pat].elements())
            tags, fams = summary(lines)
            print("%s %d records" % (pat, len(lines)))
            print("   tags: " + ", ".join("%s %d" % kv for kv in tags.most_common(10)))
            print("   names: " + ", ".join("%s %d" % kv for kv in fams.most_common(25)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
