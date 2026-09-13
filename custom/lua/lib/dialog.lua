-- SacredSDK / lua / lib / dialog.lua
--
-- Sacred NPC dialog primitives. Vanilla encodes dialog as a triplet of
-- record types working together:
--
--   tag=0x1a QuestTrigger  — register a name-keyed event hook
--     ops: { STR_REF "HQ_3_2_1_DLG_START" }
--
--   tag=0x3c ResRef        — one renderable line + a follow-up trigger
--     ops: { DLG_OP_a "res:1037" "\x01trigger9511" }   // text + button-action
--
--   tag=0x42 BlockReader   — collect emitted text for display
--     ops: { EMIT_f 0 "9511" }
--
-- A complete dialog block in vanilla looks like:
--   QuestTrigger "HQ_3_2_1_DLG_OFFEN"
--   ResRef        "res:1037"  "btn_ok"
--   BlockReader   EMIT_a 31 "9511"
--   QuestTrigger "HQ_3_2_1_DLG_SIEG"
--   …
--
-- The wrappers below emit those records. Note the trigger payload sometimes
-- has a `\x01` prefix on the trigger-name string — that byte is part of the
-- DLG_OP_a cstring, NOT a separate opcode, so include it when you call.

local raw = require "raw"
local fc  = require "funkcode"

local M = {}

-- Register a dialog-trigger hook. `name` is the symbolic event id (e.g.
-- "HQ_3_2_1_DLG_OFFEN"). Sacred fires this when control flow reaches it.
function M.trigger(name)
  return raw.rec(0x1a, 0x00, fc.res_ref(name))
end

-- A single dialog line with a follow-up button. `res_id` is the text
-- resource (e.g. "res:1037"). `button_action` is the trigger name to fire
-- after the player confirms; typically prefixed with `\x01` in vanilla.
function M.line(res_id, button_action)
  return raw.rec(0x3c, 0x00, fc.dialog(res_id, button_action))
end

-- BlockReader emit. Pairs with a line; encodes the actual visible text
-- routing through Sacred's per-block accumulator.
--   M.emit(31, "9511")
function M.emit(channel, target)
  return raw.rec(0x42, 0x00, fc.emit("EMIT_a", channel, target))
end

-- Convenience: a full dialog "scene" — register the trigger, lay down one
-- line, and emit its block-reader. Multiple lines? Call line()/emit() yourself
-- between the trigger and the next scene.
function M.scene(trigger_name, res_id, button_action, channel, target)
  return
    M.trigger(trigger_name),
    M.line(res_id, button_action),
    M.emit(channel or 0, target or "")
end

return M
