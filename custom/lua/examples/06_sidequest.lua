-- ============================================================
-- Example 06: A full side-quest, copy-paste ready.
-- ============================================================
--
-- This is THE template to start from when you want to add a new side quest
-- to Sacred. The file demonstrates EVERY moving part of an authored quest:
--
--   * inline strings via T"..." (auto-baked into custom/scripts/us/global.res)
--   * quest log entries (Title / Header / opening text)
--   * NPC dialog scenes (greet / give-quest / turn-in)
--   * quest state via qbits and named variables
--   * appending your quest to a vanilla NPC class WITHOUT erasing the rest
--
-- HOW TO USE
-- ----------
--   1. Pick which vanilla NPC class will host your quest. Most side quests
--      hang off a town NPC class like TYPE_NPC_GLADIATOR or
--      TYPE_NPC_SERAPHIM. Copy this file to:
--        custom/lua/bin/TYPE_NPC_<class>/FunkCode.lua
--   2. Make sure the vanilla snapshot for that class is decompiled to
--      custom/lua/_vanilla/bin/TYPE_NPC_<class>/FunkCode.lua (one-time):
--        python sdk/re/py/funkcode_decompile_lua.py ^
--               bin/TYPE_NPC_<class>/FunkCode.bin ^
--               -o custom/lua/_vanilla/bin/TYPE_NPC_<class>/FunkCode.lua
--   3. Edit the trigger names, QUEST_ID, and dialogue text below.
--   4. Launch Sacred. Bake takes 2-3 s on first run.
--
-- WHAT WORKS TODAY vs WHAT IS TODO
-- --------------------------------
-- ✅ Inline strings — T"..." gets a unique slot in global.res automatically
-- ✅ Journal entries — q.log_entry writes three native log records
-- ✅ NPC dialogue — d.trigger / d.line / d.emit triple per scene
-- ✅ Quest state — q.var / q.assign / q.set_hero_qbit
-- ✅ Inventory check — q.has_item(item_res) emits the native 0x3a predicate;
--    q.if_has_item(item, {then_body}) wraps it with the 0x42 THEN block.
-- ✅ Gold reward — q.give_gold(amount) emits the native 0x12 HeroOp_b record.
--    Negative amounts charge the hero; q.charge_gold(N) is a positive alias.
-- 🔴 Runtime branching from Lua — `if has_item then dialogA else dialogB end`
--    in your .lua runs at BAKE time, not game time. Sacred-native conditionals
--    (tag-0x3a/0x42) ARE possible — see examples/05_conditional_dialog.lua.
--    The ELSE branch (STACK_96 / STACK_97 markers) is partly decoded; for now
--    you build it by chaining records via raw.rec.

local q   = require "quest"
local d   = require "dialog"
local v   = require "vanilla"
local T   = require "text"

-- ============================================================
-- TWEAK THIS BLOCK FOR YOUR QUEST
-- ============================================================

-- 1) Which vanilla NPC class hosts the quest? This MUST match the directory
--    name you placed this file in (bin/TYPE_NPC_<HOST>/FunkCode.lua).
local HOST_CLASS = "TYPE_NPC_SERAPHIM"

-- 2) Pick a free Sacred-internal quest id. Vanilla uses 1..9510 for shipped
--    content; we leave 9500..9999 free for SDK mods. Don't clash with another
--    mod in your install.
local QUEST_ID   = 9512

-- 3) Trigger names. These are how Sacred routes player choices to your
--    dialog scenes. Convention: `<MyQuest>_DLG_<state>`. Pick names that
--    don't collide with anything else in your install.
local TRG_START  = "MyQuest_DLG_START"   -- player walks up to NPC
local TRG_GIVE   = "MyQuest_DLG_GIVE"    -- player accepts the quest
local TRG_TURNIN = "MyQuest_DLG_TURNIN"  -- player returns to NPC
local TRG_POST   = "MyQuest_DLG_POST"    -- post-completion chitchat

-- 4) Internal state vars (per-hero numeric storage). Names are arbitrary but
--    should be unique across the whole save — prefix with your mod name.
local VAR_STEP   = "myq_step"            -- 0=not_started, 1=given, 2=done

-- 5) The "fetch" item the player must bring back. Use any valid item resource
--    id (Sacred ships ~17000-18000). 17562 is a known consumable used in
--    vanilla's whiskey quest, perfect for testing.
local ITEM_TOME  = 17562

-- 6) Reward in gold pieces.
local REWARD_GOLD = 500

-- ============================================================
-- QUEST BODY
-- ============================================================
--
-- All inline strings below are T"…" so they end up in global.res with stable
-- auto-generated keys. Two T() calls with the SAME text dedupe to the same
-- key (FNV-1a content tag), so feel free to reuse phrases.

local quest = q.script {

  -- ----- state -----
  q.var(VAR_STEP),
  q.assign(VAR_STEP, 0),

  -- ----- journal entry (shows up when QUEST_ID's qbit is set) -----
  q.log_entry(QUEST_ID,
    T "The Lost Tome of Ancaria",         -- title shown in the log list
    T "Chapter 1",                        -- header within the entry
    T "A young monk runs up to you, breathless. " ..
      "An ancient tome was stolen from the monastery " ..
      "library — she begs you to find it before the cult " ..
      "uses its secrets."
  ),

  -- ----- scene 1: NPC greets the hero, offers the quest -----
  d.trigger(TRG_START),
  d.line(
    T "Adventurer! Please, you must help us. " ..
      "A relic has been stolen — the Tome of Ancaria. " ..
      "Without it, our wards over the catacombs will fail.",
    "btn_ok"),
  d.emit(31, tostring(QUEST_ID)),

  -- ----- scene 2: player accepts; set the qbit and step var -----
  d.trigger(TRG_GIVE),
  q.assign(VAR_STEP, 1),
  q.set_hero_qbit(QUEST_ID),  -- journal entry becomes visible
  d.line(
    T "Thank you, brave one. The thieves fled south, " ..
      "toward the catacombs. May the gods light your path.",
    "btn_ok"),
  d.emit(31, tostring(QUEST_ID)),

  -- ----- scene 3: hero returns. Engine checks for the item natively. -----
  -- q.if_has_item emits the tag-0x3a predicate + tag-0x42 wrapper so the
  -- THEN body runs ONLY when the hero is carrying item ITEM_TOME at
  -- game-time. (Encoding lives in lib/inventory.lua; see HANDOFF #19.)
  d.trigger(TRG_TURNIN),
  q.if_has_item(ITEM_TOME, {
    q.assign(VAR_STEP, 2),
    q.give_gold(REWARD_GOLD),               -- native 0x12 HeroOp_b record
    d.line(
      T "You found it! Praise be. " ..
        "Here, take this purse as a token of our thanks — " ..
        ("%d gold pieces. May they ease your journey."):format(REWARD_GOLD),
      "btn_ok"),
    d.emit(31, tostring(QUEST_ID)),
  }),

  -- ----- scene 4: post-completion chitchat (re-talk to NPC) -----
  d.trigger(TRG_POST),
  d.line(
    T "The wards hold strong thanks to you. " ..
      "If ever you pass this way again, you'll find a friend.",
    "btn_ok"),
  d.emit(31, tostring(QUEST_ID)),
}

-- ============================================================
-- ASSEMBLE: append our quest to the vanilla class file
-- ============================================================
--
-- We load the existing class so all the NPC's other quests/dialogs stay
-- intact, then concatenate our new records onto the end. The runtime walks
-- the whole record list and triggers fire by name regardless of position.
--
-- If you'd rather REPLACE the whole class (rare), drop the v.load and just
-- `return quest`.

local recs = v.load("bin/" .. HOST_CLASS .. "/FunkCode")
for _, r in ipairs(quest) do recs[#recs+1] = r end
return recs
