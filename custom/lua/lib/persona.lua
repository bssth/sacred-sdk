-- SacredSDK / lua / lib / persona.lua
--
-- A named NPC that outlives one quest: the same character across the whole
-- storyline, found again after a savegame load, moved where the next chapter
-- needs him.
--
--   local P = require "persona"
--
--   local roch = P.define("roch", {
--     type = NPC.ROCHEFORD, name = "res:ROCHEFORD_NAME", sub_id = 77,
--     home = { 2713, 2196 },          -- where he waits until someone moves him
--     immortal = true,                -- he is not allowed to die
--     setup = function(o, adopted)    -- runs after every spawn AND every adopt
--       o:stance(1, 7)
--       o:bind_quest("Rocheford", true)
--     end,
--   })
--
--   roch:move_to(2626, 2072)          -- he walks there, and stays there
--   roch:companion(true)              -- joins the party
--   local o = roch:npc()              -- the npcobj wrapper, or nil
--
-- WHAT MAKES IT PERSISTENT. A savegame brings back the world, our NPCs included,
-- with the same handles -- so the handle is written into an ENGINE VARIABLE
-- (`vars.lua`, saved with the game) together with the spot he was last told to
-- stand on. On the next world load the persona adopts that handle if it still
-- holds the right creature, and only spawns a new one when it does not. That is
-- the rule the SDK follows everywhere: adopt, never respawn blindly, or the
-- world fills up with copies of the same character.

local V    = require "vars"
local NPCo = require "npcobj"

local P = {}
local personas = {}

local Persona = {}
Persona.__index = Persona

local function log(fmt, ...)
  if sacred and sacred.log then sacred.log("[persona] " .. fmt:format(...)) end
end

-- Define (or redefine) a persona. `id` is short -- it becomes part of an engine
-- variable name, and those are capped at 31 characters.
function P.define(id, spec)
  assert(type(id) == "string" and #id <= 10, "persona: the id must be a short string")
  assert(type(spec) == "table" and spec.type, "persona: spec.type is required")
  local p = personas[id]
  if not p then
    p = setmetatable({ id = id, vh = "SDKNPC_" .. id .. "_H",
                       vx = "SDKNPC_" .. id .. "_X",
                       vy = "SDKNPC_" .. id .. "_Y" }, Persona)
    personas[id] = p
  end
  p.spec = spec
  return p
end

function P.get(id) return personas[id] end

-- Bring every defined persona into the world. Call it once a world is up
-- (vars.lua V.on_ready does that for you if you use P.watch()).
function P.ensure_all()
  for _, p in pairs(personas) do p:ensure() end
end

-- ---- one persona --------------------------------------------------------------

function Persona:npc()
  local o = self.o
  if o and o.alive and o:alive() then return o end
  self.o = nil
  return nil
end

-- The spot he should be standing on: whatever he was last told, else his home.
function Persona:where()
  local x, y = V.get(self.vx), V.get(self.vy)
  if x and y then return x, y end
  local h = self.spec.home
  if h then return h[1], h[2] end
  return nil
end

-- Adopt the creature the savegame brought back, or spawn a new one. Idempotent:
-- calling it on every world load is the intended use.
function Persona:ensure()
  if self:npc() then return self.o end
  local spec = self.spec
  local h = V.get(self.vh)
  local inf = h and sacred.npc_info and sacred.npc_info(h)
  if inf and inf.type == spec.type then
    self.o = NPCo.wrap(h)
    self.o._res = spec.name
    self.adopted = true
    log("%s adopted h=%d at %s,%s", self.id, h, tostring(inf.kx), tostring(inf.ky))
  else
    local o = NPCo.spawn_template(spec.template or "quest_npc",
      { type = spec.type, pos = "CPOS:HERO", name = spec.name, sub_id = spec.sub_id })
    if not o then log("%s spawn FAILED", self.id) return nil end
    local x, y = self:where()
    if x and y then o:teleport(x, y) end
    V.set(self.vh, o:handle())
    self.o, self.adopted = o, false
    log("%s spawned h=%d at %s,%s", self.id, o:handle(), tostring(x), tostring(y))
  end
  if spec.immortal then self:immortal(true) end
  if spec.setup then
    local ok, err = pcall(spec.setup, self.o, self.adopted)
    if not ok then log("%s setup error: %s", self.id, tostring(err)) end
  end
  return self.o
end

-- Send him somewhere and remember it. `walk = false` places him instead.
function Persona:move_to(x, y, walk)
  V.set(self.vx, x)
  V.set(self.vy, y)
  local o = self:npc()
  if not o then return false end
  if walk == false then
    o:teleport(x, y)
    log("%s placed at %d,%d", self.id, x, y)
    return true
  end
  local Vb  = require "verbs"
  local A   = require "actions"
  o:set_stationary(false)
  A.run(Vb.npc_goto(self.spec.name, { x, y }))
  self.walk = { x = x, y = y, tries = 3, last = nil, t = 0 }
  log("%s walks to %d,%d", self.id, x, y)
  return true
end

-- Party member or not, through the engine's own paths.
function Persona:companion(on, quest_id)
  local o = self:npc()
  if not o then return false end
  if on == false then return o:dismiss() end
  return o:make_companion(quest_id or 0, { combat = true })
end

-- Not allowed to die. `npcobj.Npc:immortal` sets the engine's own death-skip on
-- the creature's TYPE (class 5) -- which is how the shipped story companions
-- survive -- and the keeper registered here tops his health up on the heartbeat
-- so he is not left standing at zero. Type-wide: give a persona a model no
-- ordinary enemy shares.
function Persona:immortal(on)
  local o = self:npc()
  self.keep = (on ~= false) and (self.spec.hp or 100000) or nil
  if o then o:immortal(on ~= false, self.spec.hp) end
  return true
end

function Persona:icon(kind) local o = self:npc() return o and o:icon(kind) end
function Persona:node(name) local o = self:npc() return o and o:node(name) end
function Persona:say(...)   local o = self:npc() return o and o:say(...) end

-- Down, but not out. The engine restores a fallen party member's health when he
-- carries the "gets back up" bit, yet it still marks him as fallen -- live, his
-- health read full while his state read 9. So a persona that must not be lost is
-- put back on his feet here: first the engine's own restore (`npc_revive`), and
-- if he is still down a moment later, the character is placed in the world again
-- at the spot he fell. The handle in the engine variable is updated, so he stays
-- the same character to every other script.
function Persona:pick_up()
  local o = self.o
  if not o then return false end
  local h = o:handle()
  if sacred.npc_revive then sacred.npc_revive(h) end
  self.down = (self.down or 0) + 1
  if self.down < 8 then return true end          -- give the engine ~2 s
  local x, y = o:pos()
  if x then V.set(self.vx, x); V.set(self.vy, y) end
  if sacred.npc_despawn then pcall(sacred.npc_despawn, h) end
  self.o, self.down = nil, 0
  log("%s could not be woken; placing him in the world again", self.id)
  self:ensure()
  if self.o and self.spec.companion then self:companion(true) end
  return true
end

-- ---- the heartbeat ------------------------------------------------------------
-- Remembers where he stands, keeps him alive if he is immortal, nudges him when
-- the engine's path-finding gives up, and picks him up when he falls.
local watching = false
function P.watch()
  if watching then return end
  watching = true
  V.on_ready(function() P.ensure_all() end)
  local t = 0
  sacred.on_tick(function()
    if not V.is_ready() then return end
    t = t + 1
    for _, p in pairs(personas) do
      local o = p:npc()
      if o and p.spec.immortal then
        local a = sacred.npc_ai and sacred.npc_ai(o:handle())
        if (a and a.fc == 9) or not o:alive() then
          p:pick_up()
          o = p:npc()
        elseif p.down then
          p.down = nil
        end
      end
      if o then
        if p.keep then o:set_hp(p.keep) end
        if p.walk then
          p.walk.t = p.walk.t + 1
          if p.walk.t >= 16 then                    -- every ~4 s
            p.walk.t = 0
            local x, y = o:pos()
            local dx, dy = (x or 0) - p.walk.x, (y or 0) - p.walk.y
            local here = ("%s,%s"):format(tostring(x), tostring(y))
            if dx * dx + dy * dy <= 9 then
              p.walk = nil
              o:set_stationary(true)
              log("%s arrived", p.id)
            elseif here == p.walk.last then
              p.walk.tries = p.walk.tries - 1
              if p.walk.tries > 0 then
                local Vb, A = require "verbs", require "actions"
                A.run(Vb.npc_goto(p.spec.name, { p.walk.x, p.walk.y }))
                log("%s is stuck at %s, walking him again", p.id, here)
              else
                o:teleport(p.walk.x, p.walk.y)
                p.walk = nil
                log("%s could not path there; placed him", p.id)
              end
            else
              p.walk.last = here
            end
          end
        end
        if t % 20 == 0 then                          -- every ~5 s: remember the spot
          local x, y = o:pos()
          if x and not p.walk then V.set(p.vx, x); V.set(p.vy, y) end
        end
      end
    end
  end)
end

return P
