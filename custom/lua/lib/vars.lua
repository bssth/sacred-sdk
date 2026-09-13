-- SacredSDK / lua / lib / vars.lua
--
-- The engine's script variables: the table vanilla quests test with IsVar*,
-- splice into keys with +VAR(), and that every savegame carries. SDK quest
-- state that has to survive a reload belongs here
-- (sdk/.claude/knowledge/quests/SDK_GAPS.md, gaps 13 and 1).
--
--   local V = require "vars"
--   V.set("SDKQ_99", 2)          -- creates the variable when it is missing
--   V.get("SDKQ_99", 0)          -- the value, or the default when missing
--   V.inc("kills")               -- vanilla IncVar: +1, never goes down
--   V.set_bit("1101", 4)         -- vanilla SetVarBit; V.bit("1101", 4) -> bool
--   V.on_ready(function(loaded) ... end)
--       -- once per world, when the hero is in it and the variables are final;
--       -- loaded = true after a savegame load. Read saved state here.
--
-- Names are 1..31 characters and case-insensitive (the engine's own lookup).
-- Give SDK variables a prefix so they never meet vanilla ones, which are quest
-- ids ('9511') and plain words ('DQ_Quest'): SDKQ_<quest id> is the convention
-- for a quest's step. Writes need a loaded world and stay on this machine in
-- multiplayer.

local V = {}

local function api(name)
  local f = sacred[name]
  if not f then error("vars: this SDK build has no sacred." .. name, 3) end
  return f
end

-- The engine stores int32; Lua integers are 64-bit.
local function s32(x)
  x = x & 0xFFFFFFFF
  if x >= 0x80000000 then x = x - 0x100000000 end
  return x
end

function V.get(name, default)
  local v = api("var_get")(name)
  if v == nil then return default end
  return v
end

function V.set(name, value) return api("var_set")(name, value) end
function V.inc(name, n)     return api("var_inc")(name, n or 1) end
function V.dec(name, n)     return api("var_dec")(name, n or 1) end
function V.all(prefix)      return api("vars")(prefix) end
function V.dump(prefix)     return api("var_dump")(prefix) end

-- Bits, numbered 0..31 like the engine's (bit & 31). A missing variable has no
-- bits set; setting one creates it (SetVarBit does the same).
-- The name "HeroQBit" is special, as in vanilla: its bits 0..159 belong to the
-- hero, one set per difficulty, and travel with the hero's save
-- (sacred.hero_qbit, SDK_GAPS gap 12). Vanilla uses 0-11 and 101-106; SDK bits
-- belong in 150-159.
local function hero(name) return name:lower() == "heroqbit" end

function V.bit(name, b)
  if hero(name) then return api("hero_qbit")(b) == true end
  local v = V.get(name)
  return v ~= nil and (v & (1 << (b & 31))) ~= 0
end

function V.set_bit(name, b)
  if hero(name) then return api("hero_qbit_set")(b, true) end
  return V.set(name, s32(V.get(name, 0) | (1 << (b & 31))))
end

function V.clear_bit(name, b)
  if hero(name) then return api("hero_qbit_set")(b, false) end
  local v = V.get(name)
  if v == nil then return false end
  return V.set(name, s32(v & ~(1 << (b & 31))))
end

-- ---- on_ready ---------------------------------------------------------------
-- A world is "ready" once its scripts are loaded (sacred.world_alive()) and the
-- hero has stood in it for SETTLE ticks since the engine built it. The DLL
-- counts world builds (sacred.world_serial(): each new game and each savegame
-- load). Together they keep a script from acting on a world that is gone: after
-- "quit to menu" the next new game's hero exists well before its world is
-- built, and anything spawned in that gap lives on as a duplicate.
-- loaded = true when the build was a savegame load (trigger SDK:SAVE_LOADED).
local SETTLE = 8                           -- ticks, about 2 s
local ready_fns = {}
local serial = sacred.world_serial or function() return 0 end
local alive  = sacred.world_alive or function() return true end
local seen, pending, from_save, settle, ready = nil, true, false, 0, false

function V.on_ready(fn) ready_fns[#ready_fns + 1] = fn end
-- True while the world the last on_ready ran for is still the current one.
function V.is_ready() return ready and serial() == seen and alive() end
-- The serial of the world on_ready last ran for.
function V.world() return seen end

local function rearm() pending, settle, ready = true, 0, false end
if not sacred.world_serial then sacred.on_world_load(rearm) end   -- an older DLL
sacred.on_trigger("SDK:SAVE_LOADED", function() from_save = true; rearm() end)
sacred.on_tick(function()
  local ws = serial()
  if ws ~= seen then seen = ws; rearm() end
  if not alive() then rearm(); return end  -- no world (menu, teardown): wait for the next
  if not pending then return end
  if not sacred.hero_pos() then settle = 0; return end
  settle = settle + 1
  if settle < SETTLE then return end
  pending, ready = false, true
  local loaded = from_save
  from_save = false
  for _, fn in ipairs(ready_fns) do
    local ok, err = pcall(fn, loaded)
    if not ok then sacred.log("[vars] on_ready handler failed: " .. tostring(err)) end
  end
end)

return V
