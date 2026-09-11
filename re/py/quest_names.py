"""quest_names.py -- id -> name resolution for the quest knowledge base.

Turns the raw numbers that fill the quest detail files into names, from the
shipped game data only, and annotates them in place.

WHAT RESOLVES, AND FROM WHERE (every rule below is cited; see NAMES.md)
-------------------------------------------------------------------------
1. Numeric resource ids  ("res:1024", "Res:17084", "CPOS:Res:17085")
   The FunkCode field reader FUN_00472bc0 (case 0x40/0x95, decompile lines
   1733-1791) copies the operand, tests the prefix "res:" with _strnicmp
   (FUN_0085aa60, DAT_0094d5b8 = "res:"), and when the next character is a
   digit (FUN_0084b2e5 = ctype & 4) converts it with atoi (FUN_0084b24a) and
   calls FUN_006726f0(id).  FUN_006726f0 (lines 5-9): bit 31 clear ->
   FUN_0080e780(id), which formats the id in DECIMAL through an ostream
   (FUN_005f6290 = operator<<(int): sentry 0x5f7b90, num_put facet 0x1830368,
   do_put vtbl+0x18) and hashes the string (x113 mod 999999991, toupper)
   before FUN_0080eaf0 does the global.res lookup.
   => text(res:N) = global.res[ sacred_hash(str(N)) ].
   The vanilla exe opens ".\\SCRIPTS\\%s\\global.res" (string in
   Sacred.exe / Sacred_decrypted.exe); scripts/us/global.res is the only
   vanilla table in this install.  scripts/<lang>/SRglbl.res belong to
   SacredReborn.exe (".\\SCRIPTS\\%s\\SRglbl.res") -- ReBorn data, reported
   separately and never used for an annotation.

2. Type ids (creatures, items, objects, FX -- ONE id space)
   * Symbol: the exe's static TYPE_* table, VA 0x008EC328, stride 0x44,
     id @+0, name @+4, 5624 rows (FUN_0043cd90 lines 45-53: the name walk
     from s_TYPE_INVALID_008ec32c to PTR_DAT_0094990c, id from DAT_008ec328).
     Certain.
   * Definition record: pak/Items.pak ("ITM", 32768 entries; 256-byte header,
     32768 x {i32 type, i32 offset, i32 size} descriptors, then 128-byte
     records, descriptors contiguous) indexed by type id -- the engine's
     def table (FUN_00425ea0: id*0x80 + base, 0 < id < 0x7e60).  +0x2E =
     class byte (FUN_00426610: creature <=> byte in {2,3,0x20});
     +0x37 = model file name (FUN_0043cd90 lines 22-27 compare names there).
     Layout per Resacred rs_file.h:359-386 / rs_file.cpp:774-798.
   * Display name: global.res key str(id) (the same decimal-string path) --
     but that numeric key space is SHARED with UI strings, so it is used
     only where it is validated:
       - creature class: 443 of the 448 creature ids that both sides name
         agree with the community characters.csv
         (custom/lua/lib/data/creatures.lua); the 5 that differ are named
         variants of the same model (four Subkari "Demon Lord", 700
         "Shareefa");
       - any other id that has a pak/Weapon.pak record (every object-class
         TYPE id outside FX does, 1:1): the English text is the translation
         of that record's German designer name (Weapon.pak Editor
         MainForm.frm:344-394: "WPN", version 8, u32 count @4, 258-byte
         records at i*258, name ASCIIZ @+38, id @+0x7E);
       - FX (class 12) and the few class-1 ids with no Weapon.pak record get
         the symbol only: nothing validates their texts, and many are UI
         strings (853 "Greed", 1160 "Weapon Bonus", 1161 "vs.", 1179
         "Animals") although a few happen to fit (815 "Net Gate").
     Placeholder texts ("+++EMPTY+++") count as no name.

3. Named text keys ("res:HQ_6_2_1_Log_preQuest"): sacred_hash(name) in
   global.res; absence is checked in all 10 .res files.
4. PlaySound (tag 0x68) names: FUN_004a9730 prepends "SOUND_FX_" when the
   name lacks it (strnicmp with s_SOUND_FX__0094f680, 8), then
   FUN_00676170 getSndType does a linear stricmp over the exe's static sound
   table (VA 0x00964870, id @+0, name @+4, stride 0x44, to 0x009D6908).
   global.res is not on that path.

CLI (run from anywhere; paths derive from this file):
    python quest_names.py res 1024 17643          numeric res ids, all tables
    python quest_names.py key HQ_6_2_1_Log_preQuest
    python quest_names.py type 689 1330 1207
    python quest_names.py sound HQ_0_1_NPC_KOMMENTAR_ALCALATA2
    python quest_names.py sweep                   corpus sweeps (all 20 sources)
    python quest_names.py scan [--all]            dry run over the 9 detail files
    python quest_names.py annotate [--check]      in place, idempotent
    python quest_names.py build                   names.json + NAMES.md blocks
    python quest_names.py selftest

Read-only on bin/, pak/, scripts/, the exe.  Writes only
sdk/.claude/knowledge/quests/{names.json, NAMES.md generated blocks} and,
for `annotate`, the nine per-quest detail files listed in DETAIL_FILES.
"""
import argparse
import collections
import datetime
import hashlib
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import funkcode_sources as fs          # noqa: E402
import funkcode_disasm as fd           # noqa: E402
from sacred_hash import sacred_hash    # noqa: E402

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

GAME = fs.GAME_ROOT
EXE = os.path.join(GAME, "sdk", "Sacred_decrypted.exe")
KB = os.path.join(GAME, "sdk", ".claude", "knowledge", "quests")
PAK = os.path.join(GAME, "pak")
SCRIPTS = os.path.join(GAME, "scripts")
IMAGE_BASE = 0x400000

TYPE_TABLE_VA = 0x008EC328       # FUN_0043cd90:45-53
TYPE_TABLE_ROWS = 5624           # (0x0094990C - 0x008EC32C) / 0x44 ; 0x15f8 at :59
SOUND_TABLE_VA = 0x00964870      # FUN_00676170:10-18
SOUND_TABLE_END_VA = 0x009D6908
STRIDE = 0x44
CREATURE_CLASS = (2, 3, 0x20)    # FUN_00426610

PLACEHOLDER_RE = re.compile(r"^\+*\s*EMPTY\s*\+*$|^\s*$", re.I)

LANGS = ["us", "de", "fr", "it", "sp", "pl", "ru", "ko", "zh"]
DETAIL_FILES = ["HQ_base_1.md", "HQ_base_2.md", "HQ_addon_1.md", "NQ_1.md",
                "NQ_2.md", "RB_SQ_misc_1.md", "DQ_1.md", "DQ_2.md", "DQ_3.md"]

OPEN_L, CLOSE_R = "⟨", "⟩"      # the annotation brackets
MARK = " " + OPEN_L


def _md5(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ------------------------------------------------------------------ loaders --
_cache = {}


def _cached(key, fn):
    if key not in _cache:
        _cache[key] = fn()
    return _cache[key]


def exe_bytes():
    return _cached("exe", lambda: open(EXE, "rb").read())


def exe_cstr(va, n=64):
    o = va - IMAGE_BASE
    return exe_bytes()[o:o + n].split(b"\0")[0].decode("latin-1")


def type_table():
    """{type id: TYPE_* symbol} from the exe's static table (certain)."""
    def load():
        exe = exe_bytes()
        out = collections.OrderedDict()
        for r in range(TYPE_TABLE_ROWS):
            o = TYPE_TABLE_VA - IMAGE_BASE + r * STRIDE
            tid = struct.unpack_from("<i", exe, o)[0]
            out[tid] = exe[o + 4:o + STRIDE].split(b"\0")[0].decode("latin-1")
        return out
    return _cached("types", load)


def sound_table():
    """{upper-case SOUND_FX_* name: sound id} from the exe's static table."""
    def load():
        exe = exe_bytes()
        out = collections.OrderedDict()
        va = SOUND_TABLE_VA
        while va + 4 < SOUND_TABLE_END_VA:
            o = va - IMAGE_BASE
            sid = struct.unpack_from("<I", exe, o)[0]
            nm = exe[o + 4:o + STRIDE].split(b"\0")[0].decode("latin-1")
            out.setdefault(nm.upper(), (sid, nm))
            va += STRIDE
        return out
    return _cached("sounds", load)


def items_pak(name="Items.pak"):
    """{type id: (class byte @+0x2E, model name @+0x37)} for every def record."""
    def load():
        d = open(os.path.join(PAK, name), "rb").read()
        assert d[:3] == b"ITM", "not an ITM pak"
        n = struct.unpack_from("<i", d, 4)[0]
        out = {}
        for i in range(n):
            _t, off, size = struct.unpack_from("<iii", d, 256 + 12 * i)
            rec = d[off:off + 128]
            if len(rec) < 128:
                continue
            out[i] = (rec[0x2E], rec[0x37:0x37 + 32].split(b"\0")[0].decode("latin-1"))
        return out
    return _cached("pak:" + name, load)


def creature_pak_ids(name="Creature.pak"):
    """ids of the CIF records (sdk/ports/balance/sacred_creature.h layout)."""
    def load():
        c = open(os.path.join(PAK, name), "rb").read()
        assert c[:3] == b"CIF"
        n = (len(c) - 256) // 86
        return set(struct.unpack_from("<i", c, 256 + 86 * i)[0] for i in range(n))
    return _cached("cif:" + name, load)


def load_res(path):
    """{ident: text} for a global.res-format file (re/globalres_format.md)."""
    d = open(path, "rb").read()
    bs = struct.unpack_from("<I", d, 8)[0]
    n = bs // 16
    rows = [struct.unpack_from("<IIII", d, k * 16) for k in range(n)]
    last = struct.unpack_from("<I", d, bs)[0]
    out = {}
    for k in range(n):
        units = (rows[k + 1][0] >> 1) if k < n - 1 else (last >> 1)
        off = rows[k][2]
        out[rows[k][1]] = d[off + 4: off + 4 + 2 * units].decode("utf-16-le", "replace")
    return out


def res_tables():
    """OrderedDict label -> {ident: text}.  'us/global.res' is the vanilla table."""
    def load():
        out = collections.OrderedDict()
        out["us/global.res"] = load_res(os.path.join(SCRIPTS, "us", "global.res"))
        for l in LANGS:
            p = os.path.join(SCRIPTS, l, "SRglbl.res")
            if os.path.isfile(p):
                out[l + "/SRglbl.res"] = load_res(p)
        return out
    return _cached("res", load)


def vanilla():
    return res_tables()["us/global.res"]


def community_creatures():
    p = os.path.join(GAME, "custom", "lua", "lib", "data", "creatures.lua")
    def load():
        if not os.path.isfile(p):
            return {}
        txt = open(p, encoding="utf-8").read()
        return {int(m.group(1)): m.group(2) for m in
                re.finditer(r'\[(\d+)\]\s*=\s*\{\s*name="([^"]*)"', txt)}
    return _cached("lua:creatures", load)


def community_names():
    p = os.path.join(GAME, "custom", "lua", "lib", "data", "names.lua")
    def load():
        if not os.path.isfile(p):
            return {}
        txt = open(p, encoding="utf-8").read()
        out = {}
        for m in re.finditer(r'\[(\d+)\]\s*=\s*(?:"((?:[^"\\]|\\.)*)"|\{\s*name="((?:[^"\\]|\\.)*)")', txt):
            out[int(m.group(1))] = m.group(2) if m.group(2) is not None else m.group(3)
        return out
    return _cached("lua:names", load)


def weapon_pak(name="Weapon.pak"):
    """{type id: (record index, German designer name, item-type byte @+0x83)}.
    Layout: Weapon.pak Editor v0.2.3.0 MainForm.frm:344-394 ("WPN", version
    8, u32 count @4, 258-byte records at i*258 for i = 1..count, name ASCIIZ
    @+38 (<= 63 bytes), ModellID @+0x22, id @+0x7E)."""
    def load():
        d = open(os.path.join(PAK, name), "rb").read()
        assert d[:3] == b"WPN" and d[3] == 8, "not a version-8 WPN pak"
        n = struct.unpack_from("<I", d, 4)[0]
        out = {}
        for i in range(1, n + 1):
            r = d[i * 258: i * 258 + 258]
            if len(r) < 258:
                break
            tid = struct.unpack_from("<i", r, 0x7E)[0]
            out[tid] = (i, r[38:101].split(b"\0")[0].decode("latin-1"), r[0x83])
        return out
    return _cached("wpn:" + name, load)


# ---------------------------------------------------------------- resolvers --
def clean(text, limit=None):
    if text is None:
        return None
    t = re.sub(r"<[^>]{0,60}>", "", text)
    t = re.sub(r"\s+", " ", t).strip()
    t = t.replace("|", "/").replace("`", "'").replace(OPEN_L, "(").replace(CLOSE_R, ")")
    if limit and len(t) > limit:
        t = t[:limit - 1].rstrip() + "…"
    return t


def res_text(key, table="us/global.res"):
    """Text for a numeric id (int) or a key name (str) in one table."""
    t = res_tables()[table]
    h = sacred_hash(str(key))
    return t.get(h)


def res_all(key):
    return collections.OrderedDict((lbl, tbl.get(sacred_hash(str(key))))
                                   for lbl, tbl in res_tables().items())


def type_info(tid):
    tid = int(tid)
    tt = type_table()
    pak = items_pak()
    cls, model = pak.get(tid, (None, None))
    wp = weapon_pak().get(tid)
    disp = vanilla().get(sacred_hash(str(tid)))
    sr = res_tables().get("us/SRglbl.res", {}).get(sacred_hash(str(tid)))
    creature = cls in CREATURE_CLASS if cls is not None else None
    if disp is None:
        rule = "none"
    elif PLACEHOLDER_RE.match(disp):
        rule = "placeholder"
    elif creature:
        rule = "creature"
    elif wp is not None:
        rule = "weapon.pak"
    else:
        rule = "unvalidated"          # FX / record-less: key collides with UI text
    info = collections.OrderedDict()
    info["id"] = tid
    info["symbol"] = tt.get(tid)
    info["class_byte"] = cls
    info["creature"] = creature
    info["model"] = model or None
    info["display"] = clean(disp)
    info["display_rule"] = rule
    info["display_suppressed"] = rule in ("placeholder", "unvalidated")
    info["weapon_pak_name"] = (wp[1] or None) if wp else None
    info["reborn_display"] = clean(sr) if sr is not None and sr != disp else None
    info["community"] = community_creatures().get(tid)
    info["in_creature_pak"] = tid in creature_pak_ids()
    return info


# Display names that pass the NAMES.md §1.2 rule but are authoring leftovers:
# the Items.pak model (+0x37) contradicts them (AUDIT_2 A2-10.3).
# {type id: model}; the caveat is added only while the model still matches.
LEFTOVER_NAMES = {4998: "S_Brett.grn"}   # TYPE_OBJECT_SCHWARZES_BRETT: "Chest" / Truhe, a board


def type_label(info, expect=None):
    """Annotation text for a type id, or None when the id is not a TYPE row."""
    if info["symbol"] is None:
        return None
    sym = "`%s`" % info["symbol"]
    disp = None if info["display_suppressed"] else info["display"]
    if disp and info["model"] and LEFTOVER_NAMES.get(info["id"]) == info["model"]:
        disp = "%s (authoring leftover; model %s)" % (disp, info["model"])
    if info["class_byte"] in (None, 0):
        lab = sym + " (no Items.pak record)"
    elif info["creature"]:
        lab = disp or sym
    else:
        lab = ("%s · %s" % (disp, sym)) if disp else sym
    if expect == "creature" and not info["creature"]:
        lab += " — not a creature type"
    elif expect == "object" and info["creature"]:
        lab += " — a creature type"
    return lab


def sound_lookup(name):
    n = name if name.upper().startswith("SOUND_FX_") else "SOUND_FX_" + name
    return n, sound_table().get(n.upper())


# ------------------------------------------------------------ corpus sweeps --
def _distinct_blobs(fname, spec="all"):
    seen = {}
    for s in fs.resolve(spec):
        if not s.exists(fname):
            continue
        m = fs.file_md5(s, fname)
        seen.setdefault(m, []).append(s.key)
    return seen


RES_NUM_RE = re.compile(r"(?i)(?<![A-Za-z])res:(\d{1,7})(?!\d)")
RES_NAME_RE = re.compile(r"(?i)(?<![A-Za-z])res:([A-Za-z_][A-Za-z0-9_]*)(?![A-Za-z0-9_+(])")


def sweep(spec="all"):
    """Walk every md5-distinct FunkCode.bin/StartCode.bin once and collect:
    numeric res ids, res:NAME keys, PlaySound names, and op-0x02 values of
    the tags that carry type ids.  First occurrence = (source, file, rec, off)."""
    out = {
        "res_num": collections.OrderedDict(),
        "res_name": collections.OrderedDict(),
        "playsound": collections.OrderedDict(),
        "op02": collections.defaultdict(collections.Counter),
        "blobs": [],
        "records": 0,
    }
    for fname in ("FunkCode.bin", "StartCode.bin"):
        for md5, keys in _distinct_blobs(fname, spec).items():
            src = fs.get(keys[0])
            data = src.read(fname)
            out["blobs"].append((fname, keys[0], md5, len(keys)))
            for rec, (off, tag, size, payload) in enumerate(fd.walk_records(data)):
                out["records"] += 1
                tile = fd.tile_payload(payload, tag=tag)
                n02 = 0
                for op in tile.ops:
                    v = op.value
                    if isinstance(v, str):
                        where = (keys[0], fname, rec, off)
                        for m in RES_NUM_RE.finditer(v):
                            e = out["res_num"].setdefault(int(m.group(1)), {"count": 0, "first": where, "blobs": set()})
                            e["count"] += 1
                            e["blobs"].add(keys[0] + "/" + fname)
                        for m in RES_NAME_RE.finditer(v):
                            if "+" in v[m.end():m.end() + 1]:
                                continue
                            e = out["res_name"].setdefault(m.group(1).upper(), {"name": m.group(1), "count": 0, "first": where, "blobs": set()})
                            e["count"] += 1
                            e["blobs"].add(keys[0] + "/" + fname)
                        if tag == 0x68 and op.op == 0x01 and op is tile.ops[0]:
                            e = out["playsound"].setdefault(v.upper(), {"name": v, "count": 0, "first": where, "blobs": set()})
                            e["count"] += 1
                            e["blobs"].add(keys[0] + "/" + fname)
                    if op.op == 0x02 and isinstance(v, int) and tag in (0x01, 0x08, 0x87, 0x88, 0x89):
                        out["op02"][(tag, min(n02, 2))][v] += 1
                        n02 += 1
    return out


def _json_where(w):
    return {"source": w[0], "file": w[1], "record": w[2], "offset": "0x%06x" % w[3]}


# ------------------------------------------------------- detail-file scanner --
KW_RE = re.compile(r"""(?ix)
    (?<![A-Za-z_-])
    (?P<kw> creature\s+types? | item\s+types? | object\s+types? | obj\s+types?
          | npc\s+types? | creatures? | items? | objects? | types? )
    (?P<gap> \s*[=:]?\s* (?:`\s*)? )
    (?P<b1>\*\*)?
    (?P<num>\d{1,5})
""")
LIST_CONT_RE = re.compile(r"(?P<sep>\s*[/,]\s*)(?P<b1>\*\*)?(?P<num>\d{1,5})")
RES_RE = re.compile(r"(?i)(?<![A-Za-z])res:(?P<num>\d{1,6})")
BAD_FOLLOW = re.compile(r"[\dA-Za-z_.%…]|\s?[-–]\s?\d|\+")   # '1955N', '0x3f', '1.5', ranges
EXCLUDE_WORDS = {"event", "kernel", "sub", "slot", "icon", "marker", "state",
                 "selector", "mode", "bit", "mask", "message", "msg", "opcode",
                 "command", "field", "size", "value", "sound", "subs"}
TYPE_HEADERS = {"type", "obj type", "object type", "item type", "creature type",
                "type id", "npc type"}
RES_HEADERS = {"res", "resnum", "res id"}

Hit = collections.namedtuple("Hit", "line col kind num insert_at mode expect span")


def _code_spans(line):
    spans = []
    i = 0
    while True:
        a = line.find("`", i)
        if a < 0:
            break
        run = len(re.match(r"`+", line[a:]).group(0))
        b = line.find("`" * run, a + run)
        if b < 0:
            break
        spans.append((a, b + run))
        i = b + run
    return spans


ANN_RE = re.compile(OPEN_L + "[^" + CLOSE_R + "]*" + CLOSE_R)
BLOCK_START_RE = re.compile(r"^\s*(?:[*+-]|\d+[.)]|>)\s")


def _mask_ann(line):
    """Blank out existing annotations (same length) so their backticks never
    take part in code-span pairing."""
    return ANN_RE.sub(lambda m: " " * len(m.group(0)), line)


def _paragraph_spans(lines):
    """{line index: [(a, b)]} of inline code spans, paired CommonMark-style
    across the lines of one paragraph (a backtick run pairs with the next run
    of the same length; unpaired runs are literal).  a = -1: the span opened
    on an earlier line; b = None: it closes on a later line.  Paragraphs end at
    blank lines, headings, fences, table rows (each row stands alone) and at
    the start of a list item or quote."""
    out = collections.defaultdict(list)
    para = []

    def flush():
        runs = []
        for li, s in para:
            for m in re.finditer(r"`+", s):
                runs.append((li, m.start(), len(m.group(0))))
        i = 0
        while i < len(runs):
            li, pos, n = runs[i]
            j = next((k for k in range(i + 1, len(runs)) if runs[k][2] == n), None)
            if j is None:
                i += 1
                continue
            lj, pj, _n = runs[j]
            if li == lj:
                out[li].append((pos, pj + n))
            else:
                out[li].append((pos, None))
                for mid in range(li + 1, lj):
                    out[mid].append((-1, None))
                out[lj].append((-1, pj + n))
            i = j + 1
        del para[:]

    infence = False
    for li, line in enumerate(lines):
        s = line.strip()
        if s.startswith("```") or s.startswith("~~~"):
            flush()
            infence = not infence
            continue
        if infence:
            continue
        if not s or s.startswith("#"):
            flush()
            continue
        if s.startswith("|"):
            flush()
            para.append((li, _mask_ann(line)))
            flush()
            continue
        if BLOCK_START_RE.match(line):
            flush()
        para.append((li, _mask_ann(line)))
    flush()
    return out


def _in_span(spans, pos, n=None):
    for a, b in spans:
        a0 = 0 if a < 0 else a
        b0 = (n if n is not None else 10 ** 9) if b is None else b
        if a0 <= pos < b0:
            return (a, b)
    return None


def repair_spans(files=None, check=False):
    """Remove the annotations audit_spans() finds inside a code span (the
    inserted ' ⟨…⟩' text only); a following `annotate` re-places them after a
    span that closes on the same line, or leaves the id raw.  {file: removed}."""
    report = collections.OrderedDict()
    for f, bad in audit_spans(files).items():
        if not bad:
            report[f] = 0
            continue
        path = os.path.join(KB, f)
        before = open(path, encoding="utf-8", newline="").read()
        lines = before.splitlines(keepends=True)
        by_line = collections.defaultdict(list)
        for ln, col, txt in bad:
            by_line[ln - 1].append((col, txt))
        n = 0
        for li, items in by_line.items():
            s = lines[li]
            for col, txt in sorted(items, key=lambda x: -x[0]):
                start = col - 1 if col > 0 and s[col - 1] == " " else col
                if s[col:col + len(txt)] == txt:
                    s = s[:start] + s[col + len(txt):]
                    n += 1
            lines[li] = s
        report[f] = n
        if check or not n:
            continue
        if open(path, encoding="utf-8", newline="").read() != before:
            report[f] = -1           # concurrent edit: skipped
            continue
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write("".join(lines))
    return report


def audit_spans(files=None):
    """Annotations that sit inside an inline code span (would render as code
    and can break the span).  {file: [(line no, column, annotation)]}."""
    out = collections.OrderedDict()
    for f in files or DETAIL_FILES:
        lines = open(os.path.join(KB, f), encoding="utf-8", newline="").read().splitlines(keepends=True)
        sp = _paragraph_spans(lines)
        bad = []
        for li, line in enumerate(lines):
            for m in ANN_RE.finditer(line):
                if _in_span(sp.get(li, []), m.start(), len(line)):
                    bad.append((li + 1, m.start(), m.group(0)))
        out[f] = bad
    return out


def _followed_ok(line, end):
    return not BAD_FOLLOW.match(line[end:end + 3])


def _expect_for_kw(kw):
    k = kw.lower()
    if k.startswith(("creature", "npc")):
        return "creature"
    if k.startswith(("item", "object", "obj")):
        return "object"
    return None


def _norm_header(cell):
    return re.sub(r"\s+", " ", re.sub(r"[`*_]", "", cell)).strip().lower()


def _split_cells(line):
    """[(start, end)] of the cell contents of a markdown table row."""
    pos = [m.start() for m in re.finditer(r"(?<!\\)\|", line)]
    return [(pos[i] + 1, pos[i + 1]) for i in range(len(pos) - 1)]


def scan_lines(lines):
    """Yield Hit()s for one file (list of lines, with newlines)."""
    tt = type_table()
    span_map = _paragraph_spans(lines)
    infence = False
    table = None           # dict(cols={idx: (kind, expect)}) for the current table
    prev = None
    for ln, line in enumerate(lines):
        s = line.lstrip()
        if s.startswith("```") or s.startswith("~~~"):
            infence = not infence
            prev = line
            continue
        if infence or s.startswith("#"):
            prev = line
            table = None if not s.startswith("|") else table
            continue
        spans = span_map.get(ln, [])
        nline = len(line)
        is_row = s.startswith("|")
        # ---- table bookkeeping
        if is_row and re.match(r"^\s*\|[\s:|-]+\|?\s*$", line):
            if prev is not None and prev.lstrip().startswith("|"):
                heads = [_norm_header(prev[a:b]) for a, b in _split_cells(prev)]
                cols = {}
                for i, h in enumerate(heads):
                    if h in TYPE_HEADERS or re.match(r"^(giver|target) type( @.*)?$", h):
                        e = "creature" if (h.startswith(("creature", "npc", "giver", "target"))) else (
                            "object" if h.startswith(("obj", "object", "item")) else None)
                        cols[i] = ("type", e)
                    elif h in RES_HEADERS:
                        cols[i] = ("res", None)
                table = {"cols": cols, "rows": []} if cols else None
            prev = line
            continue
        if not is_row:
            table = None
        # text inside an existing annotation is never a source of new hits
        ann = [(m.start(), m.end()) for m in re.finditer(OPEN_L + "[^" + CLOSE_R + "]*" + CLOSE_R, line)]
        hits = []
        # ---- res:N anywhere
        for m in RES_RE.finditer(line):
            end = m.end("num")
            if not _followed_ok(line, end):
                continue
            hits.append(Hit(ln, m.start(), "res", int(m.group("num")), end, "inline", None, _in_span(spans, m.start(), nline)))
        # ---- keyword + number
        for m in KW_RE.finditer(line):
            left = line[max(0, m.start() - 30):m.start()]
            words = re.findall(r"[A-Za-z]+", left.lower())[-2:]
            if EXCLUDE_WORDS.intersection(words):
                continue
            expect = _expect_for_kw(m.group("kw"))
            seq = [(m.group("num"), m.start("num"), m.end("num"), m.group("b1"))]
            if m.group("kw").lower().rstrip().endswith("s"):
                p = m.end("num")
                if m.group("b1") and line[p:p + 2] == "**":
                    p += 2
                while True:
                    c = LIST_CONT_RE.match(line, p)
                    if not c:
                        break
                    seq.append((c.group("num"), c.start("num"), c.end("num"), c.group("b1")))
                    p = c.end("num")
                    if c.group("b1") and line[p:p + 2] == "**":
                        p += 2
            # a range anywhere in the sequence -> skip the whole thing
            if any(not _followed_ok(line, e if not (b and line[e:e + 2] == "**") else e + 2)
                   and re.match(r"\s?[-–]\s?\d", line[(e + 2 if (b and line[e:e + 2] == "**") else e):][:4])
                   for _n, _a, e, b in seq):
                continue
            for num, a, e, b1 in seq:
                n = int(num)
                if n not in tt:
                    continue
                if n < 10 and expect != "creature":
                    continue
                ins = e + 2 if (b1 and line[e:e + 2] == "**") else e
                if not _followed_ok(line, ins) and not line[ins:ins + 1] in ("*",):
                    continue
                if BAD_FOLLOW.match(line[e:e + 3]):
                    continue
                hits.append(Hit(ln, a, "type", n, ins, "inline", expect, _in_span(spans, a, nline)))
        # ---- column mode
        if is_row and table:
            cells = _split_cells(line)
            for ci, (a, b) in enumerate(cells):
                if ci not in table["cols"]:
                    continue
                kind, expect = table["cols"][ci]
                txt = line[a:b]
                m = re.match(r"\s*(\*\*)?(\d{1,6})(\*\*)?", txt)
                if not m:
                    continue
                seq = [(int(m.group(2)), a + m.start(2), a + m.end(2), m.group(1))]
                p = m.end(2) + (2 if m.group(1) and txt[m.end(2):m.end(2) + 2] == "**" else 0)
                while True:
                    c = LIST_CONT_RE.match(txt, p)
                    if not c:
                        break
                    seq.append((int(c.group("num")), a + c.start("num"), a + c.end("num"), c.group("b1")))
                    p = c.end("num") + (2 if c.group("b1") and txt[c.end("num"):c.end("num") + 2] == "**" else 0)
                if re.match(r"\s?[-–…]\s?\d", txt[p:p + 4]) or re.match(r"[\dA-Za-z_.]", txt[p:p + 1]):
                    continue
                for n, s0, e0, b1 in seq:
                    if kind == "type" and n not in tt:
                        continue
                    ins = e0 + 2 if (b1 and line[e0:e0 + 2] == "**") else e0
                    hits.append(Hit(ln, s0, kind, n, ins, "column", expect, None))
            if table is not None:
                table["rows"].append(ln)
        prev = line
        hits = [h for h in hits if not any(a <= h.col < b for a, b in ann)]
        # de-duplicate by insertion point (column + keyword can meet)
        seen = set()
        for h in sorted(hits, key=lambda h: (h.insert_at, h.mode != "inline")):
            if (h.insert_at, h.num) in seen:
                continue
            seen.add((h.insert_at, h.num))
            yield h


def label_for(hit):
    if hit.kind == "res":
        t = clean(res_text(hit.num), 70)
        return ('"%s"' % t) if t else None
    return type_label(type_info(hit.num), hit.expect)


def _name_present(lab, line):
    """True when the resolved name already stands in the line as whole words
    (so the annotation would only repeat it).  Flagged labels are never skipped."""
    if lab.endswith(" type"):
        return False
    plain = re.sub(r"[`\"]", "", lab.split(" · ")[0]).split(" — ")[0].strip()
    if len(plain) < 2 or plain.startswith("TYPE_"):
        return False
    return re.search(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])" % re.escape(plain), line, re.I) is not None


def plan_file(path):
    """Compute the edits for one detail file: [(line, insert_at, text, hit)] plus
    per-reason skip counters.  Pure: does not write.  Line endings are kept
    byte-for-byte (newline='')."""
    lines = open(path, encoding="utf-8", newline="").read().splitlines(keepends=True)
    hits = list(scan_lines(lines))
    edits = []
    stats = collections.Counter()
    by_span = collections.defaultdict(list)
    for h in hits:
        lab = label_for(h)
        line = lines[h.line]
        if lab is None:
            stats["unresolved_" + h.kind] += 1
            continue
        if h.span is not None and h.mode == "inline":
            if h.span[1] is None:           # the span closes on a later line: leave it
                stats["open_span_skipped"] += 1
                continue
            by_span[(h.line, h.span)].append((h, lab))
            continue
        after = line[h.insert_at:h.insert_at + 2]
        if after.startswith(MARK) or after.startswith(OPEN_L):
            stats["already"] += 1
            continue
        if _name_present(lab, line):
            stats["name_present"] += 1
            continue
        edits.append((h.line, h.insert_at, MARK + lab + CLOSE_R, h))
        stats[h.kind + "_" + h.mode] += 1
    for (ln, (a, b)), items in by_span.items():
        line = lines[ln]
        if line[b:b + 2].startswith(MARK) or line[b:b + 1] == OPEN_L:
            stats["already"] += len(items)
            continue
        inner = line[max(a, 0):b].strip("`").strip()
        keep = []
        for h, lab in items:
            if _name_present(lab, line):
                stats["name_present"] += 1
                continue
            keep.append((h, lab))
        if not keep:
            continue
        if len(keep) == 1 and re.fullmatch(r"(?i)(cpos:)?res:\d+", inner):
            txt = keep[0][1]
        else:
            parts, seen = [], set()
            for h, lab in keep:
                k = ("res:%d" % h.num) if h.kind == "res" else str(h.num)
                if k in seen:
                    continue
                seen.add(k)
                parts.append("%s = %s" % (k, lab))
            txt = "; ".join(parts)
        edits.append((ln, b, MARK + txt + CLOSE_R, keep[0][0]))
        for h, _l in keep:
            stats[h.kind + "_span"] += 1
    return lines, edits, stats


def apply_edits(lines, edits):
    by_line = collections.defaultdict(list)
    for ln, pos, txt, _h in edits:
        by_line[ln].append((pos, txt))
    out = list(lines)
    for ln, items in by_line.items():
        s = out[ln]
        for pos, txt in sorted(items, key=lambda x: -x[0]):
            s = s[:pos] + txt + s[pos:]
        out[ln] = s
    return out


def annotate(files=None, check=False, verbose=False):
    files = files or DETAIL_FILES
    report = collections.OrderedDict()
    for f in files:
        path = os.path.join(KB, f)
        before = open(path, encoding="utf-8", newline="").read()
        lines, edits, stats = plan_file(path)
        report[f] = (len(edits), stats)
        if verbose:
            for ln, pos, txt, h in edits:
                print("%s:%d  %-5s %-6s %6d  ...%s|%s" % (f, ln + 1, h.kind, h.mode, h.num,
                      lines[ln][max(0, pos - 40):pos].replace("\n", ""), txt))
        if check or not edits:
            continue
        new = "".join(apply_edits(lines, edits))
        # never clobber a concurrent edit: re-read and compare just before writing
        if open(path, encoding="utf-8", newline="").read() != before:
            report[f] = (0, collections.Counter({"concurrent_edit_skipped": 1}))
            continue
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(new)
    return report


def detail_ids():
    """Every id the scanner resolves in the nine files: {('type'|'res', id): set(files)}."""
    out = collections.defaultdict(set)
    for f in DETAIL_FILES:
        path = os.path.join(KB, f)
        lines = open(path, encoding="utf-8", newline="").read().splitlines(keepends=True)
        for h in scan_lines(lines):
            out[(h.kind, h.num)].add(f)
        # already-annotated text is not re-scanned by scan_lines' callers, but the
        # scanner still yields those ids, so the list is stable across runs.
    return out


# ------------------------------------------------------------ question sets --
QUESTION_IDS = collections.OrderedDict([
    ("Q7", {"type": [5379, 5394, 5418, 5555, 5558, 5578, 5639, 5770,
                     5381, 5554, 5562, 5566, 5570, 5609]}),
    ("Q8", {"type": [689, 86, 319, 134, 679, 132, 312, 322]}),
    ("Q26", {"res": [1024, 1037, 1038, 17643, 17637, 17631]}),
    ("Q29", {"type": [94, 171, 182, 185, 214, 215, 222, 223, 224, 250, 251, 252, 271,
                      328, 348, 355, 356, 357, 361, 362, 363, 364, 365, 366, 367, 368,
                      514, 853, 854, 918, 1160, 1161, 1175, 1176, 1177, 1178, 1179,
                      1180, 1397, 1712, 1732, 1804, 1858, 2276, 4611, 4754, 4773,
                      4792, 4932, 4938, 4939, 4940, 4941, 4942, 4943, 4944, 4945,
                      4946, 6100]}),
    ("Q46", {"type": [1330]}),
    ("Q61", {"res": [1024, 1037, 1038, 17643, 17750]}),
    ("Q68", {"type": [1329, 1334, 1352, 1367, 1376, 4098, 4099, 4609, 4931, 5165,
                      5218, 6001, 6005, 6021, 6022, 6023]}),
    ("Q75", {"res": [1024, 1037, 1038]}),
    ("Q76", {"type": [260, 269, 275, 288, 307, 312, 313, 334, 563, 583, 597, 598, 600]
             + list(range(642, 705))
             + [1322, 1329, 1334, 1754, 4097, 4098, 4867, 5120, 5162, 5165, 6001,
                6002, 6023, 6025]}),
    ("Q86", {"type": [1331, 1353, 1355, 1366, 1369, 1370, 1372, 4097,
                      1327, 1722, 4864, 5126, 6002, 6019, 6022]}),
    ("Q91", {"type": [538]}),
])
MISSING_KEYS = ["HQ_6_2_1_Log_preQuest", "NQ_7607_LOG_HEADER",
                "HQ_0_1_NPC_KOMMENTAR_ALCALATA2"]


# ------------------------------------------------------------------- output --
def md_escape(s):
    return (s or "").replace("|", "/")


def type_row(tid, where=None):
    i = type_info(tid)
    notes = []
    if i["display_rule"] == "unvalidated":
        notes.append("unvalidated key text (FX keys largely collide with UI strings): %s" % md_escape(i["display"]))
    elif i["display_rule"] == "placeholder":
        notes.append("placeholder text %s" % md_escape(i["display"]))
    if i["reborn_display"]:
        notes.append("ReBorn SRglbl.res only: %s" % md_escape(i["reborn_display"]))
    if i["community"] and i["display"] and i["community"].strip().lower() != i["display"].strip().lower():
        notes.append("creatures.lua: %s" % md_escape(i["community"]))
    if i["community"] and not i["display"]:
        notes.append("creatures.lua: %s" % md_escape(i["community"]))
    if i["creature"] and not i["in_creature_pak"]:
        notes.append("not in Creature.pak")
    cls = i["class_byte"]
    kind = "creature" if i["creature"] else ("-" if cls in (None, 0) else "object")
    disp = i["display"] if not i["display_suppressed"] else None
    wpn = i["weapon_pak_name"]
    row = "| %d | `%s` | %s | %s | %s | %s | %s |" % (
        tid, i["symbol"] or "(not a TYPE id)", "-" if cls is None else cls, kind,
        md_escape(disp) if disp else "**none**", md_escape(wpn) if wpn else "-",
        "; ".join(notes) or "")
    if where is not None:
        row = row + " %s |" % where
    return row


TYPE_HEAD = ("| id | TYPE_* symbol (exe) | class byte | kind | vanilla display (us/global.res) | Weapon.pak name (de) | notes |\n"
             "|---:|---|---:|---|---|---|---|")


def block_questions():
    out = []
    for q, sets in QUESTION_IDS.items():
        out.append("#### %s\n" % q)
        if "type" in sets:
            out.append(TYPE_HEAD)
            for t in sets["type"]:
                out.append(type_row(t))
            out.append("")
        if "res" in sets:
            out.append("| res id | key hashed | us/global.res | de | fr | ru |\n|---:|---|---|---|---|---|")
            for r in sets["res"]:
                a = res_all(r)
                out.append("| %d | `\"%d\"` -> %d | %s | %s | %s | %s |" % (
                    r, r, sacred_hash(str(r)), md_escape(clean(a["us/global.res"])),
                    md_escape(clean(a.get("de/SRglbl.res"))), md_escape(clean(a.get("fr/SRglbl.res"))),
                    md_escape(clean(a.get("ru/SRglbl.res")))))
            out.append("")
    return "\n".join(out)


def block_detail_types(ids):
    rows = ["| id | TYPE_* symbol (exe) | class byte | kind | vanilla display (us/global.res) | Weapon.pak name (de) | notes | files |\n"
            "|---:|---|---:|---|---|---|---|---|"]
    for (kind, n), files in sorted(ids.items(), key=lambda x: x[0][1]):
        if kind != "type":
            continue
        rows.append(type_row(n, ", ".join(sorted(f.replace(".md", "") for f in files))))
    return "\n".join(rows)


def block_detail_res(ids):
    rows = ["| res id | us/global.res text | files |", "|---:|---|---|"]
    n_miss = 0
    for (kind, n), files in sorted(ids.items(), key=lambda x: x[0][1]):
        if kind != "res":
            continue
        t = clean(res_text(n), 90)
        if t is None:
            n_miss += 1
        rows.append("| %d | %s | %s |" % (n, md_escape(t) if t else "**no entry**",
                                          ", ".join(sorted(f.replace(".md", "") for f in files))))
    return "\n".join(rows)


def block_sweep(sw):
    num = sw["res_num"]
    miss_num = [n for n in num if res_text(n) is None]
    names = sw["res_name"]
    miss_names = [e for k, e in names.items() if res_text(e["name"]) is None]
    ps = sw["playsound"]
    miss_ps = [e for k, e in ps.items() if sound_lookup(e["name"])[1] is None]
    L = []
    L.append("Blobs walked (md5-distinct): %d (%s); records: %d." % (
        len(sw["blobs"]), ", ".join("%s %s x%d" % (b[1], b[0], b[3]) for b in sw["blobs"]), sw["records"]))
    L.append("")
    L.append("| sweep | distinct | resolved | unresolved |\n|---|---:|---:|---:|")
    L.append("| numeric `res:N` ids (all string operands) | %d | %d | %d |" % (len(num), len(num) - len(miss_num), len(miss_num)))
    L.append("| named `res:KEY` keys (no `+VAR`) | %d | %d | %d |" % (len(names), len(names) - len(miss_names), len(miss_names)))
    L.append("| tag-0x68 PlaySound names vs exe sound table | %d | %d | %d |" % (len(ps), len(ps) - len(miss_ps), len(miss_ps)))
    L.append("")
    if miss_num:
        L.append("Numeric ids with no us/global.res text: " + ", ".join(
            "%d (%s #%d @%s)" % (n, num[n]["first"][0], num[n]["first"][2], "0x%06x" % num[n]["first"][3]) for n in sorted(miss_num)))
        L.append("")
    L.append("Named keys with no text in any of the 10 tables:\n")
    L.append("| key | first reference | refs | blobs |\n|---|---|---:|---:|")
    for e in miss_names:
        in_any = any(t.get(sacred_hash(e["name"])) for t in res_tables().values())
        w = e["first"]
        L.append("| `%s`%s | %s %s #%d @0x%06x | %d | %d |" % (
            e["name"], "" if not in_any else " (present in another table!)", w[0], w[1], w[2], w[3], e["count"], len(e["blobs"])))
    L.append("")
    L.append("PlaySound names that miss the exe sound table (after the engine's `SOUND_FX_` prefix):\n")
    L.append("| name in script | looked up as | first reference | refs | nearest table entry |\n|---|---|---|---:|---|")
    snames = list(sound_table().values())
    import difflib
    for e in sorted(miss_ps, key=lambda e: e["name"].upper()):
        n, _ = sound_lookup(e["name"])
        near = difflib.get_close_matches(n.upper(), [x[1].upper() for x in snames], n=1, cutoff=0.85)
        nearest = ""
        if near:
            sid, nm = sound_table()[near[0]]
            nearest = "`%s` (%d)" % (nm, sid)
        w = e["first"]
        L.append("| `%s` | `%s` | %s %s #%d @0x%06x | %d | %s |" % (e["name"], n, w[0], w[1], w[2], w[3], e["count"], nearest))
    return "\n".join(L)


def block_op02(sw):
    tags = {0x01: "CreateNPC", 0x08: "CreateOBJ", 0x87: "SetOnKill", 0x88: "SetOnCollect", 0x89: "SetDrop"}
    L = ["| tag | op-0x02 position | occurrences | distinct values | TYPE ids | creature-class | object-class | top symbol prefixes |",
         "|---|---|---:|---:|---:|---:|---:|---|"]
    tt = type_table()
    for (tag, posn), cnt in sorted(sw["op02"].items()):
        tot = sum(cnt.values())
        vals = list(cnt)
        in_tt = [v for v in vals if v in tt]
        cr = sum(cnt[v] for v in in_tt if type_info(v)["creature"])
        ob = sum(cnt[v] for v in in_tt if not type_info(v)["creature"] and (type_info(v)["class_byte"] or 0) != 0)
        pref = collections.Counter()
        for v in in_tt:
            p = tt[v].split("_")
            pref["_".join(p[:2])] += cnt[v]
        L.append("| 0x%02x %s | %s | %d | %d | %d | %d | %d | %s |" % (
            tag, tags[tag], ("1st", "2nd", "3rd+")[posn], tot, len(vals), sum(cnt[v] for v in in_tt), cr, ob,
            ", ".join("%s %d" % kv for kv in pref.most_common(4))))
    return "\n".join(L)


SUBID_RE = re.compile(r"(?i)\bsub(?:-ids?|s)?\s+\**(\d{3,5}(?:\s*/\s*\d{3,5})*)")


def block_subids():
    """Every value the detail files call a CreateNPC 'sub-id' / 'sub', resolved.
    (Read-only; these are not annotated in place -- no keyword names them as
    type ids in the prose.)"""
    found = collections.defaultdict(set)
    for f in DETAIL_FILES:
        txt = open(os.path.join(KB, f), encoding="utf-8", newline="").read()
        for m in SUBID_RE.finditer(txt):
            for n in re.findall(r"\d{3,5}", m.group(1)):
                found[int(n)].add(f.replace(".md", ""))
    rows = ["| id | TYPE_* symbol (exe) | class byte | kind | vanilla display (us/global.res) | Weapon.pak name (de) | notes | files |",
            "|---:|---|---:|---|---|---|---|---|"]
    for n in sorted(found):
        rows.append(type_row(n, ", ".join(sorted(found[n]))))
    return "\n".join(rows)


def block_annotations():
    """Per-file count of the ⟨…⟩ annotations currently in the nine files."""
    rows = ["| file | annotations | on res ids | on type ids | of which symbol-only |",
            "|---|---:|---:|---:|---:|"]
    tot = [0, 0, 0, 0]
    for f in DETAIL_FILES:
        txt = open(os.path.join(KB, f), encoding="utf-8", newline="").read()
        anns = re.findall(OPEN_L + "([^" + CLOSE_R + "]*)" + CLOSE_R, txt)
        nres = sum(1 for a in anns if a.startswith('"') or a.startswith("res:"))
        nsym = sum(1 for a in anns if re.match(r"(\d+ = )?`TYPE_", a))
        row = [len(anns), nres, len(anns) - nres, nsym]
        tot = [x + y for x, y in zip(tot, row)]
        rows.append("| %s | %d | %d | %d | %d |" % ((f,) + tuple(row)))
    rows.append("| **total** | %d | %d | %d | %d |" % tuple(tot))
    return "\n".join(rows)


BLOCK_RE = re.compile(r"(<!-- BEGIN GENERATED (?P<name>[a-z0-9_]+) -->\n)(?P<body>.*?)(<!-- END GENERATED (?P=name) -->)", re.S)


def build(write=True):
    sw = sweep("all")
    ids = detail_ids()
    blocks = {
        "questions": block_questions(),
        "detail_types": block_detail_types(ids),
        "detail_res": block_detail_res(ids),
        "sweep": block_sweep(sw),
        "op02": block_op02(sw),
        "annotations": block_annotations(),
        "subids": block_subids(),
    }
    tt = type_table()
    # ---- names.json
    types = collections.OrderedDict()
    for tid in sorted(tt):
        i = type_info(tid)
        e = collections.OrderedDict([("symbol", i["symbol"]), ("class_byte", i["class_byte"]),
                                     ("model", i["model"]), ("display", i["display"]),
                                     ("display_rule", i["display_rule"])])
        if i["weapon_pak_name"]:
            e["weapon_pak_name_de"] = i["weapon_pak_name"]
        if i["reborn_display"]:
            e["reborn_display"] = i["reborn_display"]
        if i["community"]:
            e["community"] = i["community"]
        if i["creature"]:
            e["in_creature_pak"] = i["in_creature_pak"]
        types[str(tid)] = e
    res_num = collections.OrderedDict()
    for n in sorted(sw["res_num"]):
        e = sw["res_num"][n]
        res_num[str(n)] = collections.OrderedDict([
            ("text", clean(res_text(n))), ("refs", e["count"]), ("first", _json_where(e["first"]))])
    miss_names = [collections.OrderedDict([("key", e["name"]), ("refs", e["count"]),
                                           ("blobs", len(e["blobs"])), ("first", _json_where(e["first"]))])
                  for k, e in sw["res_name"].items() if res_text(e["name"]) is None]
    ps_missing = []
    for k, e in sw["playsound"].items():
        n, hit = sound_lookup(e["name"])
        if hit is None:
            ps_missing.append(collections.OrderedDict([("name", e["name"]), ("lookup", n), ("refs", e["count"]),
                                                       ("first", _json_where(e["first"]))]))
    detail = collections.OrderedDict()
    for (kind, n), files in sorted(ids.items(), key=lambda x: (x[0][0], x[0][1])):
        detail.setdefault(kind, collections.OrderedDict())[str(n)] = sorted(files)
    j = collections.OrderedDict()
    j["generated_by"] = "sdk/re/py/quest_names.py build"
    j["generated"] = datetime.date.today().isoformat()
    j["inputs"] = collections.OrderedDict([
        ("sdk/Sacred_decrypted.exe", _md5(EXE)),
        ("pak/Items.pak", _md5(os.path.join(PAK, "Items.pak"))),
        ("pak/Creature.pak", _md5(os.path.join(PAK, "Creature.pak"))),
        ("scripts/us/global.res", _md5(os.path.join(SCRIPTS, "us", "global.res"))),
    ])
    j["rules"] = collections.OrderedDict([
        ("res_numeric", "text(res:N) = us/global.res[sacred_hash(str(N))] -- FUN_00472bc0:1743-1791 -> FUN_006726f0 -> FUN_0080e780 (decimal) -> FUN_0080eaf0"),
        ("type_symbol", "exe TYPE_* table VA 0x008EC328 stride 0x44 id@+0 name@+4, 5624 rows (FUN_0043cd90:45-53)"),
        ("type_def", "pak/Items.pak record[id]: +0x2E class byte (creature = 2/3/0x20, FUN_00426610), +0x37 model"),
        ("type_display", "us/global.res[sacred_hash(str(id))], trusted for creature-class ids (443/448 vs creatures.lua) "
                         "and for ids with a pak/Weapon.pak record (German designer name @+38 = the source text); "
                         "display_rule 'unvalidated' (FX class 12, record-less class 1) = no validation; these keys "
                         "largely collide with UI strings (853 'Greed', 1160 'Weapon Bonus'), do not use"),
        ("playsound", "FUN_004a9730 prefixes SOUND_FX_; FUN_00676170 looks it up in the exe table VA 0x00964870"),
    ])
    j["inputs"]["pak/Weapon.pak"] = _md5(os.path.join(PAK, "Weapon.pak"))
    j["types"] = types
    j["res_numeric_in_corpus"] = res_num
    j["text_keys_missing_everywhere"] = miss_names
    j["playsound_missing"] = ps_missing
    j["detail_file_ids"] = detail
    j["questions"] = collections.OrderedDict(
        (q, collections.OrderedDict(
            [(k, collections.OrderedDict((str(v), (type_info(v) if k == "type" else
                                                   collections.OrderedDict(res_all(v))))
                                         for v in vals)) for k, vals in sets.items()]))
        for q, sets in QUESTION_IDS.items())
    j["missing_keys_asked"] = collections.OrderedDict(
        (k, collections.OrderedDict((lbl, t.get(sacred_hash(k)) is not None) for lbl, t in res_tables().items()))
        for k in MISSING_KEYS)
    if write:
        with open(os.path.join(KB, "names.json"), "w", encoding="utf-8", newline="\n") as fh:
            json.dump(j, fh, ensure_ascii=False, indent=1)
            fh.write("\n")
        p = os.path.join(KB, "NAMES.md")
        if os.path.isfile(p):
            txt = open(p, encoding="utf-8").read()
            def sub(m):
                name = m.group("name")
                if name not in blocks:
                    return m.group(0)
                return m.group(1) + blocks[name].rstrip("\n") + "\n" + m.group(4)
            new = BLOCK_RE.sub(sub, txt)
            if new != txt:
                with open(p, "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(new)
    return j, blocks, sw


# --------------------------------------------------------------- selftest --
def selftest():
    ok = 0
    fails = []

    def chk(name, cond):
        nonlocal ok
        if cond:
            ok += 1
        else:
            fails.append(name)
    chk("exe string res: @0x94d5b8", exe_cstr(0x0094D5B8) == "res:")
    chk("exe string RES:%d @0x94f434", exe_cstr(0x0094F434) == "RES:%d")
    chk("exe string SOUND_FX_ @0x94f680", exe_cstr(0x0094F680) == "SOUND_FX_")
    tt = type_table()
    chk("TYPE table rows", len(tt) == 5624)
    chk("TYPE[0] = TYPE_INVALID", tt.get(0) == "TYPE_INVALID")
    chk("TYPE[8191] = TYPE_LAST_ITEM", tt.get(8191) == "TYPE_LAST_ITEM")
    chk("TYPE[689] symbol", tt.get(689) == "TYPE_NPC_WARRIOR_BLOND")
    pak = items_pak()
    chk("Items.pak 32768 records", len(pak) == 32768)
    npc = [t for t, n in tt.items() if n.startswith("TYPE_NPC_")]
    chk("every TYPE_NPC_* has class byte 3", all(pak[t][0] == 3 for t in npc))
    chk("creature-class rows = NPC + NATURE", all(tt[t].split("_")[1] in ("NPC", "NATURE")
                                                  for t in tt if pak[t][0] in CREATURE_CLASS))
    chk("res 1024 = OK", res_text(1024) == "OK")
    chk("res 1037 = Accept", res_text(1037) == "Accept")
    chk("res 1038 = Reject", res_text(1038) == "Reject")
    chk("res 17643 = Quest Completed", res_text(17643) == "Quest Completed")
    chk("hash('1024')", sacred_hash("1024") == 71320567)
    chk("type 689 display Warrior", type_info(689)["display"] == "Warrior")
    chk("type 538 = Black Bear", type_info(538)["display"] == "Black Bear")
    chk("type 1330 has no text", type_info(1330)["display"] is None)
    cc = community_creatures()
    both = [t for t in cc if t in tt and type_info(t)["display"]]
    agree = sum(1 for t in both if cc[t].strip().lower() == type_info(t)["display"].strip().lower())
    chk("community agreement 443/448", (agree, len(both)) == (443, 448))
    for k in MISSING_KEYS:
        chk("missing everywhere: " + k, not any(t.get(sacred_hash(k)) for t in res_tables().values()))
    s = sound_table()
    chk("sound ALCALATA1 = 20500", s.get("SOUND_FX_HQ_0_1_NPC_KOMMENTAR_ALCALATA1", (0,))[0] == 20500)
    chk("sound ALCALATA2 absent", "SOUND_FX_HQ_0_1_NPC_KOMMENTAR_ALCALATA2" not in s)
    chk("sound ALCATATA2 = 20501", s.get("SOUND_FX_HQ_0_1_NPC_KOMMENTAR_ALCATATA2", (0,))[0] == 20501)
    wp = weapon_pak()
    chk("Weapon.pak 4883 records", len(wp) == 4883)
    chk("Weapon.pak ids are all TYPE ids", all(t in tt for t in wp))
    chk("no creature-class id in Weapon.pak", not any(pak.get(t, (0,))[0] in CREATURE_CLASS for t in wp))
    chk("no FX (class 12) id in Weapon.pak", not any(pak[t][0] == 12 for t in tt if t in wp))
    chk("every non-FX object-class TYPE id outside class 1 has a Weapon.pak record "
        "(bar the TYPE_INVALID / TYPE_LAST_ITEM sentinels)",
        sorted(t for t in tt if pak[t][0] not in CREATURE_CLASS + (1, 12) and t not in wp) == [0, 8191])
    chk("type 853 (FX) display suppressed", type_info(853)["display_rule"] == "unvalidated")
    chk("type 1207 display via Weapon.pak", type_info(1207)["display_rule"] == "weapon.pak"
        and type_info(1207)["display"] == "Elohinir's Shiny Tower Shield")
    chk("type 5705 placeholder", type_info(5705)["display_rule"] == "placeholder")
    # annotation: idempotent on a synthetic document
    doc = ["kill 5× type 190 and pick up item 1376; captions `res:1024`.\n",
           "| class | obj type | record |\n", "|---|---|---|\n", "| MAGICIAN | 5379 | `#1` |\n",
           "kernel event with `type = 6` and bit 9 of type 0x0b.\n",
           "compass `QuestKompassOBJ('res:17088', q10)` and `InfoPlayer(0, res:17643)`.\n"]
    import tempfile
    tmp = os.path.join(tempfile.gettempdir(), "quest_names_selftest.md")
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write("".join(doc))
    lines, edits, _st = plan_file(tmp)
    once = "".join(apply_edits(lines, edits))
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(once)
    lines2, edits2, st2 = plan_file(tmp)
    chk("annotate: 6 edits on synthetic doc", len(edits) == 6)
    chk("annotate: grouped span form", "q10)` ⟨res:17088 = \"Aruka, Orc Strangler of Aish-Jadar\"⟩" in once)
    chk("annotate: idempotent", not edits2)
    chk("annotate: event context untouched", "type = 6`" in once and OPEN_L not in once.splitlines()[4])
    chk("annotate: res caption", '`res:1024` ⟨"OK"⟩' in once)
    os.remove(tmp)
    print("selftest: %d/%d pass" % (ok, ok + len(fails)))
    for f in fails:
        print("  FAIL", f)
    return not fails


# -------------------------------------------------------------------- CLI ---
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    for c in ("res", "key", "type", "sound"):
        p = sub.add_parser(c)
        p.add_argument("values", nargs="+")
    sub.add_parser("sweep")
    p = sub.add_parser("scan")
    p.add_argument("--files", default=None)
    p = sub.add_parser("annotate")
    p.add_argument("--check", action="store_true", help="plan only, write nothing")
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--files", default=None)
    sub.add_parser("build")
    sub.add_parser("audit", help="list annotations that sit inside a code span")
    p = sub.add_parser("repair", help="remove the annotations `audit` lists")
    p.add_argument("--check", action="store_true")
    sub.add_parser("selftest")
    a = ap.parse_args(argv)
    if a.cmd in ("res", "key"):
        for v in a.values:
            key = int(v) if a.cmd == "res" else v
            print("== %s  (hash %d)" % (v, sacred_hash(str(key))))
            for lbl, t in res_all(key).items():
                print("   %-16s %r" % (lbl, clean(t, 120)))
    elif a.cmd == "type":
        for v in a.values:
            print(json.dumps(type_info(int(v)), ensure_ascii=False))
    elif a.cmd == "sound":
        for v in a.values:
            n, hit = sound_lookup(v)
            print("%s -> %s -> %s" % (v, n, hit))
    elif a.cmd == "sweep":
        sw = sweep("all")
        print(block_sweep(sw))
        print()
        print(block_op02(sw))
    elif a.cmd == "scan":
        files = a.files.split(",") if a.files else DETAIL_FILES
        rep = annotate(files, check=True, verbose=True)
        for f, (n, st) in rep.items():
            print("%-16s %4d planned  %s" % (f, n, dict(st)))
    elif a.cmd == "annotate":
        files = a.files.split(",") if a.files else DETAIL_FILES
        rep = annotate(files, check=a.check, verbose=a.verbose)
        tot = 0
        for f, (n, st) in rep.items():
            tot += n
            print("%-16s %4d %s  %s" % (f, n, "planned" if a.check else "inserted", dict(st)))
        print("total", tot)
    elif a.cmd == "build":
        j, blocks, sw = build(write=True)
        print("names.json: %d types, %d numeric res ids, %d missing keys, %d PlaySound misses" % (
            len(j["types"]), len(j["res_numeric_in_corpus"]), len(j["text_keys_missing_everywhere"]),
            len(j["playsound_missing"])))
    elif a.cmd == "repair":
        for f, n in repair_spans(check=a.check).items():
            print("%-16s %s" % (f, "concurrent edit, skipped" if n < 0 else "%d removed%s" % (n, " (check)" if a.check else "")))
    elif a.cmd == "audit":
        bad = audit_spans()
        for f, items in bad.items():
            print("%-16s %d annotation(s) inside a code span" % (f, len(items)))
            for ln, col, txt in items:
                print("   %s:%d col %d  %s" % (f, ln, col, txt))
        sys.exit(1 if any(bad.values()) else 0)
    elif a.cmd == "selftest":
        sys.exit(0 if selftest() else 1)
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
