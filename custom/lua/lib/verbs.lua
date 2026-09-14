-- SacredSDK / lua / lib / verbs.lua
--
-- Vanilla verbs: record builders copied from records vanilla quests use, one per
-- FunkCode tag, with the shapes and caveats measured over the whole corpus
-- (sdk/.claude/knowledge/quests/VERBS_*.md, MECHANICS.md §2). Each returns a
-- record string. Run it now with actions.lua, or put it into a named section
-- (sections.lua S.define) that a button, a counter or a timer runs, so the
-- engine does it with no Lua in between. Dialog and objective builders live in
-- sections.lua; gold is reward.give_gold.
--
--   local Vb = require "verbs"
--   local A  = require "actions"
--   A.run(Vb.add_exp(500), Vb.gewinn(1, 1, 0))
--
-- The engine runs SDK sections with no owner creature. Several handlers fall
-- back to "the section's creature" when a record names no target, and then do
-- nothing, so these builders always name one ("hero" unless given).

local S = require "sections"
local rec = S.rec
local Vb = {}

local function u32(v) return string.pack("<I4", v & 0xFFFFFFFF) end
local function i32(v) return string.pack("<i4", v) end
local function name(s) return "\1" .. s .. "\0" end

-- ---- rewards and hero operations (VERBS_1_rewards.md) --------------------------

-- AddExp (tag 0x11): `amount` experience for `who` ("hero", or the "res:<KEY>"
-- a creature was spawned with). Exact, through the engine's addExperience,
-- level-ups included. Vanilla: `11 | 01 'hero' | 0b 100` (rg1_goodie1_sieg).
function Vb.add_exp(amount, who)
  return rec(0x11, "\0" .. name(who or "hero") .. "\11" .. i32(amount))
end

-- AddGold (tag 0x12): `amount` gold for `who` (the hero by default), with the
-- engine's own "+N" popup. Vanilla: `12 | 01 'hero' | 0b 2300` (QIS_OnEnter99).
-- A section owned by a quest pays only while that quest is set up and not done;
-- SDK sections are global, so they always pay.
function Vb.add_gold(amount, who)
  return rec(0x12, "\0" .. name(who or "hero") .. "\11" .. i32(amount))
end

-- Gewinn (tag 0x7d): vanilla's reward roll for the hero. mask bit 0 = gold (with
-- the "+N" popup), bit 1 = experience (scaled by region, difficulty and hero
-- level), bits 2..6 = one random item on the ground beside the hero (0x40 wins
-- over 0x20, 0x10, 0x08, 0x04). `level` scales the roll; `tier` 0..5 picks the
-- table. Vanilla: (2, 40, 0) experience only (exitquest6001), (4, 1, 5) an item
-- (super_kiste), (127, 30, 5) the bounty board.
function Vb.gewinn(mask, level, tier)
  return rec(0x7d, "\0\11" .. u32(mask) .. "\11" .. u32(level or 1) .. "\11" .. u32(tier or 0))
end

-- SetHP (tag 0x34): `who`'s current HP to `percent` (0..100) of the maximum.
-- Vanilla heals the hero to 100 (global_geheilt); never 0 on the hero.
function Vb.set_hp(percent, who)
  return rec(0x34, "\0" .. name(who or "hero") .. "\11" .. i32(percent))
end

-- GiveStat (tag 0x8a) / GiveSkill (tag 0x8b): `n` unspent attribute / skill
-- points for the hero. Permanent, saved with the character; the bounty board
-- gives 2 of each.
function Vb.give_stat(n)  return rec(0x8a, "\0\11" .. i32(n or 1)) end
function Vb.give_skill(n) return rec(0x8b, "\0\11" .. i32(n or 1)) end

-- EquipNPC (tag 0x07): the creature named `who` (the "res:<KEY>" given at its
-- CreateNPC) gets items of `types` (a type id or a list), made at its level. A
-- merchant files them as stock instead. Vanilla: an Ice Elf in Druid Armor
-- (QIS_OnEnter6001). The engine compares the name's text with the creature's.
function Vb.equip_npc(who, types)
  local p = { "\0", name(who) }
  for _, t in ipairs(type(types) == "table" and types or { types }) do p[#p + 1] = "\2" .. u32(t) end
  return rec(0x07, table.concat(p))
end

-- ---- world, atmosphere, timers, triggers (VERBS_3_world.md) --------------------

-- A position operand: a name ("pos_x", "CPOS:HERO") or an {x, y[, z]} KompassPos.
local function pos(op, p)
  if type(p) == "string" then return string.char(op) .. i32(-2) .. p .. "\0" end
  return string.char(op) .. i32(p[1]) .. i32(p[2]) .. i32(p[3] or 0)
end

-- SetTimer (tag 0x38): run `section` once, `minutes` game minutes from now (a
-- game minute is about 2.5 s). The engine fires it the way section_run does,
-- keeps it in the savegame, and drops it if `section` is missing then, so define
-- target sections when the script loads. The same `id` replaces a running
-- timer; SDK ids are Vb.SDK_TIMER + n (ids from 0x10000000 are the engine's
-- deadlines). Vanilla: `38 | 0b 500202 | 38 4 | 01 'timer2_5002'`.
Vb.SDK_TIMER = 0x0F000000
function Vb.set_timer(id, minutes, section)
  return rec(0x38, "\0\11" .. u32(id) .. "\56" .. u32(minutes) .. name(section))
end

-- DelTimer (tag 0x39): drop timer `id`.
function Vb.del_timer(id) return rec(0x39, "\0\11" .. u32(id)) end

-- PlaySound (tag 0x68): a sound by key ("THUNDER01" or "SOUND_FX_THUNDER01"; the
-- engine adds the prefix). An unknown key plays nothing.
function Vb.play_sound(key) return rec(0x68, "\0" .. name(key)) end

-- PlayJingle (tag 0x80): a short musical sting, e.g. "MeetValor".
function Vb.play_jingle(key) return rec(0x80, "\0" .. name(key)) end

-- PlayMusic (tag 0x7f): switch the music to `key` ("ATMO_WOOD",
-- "MusicFightGiantSpider"). Vanilla sets music_control(true) first and
-- music_control(false) when the scene ends; the pair belongs together.
function Vb.play_music(key) return rec(0x7f, "\0" .. name(key) .. "\11" .. u32(0)) end

-- SetScriptSoundControle (tag 0x81): true = the script owns the music, false =
-- hand it back (the stored track restarts).
function Vb.music_control(on) return rec(0x81, "\0\11" .. u32(on and 1 or 0)) end

-- Popup (tag 0x5f): open the talk window of DlgNPC node `node` on the hero, with
-- the text of section "Dialog:<node>". Vanilla: `5f | 01 'wegweiser_MPStart2'`.
function Vb.popup(node) return rec(0x5f, "\0" .. name(node)) end

-- DlgNPC (tag 0x28): declare a dialog node, the StartCode declaration: the walker
-- appends the 80 bytes after the flag byte to the DlgNPC table (qm+0x755C) as
-- they are, handle -1, bound to nobody. `marker` 13 draws nothing. The section
-- index stays 0: the engine finds the text by the name "Dialog:<node>" first.
-- Popup opens such a node with the hero, the way a signpost does. Every run
-- appends another entry, so declare a node once.
function Vb.declare_node(node, marker)
  assert(#node < 64, "verbs.declare_node: a node name has at most 63 characters")
  return rec(0x28, "\0" .. i32(-1) .. node .. string.rep("\0", 64 - #node)
    .. u32(0) .. u32(marker or 13) .. i32(0))
end

-- Partikel (tag 0x63), the shrine blessing on the hero: `effect` 0..5 for
-- `seconds`, with the flash, the animation and the sound. Vanilla: statue_ok.
function Vb.blessing(effect, seconds)
  return rec(0x63, "\0\11" .. u32(effect) .. "\11" .. u32(seconds))
end

-- Partikel (tag 0x63) at a creature: effect `fx` (1, 5 or 10 in vanilla) in
-- colour `argb` on `who` ("hero", or a "cpos:res:<KEY>" creature). `handle`
-- names it for later removal (inferred).
function Vb.fx_on(who, argb, fx, handle)
  return rec(0x63, "\0\11" .. i32(-1) .. "\11" .. i32(-1) .. name(who)
    .. (handle and name(handle) or "") .. "\11" .. u32(argb) .. "\11" .. u32(fx or 1))
end

-- SetBaseTrigger (tag 0x04): an area trigger `trigger` over the rectangle
-- a..b (positions: names or {x, y}). When a creature steps in, the engine runs
-- section "OMO-1<trigger>" with that creature, since no vanilla binding exists
-- for the name. Define that section; delete the trigger inside it (vanilla does)
-- if it should fire once. Names up to 58 characters.
function Vb.area_trigger(trigger, a, b)
  return rec(0x04, "\0" .. name(trigger) .. pos(0x0c, a) .. pos(0x0d, b or a))
end

-- DelBaseTrigger (tag 0x05): remove area trigger `trigger` everywhere.
function Vb.del_trigger(trigger) return rec(0x05, "\0" .. name(trigger)) end

-- CallFunktion (tag 0x16): run section `section` now, inside this one.
function Vb.call(section) return rec(0x16, "\0" .. name(section)) end

-- ---- NPCs, groups, movement (VERBS_2_npc.md) -----------------------------------
-- Creatures are named by the "res:<KEY>" they were spawned with (CreateNPC op
-- 0x01), or "HERO".

-- Teleport (tag 0x2e): move `who` (nil = the hero) to `p`, facing `facing`
-- degrees; for the hero the party comes along. CAREFUL: a creature name the
-- engine cannot resolve teleports the HERO instead, and the hero jumps one at a
-- time (10 s apart at least, never in combat). Vanilla:
-- `2e | 01 'HERO' | 04 'pos_zielwaldgott5003'`.
function Vb.teleport(who, p, facing)
  return rec(0x2e, "\0" .. (who and name(who) or "") .. (p and pos(0x04, p) or "")
    .. (facing and ("\3" .. string.pack("<I2", facing)) or ""))
end

-- NPC_Goto (tag 0x48): `who` walks to `p`, path-finding (alt = the other move
-- mode, op 0x66). Its AI takes over again on arrival; Vb.ST.anchor/stay keep it.
function Vb.npc_goto(who, p, alt)
  return rec(0x48, "\0" .. name(who) .. pos(0x04, p) .. (alt and "\102" or ""))
end

-- State ops for npc_state / group_state (SetNPCState sub-ops, vanilla shapes).
Vb.ST = {
  hostile      = "\8\18",            -- hostile side, awake
  peaceful     = "\14",
  wake         = "\18",
  invulnerable = "\161",             -- the ward glow; takes no damage
  follow       = "\10\0\0\55\0",     -- dialog off, follow the hero (re-levels, joins the party)
  unfollow     = "\57",
  no_dialog    = "\10\0\0",
  stay         = "\107" .. string.pack("<I2", 1),   -- no idle roaming
  vanish       = "\81",              -- group_state only: the whole group disappears
}
function Vb.ST.hook(section) return "\5" .. section .. "\0" end   -- run `section` when it dies (once)
function Vb.ST.group(g) return "\17" .. u32(g) end
function Vb.ST.level(l) return "\31" .. u32(l) end
function Vb.ST.dialog(node) return "\9" .. node .. "\0" end
-- Bind the NPC to the dialog node `node` (a DlgNPC name): SetNPCState op 0x09
-- switches BOTH the window it opens and the glyph over its head -- the vanilla
-- way a giver moves from its offer node to its "quest running" node. 751 records.
function Vb.ST.node(n) return "\9" .. n .. "\0" end
-- Op 0x0a with 0: unbind -- no dialog, no glyph (870 vanilla records use 0).
Vb.ST.no_node = "\10" .. string.pack("<I2", 0)
-- Op 0x4d: the creature's HOME (walk anchor, creature+0x2b9..+0x2c5), in the
-- same KompassPos units as NPC_Goto's op 04 (both convert through FUN_006224b0,
-- FUN_00461540:180-205 / FUN_0049e210:213). The idle AI (FUN_00542b20:333-360)
-- sends a creature more than ~200 world units (~3.7 tiles) from its home back
-- there, now and then; a teleport of a creature outside the party does not move
-- its home. 398 vanilla records.
function Vb.ST.anchor(x, y, level) return "\77" .. u32(x) .. u32(y) .. u32(level or 0) end

-- SetNPCState (tag 0x03): state ops (Vb.ST) for creature `who`; a name the
-- engine cannot resolve does nothing. A hook replaces the creature's earlier
-- one. Vanilla: `03 | 01 'res:17167' | 05 'tot12031'` (a death hook).
function Vb.npc_state(who, ...) return rec(0x03, "\0" .. name(who) .. table.concat({ ... })) end

-- SetGroupState (tag 0x49): state ops for every member of group `g`. Vanilla:
-- `49 | 11 10 | 08 12` (an ambush wakes), `49 | 11 5001 | 51` (the group leaves).
function Vb.group_state(g, ...) return rec(0x49, "\0\17" .. u32(g) .. table.concat({ ... })) end

-- GroupIsDead (tag 0x18): a guard. While any member of group `g` lives, the rest
-- of the section is skipped. A group that does not exist counts as dead.
-- Vanilla puts it first in the members' death-hook section; a Vb.call after it
-- runs only when the last one fell.
function Vb.group_is_dead(g) return rec(0x18, "\0\11" .. u32(g)) end

-- GroupGoto (tag 0x4a): every member of group `g` walks to `p` (teleport = true:
-- they jump there).
function Vb.group_goto(g, p, teleport)
  return rec(0x4a, "\0\17" .. u32(g) .. pos(0x04, p) .. (teleport and "\123" or ""))
end

-- DelNPC/DelOBJ (tag 0x37): remove creature or object `who`, no loot; kill = true
-- kills it instead.
function Vb.del_npc(who, kill) return rec(0x37, "\0" .. (kill and "\81" or "") .. name(who)) end

-- Attack (tag 0x5c): creature `a` attacks `t` ("HERO" or a "res:<KEY>"); it must
-- be awake and hostile first (Vb.ST.hostile).
function Vb.attack(a, t) return rec(0x5c, "\0" .. name(a) .. name(t)) end

-- SetIcon (tag 0x56): the glyph over the head of whoever is bound to the dialog
-- node `bind_name` (the name given to Npc:bind_quest, not its res:). It writes
-- ONE field -- the node entry's marker (DlgNPC +0x48) -- and grants nothing.
-- The engine's texture map (FUN_004090d0, MECHANICS 2.10):
--   0, 10 -> the "!" offer glyph      1, 11 -> the "?" hand-in glyph
--   4, 14 / 5, 15 -> two more         8, 13, 126 -> nothing is drawn
--   0x20..0x23 -> smith / trader      anything else -> the "?" glyph
-- Vanilla puts 10 on the offer node, 126 on the running node and 11 on the
-- hand-in node, which is exactly Vb.ICON.offer / none / handin below.
Vb.ICON = { offer = 10, handin = 11, none = 126, alt1 = 14, alt2 = 15,
            smith = 0x20, trade = 0x21 }
function Vb.set_icon(bind_name, marker)
  if type(marker) == "string" then
    marker = Vb.ICON[marker] or error("verbs.set_icon: unknown icon " .. marker)
  end
  return rec(0x56, "\0" .. name(bind_name) .. "\11" .. u32(marker))
end

-- ---- cut scenes (MECHANICS 6; the two shipped scenes, base:SERAPHIM) ----------
-- A scene is ONE section: SetCinemaMode, the actor commands, SetPlayMode. While
-- cinema mode is on an actor command is not executed but QUEUED on that actor
-- (creature+0x588, one 0x44-byte entry each), and the actors play their queues
-- out in order afterwards. The shipped `Daemonin_Tutorial` is exactly:
--   16 CallFunktion 'Tutorial_Trigger'
--   86 SetCinemaMode
--   48 NPC_Goto   01 'res:Anducar01' 01 'HERO' 66      -- walk to the hero
--   5a Wait       01 'res:Anducar01' 01 'res:Anducar01' 5c
--   47 NPC_TalkTo 01 'res:Anducar01' 09 'dlg_HQDEMON_Anducar01'
--   59 SetPlayMode                                     -- cinema off
-- Queued command kinds: NPC_Goto 1, Wait 2, PlayAnim 3, Attack 4, Teleport 9,
-- Partikel 0xd. scene.lua wraps all of this; use it rather than these directly,
-- and never leave a scene without its `cinema_off` -- an open cinema mode keeps
-- swallowing actor commands.
-- SetFocus (tag 0x46): the camera follows `who` ("HERO" or a "res:<KEY>").
-- The addon's intro scene opens with `46 | 01 'HERO'` right before cinema mode.
function Vb.set_focus(who) return rec(0x46, "\0" .. name(who or "HERO")) end

Vb.cinema_on  = rec(0x86, "\0")     -- SetCinemaMode: queue what follows
Vb.cinema_off = rec(0x59, "\0")     -- SetPlayMode with no name: cinema off

-- NPC_Goto to a CREATURE instead of a position (the scene form): `48 | 01 mover
-- | 01 target | 66`. Both shipped scenes use it, once each way round.
function Vb.goto_creature(who, target, alt)
  return rec(0x48, "\0" .. name(who) .. name(target) .. (alt == false and "" or "\102"))
end

-- Wait (tag 0x5a), queued on `who`: until `target` is done/there (kind 0x5c,
-- 131 of the 193 vanilla records), or for `seconds` (kind 0x5f).
function Vb.wait_for(who, target) return rec(0x5a, "\0" .. name(who) .. name(target) .. "\92") end
function Vb.wait_secs(who, seconds)
  return rec(0x5a, "\0" .. name(who) .. "\95" .. u32(seconds))
end

-- NPC_TalkTo (tag 0x47): the actor opens dialog node `node` with the hero. It
-- works ONLY inside a scene (outside cinema mode the handler queues nothing).
function Vb.talk_to(who, node) return rec(0x47, "\0" .. name(who) .. "\9" .. node .. "\0") end

-- ---- quest journal, compass, conditions, variables (VERBS_4_quest.md) ----------
-- The journal and compass verbs work on the engine's journal entries. A quest
-- the engine knows -- a shipped id, or an SDK id put in the registry with
-- nativequest.lua / sacred.quest_register -- gets its entry from the engine
-- itself on TriggerQuest; sacred.questbook_register is the SDK's stand-in for
-- ids that are in no registry.

Vb.info = S.info                   -- InfoPlayer (0x84), the on-screen message
Vb.set_var_bit = S.set_var_bit     -- SetVarBit (0x44)

-- QuestBook (tag 0x35): journal line `key` ("res:<KEY>") for quest `qid`: sub 0
-- = the title, other values = the next free body line (10 at most). The engine
-- expands +VAR(name)+ in the key when it runs.
function Vb.quest_log(qid, sub, key)
  return rec(0x35, "\0\11" .. u32(qid) .. "\11" .. u32(sub) .. name(key))
end

-- QuestKompassPos (tag 0x3f): quest `qid`'s compass to KompassPos (x, y).
-- Vanilla: `3f | 0b 3217 | 0b 2771 | 0b 99` (QIS_OnEnter99). The quest needs
-- a compass column (sacred.questbook_track); don't mix with questbook_set_marker.
function Vb.compass_pos(x, y, qid)
  return rec(0x3f, "\0\11" .. u32(x) .. "\11" .. u32(y) .. "\11" .. u32(qid))
end

-- QuestKompassPos by DefPos name ("pos_x"; CPOS names are not resolved here).
function Vb.compass_at(pos_name, qid) return rec(0x3f, "\0" .. name(pos_name) .. "\11" .. u32(qid)) end

-- QuestKompassObj (tag 0x40): quest `qid`'s compass follows object `obj`
-- ("res:<KEY>"); compass_off hides the arrow, as vanilla does on hand-in.
function Vb.compass_obj(obj, qid) return rec(0x40, "\0" .. name(obj) .. "\11" .. u32(qid)) end
function Vb.compass_off(qid) return Vb.compass_obj("off", qid) end

-- ---- the quest lifecycle (RE_lifecycle_journal.md sections 3-6) ---------------
-- All four take a registry id: a shipped quest, or an SDK id registered with
-- nativequest.lua. Vanilla shape, live example `SelfTriggerQuest15054` = the
-- 9 bytes `14 00 09 00 0b ce3a0000`.

-- SetUpQuest (tag 0x15): arm quest `qid` -- flags bit 0, QIS_OnSetUp<qid> once,
-- then QIS_Trigger<qid>. Sections owned by the quest stay dead until this runs.
function Vb.setup_quest(qid) return rec(0x15, "\0\11" .. u32(qid)) end

-- TriggerQuest (tag 0x14): run QIS_Trigger<qid>; if it runs to its end the engine
-- ENTERS the quest -- its own journal entry (category 3 for ids 1..99, else 4),
-- the compass column, the entry fanfare, then QIS_OnEnter<qid>.
function Vb.trigger_quest(qid) return rec(0x14, "\0\11" .. u32(qid)) end

-- ExitQuest (tag 0x0f) / LoseQuest (tag 0x36): quest `qid` solved / failed, with
-- the fanfare and the journal look, then QIS_OnExit<qid> / QIS_OnLose<qid>.
function Vb.exit_quest(qid) return rec(0x0f, "\0\11" .. u32(qid)) end
function Vb.lose_quest(qid) return rec(0x36, "\0\11" .. u32(qid)) end

-- Predicates for Vb.if_ (the engine's IF ops; all of them must hold).
local function u16(v) return string.pack("<I2", v & 0xFFFF) end
local function nonneg(v, what)
  -- a negative value would make the engine read the next predicate as a name
  assert(math.tointeger(v) and v >= 0, "verbs.P." .. what .. ": needs an integer >= 0")
  return v
end
Vb.P = {}
function Vb.P.var(n) return "\71" .. n .. "\0" end                        -- IsVar: exists, not 0
function Vb.P.var_eq(n, v) return "\72" .. i32(nonneg(v, "var_eq")) .. n .. "\0" end
function Vb.P.var_bit(n, b) return "\73" .. i32(nonneg(b, "var_bit")) .. n .. "\0" end
function Vb.P.var_no_bit(n, b) return "\74" .. i32(nonneg(b, "var_no_bit")) .. n .. "\0" end
function Vb.P.has_gold(g) return "\147" .. u16(g) end                     -- HasGold (65535 max)
function Vb.P.hero_bit(b) return "\155" .. u16(b) end                     -- IsQBitSet
function Vb.P.no_hero_bit(b) return "\156" .. u16(b) end                  -- IsQBitNotSet
function Vb.P.has_item(owner, item) return "\58" .. owner .. "\0" .. item .. "\0" end  -- ("HERO", "res:<KEY>")
function Vb.P.difficulty(d) return string.char(0x96 + d) end              -- IsBronze .. IsNiob

-- IF (tag 0x3a) / ELSE (0x3b) / NOP (0x3e): run the records in `thens` when all
-- `preds` hold, else the ONE record `else_rec` (default: nothing). The engine's
-- blocks do not nest. Vanilla: `3a | 4a 1 '203'`, TriggerQuest, `3b`, ExitQuest
-- (Schatzkiste203).
function Vb.if_(preds, thens, else_rec)
  for _, r in ipairs(thens) do
    local tag = r:byte(1)
    assert(tag ~= 0x3a and tag ~= 0x42 and tag ~= 0x3b, "verbs.if_: no IF or ELSE inside a then-body")
  end
  if else_rec then
    assert(#else_rec == else_rec:byte(3), "verbs.if_: the else branch is exactly one record")
  end
  return rec(0x3a, "\0" .. table.concat(preds)) .. table.concat(thens)
    .. rec(0x3b, "\0") .. (else_rec or rec(0x3e, "\0"))
end

-- SetVar (0x43) / IncVar (0x4b) / DecVar (0x4c) / RndVar (0x69): the engine's
-- variable verbs, for use inside a section such as an IF body (from Lua,
-- sacred.var_* does the same). The value -1234567 is reserved by the engine.
local function val(v)
  assert(math.tointeger(v) and v ~= -1234567, "verbs: bad variable value " .. tostring(v))
  return i32(v)
end
function Vb.set_var(n, v) return rec(0x43, "\0" .. name(n) .. "\11" .. val(v)) end
function Vb.inc_var(n, v) return rec(0x4b, "\0" .. name(n) .. "\11" .. val(v or 1)) end
function Vb.dec_var(n, v) return rec(0x4c, "\0" .. name(n) .. "\11" .. val(v or 1)) end
function Vb.rnd_var(n, lo, hi) return rec(0x69, "\0" .. name(n) .. "\11" .. val(lo) .. "\11" .. val(hi)) end

-- ---- world objects, chests, hotspots, map icons (VERBS_3_world.md, batch 2) ----
-- A world object is CreateObj's second form: a type, a position, and optionally
-- a NAME (op 0x29) that other records address it by, plus a TAKE SECTION (op
-- 0x41) the engine runs when the player opens or takes it. 1,886 of the shipped
-- objects are built exactly like this. Vanilla chest (StartCode @0x02bd51):
--   08 | 02 5209 | 04 3412 2593 1 | 03 155 | 29 'chestR1_03' | 41 'fillchest_chestR1_03'
-- `pos` is { x, y [, z] } or a position name ("pos_ziel1501", "CPOS:HERO").
function Vb.create_obj(type_id, pos, opts)
  opts = opts or {}
  local p = { "\0" }
  if opts.res then p[#p + 1] = name(opts.res) end       -- op 0x01: the by-name handle
  p[#p + 1] = "\2" .. u32(type_id)
  if type(pos) == "table" then
    p[#p + 1] = "\4" .. i32(pos[1]) .. i32(pos[2]) .. i32(pos[3] or 0)
  else
    p[#p + 1] = "\4" .. i32(-2) .. (pos or "CPOS:HERO") .. "\0"
  end
  if opts.dir then p[#p + 1] = "\3" .. string.pack("<I2", opts.dir & 0xFFFF) end
  if opts.name then p[#p + 1] = "\41" .. opts.name .. "\0" end        -- op 0x29
  if opts.take then p[#p + 1] = "\65" .. opts.take .. "\0" end        -- op 0x41
  -- op 0x8e: lay it in the world for pick-up instead of handing it over. With
  -- 0x61 that is vanilla's quest item on the ground, the DQ fetch target:
  -- `01 'res:ZIELOBJEKT_15054' 02 1722 04 'POS_ZIEL_15054' 41 'od_15054' 8e 61`.
  if opts.place then p[#p + 1] = "\142" end
  if opts.give then p[#p + 1] = "\97" end                              -- op 0x61: quest item
  return rec(0x08, table.concat(p))
end

-- SetObjState (tag 0x02): act on the named world object. `what` is "lock",
-- "unlock", "open", "close". Vanilla: `02 | 01 'chestR1_03' 00 06` (lock).
local OBJ_STATE = { lock = 6, unlock = 7, open = 15, close = 16 }
function Vb.obj_state(obj_name, what)
  local op = OBJ_STATE[what] or error("verbs.obj_state: lock|unlock|open|close, got " .. tostring(what))
  return rec(0x02, "\0" .. name(obj_name) .. string.char(op))
end
function Vb.obj_take_section(obj_name, section)          -- op 0x41: re-point the take hook
  return rec(0x02, "\0" .. name(obj_name) .. "\65" .. section .. "\0")
end

-- FillChest (tag 0x4e, MECHANICS 2.17): fill the container whose take section is
-- running. `value` is a loot BUDGET (the engine spends it on gold objects or
-- random items, and re-rolls per hero), `item` a fixed item type, `junk` a count
-- of random junk, `extra` a count of extra random items. Vanilla: `4e | 53 1000`.
-- With nothing at all the budget is hero level x 800.
function Vb.fill_chest(opts)
  opts = opts or {}
  local p = { "\0" }
  if opts.chest then p[#p + 1] = name(opts.chest) end
  if opts.value then p[#p + 1] = "\83" .. u32(opts.value) end          -- op 0x53
  if opts.item  then p[#p + 1] = "\2"  .. u32(opts.item)  end          -- op 0x02
  if opts.extra then p[#p + 1] = "\87" .. u32(opts.extra) end          -- op 0x57
  if opts.junk  then p[#p + 1] = "\134" .. u32(opts.junk) end          -- op 0x86
  return rec(0x4e, table.concat(p))
end

-- MouseEvent (tag 0x5e): a clickable hotspot -- the object of type `spec.type`
-- (or the object named `spec.res`) at `spec.pos` runs `spec.section` when it is
-- clicked. It appends a 0x58-byte entry to qm+0x370 and flags the tile. A
-- CONTAINER is not a hotspot: clicking one opens it, so give a chest or a shelf
-- a take section instead (world.lua).
-- Vanilla: `5e | 1d 0x4526 | 04 'pos_silberblueten5003' | 41 'ot_silberblueten5003' | 66`.
function Vb.mouse_event(spec)
  local p = { "\0" }
  if spec.res then p[#p + 1] = "\30" .. spec.res .. "\0"              -- op 0x1e
  else p[#p + 1] = "\29" .. u32(spec.type) end                         -- op 0x1d
  if type(spec.pos) == "table" then
    p[#p + 1] = "\4" .. i32(spec.pos[1]) .. i32(spec.pos[2]) .. i32(spec.pos[3] or 0)
  else
    p[#p + 1] = "\4" .. i32(-2) .. spec.pos .. "\0"
  end
  p[#p + 1] = "\65" .. spec.section .. "\0"                           -- op 0x41
  -- The last operand picks the hotspot kind (RE_rewards_npc_world 5.7): op 0x10
  -- = mode 1 (the plain clickable, 11,102 records, always with a `1e` name),
  -- op 0x0f = mode 2 (a hiding spot), op 0x66 = kind 1 (Region4Init's form).
  p[#p + 1] = (spec.mode == 1 and "\16") or (spec.mode == 2 and "\15") or "\102"
  return rec(0x5e, table.concat(p))
end

-- SetMapIcon (tag 0x61): an icon on the WORLD MAP at (x, y). Vanilla uses icon 1.
function Vb.map_icon(x, y, icon)
  return rec(0x61, "\0\11" .. u32(x) .. "\11" .. u32(y) .. "\11" .. u32(icon or 1))
end

-- PlayAnim (tag 0x5b): creature `who` ("res:<KEY>") plays animation `anim`.
-- Outside cinema mode this is message 0x108 to the actor, which is how the
-- vanilla dance trigger OMO-1malorka_dance runs: `5b | 01 'res:17532' | 0b 53`,
-- then 54, 55, 57, 89. Never name `schmiede`: that opens the smith window.
function Vb.play_anim(who, anim, param)
  local p = "\0" .. name(who) .. "\11" .. u32(anim)
  if param then p = p .. "\11" .. u32(param) end
  return rec(0x5b, p)
end

-- Objective counters (the builders live in sections.lua, the shapes are vanilla).
Vb.set_on_kill    = S.set_on_kill     -- SetOnKill (0x87)
Vb.set_on_collect = S.set_on_collect  -- SetOnCollect (0x88)
Vb.set_drop       = S.set_drop        -- SetDrop (0x89)

-- ---- trigger objects, section flow, journal page (batch 5) ---------------------
-- Measured shapes: every record below was copied from a live vanilla record of
-- base:VAMPIRELADY (tag dump 2026-09-12), not inferred.

-- CreateTrigger (tag 0x30): a named trigger OBJECT. This is the thing vanilla
-- puts on map cells to gate a road ("nogo_vor_arena") or to guard a chest; it is
-- NOT the area trigger (that is Vb.area_trigger). Vanilla:
-- `30 | 01 'nogo_vor_arena' | 28 0 | 27`; op 0x28 is the resource kind (0 in
-- every shipped record) and the trailing op closes it.
function Vb.create_trigger(trigger, kind)
  return rec(0x30, "\0" .. name(trigger) .. string.char(0x28, kind or 0, 0x27))
end

-- SetTriggerState (tag 0x31): arm or disarm a CreateTrigger object. Pass any of
-- "lock", "unlock", "open", "close"; the state bits are 1 = open, 0x8000 =
-- locked. Vanilla: `31 | 01 'trigger6001' | 07 26` (unlock + open).
Vb.TRIG = { lock = 0x06, unlock = 0x07, open = 0x26, close = 0x27 }
function Vb.trigger_state(trigger, ...)
  local ops = {}
  for _, w in ipairs({ ... }) do
    local op = Vb.TRIG[w]
    assert(op, "verbs: trigger state " .. tostring(w))
    ops[#ops + 1] = string.char(op)
  end
  return rec(0x31, "\0" .. name(trigger) .. table.concat(ops))
end

-- TriggerPatch (tag 0x32): bind trigger object `trigger` onto map cells (a list
-- of {x, y[, z]}). Vanilla emits one record per cell and up to 99 cells per
-- object; 18 cells fit in one record here. Vanilla:
-- `32 | 01 'nogo_vor_arena' | 2a (3281, 2698, 0)`.
function Vb.trigger_patch(trigger, cells)
  local p = { "\0", name(trigger) }
  for _, c in ipairs(cells) do
    p[#p + 1] = string.char(0x2a) .. i32(c[1]) .. i32(c[2]) .. i32(c[3] or 0)
  end
  return rec(0x32, table.concat(p))
end

-- Partikel (tag 0x63) at a WORLD POSITION: effect `fx` in colour `colour` at
-- (x, y) -- how vanilla marks the trigger areas of DQ_10251 on the ground. The
-- colour is the raw operand the engine ORs with 0xFF000000; vanilla's is
-- 0x0000ACFF. Byte-for-byte the shipped record:
-- `63 | 0b 3100 | 0b 1839 | 01 '' | 0b 0xacff | 0b 1`.
Vb.FX_MARK = 0x0000ACFF
function Vb.fx_at(x, y, colour, fx)
  return rec(0x63, "\0\11" .. i32(x) .. "\11" .. i32(y) .. name("")
    .. "\11" .. u32(colour or Vb.FX_MARK) .. "\11" .. u32(fx or 1))
end

-- ActivateQuest (tag 0x75): make `qid` the quest the journal opens on and the
-- compass follows. The id must be in the engine's registry (nativequest.lua).
-- Vanilla: `75 | 0b 30`.
function Vb.activate_quest(qid) return rec(0x75, "\0\11" .. u32(qid)) end

-- SetQuestInfo (tag 0x57): which journal PAGE (tab) the entry files under, 1..4.
-- It carries no quest id: the engine writes the entry of the section's OWNER
-- quest, so this only works from a section defined with sections.define_owned
-- and only after the quest was entered. Vanilla: `57 | 0b 1` in QIS_OnEnter99.
function Vb.quest_page(page) return rec(0x57, "\0\11" .. u32(page)) end

-- UnsetVarBit (tag 0x45): the complement of set_var_bit. With the name
-- "HeroQBit" it clears the hero's own quest bit. Vanilla: `45 | 01 'tele20.1' | 0b 1`.
function Vb.unset_var_bit(n, bit) return rec(0x45, "\0" .. name(n) .. "\11" .. u32(bit)) end

-- SectionEnd (tag 0x6f): restart THIS section from its first record (the walker
-- returns 3). Vanilla uses it in 115 records. Guard it with an IF over a
-- variable the section itself changes, or the engine loops forever.
function Vb.section_restart() return rec(0x6f, "\0") end

-- DeleteFunktionBlock (tag 0x0e): latch a section so it never runs again (its
-- table byte +0x50). Bare = latch the running section and stop it; with a name =
-- latch that other section and stop this one. An SDK section loses the latch on
-- the next inject. Vanilla never emits it (0 records), the walker handles it.
function Vb.section_latch(section)
  return rec(0x0e, "\0" .. (section and name(section) or ""))
end

-- AutoSave (tag 0x79): the game's own autosave. Vanilla: `79 | 1d 30000` -- the
-- operand is a resource id and every one of the 285 shipped records uses 30000.
function Vb.autosave(resid) return rec(0x79, "\0\29" .. u32(resid or 30000)) end

-- Morph (tag 0x4f): re-type the entity named `who` into creature type `type_id`
-- (a sheep becomes a wolf). Vanilla: `4f | 01 'res:19554' | 02 232`.
function Vb.morph(who, type_id) return rec(0x4f, "\0" .. name(who) .. "\2" .. u32(type_id)) end

-- SetAnimMode (tag 0x58): the queueing half of cinema mode -- kernel event 0x13
-- {3,1} and quest-manager flag bit 0, where SetCinemaMode (0x86) sets bits 0 AND
-- 1 and, when the hero's action queue is empty, ENDS the section it stands in
-- (walker case 0x86 returns from FUN_00475680). Empty payload, 72 records.
function Vb.anim_mode() return rec(0x58, "\0") end

return Vb
