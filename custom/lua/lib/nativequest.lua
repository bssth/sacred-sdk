-- SacredSDK / lua / lib / nativequest.lua
--
-- A quest the ENGINE knows: an entry in its quest registry, driven by the four
-- vanilla lifecycle records, with the engine writing the journal entry itself.
--
-- Until now an SDK quest imitated the engine: sacred.questbook_register built a
-- journal entry by hand, and ExitQuest / LoseQuest could not be used at all,
-- because those records refuse an id that is in no registry. sacred.quest_register
-- (sdk/sdk_quests.inc) appends our ids to that registry at run time, so the
-- engine's own path works on them:
--
--   q:setup()   SetUpQuest    flags bit 0; runs QIS_OnSetUp<id> once, then Trigger
--   q:enter()   TriggerQuest  runs QIS_Trigger<id>; if it runs to its end the
--                             ENGINE creates the journal entry (category 3 for
--                             ids 1..99, else 4), registers the compass column,
--                             plays the entry fanfare, then runs QIS_OnEnter<id>
--   q:solve()   ExitQuest     journal state 100, the solved sound, QIS_OnExit<id>
--   q:fail()    LoseQuest     journal state 101, QIS_OnLose<id>
--
-- The five QIS_* sections are SDK sections (sections.lua), so each one can carry
-- vanilla records AND fire a Lua callback when the engine runs it. Records go in
-- the section itself when they must run at exactly that moment (the journal
-- title on enter); the Lua callback arrives on the next heartbeat.
--
--   local NQ = require "nativequest"
--   local q = NQ.define{
--     id = 9501, name = "Brigands",
--     enter_records = { Vb.quest_log(9501, 0, "res:BRIG_TITLE") },
--     on_enter = function() Log("entered") end,
--     on_exit  = function() Log("solved") end,
--   }
--   q:start()                        -- set up + enter, one section run
--   ...
--   q:solve()
--
-- Ids: use something outside the shipped ranges. Vanilla base ids are 1..99
-- (story), 100.. (side), DQ slots 2600..3599 and the 9000..9499 block are taken;
-- 9500+ is free. An id in 1..99 gets the STORY journal category and the big grey
-- story arrow -- only one story quest makes sense at a time.

local S  = require "sections"
local A  = require "actions"
local Vb = require "verbs"

local NQ = {}
local quests = {}                       -- id -> quest
local KINDS = { setup = "QIS_OnSetUp%d", trigger = "QIS_Trigger%d",
                enter = "QIS_OnEnter%d", exit = "QIS_OnExit%d", lose = "QIS_OnLose%d" }

local Q = {}
Q.__index = Q

local function log(fmt, ...)
  if sacred and sacred.log then sacred.log("[nativequest] " .. fmt:format(...)) end
end

local function records_of(spec, key)
  local r = spec[key]
  if r == nil then return "" end
  if type(r) == "string" then return r end
  assert(type(r) == "table", "nativequest: " .. key .. " must be a record string or a list of them")
  return table.concat(r)
end

-- One SDK section per lifecycle hook. Its body is the caller's records; running
-- it also queues "SECTION:<name>", which dispatches to the Lua callback.
function Q:_wire()
  for kind, fmt in pairs(KINDS) do
    local name = fmt:format(self.id)
    S.define(name, self.records[kind] or "")
    if not self.wired[kind] then
      self.wired[kind] = true
      sacred.on_trigger("SECTION:" .. name, function(...)
        local fn = self.handlers[kind]
        log("quest %d: %s", self.id, kind)
        if fn then fn(self, ...) end
      end)
    end
  end
  assert(sacred.quest_register, "nativequest: this SDK build has no sacred.quest_register")
  sacred.quest_register(self.id, self.name)
end

-- Define (or redefine) a quest. Redefining replaces the records and callbacks
-- and leaves the engine's state alone, so a reloaded script keeps its progress.
function NQ.define(spec)
  assert(type(spec) == "table", "nativequest.define: needs a table")
  local id = math.tointeger(spec.id)
  assert(id and id > 0, "nativequest.define: id must be a positive integer")
  local q = quests[id]
  if not q then
    q = setmetatable({ id = id, wired = {}, handlers = {}, records = {} }, Q)
    quests[id] = q
  end
  q.name = spec.name or ("SDK quest %d"):format(id)
  q.handlers = { setup = spec.on_setup, trigger = spec.on_trigger,
                 enter = spec.on_enter, exit = spec.on_exit, lose = spec.on_lose }
  q.records  = { setup = records_of(spec, "setup_records"),
                 trigger = records_of(spec, "trigger_records"),
                 enter = records_of(spec, "enter_records"),
                 exit  = records_of(spec, "exit_records"),
                 lose  = records_of(spec, "lose_records") }
  q:_wire()
  return q
end

function NQ.get(id) return quests[math.tointeger(id) or -1] end

-- ---- the lifecycle ------------------------------------------------------------
-- Each runs its record through an SDK section on the next heartbeat (actions.lua).

function Q:setup()  return A.run(Vb.setup_quest(self.id)) end
function Q:enter()  return A.run(Vb.trigger_quest(self.id)) end
function Q:solve()  return A.run(Vb.exit_quest(self.id)) end
function Q:fail()   return A.run(Vb.lose_quest(self.id)) end

-- Set up and enter in ONE section run, the way a vanilla giver's accept button
-- does it (SetUpQuest then TriggerQuest, same record stream).
function Q:start()
  return A.run(Vb.setup_quest(self.id), Vb.trigger_quest(self.id))
end

-- A journal line for this quest: sub 0 = the title, anything else appends the
-- next body line (10 at most). Only after the quest is entered.
function Q:log(sub, key)
  return A.run(Vb.quest_log(self.id, sub, key))
end

-- The compass arrow of this quest's own journal entry.
function Q:compass(x, y) return A.run(Vb.compass_pos(x, y, self.id)) end
function Q:compass_off() return A.run(Vb.compass_off(self.id)) end

-- ---- state --------------------------------------------------------------------
-- The live registry entry: { index, name, flags, setup, entered, done, trigger,
-- on_enter, on_setup, on_exit, on_lose, sdk } or nil while no script is loaded.
function Q:state() return sacred.quest_state and sacred.quest_state(self.id) or nil end

function Q:is_setup()   local s = self:state() return s ~= nil and s.setup end
function Q:is_entered() local s = self:state() return s ~= nil and s.entered end
function Q:is_done()    local s = self:state() return s ~= nil and s.done end

-- Testing aid: forget that the quest was entered and finished, so it can be
-- played again in the same session. The journal entry it left is not removed.
function Q:reset()
  if not sacred.quest_flags then return nil end
  return sacred.quest_flags(self.id, 0, 2 + 4)          -- clear done | entered
end

return NQ
