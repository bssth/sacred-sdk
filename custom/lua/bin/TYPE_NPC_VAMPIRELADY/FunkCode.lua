-- SacredSDK mod — Vampiress storyline base.
--
-- Goal: retail Vampiress world, fully populated, but no built-in quests in
-- the journal — a clean slate to grow a new storyline on.
--
-- WHY NOT DELETE QUEST RECORDS: empirically (and by design) removing ANY
-- FunkCode record corrupts the IF / BlockReader / ELSE-jump blocks the
-- new-game intro (Shaddar cinematic + fade-in) is wrapped in -> the screen
-- stays faded to black. So the scripts stay 100% VANILLA here. Instead we
-- suppress the *vanilla* quests at runtime: sacred.hide_other_quests()
-- zeroes the journal render-gate + map-marker on every quest registry
-- entry that the SDK does NOT own. Quest logic still runs (intro intact);
-- it just never shows. Idempotent, called every tick.

-- SPAWN: the engine teleport hook (FUN_0054d9d0). on_world_load arms it;
-- the FIRST teleport whose target is the active hero (= the campaign's
-- own new-game start placement) gets its destination rewritten to
-- (SPAWN_KX,SPAWN_KY) and the engine does the move via its normal path
-- (correct sector, no fade). The "[tp] ... <HERO> xy=(X,Y)" log lines
-- reveal the real start-teleport coordinate space if (2796,2261) lands
-- wrong — we then feed coords in that space.
local SPAWN = true    -- normal: teleport hero to the captain scene.
local HIDE  = true    -- normal: suppress vanilla quests.
-- (Bake-time CreateNPC injection is structurally impossible: a mid-stream
-- insert breaks the byte-counted IF/ELSE jumps and a tail append never
-- dispatches. Every NPC here is created at RUNTIME.)

-- BASE: which vanilla FunkCode to build on.
--   "campaign" = retail Vampiress (castle/Shaddar intro, full story+quests)
--   "netscript" = the Addon MP script (Ancaria, NO campaign storyline —
--                 ~78.7k recs vs 125k, QuestCode 15 B). The "almost clean"
--                 base: no built-in main/secondary quests to fight, the
--                 player spawn is the MP `tag 0x2e -> MPStartN` mechanism
--                 our arm_spawn_teleport already hijacks. Clean canvas for
--                 a new storyline. Revert by setting "campaign".
local BASE = "campaign"

local v   = require "vanilla"
local NPC = require "npc"
local V   = require "vars"
-- Q1's step lives in an engine variable, so a savegame carries it (vars.lua;
-- SDK_GAPS gaps 13 and 1). SDKQ_99_HCAP / SDKQ_99_HROCH hold the handles of the
-- NPCs spawned for it, for the save/load probe.
local Q1_VAR = "SDKQ_99"

-- Vanilla, untouched (return value drives the bake). No queststrip.
local recs = (BASE == "netscript")
  and v.load "bin/Addon/NetScript/FunkCode"
  or  v.load "bin/TYPE_NPC_VAMPIRELADY/FunkCode"
sacred.log("[vamp] base = " .. BASE .. " (" .. #recs .. " records)")

-- ============================================================
-- Story-quest suppression — race-free, scripts stay 100% vanilla
-- ============================================================
-- Hooks the journal builder FUN_006b07e0: right before it reads the
-- render gate (entry+0x24), every NON-SDK quest's gate is zeroed, so no
-- vanilla quest row ever shows. Quest logic + the new-game intro run
-- untouched (we never delete/edit a FunkCode record). One call, persists.
if HIDE then
  sacred.hide_vanilla_quests(true)
  sacred.log("[vamp] hide_vanilla_quests ON (journal-builder hook)")
end

-- ============================================================
-- NEW SPAWN — engine-teleport hijack (KompassPos 2796,2261, F7 'SPAWN')
-- ============================================================
-- Arm the FUN_0054d9d0 hook on every world load. The first hero teleport
-- after that = the campaign's own new-game placement; the hook rewrites
-- its destination, so the ENGINE moves the hero (correct sector, no
-- fade). Watch the log:
--   [tp] FUN_0054d9d0 ... <HERO> xy=(X,Y) lvl=.. flag=..  (real start args)
--   [tp] HERO start-teleport HIJACK: (X,Y) -> (2796,2261)
-- If (2796,2261) lands wrong, the <HERO> xy line shows the engine's true
-- coordinate space and we feed coords in THAT space.
local SPAWN_KX, SPAWN_KY = 2796, 2261

if SPAWN then
  -- Campaign (proven): ONE-SHOT hijack of the first hero teleport,
  -- KEEPING the engine's own level (no 3rd/4th arg). The lvl-force +
  -- 8 s rewrite-window were NetScript hacks that broke campaign spawn.
  -- Every NEW GAME's start placement comes here; a savegame load is never
  -- moved. The DLL arms the one-shot hijack while the engine builds a new-game
  -- world (sacred.set_new_game_spawn), so a second new game in one process gets
  -- it too. A hijack still armed once the world is ready (no start teleport
  -- came) is dropped then, so it cannot catch a later teleport.
  sacred.set_new_game_spawn(SPAWN_KX, SPAWN_KY)
  V.on_ready(function() sacred.disarm_spawn_teleport() end)
end

local N = require "npcobj"

-- ============================================================
-- SCENE: royal guard post — Valorian soldiers (friendly, fight enemies)
-- + Captain Miles (immortal, won't strike first) at the last post.
-- F7-captured KompassPos. No skeletons.
-- ============================================================
local SCENE = true    -- normal: spawn the captain scene (talk-advance works).
-- Varied elite/royal guard models so the post looks distinct.
-- All guards = Valorian Swordsman (uniform squad).
local GUARD_POSTS = {            -- {type, kx, ky}
  { NPC.VALORIAN_SWORDSMAN, 2799, 2287 },
  { NPC.VALORIAN_SWORDSMAN, 2800, 2286 },
}
-- Captain = a Knight (distinct from the Swordsman squad).
local CAPTAIN_TYPE = NPC.FEALTYBOUND_KNIGHT     -- "knight" officer
local CAPTAIN_POS  = { 2793, 2284 }             -- Captain Miles
if SCENE then
  local NPCo = require "npcobj"
  -- Captain display name. RE (quest_storyline.md "Runtime NPC display-name
  -- source"): the nameplate name is NOT DlgNPC entry+0x04 (that's a dialog
  -- key) — it is the CreateNPC (tag 0x01) name, which the engine stores
  -- into NameArrA, and it MUST be a global.res KEY (resolved handle), not a
  -- raw string. So register the text in global.res via text.lua at BAKE
  -- time (T.named runs now, during bake; the C++ baker flushes it into
  -- custom/scripts/us/global.res), and feed the engine the "res:KEY" form
  -- as the CreateNPC name (quest_npc template carries the {0x01,'NAME'}
  -- opcode). Engine writes NameArrA itself, exactly like a vanilla NPC.
  local CAP_NAME = require("text").named("CAPMILES_NAME", "Captain Miles")
  -- Captain's first dialog line — registered in global.res at BAKE time
  -- (same proven path as the name). dialog_arm takes the BARE key.
  require("text").named("CAPMILES_GREET",
    "Amon-Shi, I don't mean to interrupt your well-deserved rest, but in "
    .. "light of current events, relaxing at your watermill is... well, "
    .. "never mind.\n\n"
    .. "We're from Rocheford. He wants to talk to you and fill you in on "
    .. "the case he's currently working on. It has to do with the slave "
    .. "trade. With everything that's going on, it doesn't even seem like "
    .. "a big deal anymore... but it still needs to be resolved.\n\n"
    .. "Ready to go?")
  SCENE_NPCS = SCENE_NPCS or {}        -- GLOBAL: persistent refs
  SCENE_GUARDS = SCENE_GUARDS or {}    -- GLOBAL: guards only (proactive)
  -- IDEMPOTENT: never clone the scene. on_world_load can re-fire (known
  -- flaky 2nd-new-game) and old NPCs persist in the world — so guard on
  -- "are my scene NPCs still alive?" rather than a bool that gets reset.
  local settle = 0
  local EQUIP_GUARDS = true    -- arm guards with the Vampiress start sword
  local TEMPLATE_GUARDS = true -- engine-CreateNPC template path (ABI fixed)
  -- GUARD_MAINTAIN: per-tick set_faction(9) CLOBBERED the whole +0x1F4
  -- word to a bare 9, stripping the engine's real faction/AI bits (a
  -- vanilla active type-286 guard has +0x1F4=0x80400012, NOT 9). Effect:
  -- guards dropped into a default patrol/wander AI — ran to houses and
  -- stopped fighting. So the clobber is the WRONG fix (fighting the AI).
  -- OFF until the RE pass (combat_init.md "Proactive guard aggro —
  -- re-grounded") gives the engine-faithful PERSISTENT state (a CreateNPC
  -- opcode or a precise bit set, never a per-frame whole-word clobber).
  GUARD_MAINTAIN = false
  local function scene_alive()
    for _, o in ipairs(SCENE_NPCS) do
      if o and o.alive and o:alive() then return true end
    end
    return false
  end
  -- Every ready world starts from nothing, except after a savegame load: the
  -- savegame brings our NPCs back with the same handles (measured 2026-09-11),
  -- so they are adopted by the handles saved in variables, not spawned twice.
  SCENE_ADOPTED = SCENE_ADOPTED or false   -- GLOBAL: Q1's restore reads it
  local function adopt(var, want)
    local h = V.get(var)
    local inf = h and sacred.npc_info(h)
    if inf and inf.type == want then return NPCo.wrap(h) end
    return nil
  end
  V.on_ready(function(loaded)
    if not loaded and SCENE_SERIAL == V.world() then return end   -- the same world: keep it
    SCENE_SERIAL = V.world()
    settle = 0
    SCENE_NPCS, SCENE_GUARDS, CAP_SOFT, SCENE_ADOPTED = {}, {}, nil, false
    if not loaded then return end            -- a new world: the tick builds the scene
    local cap = adopt("SDKQ_99_HCAP", CAPTAIN_TYPE)
    if not cap then return end               -- he did not come back: build it fresh
    cap._name = "Captain Miles"              -- what bind_quest set; Npc:dialog needs it
    cap:save("captain_miles")
    CAP_SOFT, SCENE_ADOPTED = cap, true
    SCENE_NPCS[1] = cap
    for i, g in ipairs(GUARD_POSTS) do
      local o = adopt(("SDKQ_99_HG%d"):format(i), g[1])
      if o then
        SCENE_NPCS[#SCENE_NPCS + 1] = o
        SCENE_GUARDS[#SCENE_GUARDS + 1] = o
      end
    end
    sacred.log(("[persist] scene adopted: captain h=%d, %d of %d guards")
      :format(cap:handle(), #SCENE_GUARDS, #GUARD_POSTS))
  end)
  CAP_SOFT = CAP_SOFT or nil  -- GLOBAL: soft-immortal captain ref
  sacred.on_tick(function()
    -- Soft-immortal: top up the captain's HP every tick instead of the
    -- engine invuln bit (+0x14|0x200000) — that bit draws the red ward
    -- aura (measured: bare +14=40400000 no-glow vs invuln +14=40600000
    -- glow; single-bit 0x200000). HP-restore = invisible immortality,
    -- no engine patch, no side-effects on other creatures.
    if V.is_ready() and CAP_SOFT and CAP_SOFT.alive and CAP_SOFT:alive() then
      CAP_SOFT:set_hp(100000)
    end
    -- Guards: spawn-correct (engine-faithful friendly_town_guard +
    -- one-shot stance/wake), never maintained per-frame. No diagnostics.
    if scene_alive() then return end          -- already built & present
    if not V.is_ready() then return end       -- after a savegame load: adopted first
    local hx = sacred.hero_pos()
    if not hx then return end
    settle = settle + 1
    if settle < 2 then return end
    for i, g in ipairs(GUARD_POSTS) do
      local ty, kx, ky = g[1], g[2], g[3]
      -- TEMPLATE_GUARDS: the engine-CreateNPC path crashed (FUN_00482510
      -- invocation needs more context/cursor priming than the static
      -- recipe assumed — being hardened). OFF by default = safe known-
      -- good (friendly+tanky+retaliate). Flip true once the invocation
      -- is fixed & re-tested.
      local o, err
      if TEMPLATE_GUARDS then
        -- DEFINITIVE recipe (combat_init.md "Proactive guard aggro —
        -- DEFINITIVE"). The +0x200 contradiction was a misread: the
        -- engine's AI-arm is +0x204 (not +0x200), engine-set; +0x200 is
        -- irrelevant to aggro. Proactiveness = the picker's 800u scan,
        -- reachable only while +0xfe==1 (AWAKE, set EXCLUSIVELY by
        -- WakeUp) with +0x1F4=1 / +0xfc==0. The 'friendly_town_guard'
        -- template (02 04 02 08 12 00) makes the ENGINE itself set
        -- +0x1F4=1, run WakeUp (→+0xfe=1), +0x204=0x40200000 — the EXACT
        -- persistent state of a hand-placed vanilla guard. The earlier
        -- reluctance was post-spawn +0x1F4 pokes (make_combatant /
        -- maintainer) which set +0x1F4≠0 and permanently disable the
        -- lazy re-arm. Recipe: NO +0x1F4/+0x200/+0xfe writes, nothing
        -- per-frame; only one-shot stance(1,7) (+0x1F0 friend/foe).
        o, err = NPCo.spawn_template("friendly_town_guard",
          { type = ty, pos = "CPOS:HERO", sub_id = i })
        if o then
          -- one-shot friend/foe (+0x1F0=7 ally; matrix-only read) —
          -- the SOLE permitted post-spawn field delta.
          o:stance(1, 7)
          o:teleport(kx, ky)             -- engine move to the post
          -- plain WEAPON_SWORD (1729), main hand (no Vampiress BFG).
          if EQUIP_GUARDS then o:equip(1729, 0x0D) end
          -- Vanilla guards are hand-placed (never teleported); our
          -- post-spawn teleport consumes the CreateNPC-time AWAKE latch
          -- and +0x1F4=1 disables the idle lazy re-arm. WakeUp is the
          -- engine's OWN sanctioned mechanism to (re)latch +0xfe — call
          -- it ONCE, AFTER final positioning. Not a field poke, not
          -- per-frame. Preconditions hold (+0x200=0, +0xfc=0).
          o:wake()
        end
      else
        local h = sacred.spawn_at(ty, kx, ky)
        if h then o = NPCo.wrap(h):make_soldier(20, 5000) end
      end
      if o then
        SCENE_NPCS[#SCENE_NPCS + 1] = o
        SCENE_GUARDS[#SCENE_GUARDS + 1] = o   -- for the proactive maintainer
        V.set(("SDKQ_99_HG%d"):format(i), o:handle())   -- a savegame brings him back
        sacred.log(("[scene] guard #%d type=%d -> %s @(%d,%d)")
          :format(i, ty, tostring(o), kx, ky))
      else
        sacred.log(("[scene] guard #%d type=%d FAIL: %s")
          :format(i, ty, tostring(err)))
      end
    end
    -- Captain Miles — FINAL config (all sub-problems RE-solved):
    --  • Spawn via the engine's own CreateNPC from the 'quest_npc'
    --    template (it carries the {0x01,'NAME'} opcode). Pass the
    --    global.res name KEY (CAP_NAME = "res:CAPMILES_NAME") so the
    --    ENGINE writes NameArrA itself → the nameplate shows "Captain
    --    Miles" (the prior DlgNPC entry+0x04 strncpy was the wrong field;
    --    that offset is a dialog key, not the display name).
    --  • stance(1,7): ally matrix class (friendly to hero, not a target).
    --  • SOFT-immortal: CAP_SOFT → per-tick HP restore. NOT
    --    set_invulnerable — that ORs +0x14|0x200000 which is the engine
    --    ward-aura bit = the red glow (proven: bare +14=40400000 no-glow
    --    vs invuln +14=40600000 glow). HP-restore = invisible immortality.
    --  • set_stationary: holds the post.
    --  • bind_quest: DlgNPC entry + cCreature+0x245 stamp → the overhead
    --    "!" marker + a dialog slot (the name now comes from NameArrA, so
    --    bind is marker/dialog only).
    local cap, cerr = NPCo.spawn_template("quest_npc",
      { type = CAPTAIN_TYPE, pos = "CPOS:HERO", sub_id = 99,
        name = CAP_NAME })
    local hc = nil
    if cap then
      cap:save("captain_miles")
      hc = cap:handle()
      V.set("SDKQ_99_HCAP", hc)        -- save/load probe: does a savegame bring him back?
      cap:teleport(CAPTAIN_POS[1], CAPTAIN_POS[2])
      cap:stance(1, 7)                 -- ally cluster (friendly to hero)
      CAP_SOFT = cap                   -- soft-immortal via tick HP restore
      cap:set_stationary(true)         -- holds post (+0x2B7 bit)
      local named = cap:bind_quest("Captain Miles", true)  -- DlgNPC + dlg
      -- Upgrade the overhead marker "!" -> "?!" (combo glyph). RE-proven
      -- side-effect-free (quest_storyline.md "Yellow ?! marker — re-
      -- evaluated"): sets cCreature+0x200|=0x4000 (selector case 4, the
      -- deterministic "?!" — wins over entry+0x48; no BP) + entry+0x48
      -- =0x22. No yellow variant exists in-engine; this is THE "?!".
      cap:quest_icon(true)
      -- Arm his first line (R-B: replay a 0x1f Dialog record through the
      -- engine dispatcher). Shows when the player talks to him. The
      -- text key is the BARE global.res name registered above.
      cap:say("CAPMILES_GREET")
      SCENE_NPCS[#SCENE_NPCS + 1] = cap
      sacred.log(("[scene] Captain Miles h=%s @(%d,%d) (quest_npc, "
        .. "name=%s key=%s, ?!=on) -> %s"):format(tostring(hc),
        CAPTAIN_POS[1], CAPTAIN_POS[2], tostring(named),
        tostring(CAP_NAME), tostring(cap)))
      -- PATH A probe (talk_trigger.md): dump the engine trigger-NAME table
      -- 0xAAB708 once. Decides if Path A is viable: count>0 = live (we can
      -- register Talk_<name>_Dlg_<id>); count==0 = dormant -> Path B. Also
      -- reports whether our key is already present.
      if sacred.trigger_table_dump then
        local n = sacred.trigger_table_dump("Dialog:")  -- vanilla dialog/talk triggers
        sacred.log("[scene] trigger_table_dump -> count=" .. tostring(n))
      end
    else
      sacred.log("[scene] Captain Miles TEMPLATE FAIL: " .. tostring(cerr))
    end
    if not hc then
      sacred.log("[scene] Captain Miles spawn FAILED")
    end
  end)

  -- HP KEEPER: a tankiness backstop for the guards (runtime soldiers can
  -- have incomplete Balance stat-init). Top guards to 5000 HP every ~1 s.
  -- Skips the captain — CAP_SOFT owns his HP (soft-immortal at 100000);
  -- HP is +0x4d4/+0x4d8 only, never the AI fields, so it does not affect
  -- the engine-faithful proactive state.
  local hk = 0
  sacred.on_tick(function()
    if not (V.is_ready() and scene_alive()) then return end
    hk = hk + 1
    if hk < 24 then return end
    hk = 0
    for _, o in ipairs(SCENE_NPCS) do
      if o and o ~= CAP_SOFT and o.alive and o:alive() and o.set_hp then
        o:set_hp(5000)
      end
    end
  end)

  -- SDK talk wiring (talk-signal-0x200): once the captain exists, register the
  -- GENERAL talk detector npcobj o:on_talk (backed by cCreature+0x200 bit
  -- 0x400 — validated 4/4 live). One callback per talk → drive Q1 step 1.
  -- This REPLACES the dead sacred.on_trigger("DLGANS:Captain Miles") path
  -- (runtime NPCs never fire named triggers). Q1_CAPTAIN_TALK is a global
  -- defined in the Q1 block below; called lazily (exists by tick time).
  local armed_for = nil                -- the captain the offer is attached to
  sacred.on_tick(function()
    local cap = CAP_SOFT
    if not (cap and cap.alive and cap:alive()) or armed_for == cap then return end
    if not V.is_ready() then return end
    armed_for = cap                    -- a respawned captain (after a load) gets it again
    -- Q1 already past the offer (a restored savegame): restore() mutes him.
    if Q1 and ((Q1.restore or 0) >= 2 or Q1.state >= 2) then return end
    -- FALLING edge (on_talk_end): recruit + advance only AFTER the player
    -- closes the dialog ("OK"), not the instant the window opens. Fixes
    -- "companions join before OK". Re-talk is gated inside Q1_CAPTAIN_TALK
    -- (state guard) and by dialog_off() clearing the line.
    -- Own node (SDK sections): our line + Accept / Reject. Accept runs step 1;
    -- Reject just closes the window and the captain can be asked again.
    cap:dialog{ text = "CAPMILES_GREET",
      buttons = {
        { label = "res:1037", on = function()
            sacred.log("[dialog] captain: Accept -> Q1 step 1")
            if Q1_CAPTAIN_TALK then Q1_CAPTAIN_TALK() end
          end },
        { label = "res:1038", on = function()
            sacred.log("[dialog] captain: Reject (the offer stays open)")
          end },
      } }
  end)
end

-- ============================================================
-- STORYLINE — Quest 1: "Lair is gone" (MAIN, starts on spawn)
-- ============================================================
-- Runtime state machine (proximity-driven; STUB text — replace the
-- T.named strings later). Flow:
--   1. approach Captain Miles -> he + his 5 guards become companions
--   2. go to Rocheford (spawned at the captured pos) -> he joins; the
--      others stay where they are
--   3. reach point 2
--   4. reach point 3
-- Each step repoints the quest compass/map marker at the next target.
do
  local NPCo = require "npcobj"
  local T    = require "text"
  local Vb   = require "verbs"
  local Act  = require "actions"

  -- Journal strings (auto-registered into global.res at bake).
  T.named("Q1_LAIR_TITLE", "The Lair is Gone")
  T.named("Q1_LAIR_S1",    "Speak with Captain Miles near Bellevue.")
  T.named("Q1_LAIR_S2",    "Find Rocheford and hear his report.")
  T.named("Q1_LAIR_S3",    "Move to the rally point.")
  T.named("Q1_LAIR_S4",    "Advance to the lair site.")
  T.named("ROCHEFORD_NAME",  "Rocheford")
  -- Post-step lines. A bound quest NPC stays talkable (engine + vanilla both),
  -- so each one gets a short line for after its step is done — that, not
  -- muting him, is what stops the greeting from repeating.
  T.named("CAPMILES_DONE",
    "Rocheford is south-west of here. Go — my men are with you.")
  T.named("ROCHEFORD_DONE",
    "Lead on. I'm right behind you.")
  T.named("ROCHEFORD_GREET",
    "Finally. I was about to get killed myself. Let's go somewhere safer, "
    .. "and I'll explain what's going on.")

  local QID    = 99    -- HQ band (<=100): the engine's PRIMARY compass slot
                       -- (qm+0x3a4+C*8) and the story tab only honour ids <=100
                       -- (registry had only id 1 + ours in run 4)
  local CAP_P  = { 2793, 2284 }          -- Captain Miles (placed by scene)
  local ROCH_P = { 2713, 2196 }          -- captured: 'ROCHEFORD'
  local PT2    = { 2608, 2118 }          -- captured point 2
  local PT3    = { 2626, 2072 }          -- captured point 3
  -- Steps 1-2 are TALK steps: triggered by the player opening the NPC's
  -- DIALOG, never by distance (spec). The engine sacred_hash()es a
  -- global.res text key only when it renders that string, so a hook on
  -- the NPC's dialog text key = "player talked to this NPC". Steps 3-4
  -- are "go to point" — genuinely positional (proximity is correct).
  local POINT_REACH = 30                 -- arrive-at-point radius (3-4)

  local function near(p)
    local hx, hy = sacred.hero_pos()
    if not hx then return false end
    local dx, dy = hx - p[1], hy - p[2]
    return (dx * dx + dy * dy) <= (POINT_REACH * POINT_REACH)
  end

  -- Repoint the on-map quest marker + directional compass at `p`, and
  -- append the STUB journal line. All defensive (questbook needs the
  -- world loaded; the compass is the priority, journal is STUB).
  local function compass(p, logname)
    -- Q1 is a STORY quest (id 99): its big grey arrow comes from the story
    -- marker column that questbook_track writes, live-confirmed once the column
    -- address was fixed (2026-09-12). Slot 3 is left alone: that is the engine's
    -- short-lease marker, which the group compass uses.
    pcall(sacred.questbook_set_kompass, QID, p[1], p[2])
    pcall(sacred.questbook_track,       QID)     -- native: tracked slot + +0x18/+0x1c
    if logname then pcall(sacred.questbook_add_log, QID, logname) end
  end

  Q1 = Q1 or { state = 0, roch = nil }   -- GLOBAL: persists across ticks

  -- Every step change goes through here: the variable is what a savegame keeps.
  local function set_state(s)
    Q1.state = s
    V.set(Q1_VAR, s)
  end

  local function journal_idx()
    local n = sacred.questbook_count and sacred.questbook_count() or 0
    for i = 0, (n or 0) - 1 do
      if sacred.questbook_get_id(i) == QID then return i end
    end
    return nil
  end

  -- Save/load probe: is the creature at a saved handle still ours?
  local function probe_npc(tag, var, want)
    local h = V.get(var)
    if not h then return tag .. ": no handle saved" end
    local inf = sacred.npc_info(h)
    if not inf then return ("%s h=%d: no creature"):format(tag, h) end
    return ("%s h=%d: type %d%s at (%d,%d)"):format(tag, h, inf.type,
      inf.type == want and " (same type)" or " (another creature)", inf.kx, inf.ky)
  end

  -- Every ready world takes Q1 from its variable: nothing in a new game (the
  -- engine resets variables for one), the saved step after a savegame load.
  -- The log line doubles as the save/load probe.
  V.on_ready(function(loaded)
    if not loaded and Q1_SERIAL == V.world() then return end      -- the same world: keep Q1
    Q1_SERIAL = V.world()
    local was = Q1 and Q1.state
    Q1 = { state = 0, roch = nil }
    local s = V.get(Q1_VAR, 0)
    local party = sacred.hero_party and #(sacred.hero_party() or {}) or -1
    sacred.log(("[persist] ready after %s: %s = %d (Q1 was at %s), journal idx %s, party %d | %s | %s")
      :format(loaded and "a savegame load" or "a new world", Q1_VAR, s, tostring(was),
              tostring(journal_idx()), party,
              probe_npc("captain", "SDKQ_99_HCAP", CAPTAIN_TYPE),
              probe_npc("Rocheford", "SDKQ_99_HROCH", NPC.ROCHEFORD)))
    if s >= 1 then Q1.restore = s end
  end)

  -- STEP 1 (TALK): driven by the GENERAL talk signal (npcobj o:on_talk =
  -- cCreature+0x200 bit 0x400). Registered as a global so the scene block's
  -- captain:on_talk wiring can call it. State-guarded + idempotent.
  function Q1_CAPTAIN_TALK()
    if Q1.state ~= 1 then return end
    local cap = CAP_SOFT
    if not (cap and cap.alive and cap:alive()) then return end
    cap:make_companion(QID, { combat = true })     -- hireling AI: joins the hero's fights
    for _, g in ipairs(SCENE_GUARDS or {}) do
      if g and g.alive and g:alive() then g:make_companion(QID, { combat = true }) end
    end
    cap:dialog_off("CAPMILES_DONE")  -- "?!" off + post-step line
    compass(ROCH_P, "Q1_LAIR_S2")
    set_state(2)
    sacred.log("[Q1] s1: TALKED to Captain -> captain+guards companions -> Rocheford")
  end

  -- STEP 2 (TALK): same general signal on Rocheford. Registered via o:on_talk
  -- right after he spawns (state 2 branch below). State-guarded.
  function Q1_ROCHEFORD_TALK()
    if Q1.state ~= 2 then return end
    local r = Q1.roch
    if not (r and r.alive and r:alive()) then return end
    r:make_companion(QID)                  -- only Rocheford joins
    local cap = CAP_SOFT
    if cap and cap.dismiss then cap:dismiss() end
    for _, g in ipairs(SCENE_GUARDS or {}) do
      if g and g.alive and g:alive() then g:dismiss() end  -- they stay put
    end
    r:dialog_off("ROCHEFORD_DONE")    -- "?!" off + post-step line
    compass(PT2, "Q1_LAIR_S3")
    set_state(3)
    sacred.log("[Q1] s2: TALKED to Rocheford -> he joins; captain+guards dismissed -> pt2")
  end

  -- Rocheford's own node: our line + OK -> step 2. The node lives in the DLL,
  -- not in the savegame, so a Rocheford the savegame brought back gets it again.
  local function arm_rocheford(r)
    r:dialog{ text = "ROCHEFORD_GREET",
      buttons = { { label = "res:1024", on = function()
        sacred.log("[dialog] Rocheford: OK -> Q1 step 2")
        if Q1_ROCHEFORD_TALK then Q1_ROCHEFORD_TALK() end
      end } } }
  end

  -- The Rocheford a savegame brought back, if his saved handle still holds him.
  local function adopt_roch()
    local h = V.get("SDKQ_99_HROCH")
    local inf = h and sacred.npc_info(h)
    if not (inf and inf.type == NPC.ROCHEFORD) then return nil end
    local r = NPCo.wrap(h)
    r._name = "Rocheford"
    return r
  end

  -- Rebuild step s after a savegame load. The savegame brings back the
  -- variables, the journal entry, the party and the NPCs we spawned (same
  -- handles, measured 2026-09-11): the scene block adopts the captain and the
  -- guards, this adopts Rocheford and redoes only what lives in the DLL or in
  -- Lua. Whatever did not come back is spawned again.
  local function restore(s)
    local cap = CAP_SOFT
    local back = SCENE_ADOPTED
    sacred.log(("[persist] Q1: resuming at step %d (scene %s)")
      :format(s, back and "adopted" or "respawned"))
    local had = journal_idx()
    pcall(sacred.questbook_register, QID)       -- idempotent; back on the SDK's list
    if not had then                             -- the savegame had no entry: rebuild it
      pcall(sacred.questbook_set_log, QID, 0, "Q1_LAIR_TITLE", "Q1_LAIR_S1")
      for k, line in ipairs({ "Q1_LAIR_S2", "Q1_LAIR_S3", "Q1_LAIR_S4" }) do
        if s > k then pcall(sacred.questbook_add_log, QID, line) end
      end
    end
    if s >= 2 then
      if back then cap:set_talkable(false) else cap:dialog_off("CAPMILES_DONE") end
    end
    local r = (s >= 2) and adopt_roch() or nil
    if r then
      Q1.roch = r
      if SCENE_NPCS then SCENE_NPCS[#SCENE_NPCS + 1] = r end
      if s == 2 then arm_rocheford(r) end
    end
    if s == 1 then
      compass(CAP_P, nil)
    elseif s == 2 then
      if not back then                          -- respawned: make the party again
        cap:make_companion(QID, { combat = true })
        for _, g in ipairs(SCENE_GUARDS or {}) do
          if g and g.alive and g:alive() then g:make_companion(QID, { combat = true }) end
        end
      end
      compass(ROCH_P, nil)                      -- no Rocheford: the step-2 branch spawns him
    elseif s <= 4 then
      if not r then
        r = NPCo.spawn_template("quest_npc",
          { type = NPC.ROCHEFORD, pos = "CPOS:HERO", sub_id = 77, name = "res:ROCHEFORD_NAME" })
        if r then
          r:stance(1, 7)
          r:make_companion(QID)
          V.set("SDKQ_99_HROCH", r:handle())
          if SCENE_NPCS then SCENE_NPCS[#SCENE_NPCS + 1] = r end
          Q1.roch = r
        end
      end
      compass(s == 3 and PT2 or PT3, nil)
    elseif not had then                         -- done; a returned entry is solved already
      pcall(sacred.questbook_mark_solved, QID)
    end
    Q1.state = s
  end

  sacred.on_tick(function()
    -- Need the scene up: Captain Miles (CAP_SOFT) + guards (SCENE_GUARDS).
    local cap = CAP_SOFT
    if not (cap and cap.alive and cap:alive()) then return end
    if not V.is_ready() then return end
    if Q1.restore then
      local s = Q1.restore
      Q1.restore = nil
      restore(s)
      return
    end

    -- keep the compass on Rocheford while he is the target (he moves)
    Q1.t = (Q1.t or 0) + 1
    -- COMPANION TELEMETRY (every ~2 s once the party exists): AI word, state,
    -- owner, distance, and whether the hero currently has a target (+0x104).
    -- One-time registry dump at start (which quest ids <= 100 are taken).
    if Q1.t == 4 and sacred.questbook_dump then pcall(sacred.questbook_dump) end
    if Q1.state >= 2 and Q1.t % 8 == 0 and sacred.npc_ai then
      local hx, hy = sacred.hero_pos()
      local htgt = sacred.hero_party and sacred.peek_u32 and "?" or "?"
      local parts = {}
      local function one(tag, o)
        if not (o and o.alive and o:alive()) then return end
        local a, inf = sacred.npc_ai(o:handle()), o:info()
        if not (a and inf) then return end
        local d = (hx and inf.kx) and math.floor(math.sqrt((hx-inf.kx)^2 + (hy-inf.ky)^2)) or -1
        parts[#parts+1] = string.format("%s:h%d d=%d 1F4=%X 1F0=%d st=%d fc=%d fe=%d s150=%d",
          tag, o:handle(), d, a.f1f4, a.f1f0, a.state, a.fc, a.fe, a.s150)
      end
      one("CAP", CAP_SOFT)
      for i, g in ipairs(SCENE_GUARDS or {}) do if i <= 2 then one("G"..i, g) end end
      one("ROCH", Q1.roch)
      local party = sacred.hero_party and #(sacred.hero_party() or {}) or -1
      local wnd = sacred.peek_u32 and sacred.peek_u32(0x00AB7394) or -1
      sacred.log(("[q1tel] party=%d talkwnd=0x%X | %s"):format(party, wnd, table.concat(parts, " | ")))
    end
    if Q1.state == 2 and Q1.roch and Q1.roch.alive and Q1.roch:alive() and Q1.t % 8 == 0 then
      pcall(sacred.questbook_track, QID, Q1.roch:handle())
    end

    -- Rocheford walking off at the end: the engine's path-finding gives up on an
    -- obstacle and he just stands there, so nudge him -- re-issue the walk when
    -- he has not moved for ~4 s, and step him past the obstacle if even that
    -- fails. Three tries, then leave him wherever he got.
    if Q1.walk and Q1.roch and Q1.roch.alive and Q1.roch:alive() then
      Q1.walk.t = Q1.walk.t + 1
      if Q1.walk.t >= 16 then
        Q1.walk.t = 0
        local x, y = Q1.roch:pos()
        local here = ("%s,%s"):format(tostring(x), tostring(y))
        local dx, dy = (x or 0) - PT3[1], (y or 0) - PT3[2]
        if dx * dx + dy * dy <= 9 then            -- arrived: within 3 tiles
          Q1.walk = nil
          Q1.roch:set_stationary(true)            -- and he stays put there
          sacred.log("[Q1] Rocheford reached the point and stays there")
        elseif here == Q1.walk.last then          -- stuck: same tile as 4 s ago
          Q1.walk.tries = Q1.walk.tries - 1
          if Q1.walk.tries > 0 then
            Act.run(Vb.npc_goto("res:ROCHEFORD_NAME", PT3))
            sacred.log(("[Q1] Rocheford is stuck at %s, walking him again (%d left)")
              :format(here, Q1.walk.tries))
          else
            Q1.walk = nil
            Q1.roch:teleport(PT3[1], PT3[2])      -- last resort: put him there
            sacred.log("[Q1] Rocheford could not path there; placed him at the point")
          end
        else
          Q1.walk.last = here
        end
      end
    end

    if Q1.state == 0 then
      pcall(sacred.questbook_register, QID)
      pcall(sacred.questbook_set_log, QID, 0, "Q1_LAIR_TITLE", "Q1_LAIR_S1")
      compass(CAP_P, nil)
      set_state(1)
      sacred.log("[Q1] 'Lair is gone' started -> TALK to Captain Miles")

    elseif Q1.state == 2 then
      -- Spawn Rocheford once (real NPC type 323, Gladiator branch) at
      -- his captured pos, bound + dialog armed. The player walks over
      -- and TALKS; the ROCHEFORD_GREET hook does the 2->3 transition.
      if not (Q1.roch and Q1.roch.alive and Q1.roch:alive()) then
        local r = NPCo.spawn_template("quest_npc",
          { type = NPC.ROCHEFORD, pos = "CPOS:HERO", sub_id = 77,
            name = "res:ROCHEFORD_NAME" })
        if r then
          r:teleport(ROCH_P[1], ROCH_P[2])
          r:stance(1, 7)                      -- ally cluster (not hostile)
          r:set_stationary(true)
          r:bind_quest("Rocheford", true)
          r:quest_icon(true)
          pcall(sacred.questbook_track, QID, r:handle())   -- compass arrow follows HIM
          r:say("ROCHEFORD_GREET")
          arm_rocheford(r)                      -- own node, own OK -> step 2
          if SCENE_NPCS then SCENE_NPCS[#SCENE_NPCS + 1] = r end -- HP keeper
          Q1.roch = r
          V.set("SDKQ_99_HROCH", r:handle())
          sacred.log(("[Q1] Rocheford spawned @(%d,%d) h=%s")
            :format(ROCH_P[1], ROCH_P[2], tostring(r:handle())))
        end
      end

    elseif Q1.state == 3 then               -- "go to point" = positional
      if near(PT2) then
        compass(PT3, "Q1_LAIR_S4")
        set_state(4)
        sacred.log("[Q1] s3: reached point 2 -> point 3")
      end

    elseif Q1.state == 4 then
      if near(PT3) then
        pcall(sacred.questbook_mark_solved, QID)
        -- Rocheford's part is over: he leaves the party (dismiss clears the
        -- follow bits, the hireling bit and the party slot, and puts him back on
        -- a neutral stance) and then walks to the point on his own legs, through
        -- the engine's own NPC_Goto.
        local r = Q1.roch
        if r and r.alive and r:alive() then
          r:dismiss()
          r:set_stationary(false)                 -- let his own legs work again
          Act.run(Vb.npc_goto("res:ROCHEFORD_NAME", PT3))
          Q1.walk = { tries = 3, last = nil, t = 0 }   -- the engine's path-finding
          sacred.log(("[Q1] Rocheford left the party and walks to %d,%d")
            :format(PT3[1], PT3[2]))
        end
        set_state(5)
        sacred.log("[Q1] s4: reached point 3 -> quest complete (STUB)")
      end
    end
  end)
end

-- ============================================================
-- The SDK's own test scaffolding used to live here: the "Brigands at the Mill"
-- quest (batch-1 verbs, the native quest registry, item rewards, hero quest
-- bits), the batch-2 world props (chests, hotspots, map icons, drops) and the
-- 2026-09-10 follow test. All of it was live-verified and then removed; what it
-- proved is in sdk/.claude/knowledge/quests/ (MECHANICS 2.10, 2.15-2.17,
-- 2.31-2.32, VERBS_*.md) and the libraries themselves carry the recipes:
--   world.lua       chests, clickable objects, map icons
--   npc_templates   merchant / smith / trainer (the shop windows), enemies
--   npcobj.lua      Npc:icon / Npc:node -- the glyph and the dialog node
--   scene.lua       cut scenes: a queued walk and animation, and Sc.say
--   nativequest.lua a quest the engine drives (SetUpQuest/TriggerQuest/Exit)
--   verbs.lua       one builder per vanilla record, with its live caveats
--   objectives.lua  the engine's kill / pickup counters
--   actions.lua     run records now, optionally as another object
-- ============================================================

-- ============================================================
-- THE FAINT (2026-09-12, run 5) — the engine heals a fallen companion but still
-- marks him as fallen, so the SDK picks him up. When a party member is dropped in a fight, the engine calls its
-- remove-from-party routine, and that routine's FIRST act is: if the creature
-- carries bit 8 at +0x2B7 -- the flag vanilla's CreateNPC opcode 0x6b puts on
-- 421 shipped NPCs -- restore him to FULL HEALTH, then take him out of the
-- party (FUN_0054d380 @0x54d3ad calling the full heal at FUN_0053e460; the
-- release path tests the same bit at FUN_0052e590 @0x52e71a). That is the
-- "falls over and wakes up" of a vanilla story companion. In the earlier runs
-- our companion simply did not have the bit.
--
-- Run 4 measured it: the one with the flag ended at FULL health and state 9 --
-- healed, and still down. So `persona.lua` now watches its characters: the
-- moment one reads state 9 it calls the engine's own restore (`npc_revive`,
-- FUN_0053e460), and if he is still down two seconds later it puts the character
-- back in the world at the spot he fell, keeping his identity (the same persona,
-- the same saved handle). Run 5 checks that a persona cannot be lost:
--   REVIVE  a persona with the flag AND the watchdog -- he must come back
--   PLAIN   an ordinary companion                    -- he stays dead
-- Their health and state are logged four times a second, so the faint shows up
-- in the numbers even if it is over in a blink.
--
-- WHAT TO DO IN-GAME: go to Captain Miles. Two men join you and three brigands
-- come at them. Watch which one gets up. Then SAVE and LOAD: the one with the
-- flag is a `persona`, and the log will say whether he was adopted or respawned.
-- ============================================================
local FAINT_TEST = false
if FAINT_TEST then
  local NPCo = require "npcobj"
  local Vb   = require "verbs"
  local Act  = require "actions"
  local T    = require "text"
  local Pr   = require "persona"

  local CX, CY = CAPTAIN_POS[1], CAPTAIN_POS[2]
  local GROUP  = 909094

  T.named("FT_REVIVE", "Gets back up")
  T.named("FT_PLAIN",  "Stays down")
  T.named("FT_FOE",    "Brigand")
  T.named("FT_MSG",    "Faint test: watch which companion gets back up.")

  FT = FT or {}                              -- GLOBAL: survives a script reload

  -- The one with the flag is a persona, so the same run also shows persistence.
  local revive = Pr.define("faint", {
    type = NPC.VALORIAN_SOLDIER, name = "res:FT_REVIVE", template = "quest_npc",
    home = { CX - 4, CY + 2 }, immortal = true, hp = 60, companion = true,
    setup = function(o, adopted)
      o:stance(1, 7)
      o:set_level(5)
      sacred.log("[ft] persona " .. (adopted and "ADOPTED from the save" or "freshly spawned"))
    end,
  })
  Pr.watch()

  local function place()
    FT.men = {}
    local r = revive:npc()
    if r then
      r:wake()
      r:revive_on_fall(true)                 -- the engine's flag, explicitly
      r:set_level(5)
      revive:companion(true, 99)
      r:set_hp(60)
      FT.men.REVIVE = r
      sacred.log(("[ft] REVIVE h=%d in the party, 60 health, flag ON"):format(r:handle()))
    end
    local p = NPCo.spawn_template("quest_npc", { type = NPC.VALORIAN_SOLDIER,
                                                 pos = "CPOS:HERO", name = "res:FT_PLAIN" })
    if p then
      p:teleport(CX - 4, CY + 4)
      p:wake()
      p:set_level(5)
      p:stance(1, 7)
      p:make_companion(99, { combat = true })
      p:set_hp(60)
      FT.men.PLAIN = p
      sacred.log(("[ft] PLAIN h=%d in the party, 60 health, no flag"):format(p:handle()))
    end
    for i = 1, 3 do
      local e = NPCo.spawn_template("named_enemy", { type = NPC.BRIGAND, pos = "CPOS:HERO",
                                                     name = "res:FT_FOE", group = GROUP })
      if e then
        e:teleport(CX - 8, CY + i * 2)
        e:set_level(30)
        e:wake()
      end
    end
    Act.run(Vb.info("FT_MSG"))
    FT.t = 0
  end

  V.on_ready(function() FT.pending = 10 end)

  sacred.on_tick(function()
    if not V.is_ready() then return end
    if FT.pending then
      FT.pending = FT.pending - 1
      if FT.pending <= 0 then FT.pending = nil; place() end
      return
    end
    if not FT.men then return end
    local parts = {}
    for _, key in ipairs({ "REVIVE", "PLAIN" }) do
      local o = FT.men[key]
      if o then
        local a = sacred.npc_ai and sacred.npc_ai(o:handle())
        local c = sacred.peek_u32 and (function()
          local om = sacred.peek_u32(0x00AD5C40)
          local arr = om and om ~= 0 and sacred.peek_u32(om + 4)
          return arr and arr ~= 0 and sacred.peek_u32(arr + o:handle() * 4)
        end)()
        local hp, mx, lead = "?", "?", "?"
        if c and c ~= 0 then
          hp = tostring(sacred.peek_u32(c + 0x4d8))
          mx = tostring(sacred.peek_u32(c + 0x4d4))
          lead = tostring(sacred.peek_u32(c + 0x251))
        end
        parts[#parts + 1] = ("%s:%s/%s st=%s leader=%s%s"):format(key, hp, mx,
          a and tostring(a.fc) or "?", lead, o:alive() and "" or " GONE")
      end
    end
    local line = table.concat(parts, " | ")
    if line ~= FT.last then
      FT.last = line
      sacred.log("[ft] " .. line)
    end
  end)
end

-- ============================================================
-- BATCH 5 (2026-09-12): the vanilla mechanics the SDK had never run once --
-- the engine's own AREA TRIGGERS (a rectangle on the ground that runs a
-- section when you step in), trigger objects as a barrier, section flow
-- (restart / latch), ActivateQuest and the journal page, UnsetVarBit, Morph,
-- AutoSave, and the two cinema modes side by side to settle why a queued cut
-- scene never played. The whole run is custom/lua/lib/probe_b5.lua; every line
-- it logs is tagged [b5]. Set PROBE_B5 = false to switch it off.
-- ============================================================
PROBE_B5 = (PROBE_B5 == true)   -- batch 5 done 2026-09-12 (all of it live-confirmed): OFF
if PROBE_B5 then
  local ok, m = pcall(require, "probe_b5")
  if ok and type(m) == "table" then m.start()
  else sacred.log("[b5] probe module failed to load: " .. tostring(m)) end
end

-- LIVE_PROBES.md session S0 (sdk/.claude/knowledge/quests/LIVE_PROBES.md 6.1).
-- Lua-only probes; every log line is tagged [s0:*]. Off by default; set PROBE_S0 = true to re-run.
PROBE_S0 = (PROBE_S0 == true)   -- S0 ran 2026-09-11 (LIVE_S0_RESULTS.md): OFF by default; set true to re-run
if PROBE_S0 then
  local ok, err = pcall(require, "probe_s0")
  if not ok then sacred.log("[s0] probe module failed to load: " .. tostring(err)) end
end

-- S0b (LIVE_S0_RESULTS.md 1b): who truncated cCreature+0x245 (answer: the SDK's dialog_arm, fixed).
PROBE_S0B = (PROBE_S0B == true)   -- S0b done 2026-09-11 (LIVE_S0_RESULTS.md 1b): OFF; set true to re-run
if PROBE_S0B then
  local ok, err = pcall(require, "probe_s0b")
  if not ok then sacred.log("[s0b] probe module failed to load: " .. tostring(err)) end
end

return recs
