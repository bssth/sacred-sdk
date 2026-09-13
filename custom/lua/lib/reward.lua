-- SacredSDK / lua / lib / reward.lua
--
-- Hand stuff TO the hero. Sacred encodes "give gold" (and "take gold") via a
-- tag-0x12 `HeroOp_b` record whose single op is `DLG_OP_a("hero", trailer)`
-- where the trailer is 5 raw bytes:
--
--     trailer = "\x0b" .. u32le(amount_signed)
--
-- Positive amounts give gold; negative amounts charge the hero. Verified
-- across vanilla records 14649 (give 900000) and several DQ_*_NPC quests
-- that charge with negative values (e.g. -2400000 for a major purchase).
--
-- A second form ("named-variable indirect") stores the amount in a global
-- variable and references it by name. Used by vanilla's `Belohnung_Whiskey`
-- pattern. We expose that as `give_gold_from_var`.
--
-- HANDOFF.md task 19 carries the full reverse-engineering note.
--
-- Items, at game time:
--   give_item(type)        a new item into the hero's backpack, by the engine's
--                          own cCreature_inventory_putItem (sacred.hero_put_item).
--                          With the backpack full it lies at the hero's feet
--                          (sacred.spawn_item). A real item: wear it, sell it.
--   take_item(type[, n])   items of that type out of the backpack or off the
--                          hero, then destroyed, as vanilla's DelOBJ takes a
--                          held item (sacred.hero_take_item).
--   give_quest_item(type)  vanilla's CreateObj op 0x61 give (without 0x8e): the
--                          engine's addMainQuestItem puts the type on the hero's
--                          QUEST-ITEM list, not in the backpack (live log,
--                          2026-09-11). For things to carry and hand in, like
--                          vanilla's keys, scrolls and "The Finished Prototype of
--                          the Sword". Give it a name: a nameless entry has res 0,
--                          and the engine's HasItem by type matches any item
--                          against that.
-- Tag 0x37 never gives an item (SDK_GAPS gap 7): do not build it.

local raw = require "raw"
local S   = require "sections"

local M = {}

-- Give the hero `amount` gold. Negative removes gold. `amount` must fit in a
-- signed 32-bit integer (Sacred's purse caps at ~2.1G, but go gentle).
function M.give_gold(amount)
  assert(type(amount) == "number" and math.tointeger(amount),
         "reward.give_gold: amount must be an integer, got " .. tostring(amount))
  local tail = "\x0b" .. string.pack("<i4", amount)  -- 5 raw bytes
  return raw.rec(0x12, 0x00, {"DLG_OP_a", "hero", tail})
end

-- Same record, but the amount comes from a named global var (Sacred resolves
-- it at game-time). The marker `\x0by)\xed\xff` is the disassembler's
-- `magic = -0x12d687` that means "and now read a cstring with the var name".
-- Use when the reward should depend on hero state computed elsewhere.
function M.give_gold_from_var(varname)
  assert(type(varname) == "string" and #varname > 0,
         "reward.give_gold_from_var: varname must be a non-empty string")
  local magic = "\x0by)\xed\xff"            -- 5 bytes: \x0b + 0xffed2979 LE
  local tail  = magic .. varname             -- followed by var-name cstring
  return raw.rec(0x12, 0x00, {"DLG_OP_a", "hero", tail})
end

-- Symmetric helper: -amount. Reads more naturally in quest scripts where the
-- NPC is asking the player to pay something.
function M.charge_gold(amount)
  return M.give_gold(-amount)
end

local function check_type(fn, type_id)
  assert(math.tointeger(type_id) and type_id > 0,
         "reward." .. fn .. ": type_id must be a positive integer, got " .. tostring(type_id))
end

-- A new item of `type_id` (an items_gen.lua id) into the hero's backpack, now.
-- Unlike the gold helpers (records to bake) this acts at game time. Returns the
-- item handle and "bag"; nil and "ground" when the backpack was full and the
-- item lies at the hero's feet; nil and "failed" otherwise.
function M.give_item(type_id)
  check_type("give_item", type_id)
  if not sacred.hero_put_item then
    sacred.log("[reward] this SDK build has no sacred.hero_put_item")
    return nil, "failed"
  end
  local h = sacred.hero_put_item(type_id)
  if h then return h, "bag" end
  local kx, ky = sacred.hero_pos()
  if kx and sacred.spawn_item and sacred.spawn_item(type_id, kx, ky) then return nil, "ground" end
  return nil, "failed"
end

-- Take `n` (default 1) items of `type_id` from the hero: the backpack first,
-- then what the hero wears. Returns how many were taken. Entries on the
-- quest-item list (give_quest_item) are not touched.
function M.take_item(type_id, n)
  check_type("take_item", type_id)
  if not sacred.hero_take_item then
    sacred.log("[reward] this SDK build has no sacred.hero_take_item")
    return 0
  end
  return sacred.hero_take_item(type_id, n or 1) or 0
end

-- Put `type_id` on the hero's quest-item list (see the header). The record runs
-- through the engine's own CreateObj handler (sacred.section_run) on the next
-- tick; returns true when it is queued. `name` = the entry's script name,
-- "res:<id|KEY>".
function M.give_quest_item(type_id, name)
  check_type("give_quest_item", type_id)
  if not sacred.section_run then
    sacred.log("[reward] this SDK build has no sacred.section_run")
    return false
  end
  local sec = "sdk_give_" .. type_id
  S.define(sec, S.create_obj(type_id, { name = name, give = true }))
  return sacred.section_run(sec)
end

return M
