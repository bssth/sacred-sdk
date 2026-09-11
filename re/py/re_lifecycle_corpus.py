"""re_lifecycle_corpus.py -- wave-2 R1 (quest lifecycle, journal, flow) corpus
measurements for sdk/.claude/knowledge/quests/RE_lifecycle_journal.md.

Read-only.  Nothing here edits a .bin.  It measures, per script source:

  * the QIS_* / SelfTriggerQuest* section family in Vectoren.bin (counts,
    zero-length stubs, the compiler-emitted 9-byte SelfTriggerQuest body and
    its adjacency to QIS_Trigger<id>);
  * the quest registry's five cached QIS indices and the QuestCode.bin
    {start,len} pair at +0x104/+0x108;
  * every quest-lifecycle record (tags 0x0a-0x0f, 0x14, 0x15, 0x32, 0x35,
    0x36, 0x3f, 0x40, 0x4d, 0x57, 0x75, 0x76, 0x84) in FunkCode.bin and
    StartCode.bin: operand form, the Vectoren section family that contains it,
    and whether its quest-id operand names the quest that owns the section
    ("self") or another one ("other");
  * which section families issue SetUpQuest / TriggerQuest for each registry
    quest, and which registry quests are never set up.

usage (run from sdk/re/py or anywhere):
  python re_lifecycle_corpus.py                        # base:VAMPIRELADY summary
  python re_lifecycle_corpus.py --sources base:VAMPIRELADY,addon:SERAPHIM,base:NetScript
  python re_lifecycle_corpus.py --list 0x84 --limit 20 # individual records
  python re_lifecycle_corpus.py --list 0x15 --grep Region
  python re_lifecycle_corpus.py --json out.json

Record numbers (#) are 0-based indices of funkcode_disasm.walk_records over
the named file of that source, i.e. the numbering the shards use.
"""
from __future__ import print_function

import argparse
import bisect
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import funkcode_sources as FS   # noqa: E402
import funkcode_disasm as D     # noqa: E402
import funkcode_tags as T       # noqa: E402
import vectoren as V            # noqa: E402

LIFE_TAGS = (0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x14, 0x15, 0x32, 0x35,
             0x36, 0x3f, 0x40, 0x4d, 0x57, 0x75, 0x76, 0x84)
ID_TAGS = (0x0a, 0x0b, 0x0c, 0x0d, 0x0f, 0x14, 0x15, 0x36, 0x4d, 0x75)

QIS_RE = re.compile(r"^QIS_(Trigger|OnEnter|OnExit|OnSetUp|OnLose)(-?\d+)$", re.I)
STQ_RE = re.compile(r"^SelfTriggerQuest(Pool)?(-?\d+)$", re.I)
HOOK_RE = re.compile(r"^(Region\d+|Sector\d+)(Init|Enter|Exit)$", re.I)


# ------------------------------------------------------------------ helpers --
def ops_of(tag, payload):
    """Decoded operands up to END (engine semantics)."""
    t = D.tile_payload(payload, tag)
    out = []
    for o in t.ops:
        if o.status == "end":
            break
        out.append(o)
    return out, t


def form_of(ops):
    return " ".join("%02x" % o.op for o in ops) or "(empty)"


def scalar(v):
    """Op.value -> python int / str."""
    if isinstance(v, tuple) and len(v) == 2 and isinstance(v[1], int):
        return v[1]
    if isinstance(v, tuple) and len(v) == 2:
        return v[1]
    return v


def first_id(ops):
    """First 0x0b operand (quest id) or first 0x01 name, else None."""
    for o in ops:
        if o.op == 0x0b:
            return scalar(o.value)
        if o.op == 0x01:
            return scalar(o.value)
    return None


class SecIndex(object):
    """offset -> containing Vectoren section (sections tile FunkCode.bin)."""

    def __init__(self, secs):
        nz = sorted([s for s in secs if s.length > 0], key=lambda s: (s.start, s.index))
        self.starts = [s.start for s in nz]
        self.secs = nz

    def containing(self, off):
        i = bisect.bisect_right(self.starts, off) - 1
        while i >= 0:
            s = self.secs[i]
            if s.start <= off < s.start + s.length:
                return s
            if s.start + s.length <= off:
                return None
            i -= 1
        return None


def fam(sec):
    if sec is None:
        return "(no section)"
    m = QIS_RE.match(sec.name)
    if m:
        return "QIS_" + m.group(1)
    m = STQ_RE.match(sec.name)
    if m:
        return "SelfTriggerQuest" + (m.group(1) or "")
    m = HOOK_RE.match(sec.name)
    if m:
        return re.sub(r"\d+", "<n>", m.group(1)) + m.group(2)
    return V.family(sec.name)


# ---------------------------------------------------------------- measure --
def measure(src_key, want_list=None, list_grep=None, list_limit=None):
    src = FS.get(src_key)
    fk = src.read("FunkCode.bin")
    sc = src.read("StartCode.bin") if src.exists("StartCode.bin") else b""
    qc = src.read("QuestCode.bin") if src.exists("QuestCode.bin") else b""
    secs = V.sections(src)
    reg = V.registry(src)
    idx = SecIndex(secs)
    res = collections.OrderedDict()
    res["source"] = src.key

    # --- QIS family ------------------------------------------------------
    qis = collections.Counter()
    qis_zero = collections.Counter()
    qis_quest_match = collections.Counter()
    qis_names = set()
    trig_by_id = {}
    for s in secs:
        m = QIS_RE.match(s.name)
        if not m:
            continue
        kind, qid = m.group(1), int(m.group(2))
        qis[kind] += 1
        qis_names.add(s.name.lower())
        if s.length == 0:
            qis_zero[kind] += 1
        if s.quest == qid:
            qis_quest_match[kind] += 1
        if kind.lower() == "trigger":
            trig_by_id.setdefault(qid, s)
    res["qis_sections"] = dict(qis)
    res["qis_zero_length"] = dict(qis_zero)
    res["qis_owner_equals_name_id"] = dict(qis_quest_match)
    res["qis_total"] = sum(qis.values())

    stq = collections.Counter()
    stq_shape_ok = 0
    stq_adjacent = 0
    stq_bad = []
    for s in secs:
        m = STQ_RE.match(s.name)
        if not m:
            continue
        pool = bool(m.group(1))
        n = int(m.group(2))
        stq["pool" if pool else "quest"] += 1
        body = fk[s.start:s.start + s.length]
        want = bytes([0x14, 0x00, 0x09, 0x00, 0x0b]) + (n & 0xffffffff).to_bytes(4, "little")
        if body == want:
            stq_shape_ok += 1
        else:
            stq_bad.append((s.index, s.name, s.start, s.length, body.hex()))
        t = trig_by_id.get(n)
        if (not pool) and t is not None and t.start + t.length == s.start:
            stq_adjacent += 1
    res["selftrigger_sections"] = dict(stq)
    res["selftrigger_body_is_TriggerQuest_self"] = stq_shape_ok
    res["selftrigger_immediately_after_QIS_Trigger"] = stq_adjacent
    res["selftrigger_bad"] = stq_bad[:10]
    res["qis_trigger_without_selftrigger"] = sorted(
        q for q in trig_by_id
        if not any(STQ_RE.match(s.name) and int(STQ_RE.match(s.name).group(2)) == q
                   and not STQ_RE.match(s.name).group(1) for s in V.sections_named(src, "SelfTriggerQuest%d" % q)))

    # --- region / sector hooks ---------------------------------------------
    hooks = collections.Counter()
    for s in secs:
        m = HOOK_RE.match(s.name)
        if m:
            hooks[re.sub(r"\d+", "<n>", m.group(1)) + m.group(2)] += 1
    res["hook_sections"] = dict(hooks)

    # --- registry -----------------------------------------------------------
    rq = collections.OrderedDict()
    rq["entries_incl_null"] = len(reg)
    for f in ("trigger", "on_enter", "on_setup", "on_exit", "on_lose"):
        rq[f + "_nonzero"] = sum(1 for q in reg if getattr(q, f))
    rq["questcode_len_nonzero"] = [(q.qid, q.name, q.f104, q.f108,
                                    qc[q.f104:q.f104 + q.f108].hex())
                                   for q in reg if q.f108]
    rq["questcode_file_bytes"] = len(qc)
    rq["registry_trigger_zero_length"] = sum(
        1 for q in reg if q.trigger and 0 < q.trigger < len(secs) and secs[q.trigger].length == 0)
    res["registry"] = rq

    # --- lifecycle records -----------------------------------------------
    tagstats = collections.OrderedDict()
    target_fams = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    listing = []
    reg_ids = set(q.qid for q in reg if q.index)
    for fname, buf in (("FunkCode.bin", fk), ("StartCode.bin", sc)):
        for i, (off, tag, size, payload) in enumerate(D.walk_records(buf)):
            if tag not in LIFE_TAGS:
                continue
            ops, tile = ops_of(tag, payload)
            sec = idx.containing(off) if fname == "FunkCode.bin" else None
            family = fam(sec) if fname == "FunkCode.bin" else "StartCode"
            st = tagstats.setdefault("%02x" % tag, {
                "label": T.label_for(tag), "records": 0, "by_file": collections.Counter(),
                "forms": collections.Counter(), "families": collections.Counter(),
                "target": collections.Counter(), "values": collections.Counter()})
            st["records"] += 1
            st["by_file"][fname] += 1
            st["forms"][form_of(ops)] += 1
            st["families"][family] += 1
            tid = first_id(ops) if tag in ID_TAGS else None
            if tag in ID_TAGS:
                if tid is None:
                    st["target"]["(none: owner)"] += 1
                elif sec is not None and isinstance(tid, int) and tid == sec.quest:
                    st["target"]["self"] += 1
                elif isinstance(tid, int):
                    st["target"]["other" if sec is not None else "startcode"] += 1
                    if tid not in reg_ids:
                        st["target"]["not-in-registry"] += 1
                else:
                    st["target"]["by-name"] += 1
                if tag in (0x14, 0x15, 0x0f, 0x36, 0x4d, 0x75) and tid is not None:
                    target_fams["%02x" % tag][tid][family] += 1
            if tag in (0x57, 0x84, 0x75):
                vals = tuple(scalar(o.value) for o in ops)
                st["values"][repr(vals)[:80]] += 1
            if want_list is not None and tag == want_list:
                line = "%s %s #%d @0x%06x %-22s %s (q%s) | %s" % (
                    src.key, fname, i, off, family,
                    (sec.name if sec else "-"), (sec.quest if sec else "-"),
                    form_of(ops) + "  " +
                    " ".join(repr(scalar(o.value)) for o in ops if o.value is not None))
                if list_grep is None or re.search(list_grep, line, re.I):
                    listing.append(line)
    for k, st in tagstats.items():
        for f in ("by_file", "forms", "families", "target", "values"):
            st[f] = dict(st[f].most_common(25))
    res["tags"] = tagstats

    # --- set-up / trigger coverage per registry quest -------------------------
    cov = collections.OrderedDict()
    for tg in ("15", "14", "0f", "36", "4d", "75"):
        tf = target_fams.get(tg, {})
        covered = [q for q in reg_ids if q in tf]
        fams = collections.Counter()
        for q in covered:
            for f in tf[q]:
                fams[f] += 1
        cov[tg] = {"registry_quests_targeted": len(covered),
                   "registry_quests_not_targeted": len(reg_ids) - len(covered),
                   "families_issuing (quests per family)": dict(fams.most_common(20))}
    never_setup = sorted(q for q in reg_ids if q not in target_fams.get("15", {}))
    res["coverage"] = cov
    res["never_set_up_sample"] = never_setup[:60]
    res["never_set_up_count"] = len(never_setup)
    if want_list is not None:
        res["_listing"] = listing[:list_limit] if list_limit else listing
    return res


def find_text(src_key, needle, limit=None):
    """Every FunkCode/StartCode record whose payload contains `needle`
    (bytes, case-insensitive), with its Vectoren section and owner quest."""
    src = FS.get(src_key)
    idx = SecIndex(V.sections(src))
    low = needle.lower()
    out = []
    for fname in ("FunkCode.bin", "StartCode.bin"):
        if not src.exists(fname):
            continue
        for i, (off, tag, size, payload) in enumerate(D.walk_records(src.read(fname))):
            if low not in payload.lower():
                continue
            ops, _ = ops_of(tag, payload)
            sec = idx.containing(off) if fname == "FunkCode.bin" else None
            out.append("%s %s #%d @0x%06x tag %02x %-16s %s (q%s) | %s" % (
                src.key, fname, i, off, tag, T.label_for(tag),
                sec.name if sec else "-", sec.quest if sec else "-",
                " ".join("%02x:%r" % (o.op, scalar(o.value)) for o in ops)))
            if limit and len(out) >= limit:
                return out
    return out


def _print(res):
    lst = res.pop("_listing", None)
    print(json.dumps(res, indent=1, default=str))
    if lst is not None:
        print("\n# listing (%d lines)" % len(lst))
        for l in lst:
            print(l)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default="base:VAMPIRELADY")
    ap.add_argument("--json", default=None, help="write all results to this file")
    ap.add_argument("--list", default=None, help="hex tag whose records to list")
    ap.add_argument("--grep", default=None, help="regex filter for --list lines")
    ap.add_argument("--limit", type=int, default=None)
    ap.add_argument("--find", default=None,
                    help="list records whose payload contains this text (bytes, case-insensitive)")
    a = ap.parse_args(argv)
    if a.find:
        for s in FS.resolve(a.sources):
            for line in find_text(s.key, a.find.encode("latin1"), a.limit):
                print(line)
        return
    want = int(a.list, 16) if a.list else None
    out = []
    for s in FS.resolve(a.sources):
        r = measure(s.key, want, a.grep, a.limit)
        out.append(r)
        if not a.json:
            _print(dict(r))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(out, f, indent=1, default=str)
        print("wrote", a.json)


if __name__ == "__main__":
    main()
