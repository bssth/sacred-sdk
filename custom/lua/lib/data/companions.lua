-- SacredSDK / lua / lib / data / companions.lua
--
-- Per-class summonable companion data for Sacred Gold.
-- Source: refs/'sacred_modding - companions.csv' (4 rows, verified).
--
-- Only four hero classes have a named companion creature:
--   Battle Mage, Dark Elf, Wood Elf, Vampiress.
-- (The Vampiress lists a 4th name handle; the other three list three.)
--
-- All values are global.res string/model HANDLES in the 0x0019xxxx /
-- 0x001Axxxx band (the same band the engine uses for companion model +
-- name resources). They are NOT creature type ids -- feed them to
-- global.res to resolve the actual model / display name.
--
--   model_res  = res handle for the companion's model
--   name_res   = ordered list of res handles for the companion's name
--                variants (the engine picks one, typically by index)
--
-- Keys are the display class names (matching data.creatures heroes and
-- classes.lua). Use this to populate the companion roster panel.
--
-- Usage:
--   local CO = require "data.companions"
--   CO.by_class["Wood Elf"].model_res        --> 0x0019FA3D
--   CO.by_class["Vampiress"].name_res[1]      --> 0x0019F6E3

local M = {}

local C = {
  ["Battle Mage"] = {
    model_res = 0x001A0153,
    name_res  = { 0x001A014C, 0x001A032C },
  },
  ["Dark Elf"] = {
    model_res = 0x0019FA57,
    name_res  = { 0x0019FA50, 0x0019F8F1 },
  },
  ["Wood Elf"] = {
    model_res = 0x0019FA3D,
    name_res  = { 0x0019FA36, 0x0019F8D7 },
  },
  ["Vampiress"] = {
    model_res = 0x0019F6EA,
    name_res  = { 0x0019F6E3, 0x0019F46D, 0x0019F418 },
  },
}

function M.get(class_name) return C[class_name] end

M.by_class = C
return M
