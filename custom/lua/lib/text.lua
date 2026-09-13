-- SacredSDK / lua / lib / text.lua
--
-- Inline strings → global.res. Lets modders write `T"Welcome, hero!"` in
-- quest scripts instead of hunting for free `res:NNNN` slots and editing
-- global.res by hand. The string is collected during the bake, given a
-- unique resource name like `SDK_xxxxxxxx`, and at the end of the bake we
-- patch `custom/scripts/us/global.res` so Sacred can resolve it.
--
-- WHEN to use
-- -----------
--   d.line(T"Hello hero! I have a task for you.", "btn_ok")
--   q.log_entry(9511,
--               T"The Lost Tome",     -- title
--               T"Chapter 1",         -- header
--               T"A nervous monk runs up to you...")  -- start text
--
-- WHEN NOT to use
-- ---------------
--   If you're re-using a vanilla string, just write `"res:1037"` — that's
--   faster and avoids the global.res rewrite. T() is for NEW strings.
--
-- HOW it works
-- ------------
--   T(s) computes a stable name `SDK_<8 hex>` derived from the content,
--   registers (name, s) in this module's table, and returns the string
--   `"res:SDK_xxxxxxxx"`. Two identical T() calls in different mods produce
--   the same resource id — strings dedupe automatically.
--
--   At the end of the bake the C++ baker calls `text.flush()` (see
--   sdk/lua_bake.cpp / FINALIZE_MODULES). We then:
--     1. read vanilla scripts/us/global.res via sacred.read_file
--     2. parse its index/blob layout
--     3. modify existing slots for any T() whose hash collides with one
--     4. append fresh index slots + text bytes for new T()
--     5. write the result via sacred.write_file to custom/scripts/us/
--
--   Sacred reads custom/scripts/us/global.res through Patch 1 (FUN_0080e680
--   detour) so the new strings are seen on the next launch.

local M = {}

-- ============================================================
-- Configuration
-- ============================================================
M.lang = "us"   -- folder under scripts/. The mod-author can override via
                -- `require("text").lang = "de"` at module-load time if they're
                -- writing a localized mod. Defaults to the language that the
                -- Steam build ships with.

-- ============================================================
-- Registry  (filled by T() calls during the bake)
-- ============================================================
-- name -> text     (resolved by Sacred via sacred_hash(name))
local _registry = {}

-- ============================================================
-- Internal helpers
-- ============================================================

-- Sacred Gold resource-name hash (FUN_0080e780). Pure Lua port. Matches
-- sdk/re/py/sacred_hash.py. Verified against the 6 cases in that file's
-- self-test plus all of `hash_names.csv` (823 numeric ids).
local function sacred_hash(name)
  local MOD = 999999991
  local MUL = 113
  local h = 0
  for i = 1, #name do
    local oc = string.byte(name, i)
    if oc >= 0x61 and oc <= 0x7a then oc = oc - 0x20 end
    local prod = (h * MUL) & 0xFFFFFFFF
    local s    = (oc + prod) & 0xFFFFFFFF
    local si   = (s >= 0x80000000) and (s - 0x100000000) or s
    local r
    if si >= 0 then r = si % MOD
    else            r = -((-si) % MOD)
    end
    if r < 0 then r = r + 0x100000000 end
    h = r & 0xFFFFFFFF
  end
  return h & 0x7FFFFFFF
end
M.sacred_hash = sacred_hash  -- public for diagnostic / advanced use

-- Content-hash that produces a stable, collision-resistant 8-hex-char tag
-- from the input string. Uses FNV-1a 32-bit so two identical strings always
-- map to the same key and reuse the same global.res slot.
local function content_tag(s)
  local h = 0x811C9DC5
  for i = 1, #s do
    h = (h ~ string.byte(s, i)) & 0xFFFFFFFF
    h = (h * 16777619) & 0xFFFFFFFF
  end
  return string.format("%08X", h)
end

-- Encode an ASCII (latin-1) Lua string as UTF-16 LE, NO terminator.
-- Sacred's global.res blob is UTF-16 LE throughout and stores NO per-string
-- NUL on disk — the engine appends the terminator itself at load time (see
-- the SDK's RE notes on the global.res format (wiki: Reverse-Engineering), byte-exact verified). Non-ASCII
-- bytes pass through as the low byte (Windows-1252 ≈ Latin-1); use
-- \u-escapes for true unicode (Lua 5.4: `"\u{00E4}"` for ä).
local function utf16le(s)
  local out = {}
  for i = 1, #s do
    out[#out+1] = string.char(string.byte(s, i), 0)
  end
  return table.concat(out)
end

-- ============================================================
-- Public API
-- ============================================================

-- T(s) → "res:SDK_xxxxxxxx" symbolic identifier you can drop into any place
-- that expects a `res:` reference (d.line first arg, q.log_entry fields, …).
-- Registers the string for inclusion in global.res at flush time.
function M.text(s)
  assert(type(s) == "string", "T(): expected string, got " .. type(s))
  local tag  = content_tag(s)
  local name = "SDK_" .. tag
  _registry[name] = s
  return "res:" .. name
end

-- T.named(name, s) — explicit resource name. Use this only when you need a
-- specific symbolic id, e.g. to override a vanilla one. The `name` MUST be
-- ASCII uppercase/underscore (we don't sanitize). Returns "res:<name>".
function M.named(name, s)
  assert(type(name) == "string" and #name > 0, "T.named: empty name")
  assert(type(s) == "string", "T.named: expected string value")
  _registry[name] = s
  return "res:" .. name
end

-- Inspect what's currently queued. Mostly for debugging from sacred.log().
function M.pending()
  local n, total_bytes = 0, 0
  for _, s in pairs(_registry) do
    n = n + 1
    total_bytes = total_bytes + #s
  end
  return n, total_bytes
end

-- ============================================================
-- global.res patcher  (byte-exact format, RE-verified)
-- ============================================================
--
-- See the SDK's RE notes on the global.res format (wiki: Reverse-Engineering) — a from-scratch rebuild from
-- this model reproduces the real 23123-slot file byte-for-byte.
--
--   [0 .. blob_start)   INDEX: N records, 16 bytes:
--                          u32 d0     (NOT this slot's length — see below)
--                          u32 ident  (sacred_hash(name) & 0x7fffffff)
--                          u32 off    (absolute byte offset; payload at off+4)
--                          u32 pad    (unused; 0)
--   [blob_start .. +4)  u32 = 2 * code_units(slot N-1)   ("extra length" dword)
--   [blob_start+4 .. eof) payloads, UTF-16-LE, contiguous, NO terminators,
--                          NO per-entry header. Physical order == INDEX order.
--
-- Conventions (all certain, byte-exact verified):
--   * blob_start = u32 @ file offset 8 = off[0] = N*16.  N = blob_start/16.
--   * d0[0]   = N  (the resolver's binary-search hi bound).
--   * d0[k]   = 2 * units(k-1)  for k >= 1   (length of the PREVIOUS slot!)
--   * the dword at blob_start    = 2 * units(N-1)        (last slot's length)
--   * off[0]  = blob_start;  off[k+1] = off[k] + 2*units(k)
--   * units(k) = UTF-16 code units of slot k's text (terminator excluded)
--   * The engine binary-searches `ident` (FUN_0080f5e0) → the INDEX MUST be
--     sorted ascending by ident. Vanilla is; appending unsorted (the old
--     bug) made new slots unreachable while in-place reskins still worked.
--
-- Parse keeps only {ident, bytes(=utf16 payload, no term), pad} per slot.

local function _parse(blob)
  assert(#blob > 16, "global.res too small")
  local blob_start = string.unpack("<I4", blob, 9)        -- = off[0] = N*16
  local n = blob_start // 16
  assert(n * 16 == blob_start,
         "global.res blob_start not 16-aligned: " .. tostring(blob_start))

  local d0, ident, off = {}, {}, {}
  local pad = {}
  for k = 0, n - 1 do
    local a, b, c, p = string.unpack("<I4I4I4I4", blob, k * 16 + 1)
    d0[k], ident[k], off[k], pad[k] = a, b, c, p
  end
  -- "extra length" dword sitting at blob_start = 2*units(N-1).
  local last_len_dw = string.unpack("<I4", blob, blob_start + 1)

  -- units(k): for k<N-1 it's d0[k+1]>>1; for the last slot it's last_len_dw>>1.
  local slots = {}
  for k = 0, n - 1 do
    local two_units = (k < n - 1) and d0[k + 1] or last_len_dw
    local nbytes    = two_units            -- 2*units == byte length
    -- payload of slot k begins at off[k] + 4 (1-based: +5).
    local s = blob:sub(off[k] + 5, off[k] + 4 + nbytes)
    slots[#slots + 1] = { ident = ident[k], bytes = s, pad = pad[k] }
  end
  return { slots = slots }
end

-- Build the new global.res from parsed originals + the registered strings.
-- Collisions (same ident as an existing slot) overwrite it (= "reskin",
-- already worked); brand-new idents are inserted and the WHOLE index is
-- re-sorted by ident so the engine's binary search can find them.
local function _emit(parsed, registry)
  -- ident -> slot. Originals first; registered entries override/extend.
  local by_ident = {}
  local order = {}                       -- preserve first-seen for stability
  for _, s in ipairs(parsed.slots) do
    if by_ident[s.ident] == nil then order[#order + 1] = s.ident end
    by_ident[s.ident] = { ident = s.ident, bytes = s.bytes, pad = s.pad or 0 }
  end
  for name, text in pairs(registry) do
    local id = sacred_hash(name) & 0x7fffffff
    if by_ident[id] == nil then order[#order + 1] = id end
    by_ident[id] = { ident = id, bytes = utf16le(text), pad = 0 }
  end

  -- Flatten + sort ascending by ident (unsigned == signed; all < 2^31).
  local list = {}
  for _, id in ipairs(order) do list[#list + 1] = by_ident[id] end
  table.sort(list, function(a, b) return a.ident < b.ident end)

  local M = #list
  local units = {}                       -- code-unit count per slot
  for k = 1, M do units[k] = #list[k].bytes // 2 end

  -- INDEX: d0[0]=M; d0[k]=2*units(k-1); off[0]=M*16; off[k+1]=off[k]+2u(k).
  local idx_parts = {}
  local off = M * 16
  for k = 1, M do
    local d0k = (k == 1) and M or (2 * units[k - 1])
    idx_parts[k] = string.pack("<I4I4I4I4",
      d0k, list[k].ident, off, list[k].pad or 0)
    off = off + 2 * units[k]
  end

  -- BLOB: [u32 = 2*units(last)] then payloads in this (sorted) order.
  local blob_parts = { string.pack("<I4", 2 * units[M]) }
  for k = 1, M do blob_parts[#blob_parts + 1] = list[k].bytes end

  -- No tail, no terminators, no padding.
  return table.concat(idx_parts) .. table.concat(blob_parts)
end

-- ============================================================
-- flush() — called by the C++ baker after all .lua mods have run
-- ============================================================
function M.flush()
  -- Skip silently if nobody asked for inline strings on this bake.
  if next(_registry) == nil then return end

  if not (sacred and sacred.read_file and sacred.write_file) then
    error("text.flush: sacred.read_file/write_file unavailable (DLL too old?)")
  end

  local rel_vanilla = ("scripts/%s/global.res"):format(M.lang)
  local rel_custom  = ("custom/scripts/%s/global.res"):format(M.lang)

  -- Prefer a previously-baked custom override as the source — that way two
  -- separate flushes during one process lifetime stay idempotent. Fall back
  -- to vanilla when no override exists yet.
  local blob, err = sacred.read_file(rel_custom)
  if not blob then
    blob, err = sacred.read_file(rel_vanilla)
    if not blob then
      error("text.flush: cannot read global.res: " .. tostring(err))
    end
  end

  local parsed = _parse(blob)
  local n, total_bytes = M.pending()
  sacred.log(("text.flush: baking %d inline string%s (%d bytes) into %s"):format(
              n, n == 1 and "" or "s", total_bytes, rel_custom))

  local out = _emit(parsed, _registry)
  sacred.write_file(rel_custom, out)
  sacred.log(("text.flush: wrote %s (%d bytes)"):format(rel_custom, #out))

  -- We do NOT clear the registry — if the user clicks "Rebake all" in the
  -- overlay later in the session, the strings should still get written.
end

-- ============================================================
-- Callable module: `T = require"text"; T"Hello"` works AND `T.named(...)`
-- works AND `T.flush()` works. Achieved via a forwarding metatable on the
-- module table itself.
-- ============================================================
setmetatable(M, { __call = function(_, s) return M.text(s) end })

return M
