-- SacredSDK / lua / lib / state.lua
--
-- Variable declarations and assignments. Sacred quest scripts maintain a lot
-- of per-hero state via named integer "globals" (`dq_belohnung`,
-- `dq_belohnung_typ`, `hq_uw`, …). Vanilla encodes these as tag-0x43
-- (VarDecl_C) and tag-0x69 (VarAssign_int) records around a `DLG_OP_a`
-- opcode that carries the variable name as its first cstring.
--
-- Variants observed in vanilla
-- ----------------------------
-- The structural pattern is stable; the small trailer byte and the count of
-- trailing END opcodes vary slightly between records. We capture the two
-- patterns we've seen most often and give them distinct constructors:
--
--   state.var(name)             — `{tag=0x43, DLG_OP_a name "\x0b", END×3}`
--   state.assign(name, value)   — `{tag=0x69, DLG_OP_a name "\x0b", END×3, U32 value}`
--   state.assign_inc(name, value) — variant with "\x0b\x01" trailer + END×2
--
-- The patterns aren't 100 % universal — some records have additional
-- ConditionalEval/BlockReader siblings that participate in the logic. For
-- those, drop down to `lib/funkcode.lua` or `lib/unsafe.lua`.

local raw = require "raw"

local M = {}

function M.var(name)
  return raw.rec(0x43, 0x00,
    {"DLG_OP_a", name, "\x0b"},
    {"END"}, {"END"}, {"END"})
end

function M.assign(name, value)
  return raw.rec(0x69, 0x00,
    {"DLG_OP_a", name, "\x0b"},
    {"END"}, {"END"}, {"END"},
    {"U32_qid_a", value})
end

-- Alternate variant we see in vanilla — trailer is "\x0b\x01" and there are
-- only two trailing END opcodes. Semantics unknown yet; probably "increment"
-- or "scoped". Use when round-tripping requires it.
function M.assign_inc(name, value)
  return raw.rec(0x69, 0x00,
    {"DLG_OP_a", name, "\x0b\x01"},
    {"END"}, {"END"},
    {"U32_qid_a", value})
end

-- Declare a hero-bound quest bit (tag 0x44 HeroQBit_set). `bit` is a numeric
-- bit index expressed as a decimal string (vanilla encodes it inside the
-- DLG_OP_a's first cstring). Confirmed against
-- `bin/TYPE_NPC_GLADIATOR/QuestCode.bin` — the record has NO U32 trailer.
function M.set_hero_qbit(bit)
  return raw.rec(0x44, 0x00,
    {"DLG_OP_a", tostring(bit), "\x0b"},
    {"END"}, {"END"}, {"END"})
end

return M
