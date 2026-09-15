-- SacredSDK mod — the Dwarf slot becomes Wilbur.
--
-- Body: creature type 83 (WILBUR.GRN). The slot keeps the Dwarf's stats,
-- skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.DWARF, {
  name = "Wilbur",
  info = "Wilbur sets out to make his own way through Ancaria.",
  body = 83,
  parts = false,
})

return {}
