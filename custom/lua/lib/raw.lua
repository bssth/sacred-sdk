-- SacredSDK / lua / lib / raw.lua
--
-- Low-level record/op builders. This is the layer you reach for when you
-- need to emit specific FunkCode bytecode by hand. Everything else in lib/
-- is built on top of these primitives.
--
-- Usage:
--   local raw = require "raw"
--   return {
--     raw.rec(0x14, 0x00, raw.op("U32_qid_a", 9511)),
--     raw.rec(0x1a, 0x00, raw.op("STR_REF", "HQ_3_2_1_DLG_START")),
--   }
--
-- The C++ baker reads each record as a Lua sequence:
--   { tag:int, flags:int, op1:table, op2:table, ... }
-- where each op is itself a sequence: { label:string, arg1, arg2, ... }
--
-- See sdk/lua_bake.cpp for the full opcode kind/arg matrix.

local M = {}

-- raw.rec(tag, flags, ...ops) -> record table
function M.rec(tag, flags, ...)
  return { tag, flags, ... }
end

-- raw.op(label, ...args) -> op table
function M.op(label, ...)
  return { label, ... }
end

-- raw.hex(hex_string) -> _HEX op (verbatim bytes appended to payload)
-- Use for opcodes the mnemonic encoder doesn't yet model. The baker accepts
-- {"_HEX", "deadbeef"} and copies those bytes into the record's payload
-- immediately after the flags byte. Combined with `raw.rec`, this gives you
-- complete byte-level control while still riding the record framing.
function M.hex(s)
  return { "_HEX", s }
end

return M
