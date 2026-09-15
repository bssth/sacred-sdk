-- SacredSDK mod — the Dark Elf slot becomes Shalinor the Dark Elf.
--
-- Body: creature type 326 (DARKELVE2B.GRN). The slot keeps the Dark Elf's stats,
-- skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.DARKELF, {
  name = "Shalinor the Dark Elf",
  info = "Shalinor fights with the blades and the cunning of the dark elves.",
  body = 326,
})

return {}
