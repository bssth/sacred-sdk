#!/usr/bin/env python3
# npc_templates_extract.py - extract every tag-0x01 CreateNPC record from the
# Vampiress campaign FunkCode.bin, decode the opcode stream (npc_decode2.py
# grammar), cluster into dev-authored ARCHETYPES by side/flag opcode signature,
# and emit:
#   * a stats report (stdout / npc_templates_stats.txt)
#   * custom/lua/lib/npc_templates.lua  (named templates = exact ordered opcode
#     lists + metadata + helper accessors)
#
# A "template" is the canonical ordered opcode list a dev archetype uses, with
# the per-instance value slots (Type, position, name) left as named holes the
# Lua helper fills. The bytes a template emits feed the engine's OWN CreateNPC
# handler (FUN_00482510) at runtime — see npc_templates.md for the ABI.
#
# Run:  python npc_templates_extract.py
import struct, collections, re, io, sys, os

BIN  = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin"
NPCL = r"E:\SteamLibrary\steamapps\common\Sacred Gold\custom\lua\lib\npc.lua"
LUA  = r"E:\SteamLibrary\steamapps\common\Sacred Gold\custom\lua\lib\npc_templates.lua"
STAT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\scratch\npc_templates_stats.txt"

# ---- npc.lua id -> name ---------------------------------------------------
def load_npc_names():
    m = {}
    try:
        txt = open(NPCL, "r", encoding="utf-8", errors="replace").read()
    except OSError:
        return m
    for mo in re.finditer(
            r'add\(\s*(\d+)\s*,\s*"[0-9A-Fa-f]+"\s*,\s*"([^"]*)"', txt):
        m[int(mo.group(1))] = mo.group(2)
    return m
NPC = load_npc_names()

# ---- opcode width grammar (FUN_00472bc0; npc_model.md / npc_decode2.py) ----
W4  = {0x02, 0x11, 0x36, 0x75}
W4P = {0x04, 0x0c, 0x0d, 0x2a}
W2  = {0x03, 0x0a, 0x6b, 0x6c, 0x93, 0x9b, 0x9c}
W8  = {0x15}
W12 = {0x19}
WS  = {0x01, 0x29, 0x63, 0x68, 0x69, 0x6a, 0x9d, 0x16, 0x05, 0x09}

# side / behaviour flag opcodes (no payload), npc_model.md faction table
SIDE = {
    0x08: "ally(local_6c8=1)",   0x0e: "neutral(local_6c8=0)",
    0x2b: "side(EBX=8)",         0x2e: "side(EBX|=0x10)",
    0x2f: "ENEMY(EBX=0x40)",     0x31: "ENEMY(EBX=0x40)",
    0x30: "side(EBX=0x20)",      0x32: "side(EBX=0x80)",
    0x42: "side(EBX=0x400)",
}
FLAG = {0x12: "awake/WakeUp", 0x09: "link-existing-dlgNPC",
        0xa1: "invulnerable", 0x6b: "stationary",
        0x72: "aux", 0x74: "aux", 0x1a: "aux", 0x46: "aux"}

def decode(pl):
    """Return list of (op, kind, value). kind in
    {i32,pos,posi,u16,rect,3i,cstr,flag,END}."""
    ops = []; off = 1; n = len(pl)
    while off < n:
        op = pl[off]
        if op == 0:
            ops.append((0, "END", None)); break
        if op in W4:
            if off + 5 > n: ops.append((op, "TRUNC", None)); break
            ops.append((op, "i32", struct.unpack_from("<i", pl, off+1)[0]))
            off += 5
        elif op in W4P:
            if off + 5 > n: ops.append((op, "TRUNC", None)); break
            v = struct.unpack_from("<i", pl, off+1)[0]
            if v == -2:
                e = pl.find(b"\0", off+5); e = n if e < 0 else e
                ops.append((op, "pos", pl[off+5:e].decode("latin1", "replace")))
                off = e + 1
            else:
                ops.append((op, "posi", v)); off += 5
        elif op in W2:
            if off + 3 > n: ops.append((op, "TRUNC", None)); break
            ops.append((op, "u16", struct.unpack_from("<H", pl, off+1)[0]))
            off += 3
        elif op in W8:
            if off + 9 > n: ops.append((op, "TRUNC", None)); break
            ops.append((op, "rect", struct.unpack_from("<4H", pl, off+1)))
            off += 9
        elif op in W12:
            if off + 13 > n: ops.append((op, "TRUNC", None)); break
            ops.append((op, "3i", struct.unpack_from("<3i", pl, off+1)))
            off += 13
        elif op in WS:
            e = pl.find(b"\0", off+1); e = n if e < 0 else e
            ops.append((op, "cstr", pl[off+1:e].decode("latin1", "replace")))
            off = e + 1
        else:
            ops.append((op, "flag", None)); off += 1
    return ops

def walk(d):
    off = 0; cn = []
    while off + 3 <= len(d):
        tag = d[off]; size = (d[off+1] << 8) | d[off+2]
        if size < 3 or off + size > len(d):
            break
        if tag == 1:
            cn.append((off, d[off+3:off+size]))
        off += size
    return cn

def first_type(ops):
    for op, k, v in ops:
        if op == 0x02:
            return v
    return None

def is_dynamic(ops):
    """Dynamic-quest (DQ) generated spawn, not a hand-placed dev NPC."""
    for op, k, v in ops:
        if op == 0x01 and isinstance(v, str) and (
                v.startswith("res:") or "DQ" in v or "AUFTRAG" in v):
            return True
        if op == 0x04 and isinstance(v, str) and (
                v == "aaa" or v.startswith("POS_AUFTRAG")):
            return True
    return False

def side_sig(ops):
    return tuple(op for op, k, v in ops
                 if op in SIDE or op in (0x12, 0x09, 0xa1, 0x6b))

def op_sig(ops):
    return tuple(op for op, k, v in ops)

# ---------------------------------------------------------------------------
# Archetype classifier — by side/flag signature + type family.
#   friendly_town_guard : ally soldier, side 0x08 (+ 0x12 awake)
#   patrol_soldier       : soldier type, side 0x08, no 0x6b stationary
#   bellevue_enemy       : hostile monster, hand-placed, no ally side
#   quest_npc            : 0x09 dialog link OR unique 0x01 name + concrete pos
#   townsperson          : Citizen/Farmer/Nobleman/Child, side 0x2b/0x42/0x30/0x2e
# ---------------------------------------------------------------------------
SOLDIER_NAMES = ("Soldier", "Infantry", "Guard", "Pioneer", "Sentry",
                 "Officer", "Captain", "Knight")
TOWN_NAMES    = ("Citizen", "Farmer", "Nobleman", "Child", "Townsman",
                  "Townswoman", "Beggar", "Maid", "Servant")
ANIMAL_NAMES  = ("Cow", "Pig", "Chicken", "Sheep", "Horse", "Dog")

def archetype(ops):
    t = first_type(ops)
    nm = NPC.get(t, "")
    ss = set(side_sig(ops))
    has_ally  = 0x08 in ss
    has_awake = 0x12 in ss
    has_link  = 0x09 in ss
    has_stat  = 0x6b in ss
    is_soldier = any(s in nm for s in SOLDIER_NAMES)
    is_town    = any(s in nm for s in TOWN_NAMES)
    is_animal  = any(s in nm for s in ANIMAL_NAMES)
    enemy_side = bool(ss & {0x2f, 0x31})
    town_side  = bool(ss & {0x2b, 0x42, 0x30, 0x2e})

    if has_link:
        return "quest_npc"
    if is_soldier and has_ally:
        return "friendly_town_guard" if has_stat else "patrol_soldier"
    if has_ally and has_awake:
        return "ally_companion"
    if is_town or town_side:
        return "townsperson"
    if is_animal:
        return "ambient_animal"
    if enemy_side or (not has_ally and has_awake):
        return "bellevue_enemy"
    if not has_ally and not has_awake and not town_side:
        return "dormant_enemy"
    return "misc"

# ---------------------------------------------------------------------------
def fmt_ops(ops):
    out = []
    for op, k, v in ops:
        if k in ("flag", "END"):
            out.append("%02x" % op)
        else:
            out.append("%02x=%s" % (op, v))
    return " ".join(out)

def pick_canonical(records):
    """records: list of (off, ops). Return the ops whose full op signature is
    the single most common in the cluster (the dev 'template')."""
    bysig = collections.Counter(op_sig(o) for _, o in records)
    best = bysig.most_common(1)[0][0]
    for off, ops in records:
        if op_sig(ops) == best:
            return off, ops, bysig.most_common(1)[0][1], len(bysig)
    off, ops = records[0]
    return off, ops, 0, len(bysig)

# Hand-curated MINIMAL canonical templates — the cleanest dev form per
# archetype (verified by scanning the simplest real records, see
# npc_templates.md). These are what the Lua templates emit; the auto
# clusters above are the statistical backing. Each value is the ordered
# (op, kind, value) list with holes (value None for filled-by-opts slots).
CURATED = {
    # ally town guard / patrol soldier: Type, position, sub-id, side 0x08
    # (ally), 0x12 (awake -> WakeUp), END.  e.g. @0x02ab97 Demon Soldier,
    # @0x057645 Sharuka Captain.
    "friendly_town_guard": [
        (0x02, "i32", None), (0x04, "pos", None), (0x02, "i32", None),
        (0x08, "flag", None), (0x12, "flag", None), (0x00, "END", None)],
    # same but with a team/group id (squad patrol). @0x0619a9.
    "patrol_soldier": [
        (0x02, "i32", None), (0x02, "i32", None), (0x04, "pos", None),
        (0x11, "i32", None), (0x08, "flag", None), (0x12, "flag", None),
        (0x00, "END", None)],
    # hand-placed hostile (Bellevue / dungeon): Type, position, 0x12 awake,
    # END.  Monster class default = hostile; no ally side.  @0x06deea form.
    "bellevue_enemy": [
        (0x02, "i32", None), (0x04, "pos", None), (0x12, "flag", None),
        (0x00, "END", None)],
    # dormant/ambush enemy: Type, sub-id, position, 0x0e neutral-default
    # (wakes on aggro/trigger, not pre-awake).  @0x1a74cb family.
    "dormant_enemy": [
        (0x02, "i32", None), (0x02, "i32", None), (0x04, "pos", None),
        (0x0e, "flag", None), (0x00, "END", None)],
    # quest NPC: unique name, Type, position, 0x09 link to existing dialog
    # NPC, 0x0e neutral.  @0x06306e Noble Lady form.
    "quest_npc": [
        (0x01, "cstr", None), (0x02, "i32", None), (0x04, "pos", None),
        (0x09, "cstr", None), (0x0e, "flag", None), (0x00, "END", None)],
    # townsperson: Type, position, group id, 0x0e neutral.  @0x061caf.
    "townsperson": [
        (0x02, "i32", None), (0x04, "pos", None), (0x11, "i32", None),
        (0x0e, "flag", None), (0x00, "END", None)],
    # ambient animal: name, Type, position, 0x0e neutral.
    "ambient_animal": [
        (0x01, "cstr", None), (0x02, "i32", None), (0x04, "pos", None),
        (0x0e, "flag", None), (0x00, "END", None)],
    # ally companion (awake fighter that follows/defends, non-soldier type):
    # Type, sub-id, position, 0x08 ally, 0x12 awake, END.  @0x02ee8d.
    "ally_companion": [
        (0x02, "i32", None), (0x02, "i32", None), (0x04, "pos", None),
        (0x08, "flag", None), (0x12, "flag", None), (0x00, "END", None)],
}

def main():
    d = open(BIN, "rb").read()
    cn = walk(d)

    clusters = collections.defaultdict(list)        # archetype -> [(off,ops)]
    hand = 0
    for off, pl in cn:
        ops = decode(pl)
        if is_dynamic(ops):
            clusters["_dynamic_quest_gen"].append((off, ops))
            continue
        hand += 1
        clusters[archetype(ops)].append((off, ops))

    S = io.StringIO()
    P = lambda *a: print(*a, file=S)
    P("# CreateNPC archetype extraction — %d tag-0x01 records "
      "(%d hand-placed, %d dynamic-quest-gen)"
      % (len(cn), hand, len(clusters["_dynamic_quest_gen"])))
    P("# Source: bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin")
    P("")
    # side/flag opcode global histogram
    sh = collections.Counter()
    for off, pl in cn:
        for op, k, v in decode(pl):
            if op in SIDE or op in FLAG:
                sh[op] += 1
    P("## side / flag opcode frequency (all %d records)" % len(cn))
    for op, c in sorted(sh.items()):
        P("  0x%02x  %-22s  %d"
          % (op, SIDE.get(op) or FLAG.get(op, "?"), c))
    P("")
    P("## archetype clusters")
    P("%-22s %7s  %s" % ("archetype", "count", "canonical opcode signature"))
    templates = []
    order = ["friendly_town_guard", "patrol_soldier", "ally_companion",
             "bellevue_enemy", "dormant_enemy", "townsperson",
             "ambient_animal", "quest_npc", "misc"]
    for arch in order:
        recs = clusters.get(arch)
        if not recs:
            continue
        off, ops, freq, distinct = pick_canonical(recs)
        types = collections.Counter()
        for _, oo in recs:
            t = first_type(oo)
            types[NPC.get(t, "#%s" % t)] += 1
        P("%-22s %7d  @0x%06x  %s"
          % (arch, len(recs), off,
             " ".join("%02x" % op for op, k, v in ops)))
        P("    most-common-sig %d/%d (%d distinct sigs)  top-types: %s"
          % (freq, len(recs), distinct,
             ", ".join("%s x%d" % (n, c) for n, c in types.most_common(6))))
        P("    most-common decoded: %s" % fmt_ops(ops))
        # Emit the hand-curated MINIMAL canonical form (the dev template).
        cur = CURATED.get(arch)
        if cur:
            P("    >> CURATED template: %s"
              % " ".join("%02x" % op for op, k, v in cur))
            templates.append((arch, off, cur, len(recs),
                              types.most_common(8)))
        else:
            templates.append((arch, off, ops, len(recs),
                              types.most_common(8)))
    # Curated archetypes with NO direct auto-cluster (e.g. friendly_town_guard
    # — its records fold into patrol_soldier's cluster). Emit them too, with
    # stats borrowed from the closest cluster for context.
    emitted = {a for a, *_ in templates}
    SOLDIER_REC = clusters.get("patrol_soldier", [])
    for arch, cur in CURATED.items():
        if arch in emitted:
            continue
        # gather soldier-family types for the guard template's metadata
        if arch == "friendly_town_guard":
            types = collections.Counter()
            for _, oo in SOLDIER_REC:
                ss = set(side_sig(oo))
                if 0x08 in ss and 0x12 in ss:
                    t = first_type(oo)
                    types[NPC.get(t, "#%s" % t)] += 1
            n = sum(types.values())
            P("%-22s %7d  (curated; folds into patrol_soldier)  %s"
              % (arch, n, " ".join("%02x" % op for op, k, v in cur)))
            P("    >> CURATED template: %s"
              % " ".join("%02x" % op for op, k, v in cur))
            templates.append((arch, 0x02ab97, cur, n, types.most_common(8)))
        else:
            templates.append((arch, 0, cur, 0, []))
    P("")

    # ---- emit npc_templates.lua -------------------------------------------
    L = io.StringIO()
    W = lambda *a: print(*a, file=L)
    W("-- SacredSDK / lua / lib / npc_templates.lua  (AUTO-GENERATED)")
    W("--")
    W("-- Dev-authored CreateNPC archetypes extracted from the vanilla")
    W("-- campaign (bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin, %d tag-0x01"
      % len(cn))
    W("-- records). Each template is the EXACT ordered opcode list a dev")
    W("-- archetype uses; the per-instance slots (type/pos/name/level) are")
    W("-- filled by the helper. The emitted bytes drive the engine's OWN")
    W("-- CreateNPC handler FUN_00482510 at runtime (see")
    W("-- sdk/tools/scratch/npc_templates.md for the ABI / context object),")
    W("-- so spawned NPCs get the engine's full, correct init (HP, AI,")
    W("-- faction, combat-arts) with zero struct guessing.")
    W("--")
    W("-- Regenerate: python sdk/tools/scratch/npc_templates_extract.py")
    W("--")
    W("-- Opcode atom forms in a template's `ops` list:")
    W("--   {0x02,'TYPE'}        creature type id  (hole: spawn type)")
    W("--   {0x02,'SUBID'}       2nd 0x02 = NPC sub/instance id (optional)")
    W("--   {0x01,'NAME'}        unique name cstr  (hole: opts.name)")
    W("--   {0x04,'POS'}         position: i32 -2 + ASCIIZ (hole: opts.pos)")
    W("--   {0x04,'POSI'}        position: numeric vx i32 (hole: opts.pos #)")
    W("--   {0x03,'LEVEL'}       level/orientation u16 (hole: opts.level)")
    W("--   {0x11,'GROUP'}       team/group id i32 (hole: opts.group)")
    W("--   {0x09,'LINK'}        link existing dlg NPC by name (hole)")
    W("--   {op}                 bare flag/side opcode, literal (no hole)")
    W("--   {op,'#i32',v}/{op,'#u16',v}  literal-valued opcode (kept verbatim)")
    W("--   {0x00}               END")
    W("--")
    W("-- Usage:")
    W("--   local T   = require 'npc_templates'")
    W("--   local NPC = require 'npc'")
    W("--   local bytes = T.build('friendly_town_guard',")
    W("--                  { type=NPC.VALORIAN_SOLDIER, pos='CPOS:HERO',")
    W("--                    name='sdk_guard1', level=20 })")
    W("--   -- bytes = the full CreateNPC payload (flags byte + opcode")
    W("--   -- stream + END); feed to sacred.createnpc_engine(bytes) (the")
    W("--   -- engine-handler driver, npc_templates.md §B) OR bake via")
    W("--   -- npcspawn.record-style hex. T.names() lists archetypes.")
    W("")
    W("local M = { templates = {} }")
    W("")
    W("-- atom holes the builder fills from opts")
    W("M.HOLES = { TYPE='type', SUBID='sub_id', NAME='name', POS='pos',")
    W("            POSI='pos', LEVEL='level', GROUP='group', LINK='link' }")
    W("")

    def atomize(ops):
        atoms = []
        seen_type = False
        for op, k, v in ops:
            if op == 0x00:
                atoms.append("{0x00}")
            elif op == 0x02 and k == "i32":
                if not seen_type:
                    atoms.append("{0x02,'TYPE'}"); seen_type = True
                else:
                    atoms.append("{0x02,'SUBID'}")
            elif op == 0x01 and k == "cstr":
                atoms.append("{0x01,'NAME'}")
            elif op == 0x04 and k == "pos":
                atoms.append("{0x04,'POS'}")
            elif op == 0x04 and k == "posi":
                atoms.append("{0x04,'POSI'}")
            elif op == 0x03 and k == "u16":
                atoms.append("{0x03,'LEVEL'}")
            elif op == 0x11 and k == "i32":
                atoms.append("{0x11,'GROUP'}")
            elif op == 0x09 and k == "cstr":
                atoms.append("{0x09,'LINK'}")
            elif k == "flag":
                atoms.append("{0x%02x}" % op)
            elif k == "i32":
                atoms.append("{0x%02x,'#i32',%d}" % (op, v))
            elif k == "u16":
                atoms.append("{0x%02x,'#u16',%d}" % (op, v))
            elif k == "posi":
                atoms.append("{0x%02x,'#i32',%d}" % (op, v))
            elif k == "cstr":
                atoms.append("{0x%02x,'#cstr',%r}" % (op, v))
            elif k == "pos":
                atoms.append("{0x%02x,'#pos',%r}" % (op, v))
            elif k == "rect":
                atoms.append("{0x%02x,'#rect',{%s}}"
                             % (op, ",".join(str(x) for x in v)))
            elif k == "3i":
                atoms.append("{0x%02x,'#3i',{%s}}"
                             % (op, ",".join(str(x) for x in v)))
            else:
                atoms.append("{0x%02x}" % op)
        return atoms

    for arch, off, ops, count, toptypes in templates:
        if arch == "misc":
            continue                       # not a clean reusable template
        atoms = atomize(ops)
        tt = ", ".join('%s' % n for n, c in toptypes[:5])
        W("-- %s : %d vanilla records; common types: %s"
          % (arch, count, tt))
        W("-- canonical @0x%06x : %s" % (off, fmt_ops(ops)))
        W("M.templates[%r] = {" % arch)
        W("  archetype = %r," % arch)
        W("  vanilla_count = %d," % count)
        W("  src_offset = 0x%06x," % off)
        W("  top_types = { %s }," % ", ".join("%r" % n for n, c in toptypes))
        W("  ops = {")
        for i in range(0, len(atoms), 6):
            W("    " + ", ".join(atoms[i:i+6]) + ",")
        W("  },")
        W("}")
        W("")

    # builder + accessors
    W(r"""
-- ---- byte packers ----------------------------------------------------
local function u8(b)  return string.char(b % 256) end
local function le16(v) v=v%0x10000; return string.char(v%256, math.floor(v/256)%256) end
local function le32(v)
  if v < 0 then v = v + 0x100000000 end
  return string.char(v%256, math.floor(v/0x100)%256,
                     math.floor(v/0x10000)%256, math.floor(v/0x1000000)%256)
end
local function cstr(s) return (s or "") .. "\0" end

-- Build the full CreateNPC payload bytes (leading flags byte 0x00 + the
-- template's opcode stream with holes filled from `opts` + END) for the
-- named archetype. `opts` = { type=, pos=, name=, level=, group=, sub_id=,
-- link= }. Unfilled optional holes (no opts value) are SKIPPED; required
-- TYPE/POS must be supplied (or the template carried a literal).
function M.build(name, opts)
  local t = M.templates[name]
  assert(t, "npc_templates: unknown archetype '"..tostring(name).."'")
  opts = opts or {}
  local p = { u8(0x00) }                  -- leading flags byte
  for _, atom in ipairs(t.ops) do
    local op  = atom[1]
    local tag = atom[2]
    if op == 0x00 then
      p[#p+1] = u8(0x00)
    elseif tag == nil then                 -- bare flag/side opcode
      p[#p+1] = u8(op)
    elseif tag == 'TYPE' then
      assert(opts.type, "npc_templates: opts.type required")
      p[#p+1] = u8(0x02) .. le32(opts.type)
    elseif tag == 'SUBID' then
      if opts.sub_id then p[#p+1] = u8(0x02) .. le32(opts.sub_id) end
    elseif tag == 'NAME' then
      if opts.name then p[#p+1] = u8(0x01) .. cstr(opts.name) end
    elseif tag == 'LINK' then
      if opts.link then p[#p+1] = u8(0x09) .. cstr(opts.link) end
    elseif tag == 'POS' then
      local pos = opts.pos or "CPOS:HERO"
      if type(pos) == "number" then
        p[#p+1] = u8(0x04) .. le32(pos)
      else
        p[#p+1] = u8(0x04) .. le32(-2) .. cstr(pos)
      end
    elseif tag == 'POSI' then
      local pos = opts.pos or 0
      p[#p+1] = u8(0x04) .. le32(pos)
    elseif tag == 'LEVEL' then
      if opts.level then p[#p+1] = u8(0x03) .. le16(opts.level) end
    elseif tag == 'GROUP' then
      if opts.group then p[#p+1] = u8(0x11) .. le32(opts.group) end
    elseif tag == '#i32' then
      p[#p+1] = u8(op) .. le32(atom[3])
    elseif tag == '#u16' then
      p[#p+1] = u8(op) .. le16(atom[3])
    elseif tag == '#cstr' then
      p[#p+1] = u8(op) .. cstr(atom[3])
    elseif tag == '#pos' then
      p[#p+1] = u8(op) .. le32(-2) .. cstr(atom[3])
    elseif tag == '#rect' then
      local b = u8(op)
      for _, x in ipairs(atom[3]) do b = b .. le16(x) end
      p[#p+1] = b
    elseif tag == '#3i' then
      local b = u8(op)
      for _, x in ipairs(atom[3]) do b = b .. le32(x) end
      p[#p+1] = b
    end
  end
  -- ensure trailing END
  if p[#p] ~= u8(0x00) then p[#p+1] = u8(0x00) end
  return table.concat(p)
end

-- Hex of M.build (for baking / inspection).
function M.build_hex(name, opts)
  return (M.build(name, opts):gsub(".",
           function(c) return ("%02x"):format(c:byte()) end))
end

function M.get(name) return M.templates[name] end
function M.names()
  local r = {}
  for k in pairs(M.templates) do r[#r+1] = k end
  table.sort(r); return r
end

return M""")

    open(STAT, "w", encoding="utf-8").write(S.getvalue())
    open(LUA,  "w", encoding="utf-8").write(L.getvalue())
    print("wrote %s" % STAT)
    print("wrote %s" % LUA)
    print(S.getvalue())

if __name__ == "__main__":
    main()
