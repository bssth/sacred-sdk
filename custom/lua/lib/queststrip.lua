-- SacredSDK / lua / lib / queststrip.lua
--
-- "Soft" quest removal for a vanilla FunkCode record list. Use it to wipe a
-- class's built-in quests so you can lay a fresh storyline on top, WITHOUT
-- depopulating the world.
--
-- Strategy (the SAFE / "soft" reading):
--   A record is dropped ONLY when BOTH hold:
--     1. its tag is a quest-subsystem tag (QuestLogSet / QuestState /
--        KompassPos / HeroQBit / pool / DQ-setup / inline quest-state), AND
--     2. it actually names a quest token (HQ_*, NQ_*, DQ_*, DQ<n>_*),
--        detected in decoded string args AND inside `_HEX` raw payloads.
--   CreateNPC (0x01), CreateOBJ (0x08), dialogs, IF/ELSE/conditions,
--   atmosphere, sectors, sounds — i.e. the whole *world* — are NEVER
--   touched, even if they mention a quest name. So the Vampiress map stays
--   fully populated with NPCs/scenery; only the quest layer is gone.
--
-- Quest families (toggle individually):
--   hq  -> HQ_*  : Main / primary quests
--   nq  -> NQ_*  : Named / secondary quests
--   dq  -> DQ_* / DQ<n>_* : Daily / random-pool quests
--
-- Usage:
--   local v  = require "vanilla"
--   local qs = require "queststrip"
--   local recs = v.load "bin/TYPE_NPC_VAMPIRELADY/FunkCode"
--   local st = qs.strip(recs, { hq = true, nq = true, dq = true })
--   -- st.removed, st.by_tag, st.by_family, st.kept
--   -- Dry run first (no mutation):  local st = qs.scan(recs)

local M = {}

-- Quest tags split by risk (docs/18-funkcode-tag-table.md):
--
-- SAFE = the player-FACING quest layer. Removing these just deletes the
-- journal text, the map markers and the daily-quest pool. The world and
-- the campaign's control flow do NOT depend on them.
M.SAFE_TAGS = {
  [0x35] = "QuestLogSet",     -- journal title/open/done text
  [0x3f] = "QuestKompassPos", -- map marker position
  [0x40] = "QuestKompassOBJ", -- map marker object
  [0x21] = "HideTmpToDo",     -- temporary objective hide
  [0x77] = "DQ_QuestSetup",   -- daily-quest setup
  [0x6d] = "PoolClear_v1",    -- daily pool
  [0x71] = "PoolClear_v2",
  [0x85] = "PoolGetPos",
  [0x44] = "HeroQBit_set",    -- per-hero quest bits
  [0x45] = "HeroQBit_clear",
}
-- STRUCTURAL = quest-STATE plumbing. Tag 0x1a (InlineQuestState, ~3088
-- recs) and 0x17/0x6c/0x70 (QuestState A/B/C), 0x76 also drive triggers
-- and the new-game intro/fade-in sequence — ripping them out is what
-- caused the stuck fade-to-black. Kept by default; only removed with
-- opts.aggressive = true (will likely re-break the intro).
M.STRUCTURAL_TAGS = {
  [0x1a] = "InlineQuestState",
  [0x17] = "QuestStateA",
  [0x6c] = "QuestStateB",
  [0x70] = "QuestStateC",
  [0x76] = "Subsys_76_quest",
}
-- Back-compat alias = the full set (used only when aggressive).
M.QUEST_TAGS = {}
for k, v in pairs(M.SAFE_TAGS)       do M.QUEST_TAGS[k] = v end
for k, v in pairs(M.STRUCTURAL_TAGS) do M.QUEST_TAGS[k] = v end

-- Decode a "_HEX" ascii-hex string to a raw byte string.
local function hex_to_bytes(s)
  return (s:gsub("..", function(p) return string.char(tonumber(p, 16)) end))
end

-- Classify a text blob: returns "hq"|"nq"|"dq" if it carries a quest token,
-- else nil. Matches both `HQ_3_1_4_...` and `res:HQ_...`, case-insensitive
-- start, anchored on a non-alnum (or string start) so we don't match e.g.
-- a word ending in "dq".
local function family_of(text)
  -- normalize: scan for tokens with a boundary before them
  for tok in text:gmatch("[^%w_]?([A-Za-z]+_%w*)") do
    local up = tok:upper()
    if up:match("^HQ_") then return "hq" end
    if up:match("^NQ_") then return "nq" end
    if up:match("^DQ%d*_") then return "dq" end
  end
  -- also catch leading-position tokens (string starts with the token)
  local up = text:upper()
  if up:match("^RES:HQ_") or up:match("^HQ_") then return "hq" end
  if up:match("^RES:NQ_") or up:match("^NQ_") then return "nq" end
  if up:match("^RES:DQ%d*_") or up:match("^DQ%d*_") then return "dq" end
  return nil
end

-- Inspect one record; return the quest family it belongs to (or nil).
-- Scans every op: plain string args, and `_HEX` decoded bytes.
local function record_family(rec, tagset)
  local tag = rec[1]
  if not tagset[tag] then return nil end
  for oi = 3, #rec do
    local op = rec[oi]
    if type(op) == "table" then
      if op[1] == "_HEX" and type(op[2]) == "string" and #op[2] > 0 then
        local fam = family_of(hex_to_bytes(op[2]))
        if fam then return fam end
      end
      for i = 2, #op do
        if type(op[i]) == "string" then
          local fam = family_of(op[i])
          if fam then return fam end
        end
      end
    end
  end
  return nil
end

-- Core pass. `mutate=true` rewrites `recs` in place. Returns a stats table.
local function run(recs, opts, mutate)
  opts = opts or {}
  local want = {
    hq = opts.hq ~= false,   -- default ON
    nq = opts.nq ~= false,
    dq = opts.dq ~= false,
  }
  -- Default = SAFE only (player-facing layer). aggressive=true also rips
  -- the STRUCTURAL quest-state plumbing (breaks the new-game intro).
  local tagset = {}
  for k, v in pairs(M.SAFE_TAGS) do tagset[k] = v end
  if opts.aggressive then
    for k, v in pairs(M.STRUCTURAL_TAGS) do tagset[k] = v end
  end
  local st = {
    removed = 0, kept = 0, aggressive = opts.aggressive or false,
    by_tag = {}, by_family = { hq = 0, nq = 0, dq = 0 },
  }
  local out = {}
  for _, rec in ipairs(recs) do
    local fam = record_family(rec, tagset)
    if fam and want[fam] then
      st.removed = st.removed + 1
      st.by_family[fam] = st.by_family[fam] + 1
      local name = tagset[rec[1]] or ("0x%02x"):format(rec[1])
      st.by_tag[name] = (st.by_tag[name] or 0) + 1
    else
      st.kept = st.kept + 1
      out[#out + 1] = rec
    end
  end
  if mutate then
    for i = #recs, 1, -1 do recs[i] = nil end
    for i, r in ipairs(out) do recs[i] = r end
  end
  return st
end

-- Dry run: count what WOULD be removed, no mutation.
function M.scan(recs, opts) return run(recs, opts, false) end

-- Apply: removes the quest records from `recs` in place, returns stats.
function M.strip(recs, opts) return run(recs, opts, true) end

-- Pretty one-line summary for logging.
function M.summary(st)
  return ("queststrip[%s]: removed %d (hq=%d nq=%d dq=%d), kept %d")
    :format(st.aggressive and "aggressive" or "safe", st.removed,
            st.by_family.hq, st.by_family.nq, st.by_family.dq, st.kept)
end

return M
