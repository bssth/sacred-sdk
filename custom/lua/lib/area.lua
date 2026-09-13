-- SacredSDK / lua / lib / area.lua
--
-- Named world positions and "the hero entered this area" triggers
-- (sdk/.claude/knowledge/quests/SDK_GAPS.md, gaps 3 and 5).
--
--   local A = require "area"
--   local kx, ky, r, z = A.pos("pos_ziel1501")    -- nil if the name is unknown
--   local a = A.on_enter{ pos = "pos_ziel1501", radius = 5, fn = function(a) ... end }
--   A.on_enter{ x = 2810, y = 2297, radius = 3, once = true, fn = ... }
--   A.on_enter{ ll = "pos_x_ll", ur = "pos_x_ur", fn = ... }   -- a rectangle
--   a:cancel()
--
-- Positions come from the engine's named-position table: the DefPos records of
-- StartCode and FunkCode (pos_*, LOC_RG*, ...; 1,921 names for the Vampiress),
-- in KompassPos like sacred.hero_pos() and Npc:teleport. The table is filled
-- when a world loads, so ask once the world is up (vars.lua V.on_ready).
-- sdk/re/py/startcode.py lists every name and its coordinates offline.
--
-- An area fires on the tick the hero steps in, and also when it is armed with
-- the hero already inside. Standing inside does not fire again; leaving and
-- coming back does, unless once = true. The check runs on the 250 ms heartbeat
-- and ignores the level (z). Areas are Lua state: arm them again from the
-- quest's saved step after a savegame load.

local A = {}

local TILE = 53.66563034057617   -- world units per KompassPos tile (CreateNPC 0x482e86)

-- kx, ky, radius, z of a named position; nil if unknown, or a LOC_RG* slot not
-- assigned yet (X == 0). A pair of negative values is in world units.
function A.pos(name)
  local v = sacred.state_get(name)
  if not v then return nil end
  local x, y = v[1], v[2]
  if x < 0 and y < 0 then x, y = math.floor(-x / TILE), math.floor(-y / TILE) end
  if x == 0 then return nil end
  return x, y, v[3], v[4]
end

-- A test for the hero's (x, y), or nil while a name does not resolve.
local function shape(spec)
  if spec.ll or spec.ur then
    local function pt(p)
      if type(p) == "string" then return A.pos(p) end
      if type(p) == "table" then return p[1], p[2] end
      return nil
    end
    local x1, y1 = pt(spec.ll)
    local x2, y2 = pt(spec.ur)
    if not (x1 and x2) then return nil end
    if x1 > x2 then x1, x2 = x2, x1 end
    if y1 > y2 then y1, y2 = y2, y1 end
    return function(x, y) return x >= x1 and x <= x2 and y >= y1 and y <= y2 end
  end
  local cx, cy = spec.x, spec.y
  if spec.pos then cx, cy = A.pos(spec.pos) end
  if not cx then return nil end
  local r2 = (spec.radius or 3) ^ 2
  return function(x, y) return (x - cx) ^ 2 + (y - cy) ^ 2 <= r2 end
end

local areas = {}

-- Arm an area. spec: pos (a name) or x, y, with radius in tiles (default 3); or
-- ll and ur (names or {x, y}) for a rectangle; fn(area) on entering; once.
-- Returns the area; area:cancel() disarms it.
function A.on_enter(spec)
  assert(type(spec) == "table" and type(spec.fn) == "function", "area.on_enter: spec.fn is required")
  local a = { spec = spec, inside = false, dead = false }
  function a:cancel() self.dead = true end
  areas[#areas + 1] = a
  return a
end

local serial = sacred.world_serial or function() return 0 end
local alive  = sacred.world_alive or function() return true end

sacred.on_tick(function()
  if #areas == 0 or not alive() then return end
  local hx, hy = sacred.hero_pos()
  if not hx then return end
  local ws = serial()
  local list = areas
  areas = {}                               -- a handler may arm new areas meanwhile
  for _, a in ipairs(list) do
    if not a.dead then
      if a.world ~= ws then a.world, a.test, a.inside = ws, nil, false end
      a.test = a.test or shape(a.spec)     -- a name can resolve late
      if a.test then
        local now = a.test(hx, hy)
        if now and not a.inside then
          if a.spec.once then a.dead = true end
          local ok, err = pcall(a.spec.fn, a)
          if not ok then sacred.log("[area] handler failed: " .. tostring(err)) end
        end
        a.inside = now
      end
    end
    if not a.dead then areas[#areas + 1] = a end
  end
end)

return A
