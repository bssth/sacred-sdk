-- SacredSDK / lua / lib / questfsm.lua
--
-- Declarative quest state machines on top of `sacred.on_trigger`.
--
-- Why this exists
-- ---------------
-- `events.on_trigger_once` is fine for one-off reactions, but real quests
-- have ORDER ("you can only redeem the reward AFTER picking up the macguffin")
-- and STATE ("don't fire the 'found it' banner if we've already moved past
-- that step"). Hand-rolling that with bare `on_trigger_once` ends up as a
-- pile of `if _seen.X and not _seen.Y` flags. This module collapses it
-- into a single `fsm.define{ … }` block.
--
-- Conceptual model
-- ----------------
-- A quest is a LINEAR chain of steps (1..N). The FSM enforces:
--
--   * step K can only be entered if step K-1 is the current state
--     (skipping is silently rejected and logged)
--   * each step's `on_enter` fires exactly once per session (no re-fire
--     even if Sacred re-queries the trigger resource id)
--   * an optional `guard` lets a step refuse to advance unless some
--     runtime condition is met (e.g. "hero must carry the item")
--
-- Persistence is in-memory only. State resets each time you launch the
-- game. That's deliberate — Sacred's own qbit/journal already persists
-- inside the save game; our FSM is a transient overlay that re-derives
-- itself when the engine queries trigger resources during play.
-- (Cross-session persistence is tracked as an open SDK issue.)
--
-- Spec format
-- -----------
--   local fsm = require "questfsm"
--
--   local lost_tome = fsm.define {
--     id = "lost_tome",                     -- string id (any unique)
--     steps = {
--       { name = "started",
--         trigger = "17095",                -- Sacred resource id
--         on_enter = function(self, ctx)
--           ctx:notify("Quest accepted: The Lost Tome")
--         end,
--       },
--       { name = "found_book",
--         trigger = "17562",
--         guard = function(self, ctx) return ctx:has_item(17562) end,
--         on_enter = function(self, ctx)
--           ctx:notify("You found the Tome!")
--         end,
--       },
--       { name = "completed",
--         trigger = "17400",
--         on_enter = function(self, ctx)
--           ctx:give_gold(500)
--           ctx:notify("Reward claimed: +500 gold")
--         end,
--       },
--     },
--   }
--
--   -- Anywhere later you can ask:
--   if lost_tome:at("found_book") then … end
--   print(lost_tome:current_step())   -- "started" / "found_book" / "completed" / nil
--
-- Discovery flow for trigger ids
-- ------------------------------
-- Same as `events.lua`: open the F11 overlay → "Runtime triggers" panel
-- → play through the moment, copy the resource id from "Recent trigger
-- names". See `examples/07_runtime_triggers.lua` for screenshots-worth
-- of explanation.

local M = {}

-- Registry of every defined FSM, keyed by its `id`. Lets `M.dump()` and
-- `M.get(id)` work from anywhere without the modder having to keep their
-- own table around.
local _fsms = {}

local Quest = {}
Quest.__index = Quest

-- ---- Quest instance methods --------------------------------------------

-- Index of the step currently entered (1..N), or 0 if not started yet.
function Quest:current_step_index() return self._cur or 0 end

-- Name of the current step ("started" / "found_book" / …) or nil.
function Quest:current_step()
  local i = self._cur or 0
  return i > 0 and self.steps[i].name or nil
end

-- True iff we've reached `step_name` OR any later step.
function Quest:at(step_name)
  local target = self._step_idx[step_name]
  if not target then
    error(("questfsm: %s has no step %q"):format(self.id, step_name), 2)
  end
  return (self._cur or 0) >= target
end

-- True iff we've passed every step (entered the LAST one).
function Quest:done() return (self._cur or 0) >= #self.steps end

-- Force-reset back to "not started". Useful for testing — your mod can
-- call this from a debug trigger, e.g. on a unique resource id you only
-- ever query from a developer console mod.
function Quest:reset()
  if self._cur and self._cur > 0 then
    sacred.log(("[questfsm/%s] reset (was step %d)"):format(self.id, self._cur))
  end
  self._cur = 0
end

-- Internal: try to enter step `idx`. Rejects skipping; rejects re-entry.
function Quest:_advance_to(idx, ctx)
  local cur = self._cur or 0
  if cur >= idx then return end                  -- already past
  if idx ~= cur + 1 then                         -- skipping forbidden
    sacred.log(("[questfsm/%s] skip blocked: at %d, target %d (step %q)")
                :format(self.id, cur, idx, self.steps[idx].name))
    return
  end
  self._cur = idx
  local step = self.steps[idx]
  sacred.log(("[questfsm/%s] -> step %d/%d (%s)")
              :format(self.id, idx, #self.steps, step.name))
  if step.on_enter then
    local ok, err = pcall(step.on_enter, self, ctx)
    if not ok then
      sacred.log(("[questfsm/%s] on_enter ERROR at step %s: %s")
                  :format(self.id, step.name, tostring(err)))
    end
  end
end

-- ---- Public registry ---------------------------------------------------

function M.define(spec)
  assert(type(spec) == "table",     "questfsm.define: spec must be a table")
  assert(type(spec.id) == "string", "questfsm.define: spec.id (string) required")
  assert(type(spec.steps) == "table" and #spec.steps > 0,
         "questfsm.define: spec.steps (non-empty array) required")
  if _fsms[spec.id] then
    error(("questfsm.define: duplicate id %q"):format(spec.id), 2)
  end
  assert(sacred and sacred.on_trigger,
         "questfsm.define: sacred.on_trigger unavailable " ..
         "(DLL too old or runtime_triggers init failed)")

  local q = setmetatable({
    id        = spec.id,
    steps     = spec.steps,
    _cur      = 0,
    _step_idx = {},
  }, Quest)

  for i, st in ipairs(spec.steps) do
    assert(type(st.name) == "string",
           ("questfsm.define[%s]: step %d missing .name"):format(spec.id, i))
    assert(type(st.trigger) == "string",
           ("questfsm.define[%s]: step %s missing .trigger (string)")
             :format(spec.id, st.name))
    if q._step_idx[st.name] then
      error(("questfsm.define[%s]: duplicate step name %q"):format(spec.id, st.name), 2)
    end
    q._step_idx[st.name] = i

    -- Capture the index in a local so the closure doesn't see a mutating loop var.
    local target = i
    local step   = st
    sacred.on_trigger(st.trigger, function(ctx)
      -- Only react if we're in the immediately-prior state. This is what
      -- makes the chain LINEAR — out-of-order trigger fires are ignored.
      if (q._cur or 0) ~= target - 1 then return end
      if step.guard then
        local ok, allow = pcall(step.guard, q, ctx)
        if not ok then
          sacred.log(("[questfsm/%s] guard ERROR at step %s: %s")
                      :format(q.id, step.name, tostring(allow)))
          return
        end
        if not allow then return end
      end
      q:_advance_to(target, ctx)
    end)
  end

  _fsms[spec.id] = q
  return q
end

-- Lookup by id. Useful for cross-quest gates: one quest can ask whether
-- another has started / completed.
function M.get(id) return _fsms[id] end

-- Dump every registered quest's state to sdk_loaded.log. Bind this to a
-- trigger (or a Lua-side debug command) when investigating.
function M.dump()
  local n = 0
  for id, q in pairs(_fsms) do
    n = n + 1
    local cur  = q._cur or 0
    local name = cur > 0 and q.steps[cur].name or "(not started)"
    sacred.log(("[questfsm] %-20s  step %d/%d  %s")
                :format(id, cur, #q.steps, name))
  end
  if n == 0 then sacred.log("[questfsm] no quests defined") end
end

-- Iterator over (id, quest) for tooling that wants to walk all of them.
function M.all()
  local k
  return function()
    local v
    k, v = next(_fsms, k)
    return k, v
  end
end

return M
