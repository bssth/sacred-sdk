-- SacredSDK mod — the Gladiator slot becomes Prince Valor.
--
-- Body: creature type 322 (VALOR.GRN). The slot keeps the Gladiator's stats,
-- skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.GLADIATOR, {
  name = "Prince Valor",
  info = "Prince Valor leaves the safety of the court and takes up his sword for his realm.",
  body = 322,
  parts = false,
})

return {}
