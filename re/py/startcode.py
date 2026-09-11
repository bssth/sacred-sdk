"""startcode.py -- reader for ``StartCode.bin``: named world positions (DefPos,
tag 0x17), dialog-NPC declarations (DlgNPC, tag 0x28), initial script variables
(DefNum/SetVar/SetVarBit, tags 0x41/0x43/0x44/0x45), pool positions (AddPoolPos,
tag 0x6c) and trigger->section bindings (tag 0x27).

Evidence: ``sdk/Sacred_decrypted.exe`` (VA = file offset + 0x400000) and the
decompiles in ``sdk/re/ghidra/decompiled/``; every count below is reproduced by
``--selftest``.

WHAT THE FILE IS
================
A FunkCode-format TLV record stream (tag u8, size u16 BE incl. the 3-byte
header; ``funkcode_disasm.walk_records``), 15,457 records in base:VAMPIRELADY,
walking to EOF in all 20 sources.  The loader ``FUN_0046f9b0`` uses it twice:
  * NEW GAME: :1089-1111 rewinds it and runs every record through the walker
    ``FUN_00475680`` with ctx+0x14 = 1 (the flag that suppresses the MP network
    echo in the handlers, e.g. FUN_00478780:192).  A loaded savegame restores
    the tables from the save instead.
  * CACHE REBUILD: :273-348 -- when ``DefPos.bin`` is missing or stale (first
    dword != 0x4D2, or its third count <= 9, :196-272) the engine pre-scans
    StartCode.bin then FunkCode.bin (FUN_0045f220 loops :280-296) and WRITES
    DefPos.bin = magic 0x4D2, the 100-byte position table (qm+0x334), the
    80-byte DlgNPC table (qm+0x755c) and a 76-byte table (DAT_00ab7820).  That
    is why every DefPos.bin with the magic word is post-install (AUDIT Sev. 1):
    the shipped files lack the magic and are discarded and regenerated.  This
    module never reads DefPos.bin as content; ``cache_check()`` only uses the
    engine-written copy to prove the reader reproduces the engine's table
    (1,921 = 1,921 names, all X/Y/R/Z equal, base:VAMPIRELADY).

DefPos -- tag 0x17 -> FUN_00478780 (walker case :652-656)
=========================================================
Operands (opcode stream read by FUN_00472bc0): ``01 <name>`` then up to four
``0b <i32>``.  The prologue (0x4787a2-0x4787df) zeroes entry +0x00 and name[0]
and sets four slots X, Y, R, Z to -1; each ``0b`` fills the FIRST slot still at
-1 (:136-149).  A SECOND ``01 <name2>`` copies X, Y, R from an already-declared
position of that name (:95-134) -- the 'ss' alias form: 348 records in the base
FunkCode.bin, none in StartCode, every one ``LOC_RG<n>_DUNGEONZIEL<k>`` <-
``LOC_RG<n>_ZIEL<k>``.  Those source names are never DefPos-declared (they look
like runtime locations; GetPoolPos, tag 0x6d, logs "GetPoolPos from %s to %s:
%d, %d" -- not traced), so neither the engine pre-scan nor this reader stores
them.  The entry is stored only if a name was given and X, Y != -1 (:152); a
declaration whose name (case-insensitive, same length) already exists
OVERWRITES that entry in place (:169-187), otherwise it is appended
(:288-303).  StartCode itself redeclares two names (tpstart_dun2_rg1 #149 ->
#157 moves it from (3181, 2671) to (4819, 357); posTorfstecher #15291/#15330
identical), so 1,440 records give 1,438 positions.  Runtime entry, stride 100
(= DefPos.bin array 0):
    +0x00 u32 0     +0x04 char name[64] (strcpy; bytes after the NUL are stack)
    +0x44 i32 X     +0x48 i32 Y     +0x4c i32 R (radius)     +0x50 i32 Z
    +0x54..+0x63    NEVER WRITTEN -- the 100-byte copy (:183-187 / :296-300)
                    takes uninitialised stack; so are +0x40..+0x43 when the
                    name is short.  DefPos.bin "+0x40 kind 40/41" and "+0x54
                    category 23..41, +0x58 = 4096" are that residue (no reader
                    of +0x54/+0x58 exists in the decompiles).
Resolution of a position operand (FUN_00472bc0:521-563, :2470-2519): X, Y ->
ctx+0xa860/0xa864; if R >= 1 the point is scattered uniformly in
[X-R, X+R] x [Y-R, Y+R] (rand() % (2R+1)), retried up to 10 times until
FUN_00472af0(x, y) accepts it; Z -> ctx+0xa868 (negative -> 0).  Z is the same
third component that ``CPOS_hero``/``CPOS_RES:`` references fill from the
object's u8 at +0x24 (:220-222, :437-446), and Teleport hands it to
FUN_0054d9d0(x, y, z, 1) and the cell packer FUN_006224b0(0, x, y, z)
(FUN_00491d40:201-211), jittering it by rand()%3 on retry (:266-267).

DlgNPC -- tag 0x28, inline in the walker (FUN_00475680:740-800)
===============================================================
Payload = 1 flag byte + the raw 80-byte runtime entry, appended to qm+0x755c; a
dummy entry 0 is inserted in front of the first one (:765-789), so runtime
index = list position + 1 (and DefPos.bin count2 = records + 1).
    +0x00 i32 -1 (every record)     +0x04 char name[64]
    +0x44 i32 section index of "Dialog:<name>" in Vectoren.bin (100 % of
          records in all 20 sources); re-resolved by name on load
          (FUN_00465690:869-876); sent as FUN_0080eaa0(name) over the network
          (:797-800)
    +0x48 i32 marker (10 x323, 11 x276, 15 x91, 126 x90, 1043 x65, 13, 8, -1,
          1, 1024 ... in base:VAMPIRELADY)
    +0x4c u32 runtime dialog handle, written by tag 0x1f (0 in every record)

Variables -- tags 0x41 DefNum / 0x43 SetVar -> FUN_0049b2b0 (same handler)
=========================================================================
Table qm+0x7550, stride 0x24: char name[32] + i32 value at +0x20 (:92-115).
``01 <name> 0b <value>`` sets the value; a second ``01 <var>`` copies that
variable's current value (:82-116).  Existing name (case-insensitive) ->
updated in place (:161); else appended (:219-234).  Side effect (:123-134,
disassembly 0x49b438-0x49b47e): ``if (FUN_0085aa60(name, "atmo_rg", 7) != 0)``
the value is ALSO stored to qm+0x7750 + 4*clamp(atoi(name+7), 0, 149).
FUN_0085aa60 is used as _strnicmp (0 = match) by every other caller
(FUN_00472bc0:436/457/502 CPOS_hero/CPOS_RES:/CPOS_, FUN_0046ba90:56/60/63), so
as compiled the store fires for names that do NOT start with "atmo_rg" -- and
no corpus variable does: StartCode's region-atmosphere variables are spelled
``atmos_rg<N>`` (24 in base:VAMPIRELADY, 20 in addon:SERAPHIM), so atoi("g<N>")
= 0 and they all land in slot 0.  This reader records the slot the engine writes
as ``slot7750`` per step (None when the name is shorter than 7 chars: name+7 is
then past the NUL, i.e. stack residue).  What qm+0x7750 is used for: UNKNOWN.
Longest name in the corpus: 21 chars (buffer is 32).
SetVarBit -- tag 0x44 -> FUN_0049b840: every ``0b <b>`` ORs 1 << (b & 31) into
a mask (:199); a second string starting "res" names a target creature, any
other second string names a variable whose VALUE is the bit (:136-190).  Name
"HeroQBit" goes to the hero's stats (:204-212); otherwise an existing variable
gets value |= mask (:320-322) and a missing one is APPENDED with value = mask
(:384-399 -- the 9-dword copy is the 32-byte name buffer at -0x190 plus the mask
local at -0x170, i.e. entry +0x20).  104 of the 136 SetVarBit records in
base:VAMPIRELADY StartCode create their variable that way ('9512' = 1,
'TYPE_WEAPON_09' = 2, 'audiomeldungen' = 1 | 1<<15); the step is marked
``created_by_bit``.  UnsetVarBit (0x45, FUN_0049c160) is not read; it
is modelled as &= ~mask and flagged ``inferred`` (no 0x45 record in any base or
addon class StartCode).

Conditionality: the walker's IF is flat (FUN_00475680:1439-1532): a false IF
(0x3a) skips to the next ELSEIF (0x42, predicate re-tested) or ELSE (0x3b); a
true branch runs up to the ELSE, and ELSE then skips EXACTLY ONE record (the
else-branch).  Tag 0x3e has no case in the walker (a no-op); ``3b 3e`` is the
corpus idiom for "ELSE {nothing}" = end-of-IF.  ``conditional`` marks records
inside an IF..ELSE span or in an else slot.

AddPoolPos -- tag 0x6c -> FUN_004790c0: ``01 <pool> 0b x 0b y`` (1,595 records,
base:VAMPIRELADY; pool names Versteck_*, ...).  Consumers GetPoolPos /
GetPoolPosition log "GetPoolPos failed. Pool (%s) is empty" (strmap).
Trigger binding -- tag 0x27 -> FUN_004918b0: ``0b <section index> 01 <name>``
-> FUN_004636c0(name, index).  The index always names the section
"OMO<quest><name>" (661/661 base:VAMPIRELADY, all 20 sources).

KNOWN SHAPES (base:VAMPIRELADY StartCode): DefPos sii 1,030 / siii 239 /
siiii 171; the 4th int (Z) is 1 x146, 2 x22, 0 x2, 3 x1; the 3rd (R) is 0 x343,
4 x26, 2 x11 ... 20 x1.  Negative X/Y: 5 names here, 17 in the addon and net
StartCode (all interiors: pos_Hoehle9521, posVilyaEnklave, pos_valor_thronsaal,
pos_7606_alb1 ...); see ``--help`` of the CLI for the consumer analysis.

API
===
    records(src, fname="StartCode.bin") -> [Record]
    positions(src, include_funkcode=False) -> OrderedDict name -> Position
        Position is a (x, y, extras) tuple with .radius .z .name properties;
        extras: radius, z, file, record, offset, copied_from, declarations
    resolve(src, name, include_funkcode=True) -> (x, y, z, radius) | None
    dlgnpcs(src) -> [DlgNPC]
    variables(src) -> OrderedDict name -> Variable (name, value, extras)
    pool_positions(src) -> OrderedDict pool -> [(x, y, record, offset)]
    trigger_bindings(src) -> [Binding]
    cache_check(src) -> dict | None           (engine-written DefPos.bin only)
``src``: funkcode_sources.Source, source key or legacy class name.

CLI
===
    python startcode.py --summary            [--sources SPEC]
    python startcode.py --positions [--grep RE] [--with-funkcode]
    python startcode.py --position NAME [--with-funkcode]
    python startcode.py --dlgnpcs | --variables | --pools | --triggers [--grep RE]
    python startcode.py --cache-check
    python startcode.py --selftest
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

TAG_DEFPOS = 0x17
TAG_TRIGBIND = 0x27
TAG_DLGNPC = 0x28
TAG_IF = 0x3a
TAG_ELSE = 0x3b
TAG_ELSEIF = 0x42
TAG_DEFNUM = 0x41
TAG_SETVAR = 0x43
TAG_SETVARBIT = 0x44
TAG_UNSETVARBIT = 0x45
TAG_ADDPOOLPOS = 0x6c

OP_STR = 0x01
OP_I32 = 0x0b

_ASCII_LOWER = {c: c + 32 for c in range(ord("A"), ord("Z") + 1)}


def fold(name):
    """ASCII-only case fold (the engine's FUN_00859690 compare)."""
    return name.translate(_ASCII_LOWER)


def _source(src):
    return src if isinstance(src, fs.Source) else fs.get(src)


# ------------------------------------------------------------ records -------
Record = collections.namedtuple("Record", "index offset tag size payload file")

_REC_CACHE = {}


def records(src, fname="StartCode.bin"):
    s = _source(src)
    path = s.file(fname)
    st = os.stat(path)
    ck = (path, st.st_mtime, st.st_size)
    r = _REC_CACHE.get(ck)
    if r is None:
        data = s.read(fname)
        r = [Record(i, off, tag, size, payload, fname)
             for i, (off, tag, size, payload) in enumerate(fd.walk_records(data))]
        end = (r[-1].offset + r[-1].size) if r else 0
        if end != len(data):
            raise ValueError("%s %s: record walk stops at %d of %d bytes"
                             % (s.key, fname, end, len(data)))
        _REC_CACHE[ck] = r
    return r


def operands(payload):
    """Operand stepper for the two operand kinds these declaration tags use:
    0x01 ASCIIZ and 0x0b i32 (widths per FUN_00472bc0).  Returns
    (list of (op, value), clean) -- clean is False if another opcode appears."""
    out = []
    i = 1                       # payload[0] is the flags byte
    n = len(payload)
    while i < n:
        op = payload[i]
        if op == OP_STR:
            e = payload.find(b"\0", i + 1)
            if e < 0:
                return out, False
            out.append((OP_STR, payload[i + 1:e].decode("latin-1")))
            i = e + 1
        elif op == OP_I32 and i + 5 <= n:
            out.append((OP_I32, struct.unpack_from("<i", payload, i + 1)[0]))
            i += 5
        else:
            return out, False
    return out, True


def conditional_indices(recs):
    """Record indices executed only under an IF (flat walker semantics)."""
    cond = set()
    in_if = False
    else_slot = False
    for r in recs:
        if else_slot:
            cond.add(r.index)
            else_slot = False
            continue
        if r.tag == TAG_IF:
            in_if = True
            continue
        if in_if:
            if r.tag == TAG_ELSE:
                in_if = False
                else_slot = True
                continue
            cond.add(r.index)
    return cond


# ----------------------------------------------------------- positions ------
class Position(collections.namedtuple("Position", "x y extras")):
    __slots__ = ()

    @property
    def radius(self):
        return self.extras.get("radius", -1)

    @property
    def z(self):
        return self.extras.get("z", -1)

    @property
    def name(self):
        return self.extras.get("name")


def _defpos_stream(recs, table, order):
    """Apply DefPos records to (table: folded -> [name, X, Y, R, Z, meta])."""
    for r in recs:
        if r.tag != TAG_DEFPOS:
            continue
        ops, clean = operands(r.payload)
        name = ""
        slots = [-1, -1, -1, -1]
        copied = None
        for op, val in ops:
            if op == OP_STR:
                if not name:
                    name = val
                else:
                    src_e = table.get(fold(val))
                    if src_e is not None:
                        slots[0], slots[1], slots[2] = src_e[1], src_e[2], src_e[3]
                        copied = val
            else:
                for k in range(4):
                    if slots[k] == -1:
                        slots[k] = val
                        break
        if not name or slots[0] == -1 or slots[1] == -1:
            continue
        key = fold(name)
        meta = {"file": r.file, "record": r.index, "offset": r.offset,
                "copied_from": copied, "clean": clean}
        if key in table:
            prev = table[key]
            meta["declarations"] = prev[5].get("declarations", 1) + 1
            meta["first"] = prev[5].get("first", (prev[5]["file"], prev[5]["record"]))
            table[key] = [name] + slots + [meta]
        else:
            meta["declarations"] = 1
            meta["first"] = (r.file, r.index)
            table[key] = [name] + slots + [meta]
            order.append(key)


_POS_CACHE = {}


def positions(src, include_funkcode=False):
    """Named positions in engine table order: {name: Position(x, y, extras)}.
    include_funkcode=True reproduces the engine pre-scan (StartCode then
    FunkCode), i.e. the exact contents of qm+0x334 / DefPos.bin array 0."""
    s = _source(src)
    ck = (s.key, bool(include_funkcode))
    if ck in _POS_CACHE:
        return _POS_CACHE[ck]
    table, order = {}, []
    _defpos_stream(records(s, "StartCode.bin"), table, order)
    if include_funkcode:
        _defpos_stream(records(s, "FunkCode.bin"), table, order)
    out = collections.OrderedDict()
    for key in order:
        name, x, y, rr, z, meta = table[key]
        extras = dict(meta)
        extras.update({"name": name, "radius": rr, "z": z})
        out[name] = Position(x, y, extras)
    _POS_CACHE[ck] = out
    return out


def resolve(src, name, include_funkcode=True):
    """What a position operand naming `name` resolves to: (x, y, z, radius)
    before the random scatter; z < 0 becomes 0 (FUN_00472bc0:2508-2514)."""
    key = fold(name)
    for n, p in positions(src, include_funkcode).items():
        if fold(n) == key:
            return (p.x, p.y, max(p.z, 0), p.radius)
    return None


# ------------------------------------------------------------- DlgNPC -------
DlgNPC = collections.namedtuple(
    "DlgNPC", "name handle section marker dlg_handle record offset section_name")


def dlgnpcs(src, resolve_sections=True):
    s = _source(src)
    names = None
    if resolve_sections:
        try:
            import vectoren
            names = vectoren.sections(s)
        except Exception:
            names = None
    out = []
    for r in records(s):
        if r.tag != TAG_DLGNPC:
            continue
        e = r.payload[1:81]
        handle = struct.unpack_from("<i", e, 0)[0]
        name = e[4:0x44].split(b"\0", 1)[0].decode("latin-1")
        sec, marker, dh = struct.unpack_from("<iiI", e, 0x44)
        sname = names[sec].name if names is not None and 0 <= sec < len(names) else None
        out.append(DlgNPC(name, handle, sec, marker, dh, r.index, r.offset, sname))
    return out


# ---------------------------------------------------------- variables -------
class Variable(collections.namedtuple("Variable", "name value extras")):
    __slots__ = ()


def _c_atoi(s):
    """C atoi: optional leading whitespace and sign, then digits; else 0."""
    m = re.match(r"\s*([+-]?\d+)", s)
    return int(m.group(1)) if m else 0


def variables(src):
    """Initial script variables after running StartCode's DefNum/SetVar/
    SetVarBit/UnsetVarBit records in order (a new game's starting table)."""
    s = _source(src)
    recs = records(s)
    cond = conditional_indices(recs)
    table = collections.OrderedDict()   # folded -> [name, value, extras]

    def get(nm):
        e = table.get(fold(nm))
        return e[1] if e is not None else None

    for r in recs:
        if r.tag not in (TAG_DEFNUM, TAG_SETVAR, TAG_SETVARBIT, TAG_UNSETVARBIT):
            continue
        ops, clean = operands(r.payload)
        strs = [v for o, v in ops if o == OP_STR]
        ints = [v for o, v in ops if o == OP_I32]
        if not strs:
            continue
        name = strs[0]
        key = fold(name)
        step = {"record": r.index, "offset": r.offset, "tag": r.tag,
                "conditional": r.index in cond, "operands": ops}
        if r.tag in (TAG_DEFNUM, TAG_SETVAR):
            value = ints[-1] if ints else None
            if len(strs) > 1:
                cv = get(strs[1])
                if cv is not None:
                    value = cv
                step["copied_from"] = strs[1]
            kind = "DefNum" if r.tag == TAG_DEFNUM else "SetVar"
        else:
            mask = 0
            for b in ints:
                mask |= 1 << (b & 31)
            for extra in strs[1:]:
                if fold(extra[:3]) == "res":
                    step["target"] = extra
                else:
                    bv = get(extra)
                    if bv is not None:
                        mask |= 1 << (bv & 31)
            prev = get(name)
            if r.tag == TAG_SETVARBIT:
                kind = "SetVarBit"
                if prev is None:
                    step["created_by_bit"] = True
                value = (prev or 0) | mask
            else:
                kind = "UnsetVarBit"
                step["inferred"] = True
                value = (prev or 0) & ~mask
            step["mask"] = mask
        step["kind"] = kind
        step["value_after"] = value
        if kind in ("DefNum", "SetVar") and fold(name[:7]) != "atmo_rg":
            # FUN_0049b2b0 @0x49b447-0x49b47e: store to qm+0x7750[clamp(atoi(name+7))]
            step["slot7750"] = (max(0, min(0x95, _c_atoi(name[7:])))
                                if len(name) >= 7 else None)
        if key in table:
            e = table[key]
            e[1] = value
            e[2]["history"].append(step)
            e[2]["conditional"] = e[2]["conditional"] or step["conditional"]
        else:
            table[key] = [name, value, {"history": [step], "record": r.index,
                                        "offset": r.offset, "kind": kind,
                                        "conditional": step["conditional"]}]
    out = collections.OrderedDict()
    for key, (name, value, extras) in table.items():
        out[name] = Variable(name, value, extras)
    return out


# ------------------------------------------------ pools / trigger bindings --
def pool_positions(src):
    out = collections.OrderedDict()
    for r in records(src):
        if r.tag != TAG_ADDPOOLPOS:
            continue
        ops, clean = operands(r.payload)
        strs = [v for o, v in ops if o == OP_STR]
        ints = [v for o, v in ops if o == OP_I32]
        if strs and len(ints) >= 2:
            out.setdefault(strs[0], []).append((ints[0], ints[1], r.index, r.offset))
    return out


Binding = collections.namedtuple("Binding", "trigger section section_name record offset")


def trigger_bindings(src, resolve_sections=True):
    s = _source(src)
    names = None
    if resolve_sections:
        try:
            import vectoren
            names = vectoren.sections(s)
        except Exception:
            names = None
    out = []
    for r in records(s):
        if r.tag != TAG_TRIGBIND:
            continue
        ops, clean = operands(r.payload)
        strs = [v for o, v in ops if o == OP_STR]
        ints = [v for o, v in ops if o == OP_I32]
        if not strs or not ints:
            continue
        idx = ints[0]
        sname = names[idx].name if names is not None and 0 <= idx < len(names) else None
        out.append(Binding(strs[0], idx, sname, r.index, r.offset))
    return out


# ------------------------------------------------------ engine cache check --
def _read_engine_cache(src):
    """Parse an ENGINE-WRITTEN DefPos.bin (magic 0x4D2) -> (names, positions,
    count2, count3) or None.  Never content -- validation only."""
    s = _source(src)
    if not s.exists("DefPos.bin"):
        return None
    d = s.read("DefPos.bin")
    if len(d) < 8 or struct.unpack_from("<I", d, 0)[0] != 0x4D2:
        return None
    n1 = struct.unpack_from("<I", d, 4)[0]
    ents = []
    for i in range(n1):
        e = 8 + i * 100
        name = d[e + 4:e + 0x44].split(b"\0", 1)[0].decode("latin-1")
        x, y, rr, z = struct.unpack_from("<4i", d, e + 0x44)
        ents.append((name, x, y, rr, z))
    o = 8 + n1 * 100
    n2 = struct.unpack_from("<I", d, o)[0]
    o += 4 + n2 * 80
    n3 = struct.unpack_from("<I", d, o)[0] if o + 4 <= len(d) else None
    return ents, n2, n3


def cache_check(src):
    c = _read_engine_cache(src)
    if c is None:
        return None
    ents, n2, n3 = c
    pos = positions(src, include_funkcode=True)
    mine = {fold(n): (p.x, p.y, p.radius, p.z) for n, p in pos.items()}
    same = diff = missing = 0
    diffs = []
    for name, x, y, rr, z in ents:
        m = mine.get(fold(name))
        if m is None:
            missing += 1
        elif m == (x, y, rr, z):
            same += 1
        else:
            diff += 1
            if len(diffs) < 5:
                diffs.append((name, m, (x, y, rr, z)))
    order_ok = [fold(e[0]) for e in ents] == [fold(n) for n in pos]
    return {"cache_positions": len(ents), "reader_positions": len(pos), "equal": same,
            "different": diff, "missing": missing, "order_equal": order_ok,
            "diffs": diffs, "cache_dlgnpc": n2, "reader_dlgnpc": len(dlgnpcs(src, False)),
            "cache_count3": n3}


# ---------------------------------------------------------------- self-test --
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
    import vectoren
    t = _T()
    b = fs.get(fs.BASELINE)
    recs = records(b)
    print("== %s" % b.key)
    t.check(len(recs) == 15457, "StartCode records 15,457 (got %d)" % len(recs))
    h = collections.Counter(r.tag for r in recs)
    t.check((h[TAG_DEFPOS], h[TAG_DLGNPC], h[TAG_SETVAR], h[TAG_DEFNUM], h[TAG_SETVARBIT],
             h[TAG_ADDPOOLPOS], h[TAG_TRIGBIND]) == (1440, 1022, 387, 2, 136, 1595, 661),
            "tag counts DefPos 1440 / DlgNPC 1022 / SetVar 387 / DefNum 2 / SetVarBit 136 / "
            "AddPoolPos 1595 / binding 661")
    r2 = recs[2]
    ops, _ = operands(r2.payload)
    t.check(r2.offset == 0x28 and r2.tag == TAG_DEFPOS and ops == [(1, "hauptmann3"), (11, 3349), (11, 2534)],
            "#2 @0x28 = DefPos hauptmann3 (3349, 2534)")
    r = recs[15272]
    ops, _ = operands(r.payload)
    t.check(r.offset == 0x8b421 and r.tag == TAG_SETVAR and ops == [(1, "HQ65Gold"), (11, 30)],
            "#15272 @0x8b421 = SetVar HQ65Gold = 30")
    var = variables(b)
    t.check(var["HQ65Gold"].value == 30, "variables()['HQ65Gold'] == 30")
    made = [n for n, v in var.items() if v.extras["history"][0].get("created_by_bit")]
    t.check(len(made) == 104 and var["9512"].value == 1 and var["TYPE_WEAPON_09"].value == 2
            and var["audiomeldungen"].value == (1 | 1 << 15),
            "104 variables created by SetVarBit (value = mask, FUN_0049b840:384-399): "
            "9512=1, TYPE_WEAPON_09=2, audiomeldungen=0x8001")
    t.check(max(len(n) for n in var) <= 31, "no variable name exceeds the 32-byte buffer")
    pos = positions(b)
    t.check(len(pos) == 1438, "1,440 DefPos records -> 1,438 names (got %d)" % len(pos))
    p = pos["tpstart_dun2_rg1"]
    t.check((p.x, p.y, p.extras["declarations"], p.extras["record"]) == (4819, 357, 2, 157),
            "tpstart_dun2_rg1 declared twice (#149 3181,2671 then #157): later wins = (4819, 357)")
    t.check(pos["posTorfstecher"].extras["declarations"] == 2,
            "posTorfstecher declared twice (#15291, #15330, identical)")
    p = pos["tprein11b.3pos"]
    t.check((p.x, p.y, p.radius, p.z) == (1755, 1940, 0, 1), "tprein11b.3pos = (1755, 1940) R0 Z1")
    p = pos["aaa"]
    t.check((p.x, p.y, p.radius, p.z) == (223, 5023, 20, -1), "aaa = (223, 5023) R20, no Z")
    p = pos["pos_Hoehle9521"]
    t.check((p.x, p.y) == (-1264, -632), "pos_Hoehle9521 = (-1264, -632) (negative)")
    shapes = collections.Counter(
        "".join("s" if o == OP_STR else "i" for o, _ in operands(r.payload)[0])
        for r in recs if r.tag == TAG_DEFPOS)
    t.check((shapes["sii"], shapes["siii"], shapes["siiii"]) == (1030, 239, 171) and len(shapes) == 3,
            "record shapes sii 1030 / siii 239 / siiii 171 (got %s)" % dict(shapes))
    t.check(resolve(b, "HAUPTMANN3") == (3349, 2534, 0, -1), "resolve() is case-insensitive, z<0 -> 0")
    pf = positions(b, include_funkcode=True)
    t.check(len(pf) == 1921, "StartCode+FunkCode pre-scan = 1,921 positions (got %d)" % len(pf))
    ss = [operands(r.payload)[0] for r in records(b, "FunkCode.bin")
          if r.tag == TAG_DEFPOS and len(operands(r.payload)[0]) == 2]
    t.check(len(ss) == 348 and all(re.match(r"LOC_RG\d+_DUNGEONZIEL\d+$", a[0][1])
                                   and re.match(r"LOC_RG\d+_ZIEL\d+$", a[1][1]) for a in ss)
            and not any(p.extras.get("copied_from") for p in pf.values()),
            "348 FunkCode alias records, all LOC_RG<n>_DUNGEONZIEL<k> <- LOC_RG<n>_ZIEL<k>; "
            "none resolves statically")
    dl = dlgnpcs(b)
    t.check(len(dl) == 1022, "1,022 DlgNPC")
    print("== all 20 sources")
    for s in fs.resolve("all", default="all"):
        rs = records(s)                     # raises if the walk does not tile
        dl = dlgnpcs(s)
        ok_d = all(d.section_name is not None and fold(d.section_name) == fold("Dialog:" + d.name)
                   and d.handle == -1 and d.dlg_handle == 0 for d in dl)
        secs = vectoren.sections(s)
        bd = trigger_bindings(s)
        ok_b = all(fold(x.section_name or "") == fold("OMO%d%s" % (secs[x.section].quest, x.trigger))
                   for x in bd)
        cc = cache_check(s)
        if cc is None:
            cmsg = "no engine cache (no 0x4D2 magic) - skipped"
            ok_c = True
        else:
            ok_c = (cc["cache_positions"] == cc["reader_positions"] == cc["equal"]
                    and cc["order_equal"] and cc["cache_dlgnpc"] == cc["reader_dlgnpc"] + 1)
            cmsg = ("engine cache: %d/%d positions equal, order=%s, dlgnpc %d = %d+1"
                    % (cc["equal"], cc["cache_positions"], cc["order_equal"],
                       cc["cache_dlgnpc"], cc["reader_dlgnpc"]))
        t.check(ok_d and ok_b and ok_c,
                "%-20s recs=%5d DefPos=%4d DlgNPC=%4d (+0x44 == Dialog:<name>: %s) bindings=%3d "
                "(OMO rule: %s)  %s"
                % (s.key, len(rs), sum(1 for r in rs if r.tag == TAG_DEFPOS), len(dl), ok_d,
                   len(bd), ok_b, cmsg))
    print("\n%d checks, %d failed" % (t.n, t.fail))
    return t.fail == 0


# --------------------------------------------------------------------- CLI --
NEG_NOTE = """negative coordinates: the only consumers in the corpus are tag 0x5c records
(res:N, 04 fe ff ff ff <pos>, 67 "gECS_TELEPORT") and 2 addon CreateNPC records;
tag 0x2e Teleport refuses x<0 or y<0 (FUN_00491d40:154-159) while FUN_004a2b40
applies abs() to the converted target and places the figure directly when its
0x67 mode is 1 and FUN_006405d0() is false (:1096-1115).  Landing spot UNKNOWN."""


def main(argv=None):
    ap = argparse.ArgumentParser(description="StartCode.bin reader",
                                 epilog=NEG_NOTE,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sources", default=fs.BASELINE)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--positions", action="store_true")
    ap.add_argument("--position")
    ap.add_argument("--with-funkcode", action="store_true",
                    help="include FunkCode.bin DefPos records (engine pre-scan order)")
    ap.add_argument("--dlgnpcs", action="store_true")
    ap.add_argument("--variables", action="store_true")
    ap.add_argument("--pools", action="store_true")
    ap.add_argument("--triggers", action="store_true")
    ap.add_argument("--cache-check", action="store_true")
    ap.add_argument("--grep")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    if a.selftest:
        return 0 if selftest() else 1
    rx = re.compile(a.grep, re.I) if a.grep else None
    if not any([a.positions, a.position, a.dlgnpcs, a.variables, a.pools, a.triggers, a.cache_check]):
        a.summary = True
    for s in fs.resolve(a.sources, default=fs.BASELINE):
        rs = records(s)
        print("# %s  %s" % (s.key, s.file("StartCode.bin")))
        if a.summary:
            h = collections.Counter(r.tag for r in rs)
            print("  %d records; " % len(rs) + ", ".join("0x%02x:%d" % (t_, c) for t_, c in h.most_common()))
        if a.positions:
            for n, p in positions(s, a.with_funkcode).items():
                if rx and not rx.search(n):
                    continue
                print("  %-36s %6d %6d  R=%-3d Z=%-3d  %s#%d%s%s"
                      % (n, p.x, p.y, p.radius, p.z, p.extras["file"][:5], p.extras["record"],
                         "  alias-of " + p.extras["copied_from"] if p.extras.get("copied_from") else "",
                         "  (declared %dx)" % p.extras["declarations"] if p.extras["declarations"] > 1 else ""))
        if a.position:
            print("  %s -> %s" % (a.position, resolve(s, a.position, a.with_funkcode)))
        if a.dlgnpcs:
            for d in dlgnpcs(s):
                if rx and not rx.search(d.name):
                    continue
                print("  #%-5d %-36s section=%-6d %-40s marker=%d"
                      % (d.record, d.name, d.section, d.section_name, d.marker))
        if a.variables:
            for n, v in variables(s).items():
                if rx and not rx.search(n):
                    continue
                print("  %-28s = %-10s %s#%d%s" % (n, v.value, v.extras["kind"], v.extras["record"],
                                                    "  (conditional)" if v.extras["conditional"] else ""))
        if a.pools:
            for n, L in pool_positions(s).items():
                if rx and not rx.search(n):
                    continue
                print("  %-28s %3d positions  %s" % (n, len(L), [(x, y) for x, y, _, _ in L[:6]]))
        if a.triggers:
            for b in trigger_bindings(s):
                if rx and not rx.search(b.trigger):
                    continue
                print("  #%-5d %-36s -> section %-6d %s" % (b.record, b.trigger, b.section, b.section_name))
        if a.cache_check:
            print("  %s" % (cache_check(s),))
    return 0


if __name__ == "__main__":
    sys.exit(main())
