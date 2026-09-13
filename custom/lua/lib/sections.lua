-- SacredSDK / lua / lib / sections.lua
--
-- Build and register SDK-owned script sections (sacred.section_define, see
-- sdk/sdk_sections.inc). A section is a string of FunkCode records in the
-- engine's TLV format: tag u8, size u16 BIG-ENDIAN including the 3-byte header,
-- then the payload (a flag byte + the opcode stream). The shapes are copied
-- from vanilla records (.claude/knowledge/quests/RE_dialog_buttons.md §11.1).
--
--   local S = require "sections"
--   S.define("Dialog:My NPC", S.text("MY_KEY"), S.button(S.ACCEPT, "my_yes"))
--   S.define("my_yes")                        -- an empty handler section
--   sacred.on_trigger("SECTION:my_yes", function() ... end)
--
-- Npc:dialog (npcobj.lua) wraps all of this for the common case.

local S = {}

-- Button labels. "res:<id>" or "res:<NAME>" (a key baked with text.lua); a label
-- without "res:" gets id 0 and the engine creates no button (RE_dialog §2.2).
S.OK, S.ACCEPT, S.REJECT = "res:1024", "res:1037", "res:1038"

-- 255 bytes at most: the engine reads the tag and the size as two little-endian
-- u16 (bytes 0-1, 2-3), which equals this big-endian size only while byte 1 is 0
-- (MECHANICS §1.2; no vanilla record is longer than 169 bytes).
local function rec(tag, payload)
  local size = #payload + 3
  assert(size <= 255, "sections: a record over 255 bytes would misread in the engine")
  return string.char(tag, (size >> 8) & 0xFF, size & 0xFF) .. payload
end
S.rec = rec

-- Text (tag 0x1a): op 0x1e + a global.res key. Fills the window text and sets
-- the talk signal the conversation needs to open a window.
function S.text(key)
  return rec(0x1A, "\0\30" .. key .. "\0")
end

-- SetButton (tag 0x3c): op 0x01 label, op 0x01 action = the section run on click.
-- At most 4 buttons show per conversation.
function S.button(label, action)
  return rec(0x3C, "\0\1" .. label .. "\0\1" .. action .. "\0")
end

local function u32(v) return string.pack("<I4", v & 0xFFFFFFFF) end
local function i32(v) return string.pack("<i4", v) end

-- Objective counters (vanilla shapes, RE_conditions_vars.md §4.1). `types` is a
-- creature or item type id, or a list of them. At zero the engine runs `section`
-- and drops the counter; count 0 removes a running counter for those types.
-- SetOnKill (tag 0x87): kills of creatures of `types`.
function S.set_on_kill(types, count, section)
  local p = { "\0" }
  for _, t in ipairs(type(types) == "table" and types or { types }) do p[#p + 1] = "\2" .. u32(t) end
  p[#p + 1] = "\11" .. i32(count)
  if section then p[#p + 1] = "\5" .. section .. "\0" end
  return rec(0x87, table.concat(p))
end

-- SetOnCollect (tag 0x88): pickups of items of `types` (op 0x41, as vanilla).
function S.set_on_collect(types, count, section)
  local p = { "\0" }
  for _, t in ipairs(type(types) == "table" and types or { types }) do p[#p + 1] = "\2" .. u32(t) end
  p[#p + 1] = "\11" .. i32(count)
  if section then p[#p + 1] = "\65" .. section .. "\0" end
  return rec(0x88, table.concat(p))
end

-- SetDrop (tag 0x89): creatures of type `creature` drop `item` with `chance` %;
-- chance 0 removes the rule.
function S.set_drop(creature, item, chance)
  return rec(0x89, "\0\2" .. u32(creature) .. "\11" .. i32(chance) .. "\2" .. u32(item))
end

-- SetVarBit (tag 0x44): bit `bit` of variable `name` (op 0x01 name, op 0x0b
-- u32 bit), the vanilla shape `44 | 01 'heroqbit' | 0b 8`. With the name
-- "HeroQBit" the engine sets the hero's own quest bit instead (FUN_0049b840).
function S.set_var_bit(name, bit)
  return rec(0x44, "\0\1" .. name .. "\0\11" .. u32(bit))
end

-- InfoPlayer (tag 0x84): the game's own on-screen message for `key` (a name
-- baked with text.lua, or a global.res number). Selector 0 plays chime 200,
-- selector 1 chime 201.
function S.info(key, selector)
  return rec(0x84, "\0\11" .. i32(selector or 0) .. "\1res:" .. tostring(key) .. "\0")
end

-- CreateObj (tag 0x08): an object or item of `type_id` (vanilla shapes,
-- RE_rewards_npc_world.md §2.4). opts:
--   pos  = a position name, default "CPOS:HERO" (op 0x04: i32 -2, then the name)
--   name = the object's script name, "res:<id|KEY>" (op 0x01); optional
--   give = true: straight into the hero's inventory (op 0x61 without 0x8e), by
--          the engine's addMainQuestItem, no world object. Vanilla hands out
--          quest items, potions and equipment this way. Without it an item is
--          placed in the world at `pos`.
function S.create_obj(type_id, opts)
  opts = opts or {}
  local p = { "\0" }
  if opts.name then p[#p + 1] = "\1" .. opts.name .. "\0" end
  p[#p + 1] = "\2" .. u32(type_id)
  p[#p + 1] = "\4" .. i32(-2) .. (opts.pos or "CPOS:HERO") .. "\0"
  if opts.give then p[#p + 1] = "\97" end
  return rec(0x08, table.concat(p))
end

-- Register a section from record strings; returns the SDK section number.
function S.define(name, ...)
  if not sacred.section_define then
    sacred.log("[sections] this SDK build has no sacred.section_define")
    return nil
  end
  return sacred.section_define(name, table.concat({ ... }))
end

-- The same, but OWNED by quest `qid` (the section entry's +0x48). The engine
-- refuses to run a quest-owned section until that quest is set up -- the vanilla
-- gate on the 6,196 owned sections of a shipped script (MECHANICS 1.4). Only
-- useful for a quest the engine knows: see nativequest.lua.
function S.define_owned(name, qid, ...)
  if not sacred.section_define then
    sacred.log("[sections] this SDK build has no sacred.section_define")
    return nil
  end
  return sacred.section_define(name, table.concat({ ... }), qid)
end

return S
