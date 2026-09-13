-- ============================================================
-- Example 03: An NPC dialog block authored from scratch.
-- ============================================================
--
-- A single quest-trigger that pops up one dialog line with a confirm
-- button. Vanilla Sacred uses exactly this pattern for thousands of NPC
-- interactions — this example shows the recipe.
--
-- The three records produced map to Sacred's runtime dialog dispatch:
--   d.trigger ─ tag 0x1a QuestTrigger, registers a name-keyed event
--   d.line    ─ tag 0x3c ResRef,       text resource + follow-up button id
--   d.emit    ─ tag 0x42 BlockReader,  routes the rendered text to a block
--
-- See `lib/dialog.lua` for what each builder emits at the byte level.

local q = require "quest"
local d = require "dialog"

return q.script {
  d.trigger "HQ_3_2_1_DLG_START",
  d.line   ("res:1037", "btn_ok"),
  d.emit   (31, "9511"),
}
