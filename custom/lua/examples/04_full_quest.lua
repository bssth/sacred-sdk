-- ============================================================
-- Example 04: A complete (small) quest authored from scratch.
-- ============================================================
--
-- Brings together state, dialog, and quest-log builders into one cohesive
-- mod. The pattern below is a starting point — copy it, retarget the
-- resource ids and quest-bit numbers, and you have a custom side quest.
--
-- !!! Note on conditionals !!!
--
-- Sacred's bytecode HAS native if/else (tags 0x3a ConditionalEval, 0x3b
-- ELSE_jump, 0x42 BlockReader) but its full semantics are still being
-- reverse-engineered. For now we author conditional branches as raw
-- records using `raw.rec`. As soon as we model the control-flow encoding
-- properly there'll be a `q.if_(cond, then_body, else_body)` helper —
-- track that in HANDOFF.md task #18.

local q   = require "quest"
local d   = require "dialog"
local raw = require "raw"

local QUEST_ID = 9511   -- Sacred-internal quest number; pick a free slot

return q.script {
  -- ----- state: what this quest tracks -----
  q.var "myquest_reward_gold",
  q.var "myquest_step",

  -- ----- initial values -----
  q.assign("myquest_reward_gold", 500),
  q.assign("myquest_step", 0),

  -- ----- quest log entries (Title / Header / Initial body text) -----
  -- All three strings must exist in global.res; the editor sees them as
  -- `res:HQ_3_2_1_Log_Title`-style symbols.
  q.log_entry(QUEST_ID,
              "MyQuest_Log_Title",
              "MyQuest_Log_Header",
              "MyQuest_Log_Qstart"),

  -- ----- dialog: NPC greets hero -----
  d.trigger "MyQuest_DLG_START",
  d.line   ("res:1037", "btn_ok"),
  d.emit   (31, tostring(QUEST_ID)),

  -- ----- on victory: set the qbit so the journal updates -----
  d.trigger "MyQuest_DLG_SIEG",
  q.set_hero_qbit(QUEST_ID),
}
