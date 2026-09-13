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
-- Note: there is NO 9th-class slot — all 8 mask bits are taken (Ascaron
-- used 64/128 for the Underworld DLC classes). A mod can *replace/
-- repurpose* one of these 8 slots, but cannot add a new one. See
-- the SDK's RE notes on class mods (wiki: Reverse-Engineering).
--
-- Usage:
--   local C = require "classes"
--   print(C.VAMPIRESS.dir)            --> "TYPE_NPC_VAMPIRELADY"
--   print(C.VAMPIRESS.bit)            --> 32
--   for _, c in ipairs(C.ALL) do ... end
--   local c = C.by_dir("TYPE_NPC_ZWERG")   --> C.DWARF
--   local c = C.by_abbr("vamp")            --> C.VAMPIRESS

local M = {}

-- Ordered by class bit (1,2,4,...,128).
local DEFS = {
  { key = "SERAPHIM",   name = "Seraphim",   dir = "TYPE_NPC_SERAPHIM",   bit = 1,   abbr = "sera" },
  { key = "GLADIATOR",  name = "Gladiator",  dir = "TYPE_NPC_GLADIATOR",  bit = 2,   abbr = "glad" },
  { key = "BATTLEMAGE", name = "Battle Mage",dir = "TYPE_NPC_MAGICIAN",   bit = 4,   abbr = "mage" },
  { key = "DARKELF",    name = "Dark Elf",   dir = "TYPE_NPC_DARKELVE",   bit = 8,   abbr = "delf" },
  { key = "WOODELF",    name = "Wood Elf",   dir = "TYPE_NPC_ELVE",       bit = 16,  abbr = "helf" },
  { key = "VAMPIRESS",  name = "Vampiress",  dir = "TYPE_NPC_VAMPIRELADY",bit = 32,  abbr = "vamp" },
  { key = "DWARF",      name = "Dwarf",      dir = "TYPE_NPC_ZWERG",      bit = 64,  abbr = "DWA"  },
  { key = "DAEMON",     name = "Daemon",     dir = "TYPE_NPC_DAEMONIN",   bit = 128, abbr = "DEM"  },
}

-- Combined masks Ascaron uses (handy for item / CA "usable by" fields).
M.ALL_CLASSIC    = 1 + 2 + 4 + 8 + 16 + 32   -- 63  (the 6 base-game classes)
M.ALL_UNDERWORLD = 64 + 128                  -- 192 (Dwarf + Daemon, the DLC)
M.ALL_CLASSES    = 255                       -- every class

M.ALL = {}                 -- array, class-bit order
local _by_dir, _by_abbr, _by_bit = {}, {}, {}

for i, d in ipairs(DEFS) do
  local c = {
    key = d.key, name = d.name, dir = d.dir, bit = d.bit,
    abbr = d.abbr, index = i,          -- index = 1..8 in class-bit order
  }
  M[d.key]            = c             -- C.VAMPIRESS, C.SERAPHIM, ...
  M.ALL[i]            = c
  _by_dir[d.dir]      = c
  _by_abbr[d.abbr]    = c
  _by_bit[d.bit]      = c
end

function M.by_dir(dir)   return _by_dir[dir]   end
function M.by_abbr(abbr) return _by_abbr[abbr] end
function M.by_bit(bit)   return _by_bit[bit]   end

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
