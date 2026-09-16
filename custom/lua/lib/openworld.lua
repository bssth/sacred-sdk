-- SacredSDK / lua / lib / openworld.lua
--
-- What the vanilla quests used to open, opened when a world is ready -- the
-- companion of novanilla.lua for a world without quests. Every entry is a
-- vanilla record copied from the quest section that did it; the multiplayer
-- script (NetScript, which has no story either) already opens the first group
-- the same way in its StartCode (#16937-16956, #40-48).
--
--   require("openworld").open()            -- once per ready world (idempotent)
--
-- 1. PASSES. Invisible walls: trigger objects StartCode lays over map cells
--    (CreateTrigger + TriggerPatch), opened only inside quest sections by
--    `SetTriggerState <name> 07 26` and, for two of them, `SetTriggerState 0b 2
--    04 x y 0` on their cells.
-- 2. DOORS. `SetObjState <name> 07` (unlock) for every door a quest section
--    unlocks and no global section does (28 in base:VAMPIRELADY), plus the
--    multiplayer set; Khorad-Nur's gate is also opened (0f), as NetScript does.
--    A name the engine does not resolve right now is skipped and logged.
-- 3. TELEPORTERS. Five quest sections place a Teleporter object (type 802) and
--    an area trigger whose OMO section teleports whoever steps on it. Here: the
--    same object (once per game, kept by the savegame) and an SDK zone with the
--    same Teleport record. Story-only transitions (the tavern, the cellar exit,
--    the throne room, Stern valley's final jump) are left out.
--
-- Evidence and the full lists: sdk/.claude/knowledge/quests/DISABLE_VANILLA.md.

local Vb = require "verbs"
local A  = require "actions"
local S  = require "sections"
local V  = require "vars"

local W = {}

local function u32(v) return string.pack("<I4", v & 0xFFFFFFFF) end
local function i32(v) return string.pack("<i4", v) end

-- `SetTriggerState 0b <mode> 04 x y 0`: vanilla follows an opened pass with these
-- on its cells (base FunkCode #72268-72275, NetScript StartCode #41-48).
local function trigger_cell(mode, x, y)
  return S.rec(0x31, "\0\11" .. u32(mode) .. "\4" .. i32(x) .. i32(y) .. i32(0))
end

W.PASSES = {
  { "LindwurmPass" },                                    -- q51 extquest51 #71086
  { "PassZurKlosterfeste", cells = {                     -- q59 Init59 #72267-72275
      { 1314, 2170 }, { 1310, 2171 }, { 1315, 2166 }, { 1308, 2165 },
      { 1298, 2161 }, { 1300, 2160 }, { 1300, 2153 }, { 1298, 2142 } } },
  { "VersunkeneFestung", cells = { { 3563, 1336 } } },   -- q65 SwitchPump65 #73310-73311
}

W.DOORS = {
  -- unlocked only by quest sections (base:VAMPIRELADY)
  "DEMON_DOOR_01", "DEMON_DOOR_02",                      -- q73/74 Shaddar's palace
  "HQ_5.6.2_T\252r1",                                       -- q57
  "HQ_6.3b2_T\252r",                                        -- q65
  "HQ_7.2c_T\252r1", "HQ_7.2c_T\252r2",                        -- q74
  "R11B_NQ3_T\252r1",                                       -- q1113
  "R14_NQ04_T\252r1", "R14_NQ04_T\252r2",                      -- q1404
  "R14_NQ1_T\252r1", "R14_NQ1_T\252r2", "R14_NQ1_T\252r3", "R14_NQ1_T\252r4",   -- q1405
  "R22_NQ1_T\252r1", "R22_NQ2_T\252r1",                        -- q2201, q2202
  "R3_HQ_4.3_T\252r1",                                      -- q43
  "R5_NQ2_T\252r1",                                         -- q502
  "R9_NQ1_T\252r1", "R9_NQ1_T\252r2", "R9_NQ1_T\252r3", "R9_NQ1_T\252r4", "R9_NQ1_T\252r5",   -- q901
  "Tyr_N_1", "Tyr_N_2", "Tyr_S_1",                       -- q6001 Tyrfasul
  "T\252r_5001_A", "T\252r_5001_B",                            -- q5003
  "T\252r_HQ_5.4",                                          -- q51/53
  -- the rest of the multiplayer set (NetScript StartCode #16937-16954)
  "Khorad_Nur_Tor", "HQ_6.2_T\252r1", "Medusa_T\252r", "HQ_6.4d_T\252r", "R12_HQ_7.1_T\252r1",
  "Delf_Sklaven_1", "Delf_Sklaven_2", "Delf_Sklaven_3", "Delf_Sklaven_4", "Delf_Sklaven_5",
}
W.OPEN = { Khorad_Nur_Tor = true }

W.TELEPORTERS = {
  -- q61 NimmtElementErde: the Earth element temple, both ways
  { name = "sdk_tp_erdelement_rein", at = { 4582, 1010 }, to = { 4523, 465 } },
  { name = "sdk_tp_erdelement_raus", at = { 4521, 465 },  to = { 4584, 1010 } },
  -- q59 MakeTeleporter59: out of the sewers to the rebel camp
  { name = "sdk_tp_rebellenlager",   at = { 152, 4298 },  to = { 2507, 2200 } },
  -- q9011 eskorte9011: the dungeon, both ways (pos_teleporter_rein/raus_9011)
  { name = "sdk_tp_9011_rein",       at = { 4034, 2568 }, to = { 1668, 6149 } },
  { name = "sdk_tp_9011_raus",       at = { 1668, 6152 }, to = { 4044, 2564 } },
  -- q55 NachOrkland55: to the orc desert (posTeleportWueste55)
  { name = "sdk_tp_wueste",          at = { 1265, 3342 }, to = { 3409, 3330 } },
}

local function log(fmt, ...)
  if sacred and sacred.log then sacred.log("[openworld] " .. fmt:format(...)) end
end

function W.open()
  local recs, skipped = {}, {}
  for _, p in ipairs(W.PASSES) do
    recs[#recs + 1] = Vb.trigger_state(p[1], "unlock", "open")
    for _, c in ipairs(p.cells or {}) do recs[#recs + 1] = trigger_cell(2, c[1], c[2]) end
  end
  local doors = 0
  for _, d in ipairs(W.DOORS) do
    if sacred.object_by_name and not sacred.object_by_name(d) then
      skipped[#skipped + 1] = d
    else
      recs[#recs + 1] = Vb.obj_state(d, "unlock")
      if W.OPEN[d] then recs[#recs + 1] = Vb.obj_state(d, "open") end
      doors = doors + 1
    end
  end
  A.run(table.concat(recs))
  log("%d passes and %d doors opened%s", #W.PASSES, doors,
    #skipped > 0 and (" (not found: " .. table.concat(skipped, ", ") .. ")") or "")

  local Z = require "zones"
  local made = {}
  for i, t in ipairs(W.TELEPORTERS) do
    local var = "SDKOW_TP" .. i
    if not V.get(var) then                         -- the object stays in the savegame
      V.set(var, 1)
      made[#made + 1] = Vb.create_obj(802, { t.at[1], t.at[2], 0 })
    end
    Z.define(t.name, { rect = { t.at[1], t.at[2], t.at[1], t.at[2] },
                       records = { Vb.teleport(nil, { t.to[1], t.to[2], 0 }) } }):arm()
  end
  if #made > 0 then A.run(table.concat(made)) end
  log("%d teleporters armed (%d objects placed)", #W.TELEPORTERS, #made)
end

-- Open once whenever a world becomes ready. Call once.
local armed = false
function W.keep_open()
  if armed then return end
  armed = true
  local t = 0
  sacred.on_tick(function()
    if not V.is_ready() then t = 0; return end
    t = t + 1
    if t == 10 then W.open() end
  end)
end

return W
