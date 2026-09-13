-- examples/12_npc_classes.lua — every creature / NPC class as constants.
--
-- `require "npc"` exposes all 474 creature/NPC classes from the community
-- characters table. The value is the numeric Type for the FunkCode
-- `CreateNPC` record (tag 0x01). Different namespace from classes.lua
-- (the 8-bit hero save/item mask).

local NPC = require "npc"

sacred.log(("[ex12] SKELETON id=%d, total classes=%d")
  :format(NPC.SKELETON, #NPC.ALL))

-- Lookups.
sacred.log("[ex12] by_id[88] -> " .. NPC.by_id[88].name)        -- D'Cay...
sacred.log("[ex12] find Unicorn -> id " .. NPC.find("Unicorn").id)

-- Duplicate names keep distinct keys; iterate them all by name.
local pirates = NPC.all_named("Pirate")
sacred.log(("[ex12] %d distinct 'Pirate' classes (ids %d..%d)")
  :format(#pirates, pirates[1].id, pirates[#pirates].id))

-- Hero models (CreateNPC ids 1..9) — note this is NOT the class mask:
sacred.log(("[ex12] Vampiress CreateNPC ids: human=%d vampire=%d")
  :format(NPC.VAMPIRESS_HUMAN, NPC.VAMPIRESS_VAMPIRE))
