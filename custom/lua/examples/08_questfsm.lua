-- ============================================================
-- Example 08: Quest state machines via lib/questfsm.lua.
-- ============================================================
--
-- 07_runtime_triggers.lua showed the RAW primitive (`sacred.on_trigger`)
-- and its convenience wrappers (`q.on_trigger_once`, `q.on_any_once`).
-- Those work for one-shot reactions, but real quests have ORDER:
--
--   * "found the book" should only count AFTER "quest accepted"
--   * "redeem reward" should only fire AFTER "found the book"
--   * each step's effect (banner, gold drop) should fire ONCE
--
-- Hand-rolling that with bare `on_trigger_once` ends up as a tower of
-- `if _seen.X and not _seen.Y` flags. `q.fsm.define` collapses the same
-- logic into a declarative chain.
--
-- WHAT YOU GET FROM THE FSM
-- -------------------------
--   * Linear ordering — step K only fires if step K-1 is the current
--     state. Out-of-order trigger fires are silently dropped + logged.
--   * Per-step `on_enter(self, ctx)` — runs exactly once when the step
--     becomes current. Use it for `ctx:notify`, `ctx:give_gold`, etc.
--   * Optional `guard(self, ctx)` — runs BEFORE on_enter; return false
--     to refuse the transition (e.g. "hero must carry the macguffin").
--   * Self-inspection — `quest:at("found_book")`, `quest:current_step()`,
--     `quest:done()`. Cross-quest logic becomes one-liners.
--
-- ABSENT FROM v1
-- --------------
-- Persistence: state resets each game launch. Sacred's own qbits/journal
-- already persist inside the save file; the FSM is a transient overlay
-- that re-derives itself the next time the engine queries the relevant
-- trigger ids during play.

local q = require "quest"

-- ============================================================
-- Quest 1: a simple 3-step chain that awards 500 gold at the end.
-- We're piggy-backing on existing Sera dialog ids (17095 / 17562 / 17400)
-- so this DOES something visible without needing to RE the quest book.
-- Replace those numbers with whatever you discover via F11 → Runtime
-- triggers panel for your own scene.
-- ============================================================
local lost_tome = q.fsm.define {
  id = "lost_tome",
  steps = {
    { name = "started",
      trigger = "17095",
      on_enter = function(self, ctx)
        ctx:notify("Quest accepted: The Lost Tome")
      end,
    },
    { name = "found_book",
      trigger = "17562",
      -- Guard: only count this step if the hero actually has the item.
      -- (For a real mod the item id would be one your bake ships in.)
      guard = function(self, ctx) return ctx:has_item(17562) end,
      on_enter = function(self, ctx)
        ctx:notify("You found the Tome!")
      end,
    },
    { name = "completed",
      trigger = "17400",
      on_enter = function(self, ctx)
        ctx:give_gold(500)
        ctx:notify("Reward: +500 gold")
      end,
    },
  },
}

-- ============================================================
-- Quest 2: cross-quest gate. This one only progresses if `lost_tome`
-- has reached "completed" — a follow-up that unlocks after the first.
-- ============================================================
q.fsm.define {
  id = "tome_aftermath",
  steps = {
    { name = "rumour_heard",
      trigger = "17633",
      guard  = function(self, ctx)
        -- Look up the other quest at runtime. Returns nil if we're
        -- baking before lost_tome registered, hence the safe-call.
        local lt = q.fsm.get("lost_tome")
        return lt and lt:done()
      end,
      on_enter = function(self, ctx)
        ctx:notify("A scholar mutters about the missing Tome…")
      end,
    },
    { name = "investigated",
      trigger = "17634",
      on_enter = function(self, ctx)
        ctx:give_gold(100)
        ctx:notify("Investigation reward: +100 gold")
      end,
    },
  },
}

-- ============================================================
-- Debug helper: bind a rare resource id to a `dump` of every FSM.
-- Pick a trigger that you can reliably fire on demand (e.g. a chest
-- you know nobody touches) and check sdk_loaded.log for output.
--
-- Uncomment if you want it; left commented so the example is silent.
-- ============================================================
-- sacred.on_trigger("9999", function(_) q.fsm.dump() end)

return {}
