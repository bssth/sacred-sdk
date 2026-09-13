-- SacredSDK / lua / lib / world.lua
--
-- World objects the player can interact with: chests, clickable hotspots and
-- map icons, built the way the shipped scripts build them (verbs.lua, batch 2).
--
-- A vanilla chest is one CreateObj record with a NAME (op 0x29) and a TAKE
-- SECTION (op 0x41). Opening it runs that section, which holds a FillChest
-- record: a loot BUDGET the engine spends on gold or random items, re-rolled
-- per hero. base:VAMPIRELADY ships 1,805 of them (`fillchest_<name>`).
--
--   local W = require "world"
--   local c = W.chest{ x = 2810, y = 2297, gold = 1000, locked = true,
--                      on_open = function(c) Log("opened " .. c.name) end }
--   c:unlock()
--
-- Because the take section is an SDK section, opening the chest also reaches
-- Lua as `SECTION:<take>` on the next heartbeat -- that is the `on_open`
-- callback. The same trick gives a hotspot its `on_click`.
--
-- FILLING (live, 2026-09-12): the engine runs a Take: hook with the HERO as the
-- section's context (`cCreature_pickupItem_00558080` @0x558cdf passes `this`),
-- and FillChest fills its CONTEXT object -- it casts it to cObject3D, asks
-- FUN_00428440 whether that type is a container and returns silently otherwise.
-- So a chest cannot fill itself from its own hook: the SDK looks the object up
-- by name (`sacred.object_by_name`, the engine's own name -> id -> handle) and
-- runs the FillChest record with the chest as the context. That also means the
-- chest can be (re)filled at any moment, which is what `Chest:fill()` does.
--
-- LOCKING is not a container thing either: SetObjState's lock/unlock sets bit
-- 0x8000 on a cTrigger (a door), so it does nothing to a barrel. Gate a chest by
-- filling it when the quest step is reached, or by creating it then.

local S  = require "sections"
local A  = require "actions"
local Vb = require "verbs"

local W = {}

-- Container types seen in StartCode's CreateObj census (the count is how many
-- the base campaign places). Types 5214..5226 may hold a trap or a mimic, so
-- the safe plain ones are listed here.
W.CHEST = { plain = 5207, small = 5201, wide = 5211, tall = 5210, book = 5222 }

local seq = 0
local function next_name(prefix)
  seq = seq + 1
  return ("%s%02d"):format(prefix, seq)
end

local Chest = {}
Chest.__index = Chest

function Chest:lock()   return A.run(Vb.obj_state(self.name, "lock")) end
function Chest:unlock() return A.run(Vb.obj_state(self.name, "unlock")) end
function Chest:open()   return A.run(Vb.obj_state(self.name, "open")) end
function Chest:close()  return A.run(Vb.obj_state(self.name, "close")) end

-- The object's handle. A name the SDK invented never binds (the engine only
-- registers a CreateObj op-0x29 name that is already in its own name table), so
-- the lookup falls back to "the nearest object of this type at this spot".
function Chest:find()
  if self.handle then return self.handle end
  local h = sacred.object_by_name and sacred.object_by_name(self.name)
  if not h and sacred.object_at and self.x then
    h = sacred.object_at(self.type, self.x, self.y, 3)
  end
  if h then self.handle = h end
  return h
end

-- Fill the chest NOW (the engine spends the budget and rolls the items). Needs
-- the object to exist, so a chest placed this tick fills on one of the next few.
function Chest:fill(opts)
  if opts then self.loot = opts end
  local h = self:find()
  if not h then
    W._pending[#W._pending + 1] = { chest = self, tries = 20 }
    return false
  end
  S.define(self.fill_section, Vb.fill_chest(self.loot))
  A.run_as(h, Vb.fill_chest(self.loot))
  sacred.log(("[world] chest '%s' (handle %d) filled: %s"):format(
    self.name, h, self.loot.value and ("value " .. self.loot.value) or "defaults"))
  return true
end

-- Change what the chest holds and fill it again.
function Chest:refill(opts)
  self.loot = opts or {}
  return self:fill()
end

-- Let the chest pay its gold again the next time it is opened.
function Chest:arm()
  if sacred.var_set then sacred.var_set(self.var, 0) end
  return self
end

-- Create a chest in the world.
--   x, y      where (world tiles); or pos = "<named position>"
--   type      W.CHEST.* (default plain)
--   name      the object's own name (other records address it by this)
--   gold      GOLD paid once, when the chest is first opened (AddGold, with the
--             engine's "+N" popup). The paid/not-paid flag is an engine variable,
--             so a savegame remembers it; `Chest:arm()` lets it pay again.
--   spill     object types dropped ON THE FLOOR around the chest when it is
--             opened -- what a vanilla chest does. Gold piles are 5132 (a coin),
--             5133, 5134 and 5135, the four the engine's own fill picks from.
--             LIVE 2026-09-12: a dropped pile is worth what the ENGINE decides
--             (600 gold each at the tester's level, the same for 5134 and 5135,
--             so the type is only the look) -- it scales with the hero, like a
--             vanilla chest. Use `gold` when the amount has to be exact.
--             Any object type works here: a weapon, a potion, a quest item.
--   fill_*     the FillChest budget (`fill_value`, `fill_item`, `fill_junk`).
--             LIVE 2026-09-12: the engine's fill does nothing for a chest the SDK
--             created, even with the chest as the section's context and its own
--             handle -- FUN_00428440 does not accept it. Kept for shipped chests.
--   locked    lock it right after creating it
--   records   extra records to run when it is opened (a banner, a journal line)
--   on_open   Lua callback, on the heartbeat after it is opened
--   create    false = only (re)define the take section and the callback, do not
--             place an object. A chest placed in an earlier session is already
--             in the world and in the savegame, but its take SECTION lives in
--             the DLL and has to be defined again every session.
function W.chest(spec)
  spec = spec or {}
  local self = setmetatable({}, Chest)
  self.name = spec.name or next_name("sdk_chest_")
  self.take = "sdk_take_" .. self.name
  self.fill_section = "sdk_fill_" .. self.name
  self.loot = { value = spec.fill_value, item = spec.fill_item, junk = spec.fill_junk }
  self.gold = spec.gold
  self.var  = "sdkchest_" .. self.name:gsub("^sdk_", "")   -- engine variable: paid yet?
  self.type = spec.type or W.CHEST.plain
  self.x, self.y = spec.x, spec.y
  self.extra = spec.records or {}
  if type(self.extra) == "string" then self.extra = { self.extra } end
  -- The name has to exist in the engine's own name table before the object can
  -- bind to it (sacred.name_register; 89 dynamic slots). Without it the object
  -- is still created, but nothing can address it by name afterwards.
  if sacred.name_register then
    self.name_id = sacred.name_register(self.name)
    if not self.name_id then
      sacred.log(("[world] no name id for '%s' (the dynamic pool is full)"):format(self.name))
    end
  end
  -- The Take: hook is what the engine runs when the chest is opened. The gold
  -- sits in an IF on an engine variable, the vanilla way to pay a reward once:
  -- the whole thing runs inside the engine, with no Lua in between, and the
  -- variable is in the savegame.
  local hook = {}
  local once = {}                       -- what the first open does
  if self.gold then once[#once + 1] = Vb.add_gold(self.gold) end
  -- The spill lands in a ring around the chest, one object per tile, so the
  -- piles do not stack on the same spot.
  local RING = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } }
  for i, t in ipairs(spec.spill or {}) do
    local d = RING[(i - 1) % #RING + 1]
    once[#once + 1] = Vb.create_obj(t, { (self.x or 0) + d[1], (self.y or 0) + d[2], 0 })
  end
  if #once > 0 then
    if sacred.var_get and sacred.var_get(self.var) == nil then sacred.var_set(self.var, 0) end
    once[#once + 1] = Vb.set_var(self.var, 1)
    hook[#hook + 1] = Vb.if_({ Vb.P.var_eq(self.var, 0) }, once)
  end
  hook[#hook + 1] = table.concat(self.extra)
  S.define(self.take, table.concat(hook))
  S.define(self.fill_section, Vb.fill_chest(self.loot))
  if spec.on_open then
    sacred.on_trigger("SECTION:" .. self.take, function() spec.on_open(self) end)
  end
  if spec.create ~= false then
    local pos = spec.pos or { spec.x, spec.y, spec.z or 0 }
    A.run(Vb.create_obj(self.type, pos,
                        { name = self.name, take = self.take, dir = spec.dir }))
    if spec.fill_value or spec.fill_item or spec.fill_junk then self:fill() end
  end
  return self
end

-- Chests waiting for their object to appear (a creation record runs on the next
-- heartbeat, and the name is only findable after that).
W._pending = {}
sacred.on_tick(function()
  for i = #W._pending, 1, -1 do
    local job = W._pending[i]
    job.tries = job.tries - 1
    if job.chest:find() then
      table.remove(W._pending, i)
      job.chest:fill()
    elseif job.tries <= 0 then
      table.remove(W._pending, i)
      sacred.log(("[world] chest '%s': no object found at %s,%s, not filled")
        :format(job.chest.name, tostring(job.chest.x), tostring(job.chest.y)))
    end
  end
end)

-- A clickable hotspot: the object of `type` (or the "res:<KEY>" object `res`)
-- at that spot runs a section when the player clicks it.
--   W.hotspot{ x = .., y = .., type = 0x4526, on_click = function() ... end }
function W.hotspot(spec)
  spec = spec or {}
  if spec.res and sacred.name_register then sacred.name_register(spec.res) end
  local section = spec.section or next_name("sdk_spot_")
  local records = spec.records or {}
  if type(records) == "string" then records = { records } end
  S.define(section, table.concat(records))
  if spec.on_click then
    sacred.on_trigger("SECTION:" .. section, function() spec.on_click(section) end)
  end
  A.run(Vb.mouse_event{ type = spec.type, res = spec.res, mode = spec.mode,
                        pos = spec.pos or { spec.x, spec.y, spec.z or 0 },
                        section = section })
  return section
end

-- An icon on the world map.
function W.map_icon(x, y, icon) return A.run(Vb.map_icon(x, y, icon)) end

-- Named-object state. This is the TRIGGER path (doors, gates): lock/unlock set
-- and clear bit 0x8000 on a cTrigger, so they do nothing to a container.
function W.lock(obj)   return A.run(Vb.obj_state(obj, "lock")) end
function W.unlock(obj) return A.run(Vb.obj_state(obj, "unlock")) end
function W.open(obj)   return A.run(Vb.obj_state(obj, "open")) end
function W.close(obj)  return A.run(Vb.obj_state(obj, "close")) end

return W
