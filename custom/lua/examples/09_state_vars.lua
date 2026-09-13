-- ============================================================
-- Example 09: script variables at runtime
-- ============================================================
--
-- Vanilla quests keep their state in NAMED INTEGER VARIABLES. SetVar, IncVar,
-- DecVar, RndVar and SetVarBit write them, the IsVar* conditions test them, and
-- +VAR(name) splices them into resource keys. The table belongs to the quest
-- manager (qm+0x7550), and every savegame carries it, so a variable is also
-- where SDK quest state goes when it must survive a reload.
--
-- Runtime API
--   sacred.var_get(name)          -> integer or nil
--   sacred.var_set(name, value)   -> true / false (creates a missing variable)
--   sacred.var_inc(name[, n=1])   -> new value (vanilla IncVar: never goes down)
--   sacred.var_dec(name[, n=1])   -> new value (vanilla DecVar: floors at 0)
--   sacred.vars([prefix])         -> { name = value, ... }
--   sacred.var_dump([prefix])     -> count (also written to sdk_loaded.log)
--   ctx:get_var(name) / ctx:set_var(name, value)   -- the same, in on_trigger
--   require "vars"                -- Lua helpers: defaults, bits, on_ready
--
-- Names are 1..31 characters and case-insensitive. Writes need a loaded world.
-- sacred.state_get / state_set are a different table: the named world
-- POSITIONS (pos_haduk_village, LOC_RG1_ZIEL1, ...), not variables.

local V = require "vars"

-- Count how often resource 17095 fires, in a variable the savegame keeps.
sacred.on_trigger("17095", function(ctx)
  local n = V.inc("SDK_EX09_COUNT")
  sacred.log(("[ex09] SDK_EX09_COUNT = %s"):format(tostring(n)))
  if n == 3 then ctx:notify("You reached the threshold!") end
end)

-- Read a vanilla variable: quest 1101's progress word (bit 0 = offer open).
sacred.on_trigger("17631", function()
  sacred.log(("[ex09] 1101 = %s, offer bit %s")
    :format(tostring(V.get("1101")), tostring(V.bit("1101", 0))))
end)

-- Rebuild saved state once the world is up; loaded = true after a savegame.
V.on_ready(function(loaded)
  sacred.log(("[ex09] ready after %s: SDK_EX09_COUNT = %d")
    :format(loaded and "a savegame load" or "a world load", V.get("SDK_EX09_COUNT", 0)))
end)

-- DEBUG: every variable into sdk_loaded.log.
sacred.on_trigger("9999", function() sacred.var_dump() end)

return {}
