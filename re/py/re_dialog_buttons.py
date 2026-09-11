"""re_dialog_buttons.py -- wave-2 R2 evidence tool: dialog sections, answer
buttons (tag 0x3c), SetNPCState (tag 0x03), SetIcon (tag 0x56), DlgNPC (tag
0x28) and the CreateNPC dialog operand (tag 0x01 op 0x09).

Read-only over bin/. Every number quoted in
sdk/.claude/knowledge/quests/RE_dialog_buttons.md comes from this script.

    python re_dialog_buttons.py [--sources SPEC] [--section NAME] [--json F]
                                [--buttons] [--dialogs] [--tag03] [--tag56]
                                [--dlgnpc] [--create] [--all]

SPEC defaults to 'canonical' (one source per md5-distinct FunkCode blob) for
the corpus-wide counts; per-source record numbers are quoted with their key.

Engine facts the script encodes (see the .md for the evidence):
  * SetButton FUN_00499ba0: first op-0x01 string = label, the LAST later op-0x01
    string = action (disasm 0x499c0b-0x499c68); the button is added only when
    the resolved label has >= 2 chars (0x499c7b test [esp+0x19]); 5 slots.
  * The talk state machine FUN_0052ab70 hands only slots 0..3 to the window
    (0x52bca4..0x52bd07), converting each action string with FUN_00460590
    (case-insensitive, equal-length, first match; -1 if absent, 0 if empty).
"""
import os, sys, re, json, argparse, collections

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import funkcode_sources as FS
import funkcode_disasm as D
import vectoren as V
import startcode as S

COND_TAGS = {0x3a: "IF", 0x42: "ELSEIF", 0x3b: "ELSE"}


def _s(v):
    if isinstance(v, bytes):
        return v.decode("latin-1")
    return v


def ops_of(payload, tag):
    """[(op, label, value)] for one record payload (flag byte included)."""
    t = D.tile_payload(payload, tag=tag)
    out = []
    for o in t.ops:
        if o.status not in ("ok",):
            continue
        out.append((o.op, o.label, _s(o.value)))
    return out


def strings_of(ops, opcode=0x01):
    return [v for (op, lab, v) in ops if op == opcode and isinstance(v, str)]


def engine_find(src, name):
    """FUN_00460590 semantics: 1..63 chars, ASCII case-insensitive, first
    equal-length match; -1 if absent, 0 if name empty/too long."""
    if not name or len(name) >= 0x40:
        return 0
    sec = V.section(src, name)
    return sec.index if sec is not None else -1


def label_form(label):
    if label is None:
        return "none"
    m = re.match(r"(?i)res:(.*)$", label)
    if m:
        rest = m.group(1)
        if rest[:1].isdigit():
            return "res:<digits>"
        return "res:<name>"
    return "plain"


# ----------------------------------------------------------------- census --
def section_segments(recs):
    """Split a section's records into condition arms: a new segment starts at
    every IF / ELSEIF / ELSE record. Returns [[(off,tag,size,payload)]]."""
    segs, cur = [], []
    for r in recs:
        if r[1] in COND_TAGS:
            segs.append(cur)
            cur = []
            continue
        cur.append(r)
    segs.append(cur)
    return segs


def buttons(src):
    out = dict(records=0, label_forms=collections.Counter(),
               labels=collections.Counter(), actions=collections.Counter(),
               action_resolves=collections.Counter(), unresolved=collections.Counter(),
               nstrings=collections.Counter(), short_label=0,
               in_family=collections.Counter(), max_per_segment=collections.Counter(),
               over4=[], ids=collections.Counter(), examples={})
    for sec in V.sections(src):
        recs = V.section_records(src, sec)
        if not recs:
            continue
        for seg in section_segments(recs):
            n = sum(1 for r in seg if r[1] == 0x3c)
            if n:
                out["max_per_segment"][n] += 1
                if n > 4:
                    out["over4"].append((sec.index, sec.name, n))
        for (off, tag, size, payload) in recs:
            if tag != 0x3c:
                continue
            out["records"] += 1
            ops = ops_of(payload, tag)
            strs = strings_of(ops)
            out["nstrings"][len(strs)] += 1
            label = strs[0] if strs else None
            action = strs[-1] if len(strs) >= 2 else ""
            lf = label_form(label)
            out["label_forms"][lf] += 1
            out["labels"][label] += 1
            out["actions"][action] += 1
            if lf == "res:<digits>":
                out["ids"][int(re.match(r"(?i)res:(\d+)", label).group(1))] += 1
            if label is not None and len(label) < 2:
                out["short_label"] += 1
            fam = V.family(sec.name)
            out["in_family"][fam] += 1
            idx = engine_find(src, action)
            key = "resolves" if idx > 0 else ("empty" if idx == 0 else "missing")
            out["action_resolves"][key] += 1
            if idx < 0:
                out["unresolved"][action] += 1
            if (label, action) not in out["examples"]:
                out["examples"][(label, action)] = (sec.name, off)
    return out


def dialogs(src):
    secs = [s for s in V.sections(src) if s.name.lower().startswith("dialog:")]
    tags = collections.Counter()
    shape = collections.Counter()
    with_text = with_btn = empty = 0
    for s in secs:
        recs = V.section_records(src, s)
        if not recs:
            empty += 1
            continue
        ts = [r[1] for r in recs]
        tags.update(ts)
        with_text += (0x1a in ts)
        with_btn += (0x3c in ts)
        shape[tuple(sorted(set(ts)))] += 1
    return dict(count=len(secs), empty=empty, with_text=with_text,
                with_button=with_btn, tags=tags, shapes=shape.most_common(12))


def tag03(src):
    ops = collections.Counter()
    first = collections.Counter()
    combos = collections.Counter()
    records = 0
    examples = {}
    for sec in V.sections(src):
        for (off, tag, size, payload) in V.section_records(src, sec) or []:
            if tag != 0x03:
                continue
            records += 1
            o = ops_of(payload, tag)
            seq = tuple(op for (op, lab, v) in o)
            ops.update(set(seq))
            if seq:
                first[seq[0]] += 1
            combos[tuple(x for x in seq if x != 0x01)] += 1
            for (op, lab, v) in o:
                if op not in examples:
                    examples[op] = (sec.name, off, [(hex(a), l, vv) for (a, l, vv) in o])
    return dict(records=records, ops=ops, first=first, combos=combos.most_common(25),
                examples=examples)


def tag56(src):
    forms = collections.Counter()
    values = collections.Counter()
    named_values = collections.Counter()
    bare_values = collections.Counter()
    examples = {}
    families = collections.defaultdict(collections.Counter)
    records = 0
    for sec in V.sections(src):
        for (off, tag, size, payload) in V.section_records(src, sec) or []:
            if tag != 0x56:
                continue
            records += 1
            o = ops_of(payload, tag)
            names = strings_of(o)
            ints = [v[1] if isinstance(v, tuple) else v for (op, lab, v) in o if op == 0x0b]
            form = ("name+" if names else "bare+") + "int" * len(ints)
            forms[form] += 1
            families[form][V.family(sec.name)] += 1
            for v in ints:
                values[v] += 1
                (named_values if names else bare_values)[v] += 1
                k = (form, v)
                if k not in examples:
                    examples[k] = (sec.name, off, names)
    return dict(records=records, forms=forms, values=values, families=families,
                named=named_values, bare=bare_values, examples=examples)


def dlgnpc(src):
    rows = S.dlgnpcs(src)
    markers = collections.Counter(r.marker for r in rows)
    ok = bad = 0
    badrows = []
    for r in rows:
        want = "dialog:" + (r.name or "").lower()
        got = (r.section_name or "").lower()
        if got == want:
            ok += 1
        else:
            bad += 1
            if len(badrows) < 10:
                badrows.append((r.name, r.section, r.section_name))
    handles = collections.Counter(r.handle for r in rows)
    return dict(count=len(rows), markers=markers, dialog_section_ok=ok,
                dialog_section_bad=bad, bad_examples=badrows, handles=handles)


def create_dialog_op(src):
    """CreateNPC (tag 0x01) op 0x09 = DlgNPC name the spawn is bound to."""
    names = S.dlgnpcs(src)
    known = set((r.name or "").lower() for r in names)
    total = with09 = in_startcode = 0
    missing = collections.Counter()
    for sec in V.sections(src):
        for (off, tag, size, payload) in V.section_records(src, sec) or []:
            if tag != 0x01:
                continue
            total += 1
            o = ops_of(payload, tag)
            d = [v for (op, lab, v) in o if op == 0x09 and isinstance(v, str)]
            if not d:
                continue
            with09 += 1
            if d[-1].lower() in known:
                in_startcode += 1
            else:
                missing[d[-1]] += 1
    return dict(createnpc=total, with_op09=with09, names_a_dlgnpc=in_startcode,
                missing=missing.most_common(15))


def dump_section(src, name, nops=40):
    sec = V.section(src, name)
    if sec is None:
        print("no section %r in %s" % (name, src))
        return
    print("#%d %s {%d,%d} quest=%d" % (sec.index, sec.name, sec.start, sec.length, sec.quest))
    for i, (off, tag, size, payload) in enumerate(V.section_records(src, sec)):
        print("  @0x%06x tag 0x%02x size %3d  %s" % (off, tag, size, payload.hex()))
        for (op, lab, v) in ops_of(payload, tag)[:nops]:
            print("        op 0x%02x %-12s %r" % (op, lab, v))


def _fmt_counter(c, n=15):
    return ", ".join("%s x%d" % (k if not isinstance(k, int) else hex(k) if k > 9 else k, v)
                     for k, v in c.most_common(n))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default="canonical")
    ap.add_argument("--section")
    ap.add_argument("--buttons", action="store_true")
    ap.add_argument("--dialogs", action="store_true")
    ap.add_argument("--tag03", action="store_true")
    ap.add_argument("--tag56", action="store_true")
    ap.add_argument("--dlgnpc", action="store_true")
    ap.add_argument("--create", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    srcs = FS.resolve(a.sources)
    if a.section:
        for s in srcs:
            dump_section(s, a.section)
        return
    want = lambda k: a.all or getattr(a, k)
    dump = {}
    for s in srcs:
        key = s.key
        print("=" * 78)
        print("# %s" % key)
        d = dump.setdefault(key, {})
        if want("buttons"):
            b = buttons(s)
            print("SetButton records: %d; op-01 string count per record: %s"
                  % (b["records"], dict(b["nstrings"])))
            print("  label forms: %s" % dict(b["label_forms"]))
            print("  labels < 2 chars: %d" % b["short_label"])
            print("  top labels: %s" % _fmt_counter(b["labels"], 12))
            print("  top res ids: %s" % ", ".join("%d x%d" % kv for kv in b["ids"].most_common(12)))
            print("  top actions: %s" % _fmt_counter(b["actions"], 12))
            print("  action -> section (FUN_00460590): %s" % dict(b["action_resolves"]))
            print("  unresolved actions: %s" % _fmt_counter(b["unresolved"], 15))
            print("  section families holding buttons: %s" % _fmt_counter(b["in_family"], 10))
            print("  buttons per condition arm: %s" % dict(sorted(b["max_per_segment"].items())))
            print("  arms with >4 buttons: %d %s" % (len(b["over4"]), b["over4"][:8]))
            d["buttons"] = {k: (dict(v) if isinstance(v, collections.Counter) else v)
                            for k, v in b.items() if k != "examples"}
        if want("dialogs"):
            g = dialogs(s)
            print("Dialog: sections: %d (empty %d, with Text %d, with SetButton %d)"
                  % (g["count"], g["empty"], g["with_text"], g["with_button"]))
            print("  record tags in Dialog: sections: %s" % _fmt_counter(g["tags"], 20))
            for shp, n in g["shapes"]:
                print("    shape %s x%d" % ([hex(t) for t in shp], n))
            d["dialogs"] = dict(g, tags=dict(g["tags"]))
        if want("tag03"):
            t = tag03(s)
            print("tag 0x03 records: %d" % t["records"])
            print("  op present (records containing it): %s" % _fmt_counter(t["ops"], 40))
            print("  first op: %s" % _fmt_counter(t["first"], 10))
            for c, n in t["combos"]:
                print("    non-name op sequence %s x%d" % ([hex(x) for x in c], n))
            for op in sorted(t["examples"]):
                sn, off, lst = t["examples"][op]
                print("    e.g. op 0x%02x  %s @0x%06x  %s" % (op, sn, off, lst))
            d["tag03"] = dict(records=t["records"], ops={hex(k): v for k, v in t["ops"].items()})
        if want("tag56"):
            t = tag56(s)
            print("tag 0x56 records: %d forms %s" % (t["records"], dict(t["forms"])))
            print("  values (named form): %s" % _fmt_counter(t["named"], 20))
            print("  values (bare form):  %s" % _fmt_counter(t["bare"], 20))
            print("  section family per form: %s" % {f: _fmt_counter(c, 8) for f, c in t["families"].items()})
            for k in sorted(t["examples"], key=lambda k: (k[0], k[1])):
                print("    e.g. %s value %d  %s @0x%06x %s" % ((k[0], k[1]) + tuple(t["examples"][k])))
            d["tag56"] = dict(records=t["records"], forms=dict(t["forms"]),
                              named=dict(t["named"]), bare=dict(t["bare"]))
        if want("dlgnpc"):
            n = dlgnpc(s)
            print("tag 0x28 DlgNPC: %d; +0x44 names 'Dialog:<name>': %d ok / %d not"
                  % (n["count"], n["dialog_section_ok"], n["dialog_section_bad"]))
            print("  markers (+0x48): %s" % _fmt_counter(n["markers"], 20))
            print("  handles (+0x00): %s" % _fmt_counter(n["handles"], 5))
            if n["bad_examples"]:
                print("  not-Dialog examples: %s" % n["bad_examples"])
            d["dlgnpc"] = dict(count=n["count"], markers=dict(n["markers"]),
                               ok=n["dialog_section_ok"], bad=n["dialog_section_bad"])
        if want("create"):
            c = create_dialog_op(s)
            print("CreateNPC: %d, with op 0x09: %d, of which name a StartCode DlgNPC: %d"
                  % (c["createnpc"], c["with_op09"], c["names_a_dlgnpc"]))
            print("  op-0x09 names with no DlgNPC: %s" % c["missing"])
            d["create"] = c
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(dump, f, indent=1, default=str)


if __name__ == "__main__":
    main()
