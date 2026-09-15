-- SacredSDK mod — the Vampiress slot becomes Baroness Vilya.
--
-- Body by day: creature type 699 (BARONESS_OUTDOOR.GRN); by night, in the
-- vampire form: type 224, the pale Vilya of Underworld (VILYAUW.GRN). The slot
-- keeps the Vampiress's stats, skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.VAMPIRESS, {
  name = "Baroness Vilya",
  info = "Baroness Vilya of Mascarell trades her court for the battlefield.",
  body = { [6] = 699, [7] = 224 },
  parts = false,
})

return {}
