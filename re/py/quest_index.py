"""CANONICAL QUEST INDEX — derives the quest list from the FunkCode data itself.

What counts as a quest here: a *canonical prefix* that owns a set of
FunkCode.bin records.  Every symbolic name the scripts use is a token of the
form <PREFIX>_<ROLE>, e.g.

    NQ_5001_LOG_TITLE          NQ_5001_PRENPC_QOFFEN
    HQ_3_1_4_Log_Title         HQ_3_1_4_glad_NPC_Auftrag_Qstart
    DQ_15024_LOG_TITEL         DQ_15024_START
    GQ_RG22_AUFTRAG_LOG_HEADER

so the quest id is the canonical prefix (NQ_5001, HQ_3_1_4, DQ_15024,
GQ_RG22) and its record set is every record whose payload contains at least
one token that canonicalises to that prefix.  Ownership is computed
token-wise, never by raw substring, so `HQ_5` does not swallow `HQ_5_0_1`
and `DQ_1502` does not swallow `DQ_15024`.

Families found in the data (see `--families` for the raw evidence):

  HQ_<n>[_<n>...]  main-quest chain.  TWO distinct numbering schemes coexist:
                   3-part ids (HQ_3_1_4 "For King and Country" ... HQ_8_1_1
                   "Shaddar") live in the base per-class scripts = base Sacred
                   campaign; 2-part ids (HQ_0_1, HQ_1_1 "The Alteration of the
                   World" ... HQ_5_6 "Anducar?!?") live in the Addon scripts
                   and in bin/NetScript = Underworld campaign.
  NQ_<n> / NQ_UW<n>  named side quests (NQ_5001, NQ_UW9521 ...).  The
                   lowercase `nq_<n>_dlg_*` tokens fold into the same id
                   because Sacred's name hash is case-insensitive
                   (sacred_hash applies toupper).
  RB_<n>           runebook / region-clearing quests (RB_5081..RB_5089).
  DQ_<n>           AUTHORED daily/village quests — they own a questbook title
                   key (DQ_15024_LOG_TITEL = "Lonely Tears").  Their dialog
                   text also lives under DQ_START_<n> / DQ_OFFEN_<n> /
                   DQ_SIEG_<n>, which this classifier folds into DQ_<n>.
  DQSLOT           DQ_<n> ids whose ONLY tokens are `DQ_<n>` / `DQ_<n>_RICHTUNG`
                   (2600-3xxx): generator SLOT VARIABLES, not quests —
                   templates compose `res:DQ1_BRINGE_NPC_ZIEL+VAR(DQ_2600)`
                   from them at runtime.
  DQ_RG<n>_*, DQ<n>_*  daily-quest *templates* per region n=1..23
                   (BRINGE_NPC / TOETE_NPC / ESCORT / GEISEL / DUNGEONQUEST).
                   Category DQTPL — machinery, not a single playable quest.
                   Kept as two separate ids per region (DQ_RG10 and DQ10);
                   merging them would be an interpretation, not a measurement.
  GQ_<x>           "Auftrag" board quests: GQ_9010, GQ_RG1, GQ_RGHS,
                   GQ_KOPFGELD (bounty), GQ_KHORADNUR.
  SQ_<x>           class-special exchanges (SQ_SERAPHIM, SQ_VAMP, SQ_HELF,
                   SQ_DELF).
  WW_<x>           NOT quests: world-map / teleporter labels ("Arena",
                   "Cloister", "To the Volcano Plains").  Indexed because
                   the downstream schema asks for the category; every WW
                   entry is marked category "WW" so it can be filtered out.
  OTHER            quest-family tokens that are not journal quests: the dotted
                   level-object names (HQ_5.6.2 door triggers, HQ_6.2A
                   teleport zones — see DOTTED_RE) plus HQ_ENDE, HQ_2PRY,
                   HQ_MINE, HQ_EXTRAVILYA01, HQ_SOUND, HQ_12A, HQ_FINALE.
  VAR              daily-quest GENERATOR namespaces (DQ_Quest, DQ_NUM,
                   DQ_Zeit, DQ_<NPCTYPE>_BEGRUESSUNG, nq_ablehnen,
                   NQ_LOG_Qend, hq_starten ...) — see VAR_IDS.

Names that WRAP a quest id (btn_accept_dq_15024, btn_ok_dq_15024,
SOUND_FX_HQ_3_1_2_*, tpreinzHQ_6.2a) are stripped and re-classified, so the
quest's dialog buttons, voice lines and teleport zones count as its records.

NOT indexed (not quest-owned name spaces; listed by `--families`): GDQ_<n>
(daily-quest pool variables), POOL_*, LOC_*, NON_*, TYPE_*, gECS_*, SFX_*,
DLG_*, TT_* (tooltips), CITY_*, RG<n>.

Output (2026-09-10 run over all 20 sources): 741 entries —
HQ 76, NQ 51, RB 9, DQ 95, DQSLOT 352, DQTPL 44, GQ 12, SQ 4, WW 34,
OTHER 39, VAR 25; 23 of them class-specific.

Usage
-----
    python quest_index.py                       # build + write JSON and MD
    python quest_index.py --sources canonical   # cheap corpus (dedup'd)
    python quest_index.py --families            # family/token evidence dump
    python quest_index.py --quest NQ_5001       # one quest, verbose
"""
import os, re, sys, json, time, bisect, hashlib, argparse, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import funkcode_sources as fs
from funkcode_disasm import walk_records
from funkcode_tags import label_for as tag_label

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

OUT_DIR = os.path.join(fs.GAME_ROOT, "sdk", ".claude", "knowledge", "quests")
JSON_OUT = os.path.join(OUT_DIR, "quest_index.json")
MD_OUT = os.path.join(OUT_DIR, "quest_index.md")

# Any C identifier is a candidate token; classify() decides what is a quest.
# '.' is included because level-object names use a DOTTED sub-numbering that
# references a main-quest step: 'HQ_5.6.2_Tur1' (a door trigger, tag 0x02
# TriggerSetState / tag 0x08 CreateOBJ) and 'tpreinzHQ_6.2a' / 'tprauszHQ_6.2a'
# (teleport-in / teleport-out zones).  Without the dot those tokens truncate
# to a bare 'HQ_5' and invent a phantom quest.
TOKEN_RE = re.compile(rb"[A-Za-z_][A-Za-z0-9_.]{2,79}")

# A dotted level-object name.  How 'HQ_5.6.2' / 'HQ_6.3b2' / 'HQ_7.2c' map
# onto the quest ids (HQ_5_6_1, HQ_6_3_1, HQ_7_2_1 ...) is UNKNOWN — the
# dotted scheme carries letter suffixes the quest ids do not have — so these
# are indexed under their own literal id and categorised OTHER rather than
# folded into a quest by guesswork.
DOTTED_RE = re.compile(r"^(HQ|NQ|DQ|RB|GQ|SQ)_([0-9][0-9A-Z]*(?:\.[0-9A-Z]+)+)")

CATEGORY_ORDER = ["HQ", "NQ", "RB", "DQ", "DQSLOT", "DQTPL", "GQ", "SQ", "WW",
                  "OTHER", "VAR"]

# A DQ_<n> id whose ONLY tokens are `DQ_<n>` and `DQ_<n>_RICHTUNG` is not an
# authored quest: it is a daily-quest generator SLOT VARIABLE.  Evidence
# (base:VAMPIRELADY record 24366, tag 0x03 DialogShow_v1):
#     'res:DQ1_BRINGE_NPC_ZIEL+VAR(DQ_2600)'
# i.e. the region-1 template composes its resource name from the slot
# variable's value at runtime; record 24383 (tag 0x43 VarDecl_C) declares
# 'DQ_2600' with initialiser 'DQ_Quest'.  Authored daily quests instead carry
# their own questbook keys (DQ_15024_LOG_TITEL = "Lonely Tears").
DQ_SLOT_TOKEN_RE = re.compile(r"^DQ_\d+(_RICHTUNG)?$", re.I)

# Per-instance daily-quest TEXT keys: DQ_START_10250 / DQ_OFFEN_10250 /
# DQ_SIEG_10256_A ... belong to daily-quest instance DQ_<n>.  Verified through
# global.res: DQ_START_10250 = "Bring me four vials filled with insect blood…"
# and DQ_10250_LOG_TITEL = "Insect Blood for the Healer" — same quest.
DQ_TEXTKEY_RE = re.compile(
    r"^DQ_(?:NC_START|START|OFFEN2|OFFEN|SIEG|LOSE|FAIL|FRAU|ZIEL)_(\d+)")

# Namespaces of the daily-quest GENERATOR (shared variables and shared text
# pools), not quests.  Enumerated from the data — every one of these has a
# non-numeric root and hundreds of owning records spread over the whole file:
#   DQ_Quest / DQ_NUM / DQ_Zeit / DQ_AktivRgn / DQ_TypNPC / DQ_Suchrichtung …
#   DQ_<NPCTYPE>_BEGRUESSUNG|GEWONNEN|VERLOREN (ADELIGER/BAUER/BUERGER/SOLDAT)
#   DQ_ABGEBROCHEN_LOG "Quest abandoned." / DQ_GEWONNEN_LOG / DQ_VERLOREN_LOG
#   nq_ablehnen, NQ_LOG_Qend, HQ_Log_Qend  = shared refuse/end log lines
#   hq_starten / hq_uw / hq_dlg_anzeigen   = engine flags
VAR_IDS = {
    "DQ_ABGEBROCHEN", "DQ_ADELIGER", "DQ_AKTIVRGN", "DQ_BAUER", "DQ_BELOHNUNG",
    "DQ_BRINGE", "DQ_BUERGER", "DQ_ESCORT", "DQ_GEWONNEN", "DQ_NUM", "DQ_OK",
    "DQ_QUEST", "DQ_SOLDAT", "DQ_SUCHRICHTUNG", "DQ_TOETE", "DQ_TYPNPC",
    "DQ_VERLOREN", "DQ_ZEIT", "DQ_ZEITFENSTER",
    "HQ_DLG", "HQ_LOG", "HQ_STARTEN", "HQ_UW",
    "NQ_ABLEHNEN", "NQ_LOG",
}

# An OTHER-bucket id is promoted back to its family when one of its tokens is
# a questbook TITLE key — that is the engine's own marker for "this is a
# journal quest".  (HQ_MAGE_NEU_Log_Title = "Swords and Magic",
# HQ_SERA_NEU_LOG_QTitle = "Call to arms",
# NQ_CA_ZWERGENAUFSTAND_LOG_TITLE = "Dwarven Uprising on the Cemetery".)
TITLE_TOKEN_RE = re.compile(r"_(LOG_)?Q?TIT(LE|EL)\d*$")


# Wrapper names: a quest id embedded in a UI/sound resource name.  These ARE
# quest-owned records — the dialog buttons and the spoken line of that quest —
# so the wrapper is stripped and the remainder re-classified.  Evidence:
#   btn_accept_dq_15024 / btn_ok_dq_15024   sit in DQ_15024's own record run
#                                           (base:VAMPIRELADY records 10985-10988)
#   SOUND_FX_HQ_3_1_2_DEM_NPC_AUFTRAG_QSTART  is the voice line of the
#                                           HQ_3_1_2 Daemon dialog record
# NOT stripped (left unclassified on purpose, ~10 tokens, ambiguous pool
# names): R1_HQ_S_2_T, R12_HQ_7, R3_HQ_4, r_HQ_5, POOL1_HQ_, R23NQ03_NPC_NQ_*.
WRAPPER_RE = re.compile(
    r"^(SOUND_FX_|SOUND_|BTN_OK_|BTN_ACCEPT_|BTN_DECLINE_|BTN_"
    r"|TPREINZ|TPRAUSZ)(?=[A-Z]{2}_)")


# ------------------------------------------------------------ classifier --
def classify(token):
    """token -> (category, canonical_quest_id) or None.

    Deterministic and case-insensitive on the id (the engine's name hash is
    case-insensitive), so `nq_7601_dlg_offen` and `NQ_7601_LOG_TITLE` land on
    the same quest NQ_7601.
    """
    u = token.upper()
    w = WRAPPER_RE.match(u)
    if w:
        return classify(u[w.end():])
    d = DOTTED_RE.match(u)
    if d:
        return "OTHER", "%s_%s" % (d.group(1), d.group(2))
    if "." in u:                      # any other dotted name is not a quest
        return None
    parts = u.split("_")
    fam = parts[0]

    if fam == "HQ":
        if len(parts) >= 2 and parts[1].isdigit():
            nums = []
            for p in parts[1:]:
                if p.isdigit():
                    nums.append(p)
                else:
                    break
            return "HQ", "HQ_" + "_".join(nums)
        if len(parts) >= 2:
            qid = "HQ_" + parts[1]
            cat = "VAR" if qid in VAR_IDS else "OTHER"
            if cat == "OTHER" and TITLE_TOKEN_RE.search(u):
                cat = "HQ"
            return cat, qid
        return None

    if fam == "NQ":
        if len(parts) >= 2:
            m = re.match(r"^(UW)?(\d+)$", parts[1])
            if m:
                return "NQ", "NQ_" + (m.group(1) or "") + m.group(2)
            qid = "NQ_" + parts[1]
            cat = "VAR" if qid in VAR_IDS else "OTHER"
            if cat == "OTHER" and TITLE_TOKEN_RE.search(u):
                cat = "NQ"
            return cat, qid
        return None

    if fam == "RB":
        if len(parts) >= 2 and parts[1].isdigit():
            return "RB", "RB_" + parts[1]
        return None

    if fam == "GQ":
        if len(parts) >= 2:
            return "GQ", "GQ_" + parts[1]
        return None

    if fam == "SQ":
        if len(parts) >= 2:
            return "SQ", "SQ_" + parts[1]
        return None

    if fam == "WW":
        if len(parts) >= 2:
            return "WW", u
        return None

    if fam == "DQ":
        # DQ_<n>...        -> daily-quest instance
        # DQ_START_<n> ... -> per-instance text key of daily-quest <n>
        # DQ_RG<n>_...     -> region template set
        # anything else    -> generator namespace (VAR) / OTHER
        if len(parts) >= 2:
            if parts[1].isdigit():
                return "DQ", "DQ_" + parts[1]
            m = DQ_TEXTKEY_RE.match(u)
            if m:
                return "DQ", "DQ_" + m.group(1)
            m = re.match(r"^RG(\d+)$", parts[1])
            if m:
                return "DQTPL", "DQ_RG" + m.group(1)
            qid = "DQ_" + parts[1]
            return ("VAR" if qid in VAR_IDS else "OTHER"), qid
        return None

    # DQ<n>_...  (no separator) -> region template set, n = region id 1..23
    m = re.match(r"^DQ(\d+)_", u)
    if m:
        return "DQTPL", "DQ" + m.group(1)

    return None


def _cat_rank(cat):
    """A quest id may be reached from several tokens with different verdicts
    (HQ_MAGE_NEU_Log_Title says 'HQ', HQ_MAGE_SERGE_PQUEST1 says 'OTHER').
    The strongest verdict wins, deterministically."""
    if cat is None:
        return 9
    if cat == "VAR":
        return 2
    if cat == "OTHER":
        return 1
    return 0


def better_cat(a, b):
    return a if _cat_rank(a) <= _cat_rank(b) else b


# ------------------------------------------------------------- index core --
class SourceScan(object):
    """One FunkCode.bin fully scanned: records + per-quest ownership."""

    def __init__(self, key, data):
        self.key = key
        self.size = len(data)
        self.records = list(walk_records(data))        # (off, tag, size, payload)
        self.starts = [r[0] for r in self.records]
        self.covered = (self.records[-1][0] + self.records[-1][2]) if self.records else 0
        self.owners = collections.defaultdict(set)     # qid -> {record index}
        self.tokens = collections.defaultdict(set)     # qid -> {token}
        self.cats = {}                                 # qid -> category
        self._scan(data)

    def _scan(self, data):
        starts, records = self.starts, self.records
        for m in TOKEN_RE.finditer(data):
            hit = classify(m.group().decode("latin1"))
            if hit is None:
                continue
            cat, qid = hit
            i = bisect.bisect_right(starts, m.start()) - 1
            if i < 0:
                continue
            off, tag, size, payload = records[i]
            if m.start() < off + 3 or m.start() >= off + size:
                continue                                # header byte / gap
            self.owners[qid].add(i)
            self.tokens[qid].add(m.group().decode("latin1"))
            self.cats[qid] = better_cat(self.cats.get(qid), cat)

    def record_indices(self, qid):
        return sorted(self.owners.get(qid, ()))

    def payload_hash(self, qid):
        """md5 over tag byte + payload of every owned record, in file order."""
        idxs = self.record_indices(qid)
        if not idxs:
            return None
        h = hashlib.md5()
        for i in idxs:
            off, tag, size, payload = self.records[i]
            h.update(bytes([tag]))
            h.update(payload)
        return h.hexdigest()


def scan_sources(sources):
    """Scan each source once; sources with identical file content share the
    scan object (all 8 addon class dirs are the same bytes)."""
    by_md5, scans = {}, collections.OrderedDict()
    for s in sources:
        m = fs.file_md5(s)
        if m in by_md5:
            scans[s.key] = by_md5[m]
            continue
        sc = SourceScan(s.key, s.read())
        by_md5[m] = sc
        scans[s.key] = sc
    return scans


# ------------------------------------------------------------ title lookup --
_TITLE_SUFFIXES = ["_LOG_TITLE", "_LOG_TITEL", "_LOG_QTITLE", "_LOG_QTITEL",
                   "_TITLE", "_TITEL", "_LOG_HEADER", "_LOG_QHEADER"]
_qd = None


def _text_for(name):
    global _qd
    if _qd is None:
        import quest_dump
        _qd = quest_dump
    try:
        return _qd.text_for_name(name)
    except Exception:
        return None


def title_for(qid, tokens):
    """Resolve a display title through global.res: prefer a token that
    literally exists in the scripts, else try the standard suffixes."""
    up = {t.upper(): t for t in tokens}
    for sfx in _TITLE_SUFFIXES:
        cand = up.get(qid + sfx)
        if cand:
            t = _text_for(cand)
            if t and t.strip():
                return t.strip().replace("\r", " ").replace("\n", " | ")
    for sfx in _TITLE_SUFFIXES:
        t = _text_for(qid + sfx)
        if t and t.strip():
            return t.strip().replace("\r", " ").replace("\n", " | ")
    return None


# ------------------------------------------------------------- index build --
def campaign_of(present_keys):
    base_sp = any(k.startswith("base:") and fs.SOURCES[k].kind == "class" for k in present_keys)
    addon_sp = any(k.startswith("addon:") and fs.SOURCES[k].kind == "class" for k in present_keys)
    net = any(fs.SOURCES[k].kind == "net" for k in present_keys)
    if base_sp and addon_sp:
        return "both"
    if base_sp:
        return "base"
    if addon_sp:
        return "addon"
    if net:
        return "net"
    return "unknown"


BASELINE_ORDER = ([fs.BASELINE] + ["base:" + c for c in fs.CLASS_NAMES]
                  + [fs.ADDON_BASELINE] + ["addon:" + c for c in fs.CLASS_NAMES]
                  + ["base:NetScript", "addon:NetScript",
                     "base:NetScriptCamp", "addon:NetScriptCamp"])


def build_index(sources, with_titles=True):
    scans = scan_sources(sources)
    keys = list(scans)
    all_qids = collections.OrderedDict()
    for k in keys:
        for qid, cat in scans[k].cats.items():
            all_qids[qid] = better_cat(all_qids.get(qid), cat)

    base_class_keys = [k for k in keys
                       if k.startswith("base:") and fs.SOURCES[k].kind == "class"]
    addon_class_keys = [k for k in keys
                        if k.startswith("addon:") and fs.SOURCES[k].kind == "class"]

    quests = []
    for qid, cat in all_qids.items():
        present = [k for k in keys if scans[k].owners.get(qid)]
        rec_counts = {k: len(scans[k].owners[qid]) for k in present}
        hashes = {k: scans[k].payload_hash(qid) for k in present}
        tokens = sorted(set().union(*[scans[k].tokens[qid] for k in present]))

        baseline = next((k for k in BASELINE_ORDER if k in rec_counts), present[0])

        if cat == "DQ" and all(DQ_SLOT_TOKEN_RE.match(t) for t in tokens):
            cat = "DQSLOT"

        # class-specificity: compare the owned-record payload hash across the
        # 8 per-class script sets of each campaign
        def diff_report(class_keys):
            have = {k: hashes.get(k) for k in class_keys}
            seen = [v for v in have.values() if v]
            if not seen:
                return None
            ref_key = baseline if baseline in class_keys else class_keys[0]
            ref = have.get(ref_key) or seen[0]
            differing = sorted(fs.SOURCES[k].cls for k, v in have.items() if v and v != ref)
            missing = sorted(fs.SOURCES[k].cls for k, v in have.items() if not v)
            return {"reference": ref_key, "differing": differing, "missing": missing,
                    "distinct": len(set(seen))}

        base_diff = diff_report(base_class_keys) if base_class_keys else None
        addon_diff = diff_report(addon_class_keys) if addon_class_keys else None

        class_specific = bool(
            (base_diff and (base_diff["differing"] or (base_diff["missing"] and base_diff["distinct"] >= 1 and len(base_diff["missing"]) < len(base_class_keys)))) or
            (addon_diff and (addon_diff["differing"] or (addon_diff["missing"] and len(addon_diff["missing"]) < len(addon_class_keys))))
        )

        q = {
            "id": qid,
            "category": cat,
            "campaign": campaign_of(present),
            "baselineSource": baseline,
            "records": rec_counts.get(baseline, 0),
            "recordsBySource": rec_counts,
            "classSpecific": class_specific,
            "baseClassDiff": base_diff,
            "addonClassDiff": addon_diff,
            "sources": present,
            "tokens": tokens,
            "payloadMd5": hashes,
        }
        if with_titles:
            q["title"] = title_for(qid, tokens)
        quests.append(q)

    def sort_key(q):
        cat = q["category"]
        ci = CATEGORY_ORDER.index(cat) if cat in CATEGORY_ORDER else 99
        # numeric-aware id sort
        nums = tuple(int(x) for x in re.findall(r"\d+", q["id"]))
        # ids with no number at all (HQ_MAGE, GQ_KOPFGELD) sort after numbered ones
        return (ci, q["id"].split("_")[0], 0 if nums else 1, nums, q["id"])
    quests.sort(key=sort_key)

    meta = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "gameRoot": fs.GAME_ROOT,
        "tool": "sdk/re/py/quest_index.py",
        "sources": [{
            "key": s.key, "campaign": s.campaign, "kind": s.kind,
            "path": os.path.relpath(s.path, fs.GAME_ROOT),
            "md5": fs.file_md5(s), "size": scans[s.key].size,
            "records": len(scans[s.key].records),
            "coveredBytes": scans[s.key].covered,
        } for s in sources],
    }
    return meta, quests, scans


# ------------------------------------------------------------------ output --
def write_json(meta, quests, path=JSON_OUT):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({"meta": meta, "quests": quests}, fh, indent=1, ensure_ascii=False)
    return path


def write_md(meta, quests, path=MD_OUT):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    by_cat = collections.OrderedDict()
    for q in quests:
        by_cat.setdefault(q["category"], []).append(q)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("# Sacred Gold — canonical quest index\n\n")
        fh.write("Generated %s by `sdk/re/py/quest_index.py` from the FunkCode\n"
                 "record data (NOT from the old base-campaign-only book).\n\n" % meta["generated"])
        fh.write("## Sources scanned\n\n")
        fh.write("| source | campaign | kind | FunkCode.bin md5 | bytes | records |\n")
        fh.write("|---|---|---|---|---:|---:|\n")
        for s in meta["sources"]:
            fh.write("| `%s` | %s | %s | `%s` | %d | %d |\n" %
                     (s["key"], s["campaign"], s["kind"], s["md5"], s["size"], s["records"]))
        fh.write("\n## Totals\n\n| category | quests |\n|---|---:|\n")
        for c, qs in by_cat.items():
            fh.write("| %s | %d |\n" % (c, len(qs)))
        fh.write("| **total** | **%d** |\n" % len(quests))
        for c, qs in by_cat.items():
            fh.write("\n## %s (%d)\n\n" % (c, len(qs)))
            fh.write("| id | campaign | records (baseline) | baseline src | class-specific | title | shard |\n")
            fh.write("|---|---|---:|---|---|---|---|\n")
            for q in qs:
                cs = "no"
                if q["classSpecific"]:
                    d = q.get("baseClassDiff") or {}
                    bits = []
                    if d.get("differing"):
                        bits.append("differs: " + ",".join(d["differing"]))
                    if d.get("missing"):
                        bits.append("absent: " + ",".join(d["missing"]))
                    cs = "**yes** (%s)" % "; ".join(bits) if bits else "**yes**"
                title = (q.get("title") or "")[:70].replace("|", "\\|")
                shard = q.get("shardFile") or ""
                if shard:
                    shard = "[txt](%s)" % os.path.relpath(
                        os.path.join(fs.GAME_ROOT, shard),
                        os.path.dirname(os.path.abspath(path))).replace("\\", "/")
                fh.write("| `%s` | %s | %d | `%s` | %s | %s | %s |\n" %
                         (q["id"], q["campaign"], q["records"], q["baselineSource"],
                          cs, title, shard))
    return path


# ------------------------------------------------------------ evidence dump --
def families(sources, top=40):
    """Raw evidence: what name families exist and how many distinct tokens."""
    fam = collections.defaultdict(lambda: collections.defaultdict(set))
    for s in sources:
        data = s.read()
        for m in TOKEN_RE.finditer(data):
            t = m.group().decode("latin1")
            head = t.split("_")[0]
            if head == t:
                head = re.sub(r"\d+$", "#", t)
            fam[head.upper()][s.key].add(t)
    rows = sorted(fam.items(), key=lambda kv: -sum(len(v) for v in kv[1].values()))
    print("%-16s %8s  %s" % ("family", "distinct", "sources"))
    for head, per in rows[:top]:
        tot = len(set().union(*per.values()))
        print("%-16s %8d  %s" % (head, tot, ", ".join(sorted(per))))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default="all",
                    help="source spec (default: all; see funkcode_sources.resolve)")
    ap.add_argument("--json", default=JSON_OUT)
    ap.add_argument("--md", default=MD_OUT)
    ap.add_argument("--no-titles", action="store_true",
                    help="skip global.res title resolution")
    ap.add_argument("--families", action="store_true",
                    help="dump the raw name-family evidence and exit")
    ap.add_argument("--quest", help="print one quest's index entry and exit")
    args = ap.parse_args()

    sources = fs.resolve(args.sources, default="all")
    if args.families:
        families(sources)
        return

    t0 = time.time()
    meta, quests, _ = build_index(sources, with_titles=not args.no_titles)
    if args.quest:
        for q in quests:
            if q["id"].upper() == args.quest.upper():
                print(json.dumps(q, indent=2, ensure_ascii=False))
                return
        print("no such quest: %s" % args.quest)
        return
    # keep shard paths written by quest_shards.py so running the index alone
    # never silently drops them from the JSON/MD
    if os.path.isfile(args.json):
        try:
            with open(args.json, encoding="utf-8") as fh:
                prev = {q["id"]: q.get("shardFile") for q in json.load(fh)["quests"]}
            for q in quests:
                if prev.get(q["id"]):
                    q.setdefault("shardFile", prev[q["id"]])
        except Exception:
            pass
    write_json(meta, quests, args.json)
    write_md(meta, quests, args.md)
    cats = collections.Counter(q["category"] for q in quests)
    print("scanned %d sources in %.1fs" % (len(sources), time.time() - t0))
    print("quests: %d  (%s)" % (len(quests),
                                ", ".join("%s=%d" % (c, cats[c]) for c in CATEGORY_ORDER if cats[c])))
    print("wrote %s" % args.json)
    print("wrote %s" % args.md)


if __name__ == "__main__":
    main()
