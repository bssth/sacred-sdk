-- ============================================================
-- Example 05: A conditional dialog (native Sacred branching).
-- ============================================================
--
-- Sacred's bytecode HAS native if/else — it runs the condition in-engine
-- at game time, not at bake time. We emit the records that form the
-- branch; Sacred picks which path to take depending on its evaluation
-- stack.
--
-- The exact semantics of the STACK_NN markers and the ConditionalEval /
-- BlockReader combo are still being reverse-engineered. This example uses
-- the same pattern vanilla Seraphim FunkCode.bin uses around offset 0x8b.
-- Once we model the encoding properly there'll be a clean
-- `q.if_(cond, then_body, else_body)` helper that hides this skeleton.
--
-- For now: think of this as "shape your conditional after a vanilla one".
-- Disassemble a vanilla quest near a conditional you understand, copy the
-- record sequence, retarget the inner contents.

local q   = require "quest"
local d   = require "dialog"
local raw = require "raw"

return q.script {
  -- Register the dialog event
  d.trigger "HQ_MY_QUEST_DLG_START",

  -- ConditionalEval: pushes whatever STACK_96 evaluates to onto the
  -- engine's eval stack. (STACK_NN semantics not fully decoded — see
  -- sdk/re/py/funkcode_disasm.py STACK_OPS set.)
  raw.rec(0x3a, 0x00, {"STACK_96"}),

  -- THEN branch — runs when the cond is "true"
  d.line ("res:1037", "btn_ok"),
  d.emit (31, "9511"),

  -- BlockReader: starts the ELSE branch
  raw.rec(0x42, 0x00, {"STACK_97"}),

  -- ELSE branch
  d.line ("res:1038", "btn_ok"),
  d.emit (31, "9511"),
}
