-- SacredSDK / lua / lib / probe_s0b.lua
--
-- S0b (2026-09-11): who truncates cCreature+0x245 (the DlgNPC object index)?
-- sdk/.claude/knowledge/quests/LIVE_S0_RESULTS.md §1: dlgnpc_bind appends our element at
-- idx = count (~2045) and stamps a DWORD through FUN_005498F0, yet the talk state machine
-- later reads idx & 0xFF and talks through a vanilla declaration. This probe:
--   1. wraps every Npc method the Q1 scene calls and logs +0x245 before/after each call;
--   2. watches every bound NPC per tick and logs each change of +0x245 (late writers);
--   3. arms the single hardware data breakpoint (DR0, read+write, 1 byte, MAIN THREAD ONLY)
--      on +0x245 right before bind_quest, so the [hwbp] lines name the EIPs that touch it.
--
-- Static writers of +0x245 (the logged EIP is the instruction AFTER the store):
--   FUN_005498F0      0x5498f6 -> 0x5498fc   the stamp (eax = value written)
--   FUN_00482510+cad  0x4831bd -> 0x4831c3   CreateNPC
--   FUN_0054b3d0+3dc  0x54b7ac -> 0x54b7b2   packed-buffer copy (prime suspect)
--   FUN_00465690+2d3c 0x4683cc -> 0x4683d2   serializer loop
--   FUN_00544890+1c7d 0x54650d -> 0x546513   worker/server thread function
-- If +0x245 changes with no writer EIP in the [hwbp] lines, the writer runs on another thread.

local M = {}
local peek = sacred.peek_u32
local QM, OM_PTR = 0x00AACF80, 0x00AD5C40

local function log(fmt, ...) sacred.log(("[s0b] " .. fmt):format(...)) end
local function s32(v)
  if v == nil then return nil end
  if v >= 0x80000000 then return v - 0x100000000 end
  return v
end
local function cre(h)
  if not h or h <= 0 then return nil end
  local om = peek(OM_PTR); if not om or om == 0 then return nil end
  local arr, fin = peek(om + 4), peek(om + 8)
  if not arr or arr == 0 or not fin or h >= (fin - arr) // 4 then return nil end
  local c = peek(arr + h * 4)
  if not c or c == 0 then return nil end
  return c
end
local function v245(h) local c = cre(h); return c and s32(peek(c + 0x245)) end
local function dlg_n()
  local b, e = peek(QM + 0x755C), peek(QM + 0x7560)
  return (b and e and b ~= 0 and e >= b) and (e - b) // 0x50 or -1
end

local watched = {}      -- handle -> { last = value, idx = our element index, t0 = tick of bind }
local tick = 0

local N = require "npcobj"
local Npc = getmetatable(N.wrap(0)).__index

local WRAP = { "teleport", "stance", "set_stationary", "bind_quest", "quest_icon", "say",
               "dialog_off", "set_talkable", "make_companion", "dismiss" }
for _, name in ipairs(WRAP) do
  local orig = Npc[name]
  if orig then
    Npc[name] = function(self, ...)
      local h = self._h
      if name == "bind_quest" and sacred.arm_hwbp then
        local ok = sacred.arm_hwbp(h, 0x245)
        log("t=%d hwbp armed on h=%d +0x245 -> %s", tick, h, tostring(ok))
      end
      local before, n0 = v245(h), dlg_n()
      local r = table.pack(orig(self, ...))
      local after, n1 = v245(h), dlg_n()
      local extra = ""
      if name == "bind_quest" then
        local idx = r[1]
        local low = (idx and idx >= 0) and (idx & 0xFF) or nil
        watched[h] = { last = after, idx = idx, t0 = tick }
        local verdict = ""
        if idx and after == idx then verdict = " | FULL INDEX KEPT"
        elseif low and after == low then verdict = " | TRUNCATED ALREADY (inside bind)" end
        extra = (" | bind returned idx=%s (idx&0xFF=%s), dlg n %d -> %d%s"):format(
          tostring(idx), tostring(low), n0, n1, verdict)
      end
      log("t=%d %s(h=%d) +0x245 %s -> %s%s", tick, name, h, tostring(before), tostring(after), extra)
      return table.unpack(r, 1, r.n)
    end
  end
end

local orig_spawn = N.spawn_template
N.spawn_template = function(arch, opts)
  local o, err = orig_spawn(arch, opts)
  if o then
    log("t=%d spawn_template(%s) h=%d name=%s +0x245=%s dlg n=%d",
      tick, tostring(arch), o._h, tostring(opts and opts.name), tostring(v245(o._h)), dlg_n())
  end
  return o, err
end

sacred.on_tick(function()
  tick = tick + 1
  for h, w in pairs(watched) do
    local v = v245(h)
    if v ~= w.last then
      local low = (w.idx and w.idx >= 0) and (w.idx & 0xFF) or nil
      log("t=%d h=%d +0x245 CHANGED %s -> %s (%d ticks after bind; our idx=%s, idx&0xFF=%s)%s",
        tick, h, tostring(w.last), tostring(v), tick - w.t0, tostring(w.idx), tostring(low),
        (low and v == low) and "  <== TRUNCATION" or "")
      w.last = v
    end
  end
end)

sacred.log("[s0b] probe_s0b loaded: Npc methods wrapped; +0x245 watched; hwbp armed at bind")
return M
