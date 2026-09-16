-- SacredSDK / lua / lib / novanilla.lua
--
-- Turn the vanilla quests off at bake time, the way the engine already treats a
-- quest nobody set up (vanilla 9513 and 9515 in single-player).
--
-- A quest exists only after a SetUpQuest record (tag 0x15) ran for it, and
-- WorkFunktion (FUN_0046ba90:69-83) refuses every section a quest owns until then:
-- its givers' dialogs, buttons, area triggers, death hooks, QIS start and finish
-- blocks. So every SetUpQuest in FunkCode.bin AND StartCode.bin becomes a NOP
-- (tag 0x3e, no walker case) of the same size. Nothing moves: the Vectoren.bin
-- sections are absolute byte ranges, which is why deleting records blacked out the
-- new-game intro before. 694 records in base:VAMPIRELADY (557 FunkCode + 137
-- StartCode), all 9 bytes. Evidence and side effects:
-- sdk/.claude/knowledge/quests/DISABLE_VANILLA.md.
--
--   local NV = require "novanilla"
--   NV.strip(recs, "FunkCode")                       -- in bin/<class>/FunkCode.lua
--   NV.strip(recs, "StartCode", { [210] = true })    -- keep quest 210 alive
--
-- Takes effect in a NEW game only: a savegame brings its quests' set-up bits back.
-- Our own quests are set up by SDK sections at run time and are not touched.

local M = {}

M.SETUPQUEST = 0x15
M.NOP        = 0x3E

-- The quest id of a SetUpQuest record: its first numeric operand.
local function quest_id(rec)
  for i = 3, #rec do
    local op = rec[i]
    if type(op) == "table" then
      for k = 2, #op do
        if type(op[k]) == "number" then return op[k] end
      end
    end
  end
  return nil
end

-- Neutralise every SetUpQuest in `recs` whose quest id is not in `keep` (a set).
-- Returns recs, the number stripped and the number kept.
function M.strip(recs, label, keep)
  local stripped, kept = 0, 0
  for _, rec in ipairs(recs) do
    if rec[1] == M.SETUPQUEST then
      local qid = quest_id(rec)
      if keep and qid and keep[qid] then
        kept = kept + 1
      else
        rec[1] = M.NOP
        stripped = stripped + 1
      end
    end
  end
  if sacred and sacred.log then
    sacred.log(("[novanilla] %s: %d SetUpQuest records -> NOP, %d kept"):format(label or "?", stripped, kept))
  end
  return recs, stripped, kept
end

-- ── Glyphs over givers whose quest is off ───────────────────────────────────
-- A giver's "!" / "?" is its DlgNPC node's marker (+0x48), not a quest state, so a
-- stripped quest leaves the glyph over an NPC that no longer talks (LIVE
-- 2026-09-16). hide_dead_markers() gives every node whose Dialog: section belongs
-- to a vanilla quest that is not set up the marker 126 (vanilla's "running node",
-- draws nothing) through the vanilla SetIcon record (tag 0x56, `01 <node> 0b 126`).
-- Nodes of global sections (signposts, the bounty officer) and of SDK quests keep
-- theirs. Idempotent; call it when a world is ready.
local SEC_B, SEC_E, SEC_STRIDE = 0x00AAB708, 0x00AAB70C, 0x54   -- name[64], +0x48 owner
local DLG_B, DLG_E, DLG_STRIDE = 0x00AACF80 + 0x755C, 0x00AACF80 + 0x7560, 0x50
local DIAL, OG = 0x6C616944, 0x003A676F                         -- "Dial", "og:" + NUL low byte
local SILENT = { [8] = true, [13] = true, [126] = true }

local function cstr(peek, p, max)
  local out = {}
  for i = 0, (max or 64) - 1, 4 do
    local d = peek(p + i)
    if not d then break end
    for k = 0, 3 do
      local c = (d >> (8 * k)) & 0xFF
      if c == 0 then return table.concat(out) end
      out[#out + 1] = string.char(c)
    end
  end
  return table.concat(out)
end

-- node name (lower case) -> owner quest, for every quest-owned Dialog: section;
-- built once per world (the section table only changes when a world is built).
local owned_cache, owned_world, sent = nil, nil, {}
local function owned_nodes(peek)
  local world = require("vars").world()
  if owned_cache and owned_world == world then return owned_cache end
  local b, e = peek(SEC_B), peek(SEC_E)
  if not b or not e or e < b then return nil end
  local owned = {}
  for p = b, e - SEC_STRIDE, SEC_STRIDE do
    local owner = peek(p + 0x48)
    if owner and owner > 0 and owner < 0x80000000 and peek(p) == DIAL
       and ((peek(p + 4) or 0) & 0xFFFFFF) == (OG & 0xFFFFFF) then
      owned[cstr(peek, p, 64):sub(8):lower()] = owner   -- the name after "Dialog:"
    end
  end
  owned_cache, owned_world, sent = owned, world, {}
  return owned
end

function M.hide_dead_markers()
  local peek = sacred.peek_u32
  if not (peek and sacred.quest_state) then return 0 end
  local owned = owned_nodes(peek)
  if not owned then return 0 end
  local dead = {}
  local function is_dead(qid)                    -- not set up, and not ours
    if dead[qid] == nil then
      local q = sacred.quest_state(qid)
      dead[qid] = not (q and (q.setup or q.sdk))
    end
    return dead[qid]
  end
  local db, de = peek(DLG_B), peek(DLG_E)
  if not db or not de or de < db then return 0 end
  local Vb, recs = require "verbs", {}
  for p = db + DLG_STRIDE, de - DLG_STRIDE, DLG_STRIDE do          -- entry 0 is the engine's dummy
    local marker = peek(p + 0x48)
    if marker and not SILENT[marker] then
      local name = cstr(peek, p + 4, 64)
      local qid = owned[name:lower()]
      local key = name:lower()
      if qid and is_dead(qid) and (sent[key] or 0) < 3 then   -- a node that will not change is left be
        sent[key] = (sent[key] or 0) + 1
        recs[#recs + 1] = Vb.set_icon(name, Vb.ICON.none)
      end
    end
  end
  if #recs == 0 then return 0 end
  -- one section holds at most ~16 KB (WorkFunktion's buffer): send in chunks
  local A, chunk, size = require "actions", {}, 0
  for _, r in ipairs(recs) do
    if size + #r > 12000 then A.run(table.concat(chunk)); chunk, size = {}, 0 end
    chunk[#chunk + 1] = r; size = size + #r
  end
  if #chunk > 0 then A.run(table.concat(chunk)) end
  if sacred.log then
    sacred.log(("[novanilla] %d giver glyphs hidden (nodes of quests that are not set up)"):format(#recs))
  end
  return #recs
end

-- ── Silent givers: let the region's chatter talk for them ────────────────────
-- A giver of a stripped quest is still bound to its dead node (cCreature+0x245 =
-- the node's index), so a click opens nothing. Unbound (+0x245 = 0) the engine
-- picks the region's line for its creature type instead (FUN_00549920:21-52: the
-- SetRgnDialog table, NPC_Dialog_Farmer and the like), for NPCs whose side and AI
-- mode allow it (RE_dialog_buttons.md §10). The unbinding is vanilla's own record,
-- `SetNPCState <name> 0a 0` (870 records), so the NPC is addressed by its script
-- name: its name id is cObject+0x3c, the engine's name table qm+0x765C (stride
-- 0x48, "LRes:" + name, id at +0x44) turns it back into the name, and the name
-- must resolve to the same creature (object_by_name) before the record is sent.
-- All 181 live bindings to quest-owned nodes in base:VAMPIRELADY are named
-- (res:<n>); the 4 nameless ones are enemies. Region hooks spawn givers when the
-- hero comes near, so the object table is swept continuously, a slice per tick.
local OM_PTR = 0x00AD5C40
local NAMES_B, NAMES_E, NAMES_STRIDE = 0x00AACF80 + 0x765C, 0x00AACF80 + 0x7660, 0x48
local LRES = 0x7365524C                                          -- "LRes"
local sweep = { world = nil, h = 1, dead = nil, dlg_end = nil, done = {}, freed = 0, queued = {} }

local function s32(v) return v and (v >= 0x80000000 and v - 0x100000000 or v) end

-- DlgNPC indices whose node belongs to a quest that is not set up (and not ours).
local function dead_nodes(peek)
  local db, de = peek(DLG_B), peek(DLG_E)
  if not db or not de or de < db then return nil end
  if sweep.dead and sweep.dlg_end == de then return sweep.dead end
  local owned = owned_nodes(peek)
  if not owned then return nil end
  local dead, state = {}, {}
  for p = db + DLG_STRIDE, de - DLG_STRIDE, DLG_STRIDE do
    local qid = owned[cstr(peek, p + 4, 64):lower()]
    if qid then
      if state[qid] == nil then
        local q = sacred.quest_state(qid)
        state[qid] = not (q and (q.setup or q.sdk))
      end
      if state[qid] then dead[(p - db) // DLG_STRIDE] = true end
    end
  end
  sweep.dead, sweep.dlg_end = dead, de
  return dead
end

-- The script name of the object with name id `id` ("res:17085"), or nil.
local function script_name(peek, id)
  local b, e = peek(NAMES_B), peek(NAMES_E)
  if not b or not e or e < b then return nil end
  for p = b, e - NAMES_STRIDE, NAMES_STRIDE do
    if peek(p + 0x44) == id then
      if peek(p) ~= LRES then return nil end
      return cstr(peek, p, 68):sub(6)
    end
  end
  return nil
end

-- One slice of the sweep: `count` object handles. Returns how many were unbound.
function M.free_silent_givers(count)
  local peek = sacred.peek_u32
  if not (peek and sacred.quest_state and sacred.object_by_name and sacred.npc_info) then return 0 end
  local world = require("vars").world()
  if sweep.world ~= world then
    sweep.world, sweep.h, sweep.dead, sweep.done, sweep.queued = world, 1, nil, {}, {}
  end
  local dead = dead_nodes(peek)
  if not dead then return 0 end
  local om = peek(OM_PTR)
  local arr, fin = om and om ~= 0 and peek(om + 4), om and om ~= 0 and peek(om + 8)
  if not arr or arr == 0 or not fin or fin < arr then return 0 end
  local n = (fin - arr) // 4
  local Vb, recs, names = require "verbs", {}, {}
  for _ = 1, count or 600 do
    if sweep.h >= n then sweep.h = 1 end
    local h = sweep.h
    sweep.h = h + 1
    local c = peek(arr + h * 4)
    if c and c ~= 0 and not sweep.done[h] then
      local idx = s32(peek(c + 0x245))
      if idx and idx > 0 and dead[idx] and sacred.npc_info(h) then
        sweep.done[h] = true
        local id = peek(c + 0x3c)
        local name = id and id ~= 0 and script_name(peek, id)
        if name and name ~= "" and sacred.object_by_name(name) == h then
          recs[#recs + 1] = Vb.npc_state(name, Vb.ST.no_node)
          names[#names + 1] = ("%s h=%d"):format(name, h)
        elseif sacred.log then
          sacred.log(("[novanilla] h=%d is bound to a dead node but has no usable name (id %s)")
            :format(h, tostring(id)))
        end
      end
    end
  end
  if #recs > 0 then
    require("actions").run(table.concat(recs))
    sweep.freed = sweep.freed + #recs
    if sacred.log then
      sacred.log(("[novanilla] %d silent givers unbound (%d so far): %s")
        :format(#recs, sweep.freed, table.concat(names, ", ")))
    end
  end
  return #recs
end

-- Keep them hidden: once a world is ready, and every ~5 s after (16 SetIcon records
-- in global sections, mostly area triggers, can put a glyph back). Call once.
local watching = false
function M.keep_markers_hidden()
  if watching then return end
  watching = true
  local V, t = require "vars", 0
  sacred.on_tick(function()
    if not V.is_ready() then t = 0; return end
    t = t + 1
    if t == 8 or t % 20 == 0 then M.hide_dead_markers() end
  end)
end

-- Sweep the object table for silent givers, a slice every tick once a world is
-- ready. Call once.
local freeing = false
function M.keep_givers_talking()
  if freeing then return end
  freeing = true
  local V, t = require "vars", 0
  sacred.on_tick(function()
    if not V.is_ready() then t = 0; return end
    t = t + 1
    if t >= 12 then M.free_silent_givers(600) end
  end)
end

return M
