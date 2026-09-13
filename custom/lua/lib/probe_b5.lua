-- SacredSDK / lua / lib / probe_b5.lua
--
-- BATCH 5, run 4. What runs 2 and 3 already settled (log of 2026-09-12, all
-- measured, nothing inferred):
--
--   AREA TRIGGERS WORK.  SetBaseTrigger (0x04) + an SDK section "OMO-1<name>":
--     the engine ran our section 50 times during one walk through the west
--     square -- every heartbeat somebody is inside, once per creature (three
--     runs landed in the same millisecond: the hero and two party NPCs). So the
--     raw trigger is a presence pulse, not an event; `zones.lua` now turns it
--     into enter / leave and says whether it was the hero.
--   SECTION RESTART (0x6f) WORKS: the counter variable ended at 2, so the
--     section ran its records, restarted at record 0 and stopped on the guard.
--   SECTION LATCH (0x0e) WORKS: three runs of the latched section, one read.
--   UnsetVarBit (0x45) WORKS: the bit read 8, then 0.
--   MORPH (0x4f) WORKS: the sheep's type went 544 -> 569 and it became a wolf.
--   AUTOSAVE (0x79) WORKS: the engine saved a second later ("game saved, quest
--     state with 691 variables").
--   AN OWNER-GATED SDK SECTION RUNS: QIS_OnEnter9502 was defined with owner =
--     9502 and the engine ran it after TriggerQuest -- the first use of the
--     section owner field.
--   CUT SCENES: both SetAnimMode (0x58) and SetCinemaMode (0x86) queued the
--     walk and the animation on the actor (queue 0 -> 2) AND the section ran to
--     its last record. So 0x86's early return -- which reads the queue of the
--     section's own creature -- never fires for an SDK section, whose creature
--     is NULL. Batch 4's "nothing happened" was not this.
--
-- Run 4 added: the BARRIER BLOCKS (CreateTrigger + TriggerPatch + close/lock
-- across the road -- "барьер ок"), the ONE-SHOT zone fired exactly once and
-- deleted itself, and the debounced west zone gave two clean ENTER/LEAVE pairs.
-- The one wart was the message INSIDE the handler section: the engine reran it
-- on every pulse, so it filled the screen. zones.lua now defaults to one event
-- per entry -- the handler deletes its own rectangle, vanilla's own trick, and
-- the module puts it back once the hero is out -- and wraps the barrier too.
--
-- WHAT THIS RUN ASKS, and nothing else:
--   1 ONE MESSAGE PER ENTRY  walk into the west square: the engine's own message
--                    must appear ONCE, not once per step. Walk out and back in:
--                    it appears again, because the rectangle went back up.
--   2 ONE-SHOT       the east square, again, to see it stay dead after firing.
--   3 THE BARRIER    now built through Z.barrier/Z.line. Closed for two minutes,
--                    then opened from Lua.
--   4 JOURNAL PAGE   quest 9502 again, so the tab it files under can be seen.

local S    = require "sections"
local Vb   = require "verbs"
local A    = require "actions"
local V    = require "vars"
local T    = require "text"
local Z    = require "zones"
local NQ   = require "nativequest"

local M = {}

local CX, CY = 2793, 2284               -- Captain Miles
local SX, SY = 2796, 2261               -- where the hero starts: the barrier goes between
local QID    = 9502

local function log(fmt, ...)
  sacred.log("[b5] " .. (select("#", ...) > 0 and fmt:format(...) or fmt))
end

-- ---- text ----------------------------------------------------------------------

T.named("B5_START", "Batch 5: a barrier lies between you and the captain. Two squares wait beyond him.")
T.named("B5_Z1",    "East square: the one-shot trigger fired and deleted itself.")
T.named("B5_Z2",    "West square: you are inside.")
T.named("B5_WALL",  "The barrier is open now.")
T.named("B5_QTITLE", "Batch 5: the engine's own trigger zones")
T.named("B5_QLOG",   "The captain closed the road and marked two squares on the ground.")

-- ---- 1 and 2: the two squares --------------------------------------------------

local west = Z.define("SDKB5Z2", {
  rect    = { CX - 18, CY - 6, CX - 6, CY + 6 },
  records = { Vb.info("B5_Z2") },
  on_enter = function(z, who)
    M.w_enter = (M.w_enter or 0) + 1
    log("WEST ENTERED (%d) by the %s", M.w_enter, who)
  end,
  on_leave = function(z)
    M.w_leave = (M.w_leave or 0) + 1
    log("WEST LEFT (%d)", M.w_leave)
  end,
})

local east = Z.define("SDKB5Z1", {
  rect    = { CX + 6, CY - 6, CX + 18, CY + 6 },
  once    = true,
  records = { Vb.info("B5_Z1") },
  on_enter = function(z, who)
    M.e_enter = (M.e_enter or 0) + 1
    log("EAST ENTERED (%d) by the %s -- the section deleted the trigger, so this is the only one",
      M.e_enter, who)
  end,
})

-- ---- 3: the barrier ------------------------------------------------------------
-- Across the road at the halfway point, nine cells wide and two deep, so it
-- cannot be stepped over by accident -- but it is short enough to walk around.

local WALL = "sdkb5wall"
local WX, WY = SX, (SY + CY) // 2
local wall

local function build_wall()
  wall = Z.barrier(WALL, Z.line(WX - 4, WY, WX + 4, WY, 2))
  A.run(Vb.map_icon(WX, WY, 2))
  log("barrier closed and locked across (%d..%d, %d..%d)", WX - 4, WX + 4, WY, WY + 1)
end

local function open_wall()
  if wall then wall:open() end
  A.run(Vb.info("B5_WALL"))
  log("barrier opened -- walk through it now")
end

-- ---- 4: the journal page -------------------------------------------------------

local quest

local function wire_quest()
  quest = NQ.define{
    id = QID, name = "Batch 5 zones",
    on_enter = function() log("quest %d entered (the engine wrote the journal entry)", QID) end,
  }
  -- SetQuestInfo carries no quest id: it writes the entry of the section's OWNER
  -- quest. Proven to run in run 3; page 2 is what to look for in the journal.
  S.define_owned("QIS_OnEnter" .. QID, QID,
    Vb.quest_page(2),
    Vb.quest_log(QID, 0, "res:B5_QTITLE"),
    Vb.quest_log(QID, 1, "res:B5_QLOG"))
end

-- ---- the schedule --------------------------------------------------------------

local STEPS = {
  [8]   = function()
            wire_quest()
            build_wall()                      -- first, before the hero can walk past
            A.run(Vb.info("B5_START"))
          end,
  [16]  = function()
            west:arm()
            east:arm()
            A.run(Vb.map_icon(CX + 12, CY, 1), Vb.map_icon(CX - 12, CY, 2))
            log("squares armed and marked on the map; the barrier carries the cave icon")
          end,
  [40]  = function() quest:start() end,
  [56]  = function()
            A.run(Vb.activate_quest(QID))
            log("ActivateQuest %d sent -- the journal should open on it, page 2", QID)
          end,
  [480] = function() open_wall() end,         -- two minutes of a closed road
  [700] = function()
            log("SUMMARY west enter=%s leave=%s | east enter=%s (must be 1 at most)",
              tostring(M.w_enter), tostring(M.w_leave), tostring(M.e_enter))
          end,
}

function M.start()
  V.on_ready(function() M.t = 0 end)
  sacred.on_tick(function()
    if not V.is_ready() or not M.t then return end
    M.t = M.t + 1
    if M.t % 20 == 0 then log("alive t=%d", M.t) end
    local step = STEPS[M.t]
    if step then
      local ok, err = pcall(step)
      if not ok then log("step %d failed: %s", M.t, tostring(err)) end
    end
  end)
  log("batch 5 run 4 armed: captain at %d,%d, barrier at %d,%d", CX, CY, WX, WY)
end

return M
