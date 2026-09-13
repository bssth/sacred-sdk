-- examples/14_npc_runtime.lua — RUNTIME NPC spawning & behavior.
--
-- Why runtime (not bake): editing the vanilla FunkCode record stream is
-- structurally impossible — inserting a record shifts the byte offsets
-- that IF / BlockReader(0x42) / ELSE(0x3b) jumps use → the new-game
-- intro hangs (fade-to-black); appending at the end never dispatches.
-- So NPCs are created at RUNTIME via the engine's own create path.
--
-- Coordinates are KompassPos (the same space the F7 overlay dumps and
-- sacred.hero_pos() returns). spawn_at borrows the hero's (valid) sector,
-- so it's reliable for anything in/near the area the player is in.
--
-- ----------------------------------------------------------------------
-- Low-level API (sacred.*)
--   sacred.spawn_at(type, kx, ky)   -> handle|nil   spawn at KompassPos
--   sacred.spawn_here(type)         -> handle|nil   spawn on the hero
--   sacred.npc_info(h)              -> {type,kx,ky,faction}|nil
--   sacred.npc_wake(h)              activate AI (else it never reacts)
--   sacred.npc_set_stance(h,mode,v) 0=class-default, 1=explicit (2=aggro)
--   sacred.npc_set_faction(h,v)     cCreature+0x1F4 (bit0=awake/active)
--   sacred.npc_set_level(h,n)
--   sacred.npc_set_invulnerable(h[,on])   immortal/essential
--   sacred.npc_set_stationary(h[,on])     hold post (no patrol)
--
-- OOP wrapper (require "npcobj") — recommended:
--   local N = require "npcobj"
--   local o = N.spawn(type, kx, ky)      -- or N.spawn_here(type)
--   o:make_aggressive(level)   hunts the player on sight (enemy)
--   o:make_guard(level)        awake soldier: patrols + defends, friendly
--   o:make_immortal_passive(n) won't strike first, invulnerable, holds post
--   o:make_friendly() / :activate() / :set_stance() / :set_faction()
--   o:set_invulnerable() / :set_stationary() / :set_level()
--   o:pos() -> kx,ky   o:type()   o:faction()   o:alive()   o:info()
--   o:save("name")  -> stored in N.all; N.get("name") later (persistent)
--   You can also just stash o in a global; the handle stays valid while
--   the creature lives (o:alive() checks).
--
-- Archetypes were decoded 1:1 from vanilla CreateNPC (FUN_00482510):
--   aggressive  = faction bit0 + stance 2 + wake
--   guard       = faction bit0 + class-default stance + wake (class AI)
--   passive     = class-default stance + wake (only retaliates)
-- ----------------------------------------------------------------------

-- IMPORTANT: the SDK bakes/executes EVERY .lua under custom/lua/** —
-- examples included. So this file is INERT by default (it would spawn
-- duplicate NPCs on top of your real mod otherwise). Flip ENABLED=true
-- only if you want to see this example run standalone.
local ENABLED = false
if not ENABLED then return {} end

local N   = require "npcobj"
local NPC = require "npc"            -- 474 creature type constants

-- This example is illustrative; real use goes in a class mod's
-- on_world_load/on_tick. Spawn ~2 ticks after the hero is in-world (so
-- the engine has placed the hero / the spawn-hijack settled).
local fired, settle = false, 0
sacred.on_world_load(function() fired, settle = false, 0 end)
sacred.on_tick(function()
  if fired then return end
  local hx, hy = sacred.hero_pos()
  if not hx then return end          -- not in-world yet
  settle = settle + 1
  if settle < 2 then return end
  fired = true

  -- a friendly royal guard that patrols & defends, near the hero
  local g = N.spawn(NPC.PRAETORIAN, hx + 6, hy)
  if g then g:make_guard(10) end

  -- an immortal, passive captain that holds his post
  local cap = N.spawn(NPC.MASTER_OF_COMBAT_ARTS, hx - 6, hy)
  if cap then cap:make_immortal_passive(20):save("captain") end

  -- a hostile monster so you can watch the guard engage it
  local e = N.spawn(NPC.SKELETON, hx + 30, hy)
  if e then e:make_aggressive(5) end

  sacred.log(("[ex14] guard=%s captain=%s enemy=%s")
    :format(tostring(g), tostring(cap), tostring(e)))
end)

-- Later, from anywhere, re-grab the persistent reference:
--   local cap = require("npcobj").get("captain")
--   if cap and cap:alive() then local x,y = cap:pos() end

return {}
