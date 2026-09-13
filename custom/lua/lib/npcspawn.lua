-- SacredSDK / lua / lib / npcspawn.lua
--
-- Build `tag 0x01 CreateNPC` FunkCode records from Lua. This is the
-- engine's own NPC-spawn path (subsystem FUN_00482510) — the same records
-- vanilla scripts use — so spawned NPCs are fully real (AI, collision,
-- dialog/quest-bindable). No DLL rebuild: records are baked into the mod's
-- FunkCode.bin by the existing pipeline.
--
-- Wire grammar (reverse-engineered, verified vs 6118 vanilla records —
-- see the SDK's RE notes on the NPC model (wiki: Reverse-Engineering)). Payload after the 1 flags byte is
-- an opcode stream; value width is fixed BY opcode:
--   0x02 +i32   Type (creature class id, see npc.lua). 2nd 0x02 = sub id.
--   0x04 +i32   position: i32 == -2 then ASCIIZ string; else numeric vx
--   0x01 +cstr  unique name (dialog/quest records reference this)
--   0x03 +u16   level / rank
--   0x11 +i32   group / team id
--   0x36 +i32   scale (float bits)
--   0x6b +i32   >0 => STATIONARY / sleeping
--   0xa1 (flag) invulnerable / essential candidate
--   side (flag) 0x08 ally-lean | 0x0e neutral | 0x2f enemy  [MED — see note]
--   0x12 (flag) awake/active -> WakeUp
--   0x00        END
--
-- FACTION NOTE: the side->friend/neutral/enemy bit mapping is HIGH on
-- mechanism, MED on exact semantics (npc_model.md open item #1, needs an
-- in-game check). Defaults: side=nil => the creature's class default
-- (monsters hostile, townsfolk neutral). Pass side="ally"/"enemy"/
-- "neutral" to force; verify in-game and tell me if a side misbehaves.
--
-- Usage:
--   local v   = require "vanilla"
--   local spn = require "npcspawn"
--   local NPC = require "npc"
--   local recs = v.load "bin/TYPE_NPC_VAMPIRELADY/FunkCode"
--   spn.add(recs, { type = NPC.UNICORN, pos = "CPOS:HERO" })
--   spn.add(recs, { type = NPC.SKELETON, pos = "CPOS:HERO",
--                   name = "sdk_guard", side = "enemy", level = 20 })
--   return recs

local M = {}

-- ---- little-endian packers -------------------------------------------
local function u8(b)  return string.char(b % 256) end
local function le16(v)
  v = v % 0x10000
  return string.char(v % 256, math.floor(v / 256) % 256)
end
local function le32(v)
  -- accept negatives (two's complement)
  if v < 0 then v = v + 0x100000000 end
  return string.char(v % 256,
                     math.floor(v / 0x100) % 256,
                     math.floor(v / 0x10000) % 256,
                     math.floor(v / 0x1000000) % 256)
end
local function cstr(s) return (s or "") .. "\0" end
local function hexenc(s)
  return (s:gsub(".", function(c) return ("%02x"):format(c:byte()) end))
end

-- side keyword -> bare side opcode (npc_model.md faction table)
local SIDE = {
  ally    = 0x08,   -- local_6c8=1 (ally-leaning)
  neutral = 0x0e,   -- local_6c8=0 (class-neutral)
  enemy   = 0x2f,   -- EBX|=0x40   (forced hostile candidate)
}

-- Build the CreateNPC payload (opcode stream, WITHOUT the leading flags
-- byte — the baker writes flags=0x00 itself and copies this verbatim).
local function build_payload(o)
  assert(o and o.type, "npcspawn: 'type' (creature class id) is required")
  local p = {}
  -- Type (must be first 0x02)
  p[#p+1] = u8(0x02) .. le32(o.type)
  -- optional sub / instance id (2nd 0x02)
  if o.sub_id then p[#p+1] = u8(0x02) .. le32(o.sub_id) end
  -- unique name (so dialog/quest records can find it)
  if o.name then p[#p+1] = u8(0x01) .. cstr(o.name) end
  -- position
  local pos = o.pos
  if pos == nil then pos = "CPOS:HERO" end
  if type(pos) == "number" then
    p[#p+1] = u8(0x04) .. le32(pos)            -- numeric vx index
  else
    p[#p+1] = u8(0x04) .. le32(-2) .. cstr(pos) -- string target
  end
  if o.level then p[#p+1] = u8(0x03) .. le16(o.level) end
  if o.group then p[#p+1] = u8(0x11) .. le32(o.group) end
  if o.scale then p[#p+1] = u8(0x36) .. le32(o.scale) end
  if o.stationary then p[#p+1] = u8(0x6b) .. le32(1) end
  if o.invulnerable then p[#p+1] = u8(0xa1) end
  if o.side then
    local op = SIDE[o.side]
    assert(op, "npcspawn: side must be 'ally'|'neutral'|'enemy'")
    p[#p+1] = u8(op)
  end
  if o.awake ~= false then p[#p+1] = u8(0x12) end  -- default awake
  p[#p+1] = u8(0x00)                               -- END
  return table.concat(p)
end

-- Return a single CreateNPC record table (baker shape: {tag,flags,ops...}).
-- Emitted as one _HEX op = exact bytes, byte-identical to vanilla style.
function M.record(opts)
  return { 0x01, 0x00, { "_HEX", hexenc(build_payload(opts)) } }
end

-- Index just after the FIRST vanilla CreateNPC (tag 0x01) record. That
-- cluster is the start-area spawn block: the walker has set up the start
-- sector/region and the hero exists, so a CreateNPC there resolves its
-- position (incl. CPOS:HERO) correctly. Appending at the very END of the
-- record stream instead spawns into whatever stale sector/region context
-- the walker finished on → the NPC silently fails / parks invisibly.
function M.first_createnpc_index(records)
  for i = 1, #records do
    if records[i][1] == 0x01 then return i end
  end
  return nil
end

-- WARNING: editing the vanilla FunkCode record stream is STRUCTURALLY
-- UNSAFE. Records sit inside IF / BlockReader(0x42) / ELSE(0x3b) blocks
-- whose jumps are BYTE offsets. Inserting a record mid-stream shifts all
-- following bytes → those offsets desync → the new-game intro hangs
-- (fade-to-black). Appending at the end is byte-safe but the walker
-- finishes in a stale sector/region context so the CreateNPC never
-- actually spawns. Net: you cannot bake-add NPCs into vanilla FunkCode.
-- The SDK NPC layer is RUNTIME (sacred.spawn_npc + engine create). This
-- helper only appends (debug) and defaults to a no-op-ish tail add.
function M.add(records, opts, at)
  local rec = M.record(opts)
  if type(at) == "number" then
    table.insert(records, at, rec)          -- caller takes the risk
  else
    records[#records + 1] = rec             -- byte-safe tail (won't spawn)
  end
  return rec
end

M.SIDE = SIDE
return M
