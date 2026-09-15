-- SacredSDK / lua / lib / npc_templates.lua  (AUTO-GENERATED)
--
-- Dev-authored CreateNPC archetypes extracted from the vanilla
-- campaign (bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin, 6118 tag-0x01
-- records). Each template is the EXACT ordered opcode list a dev
-- archetype uses; the per-instance slots (type/pos/name/level) are
-- filled by the helper. The emitted bytes drive the engine's OWN
-- CreateNPC handler FUN_00482510 at runtime (see
-- the SDK's RE notes on NPC templates (wiki: Reverse-Engineering) for the ABI / context object),
-- so spawned NPCs get the engine's full, correct init (HP, AI,
-- faction, combat-arts) with zero struct guessing.
--
-- Regenerate: the extractor in sdk/re/py/
--
-- Opcode atom forms in a template's `ops` list:
--   {0x02,'TYPE'}        creature type id  (hole: spawn type)
--   {0x02,'SUBID'}       2nd 0x02 = NPC sub/instance id (optional)
--   {0x02,'WEAPON'}      a 2nd 0x02 is an EQUIPMENT item type (RE_rewards_npc_world.md,
--                        CreateNPC table): vanilla's awake fighters carry 1729 sword x95,
--                        1712 dagger x80, 1724 bastard sword x52 (hole: opts.weapon)
--   {0x01,'NAME'}        unique name cstr  (hole: opts.name)
--   {0x04,'POS'}         position: i32 -2 + ASCIIZ, or {x, y[, z]} cells as three
--                        i32 (vanilla `04 3225 2768 0`) (hole: opts.pos)
--   {0x04,'POSI'}        position: numeric vx i32 (hole: opts.pos #)
--   {0x03,'LEVEL'}       level/orientation u16 (hole: opts.level)
--   {0x03,'FACING'}      facing in degrees u16 -- op 03 is the facing, not a level
--                        (MECHANICS 2.13); absent = random (hole: opts.facing)
--   {0x11,'GROUP'}       team/group id i32 (hole: opts.group)
--   {0x09,'LINK'}        link existing dlg NPC by name (hole)
--   {0x02,'ITEMS'}       every item of opts.items (or opts.weapon) as its own op 02
--   {0x90,'MOUNTLEVEL'}  a horse's op 0x90, i32 (hole: opts.level, default 1)
--   {op}                 bare flag/side opcode, literal (no hole)
--   {op,'#i32',v}/{op,'#u16',v}  literal-valued opcode (kept verbatim)
--   {0x00}               END
--
-- Usage:
--   local T   = require 'npc_templates'
--   local NPC = require 'npc'
--   local bytes = T.build('friendly_town_guard',
--                  { type=NPC.VALORIAN_SOLDIER, pos='CPOS:HERO',
--                    name='sdk_guard1', level=20 })
--   -- bytes = the full CreateNPC payload (flags byte + opcode
--   -- stream + END); feed to sacred.createnpc_engine(bytes) (the
--   -- engine-handler driver, npc_templates.md §B) OR bake via
--   -- npcspawn.record-style hex. T.names() lists archetypes.

local M = { templates = {} }

-- atom holes the builder fills from opts
M.HOLES = { TYPE='type', SUBID='sub_id', NAME='name', POS='pos',
            POSI='pos', LEVEL='level', GROUP='group', LINK='link', HOOK='hook' }

-- patrol_soldier : 130 vanilla records; common types: DeMordreyan Sharuka Captain, DeMordreyan Infantry, DeMordreyan Soldier, Valorian Soldier, Deserting Soldier
-- canonical @0x063bca : 02=None 02=None 04=None 11=None 08 12 00
M.templates['patrol_soldier'] = {
  archetype = 'patrol_soldier',
  vanilla_count = 130,
  src_offset = 0x063bca,
  top_types = { 'DeMordreyan Sharuka Captain', 'DeMordreyan Infantry', 'DeMordreyan Soldier', 'Valorian Soldier', 'Deserting Soldier', 'Freelancing Soldier of Hedgenton', 'Demon Soldier', 'Mascarellian Knight' },
  ops = {
    {0x02,'TYPE'}, {0x02,'SUBID'}, {0x04,'POS'}, {0x11,'GROUP'}, {0x08}, {0x12},
    {0x00},
  },
}

-- ally_companion : 1091 vanilla records; common types: Fading Spirit, Frost Goblin, Goblin Warrior, DeMordreyan Sharuka Warrior, Life-leecher of the Orcus
-- canonical @0x02ee8d : 02=None 02=None 04=None 08 12 00
M.templates['ally_companion'] = {
  archetype = 'ally_companion',
  vanilla_count = 1091,
  src_offset = 0x02ee8d,
  top_types = { 'Fading Spirit', 'Frost Goblin', 'Goblin Warrior', 'DeMordreyan Sharuka Warrior', 'Life-leecher of the Orcus', 'Poisonous Troll', 'Orcish Ghost Warrior', 'Ghost' },
  ops = {
    {0x02,'TYPE'}, {0x02,'SUBID'}, {0x04,'POS'}, {0x08}, {0x12}, {0x00},
  },
}

-- bellevue_enemy : 43 vanilla records; common types: Brigand, Slavecatcher, Orc Warrior, Crow, Sirithcam, Guardian of Fire
-- canonical @0x1ab785 : 02=None 04=None 12 00
M.templates['bellevue_enemy'] = {
  archetype = 'bellevue_enemy',
  vanilla_count = 43,
  src_offset = 0x1ab785,
  top_types = { 'Brigand', 'Slavecatcher', 'Orc Warrior', 'Crow', 'Sirithcam, Guardian of Fire', 'Dryad Druid', 'Orc Chieftain', 'Valorian Soldier' },
  ops = {
    {0x02,'TYPE'}, {0x04,'POS'}, {0x12}, {0x00},
  },
}

-- named_enemy : SDK-composed from vanilla's quest-monster ops, the shape of
-- base:SERAPHIM #2292 grab_special_ok (02 type, 01 'res:<name>', 04 pos, 08 12,
-- 05 '<death hook>'): a hostile, awake enemy with a script name (records can
-- address it: "res:<KEY>"), a group (GroupIsDead / SetGroupState) and a death
-- hook, the section the engine runs when it dies (VERBS_2_npc.md).
M.templates['named_enemy'] = {
  archetype = 'named_enemy',
  vanilla_count = 0,
  src_offset = 0x00aede,
  top_types = { 'Brigand' },
  ops = {
    {0x02,'TYPE'}, {0x01,'NAME'}, {0x04,'POS'}, {0x11,'GROUP'}, {0x08}, {0x12},
    {0x05,'HOOK'}, {0x00},
  },
}

-- named_guard : a named awake fighter, 01 'res:<name>' 02 type 04 pos [02 weapon]
-- 08 12 (1,851 named CreateNPC records in base:VAMPIRELADY carry 08 12). Side 08 + wake
-- 12 make the ENGINE arm its AI the way friendly_town_guard does, so it fights
-- (give it the ally stance 7 after the spawn, as the scene guards get). Unlike
-- quest_npc (side off, 0e) it does not stand and watch the hero.
M.templates['named_guard'] = {
  archetype = 'named_guard',
  vanilla_count = 1851,
  src_offset = 0,
  top_types = { 'Dark Elven Zhur-Urkahi' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x02,'WEAPON'}, {0x08}, {0x12}, {0x00},
  },
}

-- talk_guard : a guard you can talk to, Sergeant Flavius' record 1:1 (base:VAMPIRELADY
-- StartCode #13319): 01 'res:17460' 02 286 02 1729 04 3225 2768 0 03 45
-- 09 'ausregionwill' 46 0e 6b 1. Op 46 is the guard mode (+0x1F4 0x4000: it walks
-- about its home, goes for a target within 500, carries a torch at night), 0e with it
-- makes CreateNPC itself give the ally class 7, 6b 1 = +0x2B7 bit 8, and 09 binds the
-- DlgNPC node (declare it first) in the same record. No wake, no stance, no home
-- record after it: the engine does the rest. All 17 talkable guards in the corpus are
-- 46 0e; no talkable NPC at all is 08. Give pos as {x, y} so it is born at its post.
M.templates['talk_guard'] = {
  archetype = 'talk_guard',
  vanilla_count = 17,
  src_offset = 0x07595e,
  top_types = { 'Valorian Swordsman' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x02,'WEAPON'}, {0x04,'POS'}, {0x03,'FACING'},
    {0x09,'LINK'}, {0x46}, {0x0e}, {0x6b,'#u16',1}, {0x00},
  },
}

-- mount : 722 vanilla records (StartCode, the world's horses, types 550..554):
-- 01 'res:<name>' 02 type 04 pos [03 facing] 90 1. Op 0x90 is the creature's level
-- (FUN_00482510:1099-1108: raised to it through FUN_00564d60, byte +0x400) and takes
-- FOUR bytes: vanilla #1471 `... 03 5a 00 90 01 00 00 00`. Until 2026-09-15 this
-- template wrote two, and the reader took the END byte and whatever followed.
M.templates['mount'] = {
  archetype = 'mount',
  vanilla_count = 722,
  src_offset = 0x00bb15,
  top_types = { 'Horse' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x03,'FACING'}, {0x90,'MOUNTLEVEL'}, {0x00},
  },
}

-- horse_dealer : all 56 op-64 CreateNPC records of the corpus are named "Horse
-- Dealer" or "Orc Horse Master" (base StartCode #1470: 01 'res:17466' 02 288
-- 04 3360 2513 0 03 90 64). Op 64 sets +0x200 |= 0x4000000, the fourth service bit
-- (npc_ai_flags.md). The horses he sells are template mount plus one SetNPCState
-- each: `01 <horse> 63 <dealer> [1f <level>]` (#1473, #1481; Vb.ST.sold_by).
M.templates['horse_dealer'] = {
  archetype = 'horse_dealer',
  vanilla_count = 56,
  src_offset = 0x00af0d,
  top_types = { 'Citizen', 'Royal Pioneer', 'Haduk Merchant' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x03,'FACING'}, {0x64}, {0x00},
  },
}

-- skirmish_ally / skirmish_enemy : the five vanilla skirmishes (base StartCode
-- #13242-13269, `Scharmuetzel2..8`): one DefPos with radius 5 and both sides placed
-- on it, `02 type 02 item [02 item] 04 'Scharmuetzel4' 0e 46` for the king's men and
-- `... 08 46` for the orcs. Guard mode 46 on both sides, so they fight on the spot
-- (0e 46 = ally class 7, 08 46 = monster class 0xE). NAME and GROUP are optional;
-- with them the shape is DeMordrey's Sharuka (`01 11 02 02 [02] 04 0e 46`, #14941).
-- Use NPCo.skirmish (npcobj), which declares the position first.
M.templates['skirmish_ally'] = {
  archetype = 'skirmish_ally',
  vanilla_count = 23,
  src_offset = 0x074e71,
  top_types = { 'Royal Pioneer', 'Fealtybound Knight' },
  ops = {
    {0x01,'NAME'}, {0x11,'GROUP'}, {0x02,'TYPE'}, {0x02,'ITEMS'}, {0x04,'POS'}, {0x0e}, {0x46}, {0x00},
  },
}
M.templates['skirmish_enemy'] = {
  archetype = 'skirmish_enemy',
  vanilla_count = 25,
  src_offset = 0x074e71,
  top_types = { 'Orc Warrior', 'Goblin Warrior', 'Morgwath of the Dark Elves' },
  ops = {
    {0x01,'NAME'}, {0x11,'GROUP'}, {0x02,'TYPE'}, {0x02,'ITEMS'}, {0x04,'POS'}, {0x08}, {0x46}, {0x00},
  },
}

-- dormant_group : SDK-composed. The vanilla ambush: enemies placed with the
-- side switched off (0e, the townsperson / dormant_enemy shape) in a group, so
-- they stand around peacefully until one SetGroupState `11 <group> 08 12` turns
-- the whole group hostile and awake (vanilla btn_accept_10253_start). NAME and
-- HOOK are optional: give them to the one the quest talks to or waits to die.
M.templates['dormant_group'] = {
  archetype = 'dormant_group',
  vanilla_count = 0,
  src_offset = 0x061caf,
  top_types = { 'Brigand', 'Slaver', 'Slavecatcher' },
  ops = {
    {0x02,'TYPE'}, {0x01,'NAME'}, {0x04,'POS'}, {0x11,'GROUP'}, {0x0e},
    {0x05,'HOOK'}, {0x00},
  },
}

-- dormant_enemy : 954 vanilla records; common types: Orc Warrior, Royal Pioneer, Lesser Gargoyle, Sakkara Priest, Valorian Swordsman
-- canonical @0x1a74cb : 02=None 02=None 04=None 0e 00
M.templates['dormant_enemy'] = {
  archetype = 'dormant_enemy',
  vanilla_count = 954,
  src_offset = 0x1a74cb,
  top_types = { 'Orc Warrior', 'Royal Pioneer', 'Lesser Gargoyle', 'Sakkara Priest', 'Valorian Swordsman', 'Valorian Soldier', 'Skeleton', 'Winged Varanidae' },
  ops = {
    {0x02,'TYPE'}, {0x02,'SUBID'}, {0x04,'POS'}, {0x0e}, {0x00},
  },
}

-- townsperson : 1330 vanilla records; common types: Farmer, Citizen, Child, Chicken, Pig
-- canonical @0x061caf : 02=None 04=None 11=None 0e 00
M.templates['townsperson'] = {
  archetype = 'townsperson',
  vanilla_count = 1330,
  src_offset = 0x061caf,
  top_types = { 'Farmer', 'Citizen', 'Child', 'Chicken', 'Pig', 'Cow', 'Darethian', 'Haduk' },
  ops = {
    {0x02,'TYPE'}, {0x04,'POS'}, {0x11,'GROUP'}, {0x0e}, {0x00},
  },
}

-- ambient_animal : 15 vanilla records; common types: Horse, Chicken, Cow
-- canonical @0x0528b9 : 01=None 02=None 04=None 0e 00
-- Service NPCs. The flag opcode is what makes the shop window open: CreateNPC
-- op 0x23 sets creature+0x200 bit 0x1000 (blacksmith), 0x24 bit 0x2000
-- (merchant) and 0x25 bit 0x4000 (combat-art master); the talk path tests the
-- composite mask 0x4007000 and opens UI_WND_BLACKSMITH / _MERCHANT / _MASTER
-- (re/npc_ai_flags.md 1-2). Vanilla example (StartCode @0x066ded):
--   01 'res:17520' | 02 <type> | 04 <pos> | 11 <group> | 24
M.templates['merchant'] = {
  archetype = 'merchant',
  vanilla_count = 524,
  src_offset = 0x066ded,
  top_types = { 'Merchant', 'Darethian Merchant', 'Haduk Merchant' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x11,'GROUP'}, {0x24}, {0x0e}, {0x00},
  },
}

M.templates['smith'] = {
  archetype = 'smith',
  vanilla_count = 346,
  src_offset = 0x066e11,
  top_types = { 'Smith', 'Darethian Blacksmith' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x11,'GROUP'}, {0x23}, {0x0e}, {0x00},
  },
}

M.templates['trainer'] = {
  archetype = 'trainer',
  vanilla_count = 269,
  src_offset = 0x072326,
  top_types = { 'Master of Combat Arts', 'Combo Master' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x11,'GROUP'}, {0x25}, {0x0e}, {0x00},
  },
}

M.templates['ambient_animal'] = {
  archetype = 'ambient_animal',
  vanilla_count = 15,
  src_offset = 0x0528b9,
  top_types = { 'Horse', 'Chicken', 'Cow' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x0e}, {0x00},
  },
}

-- quest_npc : 474 vanilla records; common types: Farmer, Citizen, Chicken, Wargh of Zhurag-Nar, Thief
-- canonical @0x062101 : 01=None 02=None 04=None 09=None 0e 00
M.templates['quest_npc'] = {
  archetype = 'quest_npc',
  vanilla_count = 474,
  src_offset = 0x062101,
  top_types = { 'Farmer', 'Citizen', 'Chicken', 'Wargh of Zhurag-Nar', 'Thief', 'Nobleman', 'Cow', 'Child' },
  ops = {
    {0x01,'NAME'}, {0x02,'TYPE'}, {0x04,'POS'}, {0x09,'LINK'}, {0x0e}, {0x00},
  },
}

-- friendly_town_guard : 91 vanilla records; common types: DeMordreyan Sharuka Captain, DeMordreyan Infantry, DeMordreyan Soldier, Freelancing Soldier of Hedgenton, Deserting Soldier
-- canonical @0x02ab97 : 02=None 04=None 02=None 08 12 00
M.templates['friendly_town_guard'] = {
  archetype = 'friendly_town_guard',
  vanilla_count = 91,
  src_offset = 0x02ab97,
  top_types = { 'DeMordreyan Sharuka Captain', 'DeMordreyan Infantry', 'DeMordreyan Soldier', 'Freelancing Soldier of Hedgenton', 'Deserting Soldier', 'Demon Soldier', 'Mascarellian Knight', 'Valorian Soldier' },
  ops = {
    {0x02,'TYPE'}, {0x04,'POS'}, {0x02,'SUBID'}, {0x08}, {0x12}, {0x00},
  },
}

-- SDK-curated proactive defender. Same as friendly_town_guard but side
-- opcode 0x2b instead of 0x08 → the engine sets cCreature+0x1F4 = 9
-- (bit0 awake + 0x8 side) at CreateNPC, so the AI proactive picker
-- FUN_00542b20 passes its `+0x1F4 & 0x1006dcf8 != 0` gate and engages
-- hostiles from the full 800-unit radius (vanilla-soldier behaviour).
-- The 0x08+0x12 combo gave +0x1F4=1 (bit0 only) → permanently passive /
-- point-blank only (combat_init.md "Aggro/detection range"). Pair with
-- o:stance(1,7) for ally friend/foe (never the hero).
M.templates['proactive_guard'] = {
  archetype = 'proactive_guard',
  vanilla_count = 0,            -- SDK-curated (0x08→0x2b of friendly_town_guard)
  ops = {
    {0x02,'TYPE'}, {0x04,'POS'}, {0x02,'SUBID'}, {0x2b}, {0x12}, {0x00},
  },
}


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
    elseif tag == 'WEAPON' then
      if opts.weapon then p[#p+1] = u8(0x02) .. le32(opts.weapon) end
    elseif tag == 'ITEMS' then                -- every item as its own op 02 (opts.items, or opts.weapon)
      for _, it in ipairs(opts.items or { opts.weapon }) do p[#p+1] = u8(0x02) .. le32(it) end
    elseif tag == 'MOUNTLEVEL' then          -- op 0x90: i32, 1 when not given
      p[#p+1] = u8(0x90) .. le32(opts.level or 1)
    elseif tag == 'NAME' then
      if opts.name then p[#p+1] = u8(0x01) .. cstr(opts.name) end
    elseif tag == 'LINK' then
      if opts.link then p[#p+1] = u8(0x09) .. cstr(opts.link) end
    elseif tag == 'POS' then
      local pos = opts.pos or "CPOS:HERO"
      if type(pos) == "number" then
        p[#p+1] = u8(0x04) .. le32(pos)
      elseif type(pos) == "table" then
        p[#p+1] = u8(0x04) .. le32(pos[1]) .. le32(pos[2]) .. le32(pos[3] or 0)
      else
        p[#p+1] = u8(0x04) .. le32(-2) .. cstr(pos)
      end
    elseif tag == 'POSI' then
      local pos = opts.pos or 0
      p[#p+1] = u8(0x04) .. le32(pos)
    elseif tag == 'LEVEL' then
      if opts.level then p[#p+1] = u8(0x03) .. le16(opts.level) end
    elseif tag == 'FACING' then
      if opts.facing then p[#p+1] = u8(0x03) .. le16(opts.facing) end
    elseif tag == 'GROUP' then
      if opts.group then p[#p+1] = u8(0x11) .. le32(opts.group) end
    elseif tag == 'HOOK' then                -- op 0x05: the section run when it dies
      if opts.hook then p[#p+1] = u8(0x05) .. cstr(opts.hook) end
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

return M
