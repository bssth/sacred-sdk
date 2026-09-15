-- SacredSDK / lua / lib / classmod.lua
--
-- Repurpose a playable class slot: its name, its class-select text, its body
-- and how that body is shown. Data goes through the engine's own loaders at
-- bake time; what the exe hardcodes is changed by guarded DLL patches, applied
-- once the build is verified and only over the expected vanilla bytes:
--
--   * name / label / description  -> global.res slots, via lib/text.lua
--   * body                         -> the slot's hero TYPE record in
--                                     pak/Items.pak, rewritten into
--                                     custom/pak/Items.pak (fs_override
--                                     serves it instead of the vanilla file)
--   * body's empty motion slots    -> filled with its closest motions by the
--                                     DLL at runtime (sacred.model_slots)
--   * portraits (roster, dialog)   -> the body's creature type portraits, by
--                                     copying its cells in the exe's two
--                                     portrait switches (sacred.patch_u32_copy)
--   * class-select preview skin    -> the preview draws the body with the TYPE
--                                     texture like the game does (sacred.patch_bytes)
--   * worn item meshes             -> not drawn on the borrowed body (armor
--                                     stays equipped and counts; weapons, rings,
--                                     wings still show) (sacred.wear_hide)
--   * class-select preview items   -> removed (sacred.patch_u32)
--   * the class's default part     -> switched off in the exe (sacred.patch_u32):
--                                     the item the engine wears in an empty slot
--                                     (Battle Mage cowl, Seraphim/Vampiress hair,
--                                     Gladiator belt, Dwarf goggles)
--
-- What stays the slot's own: stats, skills, combat arts, voice, intro
-- quests, and every class check in the exe. See
-- .claude/knowledge/re/class_mod_feasibility.md for which slot fits what.
--
-- Usage (from any mod file):
--   local C  = require "classes"
--   local CM = require "classmod"
--   CM.replace(C.BATTLEMAGE, {
--     name = "Alcalata the Wise",       -- class name + class-select label
--     info = "…",                       -- class-select description
--     body = 271,                       -- copy the body of creature type 271
--   })
--
-- Changes show up on the next launch; the first start after adding a mod may
-- still show the old slot. To undo a body swap, remove the mod AND delete
-- custom/pak/Items.pak (a stale file keeps serving the old swap). A replaced
-- slot is for new heroes: older saves of that class keep class-specific
-- skills, combat arts and script state.
--
-- Live-tested 2026-09-16 with all eight slots (lua/classes/*.lua).
--
-- Items.pak layout (verified on Steam 2.0.2.28):
--   "ITM\5", u16 count @+4 (0x8000), index @0x100: 12 bytes per TYPE id
--   {u32 kind, u32 offset, u32 size}; each TYPE record is 128 bytes:
--     +0x08  u32   texture id           (NPC skins: black magicians 6839..6849)
--     +0x37  char[32] model file name   (FUN_00426250)
--     +0x70  u32   models.pak index     (FUN_00426270 -> models manager FUN_004115d0)

local T = require "text"

local M = {}

-- Per slot: hero creature type(s), global.res keys of the class name, the
-- class-select label (FUN_0070cd50) and the description (FUN_0070f5c0; the
-- numeric 0x424..0x42B ids are shown by the character panel too).
--
-- part: the class's default part. FUN_00426580(class, slot) picks it through
-- the jump table at 0x004265E8 (index class-1); the table cell holds `case`,
-- and 0x004265E0 is the "none" case.
-- preview: the items the class-select figure wears (table 0x009E3248, 8 x 0x50,
-- item ids at +0x04..+0x20), as {va, item}.
local SLOTS = {
  SERAPHIM   = { types = {1},    label = "UI_DEFAULT_SERAPHIM",    info = {"1061"},
                 part = { cell = 0x004265E8, case = 0x00426591 },
                 preview = { {0x009E329C, 0xFA8}, {0x009E32A4, 0xFA4}, {0x009E32A8, 0xFA2}, {0x009E32AC, 0xFA7},
                             {0x009E32B0, 0xFA1}, {0x009E32B4, 0xFA5}, {0x009E32B8, 0xFA6} } },
  GLADIATOR  = { types = {2},    label = "UI_DEFAULT_GLADIATOR",   info = {"1060"},
                 part = { cell = 0x004265EC, case = 0x004265A1 },
                 preview = { {0x009E324C, 0x6BE}, {0x009E3254, 0xFD6} } },
  BATTLEMAGE = { types = {3},    label = "UI_DEFAULT_KAMPFMAGIER", info = {"1064"},
                 part = { cell = 0x004265F0, case = 0x004265C0 },
                 preview = { {0x009E33DC, 0x6B4}, {0x009E33EC, 0xC93} } },
  DARKELF    = { types = {4},    label = "UI_DEFAULT_DUNKELELF",   info = {"1062"},
                 preview = { {0x009E342C, 0x749}, {0x009E3430, 0x746} } },
  WOODELF    = { types = {5},    label = "UI_DEFAULT_WALDELFIN",   info = {"1063"},
                 preview = { {0x009E3390, 0x6CF} } },
  VAMPIRESS  = { types = {6, 7}, label = "UI_DEFAULT_VAMPIRIN",    info = {"1065"},
                 part = { cell = 0x004265FC, case = 0x004265B0 },
                 preview = { {0x009E347C, 0x6BE}, {0x009E348C, 0xFBC} } },
  DWARF      = { types = {8},    label = "UI_DEFAULT_ZWERG",       info = {"UI_DEFAULT_ZWERG_INFO", "1067"},
                 part = { cell = 0x00426604, case = 0x004265D0 },
                 preview = { {0x009E333C, 0x73D}, {0x009E3344, 0xBC1}, {0x009E3348, 0xBC2}, {0x009E334C, 0xBC4},
                             {0x009E3350, 0xBC3}, {0x009E3354, 0xBC0} } },
  DAEMON     = { types = {9},    label = "UI_DEFAULT_DAEMONIN",    info = {"UI_DEFAULT_DAEMONIN_INFO", "1066"},
                 preview = { {0x009E32EC, 0x4DA}, {0x009E32F0, 0x4DE}, {0x009E32F4, 0x4C4}, {0x009E32F8, 0x4C8},
                             {0x009E32FC, 0x4C6}, {0x009E3300, 0x4C5}, {0x009E3304, 0x4C3} } },
}
local PART_NONE = 0x004265E0

-- Portraits: two switches on the creature type, no data field.
--   small (roster cUI_Mercenary, cUI_NetPortrait): FUN_00435d30, jump table
--     0x00436694 indexed type-1 (types 1..0x2CA), default case 0x0043668F
--   large (talk / merchant / smith windows): FUN_004345c0, jump table
--     0x00435264 for types 1..9, 0x00435288 indexed type-0x21 for 0x21..0x2CA,
--     default case 0x0043525A
-- Each case is `mov eax, <sprite id>; ret 4`, so pointing the hero's cell at
-- the body type's case gives the hero that portrait. Hero cells as shipped:
local PORTRAIT_CELLS = {
  [1] = { 0x00435DBA, 0x004345DC }, [2] = { 0x00435D9A, 0x004345E4 },
  [3] = { 0x00435D92, 0x004345EC }, [4] = { 0x00435DCA, 0x0043464A },
  [5] = { 0x00435DC2, 0x00434642 }, [6] = { 0x00435DA2, 0x004345F4 },
  [7] = { 0x00435DA2, 0x004345F4 }, [8] = { 0x00435DAA, 0x004345FC },
  [9] = { 0x00435DB2, 0x00434604 },
}
local function small_cell(t) return (t >= 1 and t <= 0x2CA) and 0x00436694 + 4 * (t - 1) or nil end
local function large_cell(t)
  if t >= 1 and t <= 9 then return 0x00435264 + 4 * (t - 1) end
  if t >= 0x21 and t <= 0x2CA then return 0x00435288 + 4 * (t - 0x21) end
end

-- Class-select preview (FUN_006f1b30 / FUN_006f54d0): each site did
-- `push 0; call 401bd0` (texture override 0 = the GRN's own skin) before the
-- body render; the rewrite sets the override to TypeManager getTex(class)
-- (FUN_004262b0 = TYPE +0x08), as the in-game path does. Vanilla hero types
-- have texture 0, so untouched classes look the same.
local PREVIEW_SKIN = {
  { 0x006F1DB0, "6A008BCFE817FED0FF8B96F80200008B0DE4B5AA0052E88546D3FF",
                "8B96F80200008B0DE4B5AA005252E8ED44D3FF894734E88546D3FF" },
  { 0x006F5685, "6A008BCFE842C5D0FF8B83F80200008B0DE4B5AA0050E8B00DD3FF",
                "8B83F80200008B0DE4B5AA005050E8180CD3FF894734E8B00DD3FF" },
  { 0x006F59A3, "6A008BCFE824C2D0FF8B83F80200008B0DE4B5AA0050E8920AD3FF",
                "8B83F80200008B0DE4B5AA005050E8FA08D3FF894734E8920AD3FF" },
}
local function unhex(h) return (h:gsub("..", function(b) return string.char(tonumber(b, 16)) end)) end

local ITEMS_VANILLA = "pak/Items.pak"
local ITEMS_CUSTOM  = "custom/pak/Items.pak"
local REC_SIZE      = 128

-- hero type -> creature type whose body it wears
local _bodies = {}

local function record_offset(pak, type_id)
  local count = string.unpack("<I2", pak, 5)
  assert(type_id > 0 and type_id < count, "classmod: type id out of range: " .. type_id)
  local _, off, size = string.unpack("<I4I4I4", pak, 0x100 + type_id * 12 + 1)
  assert(size == REC_SIZE, ("classmod: type %d record is %d bytes, expected %d")
                           :format(type_id, size, REC_SIZE))
  return off
end

local function model_name(pak, off)
  return (pak:sub(off + 0x37 + 1, off + 0x37 + 32):match("^[^\0]*"))
end

-- custom/pak/Items.pak = the vanilla file with every registered body copied
-- into its hero records, spliced in one pass. Written only when it differs.
local function write_items(pak)
  local cuts = {}
  for hero, donor in pairs(_bodies) do
    local h, d = record_offset(pak, hero), record_offset(pak, donor)
    for _, f in ipairs({ { 0x08, 4 }, { 0x37, 32 }, { 0x70, 4 } }) do
      cuts[#cuts + 1] = { h + f[1], pak:sub(d + f[1] + 1, d + f[1] + f[2]) }
    end
    sacred.log(("[classmod] type %d body %s -> %s (type %d, texture %d, models.pak #%d)")
      :format(hero, model_name(pak, h), model_name(pak, d), donor,
              string.unpack("<I4", pak, d + 0x08 + 1), string.unpack("<I4", pak, d + 0x70 + 1)))
  end
  table.sort(cuts, function(a, b) return a[1] < b[1] end)
  local parts, pos = {}, 0
  for _, c in ipairs(cuts) do
    parts[#parts + 1] = pak:sub(pos + 1, c[1])
    parts[#parts + 1] = c[2]
    pos = c[1] + #c[2]
  end
  parts[#parts + 1] = pak:sub(pos + 1)
  local out = table.concat(parts)
  if sacred.read_file(ITEMS_CUSTOM) == out then return end
  sacred.write_file(ITEMS_CUSTOM, out)
end

-- ============================================================
-- Motion slots
-- ============================================================
-- A borrowed NPC body fills far fewer motion slots than a hero body (Alcalata's
-- 23 of the Battle Mage's 90), and an empty slot plays as a T-pose. For every
-- slot the class's own body uses and the borrowed one leaves empty, pick the
-- borrowed body's closest motion and let the DLL write it into the model
-- manager's header table (sacred.model_slots). NPCs sharing that body only
-- gain the filled slots.
--
-- pak/Models.tmp ("MDT\2"): u32 model count @+0x10, motion count @+0x14;
-- 0x4AA-byte model headers from 0x118 (name @+0, slot s = u32 @+0x70+4*s),
-- then 256-byte motion headers (name @+0). Model and motion order = models.pak.

local MODELS_TMP = "pak/Models.tmp"
local HDR, MOT, SLOTS_MAX = 0x4AA, 0x100, 259

-- motion suffix pattern -> preferred suffixes on the borrowed body; a
-- preference ending in "_" takes the first motion starting with it. When the
-- first matching rule finds nothing, the catch-all rule ("") decides.
local FALLBACKS = {
  { "^HORSE_ATTACK_MAGIC", { "CAST_MAGIC02", "CAST_", "ATTACK_1H_A", "ATTACK_" } },
  { "^HORSE_ATTACK",       { "ATTACK_1H_A", "ATTACK_" } },
  { "^HORSE_DEATH",        { "DYING_A", "DYING_" } },
  { "^HORSE_HIT",          { "HIT_A", "HIT_" } },
  { "^HORSE_",             { "IDLE_BH", "IDLE_" } },
  { "IDLE",                { "IDLE_BH", "IDLE_" } },
  { "^F?WALK",             { "WALK_BH", "WALK_" } },
  { "^F?RUN",              { "RUN_BH", "RUN_", "WALK_BH" } },
  { "^DEFEND",             { "DEFEND_BH", "DEFEND_", "HIT_A" } },
  { "^ATTACK",             { "ATTACK_BH_A", "ATTACK_" } },
  { "^SPECIAL",            { "ATTACK_BH_A", "ATTACK_" } },
  { "^CAST",               { "CAST_MAGIC02", "CAST_", "ATTACK_1H_A", "ATTACK_" } },
  { "^DYING",              { "DYING_A", "DYING_" } },
  { "^HIT",                { "HIT_A", "HIT_" } },
  { "^TALK",               { "TALK_A", "TALK_" } },
  { "",                    { "CAST_MAGIC02", "CAST_", "IDLE_BH", "IDLE_" } },   -- ACTIVATE, PICKUP, ...
}

local function cstr(s, off) return (s:sub(off + 1, off + 32):match("^[^\0]*")) end

-- "MAGE_RUN_2H_AXT.GRN" -> "RUN_2H_AXT"
local function suffix(name) return (name:upper():gsub("%.GRN$", ""):match("^[^_]+_(.*)$")) end

local function prefer(prefs, have, order)
  for _, pref in ipairs(prefs) do
    if pref:sub(-1) ~= "_" then
      if have[pref] then return have[pref] end
    else
      for _, s in ipairs(order) do
        if s:sub(1, #pref) == pref then return have[s] end
      end
    end
  end
end

local function pick(want, have, order)
  if have[want] then return have[want] end
  for _, rule in ipairs(FALLBACKS) do
    if want:find(rule[1]) then
      return prefer(rule[2], have, order) or prefer(FALLBACKS[#FALLBACKS][2], have, order)
    end
  end
end

-- Adds to `writes` (slot -> motion) the fills of new_model's empty slots that
-- own_model uses; own_model / new_model are models.pak indices (TYPE +0x70).
local function fill_slots(tmp, own_model, new_model, writes)
  local nmodels, nmotions = string.unpack("<I4I4", tmp, 0x10 + 1)
  local function header(m) return 0x118 + m * HDR end
  local function motion_name(i) return cstr(tmp, 0x118 + nmodels * HDR + i * MOT) end
  local function slot(m, s) return string.unpack("<I4", tmp, header(m) + 0x70 + 4 * s + 1) end

  local have, order = {}, {}
  for s = 0, SLOTS_MAX - 1 do
    local v = slot(new_model, s)
    if v > 0 and v < nmotions then
      local suf = suffix(motion_name(v))
      if suf and not have[suf] then have[suf] = v; order[#order + 1] = suf end
    end
  end
  table.sort(order)

  local n, left = 0, {}
  for s = 0, SLOTS_MAX - 1 do
    local own = slot(own_model, s)
    if own > 0 and own < nmotions and slot(new_model, s) == 0 and not writes[s] then
      local want = suffix(motion_name(own)) or ""
      local v = pick(want, have, order)
      if v then writes[s] = v; n = n + 1 else left[#left + 1] = want end
    end
  end
  sacred.log(("[classmod] %s: %d empty motion slots filled for %s's slots%s")
    :format(cstr(tmp, header(new_model)), n, cstr(tmp, header(own_model)),
            #left > 0 and (", still empty: " .. table.concat(left, " ")) or ""))
end

-- Finalize hook (lua_bake.cpp FINALIZE_MODULES): after every mod has called
-- replace(), write Items.pak once and queue the motion-slot fills.
function M.flush()
  if next(_bodies) == nil then return end
  for _, site in ipairs(PREVIEW_SKIN) do
    sacred.patch_bytes(site[1], unhex(site[2]), unhex(site[3]), "class-select preview skin")
  end
  local pak = assert(sacred.read_file(ITEMS_VANILLA))
  assert(pak:sub(1, 4) == "ITM\5", "classmod: unexpected Items.pak header")
  write_items(pak)

  local tmp = assert(sacred.read_file(MODELS_TMP))
  assert(tmp:sub(1, 4) == "MDT\2", "classmod: unexpected Models.tmp header")
  local function model_of(t) return string.unpack("<I4", pak, record_offset(pak, t) + 0x70 + 1) end
  local by_model, heroes = {}, {}
  for hero in pairs(_bodies) do heroes[#heroes + 1] = hero end
  table.sort(heroes)
  for _, hero in ipairs(heroes) do
    local m = model_of(_bodies[hero])
    by_model[m] = by_model[m] or {}
    fill_slots(tmp, model_of(hero), m, by_model[m])
  end
  for m, writes in pairs(by_model) do
    if next(writes) then sacred.model_slots(m, cstr(tmp, 0x118 + m * HDR), writes) end
  end
end

-- CM.replace(class, spec)
--   class: a `classes` entry (C.BATTLEMAGE) or its key ("BATTLEMAGE")
--   spec.name: class name, also the class-select label
--   spec.info: class-select description
--   spec.body: creature type id whose body (model, texture) the hero wears, or
--              a table per hero type (Vampiress: { [6] = day, [7] = night })
--   spec.parts: false = no class default part (see SLOTS); existing heroes of
--               the class may keep theirs as a plain item
--   spec.portrait: false = keep the class's own portraits (default: the body's)
--   spec.armor: true = keep drawing worn item meshes on the body (default: hidden)
--   spec.preview_items: true = keep the class-select figure's items (default: removed)
function M.replace(class, spec)
  local key  = type(class) == "table" and class.key or class
  local slot = assert(SLOTS[key], "classmod.replace: unknown class " .. tostring(key))
  if spec.name then
    for _, t in ipairs(slot.types) do T.named(tostring(t), spec.name) end
    T.named(slot.label, spec.name)
  end
  if spec.info then
    for _, k in ipairs(slot.info) do T.named(k, spec.info) end
  end
  if spec.body then
    for _, t in ipairs(slot.types) do
      local body = type(spec.body) == "table" and spec.body[t] or spec.body
      if type(body) == "number" then
        _bodies[t] = body
        if spec.portrait ~= false then
          local src_small, src_large = small_cell(body), large_cell(body)
          if src_small then
            sacred.patch_u32_copy(small_cell(t), PORTRAIT_CELLS[t][1], src_small,
                                  ("%s small portrait <- type %d"):format(key, body), 0x0043668F)
          end
          if src_large then
            sacred.patch_u32_copy(large_cell(t), PORTRAIT_CELLS[t][2], src_large,
                                  ("%s large portrait <- type %d"):format(key, body), 0x0043525A)
          end
        end
        if spec.armor ~= true then sacred.wear_hide(t) end
      end
    end
    if spec.preview_items ~= true then
      for _, it in ipairs(slot.preview) do
        sacred.patch_u32(it[1], it[2], 0, key .. " preview item")
      end
    end
  end
  if spec.parts == false and slot.part then
    sacred.patch_u32(slot.part.cell, slot.part.case, PART_NONE, key .. " default part")
  end
end

return M
