-- ============================================================
-- Example 10: Register a BRAND-NEW quest_id with the engine.
-- ============================================================
--
-- THE PROBLEM (until now)
-- -----------------------
-- The other examples reskin EXISTING vanilla quests because Sacred has
-- a fixed quest-display registry initialized at savegame load. Our
-- bake-time tag-0x35 (`log_entry`) records for any unknown quest_id
-- silently no-op: the mutator scans the registry by quest_id, doesn't
-- find a match, and returns without writing anything.
--
-- THE FIX
-- -------
-- The DLL now exposes Sacred's underlying `vector<questEntry>::resize`
-- as `sacred.questbook_register(quest_id)`. It grows the vector by one
-- entry and stamps your quest_id at the scan key (entry+8). That single
-- write is what makes the entry "discoverable" by tag-0x35 / 0x57 / 0x40
-- / etc — the same handlers vanilla uses.
--
-- TIMING — why on_world_load
-- --------------------------
-- The walker that DISPATCHES tag-0x35 records fires repeatedly during
-- world-load. If you call register from a regular trigger, you'd be
-- racing those record fires (and likely losing). `sacred.on_world_load`
-- fires ONCE per save load, the moment the walker captures the
-- cQuestManager pointer — BEFORE any record is dispatched. Register
-- there and your tag-0x35 records (returned at the bottom of this file)
-- will hit the new entry.
--
-- LIFECYCLE
-- ---------
-- The registry is rebuilt from the savegame each load. So:
--   * register on the FIRST load this Sacred session — `on_world_load`
--     fires once per process today. Switching saves WITHOUT restarting
--     Sacred will keep our last-registered ids in place (because the
--     cQuestManager singleton is the same), but `on_world_load` won't
--     re-fire. Restart Sacred between save-tests for now.
--   * your bake-time q.log_entry records re-dispatch each load,
--     so they paint your text into the freshly-grown entry every time.
-- No cross-save persistence on the entry itself; persistence of the
-- quest's state is via Sacred's named-state store (see example 09).

local q = require "quest"

-- Pick a quest_id that doesn't clash with vanilla. Vanilla uses ids in
-- ranges roughly 1000..7000. We use 9512 (was a recurring placeholder
-- during recon, no vanilla collision known).
local SDK_QUEST_ID = 9512

-- ============================================================
-- BAKE-TIME: emit log entries for our quest_id.
-- These records DISPATCH at world load via the walker. They'll succeed
-- because our on_world_load handler (below) registers the quest BEFORE
-- the walker reaches them.
-- ============================================================
local bake_records = q.log_entry(
  SDK_QUEST_ID,
  q.T"My First SDK Quest",                            -- Title (slot 0)
  q.T"Adventures in Modding",                         -- Header (slot 1)
  q.T"This entire quest was injected at runtime by " ..
    "the SacredSDK. The questbook didn't know about " ..
    "id " .. SDK_QUEST_ID .. " until the SDK called " ..
    "vector::resize on its registry."                 -- Start text (slot 2)
)

-- ============================================================
-- RUNTIME: register the quest_id on each world load.
-- ============================================================
-- DISABLED: on_world_load is GLOBAL, so this demo injected SDK_QUEST_ID
-- (9512) into every class incl. the live NetScript Vampiress canvas.
-- Flip to true only when demonstrating questbook_register in isolation.
local EX10_REGISTER = false
if EX10_REGISTER then
sacred.on_world_load(function()
  local idx = sacred.questbook_register(SDK_QUEST_ID)
  if idx then
    sacred.log(("[ex10] quest_id=%d registered at idx=%d (%d entries total)")
                :format(SDK_QUEST_ID, idx, sacred.questbook_count()))
  else
    sacred.log("[ex10] questbook_register failed — see sdk_loaded.log")
  end
end)
end

-- ============================================================
-- DEBUG: dump all currently-registered quest_ids on a triggerable
-- resource. After SDK_QUEST_ID is registered, you should see it in
-- the dump alongside vanilla ids. Look in sdk\logs\sdk_loaded.log.
-- ============================================================
sacred.on_trigger("9999", function()
  sacred.questbook_dump()
end)

-- Hand the bake records to the SDK assembler.
return bake_records
