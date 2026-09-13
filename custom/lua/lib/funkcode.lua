-- SacredSDK / lua / lib / funkcode.lua
--
-- Typed opcode builders. Wraps `raw.op(...)` with named constructors so mod
-- code reads closer to "here's a u32 named foo" than "here's mnemonic XYZ
-- with positional args". Use whichever style feels right — both compile to
-- the same bytes.
--
-- Categories (per FUN_00472bc0 interpreter analysis):
--   stack ops      — one byte, no payload (push/pop/dup/etc.)
--   const ops      — u8 / u16 / u32 / pairs / triples / quads
--   cstr ops       — 1 or 2 null-terminated strings, optionally with a tail
--   u32+cstr ops   — u32 immediate then 1 or 2 strings
--   halt ops       — terminator (END, HALT, BREAK)
--
-- Where there are several opcodes that share a kind/width (e.g. U32_qid_a vs
-- U32_qid_b), we expose the primary one as the named helper. Drop down to
-- `raw.op` for an alternate label if you need it.

local raw = require "raw"
local M = {}

-- Stack ops (one byte each). Just convenience aliases.
function M.stack(byte)
  return { ("STACK_%02X"):format(byte) }
end
function M.END()   return { "END"   } end
function M.HALT()  return { "HALT"  } end
function M.BREAK() return { "BREAK" } end

-- Const ops (u8 / u16 / u32 / pairs / triples / quads).
function M.u8(label, v)   return { label or "U8_8b",   v } end
function M.u16(label, v)  return { label or "C1_a",    v } end
function M.u32(label, v)  return { label or "U32_qid_a", v } end
function M.u32pair(label, a, b) return { label or "U32PAIR_a", a, b } end
function M.xyz(label, x, y, z)  return { label or "XYZ_a", x, y, z } end
function M.quad(label, a, b, c, d) return { label or "U32_QUAD", a, b, c, d } end

-- Resource references.
-- `res_ref(name)` emits a STR_REF op (`HQ_3_2_1_DLG_START` style).
-- `res_id(id)` emits a HERO_REF op (numeric resource id).
function M.res_ref(name) return { "STR_REF", name } end
function M.res_id(id)    return { "HERO_REF", id } end
function M.block_marker(name) return { "BlockMarker", name } end

-- Variable declaration / assignment helpers (DLG_OP_a pattern).
-- These map to the tag-43 (VarDecl_C) / tag-69 (VarAssign_int) record bodies
-- that Sacred uses for state variables (`dq_belohnung`, `hq_uw`, …).
function M.var_decl(name)
  -- Two-string cstr with the variable name + a single payload byte.
  return { "DLG_OP_a", name, "\x0b" }
end
function M.var_op(name, payload)
  -- `payload` is a raw byte-string trailer matching what vanilla emits.
  return { "DLG_OP_a", name, payload or "\x0b" }
end

-- Dialog op (cstr2). First string is the resource id (`res:NNNN`), second
-- is the trigger / button name that runs after.
function M.dialog(res_id, trigger)
  return { "DLG_OP_a", res_id, trigger }
end

-- Emit-text ops (u32+cstr1): an id with an associated string for output.
function M.emit(label, id, text)
  return { label or "EMIT_a", id, text }
end

-- Re-export raw for escape hatches.
M.raw = raw
M.hex = raw.hex

return M
