-- SacredSDK / lua / lib / actions.lua
--
-- Run vanilla-shaped records NOW, through the engine's own record handlers: an
-- SDK section run by sacred.section_run. The records come from the builders in
-- sections.lua, each copied from a vanilla quest; this module only runs them.
--
--   local A = require "actions"
--   local S = require "sections"
--   A.run(S.info("MY_KEY"))            -- one or more records, run on the next heartbeat
--
-- The engine runs them as WorkFunktion runs any section: with no owner creature,
-- on the game thread, while a world is loaded. The DLL takes up to 16 runs per
-- 250 ms heartbeat; more wait here and go out on the following ticks. Records
-- that something must find by NAME later (a timer's target, a counter's
-- completion) belong in a named section instead: S.define(name, ...).

local S = require "sections"
local A = {}

-- Section names rotate through RING slots. The DLL's queue holds 16 runs and
-- drains every heartbeat, so a slot is never redefined while its run waits.
local RING = 32
local slot = 0
local pending = {}

local function send(bytes, handle)
  slot = slot % RING + 1
  local name = ("sdk_act_%02d"):format(slot)
  S.define(name, bytes)
  if sacred.section_run(name, handle) then return true end
  slot = slot - 1                      -- the queue was full: this slot is free again
  return false
end

-- Run records (strings from the sections.lua builders) on the next heartbeat.
-- Returns true when they are in the DLL's queue, false when they wait here for
-- a later tick.
function A.run(...)
  assert(sacred.section_run, "actions: this SDK build has no sacred.section_run")
  local bytes = table.concat({ ... })
  if #pending == 0 and send(bytes) then return true end
  pending[#pending + 1] = bytes
  return false
end

-- The same, but with a CONTEXT object: the engine runs the records as if that
-- object (a creature or a world object, by handle) were the section's own. A
-- record that acts on "this object" needs it -- FillChest fills the context and
-- silently does nothing when it is not a container, which is why a chest is
-- filled this way and not from its Take: hook (the engine runs that one with the
-- HERO as context).
function A.run_as(handle, ...)
  assert(sacred.section_run, "actions: this SDK build has no sacred.section_run")
  local bytes = table.concat({ ... })
  if #pending == 0 and send(bytes, handle) then return true end
  pending[#pending + 1] = { bytes = bytes, handle = handle }
  return false
end

sacred.on_tick(function()
  while #pending > 0 do
    local job = pending[1]
    local ok
    if type(job) == "table" then ok = send(job.bytes, job.handle) else ok = send(job) end
    if not ok then return end
    table.remove(pending, 1)
  end
end)

return A
