-- SacredSDK / lua / lib / vanilla.lua
--
-- Load + transform retail Sacred .bin scripts from Lua.
--
-- Workflow
-- --------
--   local v = require "vanilla"
--   local recs = v.load "bin/TYPE_NPC_SERAPHIM/FunkCode"
--   v.gsub_strings(recs, "_sera_", "_glad_")
--   return recs                          -- baker writes custom/bin/.../FunkCode.bin
--
-- `load(rel)` looks for a pre-decompiled snapshot of that .bin, in both trees,
-- the player's first:
--   <game>/custom/lua/_vanilla/<rel>.lua
--   <game>/sdk/custom/lua/_vanilla/<rel>.lua
--
-- THE SNAPSHOTS ARE NOT SHIPPED. They are the game's own scripts in another
-- form -- tens of megabytes of decompiled Ascaron content -- so the SDK neither
-- redistributes them nor puts them in its repository. Make the one you need from
-- your own install, once:
--
--   python sdk/re/py/funkcode_decompile_lua.py bin/TYPE_NPC_SERAPHIM/FunkCode.bin \
--          -o custom/lua/_vanilla/bin/TYPE_NPC_SERAPHIM/FunkCode.lua
--
-- Everything else in the SDK works without them: `require "vanilla"` itself, and
-- every runtime library (npcobj, zones, nativequest, verbs...). Only `v.load`,
-- which rewrites a whole shipped script at bake time, needs a snapshot.
-- `v.have(rel)` says whether one is there, so a mod can degrade politely.
--
-- A future revision will decompile on-the-fly via a C-side `sacred.disasm`
-- helper, eliminating the need to pre-bake snapshots. For now the snapshot
-- file is the source of truth.

local M = {}

-- Where a snapshot of `rel` could be: the player's tree first, then the SDK's.
local function _vanilla_paths(rel)
  rel = (rel or ""):gsub("/", "\\"):gsub("^\\+", "")
  rel = rel:gsub("%.lua$", "")
  return {
    "custom\\lua\\_vanilla\\" .. rel .. ".lua",
    "sdk\\custom\\lua\\_vanilla\\" .. rel .. ".lua",
  }
end

-- Load one candidate. `sacred.read_file` resolves against the GAME directory,
-- which is what we want: the process working directory is not ours to rely on.
local function _try(path)
  if sacred and sacred.read_file then
    local bytes = sacred.read_file(path)
    if bytes then
      local chunk, err = load(bytes, "@" .. path)
      if not chunk then return nil, err end
      return chunk
    end
    return nil, "not found"
  end
  return loadfile(path)                 -- outside the game (offline tests)
end

-- Is a snapshot of `rel` available? Lets a mod fall back instead of failing the
-- whole bake.
function M.have(rel)
  for _, p in ipairs(_vanilla_paths(rel)) do
    if _try(p) then return true, p end
  end
  return false
end

-- Read a vanilla snapshot. Returns the record table.
function M.load(rel)
  local tried = {}
  for _, p in ipairs(_vanilla_paths(rel)) do
    local chunk, err = _try(p)
    if chunk then return chunk() end
    tried[#tried + 1] = ("  %s  (%s)"):format(p, tostring(err))
  end
  error(("vanilla.load: no snapshot of '%s'. Looked in:\n%s\n"
      .. "Make one from your own install (the SDK does not ship the game's "
      .. "scripts):\n  python sdk/re/py/funkcode_decompile_lua.py %s.bin "
      .. "-o custom/lua/_vanilla/%s.lua")
      :format(rel, table.concat(tried, "\n"), rel, rel))
end

-- Apply `fn(record)` to every record in-place. The callback can mutate the
-- record or return a replacement (returning `nil` keeps the original).
function M.transform(records, fn)
  for i, rec in ipairs(records) do
    local replacement = fn(rec, i)
    if replacement ~= nil then records[i] = replacement end
  end
  return records
end

-- Apply `fn(op, rec_index, op_index)` to every op of every record.
function M.for_each_op(records, fn)
  for ri, rec in ipairs(records) do
    for oi = 3, #rec do
      fn(rec[oi], ri, oi)
    end
  end
  return records
end

-- Apply `fn(arg, op, rec_index)` to every string arg of every op. Returning
-- a new string replaces it; returning nil keeps the original. Numbers /
-- nested tables are skipped.
function M.for_each_string(records, fn)
  M.for_each_op(records, function(op, ri)
    for i = 2, #op do
      if type(op[i]) == "string" then
        local repl = fn(op[i], op, ri)
        if repl ~= nil then op[i] = repl end
      end
    end
  end)
  return records
end

-- Convenience: gsub every string arg.
function M.gsub_strings(records, pattern, replacement)
  return M.for_each_string(records, function(s)
    local new, n = s:gsub(pattern, replacement)
    return n > 0 and new or nil
  end)
end

-- Same as gsub_strings, plus reach into `_HEX` fallback ops. Those ops carry
-- raw bytes as a hex-encoded ASCII string (`"4854..."`); we decode, gsub on
-- the actual bytes, and re-encode. Required-only-when the structural
-- mnemonizer didn't fully decode a record (about 12 % of vanilla data).
local function _hex_to_bytes(s)
  return (s:gsub("..", function(p) return string.char(tonumber(p, 16)) end))
end
local function _bytes_to_hex(s)
  return (s:gsub(".", function(c) return ("%02x"):format(string.byte(c)) end))
end
function M.gsub_bytes(records, pattern, replacement)
  M.gsub_strings(records, pattern, replacement)
  M.for_each_op(records, function(op)
    if op[1] == "_HEX" and type(op[2]) == "string" then
      local bytes = _hex_to_bytes(op[2])
      local new, n = bytes:gsub(pattern, replacement)
      if n > 0 then op[2] = _bytes_to_hex(new) end
    end
  end)
  return records
end

-- Filter records by tag. Returns a new array (does not mutate).
function M.records_with_tag(records, tag)
  local out = {}
  for _, r in ipairs(records) do
    if r[1] == tag then out[#out + 1] = r end
  end
  return out
end

-- Find unique opcode labels used in `records`. Useful for diagnostics.
function M.label_set(records)
  local seen = {}
  M.for_each_op(records, function(op)
    seen[op[1]] = (seen[op[1]] or 0) + 1
  end)
  return seen
end

return M
