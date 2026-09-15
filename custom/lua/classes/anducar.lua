-- SacredSDK mod — the Daemon slot becomes Anducar.
--
-- Body: creature type 182 (ANDUCAR.GRN). The slot keeps the Daemon's stats,
-- skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.DAEMON, {
  name = "Anducar",
  info = "Anducar steps out of the shadows to fight in his own name.",
  body = 182,
})

return {}
