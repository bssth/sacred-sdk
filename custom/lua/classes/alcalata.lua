-- SacredSDK mod — the Battle Mage slot becomes Alcalata the Wise.
--
-- Alcalata is the old sorcerer of the base campaign (creature type 271,
-- TYPE_NPC_BLACK_MAGICIAN10: BLACK_MAGICIAN.GRN with his own skin). The
-- slot keeps the Battle Mage's stats, skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.BATTLEMAGE, {
  name = "Alcalata the Wise",
  info = "Alcalata the Wise has taught generations of Battle Mages. "
      .. "Now the old sorcerer takes up the staff himself: a master of the "
      .. "elements who still knows how to use a blade.",
  body = 271,
  parts = false,       -- no Battle Mage cowl over his robe
})

return {}
