-- SacredSDK mod — Vampiress storyline base: the new-game script.
--
-- The retail Vampiress StartCode.bin (everything the world places at a new game),
-- with every vanilla SetUpQuest turned into a same-size NOP when
-- NO_VANILLA_QUESTS is on (set in FunkCode.lua of this folder, baked just
-- before this file; on by default). See lib/novanilla.lua.

local v = require "vanilla"
local recs = v.load "bin/TYPE_NPC_VAMPIRELADY/StartCode"

if NO_VANILLA_QUESTS ~= false then require("novanilla").strip(recs, "StartCode") end

return recs
