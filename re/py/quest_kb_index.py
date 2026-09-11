r"""quest_kb_index.py -- the checked-in generator for the quest knowledge
base's master index (README.md §3 said the wave-1 build scripts were
throwaway; this replaces them):

    sdk/.claude/knowledge/quests/INDEX.md     human table: §1-§9 + appendices A-C
    sdk/.claude/knowledge/quests/index.json   the same rows, machine-readable

INPUTS
  quest_index.json  written by quest_index.py and quest_shards.py.  STRUCTURE:
                    the id set and order, category, campaign, record counts,
                    sources, tokens, the base-campaign class diff, shard paths,
                    and -- from quest_shards.py -- the data-derived quest ids
                    ("vectoren".questIds) and the shard tiling counts.
  index.json        the previous output of this tool (first written by the
                    wave-1 assembly pass).  Only its CURATED fields are read,
                    and they are preserved: title, numericQuestId(s),
                    whatHappens, whatHappensSource, detailFiles.
  <detail>.md       every HQ_*/NQ_*/DQ_*/RB_*/GQ_*/SQ_* markdown file in the
                    directory: headings -> GitHub anchors, to validate every
                    curated link and to supply links for ids that have none.

CORRECTIONS applied on top of the curated fields (AUDIT.md).  Each is
re-measured from the script blobs on every run and NOT applied (FAIL line,
exit 1) when the data no longer supports it:
  Severity 3   numeric quest ids of the GQ board quests from their own
               tag-0x35 journal records (base:VAMPIRELADY); HQ_6_6_1 sets bit
               9 of quest 61, not bit 10 (#73043 @0x1b31a6).
  Severity 9   one sentence under "How to read the columns".
  Severity 10b the ELVE-only Vectoren.bin section sq_test in §9 (measured).

CHECKS (always run; --check runs them and writes nothing)
  * the id set equals the previous index.json's -- a regeneration must not
    add, drop or rename an id (override: --allow-id-change);
  * every row's shard file exists on disk;
  * every detail link resolves to a heading anchor of its file.

ONE-TIME IMPORT (--import-index-md PATH): wave-1 left INDEX.md and index.json
disagreeing on some cells (the 34 WW rows: INDEX.md title "Arena" + a
mechanics sentence, index.json 'signpost label "Arena"' twice).  The import
adopts the INDEX.md cell into the curated record and logs the replaced value
in index.json "corrections", so nothing is lost and the twins agree.

    python sdk/re/py/quest_kb_index.py                 # write INDEX.md + index.json
    python sdk/re/py/quest_kb_index.py --check         # verify only
    python sdk/re/py/quest_kb_index.py --diff          # also print a diff of INDEX.md
    python sdk/re/py/quest_kb_index.py --out-dir DIR   # write the pair elsewhere
"""
from __future__ import print_function

import argparse
import collections
import difflib
import json
import os
import re
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import funkcode_sources as fs   # noqa: E402
import funkcode_disasm as fd    # noqa: E402
import funkcode_tags as ft      # noqa: E402
import quest_index as qi        # noqa: E402
import vectoren as V            # noqa: E402

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

KB = qi.OUT_DIR
QUEST_INDEX_JSON = os.path.join(KB, "quest_index.json")
INDEX_JSON = os.path.join(KB, "index.json")
INDEX_MD = os.path.join(KB, "INDEX.md")
DETAIL_FILE_RE = re.compile(r"^(?:HQ|NQ|DQ|RB|GQ|SQ)[A-Za-z0-9_]*\.md$")
TWO_PART_HQ = re.compile(r"^HQ_\d+_\d+$")
PLAYABLE = ["HQ", "NQ", "RB", "DQ", "GQ", "SQ"]
CLASS_ABBR = collections.OrderedDict([
    ("SERAPHIM", "SERA"), ("GLADIATOR", "GLAD"), ("MAGICIAN", "MAGE"), ("ELVE", "ELF"),
    ("DARKELVE", "DELF"), ("DAEMONIN", "DEM"), ("VAMPIRELADY", "VAMP"), ("ZWERG", "DWA")])
WORDS = {1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 6: "six", 7: "seven", 8: "eight",
         9: "nine", 10: "ten", 11: "eleven", 12: "twelve"}
BASE = fs.BASELINE          # base:VAMPIRELADY


def word(n, cap=False):
    w = WORDS.get(n, str(n))
    return w[:1].upper() + w[1:] if cap else w


# ================================================================ anchors ===
def gh_slug(text):
    """GitHub heading anchor: lower-case, drop everything that is not a
    letter/digit/underscore/hyphen/space, spaces -> hyphens.  Heading text is
    taken literally -- `<n>` stays 'n' -- which is the convention the curated
    links use (RB_SQ_misc_1.md:818 'WW_BREAK — signpost label "<n>"' ->
    ww_break--signpost-label-n; a renderer that parses <n> as an HTML tag
    would drop it)."""
    t = text.strip().lower()
    t = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", t)
    t = re.sub(r"[^\w\- ]", "", t)
    return t.replace(" ", "-")


def headings(path):
    """[(level, text, anchor, line)] outside code fences, GitHub de-dup suffixes."""
    out, seen, fence = [], collections.Counter(), False
    with open(path, encoding="utf-8") as fh:
        for no, line in enumerate(fh, 1):
            s = line.rstrip("\n")
            if re.match(r"^ {0,3}(```|~~~)", s):
                fence = not fence
                continue
            if fence:
                continue
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", s)
            if not m:
                continue
            base = gh_slug(m.group(2))
            n = seen[base]
            seen[base] += 1
            out.append((len(m.group(1)), m.group(2), base if n == 0 else "%s-%d" % (base, n), no))
    return out


def heading_id(text):
    """The quest id a heading documents: its first token after optional
    numbering ('1. NQ_UW11 — ...', '§1 ...'), upper-cased; else None."""
    t = re.sub(r"[`*]", "", text).strip()
    t = re.sub(r"^§?\d+(?:\.\d+)*\.?\s+", "", t)
    tok = t.split()[0].rstrip(":,") if t.split() else ""
    return tok.upper() if re.match(r"^(?:HQ|NQ|DQ|RB|GQ|SQ|WW)_?[A-Za-z0-9_.]+$", tok) else None


def scan_details(kb=KB):
    """{file: {"anchors": set, "byid": {ID: [anchor]}}} for every detail file."""
    out = collections.OrderedDict()
    for fn in sorted(os.listdir(kb)):
        if not DETAIL_FILE_RE.match(fn):
            continue
        hs = headings(os.path.join(kb, fn))
        byid = collections.defaultdict(list)
        for lvl, text, anchor, no in hs:
            hid = heading_id(text)
            if hid:
                byid[hid].append(anchor)
        out[fn] = {"anchors": set(a for _, _, a, _ in hs), "byid": byid, "headings": len(hs)}
    return out


# ================================================================ curated ===
def load_curated(path):
    if not os.path.isfile(path):
        return collections.OrderedDict(), None
    with open(path, encoding="utf-8") as fh:
        d = json.load(fh)
    out = collections.OrderedDict()
    for q in d["quests"]:
        ids = q.get("numericQuestIds")
        if ids is None:
            ids = [q["numericQuestId"]] if q.get("numericQuestId") is not None else []
        out[q["id"]] = {"title": q.get("title"), "numericQuestIds": list(ids),
                        "whatHappens": q.get("whatHappens"),
                        "whatHappensSource": q.get("whatHappensSource"),
                        "detailFiles": [dict(x) for x in (q.get("detailFiles") or [])]}
    return out, d


def parse_index_md(path):
    """{id: (section heading, [raw cells])} for every table row of an INDEX.md."""
    rows, sec = {}, None
    with open(path, encoding="utf-8") as fh:
        for ln in fh:
            s = ln.rstrip("\n")
            if s.startswith("## "):
                sec = s[3:]
                continue
            m = re.match(r"^\| `([^`]+)` \| (.*) \|$", s)
            if m and sec:
                rows[m.group(1)] = (sec, m.group(2).split(" | "))
    return rows


def import_index_md(path, curated, corrections):
    """Adopt INDEX.md title / what-happens cells that disagree with index.json."""
    n = 0
    for qid, (sec, cells) in parse_index_md(path).items():
        c = curated.get(qid)
        if c is None:
            continue
        if sec.startswith("8."):
            pairs = (("title", cells[0]), ("whatHappens", cells[1]))
        elif sec.startswith("9."):
            pairs = (("title", cells[1]), ("whatHappens", cells[2]))
        elif sec.startswith("Appendix C"):
            pairs = ()
        elif sec.startswith("Appendix"):
            pairs = (("whatHappens", cells[2]),)
        elif re.match(r"^[1-7]\.", sec):
            pairs = (("title", cells[2]), ("whatHappens", cells[3]))
        else:
            pairs = ()
        for field, cell in pairs:
            if (c.get(field) or "") != cell:
                corrections.append({"id": qid, "field": field, "source": "INDEX.md import",
                                    "reason": "wave-1 INDEX.md and index.json disagreed; the INDEX.md cell is"
                                              " adopted so the twins agree",
                                    "previous": c.get(field), "value": cell})
                c[field] = cell
                n += 1
    return n


# ============================================================ corrections ===
GQ_IDS = collections.OrderedDict([          # AUDIT.md Severity 3 table
    ("GQ_KHORADNUR", [9000]), ("GQ_RG1", [9004, 9005, 9105]), ("GQ_RG6", [9006]),
    ("GQ_RGHS", [9007]), ("GQ_RG15", [9008]), ("GQ_RG22", [9009]), ("GQ_9010", [9010])])
HQ661 = ("HQ_6_6_1", "whatHappens", "(bit 10, autosave 30017)", "(bit 9, autosave 30017)")


class Measure(object):
    """Everything the corrections re-measure, from the shipped blobs."""

    def __init__(self):
        src = fs.get(BASE)
        self.recs = list(fd.walk_records(src.read()))
        self.gq = collections.defaultdict(collections.Counter)
        self.gq_first = {}
        for i, (off, tag, size, p) in enumerate(self.recs):
            if tag != 0x35:
                continue
            t = fd.tile_payload(p, tag)
            ints = [o.value[1] for o in t.ops if o.status == "ok" and o.op == 0x0b and o.value[0] == "u32"]
            if not ints:
                continue
            for o in t.ops:
                if o.status == "ok" and isinstance(o.value, str) and o.value.lower().startswith("res:gq_"):
                    h = qi.classify(o.value[4:])
                    if h and h[0] == "GQ":
                        self.gq[h[1]][ints[0]] += 1
                        self.gq_first.setdefault((h[1], ints[0]), (i, off, o.value))

    def bitset(self, index):
        off, tag, size, p = self.recs[index]
        return off, tag, [(o.label, o.value) for o in fd.tile_payload(p, tag).ops]

    @staticmethod
    def sq_test():
        """(present [(key, Section, [(off, tag, rendered)])], absent [key], funkcode_hits)."""
        present, absent = [], []
        for s in fs.resolve("all", default="all"):
            hits = V.sections_named(s, "sq_test")
            if hits:
                for sec in hits:
                    recs = [(off, tag, " ".join(fd.render_value(o) for o in fd.tile_payload(p, tag).ops))
                            for off, tag, size, p in V.section_records(s, sec)]
                    present.append((s.key, sec, recs))
            else:
                absent.append(s.key)
        fk = sum(s.read().lower().count(b"sq_test") for s in fs.resolve("all", default="all"))
        return present, absent, fk


def apply_corrections(curated, meas, corrections, problems):
    # --- Severity 3a: GQ ids
    for gid, exp in GQ_IDS.items():
        got = sorted(meas.gq.get(gid, {}))
        ev = ", ".join("%d x%d (first #%d @0x%06x %s)" % (
            q, meas.gq[gid][q], meas.gq_first[(gid, q)][0], meas.gq_first[(gid, q)][1],
            meas.gq_first[(gid, q)][2]) for q in got)
        if got != sorted(exp):
            problems.append("AUDIT Sev 3 %s: expected ids %s, the data now gives %s -- not applied" % (gid, exp, got))
            continue
        c = curated.get(gid)
        if c is None:
            continue
        if c["numericQuestIds"] != sorted(exp):
            corrections.append({"id": gid, "field": "numericQuestIds", "source": "AUDIT.md Severity 3",
                                "reason": "ids of the id's own tag-0x35 journal records, %s" % BASE,
                                "evidence": ev, "previous": c["numericQuestIds"], "value": sorted(exp)})
            c["numericQuestIds"] = sorted(exp)
        else:
            corrections.append({"id": gid, "field": "numericQuestIds", "source": "AUDIT.md Severity 3",
                                "reason": "verified, already applied", "evidence": ev, "value": sorted(exp)})
    # --- Severity 3b: HQ_6_6_1 bit 9
    qid, field, old, new = HQ661
    off, tag, ops = meas.bitset(73043)
    off10, tag10, ops10 = meas.bitset(73426)
    ok = (off == 0x1b31a6 and tag == 0x44 and ops == [("NAME", "61"), ("INT", ("u32", 9))]
          and tag10 == 0x44 and ops10 == [("NAME", "61"), ("INT", ("u32", 10))])
    ev = ("%s #73043 @0x%06x tag 0x%02x SetVarBit %s; bit 10 is #73426 @0x%06x (another quest)"
          % (BASE, off, tag, ops, off10))
    if not ok:
        problems.append("AUDIT Sev 3 HQ_6_6_1: record #73043 no longer reads SetVarBit '61' bit 9 -- not applied")
    elif qid in curated:
        c = curated[qid]
        if old in (c.get(field) or ""):
            corrections.append({"id": qid, "field": field, "source": "AUDIT.md Severity 3",
                                "reason": "HQ_6_6_1 sets bit 9 of quest 61", "evidence": ev,
                                "previous": c[field], "value": c[field].replace(old, new)})
            c[field] = c[field].replace(old, new)
        elif new in (c.get(field) or ""):
            corrections.append({"id": qid, "field": field, "source": "AUDIT.md Severity 3",
                                "reason": "verified, already applied", "evidence": ev, "value": c[field]})
        else:
            problems.append("AUDIT Sev 3 HQ_6_6_1: neither %r nor %r in whatHappens" % (old, new))


# ================================================================== rows ===
def classes_cell(q):
    if not q["classSpecific"]:
        return "shared"
    bd = q.get("baseClassDiff") or {}
    missing = bd.get("missing") or []
    if missing:
        return "only " + "/".join(CLASS_ABBR[c] for c in CLASS_ABBR if c not in missing)
    diff = bd.get("differing") or []
    if diff:
        return "varies: " + "/".join(CLASS_ABBR[c] for c in CLASS_ABBR if c in diff)
    return "varies (addon)"


def ids_cell(ids):
    return ", ".join(str(x) for x in ids) if ids else "-"


def doc_cell(links):
    if not links:
        return "-"
    return " + ".join("[%s](%s#%s)" % (l["file"][:-3], l["file"], l["anchor"]) for l in links)


def section_of(q):
    cat = q["category"]
    if cat == "HQ":
        return "2" if TWO_PART_HQ.match(q["id"]) else "1"
    return {"NQ": "3", "RB": "4", "GQ": "5", "SQ": "6", "DQ": "7", "WW": "8", "OTHER": "9",
            "DQTPL": "A", "VAR": "B", "DQSLOT": "C"}.get(cat, "9")


def _num(s):
    m = re.search(r"\d+", s)
    return int(m.group()) if m else 0


ORDER = {
    "3": lambda r: (r["ids"][0] if r["ids"] else 10 ** 9, r["pos"]),
    "8": lambda r: r["id"],
    "9": lambda r: r["id"],
    "A": lambda r: (0 if r["id"].startswith("DQ_RG") else 1, _num(r["id"])),
    "B": lambda r: r["id"],
    "C": lambda r: _num(r["id"]),
}


# ============================================================== rendering ===
HEADINGS = collections.OrderedDict([
    ("1", ("1. Main quest - base campaign (HQ, 3-part ids)", "Main quest - base campaign (HQ, 3-part ids)", "%d entries")),
    ("2", ("2. Main quest - Underworld addon (HQ, 2-part ids)", "Main quest - Underworld addon (HQ, 2-part ids)", "%d entries")),
    ("3", ("3. Named side quests (NQ)", "Named side quests (NQ)", "%d")),
    ("4", ("4. Runebook / region-clearing quests (RB)", "Runebook / region-clearing quests (RB)", "%d")),
    ("5", ("5. Goodie errands and the bounty officer (GQ)", "Goodie errands and the bounty officer (GQ)", "%d")),
    ("6", ("6. Class-special scenes (SQ)", "Class-special scenes (SQ)", "%d")),
    ("7", ("7. Daily / village quests (DQ)", "Daily / village quests (DQ)", "%d")),
    ("8", ("8. Not quests: signpost and MP-start labels (WW)", "Not quests: signpost labels (WW)", "%d")),
    ("9", ("9. Not quests: dotted HQ level objects and misc ids (OTHER)", "Not quests: dotted HQ level objects (OTHER)", "%d")),
    ("A", ("Appendix A - daily-quest templates (DQTPL)", "Appendix A - daily-quest templates (DQTPL)", "%d")),
    ("B", ("Appendix B - generator namespaces (VAR)", "Appendix B - generator namespaces (VAR)", "%d")),
    ("C", ("Appendix C - daily-quest slot variables (DQSLOT)", "Appendix C - daily-quest slot variables (DQSLOT)", "%d")),
])
TABLE_HEAD = {
    "main": "| id | quest id | classes | title | what happens | documented in |\n|---|---|---|---|---|---|",
    "ww": "| id | title | what happens | documented in |\n|---|---|---|---|",
    "other": "| id | quest id | title | what happens | documented in |\n|---|---|---|---|---|",
    "app": "| id | campaign | recs | what happens |\n|---|---|---|---|",
    "slot": "| id | recs | tokens |\n|---|---|---|",
}
COLS = {"1": "main", "2": "main", "3": "main", "4": "main", "5": "main", "6": "main", "7": "main",
        "8": "ww", "9": "other", "A": "app", "B": "app", "C": "slot"}


def row_line(r, cols):
    if cols == "main":
        return "| `%s` | %s | %s | %s | %s | %s |" % (r["id"], ids_cell(r["ids"]), r["classes"], r["title"],
                                                    r["what"], doc_cell(r["links"]))
    if cols == "ww":
        return "| `%s` | %s | %s | %s |" % (r["id"], r["title"], r["what"], doc_cell(r["links"]))
    if cols == "other":
        return "| `%s` | %s | %s | %s | %s |" % (r["id"], ids_cell(r["ids"]), r["title"], r["what"],
                                               doc_cell(r["links"]))
    if cols == "app":
        return "| `%s` | %s | %d | %s |" % (r["id"], r["campaign"], r["records"], r["what"])
    return "| `%s` | %d | %s |" % (r["id"], r["records"], ", ".join("`%s`" % t for t in r["tokens"]))


def intro(sec, rows, ctx):
    n = len(rows)
    if sec == "1":
        tail = ("divergence is one 8,821-byte window)." if not ctx.get("sq_test") else
                "divergence is one 8,821-byte window of `FunkCode.bin`; the `Vectoren.bin`\n"
                "section table diverges separately - `sq_test`, §9).")
        return ("The original Sacred campaign, in `bin/TYPE_NPC_<CLASS>/FunkCode.bin`. Acts I-IV.\n"
                "The chapter-3 block is the per-class prologue: eight different openings that all\n"
                "converge on Sergeant Treville at Porto Vallum. Everything from `HQ_4_1_1` on is\n"
                "byte-identical in all eight classes (`CLASS_DIFFS.md` §4: the entire per-class\n" + tail)
    if sec == "2":
        return ("`bin/Addon/*/FunkCode.bin` (all eight class dirs are byte-identical) and\n"
                "`bin/NetScript`. Ids are `10*chapter + step`, **except that `HQ_5_2` uses id 53\n"
                "and `HQ_5_3` uses id 52** - they are swapped in the data (`HQ_addon_1.md` §0.1).")
    if sec == "3":
        ncs = sum(1 for r in rows if r["classes"] != "shared")
        lead = ("None of the %d is class-specific by bytes" % n) if ncs == 0 else \
            ("%d of the %d are class-specific by bytes" % (ncs, n))
        return ("`NQ_5001..6003` are base-campaign side quests; `NQ_76xx`, `NQ_UW*` and `NQ_CA`\n"
                "are Underworld. %s, but two branch on the\n"
                "hero class at runtime (`NQ_7629`, `NQ_CA` - see `NQ_2.md` §0.7)." % lead)
    if sec == "4":
        return ("%s instances of one template: awaken the region's boss group, kill all of it.\n"
                "The completion test is tag `0x18` `GroupIsDead`; while a member lives the compass\n"
                "parks on the nearest one. Full family write-up in `RB_SQ_misc_1.md` §1.0." % word(n, True))
    if sec == "5":
        ndoc = sum(1 for r in rows if r["links"])
        if ndoc == 0:
            p1 = ("**The one gap in the detail coverage.** These %s ids have shards and resolved\n"
                  "text but no per-quest detail file yet; the one-liners below come from their\n"
                  "`global.res` strings (`shards/base/<id>.txt` header block). `GQ_RG<n>` are the\n"
                  "region goodie-errand chains, `GQ_KOPFGELD` the bounty officer, `GQ_MP` the \"Wanted\" poster.\n"
                  "Documenting them is the top open item in `README.md` §6 (\"What is missing\")." % word(n))
        else:
            p1 = ("`GQ_RG<n>` are the region goodie-errand chains, `GQ_KOPFGELD` the bounty officer and\n"
                  "`GQ_MP` the single-player \"Wanted\" poster (no GQ board, `GQ_1.md` S4). %d of these %d ids have a detail file; for the rest the one-liners\n"
                  "come from their `global.res` strings (shard header block)." % (ndoc, n))
        return p1 + "\n\n" + ctx["gq_ids_text"]
    if sec == "6":
        return ("Not questbook quests: single spoken lines inside scripted encounters, keyed by\n"
                "class. `RB_SQ_misc_1.md` §2.0.")
    if sec == "7":
        return ("%d **authored** quests (not the runtime generator - that is Appendix A/C).\n"
                "`DQ_10250..10261` are the twelve base-campaign village dailies with per-quest\n"
                "reward rolls; `DQ_15001..15084` are the regional errands that all pay the same\n"
                "shared `dq_belohnung` roll. Families: fetch-object, escort/retrieve-NPC,\n"
                "kill-named, kill-N-of-type, collect-N-drops." % n)
    if sec == "8":
        return ("Text fragments assembled line by line into a signpost dialog (`wegweiser_NN`) or\n"
                "the multiplayer start menu. Kept in the index because the corpus scan produces\n"
                "them; filter them out for quest work.")
    if sec == "9":
        return ("`HQ_x.y.z` names belong to **level objects** - teleport zones (`tpreinz*` /\n"
                "`tprausz*`, tag `0x2e`) and doors (`*_Tuer<n>`, tags `0x08`+`0x02`) - that gate a\n"
                "main-quest step. Their mapping onto the underscore quest ids is deliberately not\n"
                "guessed; the `quest id` column shows the quest each one's own records name.")
    if sec == "A":
        return ("The runtime daily-quest generator. `DQ_RG<n>` is region n's template text pool,\n"
                "`DQ<n>` its name pool; the generator picks a slot variable (Appendix C), a giver\n"
                "archetype and a target, then composes the resource names at runtime\n"
                "(`res:DQ1_BRINGE_NPC_ZIEL+VAR(DQ_2600)`). Regions run 1..23; **`DQ_RG14` and\n"
                "`DQ14` do not exist**. Whether `DQ_RG<n>` and `DQ<n>` are two halves of one\n"
                "template set is unproven, so they stay separate ids.")
    if sec == "B":
        return ("Shared variables and text pools the generator and the storyline reuse. These are\n"
                "machinery, not quests - but they are where the shared strings live\n"
                "(\"Quest completed.\", \"Quest failed.\", \"Quest abandoned.\") and where the shared\n"
                "reward routine `dq_belohnung` is named.")
    if sec == "C":
        nums = [_num(r["id"]) for r in rows]
        camp = sorted(set(r["campaign"] for r in rows))
        text = ("%d generator slot variables, ids %d-%d. Each is a `DQ_<n>` script variable\n"
                "(plus `DQ_<n>_RICHTUNG`, the search direction) that a region template fills in at\n"
                "runtime; evidence: `base:VAMPIRELADY` record 24383 declares `DQ_2600` with\n"
                "initialiser `DQ_Quest`, record 24366 emits\n"
                "`res:DQ1_BRINGE_NPC_ZIEL+VAR(DQ_2600)`. **None of them is a playable quest.**\n"
                "They are listed for completeness; their shards are `shards/%s/DQ_<n>.txt`."
                % (n, min(nums), max(nums), "/".join(camp)))
        if ctx.get("slot_registry"):
            text += "\n\n" + ctx["slot_registry"]
        return text
    return ""


def sq_test_block(present, absent, fk):
    if len(present) != 1:
        return None
    key, sec, recs = present[0]
    rec_txt = " and\n".join("`@0x%06x` tag `0x%02x` %s `%s`" % (off, tag, ft.label_for(tag), txt)
                            for off, tag, txt in recs)
    return ("### Developer leftover outside the index: `sq_test`\n\n"
            "Not one of the indexed ids - the string occurs in no `FunkCode.bin` of the 20\n"
            "sources (%d hits), so the token scan never sees it - but a real `Vectoren.bin`\n"
            "section, present in `%s` only (absent from the other %d sources): section\n"
            "#%d `{start=%d, len=%d}`, quest %d. Its %d bytes are %s record%s:\n"
            "%s -\n"
            "a developer test-warp callable by name (AUDIT Severity 10b). It is also why the\n"
            "per-class divergence is not only the `FunkCode.bin` window (`CLASS_DIFFS.md` §4):\n"
            "the section table differs too. Re-measured on every run\n"
            "(`python sdk/re/py/vectoren.py --sources %s --section sq_test --records --ops 8`)."
            % (fk, key, len(absent), sec.index, sec.start, sec.length, sec.quest, sec.length,
               word(len(recs)), "" if len(recs) == 1 else "s", rec_txt, key))


# =================================================================== main ===
def build(args):
    t0 = time.time()
    problems, notes, corrections = [], [], []
    with open(args.quest_index, encoding="utf-8") as fh:
        qidx = json.load(fh)
    quests = qidx["quests"]
    curated, prev = load_curated(args.curated)

    # ---- id set
    new_ids = [q["id"] for q in quests]
    if len(set(new_ids)) != len(new_ids):
        problems.append("duplicate ids in quest_index.json")
    if prev is not None:
        a, b = set(curated), set(new_ids)
        if a != b:
            msg = "id set changed: %d dropped %s, %d added %s" % (
                len(a - b), sorted(a - b)[:10], len(b - a), sorted(b - a)[:10])
            (notes if args.allow_id_change else problems).append(msg)
    # ---- the one-time import's log is history: carry it forward on every run
    corrections.extend(c for c in ((prev or {}).get("corrections") or [])
                       if c.get("source") == "INDEX.md import")
    # ---- one-time import
    if args.import_index_md:
        n = import_index_md(args.import_index_md, curated, corrections)
        notes.append("imported %d INDEX.md cell(s) that disagreed with index.json" % n)
    # ---- corrections
    meas = Measure()
    apply_corrections(curated, meas, corrections, problems)
    present, absent, fk = Measure.sq_test()
    sq_block = sq_test_block(present, absent, fk)
    if sq_block is None:
        problems.append("AUDIT Sev 10b: sq_test is in %d sources (expected exactly 1: base:ELVE) -- not listed"
                        % len(present))

    # ---- detail anchors
    details = scan_details(os.path.dirname(args.curated) if args.curated else KB)
    rows = []
    for pos, q in enumerate(quests):
        c = curated.get(q["id"]) or {}
        links = [dict(l) for l in (c.get("detailFiles") or [])]
        for l in links:
            d = details.get(l["file"])
            if d is None:
                problems.append("%s: detail file %s missing" % (q["id"], l["file"]))
            elif l["anchor"] not in d["anchors"]:
                problems.append("%s: anchor %s#%s does not resolve" % (q["id"], l["file"], l["anchor"]))
        if not links:
            for fn, d in details.items():
                for anchor in d["byid"].get(q["id"].upper(), []):
                    links.append({"file": fn, "anchor": anchor})
                    notes.append("%s: detail link discovered from headings: %s#%s" % (q["id"], fn, anchor))
        if not c:
            notes.append("%s: no curated entry -- defaults used" % q["id"])
        bd = q.get("baseClassDiff") or {}
        data_ids = list((q.get("vectoren") or {}).get("questIds") or [])
        rows.append({
            "id": q["id"], "pos": pos, "q": q, "category": q["category"], "campaign": q["campaign"],
            "records": q["records"], "tokens": q["tokens"],
            "classes": classes_cell(q),
            "ids": list(c.get("numericQuestIds") or []),
            "title": c.get("title") if c.get("title") is not None else (q.get("title") or "(no questbook title)"),
            "what": c.get("whatHappens") or "(not curated yet - see the shard)",
            "whatSource": c.get("whatHappensSource") or "none",
            "links": links, "dataIds": data_ids,
            "shard": q.get("shardFile"),
            "classesDiffering": bd.get("differing", []), "classesMissing": bd.get("missing", []),
        })
        shard = q.get("shardFile")
        if not shard or not os.path.isfile(os.path.join(fs.GAME_ROOT, shard)):
            problems.append("%s: shard file missing (%s)" % (q["id"], shard))

    by_sec = collections.OrderedDict((k, []) for k in HEADINGS)
    for r in rows:
        by_sec[section_of(r["q"])].append(r)
    for k, lst in by_sec.items():
        if k in ORDER:
            lst.sort(key=ORDER[k])

    # ---- generated prose that depends on measurements
    ctx = {"sq_test": sq_block is not None}
    gq = by_sec["5"]
    reg = {x.qid: x.name for x in V.registry(BASE)}
    multi = [r for r in gq if len(r["ids"]) > 1]
    none = [r for r in gq if not r["ids"]]
    parts = ["Quest ids come from each id's own tag-`0x35` journal records in `%s`\n"
             "(AUDIT Severity 3, re-measured by `quest_kb_index.py` on every run)." % BASE]
    for r in multi:
        parts.append("`%s` writes %s ids (journal records: %s), each its own\n"
                     "quest in the `Vectoren.bin` registry: %s."
                     % (r["id"], word(len(r["ids"])),
                        ", ".join("%d x%d" % (x, meas.gq[r["id"]][x]) for x in r["ids"]),
                        ", ".join("%d `%s`" % (x, reg.get(x, "?").strip()) for x in r["ids"])))
    if none:
        parts.append("No journal entry, so `-`: %s." % ", ".join("`%s`" % r["id"] for r in none))
        sect = [r for r in none if r["dataIds"]]
        for r in sect:
            parts.append("`%s`'s owned records nevertheless all sit in `Vectoren.bin` sections of\n"
                         "registry quest %s - its shard's SECTIONS block lists them."
                         % (r["id"], ", ".join("%d (`%s`)" % (x, reg.get(x, "?").strip()) for x in r["dataIds"])))
    ctx["gq_ids_text"] = "\n".join(parts)
    slots = by_sec["C"]
    in_reg = [r for r in slots if _num(r["id"]) in reg]
    if slots and in_reg:
        qis_n = sum(1 for r in slots if V.section(BASE, "QIS_Trigger%d" % _num(r["id"])) is not None)
        ex = in_reg[0]
        ctx["slot_registry"] = (
            "Each slot id is also a quest of its own: %d of the %d are entries of the\n"
            "`%s` `Vectoren.bin` quest registry (e.g. %d `%s`), and %d have a\n"
            "`QIS_Trigger<id>` section - the SECTIONS block of any slot shard lists them."
            % (len(in_reg), len(slots), BASE, _num(ex["id"]), reg[_num(ex["id"])].strip(), qis_n))

    # ---- Severity 9 examples must exist
    tokmap = {r["id"]: set(r["tokens"]) for r in rows}
    for qid, tok in (("WW_MPSTART4", "WW_MPStart4"), ("DQ_QUEST", "DQ_Quest"),
                     ("HQ_5.7", "tprauszHQ_5.7"), ("HQ_6.3B2", "HQ_6.3b2_T")):
        if not any(t == tok or t.startswith(tok) for t in tokmap.get(qid, ())):
            problems.append("AUDIT Sev 9 example %s not among the tokens of %s" % (tok, qid))
    return {"rows": rows, "by_sec": by_sec, "ctx": ctx, "sq_block": sq_block, "problems": problems,
            "notes": notes, "corrections": corrections, "qidx": qidx, "prev": prev, "meas": meas,
            "seconds": time.time() - t0}


def render_md(b):
    rows, by_sec, ctx = b["rows"], b["by_sec"], b["ctx"]
    cats = collections.Counter(r["category"] for r in rows)
    nplay = sum(cats[c] for c in PLAYABLE)
    multi = [r for r in by_sec["5"] if len(r["ids"]) > 1]
    L = []
    a = L.append
    a("# Quest INDEX - every id in the Sacred Gold script corpus")
    a("")
    a("**Entry point for the whole quest knowledge base.** %d indexed ids, of which" % len(rows))
    a("**%d are playable quests** (%s); the" % (nplay, " + ".join("%s %d" % (c, cats[c]) for c in PLAYABLE)))
    a("remaining %d are signpost labels, level objects and daily-quest generator" % (len(rows) - nplay))
    a("machinery, listed here so nothing in the corpus is unaccounted for.")
    a("")
    a("* Machine-readable twin of this file: **`index.json`** (same rows, plus tokens,")
    a("  per-source record counts and shard paths).")
    a("* How the base is laid out, and how to regenerate everything: **`README.md`**.")
    a("* Designer-level narrative (how a quest is built end to end): **`OVERVIEW.md`**.")
    a("* Engine mechanics, tag by tag: **`MECHANICS.md`**. What our SDK cannot do yet:")
    a("  **`SDK_GAPS.md`**. Per-class differences: **`CLASS_DIFFS.md`**.")
    a("")
    a("## How to read the columns")
    a("")
    a("| column | meaning |")
    a("|---|---|")
    a("| **id** | the canonical script prefix, exactly as `quest_index.json` classifies it. Shard: `shards/<campaign>/<id>.txt`. |")
    qid_extra = ""
    if multi:
        qid_extra = " Several ids are comma-separated (%s)." % ", ".join(
            "`%s`: %s" % (r["id"], ", ".join(str(x) for x in r["ids"])) for r in multi)
    a("| **quest id** | the *numeric* id the engine passes to tags `0x14`/`0x15`/`0x35`/`0x3f`/`0x40`. `-` = none, or per-class (see `HQ_base_1.md` §0.4).%s |" % qid_extra)
    a("| **classes** | `shared` = byte-identical owned records in all 8 base classes. `only X/Y` = the quest exists only in those classes. `varies: X/Y` = present everywhere but those classes' bytes differ. Measured by md5 over owned records (`CLASS_DIFFS.md` §2). |")
    a("| **title** | questbook title from `scripts/us/global.res`; `(no questbook title)` where the id has none. |")
    a("| **what happens** | one line. Where the quest has a journal *header* line, that line is quoted verbatim - it is the game's own statement of the objective. Everything else is condensed from the detail file named in the last column. `index.json.whatHappensSource` names the exact origin of every line. |")
    a("| **documented in** | the detail file (and heading) that carries the full 10-field entry. `-` = not documented yet; the shard is the only source. |")
    a("")
    a("Campaign, record counts and per-source presence are in `index.json`; the")
    a("campaign is also implicit in the section a quest sits in below.")
    a("")
    a("Ids are upper-cased by `quest_index.py`; the literal bytes may differ in case")
    a("(`WW_MPStart4`, `DQ_Quest`), and the dotted `HQ_x.y` ids are substrings of")
    a("longer tokens (`tprauszHQ_5.7`, `HQ_6.3b2_T`) - see the `tokens` field of")
    a("`index.json` for the exact strings (AUDIT Severity 9).")
    a("")
    a("## Contents")
    a("")
    for i, (k, (head, label, fmt)) in enumerate(HEADINGS.items(), 1):
        extra = ""
        if k == "5" and not any(r["links"] for r in by_sec["5"]):
            extra = ", **no detail file yet**"
        a("%d. [%s](#%s) - %s%s" % (i, label, gh_slug(head), fmt % len(by_sec[k]), extra))
    a("")
    a("---")
    for k, (head, label, fmt) in HEADINGS.items():
        a("")
        a("## " + head)
        a("")
        a(intro(k, by_sec[k], ctx))
        a("")
        a(TABLE_HEAD[COLS[k]])
        for r in by_sec[k]:
            a(row_line(r, COLS[k]))
        if k == "9" and b["sq_block"]:
            a("")
            a(b["sq_block"])
    a("")
    a("---")
    a("")
    a("*Generated by `sdk/re/py/quest_kb_index.py` from `quest_index.json`, the")
    a("curated fields of the previous `index.json` and the headings of the detail")
    a("files. Rebuild with `python sdk/re/py/quest_index.py --sources all`,")
    a("`python sdk/re/py/quest_shards.py`, then `python sdk/re/py/quest_kb_index.py`")
    a("(see `README.md`).*")
    return "\n".join(L) + "\n"


def render_json(b):
    rows = b["rows"]
    cats = collections.Counter(r["category"] for r in rows)
    out = collections.OrderedDict()
    out["generated"] = time.strftime("%Y-%m-%d")
    out["source"] = ("sdk/.claude/knowledge/quests/quest_index.json (%d entries, sdk/re/py/quest_index.py +"
                     " quest_shards.py), the curated fields of the previous index.json, and the headings of"
                     " the detail files; assembled by sdk/re/py/quest_kb_index.py" % len(rows))
    out["regenerate"] = ("python sdk/re/py/quest_index.py --sources all ; python sdk/re/py/quest_shards.py ;"
                         " python sdk/re/py/quest_kb_index.py")
    out["fields"] = collections.OrderedDict([
        ("whatHappens", "one-line summary; whatHappensSource names the global.res key it was taken from, the"
                        " detail file it was condensed from, or \"curated from the detail file\""),
        ("numericQuestId", "the id the engine uses in tags 0x14/0x15/0x35/0x3f/0x40; null when unknown or"
                           " per-class (see HQ_base_1 SS0.4). The first element of numericQuestIds."),
        ("numericQuestIds", "every such id (curated, plus AUDIT.md Severity 3); GQ_RG1 has three"),
        ("detailFiles", "markdown file + heading anchor documenting this quest (anchors validated against the"
                        " file's headings on every run)"),
        ("classesDiffering", "base classes whose owned-record payload md5 differs from the baseline"
                             " (quest_index.json baseClassDiff)"),
        ("classesMissing", "base classes that do not have the quest at all"),
        ("dataQuestIds", "quest ids derived from the data by quest_shards.py (owned tag-0x35 journal records"
                         " intersected with the Vectoren.bin sections holding the owned records; see the QUEST"
                         " IDS rule in quest_shards.py). Evidence, not curated."),
        ("tiling", "operand-line tiling of every record the shard prints (quest_shards.py): records,"
                   " operandLines, failed"),
    ])
    out["counts"] = collections.OrderedDict(sorted(cats.items()))
    out["corrections"] = b["corrections"]
    qs = []
    for r in rows:
        q = r["q"]
        e = collections.OrderedDict()
        e["id"] = r["id"]
        e["category"] = r["category"]
        e["campaign"] = r["campaign"]
        e["classSpecific"] = q["classSpecific"]
        e["classesDiffering"] = r["classesDiffering"]
        e["classesMissing"] = r["classesMissing"]
        e["title"] = r["title"]
        e["numericQuestId"] = r["ids"][0] if r["ids"] else None
        e["numericQuestIds"] = r["ids"]
        e["whatHappens"] = r["what"]
        e["whatHappensSource"] = r["whatSource"]
        e["detailFiles"] = r["links"]
        e["shardFile"] = r["shard"]
        e["records"] = r["records"]
        e["sources"] = q["sources"]
        e["tokens"] = q["tokens"]
        e["dataQuestIds"] = r["dataIds"]
        e["tiling"] = q.get("tiling")
        qs.append(e)
    out["quests"] = qs
    return json.dumps(out, indent=1, ensure_ascii=False) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description="generate INDEX.md + index.json of the quest knowledge base")
    ap.add_argument("--quest-index", default=QUEST_INDEX_JSON)
    ap.add_argument("--curated", default=INDEX_JSON, help="previous index.json (curated fields)")
    ap.add_argument("--out-dir", default=KB)
    ap.add_argument("--check", action="store_true", help="verify only, write nothing")
    ap.add_argument("--diff", action="store_true", help="print a unified diff of INDEX.md")
    ap.add_argument("--allow-id-change", action="store_true")
    ap.add_argument("--import-index-md", help="one-time: adopt disagreeing cells of this INDEX.md")
    args = ap.parse_args(argv)

    b = build(args)
    md = render_md(b)
    js = render_json(b)
    old_md_path = os.path.join(args.out_dir, "INDEX.md")
    old = open(old_md_path, encoding="utf-8").read() if os.path.isfile(old_md_path) else ""
    diff = list(difflib.unified_diff(old.splitlines(), md.splitlines(), "INDEX.md (before)",
                                     "INDEX.md (generated)", lineterm="", n=0))
    plus = sum(1 for l in diff if l.startswith("+") and not l.startswith("+++"))
    minus = sum(1 for l in diff if l.startswith("-") and not l.startswith("---"))

    rows = b["rows"]
    print("quest_kb_index: %d ids (%s) in %.1fs" % (
        len(rows), ", ".join("%s=%d" % kv for kv in sorted(collections.Counter(r["category"] for r in rows).items())),
        b["seconds"]))
    nlinks = sum(len(r["links"]) for r in rows)
    print("detail links: %d on %d rows" % (nlinks, sum(1 for r in rows if r["links"])))
    for c in b["corrections"]:
        print("correction: %-14s %-16s %-22s %s -> %s" % (c["id"], c["field"], c["source"],
                                                         str(c.get("previous"))[:50], str(c["value"])[:60]))
    mism = [(r["id"], r["ids"], r["dataIds"]) for r in rows
            if r["category"] in PLAYABLE and sorted(r["ids"]) != sorted(r["dataIds"])]
    print("curated vs data-derived quest ids differ on %d playable row(s)%s" % (
        len(mism), (": " + "; ".join("%s %s/%s" % m for m in mism)) if mism else ""))
    for n in b["notes"]:
        print("note: " + n)
    for p in b["problems"]:
        print("FAIL: " + p)
    print("INDEX.md: %d line(s) added, %d removed vs the file on disk" % (plus, minus))
    if args.diff:
        print("\n".join(diff))
    if args.check:
        return 1 if b["problems"] else 0
    if b["problems"]:
        print("not writing: fix the FAIL lines first (or pass --allow-id-change for an intended id change)")
        return 1
    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, "INDEX.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(md)
    with open(os.path.join(args.out_dir, "index.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(js)
    print("wrote %s and %s" % (os.path.join(args.out_dir, "INDEX.md"), os.path.join(args.out_dir, "index.json")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
