-- examples/15_storyline.lua — the RUNTIME STORYLINE layer.
--
-- Everything here is replicated at runtime (no FunkCode edit — the record
-- stream cannot be edited, see example 14's header). Built from the
-- overnight RE campaign; full reference in docs/23-npc-runtime.md and the
-- scratch reports (funkcode_tags / quest_storyline / items /
-- triggers_dialog_move / vampiress_quests).
--
-- ----------------------------------------------------------------------
-- New sacred.* primitives (also on the npcobj OOP wrapper):
--   sacred.npc_set_name(h, "Captain Miles")  display name (DlgNPC entry)
--   sacred.npc_quest_icon(h [,on])           floating "?!" glyph (visual)
--   sacred.spawn_item(type, kx, ky)          pickup-able ground item
--   sacred.npc_teleport(h, kx, ky)           engine teleport, any NPC
--   sacred.npc_equip(h, item_type [,slot])   visible weapon (EXPERIMENTAL)
--
-- npcobj methods: o:set_name(s) o:quest_icon(on) o:teleport(kx,ky)
--   o:equip(item_type[,slot])  o:make_quest_giver(name[,level])
--   npcobj.spawn_item(type,kx,ky)
--
-- CONFIDENCE / honesty (from the RE):
--  * quest_icon  — HIGH, pure memory write, visual only, safe.
--  * npc_teleport— HIGH, reuses the proven engine teleport (the same
--                  FUN_0054d9d0 the hero spawn uses), non-destructive.
--  * spawn_item  — HIGH, items share the creature create path & id space.
--  * set_name    — offsets HIGH, but a *purely* runtime-spawned creature
--                  usually has no DlgNPC/NameArrA entry yet, so the write
--                  is a no-op until that entry exists (open item — needs
--                  the DlgNPC-append / nameplate-renderer BP). Returns
--                  false when it no-ops; harmless.
--  * npc_equip   — MED, item create ABI not yet BP-confirmed; guarded so
--                  a wrong guess can't crash. Treat as experimental.
-- ----------------------------------------------------------------------

-- IMPORTANT: the SDK executes EVERY .lua under custom/lua/** — examples
-- included. INERT by default so it doesn't spawn duplicate NPCs over your
-- real mod (that's what caused the "three Captains" cluster). Flip
-- ENABLED=true to run this example standalone.
local ENABLED = false
if not ENABLED then return {} end

local N   = require "npcobj"
local NPC = require "npc"

local fired, settle = false, 0
sacred.on_world_load(function() fired, settle = false, 0 end)
sacred.on_tick(function()
  if fired then return end
  local hx, hy = sacred.hero_pos()
  if not hx then return end
  settle = settle + 1
  if settle < 2 then return end
  fired = true

  -- A quest-giver the player walks up to: named, "?!" over the head,
  -- immortal, holds his post. (name shows once it has a dialog entry.)
  local giver = N.spawn(NPC.MASTER_OF_COMBAT_ARTS, hx + 4, hy)
  if giver then
    giver:make_quest_giver("Captain Miles", 20):save("miles")
    sacred.log("[ex15] quest-giver spawned: " .. tostring(giver))
  end

  -- A guard that, 5 s later, teleports to a forward post (storyline
  -- "the scout moves out" beat). Teleport is the engine path — correct
  -- sector, no fade.
  local scout = N.spawn(NPC.PRAETORIAN, hx + 8, hy)
  if scout then
    scout:make_guard(10):save("scout")
    local t = 0
    sacred.on_tick(function()
      if not scout:alive() then return end
      t = t + 1
      if t == 120 then       -- ~5 s (24 tick/s)
        local ok = scout:teleport(hx + 30, hy)
        sacred.log(("[ex15] scout teleport -> %s"):format(tostring(ok)))
      end
    end)
  end

  -- A pickup-able item on the ground next to the hero. `1` is a
  -- placeholder type id — use sacred item type ids (same space as
  -- npc.lua creature ids; an item-id table is the next tooling step).
  local item = N.spawn_item(1, hx + 2, hy)
  sacred.log(("[ex15] ground item handle=%s"):format(tostring(item)))

  -- Experimental: hand the captain a weapon (item type id, slot 0xC =
  -- weapon hand). Safe no-op if the ABI guess is wrong.
  -- if giver then giver:equip(1, 0x0C) end
end)

return {}
