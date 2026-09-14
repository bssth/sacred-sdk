-- SacredSDK / lua / lib / npcobj.lua
--
-- OOP wrapper around a spawned creature handle. Spawn returns an object
-- with getters/setters/methods; stash it anywhere (a global, a quest
-- table) for a persistent NPC reference — the handle stays valid for the
-- creature's lifetime, and :alive() tells you if it's still there.
--
--   local N = require "npcobj"
--   local NPC = require "npc"
--   guard = N.spawn(NPC.SKELETON, 2793, 2273)   -- guard is a global
--   ...
--   if guard:alive() then
--     local kx,ky = guard:pos()
--     guard:set_faction(0x40)                    -- experiment with side
--   end
--
-- Also a named registry so you don't even need your own global:
--   N.spawn(NPC.UNICORN, 2800, 2273):save "pet"
--   N.get("pet"):faction()

local M = {}
M.all = {}                       -- name -> object (persistent registry)

local Npc = {}
Npc.__index = Npc

local function wrap(handle)
  return setmetatable({ _h = handle }, Npc)
end

-- Spawn `type` (npc.lua id) at KompassPos (kx,ky) — uses the hero-sector
-- path (reliable near the player). Returns an object, or nil + reason.
function M.spawn(type_id, kx, ky)
  local h = sacred.spawn_at(type_id, kx, ky)
  if not h then return nil, "spawn_at failed (engine returned 0)" end
  return wrap(h)
end

-- Spawn right at the hero (no coords).
function M.spawn_here(type_id)
  local h = sacred.spawn_here(type_id)
  if not h then return nil, "spawn_here failed" end
  return wrap(h)
end

-- Wrap an existing handle (e.g. one you stored as a number).
function M.wrap(handle) return wrap(handle) end

-- THE right spawn: build a dev-authored CreateNPC record from
-- npc_templates.lua and let the ENGINE's own handler create+init it
-- (type-correct HP/AI/faction/combat-arts — no struct poking, no
-- BFG/glow/1-HP edge bugs). arch = 'friendly_town_guard' |
-- 'bellevue_enemy' | 'patrol_soldier' | 'townsperson' | ... ; opts =
-- {type=, pos='CPOS:HERO'|defpos|int, name=, level=, group=, sub_id=,
-- link=}. Spawn at the hero (pos default 'CPOS:HERO') then :teleport()
-- to the target tile (engine-driven, no struct writes). Returns Npc|nil.
function M.spawn_template(arch, opts)
  opts = opts or {}
  if opts.pos == nil then opts.pos = "CPOS:HERO" end
  local TPL = require "npc_templates"
  local bytes = TPL.build(arch, opts)
  if not bytes then return nil, "unknown template: " .. tostring(arch) end
  local h = sacred.createnpc_engine(bytes, opts.type or 0)
  if not h then return nil, "createnpc_engine failed (handler/buffer?)" end
  local o = wrap(h)
  o._res = opts.name        -- the "res:<KEY>" records address this creature by
  return o
end

-- Spawn a pickup-able ground ITEM at KompassPos (item type id; same id
-- space & create path as creatures). Returns the raw object handle (a
-- number) or nil — items aren't cCreatures so the Npc methods don't
-- apply; the handle is for despawn/tracking.
function M.spawn_item(item_type, kx, ky)
  local h = sacred.spawn_item(item_type, kx, ky)
  if not h then return nil, "spawn_item failed" end
  return h
end

-- Named registry helpers.
function M.get(name)  return M.all[name] end
function M.forget(name) M.all[name] = nil end

-- --- instance methods ---------------------------------------------------

function Npc:handle() return self._h end

-- Raw info table {type, kx, ky, faction} or nil if the creature is gone.
function Npc:info() return sacred.npc_info(self._h) end

-- Still in the world? (handle resolves to a live creature)
function Npc:alive() return sacred.npc_info(self._h) ~= nil end

function Npc:type()
  local i = self:info(); return i and i.type
end

-- KompassPos x, y (same space as F7 / spawn). Returns nil if gone.
function Npc:pos()
  local i = self:info()
  if not i then return nil end
  return i.kx, i.ky
end

-- Faction / side word (cCreature+0x1F4). Getter + setter. The exact
-- friend/neutral/enemy bit mapping is still being pinned (npc_model.md
-- open item #1) — use this to experiment and report what each value does.
function Npc:faction()
  local i = self:info(); return i and i.faction
end
function Npc:set_faction(v)
  return sacred.npc_set_faction(self._h, v)
end

-- Activate the creature's AI (without this it stands still even when hit).
function Npc:wake() return sacred.npc_wake(self._h) end

-- Stance / behavior (cCreature+0x1F0). mode 0 = class-default (a Skeleton
-- becomes a real hostile skeleton, townsfolk neutral, …); mode 1 = set
-- explicit value. Runtime-created creatures have stance 0 ⇒ aimless
-- wander until this is called.
function Npc:stance(mode, val)
  return sacred.npc_set_stance(self._h, mode or 0, val or 0)
end

-- THE one-call "make this creature behave like its kind": class-default
-- stance + AI on. Skeleton → attacks, merchant → stands/trades, etc.
function Npc:activate()
  self:stance(0, 0)   -- class-default behavior for its type
  self:wake()
  return self
end

-- Level / rank.
function Npc:set_level(n) return sacred.npc_set_level(self._h, n) end

-- Convenience: make a spawned creature a live hostile — set faction +
-- wake the AI + optional level. `fac` defaults to the current best
-- enemy-bit guess (npc_model.md: side opcode 0x2f/0x31 => EBX 0x40);
-- tune live and report which value actually fights back.
-- AGGRESSIVE enemy — the exact vanilla quest-kill-target combo decoded
-- from CreateNPC (side op 0x08 + awake 0x12): explicit stance 2
-- (FUN_0052e420 mode 1 val 2 => cCreature+0x1F0=2) + awake bit in the
-- faction word (+0x1F4 bit0) + AI wake. This is "hunts the player on
-- sight", vs activate()'s class-default (passive, only retaliates).
function Npc:make_aggressive(level)
  self:set_faction(1)        -- +0x1F4 bit0 = awake/active
  self:stance(1, 2)          -- +0x1F0 = 2  (aggressive stance)
  if level then self:set_level(level) end
  self:wake()
  return self
end

-- Class-natural: class-default stance + AI on (passive monsters that
-- only fight back when hit — the "Marten" archetype).
function Npc:make_hostile(fac, level)
  if fac then self:set_faction(fac) end
  if level then self:set_level(level) end
  self:activate()
  return self
end

function Npc:make_friendly(fac)
  self:set_faction(fac or 0x08)
  self:activate()
  return self
end

-- Invulnerable / essential (CreateNPC 0xa1 path: cCreature+0x14 |0x200000).
function Npc:set_invulnerable(on)
  if on == nil then on = true end
  return sacred.npc_set_invulnerable(self._h, on)
end

-- Hold post (no patrol) / free to roam (CreateNPC 0x6b path).
function Npc:set_stationary(on)
  if on == nil then on = true end
  return sacred.npc_set_stationary(self._h, on)
end

-- Set current+max HP (cCreature+0x4d8). Runtime-spawned creatures get
-- weak default stats, so they die in one hit — pump this up.
function Npc:set_hp(n) return sacred.npc_set_hp(self._h, n) end

-- Replay CreateNPC's combat init (the bit the bare runtime spawn skips).
-- ai_class (hostility matrix, corrected polarity): 3 or 7 = ally DEFENDER
-- (proactively fights monsters, NEVER the hero/allies; hero is class 1),
-- 2 = a monster class, 13 = immune non-combatant. Returns self.
-- ai_class 7 = ACTIVE ally soldier (what vanilla Valorian Soldiers run —
-- proactively fights monsters, never the hero). 3 = passive townsperson
-- (retaliate-only, no HP scale). 2 = monster. 13 = immune non-combatant.
function Npc:make_combatant(level, ai_class)
  sacred.npc_make_combatant(self._h, level or 20, ai_class or 7)
  return self
end

-- SOLDIER archetype: a real ally DEFENDER — tough, proactively engages
-- monsters on sight, and NEVER attacks the hero/allies. The faction bug
-- is solved: hostility-matrix polarity was inverted in the earlier
-- analysis; class 3 (town/defender, == CreateNPC's no-side default) is
-- the correct ally class (class 2 was actually a monster class, which
-- is why it attacked the Vampiress). Engine-faithful combat init +
-- level-scaled HP; set_hp() is a tankiness backstop.
-- TEMP safe state while the template system is built: friendly ally +
-- class-default stance (retaliates, never attacks hero) + tanky HP.
-- The proper path = spawn from a dev-authored CreateNPC template via
-- the engine's own handler (npc_templates.lua, in progress) — no more
-- per-bit struct reconstruction.
function Npc:make_soldier(level, hp)
  self:set_faction(1)
  self:set_level(level or 20)
  self:set_hp(hp or 5000)
  self:activate()                       -- class-default + wake (retaliates)
  return self
end

-- Royal guard: the vanilla ally-guard combo (side 0x08 + awake 0x12) is
-- +0x1F4 bit0 (awake) + WakeUp + class-default stance — the soldier's
-- own AI then patrols and defends, friendly to the player. plain
-- activate() lacked the awake bit (set_faction 1) so they just stood.
function Npc:make_guard(level)
  if level then self:set_level(level) end
  self:set_faction(1)        -- +0x1F4 bit0 = awake/active (ally soldier)
  self:activate()            -- class-default stance + AI wake
  return self
end

-- Captain-type: passive (won't strike first), immortal, holds post.
function Npc:make_immortal_passive(level)
  if level then self:set_level(level) end
  self:set_faction(1)        -- active, but...
  self:activate()            -- class-default stance (won't attack first)
  self:set_invulnerable(true)
  self:set_stationary(true)  -- stands his post, no patrol
  return self
end

-- ---- storyline layer --------------------------------------------------

-- Custom display name (e.g. "Captain Miles"). Writes the DlgNPC/NameArrA
-- entry keyed by this handle. NOTE: a purely runtime-spawned creature
-- often has no such entry yet, so this can be a no-op (returns false) —
-- the icon/teleport below always work; name is the one pending an engine
-- DlgNPC-entry path. Harmless to call.
function Npc:set_name(name)
  self._name = name
  return sacred.npc_set_name(self._h, name)
end
function Npc:name() return self._name end

-- Floating "?!" quest-giver glyph above the head (visual only, safe).
function Npc:quest_icon(on)
  if on == nil then on = true end
  return sacred.npc_quest_icon(self._h, on)
end

-- Keep this character on his feet. Two layers, both the engine's own:
--   * `revive_on_fall` -- the flag below, which makes the engine restore him to
--     full health when he is dropped out of the party. This is the vanilla
--     "faints and wakes up", and it is what the shipped story companions have.
--   * a deep health pool topped up on the heartbeat (persona.lua does the
--     topping up). Live: ordinary blows could not kill such an NPC at all; only
--     a single spell big enough to outrun one heartbeat did.
--
-- Three things that do NOT protect anyone, all measured live on 2026-09-12:
--   * the creature TYPE class 5 death-skip -- class 5 turns out to be the UNDEAD
--     category (a census of types 1..800: skeletons, zombies, liches, mummies,
--     the Vampiress), i.e. the "rises again" mechanic, and the test subject died
--     anyway, with his max health zeroed;
--   * the CreateNPC op-0xa1 bit (cCreature+0x14 bit 0x200000) is a LOOK: the
--     creature turns into the translucent burning figure the shipped Valor's
--     Ghost is. Nothing in the damage path reads it. Use `Npc:ghost(on)` when
--     you want that look, and do not expect protection from it.
--   * faction class 13 is labelled "Hirelings" in game and is attacked like any
--     other side -- the class-13 test subject was the first one killed.
function Npc:immortal(on, hp)
  if on == nil then on = true end
  self:revive_on_fall(on)                 -- the engine's own "gets back up"
  if on then self:set_hp(hp or 100000) end
  return self
end

-- The engine's "he gets back up" flag: creature+0x2B7 bit 8, what the vanilla
-- CreateNPC op 0x6b sets on 421 shipped NPCs. When a party member is dropped --
-- or released -- the engine calls its remove-from-party routine, and that
-- routine's FIRST act is: if this bit is set, restore his health to full
-- (FUN_0054d380 @0x54d3ad -> the full heal at FUN_0053e460, and the same test at
-- FUN_0052e590 @0x52e71a on the release path). That is the faint-and-wake a
-- vanilla story companion does. The bit doubles as "do not roam", which is why
-- the SDK used to call it `set_stationary`.
function Npc:revive_on_fall(on)
  if sacred.npc_set_stationary then sacred.npc_set_stationary(self._h, on ~= false) end
  return self
end

-- The ghost look (op 0xa1's bit): translucent and burning, like Valor's Ghost.
function Npc:ghost(on)
  if sacred.npc_set_invulnerable then
    sacred.npc_set_invulnerable(self._h, on ~= false)
  end
  return self
end

-- The glyph over the head, the vanilla way: a SetIcon record on the dialog node
-- this NPC is bound to. `kind` is "offer" (the "!"), "handin" (the "?"), "none",
-- or a raw marker number (verbs.lua Vb.ICON). Needs bind_quest() first.
function Npc:icon(kind)
  local Vb = require "verbs"
  local A  = require "actions"
  assert(self._name, "Npc:icon needs bind_quest() first")
  return A.run(Vb.set_icon(self._name, kind or "offer"))
end

-- Move this NPC to another dialog node, the vanilla way (SetNPCState op 0x09):
-- it switches the window AND the glyph in one record. `node` is a DlgNPC name;
-- with no argument the NPC is unbound (no dialog, no glyph).
function Npc:node(node)
  local Vb = require "verbs"
  local A  = require "actions"
  local who = self._res or self._name
  assert(who, "Npc:node needs a spawn name (res:) or bind_quest() first")
  if node then return A.run(Vb.npc_state(who, Vb.ST.node(node))) end
  return A.run(Vb.npc_state(who, Vb.ST.no_node))
end

-- Teleport this NPC to KompassPos (kx,ky), or to a named position
-- (Npc:teleport("pos_ziel1501"), area.lua A.pos), via the engine path (the
-- same one the hero spawn uses). Non-destructive: false if engine rejected
-- the tile (retry next tick). Vanish idiom = teleport off-map then let
-- it despawn, or keep a far "limbo" DefPos.
function Npc:teleport(kx, ky)
  if type(kx) == "string" then
    local name = kx
    kx, ky = require("area").pos(name)
    if not kx then return false, "unknown position: " .. name end
  end
  return sacred.npc_teleport(self._h, kx, ky)
end

-- ── Companions + dynamic behavior (npc_ai_flags.md §5, LIVE-CONFIRMED 2026-09-10) ──
-- Make this NPC the hero's companion through the engine's own script-
-- "follow" path (command 0x10B to the creature): it follows the hero
-- (leash + catch-up), fights for him, is treated as the hero for friend/
-- foe, KEEPS its level, and is registered in the hero's party vector
-- (hero+0x39c) — so its portrait shows in the companion panel. Reversible
-- with :dismiss(). quest_id is accepted for compatibility (the old
-- qm+0x31c roster theory is retired; that call is a harmless no-op).
-- opts.combat = true -> hireling AI mode (0x100): joins the hero's fights
-- (attacks what the hero attacks). Default = follow/pet mode (4): follows and
-- defends itself only. Level is kept in both modes.
function Npc:make_companion(quest_id, opts)
  local combat = opts and opts.combat and true or false
  return sacred.npc_make_companion(self._h, combat)
end
-- Recruit as a true HIRELING via the engine's own FUN_0054cf70 (script
-- keyword "hireling"): follows with a leash, fights, gets a panel portrait,
-- but is RE-LEVELED to the hero's level (vanilla mercenary rule). For story
-- companions that must keep their level use :make_companion() instead.
function Npc:hire(flag) return sacred.npc_hire(self._h, flag or 1, 0) end
-- Stop following, restore independent neutral AI + leave the panel.
function Npc:dismiss()
  sacred.npc_roster_remove(self._h)
  return sacred.npc_dismiss(self._h)
end
-- Clean engine removal (the DelNPC path). After this the Npc is dead.
function Npc:despawn()
  M.forget_speaker(self._h)
  return sacred.npc_despawn(self._h)
end

-- ── Text-override speakers (Npc:say / dialog_off) ───────────────────────────
-- The DLL's by-speaker text hook (text_logger.cpp speaker_lookup) keeps
-- {handle -> text key} and treats creature+0x150 == 6 or 7 as "this NPC's window
-- is open". 6 is the engine's DEAD state (FUN_00549080), so a registered NPC that
-- died answered for every vanilla conversation from then on -- LIVE 2026-09-14:
-- the slaver chief's corpse put his line into the mouths of vanilla soldiers. A
-- freed handle can also come back as another creature. So every registration is
-- remembered with the creature's type, and one that died, vanished or changed
-- type is dropped from the hook.
M._speakers = M._speakers or {}

local function track_speaker(o, key)
  if not sacred.dialog_speaker then return end
  sacred.dialog_speaker(o._h, key)
  if key then M._speakers[o._h] = { type = o:type() } else M._speakers[o._h] = nil end
end

function M.forget_speaker(h)
  if M._speakers[h] and sacred.dialog_speaker then sacred.dialog_speaker(h, nil) end
  M._speakers[h] = nil
end

local spk_t = 0
sacred.on_tick(function()
  spk_t = spk_t + 1
  if spk_t < 4 then return end
  spk_t = 0
  for h, s in pairs(M._speakers) do
    local inf = sacred.npc_info(h)
    local a = inf and sacred.npc_ai and sacred.npc_ai(h)
    local why = (not inf and "gone") or (inf.type ~= s.type and "another creature")
             or (a and a.s150 == 6 and "dead") or nil
    if why then
      M.forget_speaker(h)
      sacred.log(("[say] h=%d %s: its text override is dropped"):format(h, why))
    end
  end
end)

-- Put the NPC on (kx, ky) AND make that its home, so the idle AI keeps it there
-- instead of walking it back to where it was created (Vb.ST.anchor).
function Npc:place(kx, ky)
  local ok = self:teleport(kx, ky)
  self:home(kx, ky)
  return ok
end

function Npc:home(kx, ky)
  local Vb = require "verbs"
  return self:state(Vb.ST.anchor(kx, ky, 0))
end

-- The SetNPCState record (tag 0x03) for this NPC with state ops (Vb.ST), for
-- A.run. While the NPC is bound to its dialog node and not muted, the record also
-- carries `09 <node>` -- the way vanilla writes it next to a home change
-- (`01 'res:17523' 4d 4097 2547 0 09 'auftrag9011'`, 16 of 39 op-4d records).
-- LIVE 2026-09-14: after a SetNPCState 4d alone Rocheford stopped answering a
-- click; re-binding in the same record keeps the dialog whatever the handler
-- resets. Needs the spawn name (`_res`); nil without one.
function Npc:state_record(...)
  local who = self._res
  if not who then return nil end
  local Vb = require "verbs"
  local ops = { ... }
  if self._name and self._dlg and not self._mute then ops[#ops + 1] = Vb.ST.node(self._name) end
  return Vb.npc_state(who, table.concat(ops))
end

function Npc:state(...)
  local r = self:state_record(...)
  if not r then return false end
  return require("actions").run(r)
end
-- Change disposition after an event. d = 'hostile' (attacks the hero),
-- 'ally' (fights monsters, friendly to hero), 'neutral' (immune non-
-- combatant). Re-aggros so it takes effect immediately mid-game.
local _DISPO = { hostile = 2, ally = 7, neutral = 13 }
function Npc:set_disposition(d)
  local v = _DISPO[d]
  if not v then return false end
  return sacred.npc_set_disposition(self._h, v)
end
-- Walk/relocate to a KompassPos point. Uses the proven engine teleport
-- (HIGH); a true pathing-walk (NPC_Goto ring) is a documented TODO
-- pending one BP — teleport covers "now be at point" for storyline.
function Npc:walk_to(kx, ky)
  return sacred.npc_teleport(self._h, kx, ky)
end

-- Walk to (kx, ky) on its own legs: vanilla's NPC_Goto by the spawn name
-- (`_res`), watched until it gets there. The destination also becomes the NPC's
-- HOME (SetNPCState op 0x4d, Vb.ST.anchor) in the same section run: the idle AI
-- walks a creature that strays ~3.7 tiles from its home back there
-- (FUN_00542b20:333-360), and a teleport does not move the home. LIVE
-- 2026-09-14, before this: Slayer and Bladelok walked 5-15 tiles and stopped, and
-- Rocheford, teleported 500 tiles from his home, did not move at all, with or
-- without op 0x66. One that has not left its tile in ~4 s is sent again, and
-- after `opts.tries` (3) it is put on the spot. `on_arrive(self, placed)` fires
-- once, within 3 tiles. `opts.run` adds NPC_Goto's op 0x66, the other move mode
-- (walk versus run still unconfirmed). A walk is Lua state: a savegame keeps the
-- NPC where it was, not the walk. `Npc:arrive()` ends a walk now: the NPC is put
-- on the spot and on_arrive fires. Talking pauses the watch.
local walks = {}

local function send_walk(o, kx, ky, run)
  local Vb = require "verbs"
  return require("actions").run(o:state_record(Vb.ST.anchor(kx, ky, 0)),
                                Vb.npc_goto(o._res, { kx, ky }, run))
end
function Npc:go(kx, ky, on_arrive, opts)
  assert(self._res, "Npc:go needs the spawn name (spawn_template, or set o._res)")
  opts = opts or {}
  self:set_stationary(false)                      -- the flag also holds it in place
  send_walk(self, kx, ky, opts.run)
  walks[self._h] = { o = self, x = kx, y = ky, fn = on_arrive, run = opts.run,
                     tries = opts.tries or 3, t = 0 }
  return self
end

function Npc:walking() return walks[self._h] ~= nil end

function Npc:arrive()
  local w = walks[self._h]
  if not w then return false end
  walks[self._h] = nil
  self:teleport(w.x, w.y)
  sacred.log(("[walk] %s put on %d,%d"):format(tostring(self._res), w.x, w.y))
  if w.fn then
    local ok, err = pcall(w.fn, self, true)
    if not ok then sacred.log("[walk] on_arrive failed: " .. tostring(err)) end
  end
  return true
end

sacred.on_tick(function()
  for h, w in pairs(walks) do
    if not w.o:alive() then
      walks[h] = nil
    elseif sacred.npc_in_dialog(h) then
      w.t, w.last = 0, nil                        -- talking is not being stuck
      w.talked = true
    else
      if w.talked then                            -- the talk ended: walk on
        w.talked = nil
        send_walk(w.o, w.x, w.y, w.run)
      end
      w.t = w.t + 1
      if w.t >= 16 then
        w.t = 0
        local x, y = w.o:pos()
        local here = ("%s,%s"):format(tostring(x), tostring(y))
        local dx, dy = (x or 0) - w.x, (y or 0) - w.y
        local done, placed = dx * dx + dy * dy <= 9, false
        if not done and here == w.last then        -- stuck on the same tile as 4 s ago
          w.tries = w.tries - 1
          if w.tries > 0 then
            send_walk(w.o, w.x, w.y, w.run)
            local a = sacred.npc_ai and sacred.npc_ai(w.o._h)
            sacred.log(("[walk] %s stuck at %s, walking again (%d left; ai state %s fc %s s150 %s)")
              :format(tostring(w.o._res), here, w.tries, tostring(a and a.state),
                      tostring(a and a.fc), tostring(a and a.s150)))
          else
            w.o:teleport(w.x, w.y)
            sacred.log(("[walk] %s could not path to %d,%d; placed there")
              :format(tostring(w.o._res), w.x, w.y))
            done, placed = true, true
          end
        end
        w.last = here
        if done then
          walks[h] = nil
          if w.fn then
            local ok, err = pcall(w.fn, w.o, placed)
            if not ok then sacred.log("[walk] on_arrive failed: " .. tostring(err)) end
          end
        end
      end
    end
  end
end)

-- EXPERIMENTAL: give the NPC a visible item. item_type = item type id
-- (same id space as creatures). slot defaults to 0xC (weapon hand);
-- 0xD = off-hand, 0..0x12 per the hero equip map. Guarded in C; if the
-- create ABI guess is off it just returns false (cannot crash).
-- slot default 0x0D = MAIN weapon hand (items_equip.md). 0x0C=off-hand,
-- 0x12=mount. item_type = an items_gen.lua id (5624-item catalog).
function Npc:equip(item_type, slot)
  return sacred.npc_equip(self._h, item_type, slot or 0x0D)
end

-- Bind this runtime spawn as a REAL dialog/quest NPC: creates the engine
-- DlgNPC entry (the thing pure spawns lack) so the custom name shows AND
-- the overhead quest marker draws. Returns the DlgNPC index or nil.
-- Call ONCE, right after spawn, from on_tick (main thread). icon_on
-- defaults true (show the "talk to me / has quest" bubble).
-- Which vanilla Dialog: section a bound NPC talks through when the mod has not
-- baked its own. Until 2026-09-11 this was an accident: the creature's DlgNPC
-- index got truncated to a byte, so every NPC borrowed the vanilla declaration at
-- idx mod 256 - its buttons AND its consequences (Rocheford's re-reward was a
-- DLG_15024_ZIEL hand-in button). LIVE_S0_RESULTS.md 1. Now it is chosen:
-- Dialog:Wegweiser_SD is a global section (owner -1) present in all 20 script
-- sources, with ONE text line and ONE OK button whose handler (wegweiser_ok) is
-- a single no-op record. Our by-speaker override supplies the text; OK does
-- nothing but close the window. Pass node = false to bind with no section at
-- all (the NPC then opens no window), or any section name to choose another.
M.DEFAULT_DIALOG_NODE = "Dialog:Wegweiser_SD"

function Npc:bind_quest(name, icon_on, node)
  if icon_on == nil then icon_on = true end
  if node == nil then node = M.DEFAULT_DIALOG_NODE end
  self._name = name
  local idx = sacred.npc_bind_quest(self._h, name or "", icon_on, node or nil)
  self._dlg = idx
  self._mute = false
  return idx
end
function Npc:dlg_index() return self._dlg end

-- Make a bound NPC speak. `text_key` = a global.res resource NAME
-- registered via text.lua (T.named / T"..."), passed BARE (no "res:").
-- Optional `voice` = a SOUND_FX sample name (prefix auto-added).
-- Requires bind_quest() to have run (sets self._name = the DlgNPC name
-- the engine matches on). Call on the main tick. Returns bool.
-- Make a bound NPC speak our text. `text_key` = a baked global.res name. The
-- engine's talk window natively plays the creature's VANILLA quest dialog node
-- (resolved BY NAME via FUN_00672cf0). To show OUR line, pass `vanilla_node` =
-- that node's name and the SDK substitutes it for `text_key` natively (per-node,
-- no timing/gating). Discover the node name from the [dlgname] log: talk to the
-- NPC once, read `dialog line name="DQ_xxxxx_yyy"`, and pass it here.
function Npc:say(text_key, vanilla_node, voice)
  if not self._name then return false end
  local ok = sacred.dialog_arm(self._h, self._name, text_key, voice)
  -- by-NPC text: whatever vanilla node the engine attaches to this NPC's talk
  -- (it comes from the region quest pool and changes per game), show text_key.
  track_speaker(self, text_key)
  if vanilla_node and sacred.dialog_override then
    -- typo guard: warn (don't fail) if the node isn't in the vanilla catalogue.
    local okreq, nodes = pcall(require, "dialog_nodes")
    if okreq and nodes and nodes.set and not nodes.set[vanilla_node] then
      sacred.log("[say] WARN: '" .. tostring(vanilla_node) .. "' is not a known vanilla "
        .. "dialog node — check the [dlgname] log / the Dialog Nodes wiki page")
    end
    sacred.dialog_override(vanilla_node, text_key)   -- native by-name swap
  end
  return ok
end

-- End this NPC's quest step: drop the "?!" marker and the armed dialog
-- content, and (optionally) switch the line the NPC says from then on.
--
-- The engine keeps a bound quest NPC TALKABLE no matter what — unbinding it
-- also erases the nameplate, since both read the same by-handle entry. Vanilla
-- is the same: you can always re-talk to an escort, you just get a different
-- line. So "stop repeating the quest offer" is not "make him mute", it is
-- "give him his post-step line" — pass `next_key` (a baked global.res name,
-- optionally with the vanilla node to re-point) and a re-talk says that.
--
--   cap:dialog_off("CAPMILES_DONE", "DQ_15024_OFFEN")
--
-- Deliberately does NOT go through say(): that would re-arm the record and the
-- "?!" marker we just cleared. Only the TEXT maps are re-pointed.
-- Stop being a talk target at all (no talk cursor, no window) while keeping
-- the nameplate: zeroes the DlgNPC object index cCreature+0x245, which is what
-- the engine uses to FIND this NPC's dialog object. Reversible: set_talkable(true).
-- Needed because a bound quest NPC otherwise always gets a line — the engine
-- falls back to the region's dynamic-quest pool, so "the step is over" alone
-- never closes the dialog.
function Npc:set_talkable(on)
  if on == nil then on = true end
  self._mute = not on
  if not sacred.npc_talkable then return false end
  return sacred.npc_talkable(self._h, on)
end

function Npc:dialog_off(next_key, vanilla_node)
  local ok = sacred.dialog_clear(self._h)
  self:set_talkable(false)     -- and stop the pool from handing him a new line
  if next_key then
    track_speaker(self, next_key)
    if vanilla_node and sacred.dialog_override then
      sacred.dialog_override(vanilla_node, next_key)
    end
    -- Kept as a fallback: if something re-binds him later (a re-talk that
    -- slips through, a reload), the line he says is the post-step one.
    sacred.log(("[dialog_off] %s -> untalkable + post-step line '%s'")
      :format(tostring(self._name), tostring(next_key)))
  end
  return ok
end

-- One-call quest-giver: bind (name + marker entry) + immortal & holds
-- post — a storyline NPC the player walks up to and talks to. Now backed
-- by a real DlgNPC entry (no more no-op). Pass nil name for marker only.
function Npc:make_quest_giver(name, level)
  self:make_immortal_passive(level)   -- stance/invuln/stationary first
  self:bind_quest(name, true)         -- then the engine bind (name+marker)
  return self
end

-- ── Talk detection (combat_init.md / talk-signal-0x200) ───────────────
-- THE general "is the player talking to / interacting with this NPC right
-- now?" check. Backed by cCreature+0x200 bit 0x400, which the engine pulses
-- on player interaction (validated live: 4/4 talks, no false positives idle/
-- walk/fight). Works for ANY spawned NPC — no hook, no HW-BP, no quest tables.
function Npc:in_dialog()
  return sacred.npc_in_dialog(self._h)
end

-- Register a per-tick rising-edge detector: fn(self) is called ONCE each
-- time the player starts talking to this NPC. The clean SDK replacement for
-- the (dead) sacred.on_trigger("DLGANS:<name>") path — a runtime-spawned NPC
-- never fires a named trigger, but this signal always works. Returns self.
--   N.spawn_template(...):save("cap"):on_talk(function(o)
--     quest:advance(); o:dialog_off()
--   end)
-- Re-arms automatically across NPC death/respawn (edge resets when gone).
function Npc:on_talk(fn)
  local prev = false
  sacred.on_tick(function()
    if not self:alive() then prev = false; return end
    local now = sacred.npc_in_dialog(self._h) and true or false
    if now and not prev then
      local ok, err = pcall(fn, self)
      if not ok then sacred.log("[on_talk] handler error: " .. tostring(err)) end
    end
    prev = now
  end)
  return self
end

-- Falling-edge counterpart of on_talk: fn(self) is called ONCE each time the
-- player FINISHES talking (the native dialog window closes / "OK" pressed —
-- the +0x200 bit 0x400 goes 1→0). Use for effects that should land AFTER the
-- player has read the line (recruit companions, advance the quest, spawn the
-- next NPC) so they don't fire the instant the window opens. Returns self.
-- Fires ONCE per player answer (OK / Accept) on THIS NPC's dialog — the
-- vanilla moment for consequences (recruit, quest step, reward).
--
-- Mechanism (2026-09-10, after run 9). The engine's conversation state machine
-- FUN_0052AB70 holds cCreature+0x150 at 7 for as long as THIS NPC's window is
-- open and drops it the instant the player answers, so the window is a LEVEL,
-- not an event: `sacred.npc_in_dialog` reads that level (plus the old +0x200
-- bit-0x400 pulse as a fallback) and we fire on its FALLING edge.
--
-- The previous version watched only the 0x400 pulse and the DAT_00AB7394
-- global. The pulse lasts well under one 250 ms tick (talkprobe: 200=0x480 at
-- t=1, 0x90 at t=2 with the window still open), so arming was a coin flip — it
-- won in run 7 and lost in runs 8 and 9, which is exactly the "нажал ОК и
-- ничего не произошло" symptom. The global is still read, but only to report
-- WHICH button was clicked (0 = first/OK, -1 = window never wrote one).
--
-- fn(self, answer_index). Requires bind_quest() only for the log name.
local TALKWND = 0x00AB7394
local NOANSWER = 0xFFFFFFFF
function Npc:on_answer(fn)
  local me = self
  local talking, t_talk = false, 0
  sacred.on_tick(function()
    if not me:alive() then talking = false; return end
    local now = sacred.npc_in_dialog(me._h) and true or false
    if now then
      t_talk = t_talk + 1
      if not talking then
        t_talk = 1
        sacred.log(("[on_answer] %s: dialog OPEN"):format(tostring(me._name)))
      end
      -- Compact probe over the first few ticks: proves which field carries the
      -- window this run (s150 is the one we now trust) without flooding.
      if t_talk <= 3 and sacred.npc_ai then
        local a = sacred.npc_ai(me._h)
        if a then
          sacred.log(("[talkprobe] %s t=%d s150=%d s152=%d 200=0x%X 15c=%s | wnd=0x%X")
            :format(tostring(me._name), t_talk, a.s150, a.s152, a.f200,
                    tostring(a.t15c), sacred.peek_u32 and (sacred.peek_u32(TALKWND) or 0) or 0))
        end
      end
    elseif talking then
      -- FALLING EDGE = the player answered and the window closed.
      local w = sacred.peek_u32 and sacred.peek_u32(TALKWND) or 0
      local idx = (w == NOANSWER) and -1 or w
      sacred.log(("[on_answer] %s: dialog CLOSED after %d ticks, answer=%s -> fire")
        :format(tostring(me._name), t_talk, tostring(idx)))
      local ok, err = pcall(fn, me, idx)
      if not ok then sacred.log("[on_answer] handler error: " .. tostring(err)) end
    end
    talking = now
  end)
  return self
end

-- Give this NPC its OWN dialog node: our text and up to 4 buttons, each running
-- a Lua function when clicked (SDK sections, sdk/sdk_sections.inc). The engine
-- finds the node as "Dialog:<bind name>" before anything else, so call
-- bind_quest() first; the node then wins over the default node of bind_quest.
--   o:dialog{ text = "MY_KEY",
--             buttons = { { label = "res:1037", on = function(o) ... end },   -- Accept
--                         { label = "res:1038" } } }                         -- Reject: closes
-- Labels: "res:<id>" (1024 OK, 1037 Accept, 1038 Reject) or "res:<NAME>" baked
-- with text.lua. No buttons given = a single OK. Calling it again replaces the node.
-- A button's `records` (a record string or a list) run inside the engine the
-- moment it is clicked, before `on` gets the heartbeat after: a price paid with
-- IF HasGold { AddGold -N; SetVar }, so Lua only reads the variable afterwards.
M._section_fn = M._section_fn or {}
function Npc:dialog(spec)
  local S = require "sections"
  assert(self._name, "Npc:dialog needs bind_quest() first")
  if not sacred.section_define then
    sacred.log("[dialog] this SDK build has no sacred.section_define")
    return self
  end
  local base = (self._name:gsub("[^%w]", "_"))
  local parts = { S.text(spec.text) }
  local me = self
  for k, b in ipairs(spec.buttons or { { label = S.OK } }) do
    if k > 4 then break end
    local action = ("sdk_%s_b%d"):format(base, k)
    local recs = b.records or ""
    if type(recs) == "table" then recs = table.concat(recs) end
    S.define(action, recs)                         -- the engine's part; Lua acts after
    if M._section_fn[action] == nil then           -- one trigger per action, ever
      sacred.on_trigger("SECTION:" .. action, function()
        local fn = M._section_fn[action]
        if not fn then return end
        local ok, err = pcall(fn)
        if not ok then sacred.log("[dialog] " .. action .. " handler error: " .. tostring(err)) end
      end)
    end
    M._section_fn[action] = b.on and function() b.on(me) end or false
    parts[#parts + 1] = S.button(b.label or S.OK, action)
  end
  S.define("Dialog:" .. self._name, table.unpack(parts))
  return self
end

function Npc:on_talk_end(fn)
  local prev = false
  sacred.on_tick(function()
    if not self:alive() then prev = false; return end
    local now = sacred.npc_in_dialog(self._h) and true or false
    if prev and not now then
      local ok, err = pcall(fn, self)
      if not ok then sacred.log("[on_talk_end] handler error: " .. tostring(err)) end
    end
    prev = now
  end)
  return self
end

-- Persist under a name in the module registry; returns self for chaining.
function Npc:save(name)
  M.all[name] = self
  return self
end

function Npc:__tostring()
  local i = self:info()
  if not i then return ("Npc<%s: gone>"):format(tostring(self._h)) end
  return ("Npc<h=%s type=%d @(%d,%d) fac=0x%X>")
    :format(tostring(self._h), i.type, i.kx, i.ky, i.faction)
end

return M
