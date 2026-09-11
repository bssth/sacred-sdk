"""vectoren.py -- reader for ``Vectoren.bin``: the section table, the quest
registry and the DQ region-pool table of one script source.

Every claim below cites the exe (``sdk/Sacred_decrypted.exe``, VA = file offset
+ 0x400000; decompiles in ``sdk/re/ghidra/decompiled/<va>_FUN_<va>.c``) or a
measurement this module reproduces with ``--selftest``.

FILE LAYOUT (loader ``FUN_0046f9b0``, Vectoren open at :353, reads :360-545)
===========================================================================

    u32 sectionCount                        :365   (23,495 in base:VAMPIRELADY)
    sectionCount x 0x54   -> DAT_00aab708   :389   the SECTION TABLE
        +0x00 char name[64]      ASCIIZ, zero padded; entry 0 is a null entry
        +0x40 u32  start         absolute byte offset into FunkCode.bin
        +0x44 u32  length        bytes; WorkFunktion FUN_0046ba90:95-96 seeks
                                 +0x40 and reads +0x44 bytes
        +0x48 i32  quest         owning quest id, -1 = global.  FUN_0046ba90
                                 passes it to the walker (:108-110) and refuses
                                 to run the section unless that registry entry
                                 has flag bit 0 (:70-82)
        +0x4c i32  tag4c         0 except on the pool sections: the ToDo number
                                 (``ToDo:-1.<n>`` -> n, 1..123) or the pool reward
                                 key (``Belohnung:<p>.<k>`` -> k = 0x80000000 |
                                 p<<16 | slot).  Measured: 1,958 non-zero, all
                                 on ToDo:/Belohnung:/Strafe: sections.
        +0x50 u32  rt50          0 in every file.  Runtime "deleted" byte:
                                 DeleteFunktionBlock (tag 0x0e, walker
                                 FUN_00475680:592/:598) and a walker return of 2
                                 (FUN_0046ba90:118-120) set it to 1; WorkFunktion
                                 skips a section whose byte is set (:66-69).
    u32 questCount                          :395   (601 in base:VAMPIRELADY)
    questCount x 0x124    -> DAT_00aabf18   :419   the QUEST REGISTRY
        +0x000 i32  qid          quest id (entry 0 is a null entry)
        +0x004 char name[64]     German design name ("MP-Start", ...)
        +0x044..+0x103           zero in all 20 sources (measured)
        +0x104 i32  f104         15 x430, 40 x1, 0 x170 (base:VAMPIRELADY)
        +0x108 i32  f108         15, 25 once each
                                 +0x104/+0x108 = QuestCode.bin {start, len} of
                                 the quest's DefQuest body (RE_lifecycle §2.1);
                                 who executes the body is UNKNOWN.
        +0x10c i32  trigger      section index of QIS_Trigger<qid>
        +0x110 i32  on_enter     section index of QIS_OnEnter<qid>
        +0x114 i32  on_setup     section index of QIS_OnSetUp<qid>
        +0x118 i32  on_exit      section index of QIS_OnExit<qid>
        +0x11c i32  on_lose      section index of QIS_OnLose<qid>
                                 (field<->name pairing from FUN_00465690:2293-
                                 2362, which re-resolves them by name; verified
                                 for every non-zero index in all 20 sources)
        +0x120 u32  flags        0 in every file; runtime state:
                                 bit0 set up   (FUN_0046db20 @0x46db69)
                                 bit1 done     (FUN_0046cbe0 @0x46ccf6; tag 0x0a
                                                QuestDone tests &2)
                                 bit2 entered  (FUN_0046c160 tests @0x46c19d and
                                                sets @0x46c5ab right after the
                                                entry fanfare FUN_006770e0(9,
                                                0x40, (qid >= 100) + 1)
                                                @0x46c579-0x46c586;
                                                FUN_0046cbe0 clears @0x46cceb)
                                 bit3 transient during SetUp (0x46db7c/0x46dbd5)
                                 bit31 re-resolve the five indices on load
                                       (FUN_00465690:2287-2366)
    u32 poolCount                           :425   (22 base classes, 0 addon/net)
    poolCount x variable  -> DAT_00aac738   :426-541, runtime stride 0x88
        u32  pid                 +0x00  (1..23, no 14, base:VAMPIRELADY)
        char name[64]            +0x04  ("Region1" ...)
        i32  x5                  +0x44..+0x54  ((0,0,3,0,0) in all 22)
        u32 n; n x u32           -> vector +0x64  ToDo section indices; the
                                    high byte is a runtime counter (HideTmpToDo
                                    tag 0x21, FUN_00490cc0:90-91 adds 0x1000000;
                                    FUN_00471000:159-163 subtracts it)
        u32 n; n x u32           -> vector +0x70  Belohnung (reward) sections
        u32 n; n x u32           -> vector +0x7c  Strafe (penalty) sections
                                    (both run through WorkFunktion by
                                    FUN_00471000:81-90 / :109-117)
        u32 nSlots; nSlots x slot -> vector +0x58, stride 0x24; slot read by
                                    FUN_004b2aa0 (capstone, 0x4b2aa0-0x4b2be8):
            u32 n; n x u32           -> slot +0x18 vector
            i32 x4                   slot +0x00..+0x0c  (pool, 0, k, count)
            8 bytes                  slot +0x10         (49/70/.. or 257, 0)
    EOF -- all 20 sources end exactly here (measured).
    Empty vector slots hold 0 (= null section 0).  Every base-class pool: ToDo
    has 124 slots of which 39 are empty (0, 62-79, 82-100, 114) -> 85 ToDo
    sections; Belohnung/Strafe have 3 slots, slot 0 empty -> 2 each.
    22 x (85+2+2) = 1,958 = exactly the sections with tag4c != 0.

Corrections to the wave-1 knowledge base found while building this:
* README 5/1 says FunkCode[0..410) (``dq_belohnung``) is "exactly the three
  dq_belohnung* variable records".  It is 18 records: 3 SetVar inits, RndVar
  dq_belohnung 1..127 and dq_belohnung_typ 0..3, an IF/ELSEIF ladder on the
  bare predicates 0x96..0x9a setting dq_belohnung_level to 1-10 / 11-20 /
  21-30 / 31-40 / 41-50, ``3b 3e`` (ELSE-nothing), and one Gewinn (0x7d)
  record @342 that takes the three variables by symref (``9f ef be ed fe``).
* README 5/1 lists the section entry's last two u32 as "0 in every entry";
  +0x4c is non-zero on all 1,958 pool sections (see tag4c above).
* AUDIT 10a's "22 entries" IS right -- the table is variable-length, which is
  why no fixed stride tiled.

The earlier "third array: 22 entries, stride unknown" reading (AUDIT 10a) failed
because the entries are variable-length: three count-prefixed u32 vectors plus a
count-prefixed list of variable-length slots.

HOW THE ENGINE GETS FROM A NAME / QUEST ID TO A SECTION (static)
================================================================
* By name, linear: CallFunktion (tag 0x16, FUN_004813d0:115-150), TriggerQuest
  (tag 0x14, FUN_0048f030 @0x48f250-0x48f2c7), SelfTriggerQuest (tag 0x10
  pre-dispatch FUN_00475680:180-184 -> FUN_004915a0:122-150) and
  DeleteFunktionBlock (tag 0x0e, FUN_00475680:559-584) all scan DAT_00aab708 in
  index order with the case-insensitive compare FUN_00859690 plus an explicit
  equal-length test; the name must be 1..63 chars; FIRST match wins; no match =
  silent no-op.  ``section(src, name)`` reproduces exactly that.
* By quest id: TriggerQuest/SetUpQuest/ExitQuest find the registry entry by
  +0x000 (linear), else by +0x004 name, then use the CACHED indices +0x10c..
  +0x11c.  TriggerQuest fills +0x10c lazily via sprintf("QIS_Trigger%d", qid)
  (@0x48f201-0x48f2f1; -1 if the name is missing, 0 if it is not 1..63 chars).
    SetUpQuest (0x15) -> FUN_0048f7f0 -> FUN_0046db20: if bit0 clear, set bits
      0|3, run +0x114 OnSetUp ONCE (then zero it, 0x46dba6), run +0x10c Trigger,
      clear bit3.
    TriggerQuest (0x14) -> FUN_0048f030: needs bit0; run +0x10c; if WorkFunktion
      returns low byte 1 -> FUN_0046c160: skip if bit2 set, else UI/fanfare and
      run +0x110 OnEnter.
    ExitQuest (0x0f) / LoseQuest (0x36) -> FUN_0048ee20 -> FUN_0046cbe0: if bit2
      set and bit1 clear, run +0x118 OnExit (walker pushes 1 for 0x0f,
      @0x4759a1) or +0x11c OnLose (pushes 0 for 0x36, @0x47597d); clear bit2,
      set bit1.  With an EMPTY payload FUN_0048ee20:28-44 acts on the quest that
      owns the running section.
  WorkFunktion FUN_0046ba90 returns low byte 1 when the section runs to its end
  (:100-107) -- so a ZERO-LENGTH QIS_Trigger<id> is an always-pass gate, while a
  MISSING one caches -1 and the quest is never entered.
* Region/sector hooks: cInterpretSQW_initRegion/enterRegion/exitRegion
  (0x492880/0x492c90/0x493040) build "Region%dInit|Enter|Exit" and the sector
  handlers build "Sector%d%3.3dInit|Enter|Exit"; they look the name up in the
  name tree at qm+0x7534 that the loader fills from every section whose name
  starts "Sector"/"Region" and whose 7th char is a digit (FUN_0046f9b0:835-960,
  isdigit(name[6]) @0x470760).
* Network sync sends FUN_0080eaa0(name) (h = (h*0x71 + f(c)) % 0x3b9ac9f7) of a
  section instead of its index (walker tag 0x28 path :797-800).
* Indices cached at compile time live OUTSIDE this file too: DlgNPC records
  (StartCode tag 0x28, +0x44 = index of "Dialog:<name>") and trigger bindings
  (StartCode tag 0x27, index of "OMO<quest><trigger>") -- see startcode.py.  A
  FunkCode.bin whose section table changes invalidates those as well.

Duplicate names: base:VAMPIRELADY defines 142 names more than once (1,844 extra
entries).  The runtime by-name lookup only ever reaches the first; cached
indices may point at a later copy (registry qid 301 +0x11c = 4830, the second
of two ``QIS_OnLose301``; the first is 4827).

API
===
    load(src) -> Vectoren                       (cached per file)
    sections(src) -> [Section]
    section(src, name) -> Section | None        engine first-match semantics
    sections_named(src, name) -> [Section]      every duplicate
    sections_for_quest(src, qid) -> [Section]
    section_records(src, name|Section|index) -> [(abs_off, tag, size, payload)]
    registry(src) -> [Quest];  quest(src, qid) -> Quest | None
    qis(src, qid) -> OrderedDict kind -> Section
    pools(src) -> [Pool]
    family(name) -> str                          name-family classifier
``src`` is a funkcode_sources.Source, a source key ('base:VAMPIRELADY',
'addon:SERAPHIM') or a legacy class name.

CLI
===
    python vectoren.py --summary            [--sources SPEC]
    python vectoren.py --sections [--grep RE] [--limit N]
    python vectoren.py --quest 15054 [--records]
    python vectoren.py --section Dialog:MP_Romata_99 [--records]
    python vectoren.py --registry [--grep RE]
    python vectoren.py --pools
    python vectoren.py --families
    python vectoren.py --selftest
Default source: base:VAMPIRELADY.
"""
from __future__ import print_function

import argparse
import collections
import os
import re
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import funkcode_sources as fs  # noqa: E402
import funkcode_disasm as fd   # noqa: E402

SECTION_STRIDE = 0x54
REGISTRY_STRIDE = 0x124
NAME_LEN = 64

# registry offset -> (attribute, name format)   (FUN_00465690:2293-2362)
QIS_FIELDS = collections.OrderedDict([
    (0x10C, ("trigger", "QIS_Trigger%d")),
    (0x110, ("on_enter", "QIS_OnEnter%d")),
    (0x114, ("on_setup", "QIS_OnSetUp%d")),
    (0x118, ("on_exit", "QIS_OnExit%d")),
    (0x11C, ("on_lose", "QIS_OnLose%d")),
])

# _stricmp in the C locale folds ASCII only
_ASCII_LOWER = {c: c + 32 for c in range(ord("A"), ord("Z") + 1)}


def fold(name):
    """Case fold the way the engine's name compare does (ASCII only)."""
    return name.translate(_ASCII_LOWER)


def _cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1")


class Section(collections.namedtuple(
        "Section", "index name start length quest tag4c rt50")):
    __slots__ = ()

    @property
    def end(self):
        return self.start + self.length

    @property
    def family(self):
        return family(self.name)


class Quest(collections.namedtuple(
        "Quest", "index qid name f104 f108 trigger on_enter on_setup on_exit "
                 "on_lose flags")):
    __slots__ = ()

    def qis_indices(self):
        return collections.OrderedDict(
            (attr, getattr(self, attr)) for attr, _ in QIS_FIELDS.values())


PoolSlot = collections.namedtuple("PoolSlot", "values ints tail")
Pool = collections.namedtuple(
    "Pool", "index pid name ints todo belohnung strafe slots")


class Vectoren(object):
    """One parsed Vectoren.bin."""

    def __init__(self, data, key="?", path=None):
        self.key = key
        self.path = path
        self.size = len(data)
        u32 = lambda o: struct.unpack_from("<I", data, o)[0]  # noqa: E731

        # -- section table
        n1 = u32(0)
        off = 4
        secs = []
        for i in range(n1):
            e = off + i * SECTION_STRIDE
            start, length, quest, t4c, r50 = struct.unpack_from(
                "<IIiiI", data, e + 0x40)
            secs.append(Section(i, _cstr(data[e:e + NAME_LEN]),
                                start, length, quest, t4c, r50))
        off += n1 * SECTION_STRIDE
        self.sections = secs

        # -- quest registry
        n2 = u32(off)
        off += 4
        reg = []
        self.registry_gap_nonzero = 0
        for j in range(n2):
            e = off + j * REGISTRY_STRIDE
            qid = struct.unpack_from("<i", data, e)[0]
            name = _cstr(data[e + 4:e + 4 + NAME_LEN])
            if any(bytearray(data[e + 0x44:e + 0x104])):
                self.registry_gap_nonzero += 1
            vals = struct.unpack_from("<7iI", data, e + 0x104)
            reg.append(Quest(j, qid, name, *vals))
        off += n2 * REGISTRY_STRIDE
        self.registry = reg

        # -- DQ region-pool table (variable length, FUN_0046f9b0:426-541)
        pools = []
        n3 = u32(off) if off + 4 <= len(data) else 0
        off += 4

        def vec(o):
            c = u32(o)
            o += 4
            return list(struct.unpack_from("<%dI" % c, data, o)), o + 4 * c

        for k in range(n3):
            pid = u32(off)
            name = _cstr(data[off + 4:off + 4 + NAME_LEN])
            ints = struct.unpack_from("<5i", data, off + 0x44)
            o = off + 0x58
            todo, o = vec(o)
            bel, o = vec(o)
            stra, o = vec(o)
            nslots = u32(o)
            o += 4
            slots = []
            for _ in range(nslots):
                values, o = vec(o)                        # -> slot+0x18
                sints = struct.unpack_from("<4i", data, o)  # slot+0x00..0x0c
                tail = struct.unpack_from("<2i", data, o + 16)  # slot+0x10
                o += 24
                slots.append(PoolSlot(values, sints, tail))
            pools.append(Pool(k, pid, name, ints, todo, bel, stra, slots))
            off = o
        self.pools = pools
        self.end = off
        self.tiles = (off == len(data))

        # -- indexes
        self._all = collections.defaultdict(list)
        self._quest = collections.defaultdict(list)
        for s in secs:
            self._all[fold(s.name)].append(s.index)
            if s.quest != -1:
                self._quest[s.quest].append(s.index)
        self._reg_by_id = {}
        for q in reg:
            self._reg_by_id.setdefault(q.qid, q)

    # ------------------------------------------------------------------ lookups
    def find(self, name):
        """Engine semantics: 1..63 chars, first case-insensitive match."""
        if not (1 <= len(name) <= 63):
            return None
        idx = self._all.get(fold(name))
        return self.sections[idx[0]] if idx else None

    def find_all(self, name):
        return [self.sections[i] for i in self._all.get(fold(name), [])]

    def for_quest(self, qid):
        return [self.sections[i] for i in self._quest.get(int(qid), [])]

    def quest(self, qid):
        return self._reg_by_id.get(int(qid))

    def qis(self, qid):
        """The five QIS sections of a quest, via the registry's cached
        indices (falling back to the by-name lookup when the index is 0)."""
        q = self.quest(qid)
        out = collections.OrderedDict()
        for fo, (attr, fmt) in QIS_FIELDS.items():
            idx = getattr(q, attr) if q is not None else 0
            if 0 < idx < len(self.sections):
                out[attr] = self.sections[idx]
            else:
                out[attr] = self.find(fmt % int(qid))
        return out

    def duplicates(self):
        return {k: v for k, v in self._all.items() if len(v) > 1 and k}


# ----------------------------------------------------------------- caching ---
_CACHE = {}
_FUNK = {}


def _source(src):
    if isinstance(src, fs.Source):
        return src
    return fs.get(src)


def load(src):
    s = _source(src)
    path = s.file("Vectoren.bin")
    st = os.stat(path)
    ck = (path, st.st_mtime, st.st_size)
    v = _CACHE.get(ck)
    if v is None:
        v = Vectoren(s.read("Vectoren.bin"), s.key, path)
        _CACHE[ck] = v
    return v


def _funkcode(src):
    s = _source(src)
    path = s.file("FunkCode.bin")
    st = os.stat(path)
    ck = (path, st.st_mtime, st.st_size)
    d = _FUNK.get(ck)
    if d is None:
        d = s.read("FunkCode.bin")
        _FUNK[ck] = d
    return d


# ----------------------------------------------------------------- public API
def sections(src):
    return load(src).sections


def section(src, name):
    return load(src).find(name)


def sections_named(src, name):
    return load(src).find_all(name)


def sections_for_quest(src, qid):
    return load(src).for_quest(qid)


def registry(src):
    return load(src).registry


def quest(src, qid):
    return load(src).quest(qid)


def qis(src, qid):
    return load(src).qis(qid)


def pools(src):
    return load(src).pools


def section_records(src, which):
    """Records of one section as (absolute_offset, tag, size, payload)."""
    v = load(src)
    if isinstance(which, Section):
        sec = which
    elif isinstance(which, int):
        sec = v.sections[which]
    else:
        sec = v.find(which)
        if sec is None:
            raise KeyError("no section %r in %s" % (which, v.key))
    data = _funkcode(src)
    chunk = data[sec.start:sec.end]
    return [(sec.start + off, tag, size, payload)
            for off, tag, size, payload in fd.walk_records(chunk)]


# ---------------------------------------------------------- name families ---
# (regex, family, how the engine reaches it)
FAMILIES = [
    (r"^QIS_OnSetUp-?\d+$", "QIS_OnSetUp", "registry +0x114; SetUpQuest (0x15) -> FUN_0046db20"),
    (r"^QIS_OnEnter-?\d+$", "QIS_OnEnter", "registry +0x110; TriggerQuest -> FUN_0046c160"),
    (r"^QIS_OnExit-?\d+$", "QIS_OnExit", "registry +0x118; ExitQuest (0x0f) -> FUN_0046cbe0"),
    (r"^QIS_OnLose-?\d+$", "QIS_OnLose", "registry +0x11c; LoseQuest (0x36) -> FUN_0046cbe0"),
    (r"^QIS_Trigger-?\d+$", "QIS_Trigger", "registry +0x10c; sprintf QIS_Trigger%d in FUN_0048f030"),
    (r"^SelfTriggerQuestPool-?\d+$", "SelfTriggerQuestPool", "compiler string only (FUN_0045a370)"),
    (r"^SelfTriggerQuest-?\d+$", "SelfTriggerQuest", "sprintf in FUN_004915a0 (tag 0x10 pre-dispatch)"),
    (r"^Dialog:", "Dialog:", "DlgNPC +0x44 (StartCode tag 0x28); sprintf Dialog:%s in FUN_0046b480/FUN_00465690"),
    (r"^ToDo:", "ToDo:", "pool vector +0x64 (Vectoren array 3)"),
    (r"^Belohnung:", "Belohnung:", "pool vector +0x70; run by FUN_00471000"),
    (r"^Strafe:", "Strafe:", "pool vector +0x7c; run by FUN_00471000"),
    (r"^OMO", "OMO<quest><trigger>", "StartCode tag 0x27 index -> FUN_004636c0 trigger binding"),
    (r"^Region\d+(Init|Enter|Exit)$", "Region<n>Init|Enter|Exit", "name tree qm+0x7534; cInterpretSQW_*Region"),
    (r"^Sector\d+(Init|Enter|Exit)$", "Sector<n>Init|Enter|Exit", "name tree qm+0x7534; cInterpretSQW_*Sector"),
    (r"^(Region|Sector)\d", "Region/Sector<n>(other)", "name tree qm+0x7534 (loader), no sprintf seen"),
    (r"^InitRg", "InitRg*", "WorkFunktion refuses InitRg*Pools*/InitRg*(DQ_Quest)* (FUN_0046ba90:56-59)"),
    (r"^DQTrgRegion", "DQTrgRegion*", "never run by WorkFunktion (FUN_0046ba90:60)"),
    (r"^DQStartRegion", "DQStartRegion*", "never run by WorkFunktion (FUN_0046ba90:63)"),
    (r"^btn_", "btn_*", "SetButton (0x3c) action name -> by-name lookup"),
    (r"^od_", "od_*", "CreateNPC on-death hook name -> by-name lookup"),
    (r"^eq_", "eq_*", "CreateNPC hook name -> by-name lookup"),
    (r"^fillchest_", "fillchest_*", "by-name (CallFunktion)"),
    (r"^setvar_", "setvar_*", "by-name (CallFunktion)"),
    (r"^$", "(null entry)", "entry 0"),
]
_FAM_RE = [(re.compile(p, re.I), f, how) for p, f, how in FAMILIES]


def family(name):
    for rx, fam, _ in _FAM_RE:
        if rx.search(name):
            return fam
    return "other"


# ---------------------------------------------------------------- self-test --
EXPECT_DQ15054 = [
    (4061, "Dialog:DLG_15054_START", 241275, 82),
    (4062, "Dialog:DLG_15054_OFFEN", 241357, 42),
    (4063, "Dialog:DLG_15054_ZIEL", 241399, 50),
    (4064, "btn_accept_dq_15054", 241449, 9),
    (4065, "btn_ok_dq_15054", 241458, 104),
    (4066, "od_15054", 241562, 115),
    (4067, "QIS_OnEnter15054", 241677, 215),
    (4068, "QIS_OnExit15054", 241892, 20),
    (4069, "QIS_OnSetUp15054", 241912, 78),
    (4070, "QIS_Trigger15054", 241990, 0),
    (4071, "SelfTriggerQuest15054", 241990, 9),
]


class _T(object):
    def __init__(self):
        self.n = 0
        self.fail = 0

    def check(self, cond, msg):
        self.n += 1
        if not cond:
            self.fail += 1
        print("  [%s] %s" % ("ok" if cond else "FAIL", msg))
        return cond


def selftest():
    t = _T()
    base = fs.get(fs.BASELINE)
    v = load(base)
    print("== %s  (%s)" % (base.key, v.path))
    t.check(len(v.sections) == 23495, "sectionCount 23,495 (got %d)" % len(v.sections))
    t.check(len(v.registry) == 601, "questCount 601 (got %d)" % len(v.registry))
    t.check(len(v.pools) == 22, "poolCount 22 (got %d)" % len(v.pools))
    t.check(v.tiles, "parse ends exactly at EOF (%d of %d)" % (v.end, v.size))

    got = [(s.index, s.name, s.start, s.length) for s in sections_for_quest(base, 15054)]
    t.check(got == EXPECT_DQ15054,
            "DQ_15054 owns exactly the 11 AUDIT-Sev5 sections with those {start,len}")
    s = section(base, "dq_belohnung")
    t.check(s is not None and (s.start, s.length) == (0, 410),
            "dq_belohnung = {0,410} (got %s)" % ((s.start, s.length) if s else None,))
    recs = section_records(base, "dq_belohnung")
    t.check(len(recs) == 18 and recs[-1][0] + recs[-1][2] == 410
            and [r_[1] for r_ in recs[:3]] == [0x43] * 3 and recs[-1][1] == 0x7d,
            "dq_belohnung slice = 18 records tiling [0,410): 3 SetVar inits ... Gewinn 0x7d last")
    s = section(base, "Dialog:MP_Romata_99")
    t.check(s is not None and (s.start, s.length) == (486, 1894),
            "Dialog:MP_Romata_99 = {486,1894} (got %s)" % ((s.start, s.length) if s else None,))
    t.check(section(base, "DIALOG:mp_romata_99") == s, "lookup is case-insensitive")
    r = v.registry
    t.check((r[1].qid, r[1].name) == (99, "MP-Start"), "registry[1] = 99 'MP-Start'")
    t.check((r[2].qid, r[2].name) == (5001, "Das Abenteurer Lager"),
            "registry[2] = 5001 'Das Abenteurer Lager'")
    q = qis(base, 15054)
    t.check([x.index if x else None for x in q.values()] == [4070, 4067, 4069, 4068, None],
            "qis(15054) = Trigger 4070, OnEnter 4067, OnSetUp 4069, OnExit 4068, no OnLose")
    pids = [p.pid for p in v.pools]
    t.check(pids == [i for i in range(1, 24) if i != 14], "pool ids 1..23 without 14")
    t.check(all(p.name == "Region%d" % p.pid for p in v.pools), "pool names Region<pid>")
    bad = nz = 0
    for p in v.pools:
        for vecname, pre in (("todo", "ToDo:"), ("belohnung", "Belohnung:"), ("strafe", "Strafe:")):
            for x in getattr(p, vecname):
                if x == 0:          # empty slot -> null section 0
                    continue
                nz += 1
                if not v.sections[x & 0xFFFFFF].name.startswith(pre):
                    bad += 1
    t.check(bad == 0 and nz == 22 * (85 + 2 + 2) == sum(1 for s_ in v.sections if s_.tag4c),
            "all %d non-zero pool indices name ToDo:/Belohnung:/Strafe: sections "
            "(85 ToDo + 2 + 2 per pool = the tag4c!=0 count; 0 = empty slot)" % nz)
    dup = v.duplicates()
    t.check(len(dup) == 142 and sum(len(x) - 1 for x in dup.values()) == 1844,
            "142 duplicated names, 1,844 extra entries")

    print("== all 20 sources")
    for src in fs.resolve("all", default="all"):
        vv = load(src)
        # tiling of Vectoren itself
        ok_tile = vv.tiles
        # QIS indices name the right sections
        qok = qbad = qdup = 0
        for qq in vv.registry:
            for fo, (attr, fmt) in QIS_FIELDS.items():
                idx = getattr(qq, attr)
                if idx > 0:
                    if fold(vv.sections[idx].name) == fold(fmt % qq.qid):
                        qok += 1
                        if vv.find(fmt % qq.qid).index != idx:
                            qdup += 1
                    else:
                        qbad += 1
        flags0 = all(qq.flags == 0 for qq in vv.registry) and vv.registry_gap_nonzero == 0
        rt0 = all(s_.rt50 == 0 for s_ in vv.sections)
        # sections tile FunkCode.bin exactly, on record boundaries
        data = _funkcode(src)
        starts = set(o for o, _, _, _ in fd.walk_records(data))
        starts.add(len(data))
        ranges = sorted((s_.start, s_.end) for s_ in vv.sections if s_.length)
        cov, last, overl = 0, 0, 0
        for a, b in ranges:
            if a < last:
                overl += 1
            cov += max(0, b - max(a, last))
            last = max(last, b)
        onb = all(a in starts and b in starts for a, b in ranges)
        ok = (ok_tile and qbad == 0 and flags0 and rt0 and cov == len(data)
              and overl == 0 and onb)
        t.check(ok, "%-20s sec=%5d reg=%3d pools=%2d  EOF=%s  QIS ok=%d bad=%d (later-dup %d)  "
                    "file-state zero=%s  FunkCode covered %d/%d overlaps=%d boundaries=%s"
                % (src.key, len(vv.sections), len(vv.registry), len(vv.pools), ok_tile,
                   qok, qbad, qdup, flags0 and rt0, cov, len(data), overl, onb))
    print("\n%d checks, %d failed" % (t.n, t.fail))
    return t.fail == 0


# --------------------------------------------------------------------- CLI --
def _print_section(s, v=None):
    print("%6d  %-44s {%8d,%6d}  quest=%-6d tag4c=%d  [%s]"
          % (s.index, s.name, s.start, s.length, s.quest, s.tag4c, s.family))


def _print_records(src, sec, ops):
    try:
        import funkcode_tags as ft
        label = getattr(ft, "label_for", None)
    except Exception:
        label = None
    for off, tag, size, payload in section_records(src, sec):
        lab = label(tag) if label else ""
        print("        @0x%06x  tag 0x%02x %-18s size %4d  %s"
              % (off, tag, lab, size, payload[:48].hex()))
        if ops:
            lines, _, _ = fd.disasm_payload(payload, indent=12, limit=ops)
            for ln in lines:
                print(ln)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Vectoren.bin reader (section table, "
                                             "quest registry, DQ pool table)")
    ap.add_argument("--sources", default=fs.BASELINE, help="source spec (default %(default)s)")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--sections", action="store_true", help="dump the section table")
    ap.add_argument("--quest", type=int, help="sections owned by one quest id (+ registry row)")
    ap.add_argument("--section", help="one section by name (engine lookup)")
    ap.add_argument("--registry", action="store_true")
    ap.add_argument("--pools", action="store_true")
    ap.add_argument("--families", action="store_true", help="section-name family counts")
    ap.add_argument("--records", action="store_true", help="also list the FunkCode records")
    ap.add_argument("--ops", type=int, default=0, help="with --records: disassemble N ops/record")
    ap.add_argument("--grep", help="regex filter on names")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    if a.selftest:
        return 0 if selftest() else 1
    rx = re.compile(a.grep, re.I) if a.grep else None
    srcs = fs.resolve(a.sources, default=fs.BASELINE)
    if not any([a.sections, a.quest is not None, a.section, a.registry, a.pools, a.families]):
        a.summary = True
    for src in srcs:
        v = load(src)
        print("# %s  %s  (%d bytes)" % (src.key, v.path, v.size))
        if a.summary:
            print("  sections %d, registry %d, pools %d, parse end %d, tiles=%s, duplicate names %d"
                  % (len(v.sections), len(v.registry), len(v.pools), v.end, v.tiles,
                     len(v.duplicates())))
        if a.sections:
            n = 0
            for s in v.sections:
                if rx and not rx.search(s.name):
                    continue
                _print_section(s)
                if a.records:
                    _print_records(src, s, a.ops)
                n += 1
                if a.limit and n >= a.limit:
                    break
        if a.section:
            hits = v.find_all(a.section)
            if not hits:
                print("  no section %r" % a.section)
            for i, s in enumerate(hits):
                print("  %s" % ("engine resolves this one:" if i == 0 else "later duplicate:"))
                _print_section(s)
                if a.records:
                    _print_records(src, s, a.ops)
        if a.quest is not None:
            q = v.quest(a.quest)
            if q:
                print("  registry[%d] qid=%d %r f104=%d f108=%d flags=%#x"
                      % (q.index, q.qid, q.name, q.f104, q.f108, q.flags))
                for attr, idx in q.qis_indices().items():
                    print("     %-9s -> %s" % (attr, ("%d %s" % (idx, v.sections[idx].name)) if idx > 0 else idx))
            else:
                print("  quest %d not in the registry" % a.quest)
            for s in v.for_quest(a.quest):
                _print_section(s)
                if a.records:
                    _print_records(src, s, a.ops)
        if a.registry:
            for q in v.registry:
                if rx and not (rx.search(q.name) or rx.search(str(q.qid))):
                    continue
                print("%4d  qid=%-6d %-40s f104=%-3d f108=%-3d trig=%-5d enter=%-5d setup=%-5d exit=%-5d lose=%-5d"
                      % (q.index, q.qid, q.name, q.f104, q.f108, q.trigger, q.on_enter,
                         q.on_setup, q.on_exit, q.on_lose))
        if a.pools:
            for p in v.pools:
                print("pool %2d pid=%-3d %-10s ints=%s todo=%d belohnung=%d strafe=%d slots=%d"
                      % (p.index, p.pid, p.name, p.ints, len(p.todo), len(p.belohnung),
                         len(p.strafe), len(p.slots)))
                for sl in p.slots:
                    print("      slot ints=%s tail=%s values(%d)=%s"
                          % (sl.ints, sl.tail, len(sl.values), sl.values[:12]))
        if a.families:
            c = collections.Counter(s.family for s in v.sections)
            how = {f: h for _, f, h in FAMILIES}
            for fam, n in c.most_common():
                print("  %6d  %-28s %s" % (n, fam, how.get(fam, "")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
