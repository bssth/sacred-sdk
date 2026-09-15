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
-- NO_VANILLA_QUESTS (GLOBAL: StartCode.lua, baked after this file, reads it too):
-- every vanilla SetUpQuest in FunkCode.bin and StartCode.bin becomes a same-size
-- NOP (lib/novanilla.lua), so no vanilla quest is ever set up and every section a
-- quest owns stays inert. New games only. HIDE (the journal-builder hook that hid
-- vanilla quests which still ran) is off while this is tested, so the journal
-- shows what is really left.
NO_VANILLA_QUESTS = (NO_VANILLA_QUESTS ~= false)
local HIDE  = not NO_VANILLA_QUESTS
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
--   5. Rocheford leaves the party and walks to his post (Quest 2 starts here)
--   6. he stands at his post
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

  -- He got to his post (or was put there): he stays, and Quest 2 takes over.
  local function at_post(r)
    r:set_stationary(true)
    set_state(6)
    sacred.log("[Q1] Rocheford is at his post (step 6)")
    if Q2_ROCH_AT_POST then Q2_ROCH_AT_POST(r) end
  end

  -- The Rocheford a savegame brought back, if his saved handle still holds him.
  local function adopt_roch()
    local h = V.get("SDKQ_99_HROCH")
    local inf = h and sacred.npc_info(h)
    if not (inf and inf.type == NPC.ROCHEFORD) then return nil end
    local r = NPCo.wrap(h)
    r._name = "Rocheford"
    r._res = "res:ROCHEFORD_NAME"                -- what his records (NPC_Goto) name him by
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
    else
      if not had then                           -- done; a returned entry is solved already
        pcall(sacred.questbook_mark_solved, QID)
      end
      -- Steps 5-6: Rocheford belongs at his post. A save made while he was still
      -- walking puts him there now (the walk itself is Lua state and is gone).
      -- Step 7 = a save from the build where his briefing was Q1's own last step.
      local briefed = (s == 7)
      local spawned = false
      -- Quest 2 takes him away (Julius abducts him, SDKQ_98_RGONE): he stays gone.
      local gone = V.get("SDKQ_98_RGONE", 0) == 1
      if gone and r then
        r:despawn()
        r, Q1.roch = nil, nil
      end
      if not r and not gone then
        r = NPCo.spawn_template("quest_npc",
          { type = NPC.ROCHEFORD, pos = "CPOS:HERO", sub_id = 77, name = "res:ROCHEFORD_NAME" })
        if r then
          r:stance(1, 7)
          V.set("SDKQ_99_HROCH", r:handle())
          if SCENE_NPCS then SCENE_NPCS[#SCENE_NPCS + 1] = r end
          Q1.roch = r
          spawned = true
        end
      end
      if r then
        if s == 5 or spawned then r:place(PT3[1], PT3[2]) end
        r:set_stationary(true)
      end
      if s ~= 6 then s = 6; V.set(Q1_VAR, 6) end
      if Q2_RESUME then Q2_RESUME(r, briefed) end
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
    if Q1.state >= 2 and Q1.state <= 4 and Q1.t % 8 == 0 and sacred.npc_ai then
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
        -- the engine's own NPC_Goto (Npc:go nudges him past obstacles).
        local r = Q1.roch
        if r and r.alive and r:alive() then
          r:dismiss()
          r:go(PT3[1], PT3[2], at_post)
          sacred.log(("[Q1] Rocheford left the party and walks to %d,%d")
            :format(PT3[1], PT3[2]))
        end
        set_state(5)
        sacred.log("[Q1] s4: reached point 3 -> quest complete")
        -- A solved quest loses its arrow (journal state 100), so the next one
        -- starts right away: its arrow follows Rocheford to his post.
        if Q2_START then Q2_START(r) end
      end
    end
  end)
end

-- ============================================================
-- STORYLINE — Quest 2: "The Slave Trade" (MAIN, follows Q1)
-- ============================================================
-- The step lives in the engine variable SDKQ_98, so a savegame carries it:
--    1. Q1 is done: the arrow follows Rocheford to his post, where he briefs you
--    2. briefing read: a bloody axe lies on the ground at AXE_P
--    3. the axe is picked up: the hero's thoughts; Slayer waits at SLAYER_P
--    4. Slayer is paid 5,000 gold: his tip; he walks off to SLAYER_EXIT and is
--       removed there; the hero heads for PT_A
--    5. at PT_A: Captain Miles and his guards join, if any of them still stand
--    6. at PT_B: a Valorian soldier turns up; Rocheford waits at ROCH_R2
--    7. Rocheford told us "Over there!": he runs to PT_C, dagger in hand
--    8. Rocheford is at PT_C and has news
--    9. his news heard: to PT_D, where the slavers stand around peacefully
--   10. their leader answered: the whole group attacks
--   11. the leader is dead: his ring lies on the ground
--   12. the ring is taken: report to Rocheford at his post, PT_E; a soldier waits
--   13. the soldier has not seen him; Captain Miles' men, still in the party,
--       leave it and stand guard; on to CP1
--   14. at CP1: on to CP2
--   15. at CP2: on to PT_F. Near it the Gladiator Bladelok hangs around (talk to
--       him if you like), and a horse stands at HORSE_P
--   16. at PT_F (the end, for now)
-- The journal is full at step 13 (10 body lines), so 14-16 move the arrow only.
do
  local NPCo = require "npcobj"
  local T    = require "text"
  local S    = require "sections"
  local Vb   = require "verbs"
  local Act  = require "actions"
  local Sc   = require "scene"

  local QID          = 98                    -- story band; vanilla has no quest 98
  local VAR          = "SDKQ_98"
  local LAYOUT       = 2                     -- SDKQ_98_V: the step numbering above
  local AXE_TYPE     = 1722                  -- TYPE_WEAPON_AXE_WAR, vanilla's "Axe of Galadius"
  local AXE_P        = { 2510, 2395 }
  local AXE_TAKE     = "od_sdk98_axe"        -- its Take: section (vanilla's od_<id> naming)
  local SLAYER_TYPE  = NPC.SHALINOR_THE_DARK_ELF
  local SLAYER_BLADE = 1779                  -- TYPE_WEAPON_SWORD_SHADOW
  local SLAYER_P     = { 2613, 2385 }
  local SLAYER_EXIT  = { 2576, 2388 }
  local PRICE        = 5000                  -- HasGold holds 65,535 at most
  local PT_A         = { 2709, 2198 }        -- where Captain Miles was left
  local PT_B         = { 2687, 2055 }
  local SOLDIER_P    = { 2721, 2057 }
  local ROCH_R2      = { 3113, 2288 }
  local PT_C         = { 3240, 2460 }
  local ROCH_DAGGER  = 1712                  -- TYPE_WEAPON_DAGGER
  local PT_D         = { 3068, 2225 }        -- the outer edge of Hedgenton
  local GANG         = 980001                -- the slavers' group id
  local GANG_LEADER  = NPC.SLAVER
  local GANG_MEN     = {                     -- type, offset from PT_D
    { NPC.SLAVECATCHER, -3, 1 }, { NPC.SLAVECATCHER, 3, 1 },
    { NPC.BRIGAND, -2, -3 },     { NPC.BRIGAND, 2, -3 },
  }
  local LEADER_OD    = "od_sdk98_leader"     -- the leader's death hook
  local RING_TYPE    = 4864                  -- TYPE_OBJECT_RING01 (vanilla's "Signet Ring" of DQ_15062)
  local RING_TAKE    = "od_sdk98_ring"
  local PT_E         = { 2628, 2069 }        -- Rocheford's post from Q1
  local POST_SOLDIER = NPC.VALORIAN_SOLDIER
  local CP1          = { 2796, 2691 }
  local CP2          = { 3187, 2767 }
  local PT_F         = { 3228, 2767 }
  local HORSE_P      = { 3231, 2767 }
  local HORSE_TYPE   = 550                   -- Horse, as the world's own (template mount)
  -- Bladelok is the Gladiator himself: the hero class's own model, type 2.
  -- Vanilla spawns a hero model with CreateNPC 18 times (type 1, base:SERAPHIM
  -- #75919 `01 'NON_UNIQUE' 02 1 0e 04 xyz`), never type 2. If the engine refuses
  -- it, a Warrior (689, the arena's gladiator Galadius) stands in.
  local BLADELOK_TYPE     = 2
  local BLADELOK_FALLBACK = 689
  local BLADELOK_SWORD    = 1724             -- TYPE_WEAPON_SWORD_BASTARD
  local BLADELOK_HOME     = { 3226, 2763 }   -- he hangs around here: born here, so it is his home
  local BLADELOK_FACING   = 45               -- Flavius' facing
  local BLADELOK_NODE     = "sdk98_bladelok" -- his DlgNPC node (the first versions bound "Bladelok")
  local REACH        = 20                    -- arrive-at-point radius, tiles
  local NEAR_SPAWN   = 100                   -- place far NPCs once the hero is this close

  -- global.res text is latin-1: plain quotes and dashes only.
  T.named("Q2_TITLE", "The Slave Trade")
  T.named("Q2_S1",  "Speak with Rocheford.")
  T.named("Q2_S2",  "Look for leads near Silver Creek and Hedgenton.")
  T.named("Q2_S3",  "Follow the tracks east.")
  T.named("Q2_S4",  "Find the meeting spot on the outer edge of Hedgenton.")
  T.named("Q2_S6",  "Rocheford is waiting for me further east.")
  T.named("Q2_S7",  "Follow Rocheford.")
  T.named("Q2_S8",  "Rocheford went on to Bellevue. I should look into Hedgenton.")
  T.named("Q2_S10", "The slavers attacked me. Their leader must die.")
  -- The journal holds 10 body lines; S12 and S13 are the 9th and 10th.
  T.named("Q2_S12", "I took a signet ring from the slavers' leader. I should report to Rocheford.")
  T.named("Q2_S13", "Rocheford has not come back to his post.")
  T.named("Q2_POST_SOLDIER", "Valorian Soldier")
  T.named("Q2_SOLDIER_WORRY",
    "Rocheford? I thought he is with you... no, he still hasn't come back. This "
    .. "is starting to get worrisome - he's been gone too long. If we lose him "
    .. "too, we'll just collapse under the weight of all these unsolved cases.")
  T.named("BLADELOK_NAME", "Bladelok")
  -- "Ravenrock" in the draft: the English game calls it Crow's Rock (Castle), the
  -- seat of Baron DeMordrey, where vanilla's Rocheford heads after Bellevue.
  T.named("BLADELOK_TALK",
    "Rocheford? That traitorous dog took him prisoner, but I freed us both. The "
    .. "one you're looking for has gone to Crow's Rock. That's all I know.")
  T.named("Q2_AXE_NAME", "Bloody Axe")
  T.named("Q2_RING_NAME", "Slaver's Signet Ring")
  T.named("Q2_LEADER_NAME", "Slaver Chief")
  T.named("ROCHEFORD_BRIEF",
    "How to begin... All this disaster has caused a major headache for the "
    .. "kingdom. Agents like me are overwhelmed and barely have time to sleep "
    .. "or eat. That's why I sometimes have to ask for help. I didn't want to "
    .. "ask you to return to active duty, but I've run out of other options...\n\n"
    .. "I'm investigating cases related to the slave trade. So far, I haven't "
    .. "been able to find as much information as I'd like, but there are some "
    .. "leads and clues pointing to Silver Creek and Hedgenton. You can start "
    .. "there - that's where the kidnapping closest to the capital took place. "
    .. "We need any leads we can get. I'd like to give you more information, "
    .. "but the investigation is still in its very early stages. Good luck!")
  T.named("Q2_AXE_NOTE",
    "It looks like this belonged to whoever used to live here. The axe is "
    .. "covered in blood, and the tracks lead eastward.")
  T.named("SLAYER_NAME", "Slayer")
  T.named("SLAYER_OFFER",
    "That's a nice axe. Since you're carrying it, does that mean you're looking "
    .. "for someone? Well, it's not in my best interest to help you. But if you "
    .. "pay me 5,000 gold coins, I'll at least point you in the right direction...")
  T.named("SLAYER_PAID",
    "Ha. A deal with you will be more profitable than the last one. All right, "
    .. "listen: there's a criminal organization that's turned human trade into a "
    .. "system. I don't know who's at the top of the pyramid, but the ones who've "
    .. "been working with me are based in Hedgenton. The meeting spot changes all "
    .. "the time, but today they're waiting for me on the very outer edge of town. "
    .. "Or rather, they were waiting. Your gold will be enough to keep me out of "
    .. "trouble for a while, so I don't have to get my hands dirty with blood "
    .. "anymore. Goodbye, traveler.")
  T.named("SLAYER_NO_GOLD", "You need 5,000 gold coins.")
  T.named("Q2_ESCORT_JOIN", "Did you pick up the trail? Great, we'll go with you.")
  T.named("ROCHEFORD_DEVILS",
    "Are you chasing the little devils? I hope you catch up with them, because "
    .. "it looks like we're too late. They've already gone off to do something, "
    .. "and I have a feeling they're up to no good. Over there!")
  T.named("ROCHEFORD_LATE",
    "Damn, we really are too late. They've already sailed off to the monastery "
    .. "in Bellevue. I hope Serafima can handle them...\n\n"
    .. "All right. I'll continue the investigation right in Bellevue, figure out "
    .. "what kind of trouble they've caused there, and decide what to do next. "
    .. "You look into Hedgenton and try to find another lead.")
  T.named("Q2_LEADER_TALK", "What are you looking at? Do you want to come with us, too?")

  Q2 = Q2 or { state = 0 }                   -- GLOBAL: persists across ticks

  local function set_state(s)
    Q2.state = s
    V.set(VAR, s)
  end

  local function journal(line) pcall(sacred.questbook_add_log, QID, line) end

  -- The story arrow: at a spot, or following a creature (call again while it moves).
  local function point_at(p)
    pcall(sacred.questbook_set_kompass, QID, p[1], p[2])
    pcall(sacred.questbook_track, QID)
  end
  local function follow(o)
    if o and o:alive() then pcall(sacred.questbook_track, QID, o:handle()) end
  end

  local function has_entry()
    local n = sacred.questbook_count and sacred.questbook_count() or 0
    for i = 0, (n or 0) - 1 do
      if sacred.questbook_get_id(i) == QID then return true end
    end
    return false
  end

  local function dist2(p, x, y)
    if not x then return math.huge end
    return (x - p[1]) ^ 2 + (y - p[2]) ^ 2
  end
  local function hero_near(p, r) return dist2(p, sacred.hero_pos()) <= r * r end

  -- Dead the way the engine counts it (FUN_00549080): state 9, or the talk
  -- state 6. A corpse can still have an npc_info.
  local function dead(o)
    if not (o and o:alive()) then return true end
    local a = sacred.npc_ai and sacred.npc_ai(o:handle())
    return a ~= nil and (a.fc == 9 or a.s150 == 6)
  end

  local function count_items(type_id)
    local n = 0
    for _, it in ipairs(sacred.hero_items and sacred.hero_items() or {}) do
      if it.type == type_id then n = n + 1 end
    end
    return n
  end

  local function roch()
    local r = Q1 and Q1.roch
    if r and r:alive() then return r end
    return nil
  end

  local function adopt(var, want, res, name)
    local h = V.get(var)
    local inf = h and sacred.npc_info(h)
    if not (inf and inf.type == want) then return nil end
    local o = NPCo.wrap(h)
    o._res, o._name = res, name
    return o
  end

  -- Rocheford's node: `text`, one OK, and `on_ok` (only while `step` is current).
  -- dialog_off made him untalkable back in Q1, and a savegame keeps that;
  -- bind_quest re-binds his DlgNPC entry in place, which makes him talkable again.
  -- `unread` = the "?!" stays on until the player clicks OK.
  local function rocheford_says(r, text, unread, step, on_ok)
    r._name, r._res = "Rocheford", "res:ROCHEFORD_NAME"
    r:bind_quest("Rocheford", true)
    r:say(text)
    r:quest_icon(unread)                     -- after say, so the glyph is ours
    r:dialog{ text = text,
      buttons = { { label = "res:1024", on = function(o)
        sacred.log("[dialog] Rocheford: OK on " .. text)
        if on_ok and Q2.state == step then
          o:quest_icon(false)
          on_ok(o)
        end
      end } } }
  end

  -- Where Rocheford belongs while he waits. Far NPCs are moved once the hero is
  -- within NEAR_SPAWN tiles: a spawn-and-teleport has been proven over about 100.
  local function keep_rocheford_at(p) Q2.roch_at = p end

  -- ---- 1 -> 2: the briefing ------------------------------------------------------
  local function briefed()
    journal("Q2_S2")
    set_state(2)
    Q2.need_axe = true                       -- the tick places it
    point_at(AXE_P)
    sacred.log("[Q2] s1: briefing read -> the axe at " .. AXE_P[1] .. "," .. AXE_P[2])
  end

  local function arm_briefing(r, unread)
    r:set_stationary(true)
    rocheford_says(r, "ROCHEFORD_BRIEF", unread, 1, briefed)
  end

  -- ---- 2 -> 3: the axe -----------------------------------------------------------
  -- Vanilla's fetch target (DQ_15054): CreateObj with a name, the type, the spot,
  -- a Take: section, 8e (lay it on the ground) and 61 (a quest item). Picking it
  -- up runs the Take: section, which reaches Lua as SECTION:<take>. As a second
  -- signal the tick watches the hero's items for one more of that type.
  local function place_axe()
    Q2.axe_base = count_items(AXE_TYPE)
    if sacred.object_at and sacred.object_at(AXE_TYPE, AXE_P[1], AXE_P[2], 3) then
      sacred.log("[Q2] the axe is already on the ground (from the savegame)")
      return
    end
    Act.run(Vb.create_obj(AXE_TYPE, { AXE_P[1], AXE_P[2], 0 },
      { res = "res:Q2_AXE_NAME", take = AXE_TAKE, place = true, give = true }))
    sacred.log(("[Q2] the axe placed at %d,%d (hero carries %d of that type)")
      :format(AXE_P[1], AXE_P[2], Q2.axe_base))
  end

  local function found_axe(how)
    if Q2.state ~= 2 then return end
    sacred.log("[Q2] s2: the axe was picked up (" .. how .. ")")
    journal("Q2_S3")
    set_state(3)
    Sc.note("sdk_q2_axe_note", "Q2_AXE_NOTE")
    Q2.need_slayer = true                    -- the tick spawns him
  end

  S.define(AXE_TAKE)                         -- the engine's part is the pick-up itself
  sacred.on_trigger("SECTION:" .. AXE_TAKE, function() found_axe("Take: hook") end)

  -- ---- 3 -> 4: Slayer ------------------------------------------------------------
  -- He walks off and is removed on his own; the quest does not wait for that.
  -- SDKQ_98_SLG = 1 once he is gone.
  local function leave(o)
    if Q2.leaving or not (o and o:alive()) then return end
    Q2.leaving = true
    o:set_talkable(false)
    o:go(SLAYER_EXIT[1], SLAYER_EXIT[2], function(s)
      s:despawn()
      V.set("SDKQ_98_SLG", 1)
      Q2.slayer = nil
      sacred.log("[Q2] Slayer reached " .. SLAYER_EXIT[1] .. "," .. SLAYER_EXIT[2] .. " and is gone")
    end)
    sacred.log("[Q2] Slayer walks off to " .. SLAYER_EXIT[1] .. "," .. SLAYER_EXIT[2])
  end

  -- His answer becomes his node; OK on it sends him off.
  local function arm_paid(o)
    o:quest_icon(false)
    o:say("SLAYER_PAID")
    o:dialog{ text = "SLAYER_PAID",
      buttons = { { label = "res:1024", on = function(s) leave(s) end } } }
  end

  -- Paid? The Accept button's own records did it inside the engine: IF HasGold
  -- { AddGold -PRICE; SetVar SDKQ_98 = 4 } ELSE the "you need" message. This runs
  -- the heartbeat after and only reads the variable, so nothing moves without
  -- the gold having left the hero's purse.
  local function paid(o)
    if Q2.state ~= 3 then return end
    if V.get(VAR, 0) ~= 4 then
      sacred.log("[Q2] Slayer: not enough gold, nothing moves")
      return
    end
    Q2.state, Q2.paid_t = 4, 0
    sacred.log("[Q2] s3: Slayer was paid " .. PRICE .. " gold -> " .. PT_A[1] .. "," .. PT_A[2])
    arm_paid(o)
    Act.run(Sc.say("Slayer"))                -- his answer opens right away
    journal("Q2_S4")
    point_at(PT_A)
  end

  local function arm_slayer(o)
    o._res = "res:SLAYER_NAME"
    o:set_stationary(true)
    o:bind_quest("Slayer", true)
    o:say("SLAYER_OFFER")
    o:quest_icon(true)
    o:dialog{ text = "SLAYER_OFFER",
      buttons = {
        { label = "res:1037",                -- Accept: pay
          records = Vb.if_({ Vb.P.has_gold(PRICE) },
                           { Vb.add_gold(-PRICE), Vb.set_var(VAR, 4) },
                           Vb.info("SLAYER_NO_GOLD")),
          on = function(s) paid(s) end },
        { label = "res:1038" },              -- Reject: the offer stays
      } }
    Q2.slayer = o
    if SCENE_NPCS then SCENE_NPCS[#SCENE_NPCS + 1] = o end   -- HP keeper
    follow(o)
  end

  local function spawn_slayer()
    local o = NPCo.spawn_template("quest_npc",
      { type = SLAYER_TYPE, pos = "CPOS:HERO", sub_id = 98, name = "res:SLAYER_NAME" })
    if not o then
      sacred.log("[Q2] Slayer spawn FAILED")
      return nil
    end
    o:place(SLAYER_P[1], SLAYER_P[2])
    o:stance(1, 7)                           -- not hostile
    o:equip(SLAYER_BLADE, 0x0D)
    V.set("SDKQ_98_HSL", o:handle())         -- a savegame brings him back
    sacred.log(("[Q2] Slayer spawned @(%d,%d) h=%d"):format(SLAYER_P[1], SLAYER_P[2], o:handle()))
    return o
  end

  -- ---- 4 -> 5: Captain Miles and his men -----------------------------------------
  -- Whoever of them still stands joins the party, the way they did in Q1; one far
  -- away (a respawned scene is back at the post) is brought over first.
  local function escort_join()
    local hx, hy = sacred.hero_pos()
    local n = 0
    local function take(o, k)
      if dead(o) then return end
      if hx and dist2({ hx, hy }, o:pos()) > 30 * 30 then o:teleport(hx + k, hy + 2) end
      o:make_companion(QID, { combat = true })
      n = n + 1
    end
    take(CAP_SOFT, 1)
    for i, g in ipairs(SCENE_GUARDS or {}) do take(g, i + 1) end
    return n
  end

  local function at_a()
    set_state(5)
    local n = escort_join()
    if n > 0 then
      V.set("SDKQ_98_ESC", 1)
      Sc.note("sdk_q2_escort", "Q2_ESCORT_JOIN")
    end
    point_at(PT_B)
    sacred.log(("[Q2] s4: at point A, %d of Captain Miles' party joined -> %d,%d")
      :format(n, PT_B[1], PT_B[2]))
  end

  -- ---- 5 -> 6: the soldier, and Rocheford further east ------------------------------
  -- An extra: the scene guards' recipe (friendly_town_guard, ally stance, a sword,
  -- WakeUp after the move), so he fights whatever attacks. Nobody tracks him.
  local function spawn_soldier()
    local o = NPCo.spawn_template("friendly_town_guard",
      { type = NPC.VALORIAN_SOLDIER, pos = "CPOS:HERO" })
    if not o then
      sacred.log("[Q2] soldier spawn FAILED")
      return
    end
    o:stance(1, 7)
    o:teleport(SOLDIER_P[1], SOLDIER_P[2])   -- no script name, so no home: he may drift
    o:equip(1729, 0x0D)                      -- TYPE_WEAPON_SWORD
    o:wake()
    sacred.log(("[Q2] Valorian soldier h=%d @(%d,%d)"):format(o:handle(), SOLDIER_P[1], SOLDIER_P[2]))
  end

  local ran_off

  local function arm_devils(r, unread)
    r:set_stationary(true)
    rocheford_says(r, "ROCHEFORD_DEVILS", unread, 6, ran_off)
  end

  local function at_b()
    set_state(6)
    journal("Q2_S6")
    spawn_soldier()
    local r = roch()
    if r then arm_devils(r, true) end
    keep_rocheford_at(ROCH_R2)
    point_at(ROCH_R2)
    sacred.log(("[Q2] s5: at point B -> Rocheford at %d,%d"):format(ROCH_R2[1], ROCH_R2[2]))
  end

  -- ---- 6 -> 7 -> 8: Rocheford runs ahead --------------------------------------------
  local late_heard

  local function arm_late(r, unread)
    r:set_stationary(true)
    rocheford_says(r, "ROCHEFORD_LATE", unread, 8, late_heard)
  end

  local function at_c(r)
    Q2.roch_at = PT_C
    if Q2.state ~= 7 then return end
    set_state(8)
    arm_late(r, true)
    follow(r)
    sacred.log("[Q2] s7: Rocheford is at point C")
  end

  -- He walks (NPC_Goto's run flag 0x66 is unconfirmed). LIVE 2026-09-14 he did
  -- not move at all: teleported 500 tiles from his home, the idle AI held him
  -- back -- Npc:go now moves his home along. He says nothing while on his way;
  -- at_c re-binds him.
  ran_off = function(r)
    set_state(7)
    journal("Q2_S7")
    Q2.roch_at, Q2.c_wait = nil, 0
    r:set_talkable(false)
    r:go(PT_C[1], PT_C[2], at_c)
    r:equip(ROCH_DAGGER, 0x0D)
    point_at(PT_C)
    sacred.log(("[Q2] s6: Rocheford heads for %d,%d"):format(PT_C[1], PT_C[2]))
  end

  -- ---- 8 -> 9 -> 10: the slavers ---------------------------------------------------
  local fight

  late_heard = function(r)
    set_state(9)
    journal("Q2_S8")
    point_at(PT_D)
    sacred.log(("[Q2] s8: Rocheford's news heard -> %d,%d"):format(PT_D[1], PT_D[2]))
  end

  -- On his way to Bellevue Julius takes him. The player never sees it: once the
  -- hero is out of sight after his news, he is removed, and SDKQ_98_RGONE keeps
  -- Q1's restore from bringing him back after a load.
  local ROCH_OUT_OF_SIGHT = 60                -- tiles
  local function rocheford_abducted(r)
    Q2.roch_at = nil
    r:despawn()
    if Q1 then Q1.roch = nil end
    V.set("SDKQ_98_RGONE", 1)
    sacred.log("[Q2] Rocheford is gone (Julius has him)")
  end

  -- The leader's death hook drops the ring where he fell, inside the engine:
  -- vanilla's ground drop at a creature (CreateObj 04 'CPOS:res:<name>') in the
  -- fetch-target shape. The SECTION event then tells Lua.
  S.define(LEADER_OD, Vb.create_obj(RING_TYPE, "CPOS:res:Q2_LEADER_NAME",
    { res = "res:Q2_RING_NAME", take = RING_TAKE, place = true, give = true }))
  S.define(RING_TAKE)

  local function arm_leader(o, talk)
    o._res, o._name = "res:Q2_LEADER_NAME", "Slaver Chief"
    Q2.leader = o
    if talk then
      o:bind_quest("Slaver Chief", true)
      o:say("Q2_LEADER_TALK")
      o:quest_icon(true)
      o:dialog{ text = "Q2_LEADER_TALK",
        buttons = { { label = "res:1024",
          records = Vb.group_state(GANG, Vb.ST.hostile),     -- the whole gang, now
          on = function(s) fight(s) end } } }
    end
    o:state(Vb.ST.hook(LEADER_OD))           -- again after a load; keeps his node bound
  end

  -- Everyone in the gang, leader first: the handles are in SDKQ_98_HLD and
  -- SDKQ_98_HG<n>, so a savegame's gang is found again.
  local function gang()
    local list = {}
    if Q2.leader then list[1] = Q2.leader end
    for i, m in ipairs(GANG_MEN) do
      local o = adopt(("SDKQ_98_HG%d"):format(i), m[1], nil, nil)
      if o then list[#list + 1] = o end
    end
    return list
  end

  -- The faction change. Until the leader has spoken they are spawned as
  -- friendly as Rocheford is (ally stance 7: no attack cursor on them, the
  -- escort leaves them alone, the leader can be talked to) and hold their spot.
  -- His OK button runs vanilla's ambush record inside the engine (SetGroupState
  -- `08 12`: hostile side, awake), and this puts every member on the monster
  -- class of the friend/foe matrix (2) with a WakeUp, so they go for the hero
  -- at once.
  local function turn_hostile()
    local n = 0
    for _, o in ipairs(gang()) do
      if not dead(o) then
        o:set_stationary(false)
        o:set_disposition("hostile")
        n = n + 1
      end
    end
    return n
  end

  local function spawn_gang()
    Q2.gang = true
    local leader = NPCo.spawn_template("dormant_group",
      { type = GANG_LEADER, pos = "CPOS:HERO", name = "res:Q2_LEADER_NAME",
        group = GANG, hook = LEADER_OD })
    if not leader then
      sacred.log("[Q2] slaver chief spawn FAILED")
      Q2.gang = nil
      return
    end
    leader:place(PT_D[1], PT_D[2])
    leader:stance(1, 7)
    leader:set_stationary(true)
    V.set("SDKQ_98_HLD", leader:handle())
    arm_leader(leader, true)
    for i, m in ipairs(GANG_MEN) do
      local o = NPCo.spawn_template("dormant_group", { type = m[1], pos = "CPOS:HERO", group = GANG })
      if o then
        o:teleport(PT_D[1] + m[2], PT_D[2] + m[3])
        o:stance(1, 7)
        o:set_stationary(true)
        V.set(("SDKQ_98_HG%d"):format(i), o:handle())
      end
    end
    follow(leader)
    sacred.log(("[Q2] the slavers wait at %d,%d (group %d, chief h=%d)")
      :format(PT_D[1], PT_D[2], GANG, leader:handle()))
  end

  fight = function(o)
    if Q2.state ~= 9 then return end
    set_state(10)
    journal("Q2_S10")
    o:dialog_off()
    local n = turn_hostile()
    follow(o)
    sacred.log(("[Q2] s9: the slaver chief answered -> %d of the gang turn hostile"):format(n))
  end

  -- ---- 10 -> 11 -> 12: the ring ----------------------------------------------------
  local function leader_died(how)
    if Q2.state ~= 9 and Q2.state ~= 10 then return end
    local o = Q2.leader
    local x, y
    if o then x, y = o:pos() end             -- (`o and o:pos()` would keep only x)
    if not x then x, y = sacred.hero_pos() end
    V.set("SDKQ_98_RX", x or PT_D[1])
    V.set("SDKQ_98_RY", y or PT_D[2])
    Q2.ring_base = count_items(RING_TYPE)
    if how ~= "hook" then                    -- no hook ran: drop the ring from here
      Act.run(Vb.create_obj(RING_TYPE, { x or PT_D[1], y or PT_D[2], 0 },
        { res = "res:Q2_RING_NAME", take = RING_TAKE, place = true, give = true }))
    end
    set_state(11)
    point_at({ x or PT_D[1], y or PT_D[2] })
    sacred.log(("[Q2] s10: the slaver chief is dead (%s), the ring at %s,%s")
      :format(how, tostring(x), tostring(y)))
  end

  local function got_ring(how)
    if Q2.state ~= 11 then return end
    set_state(12)
    journal("Q2_S12")
    point_at(PT_E)
    sacred.log(("[Q2] s11: the ring was picked up (%s) -> report at %d,%d"):format(how, PT_E[1], PT_E[2]))
  end

  -- ---- 12 -> 13: nobody at Rocheford's post ------------------------------------------
  local function in_party(o)
    for _, e in ipairs(sacred.hero_party and sacred.hero_party() or {}) do
      if e.h == o:handle() then return true end
    end
    return false
  end

  -- Captain Miles and his guards, if they still follow the hero: out of the party,
  -- and back to what they were at the start, sentries (ally stance, holding the
  -- spot, awake so they take on what comes). SDKQ_98_ESC = 2: they do not rejoin.
  local function escort_to_posts()
    local n = 0
    local function post(o)
      if dead(o) or not in_party(o) then return end
      o:dismiss()
      o:stance(1, 7)
      o:set_stationary(true)
      o:wake()
      n = n + 1
    end
    post(CAP_SOFT)
    for _, g in ipairs(SCENE_GUARDS or {}) do post(g) end
    if V.get("SDKQ_98_ESC", 0) == 1 then V.set("SDKQ_98_ESC", 2) end
    return n
  end

  local function soldier_spoke()
    set_state(13)
    journal("Q2_S13")
    local n = escort_to_posts()
    point_at(CP1)
    sacred.log(("[Q2] s12: the soldier has not seen Rocheford; %d of the escort stand guard now -> %d,%d")
      :format(n, CP1[1], CP1[2]))
  end

  -- ---- 13 -> 16: the road east, Bladelok and the horse -------------------------------
  -- Bladelok hangs around BLADELOK_HOME, fights what comes near and can be talked
  -- to. He is made exactly like vanilla's Sergeant Flavius (StartCode #13319), who
  -- stands 5 tiles from here: template talk_guard, one CreateNPC record born at the
  -- post with the guard mode 46, side off 0e, 6b 1 and his dialog node bound by op
  -- 09 -- and nothing after it. Versions 1 (a quest_npc, 0e: stood and watched the
  -- hero) and 2 (08 12 + stance 7 + home record + wake + bind_quest: a hostile-born
  -- fighter with no AI mode, a shape vanilla never makes talkable) are replaced.
  -- Talking to him changes nothing in the quest.
  local BLADELOK_LAYOUT = 3                  -- SDKQ_98_BLV: 3 = Flavius' shape

  local function arm_bladelok(o)
    o._res, o._name = "res:BLADELOK_NAME", BLADELOK_NODE
    o:dialog{ text = "BLADELOK_TALK",
      buttons = { { label = "res:1024", on = function()
        sacred.log("[Q2] Bladelok told us about Crow's Rock")
      end } } }
    Q2.bladelok = o
    Q2.bladelok_probe = 0
  end

  -- The engine clears the AI mode of some creature types on their first ticks
  -- (FUN_004ee030, FUN_004266f0); Bladelok is a hero model (type 2), so read
  -- whether the guard bit 0x4000 is still there a few seconds after he appears.
  local function probe_bladelok()
    local o = Q2.bladelok
    local a = o and sacred.npc_ai and sacred.npc_ai(o:handle())
    if not a then return end
    local x, y = o:pos()
    sacred.log(("[Q2] Bladelok h=%d AI: +0x1F4=%08X (guard 0x4000 %s) +0x1F0=%d state=%d at %s,%s")
      :format(o:handle(), a.f1f4, (a.f1f4 & 0x4000) ~= 0 and "on" or "GONE", a.f1f0,
              a.state, tostring(x), tostring(y)))
  end

  -- Returns the Npc, or nil while his node is still being declared (Q2.bladelok_wait:
  -- the tick calls again).
  local function bladelok_up()
    Q2.bladelok_wait = nil
    local o = adopt("SDKQ_98_HBL", BLADELOK_TYPE, "res:BLADELOK_NAME", "Bladelok")
           or adopt("SDKQ_98_HBL", BLADELOK_FALLBACK, "res:BLADELOK_NAME", "Bladelok")
    local layout = V.get("SDKQ_98_BLV", 1)
    if o and layout < BLADELOK_LAYOUT then
      sacred.log(("[Q2] Bladelok of version %d: replaced by Flavius' shape"):format(layout))
      o:despawn()
      V.set("SDKQ_98_HBL", 0)                -- a call while the node waits must not adopt him again
      o = nil
    end
    if not NPCo.ensure_node(BLADELOK_NODE) then
      Q2.bladelok_wait = true
      return nil
    end
    if o then
      -- A savegame brings him back; bind the node again the vanilla way
      -- (SetNPCState 09) in case the loaded table lost the binding.
      Act.run(Vb.npc_state("res:BLADELOK_NAME", Vb.ST.node(BLADELOK_NODE)))
      arm_bladelok(o)
      return o
    end
    for _, kind in ipairs({ BLADELOK_TYPE, BLADELOK_FALLBACK }) do
      o = NPCo.spawn_template("talk_guard",
        { type = kind, pos = { BLADELOK_HOME[1], BLADELOK_HOME[2], 0 }, name = "res:BLADELOK_NAME",
          weapon = BLADELOK_SWORD, facing = BLADELOK_FACING, link = BLADELOK_NODE })
      if o then
        V.set("SDKQ_98_HBL", o:handle())
        V.set("SDKQ_98_BLV", BLADELOK_LAYOUT)
        sacred.log(("[Q2] Bladelok h=%d (type %d) born at %d,%d, Flavius' shape")
          :format(o:handle(), kind, BLADELOK_HOME[1], BLADELOK_HOME[2]))
        arm_bladelok(o)
        return o
      end
    end
    sacred.log("[Q2] Bladelok spawn FAILED (types 2 and 689)")
    return nil
  end

  local function spawn_road_extras()
    Q2.road = true
    bladelok_up()

    local h = adopt("SDKQ_98_HHO", HORSE_TYPE, nil, nil)
    if not h then
      h = NPCo.spawn_template("mount", { type = HORSE_TYPE, pos = "CPOS:HERO" })
      if h then
        h:teleport(HORSE_P[1], HORSE_P[2])
        h:set_stationary(true)
        V.set("SDKQ_98_HHO", h:handle())
        sacred.log(("[Q2] a horse h=%d @(%d,%d)"):format(h:handle(), HORSE_P[1], HORSE_P[2]))
      else
        sacred.log("[Q2] horse spawn FAILED")
      end
    end
  end

  local function arm_post_soldier(o, unread)
    o._res, o._name = "res:Q2_POST_SOLDIER", "Valorian Soldier"
    o:set_stationary(true)
    o:bind_quest("Valorian Soldier", true)
    o:say("Q2_SOLDIER_WORRY")
    o:quest_icon(unread)
    o:dialog{ text = "Q2_SOLDIER_WORRY",
      buttons = { { label = "res:1024", on = function(s)
        if Q2.state ~= 12 then return end
        s:quest_icon(false)
        soldier_spoke()
      end } } }
    Q2.post_soldier = o
    if unread then follow(o) end
  end

  local function spawn_post_soldier()
    local o = NPCo.spawn_template("quest_npc",
      { type = POST_SOLDIER, pos = "CPOS:HERO", name = "res:Q2_POST_SOLDIER" })
    if not o then
      sacred.log("[Q2] post soldier spawn FAILED")
      return
    end
    o:place(PT_E[1], PT_E[2])
    o:stance(1, 7)
    o:equip(1729, 0x0D)                      -- TYPE_WEAPON_SWORD
    V.set("SDKQ_98_HPS", o:handle())
    arm_post_soldier(o, true)
    sacred.log(("[Q2] the post soldier h=%d @(%d,%d)"):format(o:handle(), PT_E[1], PT_E[2]))
  end

  sacred.on_trigger("SECTION:" .. LEADER_OD, function() leader_died("hook") end)
  sacred.on_trigger("SECTION:" .. RING_TAKE, function() got_ring("Take: hook") end)

  -- ---- the hand-over from Q1 --------------------------------------------------
  function Q2_START(r)
    if Q2.state ~= 0 then return end
    pcall(sacred.questbook_register, QID)
    pcall(sacred.questbook_set_log, QID, 0, "Q2_TITLE", "Q2_S1")
    V.set("SDKQ_98_V", LAYOUT)
    set_state(1)
    follow(r)
    sacred.log("[Q2] 'The Slave Trade' started -> Rocheford")
  end

  function Q2_ROCH_AT_POST(r)
    if Q2.state ~= 1 then return end
    arm_briefing(r, true)
    follow(r)
  end

  -- After a savegame load (Q1's restore calls this once Rocheford is back at his
  -- post). `briefed_before`: a save from the build where the briefing was still
  -- Q1's last step and had been read.
  local JOURNAL = { { 2, "Q2_S2" }, { 3, "Q2_S3" }, { 4, "Q2_S4" }, { 6, "Q2_S6" },
                    { 7, "Q2_S7" }, { 9, "Q2_S8" }, { 10, "Q2_S10" }, { 12, "Q2_S12" },
                    { 13, "Q2_S13" } }

  function Q2_RESUME(r, briefed_before)
    local s = V.get(VAR, 0)
    if s == 0 then s = briefed_before and 2 or 1 end
    if V.get("SDKQ_98_V", 1) < LAYOUT and s == 5 then     -- the old 5 = "Slayer gone"
      s = 4
      V.set("SDKQ_98_SLG", 1)
    end
    V.set("SDKQ_98_V", LAYOUT)
    V.set(VAR, s)
    Q2.state = s
    sacred.log(("[persist] Q2: resuming at step %d"):format(s))
    pcall(sacred.questbook_register, QID)
    if not has_entry() then                  -- the savegame had no entry: rebuild it
      pcall(sacred.questbook_set_log, QID, 0, "Q2_TITLE", "Q2_S1")
      for _, j in ipairs(JOURNAL) do
        if s >= j[1] then journal(j[2]) end
      end
    end

    -- Rocheford: the node of the step, and the spot he belongs on.
    if r then
      if s <= 5 then arm_briefing(r, s == 1)
      elseif s == 6 then arm_devils(r, true); keep_rocheford_at(ROCH_R2)
      else
        if s == 7 then s = 8; set_state(8) end                -- a run is Lua state: he is there
        arm_late(r, s == 8)
        keep_rocheford_at(PT_C)
      end
    end

    -- Slayer: still leaving, or long gone.
    if s == 3 then
      local o = adopt("SDKQ_98_HSL", SLAYER_TYPE, "res:SLAYER_NAME", "Slayer")
      if o then arm_slayer(o) else Q2.need_slayer = true end
    elseif s >= 4 and V.get("SDKQ_98_SLG", 0) == 0 then
      local o = adopt("SDKQ_98_HSL", SLAYER_TYPE, "res:SLAYER_NAME", "Slayer")
      if o then
        o:bind_quest("Slayer", false)
        arm_paid(o)
        Q2.slayer = o
        if SCENE_NPCS then SCENE_NPCS[#SCENE_NPCS + 1] = o end
        leave(o)
      else
        V.set("SDKQ_98_SLG", 1)
      end
    end

    -- The escort a respawned scene lost (the savegame keeps an adopted party).
    if s >= 5 and V.get("SDKQ_98_ESC", 0) == 1 and not SCENE_ADOPTED then escort_join() end

    -- The slavers.
    if s == 9 or s == 10 then
      local o = adopt("SDKQ_98_HLD", GANG_LEADER, "res:Q2_LEADER_NAME", "Slaver Chief")
      if o and not dead(o) then
        Q2.gang = true
        arm_leader(o, s == 9)
        if s == 10 then
          Act.run(Vb.group_state(GANG, Vb.ST.hostile))
          turn_hostile()
        end
      elseif s == 10 or o then
        Q2.leader = o
        leader_died("resume")
        s = Q2.state
      end
    elseif s == 11 then
      local x, y = V.get("SDKQ_98_RX", PT_D[1]), V.get("SDKQ_98_RY", PT_D[2])
      Q2.ring_base = count_items(RING_TYPE)
      if not (sacred.object_at and sacred.object_at(RING_TYPE, x, y, 4)) and Q2.ring_base == 0 then
        Act.run(Vb.create_obj(RING_TYPE, { x, y, 0 },
          { res = "res:Q2_RING_NAME", take = RING_TAKE, place = true, give = true }))
      end
    elseif s >= 12 then                      -- the soldier at the post (spawned when near)
      local o = adopt("SDKQ_98_HPS", POST_SOLDIER, "res:Q2_POST_SOLDIER", "Valorian Soldier")
      if o then arm_post_soldier(o, s == 12) end
      -- Bladelok and the horse, if the savegame has them (else the tick places them).
      if V.get("SDKQ_98_HBL") then spawn_road_extras() end
      if s >= 16 then Act.run(Vb.compass_off(QID)) end
    end

    -- The arrow.
    local ARROW = { [2] = AXE_P, [4] = PT_A, [5] = PT_B, [6] = ROCH_R2, [8] = PT_C, [9] = PT_D,
                    [12] = PT_E, [13] = CP1, [14] = CP2, [15] = PT_F }
    if s == 1 then follow(r)
    elseif s == 2 then Q2.need_axe = true; point_at(AXE_P)
    elseif s == 3 then follow(Q2.slayer)
    elseif s == 10 then follow(Q2.leader)
    elseif s == 11 then point_at({ V.get("SDKQ_98_RX", PT_D[1]), V.get("SDKQ_98_RY", PT_D[2]) })
    elseif ARROW[s] then point_at(ARROW[s]) end
  end

  V.on_ready(function(loaded)
    if not loaded and Q2_SERIAL == V.world() then return end   -- the same world: keep Q2
    Q2_SERIAL = V.world()
    Q2 = { state = 0 }                        -- a savegame's step comes back through Q2_RESUME
  end)

  sacred.on_tick(function()
    if not V.is_ready() or Q2.state == 0 then return end
    if Q2.need_axe then
      Q2.need_axe = nil
      place_axe()
      return
    end
    if Q2.need_slayer then
      Q2.need_slayer = nil
      local o = spawn_slayer()
      if o then arm_slayer(o) end
      return
    end
    Q2.t = (Q2.t or 0) + 1
    if Q2.t % 8 ~= 0 then return end         -- every ~2 s
    local s = Q2.state

    -- Bladelok: spawn him once his node is declared; read his AI word at once and after ~6 s.
    if Q2.bladelok_wait then bladelok_up() end
    if Q2.bladelok_probe then
      if Q2.bladelok_probe == 0 or Q2.bladelok_probe == 3 then probe_bladelok() end
      Q2.bladelok_probe = Q2.bladelok_probe < 3 and Q2.bladelok_probe + 1 or nil
    end

    -- Slayer's answer: its OK starts his walk; a window closed some other way
    -- does not, so he goes by himself after a minute.
    if s >= 4 and Q2.slayer and not Q2.leaving then
      Q2.paid_t = (Q2.paid_t or 0) + 1
      if Q2.paid_t >= 30 then leave(Q2.slayer) end
    end

    -- Rocheford onto the spot he waits on, once the hero is close enough.
    local r = roch()
    if r and s >= 9 then
      local rx, ry = r:pos()
      if rx and not hero_near({ rx, ry }, ROCH_OUT_OF_SIGHT) then
        rocheford_abducted(r)
        r = nil
      end
    end
    if r and Q2.roch_at and not r:walking() and hero_near(Q2.roch_at, NEAR_SPAWN)
       and dist2(Q2.roch_at, r:pos()) > 6 * 6 then
      -- A plain teleport, as in the run where talking to him there worked (LIVE
      -- 2026-09-14: after place() he did not answer a click). He stands still
      -- there anyway; Npc:go gives him his new home when he sets off.
      r:teleport(Q2.roch_at[1], Q2.roch_at[2])
      sacred.log(("[Q2] Rocheford put on %d,%d"):format(Q2.roch_at[1], Q2.roch_at[2]))
    end

    if s == 1 then
      follow(r)                              -- he walks to his post
    elseif s == 2 then
      if count_items(AXE_TYPE) > (Q2.axe_base or 0) then found_axe("hero's items") end
    elseif s == 4 then
      if hero_near(PT_A, REACH) then at_a() end
    elseif s == 5 then
      if hero_near(PT_B, REACH) then at_b() end
    elseif s == 7 then
      -- He runs ahead. Once the hero is at the point, give him ~10 s more, then
      -- put him there whatever held him up.
      if r and hero_near(PT_C, REACH) then
        Q2.c_wait = (Q2.c_wait or 0) + 1
        if Q2.c_wait >= 5 and not r:arrive() then
          r:place(PT_C[1], PT_C[2])
          at_c(r)
        end
      end
    elseif s == 9 then
      if not Q2.gang and hero_near(PT_D, NEAR_SPAWN) then spawn_gang() end
      if Q2.gang and dead(Q2.leader) then    -- attacked before he was spoken to
        Q2.dead_t = (Q2.dead_t or 0) + 1     -- twice in a row: the hook's event may be on its way
        if Q2.dead_t >= 2 then leader_died("seen dead") end
      end
    elseif s == 10 then
      if dead(Q2.leader) then
        Q2.dead_t = (Q2.dead_t or 0) + 1
        if Q2.dead_t >= 2 then leader_died("seen dead") end
      end
    elseif s == 11 then
      if count_items(RING_TYPE) > (Q2.ring_base or 0) then got_ring("hero's items") end
    elseif s == 12 then
      if not Q2.post_soldier and hero_near(PT_E, NEAR_SPAWN) then
        Q2.post_soldier = true                -- once; arm_post_soldier puts the Npc here
        spawn_post_soldier()
      end
    elseif s == 13 then
      if hero_near(CP1, REACH) then
        set_state(14)
        point_at(CP2)
        sacred.log(("[Q2] s13: at CP1 -> %d,%d"):format(CP2[1], CP2[2]))
      end
    elseif s == 14 or s == 15 then
      if not Q2.road and hero_near(CP2, NEAR_SPAWN) then spawn_road_extras() end
      if s == 14 and hero_near(CP2, REACH) then
        set_state(15)
        point_at(PT_F)
        sacred.log(("[Q2] s14: at CP2 -> %d,%d"):format(PT_F[1], PT_F[2]))
      elseif s == 15 and hero_near(PT_F, REACH) then
        set_state(16)
        Act.run(Vb.compass_off(QID))
        sacred.log("[Q2] s15: at the horse -- the end for now")
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

if NO_VANILLA_QUESTS then
  local NV = require "novanilla"
  NV.strip(recs, "FunkCode")
  NV.keep_markers_hidden()               -- the givers of stripped quests keep their "!" otherwise
end

return recs
