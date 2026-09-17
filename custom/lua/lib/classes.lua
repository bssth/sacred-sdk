-- SacredSDK / lua / lib / classes.lua
--
-- The 8 playable hero classes of Sacred Gold, as ready-to-use constants
-- for modders. Everything here is verified against the game binary, not
-- guessed:
--
--   * `dir`  — the on-disk script folder name (`bin/<dir>/FunkCode.bin`).
--              This is what you put a mod under: `custom/lua/bin/<dir>/`.
--              Verified by listing the retail `bin/` directory.
--   * `bit`  — the class bitmask. Sacred stores "which classes may use
--              this" as a single BYTE bitmask everywhere: item usable-by,
--              combat-art / skill gating, the per-class questbook slot,
--              and the save file's class field. Cross-checked vs
--              char.cpp / SacredGameTools (see scratch/class_mod_feasibility.md).
--   * `abbr` — the 3-4 letter token Ascaron uses inside quest names
--              (`HQ_3_1_4_<abbr>_NPC_Auftrag_Qstart`). Verified by
--              grepping every quest token across all 8 FunkCode files.
--   * `name` — human-readable display name.
--
-- Note: a mod can *replace/repurpose* one of these 8 slots
-- (lib/classmod.lua), not add a 9th: the exe has per-class switch tables,
-- an 8-slot class-select table and fixed 8-class Balance tables. (The
-- engine's own class mask is 32-bit, Dwarf/Daemon = 0x400000/0x800000;
-- the `bit` values here are the SDK's compact numbering.)
--
-- Usage:
--   local C = require "classes"
--   print(C.VAMPIRESS.dir)            --> "TYPE_NPC_VAMPIRELADY"
--   print(C.VAMPIRESS.bit)            --> 32
--   for _, c in ipairs(C.ALL) do ... end
--   local c = C.by_dir("TYPE_NPC_ZWERG")   --> C.DWARF
--   local c = C.by_abbr("vamp")            --> C.VAMPIRESS

local M = {}

-- Ordered by class bit (1,2,4,...,128). `types` = the hero's creature types
-- (sacred.hero_type(); the Vampiress is 6 by day and 7 in her vampire form).
local DEFS = {
  { key = "SERAPHIM",   name = "Seraphim",   dir = "TYPE_NPC_SERAPHIM",   bit = 1,   abbr = "sera", types = { 1 } },
  { key = "GLADIATOR",  name = "Gladiator",  dir = "TYPE_NPC_GLADIATOR",  bit = 2,   abbr = "glad", types = { 2 } },
  { key = "BATTLEMAGE", name = "Battle Mage",dir = "TYPE_NPC_MAGICIAN",   bit = 4,   abbr = "mage", types = { 3 } },
  { key = "DARKELF",    name = "Dark Elf",   dir = "TYPE_NPC_DARKELVE",   bit = 8,   abbr = "delf", types = { 4 } },
  { key = "WOODELF",    name = "Wood Elf",   dir = "TYPE_NPC_ELVE",       bit = 16,  abbr = "helf", types = { 5 } },
  { key = "VAMPIRESS",  name = "Vampiress",  dir = "TYPE_NPC_VAMPIRELADY",bit = 32,  abbr = "vamp", types = { 6, 7 } },
  { key = "DWARF",      name = "Dwarf",      dir = "TYPE_NPC_ZWERG",      bit = 64,  abbr = "DWA",  types = { 8 } },
  { key = "DAEMON",     name = "Daemon",     dir = "TYPE_NPC_DAEMONIN",   bit = 128, abbr = "DEM",  types = { 9 } },
}

-- Combined masks Ascaron uses (handy for item / CA "usable by" fields).
M.ALL_CLASSIC    = 1 + 2 + 4 + 8 + 16 + 32   -- 63  (the 6 base-game classes)
M.ALL_UNDERWORLD = 64 + 128                  -- 192 (Dwarf + Daemon, the DLC)
M.ALL_CLASSES    = 255                       -- every class

M.ALL = {}                 -- array, class-bit order
local _by_dir, _by_abbr, _by_bit, _by_type = {}, {}, {}, {}

for i, d in ipairs(DEFS) do
  local c = {
    key = d.key, name = d.name, dir = d.dir, bit = d.bit,
    abbr = d.abbr, index = i,          -- index = 1..8 in class-bit order
    types = d.types,
  }
  M[d.key]            = c             -- C.VAMPIRESS, C.SERAPHIM, ...
  M.ALL[i]            = c
  _by_dir[d.dir]      = c
  _by_abbr[d.abbr]    = c
  _by_bit[d.bit]      = c
  for _, t in ipairs(d.types) do _by_type[t] = c end
end

function M.by_dir(dir)   return _by_dir[dir]   end
function M.by_abbr(abbr) return _by_abbr[abbr] end
function M.by_bit(bit)   return _by_bit[bit]   end
function M.by_type(t)    return _by_type[t]    end

-- A class from an entry, a key, a dir or an abbreviation.
local function class_of(v)
  if type(v) == "table" and v.types then return v end
  return M[v] or _by_dir[v] or _by_abbr[v]
end

-- ---- the hero's class ---------------------------------------------------------

-- The class of the hero in the current world, or nil: a menu, no hero yet, or a
-- DLL without sacred.hero_type.
function M.hero()
  local t = sacred and sacred.hero_type and sacred.hero_type()
  return t and _by_type[t] or nil
end

-- Is the hero of one of these classes?
--   C.hero_is(C.VAMPIRESS)   C.hero_is("DWARF", "DAEMON")
function M.hero_is(...)
  local c = M.hero()
  if not c then return false end
  for _, v in ipairs({ ... }) do
    if class_of(v) == c then return true end
  end
  return false
end

-- A mod for some classes only. What it registers through the scope runs in a
-- world whose hero is of one of those classes, and is skipped in every other:
--
--   local ONLY = C.only(C.VAMPIRESS)
--   ONLY.on_ready(function(loaded) ... end)   -- vars.on_ready, those classes only
--   ONLY.on_tick(function() ... end)          -- sacred.on_tick, those classes only
--   ONLY.active()                             -- the test, for the `when` argument
--                                             -- of persona, novanilla, openworld
--   sacred.set_new_game_spawn(x, y, ONLY.types)   -- the hero types, { 6, 7 }
--
-- Section triggers need no scope: their sections run only from the mod's own
-- records. Each world logs once whether the scope runs.
function M.only(...)
  local list, names, types = {}, {}, {}
  for _, v in ipairs({ ... }) do
    local c = class_of(v)
    assert(c, "classes.only: unknown class " .. tostring(v))
    list[#list + 1], names[#names + 1] = c, c.name
    for _, t in ipairs(c.types) do types[#types + 1] = t end
  end
  local S = { types = types }
  function S.active() return M.hero_is(table.unpack(list)) end
  local V = require "vars"
  function S.on_ready(fn)
    V.on_ready(function(...)
      if S.active() then return fn(...) end
    end)
  end
  function S.on_tick(fn)
    sacred.on_tick(function(...)
      if S.active() then return fn(...) end
    end)
  end
  V.on_ready(function()
    if not (sacred and sacred.hero_type) then
      sacred.log("[classes] this DLL has no sacred.hero_type: a mod for "
                 .. table.concat(names, ", ") .. " never runs")
      return
    end
    local c = M.hero()
    sacred.log(("[classes] hero type %s (%s): the mod for %s %s"):format(
      tostring(sacred.hero_type()), c and c.name or "?", table.concat(names, ", "),
      S.active() and "runs" or "is skipped"))
  end)
  return S
end

-- Build a class bitmask from a list of class tables / keys / dirs.
--   classes.mask{ C.SERAPHIM, C.VAMPIRESS }   --> 33
function M.mask(list)
  local m = 0
  for _, v in ipairs(list) do
    if type(v) == "table" and v.bit then m = m + v.bit
    elseif M[v] then m = m + M[v].bit
    elseif _by_dir[v] then m = m + _by_dir[v].bit
    elseif _by_abbr[v] then m = m + _by_abbr[v].bit end
  end
  return m
end

return M
