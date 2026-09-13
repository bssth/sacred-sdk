-- SacredSDK / lua / lib / quest.lua
--
-- High-level helpers for Sacred quest scripts. **WORK IN PROGRESS** — these
-- wrappers cover only the patterns we've fully reverse-engineered. The rest
-- are still emitted via `lib/funkcode.lua` mnemonics or `lib/unsafe.lua` raw
-- bytes. As we decode more of FUN_00472bc0 (interpreter dispatch), this lib
-- grows.
--
-- API
-- ---
--   q.script{ ... }            — flatten a nested list of records into a
--                                single record sequence the baker expects.
--                                Each element may be a record (`{tag,flags,...}`)
--                                or a list of records (`{ rec, rec, ... }`).
--   q.var(name)                — re-exports lib/state.var
--   q.assign(name, value)      — re-exports lib/state.assign
--   q.set_hero_qbit(bit, v)    — re-exports lib/state.set_hero_qbit
--   q.log_entry(quest_id, t, h, s)
--                              — emit three tag-0x35 QuestLogSet records
--                                (title / header / start)
--   q.on_trigger(name)         — emit a tag-0x1a QuestTrigger registering
--                                a hook on the named event
--   q.dialog(trigger, res, btn) — emit a triple of trigger / ResRef / emit
--                                that together render one dialog line
--
-- Naming convention: every constructor here returns either a SINGLE record
-- (a sequence whose first two entries are integers) or a LIST of records
-- (each entry is itself a record). `q.script` discriminates and flattens.

local raw   = require "raw"
local fc    = require "funkcode"
local state = require "state"
local d     = require "dialog"
local text  = require "text"
local inv   = require "inventory"
local rwd   = require "reward"
local ev    = require "events"
local fsm   = require "questfsm"

local M = {}

-- Inline string helper — see lib/text.lua for the full story. Use `q.T"…"`
-- anywhere a `res:NNNN` reference is expected; the string is auto-registered
-- into global.res by the bake. Identical strings dedupe to the same slot.
M.T = text
M.text = text  -- alias if you prefer `q.text(...)` for code clarity

-- Inventory / reward shortcuts — see lib/inventory.lua and lib/reward.lua
-- for full docs. These are native Sacred bytecode patterns; the engine
-- evaluates them at game-time, so they work without runtime hooks.
M.has_item            = inv.has_item             -- 0x3a predicate alone
M.if_has_item         = inv.if_has               -- 0x3a + 0x42 wrapper (no else)
M.has_item_then       = inv.then_body            -- 0x42 wrapper only
M.give_gold           = rwd.give_gold            -- positive amounts give
M.give_gold_from_var  = rwd.give_gold_from_var   -- indirect via named var
M.charge_gold         = rwd.charge_gold          -- give_gold(-amount)

-- Runtime-event helpers — see lib/events.lua. `q.on_trigger_once` fires
-- exactly once per session for a given name; the raw `sacred.on_trigger`
-- runs every time (which means per-frame for ui-rendered resources).
M.on_trigger_once         = ev.on_trigger_once
M.on_trigger_throttled    = ev.on_trigger_throttled
M.on_any_once             = ev.on_any_once
M.on_trigger_when_alive   = ev.on_trigger_when_alive

-- Quest state-machine helper — see lib/questfsm.lua. Wraps the bare
-- on_trigger primitives into a declarative chain of named steps with
-- linear ordering, optional guards, and per-step on_enter side effects.
M.fsm     = fsm
M.define  = fsm.define     -- shortcut: `q.define{ id=…, steps={ … } }`

-- Detect "is this value a single record, or a list of records?"
local function _is_record(t)
  return type(t) == "table" and type(t[1]) == "number" and type(t[2]) == "number"
end

-- Flatten nested lists of records into one flat array. Skips nil / false
-- entries so you can `cond and rec` inline.
function M.script(items)
  local out = {}
  local function add(x)
    if x == nil or x == false then return end
    if _is_record(x) then
      out[#out + 1] = x
    elseif type(x) == "table" then
      for _, y in ipairs(x) do add(y) end
    else
      error("q.script: unexpected entry " .. tostring(x))
    end
  end
  for _, e in ipairs(items) do add(e) end
  return out
end

-- State re-exports for ergonomic single-import.
M.var            = state.var
M.assign         = state.assign
M.assign_inc     = state.assign_inc
M.set_hero_qbit  = state.set_hero_qbit

-- Quest journal. Emits three tag-0x35 (QuestLogSet) records — one each
-- for Title, Header, and Qstart text. Vanilla format reverse-engineered
-- from Sera HQ_3_2_1 records 69656-69658:
--
--   tag=0x35  payload:  U32(quest_id)  U32(first?0:1)  0x01 + cstring + 0x00
--
-- The second u32 is 0 for the FIRST record (Title) and 1 for the rest —
-- it tells Sacred whether to overwrite or append within the slot.
--
-- The cstring opcode is 0x01 — same byte as our `DLG_OP_a` label but
-- vanilla uses it as CSTR1 here (only one cstring), not CSTR2 as our
-- opcode table assumes. So we write the cstring portion as raw _HEX
-- to exactly match vanilla byte-for-byte. Without this match Sacred's
-- interpreter silently drops the record and no journal text appears.
--
-- text_* args accept either plain strings ("HQ_X_Log_Title" → looked up
-- via global.res as res:HQ_X_Log_Title) or T()-returned ids like
-- "res:SDK_xxxxxxxx". We auto-prepend "res:" if missing.
local function _str_to_hex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

function M.log_entry(quest_id, title, header, start_text)
  local out = {}
  local first = true
  local function add(text_key)
    if not text_key then return end
    local res = (text_key:sub(1, 4) == "res:") and text_key
                                              or  ("res:" .. text_key)
    -- Vanilla cstring opcode: 01 <bytes> 00. Match byte-exact.
    local cstr_bytes = "\x01" .. res .. "\x00"
    out[#out + 1] = raw.rec(0x35, 0x00,
      fc.u32(nil, quest_id),
      fc.u32(nil, first and 0 or 1),
      raw.hex(_str_to_hex(cstr_bytes)))
    first = false
  end
  add(title); add(header); add(start_text)
  return out
end

-- Bare trigger registration.
M.on_trigger = d.trigger

-- Dialog scene: trigger + line + emit, in that order. Tail-args optional.
function M.dialog(trigger_name, res_id, button_action, channel, target)
  return { d.scene(trigger_name, res_id, button_action, channel, target) }
end

-- ---------------------------------------------------------------------------
-- RUNTIME quest-book ops. These are NOT bake-time records — do NOT put them
-- inside q.script{}. Call them from sacred.on_world_load / sacred.on_trigger,
-- where the engine's cQuestMgr is live (a brand-new quest_id must be
-- q.register'd from on_world_load BEFORE its log/marker land). They are thin,
-- validated forwarders over the sacred.questbook_* C bindings so authors get a
-- stable q.* front-door instead of raw sacred.* calls. Each errors clearly if
-- the runtime API is absent (e.g. when called at bake time).
--
--   q.register(quest_id)               — append a new display entry
--   q.set_log(quest_id, page, t,h,s)   — set title/header/lines (page 0 = story tab)
--   q.add_log(quest_id, name)          — append one journal line
--   q.marker(quest_id, wx, wy)         — white primary compass/map marker
--   q.set_step_done(quest_id, done)    — fill (true) / hollow (false) the bullet
--   q.mark_solved(quest_id)            — vanilla "solved" look (greyed, marker off)
--   q.complete(quest_id)               — remove the entry from the journal
local function _book(fn_name)
  if not (sacred and sacred[fn_name]) then
    error(("quest: sacred.%s is unavailable — these are RUNTIME ops; call them "
        .. "from sacred.on_world_load / on_trigger, not at bake time")
        :format(fn_name))
  end
  return sacred[fn_name]
end

function M.register(quest_id)            return _book("questbook_register")(quest_id) end
function M.set_log(quest_id, page, ...)  return _book("questbook_set_log")(quest_id, page, ...) end
function M.add_log(quest_id, name)       return _book("questbook_add_log")(quest_id, name) end
function M.marker(quest_id, wx, wy)      return _book("questbook_set_marker")(quest_id, wx, wy) end
function M.set_step_done(quest_id, done) return _book("questbook_set_step_done")(quest_id, done ~= false) end
function M.mark_solved(quest_id)         return _book("questbook_mark_solved")(quest_id) end
function M.complete(quest_id)            return _book("questbook_complete")(quest_id) end

-- A quest **section** with a title comment baked in as a no-op marker.
-- Useful for the human reader; the records inside are returned verbatim.
function M.section(name, body)
  -- We emit a "comment" via Lua's sacred.log so logs trace the source quest.
  if sacred and sacred.log then
    sacred.log(("quest section: %s (%d records)"):format(name, #body))
  end
  return body
end

return M
