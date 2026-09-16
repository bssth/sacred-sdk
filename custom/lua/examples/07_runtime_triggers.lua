-- ============================================================
-- Example 07: Runtime trigger hooks.
-- ============================================================
--
-- Sacred's engine queries resources by `res:NNNN` (or symbolic name)
-- through a single chokepoint — `sacred_hash` at VA 0x0080e780. Our DLL
-- trampolines that chokepoint and routes every queried name through
-- `runtime_triggers::fire(name)` which dispatches to your Lua handlers.
--
-- KEY THING TO KNOW
-- -----------------
-- `sacred.on_trigger("X", fn)` fires EVERY time Sacred resolves res:X —
-- which means 30 times per second while the resource is on-screen
-- (dialog text, UI element, cutscene panel, item tooltip, …). For most
-- mod use cases you want SEMANTIC events ("dialog opened", "panel shown"
-- ONCE), so reach for the helpers below.
--
-- THE HELPERS
-- -----------
--   q.on_trigger_once(name, fn)              -- fires the first time only
--   q.on_trigger_throttled(name, ms, fn)     -- fires at most every `ms`
--   q.on_any_once({names…}, fn)              -- first occurrence across set
--   sacred.on_trigger(name, fn)              -- RAW — every fire (rarely
--                                               what you want directly)
--   sacred.clear_triggers()                  -- drop every handler
--
-- DISCOVERING THE RIGHT IDs
-- -------------------------
-- 1. Open the overlay (F11) → "Runtime triggers" panel.
-- 2. Play through the moment you want to react to. Watch the "Recent
--    trigger names" list — it dedups consecutive repeats so you can see
--    the actual sequence.
-- 3. Cross-check by decompiling the vanilla class file in question:
--      python sdk/re/py/funkcode_decompile_lua.py \
--             bin/TYPE_NPC_<class>/FunkCode.bin \
--             -o /tmp/scratch.lua
--    grep for the trigger name (HQ_/DQ_/NQ_/…) and look at the
--    `d.line("res:NNNN", …)` records around it. Those NNNNs are the
--    runtime ids you want to register handlers for.

local q = require "quest"

-- Every handler receives a `ctx` table:
--
--   ctx.trigger_name           — the name that fired (string)
--   ctx:gold()         -> int  — current hero gold (nil while bindings stub)
--   ctx:give_gold(N)           — add N to hero gold (N>0 grants, N<0 charges)
--   ctx:charge_gold(N)         — alias for give_gold(-N)
--   ctx:has_item(res) -> bool  — does hero carry the given resource id?
--   ctx:set_qbit(n[,v])        — set hero quest bit n (default v=true)
--   ctx:get_qbit(n)   -> bool  — read hero quest bit n
--   ctx:notify(text)           — top-of-screen banner. Throttled to 1 per
--                                ~750 ms, deduped, max 256 chars; excess
--                                calls silently return false.
--
-- Bindings that aren't wired yet (gold/has_item/qbit) currently return
-- nil/false. `notify` falls back to a log line until the engine bind
-- lands. Track HANDOFF task 21 for progress.

-- ============================================================
-- Pattern 1: dialog body — Leandra's quest text in Sera intro.
-- Use ctx:notify to surface a side-quest banner in-game.
-- ============================================================
q.on_trigger_once("17095", function(ctx)
  sacred.log("[ex07] Leandra's quest dialog body queried (first time)")
  ctx:notify("Side quest discovered: The Lost Tome")
end)

-- ============================================================
-- Pattern 2: a whole sequence (intro cutscene panels). Handler gets
-- the matched name AND ctx in that order.
-- ============================================================
q.on_any_once(
  {"17631", "17632", "17633", "17634", "17635", "17636", "17637"},
  function(matched_id, ctx)
    sacred.log("[ex07] cutscene panel res:" .. matched_id .. " shown")
    if matched_id == "17637" then
      ctx:notify("Tale of Ancaria — Prologue complete")
    end
  end
)

-- ============================================================
-- Pattern 3: gate logic on hero state (once Task 21 bindings land).
-- This stub runs every time res:17095 fires; the body skips unless
-- the hero already carries the macguffin item.
-- ============================================================
-- q.on_trigger_throttled("17095", 5000, function(ctx)
--   if ctx:has_item(17562) then
--     ctx:give_gold(500)
--     ctx:notify("Quest reward: +500 gold")
--     ctx:set_qbit(9512)
--   end
-- end)

-- ============================================================
-- This file only registers handlers, so it returns an empty record list.
-- A mod like it goes in custom/lua/mods/<name>.lua, a folder that mirrors
-- no game file. Saved at a game script's path such as
-- custom/lua/bin/<class>/FunkCode.lua, its empty output would replace that
-- script.
-- ============================================================
return {}
