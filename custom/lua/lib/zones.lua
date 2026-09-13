-- SacredSDK / lua / lib / zones.lua
--
-- Two ways the engine lets a quest notice and shape the ground the hero walks
-- on, wrapped so they behave the way an author expects:
--
--   ZONES    a rectangle that runs your code when someone walks into it
--   BARRIERS an invisible blocker over map cells, that can be opened and closed
--
--   local Z = require "zones"
--
--   Z.define("harbour_gate", {
--     rect     = { 2799, 2278, 2811, 2290 },     -- x1, y1, x2, y2 (hero_pos space)
--     records  = { Vb.info("HARBOUR_WARNING") }, -- the engine runs these at the step
--     on_enter = function(z, who) Log(who .. " walked in") end,
--     on_leave = function(z)      Log("and out again") end,
--   }):arm()
--
--   Z.define("ambush", { rect = {...}, once = true, on_enter = spring }):arm()
--
--   local gate = Z.barrier("north_gate", Z.line(2792, 2272, 2800, 2272, 2))
--   gate:close()      -- the road is shut
--   gate:open()       -- and open again
--
-- HOW A ZONE WORKS, and why the wrapper is needed. SetBaseTrigger (0x04)
-- registers the rectangle in the sector's trigger list. When a creature stands
-- on one of its cells the engine looks the trigger name up in its binding tree,
-- finds nothing (that tree is filled at script-load time only) and falls back to
-- running the section "OMO-1<name>" with that creature -- so an SDK section of
-- that name IS the handler, and no engine patch is needed (FUN_0055d260 ->
-- 0x55ddf3, FUN_005941a0 -> 0x59817e).
--
-- Live, 2026-09-12: the engine runs that section EVERY HEARTBEAT somebody is
-- inside, ONCE PER CREATURE -- 50 runs from one walk through, three of them in a
-- single millisecond (the hero and two party NPCs). A message record in the
-- handler therefore repeats on every step. So by default a zone here fires once
-- per ENTRY: the handler section ends with the trigger deleting itself -- which
-- is what the shipped quests do, OMO13trg_haupteingang deletes both entrances --
-- and this module puts the rectangle back once the hero is out again.
-- `raw = true` keeps the engine's pulse if that is what you want.
--
-- Notes.
--  * The name is at most 58 characters ("OMO-1" plus a 0x40-byte buffer) and
--    must not collide with a shipped trigger name.
--  * A sector is 64x64 cells. A rectangle crossing a sector edge makes the
--    registrar split itself recursively; it works, but one sector is cleaner.
--  * "Left" is judged from the HERO's position. A zone fired by some other
--    creature while the hero is elsewhere re-arms after `repeat_delay`.
--  * Neither the rectangles nor the barriers were seen in the savegame: build
--    them on every world load.

local S  = require "sections"
local Vb = require "verbs"
local A  = require "actions"

local Z = {}
local zones = {}
local watching = false

local function log(fmt, ...)
  if sacred and sacred.log then sacred.log("[zones] " .. fmt:format(...)) end
end

-- ---- zones -----------------------------------------------------------------------

local Zone = {}
Zone.__index = Zone

function Z.define(name, spec)
  assert(type(name) == "string" and #name >= 1 and #name <= 58,
    "zones: the name must be 1..58 characters")
  assert(type(spec) == "table", "zones: define needs a spec table")
  local r = spec.rect or spec.area
  assert(type(r) == "table", "zones: spec.rect = { x1, y1, x2, y2 }")
  local x1, y1, x2, y2
  if type(r[1]) == "table" then x1, y1, x2, y2 = r[1][1], r[1][2], r[2][1], r[2][2]
  else x1, y1, x2, y2 = r[1], r[2], r[3], r[4] end
  if x1 > x2 then x1, x2 = x2, x1 end
  if y1 > y2 then y1, y2 = y2, y1 end

  local z = zones[name]
  if not z then
    z = setmetatable({ name = name, section = "OMO-1" .. name,
                       fires = 0, inside = false, quiet = 0, wait = 0 }, Zone)
    zones[name] = z
  end
  z.x1, z.y1, z.x2, z.y2 = x1, y1, x2, y2
  z.spec  = spec
  z.raw   = spec.raw == true
  z.grace = spec.grace or 3                  -- raw mode: beats of silence that mean "left"
  z.delay = spec.repeat_delay or 8           -- beats before the rectangle goes back

  -- The handler section: the author's own vanilla records, and -- unless raw --
  -- the self-delete that makes one entry one event.
  local recs = {}
  for _, rec in ipairs(spec.records or {}) do recs[#recs + 1] = rec end
  if not z.raw then recs[#recs + 1] = Vb.del_trigger(name) end
  S.define(z.section, table.concat(recs))

  if not z.wired then
    z.wired = true
    sacred.on_trigger("SECTION:" .. z.section, function()
      z.fires = z.fires + 1
      z.quiet = 0
    end)
  end
  if not watching then Z.watch() end
  return z
end

function Z.get(name) return zones[name] end
function Z.all() return zones end

function Zone:contains(x, y)
  return x and y and x >= self.x1 and x <= self.x2 and y >= self.y1 and y <= self.y2
end

function Zone:hero_inside()
  local x, y = sacred.hero_pos()
  return self:contains(x, y)
end

-- Register the rectangle with the engine.
function Zone:arm()
  A.run(Vb.area_trigger(self.name, { self.x1, self.y1 }, { self.x2, self.y2 }))
  self.armed, self.fires, self.inside, self.quiet, self.wait = true, 0, false, 0, 0
  log("%s armed (%d,%d)..(%d,%d)%s", self.name, self.x1, self.y1, self.x2, self.y2,
    self.spec.once and " one-shot" or (self.raw and " raw" or ""))
  return self
end

-- Take it down early (the one-shot and the per-entry delete do it themselves).
function Zone:disarm()
  if self.armed then A.run(Vb.del_trigger(self.name)) end
  self.armed, self.inside = false, false
  log("%s disarmed", self.name)
  return self
end

local function fire(z, key, ...)
  local fn = z.spec[key]
  if not fn then return end
  local ok, err = pcall(fn, z, ...)
  if not ok then log("%s %s error: %s", z.name, key, tostring(err)) end
end

-- ---- barriers --------------------------------------------------------------------
-- CreateTrigger (0x30) makes a named trigger object; TriggerPatch (0x32) binds it
-- onto map cells; SetTriggerState (0x31) opens, closes, locks and unlocks it.
-- Closed and locked it blocks the way -- live-confirmed 2026-09-12, laid across
-- the road to the captain. It draws nothing, so mark it (a map icon, a fence)
-- if the player is meant to understand what stopped them.

local Barrier = {}
Barrier.__index = Barrier

-- A straight line of cells from (x1,y1) to (x2,y2), `thick` rows deep.
function Z.line(x1, y1, x2, y2, thick)
  local cells = {}
  local dx = (x2 > x1) and 1 or ((x2 < x1) and -1 or 0)
  local dy = (y2 > y1) and 1 or ((y2 < y1) and -1 or 0)
  local steps = math.max(math.abs(x2 - x1), math.abs(y2 - y1))
  for t = 0, (thick or 1) - 1 do
    for i = 0, steps do
      cells[#cells + 1] = { x1 + dx * i + ((dx == 0) and t or 0),
                            y1 + dy * i + ((dy == 0) and t or 0) }
    end
  end
  return cells
end

-- Create (or redefine) a barrier over `cells`. It starts closed and locked
-- unless opts.open is true.
function Z.barrier(name, cells, opts)
  opts = opts or {}
  assert(type(name) == "string" and #name >= 1 and #name <= 58, "zones: bad barrier name")
  assert(type(cells) == "table" and #cells > 0, "zones: a barrier needs cells")
  local b = setmetatable({ name = name, cells = cells }, Barrier)
  local recs = { Vb.create_trigger(name) }
  for i = 1, #cells, 16 do                  -- 16 cells a record: well under the 255-byte limit
    local chunk = {}
    for j = i, math.min(i + 15, #cells) do chunk[#chunk + 1] = cells[j] end
    recs[#recs + 1] = Vb.trigger_patch(name, chunk)
  end
  A.run(table.unpack(recs))
  if opts.open then b:open() else b:close() end
  log("barrier %s over %d cells", name, #cells)
  return b
end

function Barrier:close()
  A.run(Vb.trigger_state(self.name, "close", "lock"))
  self.shut = true
  return self
end

function Barrier:open()
  A.run(Vb.trigger_state(self.name, "unlock", "open"))
  self.shut = false
  return self
end

function Barrier:lock()   A.run(Vb.trigger_state(self.name, "lock"))   return self end
function Barrier:unlock() A.run(Vb.trigger_state(self.name, "unlock")) return self end

-- ---- the heartbeat ---------------------------------------------------------------

function Z.watch()
  if watching then return end
  watching = true
  sacred.on_tick(function()
    for _, z in pairs(zones) do
      if z.raw then
        -- the engine's own pulse, passed through
        if z.fires > 0 then
          local hero = z:hero_inside()
          z.fires = 0
          if not z.inside then
            z.inside = true
            fire(z, "on_enter", hero and "hero" or "someone")
          else
            fire(z, "on_inside", hero and "hero" or "someone")
          end
        elseif z.inside then
          z.quiet = z.quiet + 1
          if z.quiet >= z.grace then
            z.inside, z.quiet = false, 0
            fire(z, "on_leave")
          end
        end
      else
        -- one entry, one event: the handler deleted the rectangle, so put it
        -- back once the hero is out again (unless the zone was a one-shot).
        if z.fires > 0 then
          z.fires = 0
          if not z.inside then
            z.inside, z.armed, z.wait = true, false, 0
            fire(z, "on_enter", z:hero_inside() and "hero" or "someone")
          end
        elseif z.inside then
          if not z:hero_inside() then
            z.wait = z.wait + 1
            if z.wait >= z.delay then
              z.inside, z.wait = false, 0
              fire(z, "on_leave")
              if not z.spec.once then z:arm() end
            end
          else
            z.wait = 0
          end
        end
      end
    end
  end)
end

return Z
