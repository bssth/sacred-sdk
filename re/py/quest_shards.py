"""Write one small, self-contained file per quest so later passes never have
to open a 4 MB FunkCode.bin again.

    sdk/.claude/knowledge/quests/shards/<campaign>/<id>.txt

WHAT A SHARD HOLDS -- everything for the quest's BASELINE source
(base:VAMPIRELADY for base-campaign quests, addon:SERAPHIM for addon-only
ones, base:NetScript for net-only ones; shards/README.md has the format):

  header      category, campaign, owned records per source, tokens, the
              TILING verdict of every record the shard prints ("# tiling:
              clean" or "# WARNING: N operand lines failed to tile"), the
              quest ids, a section/position summary, resolved global.res text
  BASELINE    the owned records in clusters, with neighbouring context
  SECTIONS    the Vectoren.bin sections that belong to the quest, each with
              {start,len} and its records in section order (vectoren.py)
  POSITIONS & DLGNPCS
              every named position and dialog NPC the printed records
              mention, resolved through StartCode.bin plus the FunkCode DefPos
              records -- the engine's own pre-scan (startcode.py)
  CLASS DIFF  owned records of the classes whose payload differs

Record bodies come from quest_script.dump_record() (funkcode_disasm grammar,
tag= passed), so shards and the big book stay identical in style.

QUEST IDS (drive the SECTIONS block) -- derived from the data, never curated
  journal   first operand (0b u32) of every OWNED tag-0x35 QuestBook record
            (the AUDIT.md Severity 3 method)
  sections  +0x48 of the Vectoren.bin section holding each owned record
            (sections tile FunkCode.bin exactly: vectoren.py --selftest)
  Playable categories (HQ NQ RB DQ GQ SQ): the ids in journal AND sections;
  with no journal record and every owned record inside sections of ONE id,
  that id (GQ_RG23 -> 9011, NQ_7613 -> 7613).  A DQ_/NQ_/RB_<n> id also gets
  n when n is a quest in that source's registry (the DQSLOT slots:
  base:VAMPIRELADY's registry holds qid 2600 'Bringe NPC zu Bauer' ... 3056).
  OTHER / VAR / WW / DQTPL get no quest-id expansion.

SECTION MEMBERSHIP (the reasons printed per section)
  id    section +0x48 is one of the quest ids
  name  the section name is a quest-id family carrying one of those ids:
          QIS_{Trigger,OnEnter,OnSetUp,OnExit,OnLose}<id>
          SelfTriggerQuest[Pool]<id>   Dialog:DLG_<id>_*   OMO<id><trigger>
          btn_*_<id>[_*]  od_<id>[_*]  eq_<id>   (number '_'-delimited)
          belohnung[_]<id>  belohne_*_<id>
        or it contains a token quest_index.classify() maps to this quest
        (btn_accept_dq_15054, btn_ok_hq_12a, dq_belohnung)
  rec   the section holds owned records of this quest
  Measured in base:VAMPIRELADY before choosing these: the number inside
  btn_* names equals the section's +0x48 in 217 of 222, Dialog:DLG_* 320 of
  330, od_* 67 of 73, setvar_<n> 0 of 438.  So NOT matched by number:
  setvar_<n>, fillchest_*, ToDo:/Belohnung:/Strafe: pool sections, and the
  Sector*/Region* hooks (their names are sprintf'd from region/sector
  numbers, vectoren.py: a digit match with a quest id is coincidence).
  Bodies: every member section for playable categories; for the machinery
  categories only id/name members (rec-only sections are listed, not dumped).

    python quest_shards.py                     # all quests, all sources
    python quest_shards.py --quest HQ_3_1_4    # just one (prints path)
    python quest_shards.py --categories HQ,NQ  # subset
    python quest_shards.py --no-sections --no-positions   # the old shape
"""
import os, sys, io, re, time, bisect, argparse, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import funkcode_sources as fs
import funkcode_disasm as fd
import funkcode_tags as ft
import quest_index as qi
import quest_script as qs
import vectoren as V
import startcode as S
from funkcode_tags import label_for as tag_label

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

SHARD_ROOT = os.path.join(qi.OUT_DIR, "shards")

# how many records may sit between two owned records before the run is split
DEFAULT_GAP = 24
# hard cap on records written per shard section (context included)
DEFAULT_MAX_RECORDS = 500
# cap on records dumped inside the SECTIONS block of one shard
DEFAULT_SECTION_RECORDS = 600
# cap on lines in the POSITIONS & DLGNPCS / by-name reference tables
DEFAULT_MAX_REFS = 300
# categories that are machinery, not a playable quest: owned records only,
# no context expansion (their owners are scattered over the whole file)
NO_CONTEXT_CATEGORIES = {"VAR", "DQSLOT", "DQTPL", "WW"}
PLAYABLE = ("HQ", "NQ", "RB", "DQ", "GQ", "SQ")

LITERAL_ID_RE = re.compile(r"^(?:DQ|NQ|RB)_(\d+)$")
NAME_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_.]{2,79}")   # quest_index.TOKEN_RE on str
TAG_QUESTBOOK = 0x35
OP_INT = 0x0b
FAIL_STATES = ("truncated", "unterminated")


def clusters(indices, gap=DEFAULT_GAP):
    """Group sorted record indices into runs; a run is expanded to its full
    record range so the quest's intervening logic is included."""
    out = []
    for i in indices:
        if out and i - out[-1][-1] <= gap:
            out[-1].append(i)
        else:
            out.append([i])
    return out


def _text(token):
    t = qi._text_for(token)
    if not t:
        return None
    return t.strip().replace("\r", " ").replace("\n", " | ")


# ------------------------------------------------------------- decoding ----
_TILE = {}


def tile(ck, off, tag, payload):
    """funkcode_disasm.tile_payload with tag= passed, memoised per (content
    key, offset) -- many records are printed by several shards."""
    k = (ck, off)
    t = _TILE.get(k)
    if t is None:
        t = fd.tile_payload(payload, tag)
        _TILE[k] = t
    return t


def op_strings(o):
    """(string, is_position_operand) pairs carried by one decoded operand."""
    if o.status != "ok" or o.value is None:
        return []
    f, v = o.form, o.value
    if f == fd.F_POS:
        return [(v[1], True)] if v[0] == "name" else []
    if f in (fd.F_STR,):
        return [(v, False)]
    if f == fd.F_STR2:
        return [(v[0], False), (v[1], False)]
    if f == fd.F_U32S:
        return [(v[1], False)]
    if f == fd.F_INTN:
        return [(x, False) for x in v[1:] if x]
    if f == fd.F_SEL:
        return [(v[1], False)] if v[1] else []
    if f in (fd.F_INTV, fd.F_SYM1F, fd.F_SYM9F):
        return [(v[1], False)] if v[0] in ("var", "symref") else []
    if f == "char64":
        return [(v.split(b"\0", 1)[0].decode("latin-1", "replace"), False)]
    return []


class Tiling(object):
    """Operand-line tiling of every distinct record a shard prints, counted
    the way disasm_tiling_check.py counts it (truncated / unterminated /
    leftover lines; the 2026-09-11 decoder has no overrun/unknown state)."""

    def __init__(self):
        self.seen = set()
        self.lines = 0
        self.fails = []          # (where, record index, offset, tag, op, kind)

    def add(self, ck, where, index, off, tag, payload, t):
        if (ck, off) in self.seen:
            return
        self.seen.add((ck, off))
        self.lines += len(t.ops) + (1 if t.leftover else 0)
        for o in t.ops:
            if o.status in FAIL_STATES:
                self.fails.append((where, index, off, tag, o.op, o.status))
        if t.leftover:
            self.fails.append((where, index, off, tag, payload[t.leftover[0]], "leftover"))

    @property
    def records(self):
        return len(self.seen)


# ------------------------------------------------------ section families ----
def family_numbers(name):
    """[(family, number)] for a section name of a quest-id family."""
    low = name.lower()
    out = []
    m = re.match(r"^qis_(trigger|onenter|onsetup|onexit|onlose)(\d+)$", low)
    if m:
        out.append(("QIS_*<id>", int(m.group(2))))
    m = re.match(r"^selftriggerquest(?:pool)?(\d+)$", low)
    if m:
        out.append(("SelfTriggerQuest<id>", int(m.group(1))))
    m = re.match(r"^dialog:dlg_(\d+)_", low)
    if m:
        out.append(("Dialog:DLG_<id>_*", int(m.group(1))))
    m = re.match(r"^omo(\d+)(?!\d)", low)
    if m:
        out.append(("OMO<id><trigger>", int(m.group(1))))
    m = re.match(r"^belohnung_?(\d+)$", low)
    if m:
        out.append(("belohnung_<id>", int(m.group(1))))
    for pre, fam in (("btn_", "btn_*_<id>"), ("od_", "od_<id>"), ("eq_", "eq_<id>"),
                     ("belohne_", "belohne_*_<id>")):
        if low.startswith(pre):
            for part in low[len(pre):].split("_"):
                if part.isdigit():
                    out.append((fam, int(part)))
    return out


class SourceCtx(object):
    """Vectoren/StartCode view of one baseline source, built once."""

    def __init__(self, key, scan):
        self.key = key
        self.scan = scan
        self.v = V.load(key)
        nz = sorted((s for s in self.v.sections if s.length), key=lambda s: s.start)
        self._nz = nz
        self._nz_starts = [s.start for s in nz]
        self.fam = collections.defaultdict(list)      # number -> [(section index, family)]
        self.tok = collections.defaultdict(list)      # QUEST ID -> [section index]
        for s in self.v.sections:
            for fam, n in family_numbers(s.name):
                self.fam[n].append((s.index, fam))
            seen = set()
            for m in NAME_TOKEN_RE.finditer(s.name):
                h = qi.classify(m.group())
                if h and h[1].upper() not in seen:
                    seen.add(h[1].upper())
                    self.tok[h[1].upper()].append(s.index)
        self._pos = None
        self._dlg = None
        self._dlg_by_sec = None

    def containing(self, off):
        j = bisect.bisect_right(self._nz_starts, off) - 1
        if j >= 0 and self._nz[j].start <= off < self._nz[j].end:
            return self._nz[j]
        return None

    def record_range(self, sec):
        st = self.scan.starts
        i = bisect.bisect_left(st, sec.start)
        out = []
        while i < len(st) and st[i] < sec.end:
            out.append(i)
            i += 1
        return out

    @property
    def positions(self):
        """folded name -> startcode.Position (engine table: StartCode tag 0x17
        then FunkCode tag 0x17)."""
        if self._pos is None:
            self._pos = {S.fold(n): p for n, p in
                         S.positions(self.key, include_funkcode=True).items()}
        return self._pos

    def _load_dlg(self):
        if self._dlg is None:
            self._dlg = collections.defaultdict(list)
            self._dlg_by_sec = collections.defaultdict(list)
            for i, d in enumerate(S.dlgnpcs(self.key)):
                # runtime index = list position + 1 (dummy entry 0,
                # FUN_00475680:765-789 -- startcode.py)
                self._dlg[S.fold(d.name)].append((i + 1, d))
                self._dlg_by_sec[d.section].append((i + 1, d))

    @property
    def dlgnpcs(self):
        self._load_dlg()
        return self._dlg

    @property
    def dlgnpcs_by_section(self):
        self._load_dlg()
        return self._dlg_by_sec


# -------------------------------------------------------------- builder ----
class ShardBuilder(object):
    def __init__(self, scans, root=SHARD_ROOT, gap=DEFAULT_GAP,
                 max_records=DEFAULT_MAX_RECORDS, with_text=True, ops_limit=40,
                 with_sections=True, with_positions=True,
                 max_section_records=DEFAULT_SECTION_RECORDS, max_refs=DEFAULT_MAX_REFS):
        self.scans = scans
        self.root = root
        self.gap = gap
        self.max_records = max_records
        self.with_text = with_text
        self.ops_limit = ops_limit
        self.with_sections = with_sections
        self.with_positions = with_positions
        self.max_section_records = max_section_records
        self.max_refs = max_refs
        self._ctx = {}
        self.totals = collections.Counter()

    def ctx(self, key):
        c = self._ctx.get(key)
        if c is None:
            c = SourceCtx(key, self.scans[key])
            self._ctx[key] = c
        return c

    # ------------------------------------------------------- quest ids --
    def quest_ids(self, q, ctx, owned):
        sc = ctx.scan
        journal = collections.Counter()
        holders = collections.Counter()
        secq = collections.Counter()
        for i in owned:
            off, tag, size, payload = sc.records[i]
            if tag == TAG_QUESTBOOK:
                t = tile(sc.key, off, tag, payload)
                o = t.ops[0] if t.ops else None
                if o is not None and o.op == OP_INT and o.status == "ok" and o.value[0] == "u32":
                    journal[o.value[1]] += 1
            s = ctx.containing(off)
            if s is not None:
                holders[s.index] += 1
                if s.quest != -1:
                    secq[s.quest] += 1
        ids = collections.OrderedDict()
        if q["category"] in PLAYABLE:
            for n in sorted(set(journal) & set(secq)):
                ids[n] = ["journal x%d" % journal[n], "sections x%d" % secq[n]]
            if not journal and len(secq) == 1 and sum(secq.values()) == len(owned):
                n = next(iter(secq))
                ids[n] = ["no journal record; all %d owned records sit in its sections" % len(owned)]
        m = LITERAL_ID_RE.match(q["id"])
        if m:
            n = int(m.group(1))
            if ctx.v.quest(n) is not None:
                ids.setdefault(n, []).append("literal id, a registry quest")
        return ids, journal, secq, holders

    def members(self, q, ctx, ids, holders):
        mem = collections.OrderedDict()
        fams = collections.defaultdict(set)
        for n in ids:
            for s in ctx.v.for_quest(n):
                mem.setdefault(s.index, set()).add("id")
            for si, fam in ctx.fam.get(n, ()):
                mem.setdefault(si, set()).add("name")
                fams[si].add(fam)
        for si in ctx.tok.get(q["id"].upper(), ()):
            mem.setdefault(si, set()).add("name")
            fams[si].add("token")
        for si in holders:
            mem.setdefault(si, set()).add("rec")
        return collections.OrderedDict((k, mem[k]) for k in sorted(mem)), fams

    # ------------------------------------------------------------ write --
    def write(self, q):
        """Write one quest's shard file, return its path (relative to GAME_ROOT)."""
        campaign = q["campaign"]
        d = os.path.join(self.root, campaign)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, q["id"] + ".txt")

        baseline = q["baselineSource"]
        scan = self.scans[baseline]
        ck = scan.key
        owned = scan.record_indices(q["id"])
        ownset_all = set(owned)
        no_ctx = q["category"] in NO_CONTEXT_CATEGORIES
        tiling = Tiling()
        used = collections.OrderedDict()   # record index -> (off, tag, payload, tile) printed from baseline

        body = io.StringIO()

        def emit(line=""):
            body.write(line + "\n")

        def dump(src_scan, i, marker, where, record_used=True):
            off, tag, size, payload = src_scan.records[i]
            t = tile(src_scan.key, off, tag, payload)
            tiling.add(src_scan.key, where, i, off, tag, payload, t)
            if record_used:
                used[i] = (off, tag, payload, t)
            qs.dump_record(off, tag, size, payload, indent=1, ops_limit=self.ops_limit,
                           marker=marker, index=i, emit=emit)
            return tag, t

        # ------------------------------------------------------ baseline --
        written = 0
        cls = clusters(owned, self.gap) if not no_ctx else [[i] for i in owned]
        emit("=== BASELINE %s — %d owned records in %d cluster(s) ==="
             % (baseline, len(owned), len(cls)))
        tag_hist = collections.Counter()
        ops_seen = set()
        for ci, run in enumerate(cls, 1):
            lo, hi = run[0], run[-1]
            span = range(lo, hi + 1) if not no_ctx else run
            ownset = set(run)
            emit("\n--- cluster %d: records %d..%d (%d records, %d owned) ---"
                 % (ci, lo, hi, len(list(span)), len(run)))
            for i in span:
                if written >= self.max_records:
                    emit("\n... record cap %d reached; %d owned records not shown "
                         "(raise --max-records or use quest_script.py)"
                         % (self.max_records, len([x for x in owned if x > i])))
                    break
                tag, t = dump(scan, i, ">> " if i in ownset else " . ", "baseline")
                tag_hist[tag] += 1
                ops_seen.update(o.op for o in t.ops if o.op is not None)
                written += 1
            if written >= self.max_records:
                break

        # ------------------------------------------------------ sections --
        sec_summary = None
        jq = {}
        ctx = None
        if self.with_sections or self.with_positions:
            ctx = self.ctx(baseline)
        member_idx = []
        if self.with_sections:
            ids, journal, secq, holders = self.quest_ids(q, ctx, owned)
            mem, fams = self.members(q, ctx, ids, holders)
            member_idx = list(mem)
            playable = q["category"] in PLAYABLE
            n_rec_total = 0
            emit("\n\n=== SECTIONS — Vectoren.bin of %s (vectoren.py) ===" % baseline)
            emit("# quest ids : %s" % (", ".join(
                "%d (%s)" % (n, "; ".join(why)) for n, why in ids.items()) or
                "none derived (see the QUEST IDS rule in quest_shards.py)"))
            if journal:
                emit("#   journal  : owned tag-0x35 records name %s"
                     % ", ".join("%d x%d" % kv for kv in sorted(journal.items())))
            if secq:
                emit("#   sections : owned records sit in sections of quest %s"
                     % ", ".join("%d x%d" % kv for kv in sorted(secq.items(), key=lambda kv: -kv[1])))
            glob = sum(n for si, n in holders.items() if ctx.v.sections[si].quest == -1)
            if glob:
                emit("#              and %d owned record(s) in global sections (quest -1)" % glob)
            for n in ids:
                rq = ctx.v.quest(n)
                if rq is None:
                    emit("# registry  : qid %d not in this source's quest registry" % n)
                    continue
                qis_txt = "  ".join("%s %s" % (lab, ("#%d" % idx) if idx > 0 else "-")
                                    for lab, idx in (("Trigger", rq.trigger), ("OnEnter", rq.on_enter),
                                                     ("OnSetUp", rq.on_setup), ("OnExit", rq.on_exit),
                                                     ("OnLose", rq.on_lose)))
                emit("# registry  : qid %d = registry[%d] %r  f104=%d f108=%d" % (n, rq.index, rq.name, rq.f104, rq.f108))
                emit("#             cached QIS section indices: %s   ('-' = 0 in the file)" % qis_txt)
            why_n = collections.Counter(r for rs in mem.values() for r in rs)
            emit("# members   : %d section(s) — id %d, name %d, rec %d; listed in Vectoren index order"
                 % (len(mem), why_n["id"], why_n["name"], why_n["rec"]))
            emit("#   why: id = +0x48 is a quest id above; name = quest-id name family or a token"
                 " of this quest; rec = holds owned records")
            emit("#   family: the name family that matched (why=name), else vectoren.family(name)")
            if not playable:
                emit("#   category %s: bodies only for id/name members; rec-only sections are listed,"
                     " their owned records are in BASELINE above" % q["category"])
            emit("#")
            emit("#  %6s  %-44s %-18s %6s  %-24s %-12s %s" % ("index", "name", "{start,len}", "quest",
                                                            "family", "why", "owned"))
            for si, why in mem.items():
                s = ctx.v.sections[si]
                if "name" in why and fams.get(si):
                    fam = "/".join(sorted(fams[si]))
                else:
                    fam = s.family if s.family != "other" else ""
                emit("   %6d  %-44s %-18s %6d  %-24s %-12s %s"
                     % (s.index, s.name, "{%d,%d}" % (s.start, s.length), s.quest, fam[:24],
                        " ".join(r for r in ("id", "name", "rec") if r in why),
                        holders.get(si, "") or ""))
            # bodies
            budget = self.max_section_records
            skipped = []
            for si, why in mem.items():
                s = ctx.v.sections[si]
                if not playable and not (why & {"id", "name"}):
                    continue
                recs = ctx.record_range(s)
                first = ctx.v.find(s.name)
                dup = ""
                if first is not None and first.index != s.index:
                    dup = "  (duplicate name: the engine's by-name lookup reaches #%d first)" % first.index
                head = ("\n--- section #%d %s {%d,%d} quest=%d [%s] — %d record(s)%s ---"
                        % (s.index, s.name, s.start, s.length, s.quest,
                           " ".join(r for r in ("id", "name", "rec") if r in why), len(recs), dup))
                if not s.length:
                    note = "0 bytes, no records"
                    if re.match(r"^QIS_Trigger-?\d+$", s.name, re.I):
                        note += ("; a zero-length QIS_Trigger runs to its end at once, so it is an"
                                 " always-pass gate (vectoren.py: WorkFunktion FUN_0046ba90:100-107)")
                    emit(head)
                    emit("    (%s)" % note)
                    continue
                if n_rec_total + len(recs) > budget:
                    skipped.append(s.index)
                    continue
                emit(head)
                for i in recs:
                    tag, t = dump(scan, i, ">> " if i in ownset_all else " . ", "section")
                    ops_seen.update(o.op for o in t.ops if o.op is not None)
                    n_rec_total += 1
            if skipped:
                emit("\n... section record budget %d reached; bodies of %d member section(s) not shown: %s"
                     " (raise --max-section-records or run vectoren.py --section NAME --records --ops 40)"
                     % (budget, len(skipped), ", ".join("#%d" % x for x in skipped[:40])
                        + (" ..." if len(skipped) > 40 else "")))
            sec_summary = (ids, mem, n_rec_total, holders)
            jq = {"questIds": list(ids),
                  "questIdEvidence": {"journal": {str(k): v for k, v in sorted(journal.items())},
                                      "sections": {str(k): v for k, v in sorted(secq.items())}},
                  "sectionCount": len(mem)}
            if playable or q["category"] == "DQSLOT":
                jq["sections"] = [[si, ctx.v.sections[si].name, ctx.v.sections[si].start,
                                   ctx.v.sections[si].length, ctx.v.sections[si].quest,
                                   ",".join(r for r in ("id", "name", "rec") if r in why)]
                                  for si, why in mem.items()]
            q["vectoren"] = dict(jq, source=baseline)

        # ----------------------------------------------------- positions --
        pos_summary = None
        if self.with_positions:
            pos_summary = self._positions_block(q, ctx, used, member_idx, emit)

        # ----------------------------------------------- by-name references --
        if self.with_sections:
            self._byname_block(ctx, used, set(member_idx), emit)

        emit("\n=== record tags in the BASELINE block (label = compiler keyword; old = pre-2026-09-11 shard label) ===")
        for tag, n in tag_hist.most_common():
            old = ", ".join(ft.aliases_for(tag))
            emit("  %4d  tag=0x%02x  [%s]%s" % (n, tag, tag_label(tag), ("  old: " + old) if old else ""))
        if ops_seen:
            emit("\n=== operand labels printed in this shard (funkcode_disasm.GRAMMAR; legacy = funkcode_ops / old shard label) ===")
            for op in sorted(ops_seen):
                g = fd.GRAMMAR[op]
                emit("  0x%02x  %-8s %-11s legacy %s" % (op, g.label, g.form, g.legacy or "-"))

        # --------------------------------------------------- class diffs --
        bd = q.get("baseClassDiff") or {}
        for cname in bd.get("differing", []):
            key = "base:" + cname
            if key not in self.scans:
                continue
            sc = self.scans[key]
            idxs = sc.record_indices(q["id"])
            emit("\n\n=== CLASS DIFF %s — %d owned records "
                 "(payload md5 %s vs baseline %s) ==="
                 % (key, len(idxs), q["payloadMd5"].get(key), q["payloadMd5"].get(baseline)))
            emit("=== only this class's OWNED records are shown (no context) ===")
            for n, i in enumerate(idxs):
                if n >= self.max_records // 2:
                    emit("\n... %d more owned records not shown" % (len(idxs) - n))
                    break
                dump(sc, i, ">> ", key, record_used=False)

        ad = q.get("addonClassDiff") or {}
        for cname in ad.get("differing", []):
            key = "addon:" + cname
            if key not in self.scans:
                continue
            sc = self.scans[key]
            idxs = sc.record_indices(q["id"])
            emit("\n\n=== ADDON CLASS DIFF %s — %d owned records ===" % (key, len(idxs)))
            for n, i in enumerate(idxs):
                if n >= self.max_records // 2:
                    emit("\n... %d more owned records not shown" % (len(idxs) - n))
                    break
                dump(sc, i, ">> ", key, record_used=False)

        # -------------------------------------------------------- header --
        head = io.StringIO()

        def hemit(line=""):
            head.write(line + "\n")

        hemit("# %s — %s" % (q["id"], q.get("title") or "(no questbook title resolved)"))
        hemit("#")
        nfail = len(tiling.fails)
        if nfail:
            hemit("# WARNING: %d operand lines failed to tile — see README §4.2" % nfail)
        else:
            hemit("# tiling: clean")
        hemit("# category        : %s" % q["category"])
        hemit("# campaign        : %s" % campaign)
        hemit("# baseline source : %s   (%d owned records)" % (baseline, len(owned)))
        if q["classSpecific"]:
            bits = []
            if bd.get("differing"):
                bits.append("payload differs in: " + ", ".join(bd["differing"]))
            if bd.get("missing"):
                bits.append("absent in: " + ", ".join(bd["missing"]))
            hemit("# class-specific  : YES — %s" % ("; ".join(bits) or "see index"))
        else:
            hemit("# class-specific  : no (identical owned-record payloads in every class that has it)")
        hemit("# sources         : %s" % ", ".join(
            "%s(%d)" % (k, n) for k, n in q["recordsBySource"].items()))
        hemit("# tokens          : %s" % ", ".join(q["tokens"]))
        hemit("# tiling detail   : %d distinct records printed, %d operand lines, %d failed"
              " (funkcode_disasm.GRAMMAR, tag= passed)" % (tiling.records, tiling.lines, nfail))
        for where, i, off, tag, op, kind in tiling.fails[:12]:
            hemit("#                   %s #%d @0x%06x tag=0x%02x op=%s %s"
                  % (where, i, off, tag, ("0x%02x" % op) if op is not None else "--", kind))
        if sec_summary is not None:
            ids, mem, nrec, holders = sec_summary
            hemit("# quest ids       : %s" % (", ".join(str(n) for n in ids) or "none derived"))
            hemit("# sections        : %d Vectoren.bin section(s) (%d record(s) dumped) — see SECTIONS"
                  % (len(mem), nrec))
        if pos_summary is not None:
            hemit("# positions       : %d named position(s) resolved, %d unresolved; %d DlgNPC(s)"
                  " — see POSITIONS & DLGNPCS" % pos_summary)
        hemit("# regenerate      : python sdk/re/py/quest_shards.py --quest %s" % q["id"])
        hemit("#")
        hemit("# FORMAT (shards/README.md §3)")
        hemit("#   '>>' = record OWNS this quest (its payload names one of the tokens above)")
        hemit("#   ' .' = neighbouring record, included as context because it lies inside")
        hemit("#          an owned span (gap <= %d records) or inside a member section." % self.gap)
        hemit("#          Context records may belong to another quest — check the strings.")
        hemit("#   header line: #<record index> <file offset> tag=0xNN'c' [Label] size=..")
        hemit("#          Label = the compiler keyword (funkcode_tags; old labels: funkcode_tags.ALIASES)")
        hemit("#   body: +IIII  OP  LABEL  operand  — the engine's field reader grammar")
        hemit("#          (funkcode_disasm.GRAMMAR, FUN_00472bc0); OP '--' = raw walker field (tags 0x28/0x29/0x2f)")
        if no_ctx:
            hemit("#   NOTE: category %s -> owned records only, no context expansion." % q["category"])
        hemit("#")
        if self.with_text:
            rows = []
            for tk in q["tokens"]:
                txt = _text(tk)
                if txt:
                    rows.append((tk, txt))
            if rows:
                hemit("# ---- resolved global.res strings (scripts/us/global.res) ----")
                for tk, txt in rows:
                    hemit("#   %-42s %s" % (tk, txt[:300]))
                hemit("#")

        q["tiling"] = {"records": tiling.records, "operandLines": tiling.lines, "failed": nfail}
        self.totals["shards"] += 1
        self.totals["records"] += tiling.records
        self.totals["lines"] += tiling.lines
        self.totals["failed"] += nfail
        self.totals["warned"] += 1 if nfail else 0
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(head.getvalue())
            fh.write(body.getvalue())
        try:
            return os.path.relpath(path, fs.GAME_ROOT).replace("\\", "/")
        except ValueError:                 # --root on another drive
            return os.path.abspath(path).replace("\\", "/")

    # -------------------------------------------------- positions block --
    def _positions_block(self, q, ctx, used, member_idx, emit):
        pos_hits = collections.OrderedDict()     # folded -> [name, [(rec, tag, op)]]
        unresolved = collections.OrderedDict()
        dlg_hits = collections.OrderedDict()     # (rt index) -> [DlgNPC, set(reasons), [recs]]
        posd = ctx.positions
        dlgd = ctx.dlgnpcs
        for i, (off, tag, payload, t) in used.items():
            for o in t.ops:
                for s, is_pos in op_strings(o):
                    if not s:
                        continue
                    f = S.fold(s)
                    if f in posd:
                        pos_hits.setdefault(f, [s, []])[1].append((i, tag, o.op))
                    elif is_pos and not f.startswith("cpos") and f != "hero":
                        unresolved.setdefault(s, []).append(i)
                    for rt, d in dlgd.get(f, ()):
                        e = dlg_hits.setdefault(rt, [d, set(), []])
                        e[1].add("operand")
                        e[2].append(i)
        for si in member_idx:
            for rt, d in ctx.dlgnpcs_by_section.get(si, ()):
                dlg_hits.setdefault(rt, [d, set(), []])[1].add("section")

        emit("\n\n=== POSITIONS & DLGNPCS — %s (startcode.py) ===" % ctx.key)
        emit("# positions = the engine's position table: StartCode.bin tag 0x17, then FunkCode.bin tag 0x17")
        emit("#   (startcode.positions(include_funkcode=True); R = scatter radius, Z = -1 when absent;")
        emit("#   a position operand resolves to (x, y) scattered by R, Z < 0 -> 0 — FUN_00472bc0:2470-2519)")
        emit("# looked up: every string operand of the BASELINE and SECTIONS records above")
        if pos_hits:
            emit("#  %-36s %6s %6s %4s %3s  %-26s %s" % ("name", "x", "y", "R", "Z", "declared", "referenced by"))
            for n, (f, (name, refs)) in enumerate(pos_hits.items()):
                if n >= self.max_refs:
                    emit("   ... %d more" % (len(pos_hits) - n))
                    break
                p = posd[f]
                ex = p.extras
                decl = "%s #%d @0x%06x" % (ex["file"].replace(".bin", ""), ex["record"], ex["offset"])
                if ex.get("declarations", 1) > 1:
                    decl += " (x%d)" % ex["declarations"]
                refs_txt = ", ".join("#%d %s op%s" % (i, tag_label(tg), ("%02x" % op) if op is not None else "--")
                                     for i, tg, op in refs[:4]) + (" ..." if len(refs) > 4 else "")
                emit("   %-36s %6d %6d %4d %3d  %-26s %s" % (p.name, p.x, p.y, p.radius, p.z, decl, refs_txt))
        else:
            emit("#   (no named position among the operands)")
        if unresolved:
            emit("# unresolved position operands (no tag-0x17 declaration of that name in %s):" % ctx.key)
            for n, (s, recs) in enumerate(unresolved.items()):
                if n >= self.max_refs:
                    emit("   ... %d more" % (len(unresolved) - n))
                    break
                emit("   %-36s used by %s" % (s, ", ".join("#%d" % i for i in recs[:6])))
        emit("#")
        emit("# DlgNPCs = StartCode.bin tag 0x28 (rt# = runtime index, +0x44 = the Dialog:<name> section,")
        emit("#   marker = entry +0x48); why: section = its Dialog section is a member above, operand = named by a record")
        if dlg_hits:
            emit("#  %-32s %5s  %-44s %6s  %-20s %s" % ("name", "rt#", "Dialog section", "marker", "StartCode", "why"))
            for n, (rt, (d, why, recs)) in enumerate(sorted(dlg_hits.items())):
                if n >= self.max_refs:
                    emit("   ... %d more" % (len(dlg_hits) - n))
                    break
                secn = ("#%d %s" % (d.section, d.section_name)) if d.section_name else ("#%d" % d.section)
                emit("   %-32s %5d  %-44s %6d  #%-5d @0x%06x  %s%s"
                     % (d.name, rt, secn[:44], d.marker, d.record, d.offset, "+".join(sorted(why)),
                        (" (" + ", ".join("#%d" % i for i in recs[:4]) + ")") if recs else ""))
        else:
            emit("#   (no DlgNPC)")
        return (len(pos_hits), len(unresolved), len(dlg_hits))

    # ----------------------------------------------- by-name references --
    def _byname_block(self, ctx, used, members, emit):
        hits = collections.OrderedDict()   # section index -> [section, [(rec, tag, string)]]
        for i, (off, tag, payload, t) in used.items():
            for o in t.ops:
                for s, is_pos in op_strings(o):
                    if not s or is_pos or not (1 <= len(s) <= 63):
                        continue
                    sec = ctx.v.find(s)
                    if sec is not None:
                        hits.setdefault(sec.index, [sec, []])[1].append((i, tag, s))
        emit("\n=== operand strings that name a Vectoren.bin section (engine by-name lookup: first case-insensitive match) ===")
        emit("# equality only. The engine reaches a section by name from CallFunktion 0x16, TriggerQuest 0x14,")
        emit("# SelfTriggerQuest, DeleteFunktionBlock 0x0e (vectoren.py) and from SetButton actions / CreateNPC hooks")
        emit("# (vectoren.FAMILIES); a string in another record (e.g. SetVar) may merely share the name.")
        if not hits:
            emit("#   (none)")
            return
        for n, (si, (sec, refs)) in enumerate(sorted(hits.items())):
            if n >= self.max_refs:
                emit("   ... %d more" % (len(hits) - n))
                break
            tags = collections.OrderedDict()
            for i, tg, s in refs:
                tags.setdefault(tag_label(tg), []).append(i)
            emit("   #%-6d %-44s {%d,%d} quest=%d%s  <- %s"
                 % (si, sec.name, sec.start, sec.length, sec.quest, "  [member]" if si in members else "",
                    "; ".join("%s %s" % (lab, ",".join("#%d" % x for x in ix[:4]) + (" ..." if len(ix) > 4 else ""))
                              for lab, ix in tags.items())))


def write_shard(q, scans, root=SHARD_ROOT, gap=DEFAULT_GAP,
                max_records=DEFAULT_MAX_RECORDS, with_text=True,
                ops_limit=40, builder=None):
    """Compatibility wrapper (pre-2026-09-11 signature)."""
    if builder is None:
        builder = ShardBuilder(scans, root=root, gap=gap, max_records=max_records,
                               with_text=with_text, ops_limit=ops_limit)
    return builder.write(q)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default="all")
    ap.add_argument("--root", default=SHARD_ROOT)
    ap.add_argument("--quest", help="only this quest id")
    ap.add_argument("--categories", help="comma-separated category filter")
    ap.add_argument("--gap", type=int, default=DEFAULT_GAP)
    ap.add_argument("--max-records", type=int, default=DEFAULT_MAX_RECORDS)
    ap.add_argument("--max-section-records", type=int, default=DEFAULT_SECTION_RECORDS)
    ap.add_argument("--ops", type=int, default=40, help="max opcodes per record")
    ap.add_argument("--no-text", action="store_true", help="skip global.res strings")
    ap.add_argument("--no-sections", action="store_true", help="skip the Vectoren SECTIONS block")
    ap.add_argument("--no-positions", action="store_true", help="skip the StartCode POSITIONS block")
    ap.add_argument("--index", default=qi.JSON_OUT,
                    help="also rewrite the index JSON with shardFile paths")
    args = ap.parse_args()

    t0 = time.time()
    sources = fs.resolve(args.sources, default="all")
    meta, quests, scans = qi.build_index(sources, with_titles=not args.no_text)
    builder = ShardBuilder(scans, root=args.root, gap=args.gap, max_records=args.max_records,
                           with_text=not args.no_text, ops_limit=args.ops,
                           with_sections=not args.no_sections,
                           with_positions=not args.no_positions,
                           max_section_records=args.max_section_records)

    cats = set(c.strip().upper() for c in args.categories.split(",")) if args.categories else None
    written = set()
    for q in quests:
        if args.quest and q["id"].upper() != args.quest.upper():
            continue
        if cats and q["category"] not in cats:
            continue
        q["shardFile"] = builder.write(q)
        written.add(os.path.normcase(os.path.join(fs.GAME_ROOT, q["shardFile"])))
        if args.quest:
            print(q["shardFile"])

    if not args.quest and not cats:
        qi.write_json(meta, quests, args.index)
        qi.write_md(meta, quests, qi.MD_OUT)
        stale = []
        for dp, _dn, fnames in os.walk(args.root):
            for fn in fnames:
                if fn.endswith(".txt"):
                    p = os.path.normcase(os.path.join(dp, fn))
                    if p not in written:
                        stale.append(p)
        if stale:
            print("NOTE: %d shard file(s) on disk were not written by this run (stale?): %s"
                  % (len(stale), ", ".join(sorted(stale)[:10])))
    tt = builder.totals
    print("wrote %d shards under %s in %.1fs" % (len(written), args.root, time.time() - t0))
    print("tiling: %d distinct-per-shard records, %d operand lines, %d failed lines, %d shard(s) with a WARNING"
          % (tt["records"], tt["lines"], tt["failed"], tt["warned"]))


if __name__ == "__main__":
    main()
