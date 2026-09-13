-- examples/11_classes.lua — the 8 playable hero classes as constants.
--
-- `require "classes"` gives you verified data for every class: the script
-- folder name, the class bitmask, the quest-token abbreviation, and the
-- display name. Use it instead of hardcoding "TYPE_NPC_VAMPIRELADY".

local C = require "classes"

-- Per-class fact lookup.
local v = C.VAMPIRESS
sacred.log(("[ex11] %s  dir=%s  bit=%d  abbr=%s")
  :format(v.name, v.dir, v.bit, v.abbr))

-- Iterate all 8 in class-bit order.
for _, c in ipairs(C.ALL) do
  sacred.log(("[ex11]  %-12s %-22s bit=%3d")
    :format(c.name, c.dir, c.bit))
end

-- Reverse lookups (handy when you only know the folder or quest token).
sacred.log("[ex11] by_dir  -> " .. C.by_dir("TYPE_NPC_ZWERG").name)   -- Dwarf
sacred.log("[ex11] by_abbr -> " .. C.by_abbr("delf").name)            -- Dark Elf

-- "Usable by" bitmask helpers (item / combat-art class gating).
sacred.log(("[ex11] Sera+Vamp mask = %d  (ALL_CLASSIC=%d)")
  :format(C.mask{ C.SERAPHIM, C.VAMPIRESS }, C.ALL_CLASSIC))
