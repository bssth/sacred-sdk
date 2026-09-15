-- SacredSDK mod — the Wood Elf slot becomes Ice Elf Priestess.
--
-- Body: creature type 141 (ICEELVE_DRUID.GRN). The slot keeps the Wood Elf's stats,
-- skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.WOODELF, {
  name = "Ice Elf Priestess",
  info = "A priestess of the ice elves, as sure with the bow as with the magic of her people.",
  body = 141,
})

return {}
