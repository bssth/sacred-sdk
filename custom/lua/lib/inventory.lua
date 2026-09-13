-- SacredSDK / lua / lib / inventory.lua
--
-- Hero-inventory predicates. Sacred's bytecode has a native "does the hero
-- carry item X" check via the tag-0x3a `ConditionalEval` record carrying a
-- `ResLookup_3a("HERO", "res:<item>")` op. The condition feeds Sacred's
-- internal eval stack; the matching tag-0x42 `BlockReader` runs only when
-- the predicate evaluates true.
--
-- Reverse-engineered from `bin/TYPE_NPC_GLADIATOR/FunkCode.bin` records 7144
-- and 13384 (whiskey quest, RGHS goodies) — see HANDOFF.md task 19.
--
-- Quick form
-- ----------
--   inventory.has_item(17562)   →  predicate record (use BEFORE the THEN body)
--   inventory.then_body(17562,  →  body wrapper for the THEN branch
--                       { ... THEN-records ... })
--
-- Full conditional, modeled after vanilla pattern:
--
--   inventory.if_has(17562, {
--     -- THEN: hero IS carrying item 17562
--     d.line(T"Yes, you brought it!", "btn_ok"),
--     reward.give_gold(500),
--   })
--
-- For if/else, vanilla uses STACK_96 / STACK_97 markers — control flow is
-- still being decoded. For now, model the no-else case (most common) and
-- pair manually with `q.else_block(...)` once that lands.

local raw = require "raw"

local M = {}

-- A bare "predicate" record. Use only if you understand how Sacred's eval
-- stack hands the result to the next record. Most callers want if_has().
function M.has_item(item_id)
  local res = (type(item_id) == "number")
    and ("res:" .. tostring(item_id))
    or  tostring(item_id)
  return raw.rec(0x3a, 0x00, {"ResLookup_3a", "HERO", res})
end

-- The THEN body wrapper. Sacred runs whatever ops follow inside the same
-- 0x42 BlockReader record (its payload re-states the lookup so the engine
-- can re-evaluate during walk). Records after this 0x42 are the body.
function M.then_body(item_id, body_records)
  local res = (type(item_id) == "number")
    and ("res:" .. tostring(item_id))
    or  tostring(item_id)
  local out = {
    raw.rec(0x42, 0x00, {"ResLookup_3a", "HERO", res})
  }
  if body_records then
    for _, r in ipairs(body_records) do out[#out + 1] = r end
  end
  return out
end

-- Convenience: emit the (0x3a predicate) + (0x42 then-body + body records)
-- pair as a single Lua list. Use this when there is no ELSE branch.
function M.if_has(item_id, body_records)
  local out = { M.has_item(item_id) }
  for _, r in ipairs(M.then_body(item_id, body_records)) do
    out[#out + 1] = r
  end
  return out
end

return M
