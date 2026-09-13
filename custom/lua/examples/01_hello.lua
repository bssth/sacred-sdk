-- ============================================================
-- Example 01: "Hello mod" — the smallest meaningful mod.
-- ============================================================
--
-- Drop this file as `custom/lua/bin/<class>/QuestCode.lua` (replace <class>
-- with one of TYPE_NPC_SERAPHIM / TYPE_NPC_GLADIATOR / ... — see the
-- vanilla layout under `bin/`).
--
-- The baker reads this, executes it, and writes the resulting bytes to
-- `custom/bin/<class>/QuestCode.bin`. fs_override picks the file up on the
-- next game open.
--
-- What this does: declares one hero quest-bit and a single quest variable.
-- Byte-perfect equivalent to vanilla Gladiator QuestCode.bin.

local q = require "quest"

return q.script {
  q.set_hero_qbit(1101),
  q.var "DaemonTotFranz",
}
