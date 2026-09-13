-- SacredSDK / lua / lib / objectives.lua
--
-- Native quest objectives and on-screen messages, carried out by the engine's
-- own record handlers: an SDK section holding a vanilla-shaped record, run
-- through WorkFunktion (sacred.section_run). SDK_GAPS gaps 4 and 8.
--
--   local O = require "objectives"
--   O.declare("skel5", function() ... end)     -- at script load: the completion
--   O.start_kills("skel5", NPC.SKELETON, 5)    -- the engine counts kills of that
--                                              -- type, shows "x of 5", keeps the
--                                              -- count in the savegame, and calls
--                                              -- the declared function at zero
--   O.left("skel5")                            -- remaining, total | nil
--   O.banner("MY_KEY")                         -- the game's own message + chime
--
-- The engine counts by creature TYPE (every creature of it, anywhere), as it
-- does for vanilla quests. Starting a counter again resets it, so leave a
-- running one alone after a savegame load. Declare at script load, not when
-- the step starts: a savegame's counter can reach zero in a later session, and
-- its completion section and callback must exist then.

local S = require "sections"
local O = {}
local done_fn = {}

local function done_name(id) return "sdk_obj_" .. id .. "_done" end

-- The function to call when the engine counter for `id` reaches zero.
function O.declare(id, fn)
  local name = done_name(id)
  assert(#name <= 63, "objectives: id too long")
  S.define(name)                                  -- empty: the Lua side acts
  if done_fn[id] == nil then                      -- one trigger per id, ever
    sacred.on_trigger("SECTION:" .. name, function()
      local f = done_fn[id]
      if not f then return end
      local ok, err = pcall(f)
      if not ok then sacred.log("[objectives] " .. id .. ": done handler failed: " .. tostring(err)) end
    end)
  end
  done_fn[id] = fn or false
end

-- Define a section from records and have the engine run it on the next tick.
local function run(name, ...)
  assert(#name <= 63, "objectives: section name too long")
  S.define(name, ...)
  return sacred.section_run(name)
end

-- Kills of creatures of `types` (a type id or a list) count down from `count`.
function O.start_kills(id, types, count)
  return run("sdk_obj_" .. id .. "_kills", S.set_on_kill(types, count, done_name(id)))
end

-- Pickups of items of `types` count down from `count`.
function O.start_pickups(id, types, count)
  return run("sdk_obj_" .. id .. "_pickups", S.set_on_collect(types, count, done_name(id)))
end

-- Drop the kill counter for `types` without completing it.
function O.cancel_kills(id, types)
  return run("sdk_obj_" .. id .. "_cancel", S.set_on_kill(types, 0, done_name(id)))
end

-- remaining, total of the engine counter that completes `id`; nil if none runs.
function O.left(id)
  local want = done_name(id):lower()
  for _, list in ipairs({ sacred.kill_counters(), sacred.collect_counters() }) do
    for _, c in ipairs(list) do
      if c.section:lower() == want then return c.left, c.total end
    end
  end
  return nil
end

-- The game's own on-screen message for `key` (a name baked with text.lua, or a
-- global.res number): selector 0 plays chime 200, selector 1 chime 201.
function O.banner(key, selector)
  return run("sdk_msg_" .. tostring(key), S.info(key, selector))
end

return O
