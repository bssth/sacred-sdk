-- ============================================================
-- Example 01: "Hello mod" — the smallest meaningful mod.
-- ============================================================
--
-- Drop this file as `custom/lua/bin/<class>/QuestCode.lua` (replace <class>
-- with one of TYPE_NPC_SERAPHIM / TYPE_NPC_ELVE / ... — see the vanilla
-- layout under `bin/`).
--
-- The baker reads this, executes it, and writes the resulting bytes to
-- `custom/bin/<class>/QuestCode.bin`. fs_override then serves that file in
-- place of the game's own `bin/<class>/QuestCode.bin`. It replaces the WHOLE
-- file: what this script returns is all the QuestCode that class gets.
--
-- What this does: declares one hero quest-bit and a single quest variable,
-- the same records as the vanilla QuestCode.bin of seven classes, so as it
-- stands it changes nothing there. The Gladiator's file also sets hero
-- quest-bit 31 after 1101: add `q.set_hero_qbit(31),` before using this as
-- TYPE_NPC_GLADIATOR, or that bit is lost.

local q = require "quest"

return q.script {
  q.set_hero_qbit(1101),
  q.var "DaemonTotFranz",
}
