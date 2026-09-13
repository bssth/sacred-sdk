-- SacredSDK / lua / lib / scene.lua
--
-- Cut scenes, built the way the two shipped ones are (MECHANICS §6, the
-- `Daemonin_Tutorial` and `btn_Deamonstart` sections of base:SERAPHIM).
--
-- A scene is ONE section: SetCinemaMode (0x86), the actor commands, SetPlayMode
-- (0x59). While cinema mode is on, an actor command is not executed: it is
-- QUEUED on that actor (creature+0x588, 0x44 bytes per entry), and the actors
-- play their queues out in order once the section ends. That is what makes a
-- scene a sequence instead of everything happening at once.
--
--   local Sc = require "scene"
--   Sc.play(
--     Sc.walk_to("res:GUIDE", "HERO"),     -- the guide walks up to the hero
--     Sc.wait_for("res:GUIDE", "HERO"),    -- ...and waits until it is there
--     Sc.anim("res:GUIDE", 89),
--     Sc.wait("res:GUIDE", 2),
--     Sc.talk("res:GUIDE", "MY_DLG_NODE")) -- then opens that dialog node
--
-- Actors are named the way every record names a creature: the "res:<KEY>" they
-- were spawned with, or "HERO". The hero can be an actor too -- the shipped
-- `btn_Deamonstart` walks the HERO to the NPC.
--
-- WHAT QUEUES AND WHAT DOES NOT (live, 2026-09-12, four runs measured against
-- the actor's queue length):
--   * `walk_to` a POSITION and `anim` queue and play IN ORDER -- the guide
--     walked tile by tile and gestured on arrival, queue 2 -> 1 -> 0. Teleport,
--     attack and fx use the same queue kind, so they follow the same path.
--   * `walk_to` a CREATURE ("HERO", "res:X") does nothing from an SDK section,
--     although both shipped scenes use that form. Always pass coordinates:
--     `Sc.walk_to(who, { sacred.hero_pos() })`.
--   * `wait_for` / `wait` and `talk` NEVER reach the queue from an SDK section,
--     in any arrangement (one section or one per command). Their handlers want
--     more than the record: FUN_004a1f20 gates on two resolved ids AND a
--     register the walker carries over from the record before it. So sequence
--     the rest from Lua (a tick counter), and for "the NPC speaks to you" use
--     `Sc.say(node)` -- the Popup record, which works and opens the same window.
--
-- SAFETY. An open cinema mode swallows actor commands, so `Sc.play` always
-- closes it in the same section and `Sc.abort()` closes it by hand if a scene
-- was ever left open. `Sc.is_on()` reads the engine's own flag (qm+0xA45C bits
-- 0 and 1), so a script can tell.

local Vb = require "verbs"
local A  = require "actions"

local Sc = {}

local QM_CINEMA = 0x00AACF80 + 0xA45C

-- ---- the steps ----------------------------------------------------------------
-- Walk to a creature ("HERO", "res:<KEY>") or to a position ({x, y} or a name).
function Sc.walk_to(who, target)
  if type(target) == "string" and (target == "HERO" or target:sub(1, 4) == "res:") then
    return Vb.goto_creature(who, target)
  end
  return Vb.npc_goto(who, target)
end

-- Wait until `target` has arrived / finished (the vanilla kind 0x5c).
function Sc.wait_for(who, target) return Vb.wait_for(who, target or who) end

-- Wait `seconds` (kind 0x5f).
function Sc.wait(who, seconds) return Vb.wait_secs(who, seconds or 1) end

-- Play an animation (ids the vanilla dance uses: 53, 54, 55, 57, 89, 100, 154).
function Sc.anim(who, anim, param) return Vb.play_anim(who, anim, param) end

-- Open a dialog node with the hero. NPC_TalkTo (0x47) does not reach the queue
-- from an SDK section (see the note at the top) -- kept because it is the
-- vanilla record; use Sc.say for the effect.
function Sc.talk(who, node) return Vb.talk_to(who, node) end

-- "The NPC speaks to you": the Popup record opens a dialog node with no NPC
-- needed at all, and it works from anywhere. Pass one of our own node names
-- (Npc:bind_quest + Npc:dialog) or a shipped one.
function Sc.say(node) return Vb.popup(node) end

-- Jump (no walking), attack, and a particle effect -- the other queued kinds.
function Sc.teleport(who, p, facing) return Vb.teleport(who, p, facing) end
function Sc.attack(who, target) return Vb.attack(who, target) end
function Sc.fx(who, argb, fx) return Vb.fx_on(who, argb, fx) end

-- Anything else that is not an actor command (a sound, a journal line) still
-- runs immediately, so put it before or after the scene rather than inside.

-- The camera. The addon's intro scene sets it on the hero before cinema mode.
function Sc.focus(who) return Vb.set_focus(who) end

-- ---- the actor's command queue ------------------------------------------------
-- Each actor keeps its queued scene commands in a vector at creature+0x588 /
-- +0x58C, one 0x44-byte entry per command. Reading its length is the only way to
-- tell "queued" from "ran immediately", which is exactly the difference cinema
-- mode makes.
function Sc.queued(handle)
  if not (sacred.peek_u32 and handle) then return nil end
  local om = sacred.peek_u32(0x00AD5C40)
  local arr = om and om ~= 0 and sacred.peek_u32(om + 4)
  local c = arr and arr ~= 0 and sacred.peek_u32(arr + handle * 4)
  if not c or c == 0 then return nil end
  local b, e = sacred.peek_u32(c + 0x588), sacred.peek_u32(c + 0x58C)
  if not b or not e or e < b then return nil end
  return (e - b) // 0x44
end

-- ---- running a scene ----------------------------------------------------------
-- Queue the steps and let the actors play them. One section, cinema mode opened
-- and closed inside it, exactly as vanilla does.
function Sc.play(...)
  local steps = table.concat({ ... })
  if steps == "" then return false end
  return A.run(Vb.cinema_on, steps, Vb.cinema_off)
end

-- The same steps, but cinema mode is opened and closed by SEPARATE section runs,
-- the way the addon does it (`hq15_close_gate` opens it and a timer section
-- closes it later). `Sc.open()` then the steps, then `Sc.close()`.
function Sc.open()  return A.run(Vb.cinema_on) end
function Sc.close() return A.run(Vb.cinema_off) end
function Sc.queue(...) return A.run(table.concat({ ... })) end

-- Close cinema mode by hand. Harmless when it is already closed; the escape
-- hatch if a scene ever gets interrupted half way.
function Sc.abort() return A.run(Vb.cinema_off) end

-- Is the engine in cinema mode right now? (qm+0xA45C bits 0 and 1.)
function Sc.is_on()
  if not sacred.peek_u32 then return nil end
  local v = sacred.peek_u32(QM_CINEMA)
  if v == nil then return nil end
  return (v & 3) ~= 0, v
end

return Sc
