-- SacredSDK / lua / lib / probe_s0.lua
--
-- LIVE_PROBES.md session S0 (sdk/.claude/knowledge/quests/LIVE_PROBES.md §6.1):
-- no new build, Lua only, riding on the Q1 scenario of
-- bin/TYPE_NPC_VAMPIRELADY/FunkCode.lua. Every log line is tagged [s0:<probe>].
--
--   P-01  which path gives a runtime SDK NPC its dialog node (captain, Rocheford)
--   P-10  the named-hook table near a signpost (what binds it to wegweiserF_NN)
--   P-13  IsFighting (condition op 0x77) inputs during the test-skeleton fight
--   P-15  CreateNPC op 0x6b (idle roaming) and op 0x03 (facing) on a townsperson pair
--   Q115  journal dump right after every world load (the post-load half)
--   P-19  tour of the 95 DQ givers: KompassPos round trip + sector -> region/level
--
-- Pointer recipes are LIVE_PROBES.md §3 ("Pointers every probe uses").

local M = {}
local peek = sacred.peek_u32

local function log(tag, fmt, ...)
  sacred.log(("[s0:%s] " .. fmt):format(tag, ...))
end

local QM     = 0x00AACF80
local OM_PTR = 0x00AD5C40

local function s32(v)
  if v == nil then return nil end
  if v >= 0x80000000 then return v - 0x100000000 end
  return v
end
local function u16(v) return v and (v & 0xFFFF) end
local function u8(v)  return v and (v & 0xFF) end
local function hex(v) return v and ("%X"):format(v) or "nil" end
local function ts(v)  return tostring(v) end

-- cCreature* of handle h: the object-manager array scan_creatures walks.
local function cre(h)
  if not h or h <= 0 then return nil end
  local om = peek(OM_PTR); if not om or om == 0 then return nil end
  local arr, fin = peek(om + 4), peek(om + 8)
  if not arr or arr == 0 or not fin then return nil end
  if h >= (fin - arr) // 4 then return nil end
  local c = peek(arr + h * 4)
  if not c or c == 0 then return nil end
  return c
end

-- ASCIIZ from dwords (there is no string reader).
local function str_at(p, max)
  if not p or p == 0 then return "" end
  local out = {}
  for i = 0, (max or 64) - 1, 4 do
    local d = peek(p + i); if not d then break end
    for k = 0, 3 do
      local b = (d >> (8 * k)) & 0xFF
      if b == 0 then return table.concat(out) end
      out[#out + 1] = (b >= 32 and b < 127) and string.char(b) or "."
    end
  end
  return table.concat(out)
end

-- section table DAT_00aab708 (stride 0x54): name[64], +0x40 start, +0x44 len, +0x48 owner
local function section(i)
  if i == nil or i < 0 then return nil end
  local b, e = peek(0x00AAB708), peek(0x00AAB70C)
  if not b or not e or b == 0 or e < b then return nil, -1 end
  local n = (e - b) // 0x54
  if i >= n then return nil, n end
  local p = b + i * 0x54
  return { name = str_at(p, 64), start = peek(p + 0x40), len = peek(p + 0x44), owner = s32(peek(p + 0x48)) }, n
end

-- DlgNPC vector qm+0x755c (stride 0x50): +0 handle, +4 name, +0x44 section, +0x48 marker, +0x4c content
local function dlgentry(j)
  if j == nil or j < 0 then return nil end
  local b, e = peek(QM + 0x755C), peek(QM + 0x7560)
  if not b or not e or b == 0 or e < b then return nil, -1 end
  local n = (e - b) // 0x50
  if j >= n then return nil, n end
  local p = b + j * 0x50
  return { h = s32(peek(p)), name = str_at(p + 4, 64), sec = s32(peek(p + 0x44)),
           marker = peek(p + 0x48), content = peek(p + 0x4c) }, n
end

local function hero_cre()
  local s = sacred.hero_slot and sacred.hero_slot()
  return s and cre(s), s
end

-- ─────────────────────────────────────────────────────────────── P-01
local p01 = {}
local function p01_npc(tag, o)
  if not (o and o.alive and o:alive()) then return end
  local h = o:handle()
  local st = p01[h]
  if not st then st = { talking = false, lines = 0, talks = 0 }; p01[h] = st end
  local c = cre(h); if not c then return end
  if not st.seen then
    st.seen = true
    local a = sacred.npc_ai(h)
    log("p01", "%s h=%d cre=%X | pointer check: peek(cre+1C)=%s npc_ai.wx=%s | +245=%s +14=%s (at first sight)",
      tag, h, c, ts(peek(c + 0x1C)), ts(a and a.wx), ts(s32(peek(c + 0x245))), hex(peek(c + 0x14)))
  end
  local talking = sacred.npc_in_dialog(h) and true or false
  if talking and not st.talking then
    st.lines, st.talks = 0, st.talks + 1
    log("p01", "%s h=%d dialog OPEN (talk #%d)", tag, h, st.talks)
  end
  if talking and st.lines < 16 then
    st.lines = st.lines + 1
    local d245, f15c, f160 = s32(peek(c + 0x245)), s32(peek(c + 0x15C)), s32(peek(c + 0x160))
    local sec, nsec = section(f160)
    local e, ne = dlgentry(f15c)
    local esec = e and e.sec and section(e.sec)
    log("p01", "%s t=%d s150=%s +245=%s +15c=%s +160=%s -> sec=%s owner=%s (n=%s) | dlg[+15c]: h=%s name='%s' +44=%s(%s) +48=%s +4c=%s (n=%s) | qm+75c0=%s cre+0c=%s | ans=%s",
      tag, st.lines, ts(u16(peek(c + 0x150))), ts(d245), ts(f15c), ts(f160),
      sec and ("'" .. sec.name .. "'") or "none", sec and ts(sec.owner) or "-", ts(nsec),
      e and ts(e.h) or "-", e and e.name or "-", e and ts(e.sec) or "-",
      esec and ("'" .. esec.name .. "'") or "-", e and ts(e.marker) or "-", e and hex(e.content) or "-", ts(ne),
      hex(peek(QM + 0x75C0)), hex(peek(c + 0x0C)), hex(peek(0x00AB7394)))
  end
  if not talking and st.talking then
    log("p01", "%s h=%d dialog CLOSED after %d sampled ticks | now +245=%s +14=%s",
      tag, h, st.lines, ts(s32(peek(c + 0x245))), hex(peek(c + 0x14)))
  end
  st.talking = talking
end

-- ─────────────────────────────────────────────────────────────── P-10
local p10 = { n = -1, hx = nil, hy = nil }
local function p10_dump(reason)
  local b, e = peek(QM + 0x358), peek(QM + 0x35C)          -- 0x00AAD2D8 / 0x00AAD2DC
  local hx, hy = sacred.hero_pos()
  if not b or not e or b == 0 or e < b then
    log("p10", "%s: hook table empty/unreadable b=%s e=%s hero=(%s,%s)", reason, hex(b), hex(e), ts(hx), ts(hy))
    p10.n, p10.hx, p10.hy = 0, hx, hy
    return
  end
  local n = (e - b) // 0x44
  local fam, wegw = {}, {}
  for i = 0, math.min(n, 3000) - 1 do
    local p = b + i * 0x44
    local nm = str_at(p + 4, 64)
    local key = (nm:match("^([%a_]+)") or "?"):lower():sub(1, 14)
    fam[key] = (fam[key] or 0) + 1
    if nm:lower():find("^wegw") then
      local h = s32(peek(p))
      local c = h and h > 0 and cre(h)
      wegw[#wegw + 1] = ("%s:h=%s@(%s,%s)"):format(nm, ts(h),
        c and ts(peek(c + 0x1C)) or "?", c and ts(peek(c + 0x20)) or "?")
    end
  end
  local fl = {}
  for k, v in pairs(fam) do fl[#fl + 1] = k .. "=" .. v end
  table.sort(fl)
  log("p10", "%s: %d entries, hero=(%s,%s) | name families: %s", reason, n, ts(hx), ts(hy), table.concat(fl, " "))
  if #wegw == 0 then
    log("p10", "  no wegw* entries")
  else
    for i = 1, #wegw, 5 do log("p10", "  wegw: %s", table.concat(wegw, "  ", i, math.min(i + 4, #wegw))) end
  end
  p10.n, p10.hx, p10.hy = n, hx, hy
end
local function p10_tick(t)
  local b, e = peek(QM + 0x358), peek(QM + 0x35C)
  local n = (b and e and b ~= 0 and e >= b) and (e - b) // 0x44 or 0
  if n ~= p10.n then p10_dump("changed"); return end
  if t % 120 == 0 then                                        -- every 30 s, if the hero moved
    local hx, hy = sacred.hero_pos()
    if hx and p10.hx and math.abs(hx - p10.hx) + math.abs(hy - p10.hy) >= 20 then p10_dump("moved") end
  end
end

-- ─────────────────────────────────────────────────────────────── P-13
local p13 = {}
local function isfighting_as_coded(c)       -- RE_conditions_vars.md §1.2 row 0x77
  local fc   = u16(peek(c + 0xFC)) or 0
  local s150 = u16(peek(c + 0x150)) or 0
  local t104 = peek(c + 0x104) or 0
  local t158 = peek(c + 0x158) or 0
  local r = true
  if (fc == 4 or fc == 8) and t104 ~= 0 then r = false end
  if ((s150 == 3 or s150 == 5) or (fc == 0xB or fc == 6)) and t158 ~= 0 then r = false end
  return r, fc, s150, t104, t158
end
local function p13_one(tag, h)
  local c = cre(h); if not c then return end
  local r, fc, s150, t104, t158 = isfighting_as_coded(c)
  local key = ("%d/%d/%s/%s/%s"):format(fc, s150, t104 ~= 0 and "T" or "0", t158 ~= 0 and "T" or "0", ts(r))
  if p13[h] ~= key then
    p13[h] = key
    log("p13", "%s h=%d fc=%d s150=%d +104=%s +158=%s hp=%s -> IsFighting(as coded)=%s",
      tag, h, fc, s150, hex(t104), hex(t158), ts(s32(peek(c + 0x4D8))), ts(r))
  end
end
local function p13_tick()
  if TEST_SKELETON_H then p13_one("skeleton", TEST_SKELETON_H) end
  local g1 = SCENE_GUARDS and SCENE_GUARDS[1]
  if g1 and g1.alive and g1:alive() then p13_one("guard1", g1:handle()) end
  local hc, hs = hero_cre()
  if hc then
    p13_one("hero", hs)
    local ht = s32(peek(hc + 0x104))
    if ht and ht > 0 and ht < 0x10000 and ht ~= hs and cre(ht) then p13_one("heroTarget", ht) end
  end
end

-- ─────────────────────────────────────────────────────────────── P-15
local p15 = { phase = 0, t = 0, npcs = {} }
local function le16(v) return string.char(v & 0xFF, (v >> 8) & 0xFF) end
local function le32(v)
  v = v & 0xFFFFFFFF
  return string.char(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF)
end
-- townsperson archetype (npc_templates.lua) + op 0x6b <u16> + op 0x03 <u16 facing>,
-- in the order vanilla uses: 02 type, 04 pos, 6b, 03, 0e, END (RE_rewards §2.2/§2.5).
local function build_townsperson(ty, stay, facing)
  return string.char(0x00) .. string.char(0x02) .. le32(ty)
      .. string.char(0x04) .. le32(-2) .. "CPOS:HERO" .. string.char(0)
      .. string.char(0x6B) .. le16(stay) .. string.char(0x03) .. le16(facing)
      .. string.char(0x0E) .. string.char(0x00)
end
local function p15_sample(final)
  for _, n in ipairs(p15.npcs) do
    local inf = sacred.npc_info(n.h)
    local c = cre(n.h)
    if inf then
      local d = math.floor(math.sqrt((inf.kx - n.x0) ^ 2 + (inf.ky - n.y0) ^ 2))
      if d > n.maxd then n.maxd = d end
      log("p15", "%s h=%d pos=(%d,%d) moved=%d max=%d +2b7=%s dir(+70,+74)=%s,%s",
        n.tag, n.h, inf.kx, inf.ky, d, n.maxd, c and hex(u8(peek(c + 0x2B7))) or "?",
        c and hex(peek(c + 0x70)) or "?", c and hex(peek(c + 0x74)) or "?")
    else
      log("p15", "%s h=%d gone", n.tag, n.h)
    end
  end
  if final then
    for _, n in ipairs(p15.npcs) do
      log("p15", "RESULT %s: max displacement %d over 60 s", n.tag, n.maxd)
    end
  end
end
local function p15_tick()
  if p15.phase == 0 then
    if not (CAP_SOFT and CAP_SOFT.alive and CAP_SOFT:alive()) then return end
    p15.t = p15.t + 1
    if p15.t < 80 then return end                            -- 20 s after the scene is up
    local NPC = require "npc"
    local ty = NPC.FARMER_MALE or 640                      -- Farmer (male), townsperson band
    if not ty then log("p15", "SKIPPED: no townsperson type in npc.lua"); p15.phase = 3; return end
    local hx, hy = sacred.hero_pos()
    if not hx then return end
    local specs = { { tag = "stay(6b=1,03=90)",  stay = 1, face = 90,  dx = 4 },
                    { tag = "roam(6b=0,03=270)", stay = 0, face = 270, dx = -4 } }
    for _, sp in ipairs(specs) do
      local h = sacred.createnpc_engine(build_townsperson(ty, sp.stay, sp.face), ty)
      if h then
        sacred.npc_teleport(h, hx + sp.dx, hy)
        p15.npcs[#p15.npcs + 1] = { tag = sp.tag, h = h, x0 = hx + sp.dx, y0 = hy, maxd = 0 }
        log("p15", "spawned %s type=%d h=%d at (%d,%d)", sp.tag, ty, h, hx + sp.dx, hy)
      else
        log("p15", "spawn %s FAILED (createnpc_engine returned nil)", sp.tag)
      end
    end
    p15.phase, p15.t = (#p15.npcs > 0) and 1 or 3, 0
    return
  end
  if p15.phase == 1 then
    p15.t = p15.t + 1
    if p15.t % 4 == 0 then p15_sample(false) end             -- once a second
    if p15.t >= 240 then p15_sample(true); p15.phase = 3 end  -- 60 s
  end
end

-- ─────────────────────────────────────────────────────────────── P-19
local p19 = { phase = 0, i = 0, wait = 0 }
local function region_rec(s, w)
  if not w or w == 0 then return nil end
  local arr = peek(w + 0x240); if not arr or arr == 0 then return nil end
  local p = peek(arr + s * 4); if not p or p == 0 then return nil end
  local rec = peek(p + 0x17C); if not rec or rec == 0 then return nil end
  return ("rg=%s lvl=%s..%s"):format(ts(u8(peek(rec + 0xD7))), ts(u16(peek(rec + 0xD3))), ts(u16(peek(rec + 0xD5))))
end
local function p19_measure(stn)
  local hx, hy = sacred.hero_pos()
  local hc = hero_cre()
  local s = hc and u16(peek(hc + 0x18))
  local om = peek(OM_PTR)
  local dbl = s and om and region_rec(s, peek(om))           -- RE_rewards §9: double deref
  local sgl = s and om and region_rec(s, om)                 -- scan_creatures: single deref
  local d = hx and math.floor(math.sqrt((hx - stn.x) ^ 2 + (hy - stn.y) ^ 2)) or -1
  log("p19", "DQ_%d %s target=(%d,%d) hero=(%s,%s) d=%d sector=%s | double: %s | single: %s%s",
    stn.id, stn.name, stn.x, stn.y, ts(hx), ts(hy), d, ts(s), dbl or "nil", sgl or "nil",
    stn.interior and " | INTERIOR" or "")
end
local function p19_tick()
  if S0_P19_DONE then return end
  if p19.phase == 0 then
    if not (Q1 and Q1.state == 5) then return end
    if p15.phase ~= 3 then return end
    p19.phase, p19.wait = 1, 60
    log("p19", "Q1 complete: DQ-giver tour starts in 15 s - hands off the keyboard for ~6 minutes")
    return
  end
  local hc = hero_cre()
  if hc and s32(peek(hc + 0x4D8)) and s32(peek(hc + 0x4D8)) <= 0 then
    log("p19", "ABORT: hero HP <= 0"); S0_P19_DONE = true; return
  end
  if p19.wait > 0 then p19.wait = p19.wait - 1; return end
  local ST = require "probe_s0_stations"
  if p19.phase == 1 then                                     -- go to the next station
    p19.i = p19.i + 1
    local stn = ST[p19.i]
    if not stn then
      sacred.set_spawn(2796, 2261)
      log("p19", "DONE: %d stations; hero sent back to the Q1 start (2796,2261)", #ST)
      S0_P19_DONE = true
      return
    end
    local ok = sacred.set_spawn(stn.x, stn.y)
    if not ok then
      log("p19", "DQ_%d %s target=(%d,%d) set_spawn FAILED%s", stn.id, stn.name, stn.x, stn.y,
        stn.interior and " | INTERIOR" or "")
      return
    end
    p19.phase, p19.wait = 2, 12                               -- 3 s to settle
    return
  end
  if p19.phase == 2 then
    p19_measure(ST[p19.i])
    p19.phase = 1
  end
end

-- ─────────────────────────────────────────────────────────────── wiring
local loaded, t = false, 0
sacred.on_world_load(function() loaded, t = false, 0 end)

local function safe(name, fn, ...)
  local ok, err = pcall(fn, ...)
  if not ok then log("err", "%s: %s", name, tostring(err)) end
end

sacred.on_tick(function()
  local hx = sacred.hero_pos()
  if not hx then return end
  t = t + 1
  if not loaded then
    loaded = true
    log("q115", "world loaded - journal dump follows (post-load +0x0C state)")
    if sacred.questbook_dump then safe("q115", sacred.questbook_dump) end
  end
  if t == 40 then safe("p10", p10_dump, "post-load") end
  if t > 40 then safe("p10", p10_tick, t) end
  safe("p01", p01_npc, "CAP", CAP_SOFT)
  safe("p01", p01_npc, "ROCH", Q1 and Q1.roch)
  safe("p13", p13_tick)
  safe("p15", p15_tick)
  safe("p19", p19_tick)
end)

sacred.log("[s0] probe_s0 loaded: P-01 P-10 P-13 P-15 Q115 P-19 armed")
return M
