-- SacredSDK / lua / lib / unsafe.lua
--
-- Escape hatches for cases the higher-level wrappers don't cover. None of
-- this is intrinsically unsafe — the bake step still validates everything —
-- but it gives you the rawest possible access to FunkCode bytecode.
--
-- When to use this module
-- -----------------------
--   * The opcode you need is in `funkcode_ops.OPCODE_TABLE` but not exposed
--     in `lib/funkcode.lua` yet. Use `unsafe.op(label, ...)`.
--   * You're modeling a record type the lib helpers don't recognise. Use
--     `unsafe.rec(tag, flags, ops...)` plus `unsafe.hex(...)` if some ops
--     can't be decomposed into mnemonics.
--   * You want to insert arbitrary bytes into a payload. Use `unsafe.hex`.
--
-- When NOT to use this module
-- ---------------------------
--   * If `lib/funkcode.lua` exposes a named helper for what you need, prefer
--     it — the mnemonics are easier to read and harder to typo.
--   * If you're transforming vanilla data, prefer `lib/vanilla.lua`'s
--     declarative helpers (`gsub_strings`, `for_each_op`).

local raw = require "raw"

local M = {}
M.rec = raw.rec
M.op  = raw.op
M.hex = raw.hex

-- Hex-byte string helper. Accepts pairs of hex digits with optional spaces
-- or commas: `unsafe.bytes("de ad be ef")` -> `"\xde\xad\xbe\xef"`.
function M.bytes(s)
  s = s:gsub("[%s,]", ""):lower()
  if #s % 2 ~= 0 then error("unsafe.bytes: odd-length input") end
  return (s:gsub("..", function(pair) return string.char(tonumber(pair, 16)) end))
end

-- u32 helpers: pack any integer into a little-endian 4-byte string.
function M.u32le(v)
  v = v & 0xffffffff
  return string.char(v & 0xff, (v>>8) & 0xff, (v>>16) & 0xff, (v>>24) & 0xff)
end

-- Repeat an op n times — handy for inserting padding or "END END END".
function M.rep(op, n)
  local out = {}
  for i = 1, n do out[i] = op end
  return table.unpack(out)
end

return M
