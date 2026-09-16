-- ============================================================
-- Example 02: Bulk text swap — programmatically rewrite vanilla.
-- ============================================================
--
-- Loads vanilla and applies two text-substitution passes. Sera-class quest
-- dialog identifiers are remapped to the equivalent identifiers of the
-- Gladiator and Vampirelady classes. In game, the Seraphim character now
-- meets quest NPCs that speak someone else's lines.
--
-- Demonstrates `vanilla.gsub_bytes` — which reaches inside `_HEX`-fallback
-- records too (records the structural mnemonizer didn't fully decode).
-- Use `gsub_strings` if you only want to touch decoded-string args.
--
-- To deploy:
--   1. Nothing to prepare: `v.load` decompiles the game's own
--      bin/TYPE_NPC_SERAPHIM/FunkCode.bin on the spot (lib/vanilla.lua).
--   2. Save THIS file to `custom/lua/bin/TYPE_NPC_SERAPHIM/FunkCode.lua`. It
--      returns the whole vanilla script with the swaps applied, which is what
--      that path needs: the baked file replaces the game's FunkCode.bin.
--   3. Launch Sacred. Bake takes ~2.5 s; in-game you'll see the swap.

local v = require "vanilla"

sacred.log("text-swap: loading Sera vanilla")
local recs = v.load "bin/TYPE_NPC_SERAPHIM/FunkCode"
sacred.log(("loaded %d records"):format(#recs))

v.gsub_bytes(recs, "HQ_3_2_1_sera_", "HQ_3_1_4_glad_")
v.gsub_bytes(recs, "HQ_3_2_3_sera_", "HQ_3_5_3_vamp_")

return recs
