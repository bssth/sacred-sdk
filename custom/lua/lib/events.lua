-- SacredSDK / lua / lib / events.lua
--
-- Higher-level wrappers around `sacred.on_trigger` (the raw runtime hook
-- exposed by sdk/runtime_triggers.cpp).
--
-- Why this exists
-- ---------------
-- Our `sacred.on_trigger` fires on EVERY `sacred_hash` query — i.e. every
-- time Sacred's engine resolves a `res:NNNN` style reference. That includes
-- per-frame UI rendering, so a raw handler on "1042" might run 30 times a
-- second.
--
-- For most mod use cases the modder wants SEMANTIC events: "the first time
-- the dialog text 9908 was displayed this session", or "every time, but at
-- most once per second". This module provides those wrappers as plain Lua
-- closures around the raw API — the C++ side stays simple and the modder
-- picks the semantics they need.
--
-- Discovery flow
-- --------------
-- To find which resource id corresponds to "the dialog Leandra opens", do
-- this:
--   1. Open the in-game overlay (F11), scroll to "Runtime triggers".
--   2. Walk up to Leandra and start the dialog.
--   3. The "Recent trigger names" panel shows the IDs the engine just
--      queried, MOST RECENT FIRST. The first new ID that wasn't there a
--      second ago is likely the line you want.
--   4. Use that ID in `events.on_trigger_once("ID", function() … end)`.

local M = {}

-- Internal: which raw events have we already serviced this session.
-- Keyed by trigger name (a string — Sacred resource ids come through as
-- decimal strings, named entries as their full symbolic name).
local _seen = {}

-- Reset session memory. Useful if you want "first this minute" semantics
-- — schedule yourself a clear via a periodic mod-level timer.
function M.reset()
  _seen = {}
end

-- All wrapper closures forward `ctx` (the table the C side hands every
-- handler) to the user fn unchanged. So a user-supplied callback gets:
--     function(ctx)            -- on_trigger_once / on_trigger_throttled
--     function(matched, ctx)   -- on_any_once
-- The ctx exposes :gold(), :give_gold(N), :has_item(N), :set_qbit(N),
-- :notify(text), and the field ctx.trigger_name (the name that fired).

-- Fires `fn` the FIRST time `name` flows through sacred_hash. Subsequent
-- queries of the same name are silently ignored until `M.reset()` is
-- called. Best fit for "dialog opened" / "quest accepted" semantics.
function M.on_trigger_once(name, fn)
  assert(type(name) == "string", "events.on_trigger_once: name must be a string")
  assert(type(fn) == "function", "events.on_trigger_once: fn must be a function")
  assert(sacred and sacred.on_trigger,
         "events.on_trigger_once: sacred.on_trigger unavailable " ..
         "(DLL too old or runtime_triggers init failed)")
  sacred.on_trigger(name, function(ctx)
    if _seen[name] then return end
    _seen[name] = true
    fn(ctx)
  end)
end

-- Fires `fn` every time `name` is queried, but at most once per `min_ms`
-- milliseconds. Best for state-machine events that may fire repeatedly.
function M.on_trigger_throttled(name, min_ms, fn)
  assert(type(name) == "string", "events.on_trigger_throttled: name must be a string")
  assert(type(min_ms) == "number" and min_ms > 0,
         "events.on_trigger_throttled: min_ms must be > 0")
  assert(type(fn) == "function", "events.on_trigger_throttled: fn must be a function")
  assert(sacred and sacred.on_trigger,
         "events.on_trigger_throttled: sacred.on_trigger unavailable")
  local last = 0
  sacred.on_trigger(name, function(ctx)
    -- os.clock() returns CPU seconds; granularity is fine for our needs.
    local now = math.floor((os.clock() or 0) * 1000)
    if (now - last) < min_ms then return end
    last = now
    fn(ctx)
  end)
end

-- Convenience: fire on first occurrence of ANY of the given names. The
-- handler receives the matched name AND the ctx, in that order, so it
-- can branch on which id actually triggered and still use ctx.
function M.on_any_once(names, fn)
  for _, n in ipairs(names) do
    sacred.on_trigger(n, function(ctx)
      if _seen[n] then return end
      _seen[n] = true
      fn(n, ctx)
    end)
  end
end

-- Like `on_trigger_once` but ALSO skips invocations where the hero
-- pointer isn't resolved yet (= ctx:gold() returns nil). That filters
-- out class-FunkCode preload / journal-precache passes, which fire
-- before the player has actually entered the world.
--
-- A second filter: never fires twice in the SAME game-world session.
-- A "session" implicitly starts the first time we see a valid hero.
function M.on_trigger_when_alive(name, fn)
  assert(type(name) == "string", "events.on_trigger_when_alive: name must be a string")
  assert(type(fn) == "function", "events.on_trigger_when_alive: fn must be a function")
  assert(sacred and sacred.on_trigger,
         "events.on_trigger_when_alive: sacred.on_trigger unavailable")
  sacred.on_trigger(name, function(ctx)
    if not ctx:gold() then return end       -- hero struct not built yet
    if _seen[name] then return end
    _seen[name] = true
    fn(ctx)
  end)
end

return M
