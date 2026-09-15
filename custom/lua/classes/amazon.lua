-- SacredSDK mod — the Seraphim slot becomes Amazon.
--
-- Body: creature type 316 (AMAZONE.GRN). The slot keeps the Seraphim's stats,
-- skills, combat arts, voice and intro.

local C  = require "classes"
local CM = require "classmod"

CM.replace(C.SERAPHIM, {
  name = "Amazon",
  info = "An Amazon warrior: at home in the saddle, quick with a blade and versed in magic.",
  body = 316,
  parts = false,
})

return {}
