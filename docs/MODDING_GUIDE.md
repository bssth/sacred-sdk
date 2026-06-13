# SacredSDK Modding Guide

SacredSDK lets you mod Sacred Gold (Ascaron, 2004) without touching the
original game files. Mods live in `custom/`, are written in Lua, and
Sacred reads them through a thin DLL injected at startup.

Verified on the Steam edition (build 2.0.2.28, 2006-10-13).

---

## TL;DR

```lua
-- save as: custom/lua/bin/TYPE_NPC_GLADIATOR/QuestCode.lua
local q = require "quest"

return q.script {
  q.set_hero_qbit(1101),
  q.set_hero_qbit(31),
  q.var "DaemonTotFranz",
}
```

Launch Sacred. It loads the mod, generates `custom/bin/TYPE_NPC_GLADIATOR/
QuestCode.bin` from your Lua, and uses it instead of the vanilla file. The
vanilla `bin/` tree is never modified.

---

## 1. How it hangs together

```
                                    ┌── DLL load ──┐
Steam runs Sacred.exe ─────────────►│  ijl15.dll   │
                                    └──────┬───────┘
                                           │
            ┌──────────────────────────────┴──────────────────────────┐
            │                                                         │
            ▼                                                         ▼
   ┌──────────────────┐                                ┌─────────────────────┐
   │  Embedded Lua    │ scans custom/lua/**/*.lua,     │  IAT hook on        │
   │  (5.4)           │ executes each, writes the      │  CreateFileA        │
   │                  │ result to custom/<rel>.bin     │                     │
   └──────────────────┘                                │  Redirects any read │
                                                       │  of bin/<X> →       │
                                                       │  custom/bin/<X>     │
                                                       │  (if it exists)     │
                                                       └─────────────────────┘
                                                                  │
                                                                  ▼
                                                Sacred opens FunkCode.bin etc.
                                                and never knows it was modded.
```

Two halves: a bake (Lua → .bin) that runs once at game startup, and a
read-time override (fs_override) that swaps in your custom files when
Sacred opens something.

---

## 2. The mod tree

```
<Sacred Gold>/
├── ijl15.dll                 ← SacredSDK proxy (don't edit)
├── ijl15_real.dll            ← original; we forward to it
├── bin/, scripts/, …         ← vanilla; never touched
└── custom/                   ← all your mods live here
    ├── lua/
    │   ├── lib/              ← helper modules (don't edit unless you know)
    │   │   ├── raw.lua, funkcode.lua, quest.lua, dialog.lua, state.lua, …
    │   ├── _vanilla/         ← decompiled vanilla, used by vanilla.load
    │   ├── examples/         ← copy-paste starters (see below)
    │   └── bin/              ← YOUR mods go here, mirroring bin/
    │       └── TYPE_NPC_*/   ← per-class scripts
    │           ├── QuestCode.lua
    │           └── FunkCode.lua
    └── bin/                  ← auto-generated, served to Sacred via fs_override
```

Anything outside `custom/lua/lib/` is yours to edit.

---

## 3. Authoring styles

Three layers, top to bottom:

### 3a. High-level — `lib/quest.lua` & `lib/dialog.lua`

One Lua line per quest/dialog primitive.

```lua
local q = require "quest"
local d = require "dialog"

return q.script {
  q.var "campaign_progress",
  q.assign("campaign_progress", 1),
  q.set_hero_qbit(1101),

  d.trigger "HQ_3_2_1_DLG_START",
  d.line   ("res:1037", "btn_ok"),
  d.emit   (31, "9511"),

  q.log_entry(9511,
              "HQ_3_2_1_Log_Title",
              "HQ_3_2_1_Log_Header",
              "HQ_3_2_1_Log_Qstart"),
}
```

Use this when a helper exists for what you want.

### 3b. Mid-level — `lib/funkcode.lua` & `lib/raw.lua`

When the high-level helpers don't cover your case.

```lua
local raw = require "raw"
local fc  = require "funkcode"

return {
  -- ResRef record by hand
  raw.rec(0x3c, 0x00, fc.dialog("res:1042", "trigger9999")),

  -- A "ConditionalEval" record (encoding not fully understood yet)
  raw.rec(0x3a, 0x00, {"STACK_96"}),
}
```

### 3c. Low-level — `lib/unsafe.lua` and `_HEX`

Last resort for opcodes not yet modeled in the helpers.

```lua
local raw = require "raw"

return {
  -- An opcode not yet named — paste the raw bytes
  raw.rec(0x67, 0x00, raw.hex("0b 27 25 00 00")),
}
```

`raw.hex` (and the `_HEX` pseudo-op underneath) copy bytes verbatim into
the payload. Round-trip stays byte-perfect.

---

## 4. Modding vanilla

Three steps:

1. Decompile vanilla once:
   ```cmd
   python sdk/tools/funkcode_decompile_lua.py ^
       bin/TYPE_NPC_SERAPHIM/FunkCode.bin ^
       -o custom/lua/_vanilla/bin/TYPE_NPC_SERAPHIM/FunkCode.lua
   ```
   This writes a ~23 MB Lua data snapshot you don't edit directly.

2. Write a transformer mod in `custom/lua/bin/<rel>/<name>.lua`:
   ```lua
   local v = require "vanilla"

   local recs = v.load "bin/TYPE_NPC_SERAPHIM/FunkCode"
   v.gsub_bytes(recs, "HQ_3_2_1_sera_", "HQ_3_1_4_glad_")
   return recs
   ```

3. Launch Sacred. Bake takes 2-3 s on first run, then instantaneous;
   restart Sacred to apply edits.

### `vanilla.*` helpers

| Function | Use |
|---|---|
| `v.load(rel)` | Read pre-decompiled snapshot into a records list |
| `v.gsub_strings(recs, pat, repl)` | Lua-`gsub` over every decoded string arg |
| `v.gsub_bytes(recs, pat, repl)` | Same, plus inside `_HEX` fallbacks (full coverage) |
| `v.for_each_op(recs, fn)` | Visit every op of every record |
| `v.for_each_string(recs, fn)` | Visit every string arg of every op |
| `v.records_with_tag(recs, tag)` | Filter records by tag byte |
| `v.label_set(recs)` | Counter map of opcode-labels in use |

---

## 5. Authoring from scratch — `lib/quest.lua` reference

### State

```lua
q.var(name)              -- declare a quest variable (tag 0x43)
q.assign(name, value)    -- set a quest variable (tag 0x69)
q.assign_inc(name, value)-- variant of assign with "\x0b\x01" trailer
q.set_hero_qbit(N)       -- declare hero quest-bit N (tag 0x44)
```

### Quest log

```lua
q.log_entry(quest_id, title, header, start_text)
   -- Emits three tag-0x35 (QuestLogSet) records pointing at
   -- res:<title>, res:<header>, res:<start_text> in global.res.
```

### Inline strings

Write text inline and the bake auto-routes it through
`custom/scripts/us/global.res` — no need to find free `res:NNNN` slots:

```lua
local T = require "text"   -- or `q.T` after `local q = require "quest"`

return q.script {
  d.trigger "MyQuest_DLG_START",
  d.line   (T "Hello, hero! I lost my cat — help?", "btn_ok"),
  q.log_entry(9512,
              T "The Missing Cat",
              T "Chapter 1",
              T "An old woman beckons you over..."),
}
```

How it works:
- Each `T(s)` returns a stable `res:SDK_<8 hex>` reference derived from
  the string content (FNV-1a). Two identical strings dedupe to the same
  slot.
- At the end of the bake, `text.flush()` reads vanilla
  `scripts/us/global.res`, appends one new slot per unique string, and
  writes the result to `custom/scripts/us/global.res`. Patch 1 (the
  `FUN_0080e680` detour) serves the custom file to Sacred on the next
  launch.
- Idempotent across re-bakes — flushing twice in a row produces the same
  file.

Use `T.named("MY_KEY", "text")` for a specific symbolic name (e.g. to
override a known vanilla string by hash).

### Runtime trigger hooks with ctx

Attach Lua callbacks to any trigger name. Fires at game-time when Sacred
dispatches a matching trigger from an NPC dialog or quest state change.

```lua
-- React to player meeting Leandra in the Seraphim main quest
sacred.on_trigger("HQ_3_2_1_sera_DLG_OFFEN", function(ctx)
  sacred.log("Leandra greeted the hero!")
  ctx:notify("Quest discovered!")        -- gold banner top-center
  ctx:give_gold(100)                     -- live struct write
end)

-- Multiple handlers per trigger stack; all run in registration order
sacred.on_trigger("HQ_3_2_1_sera_DLG_START", function(ctx)
  -- Maybe layered logic from a different mod
end)

-- Drop all handlers (for clean re-registration in a re-bake)
sacred.clear_triggers()
```

### The `ctx` table

Every handler receives a `ctx` table as its first arg:

| Method | Returns | Notes |
|---|---|---|
| `ctx.trigger_name` | string | the name that fired (e.g. "17095") |
| `ctx:gold()` | int or nil | nil if hero pointer not yet built (menu/loading) |
| `ctx:give_gold(N)` | bool | direct write to `hero+0x3EE`; N<0 charges; clamped |
| `ctx:charge_gold(N)` | bool | alias for `give_gold(-N)` |
| `ctx:has_item(res)` | bool/nil | scans 18 equip slots; backpack not yet covered |
| `ctx:set_qbit(n,v)` | bool | stub — use `q.set_hero_qbit` at bake-time |
| `ctx:get_qbit(n)` | bool/nil | stub |
| `ctx:notify(text)` | bool | top-of-screen gold banner via SDK overlay |

### `sacred.notify(text)`

Top-of-screen toast banner (rendered by the SacredSDK overlay, not
Sacred's own engine). For "Quest completed", "Group eliminated" style
announcements.

Anti-overflow: every call goes through:
- 256-byte hard cap (truncation)
- 750ms minimum gap between successive accepted toasts
- back-to-back duplicate dedup
- queue capped at 8, oldest evicted on overflow
- each toast lives 4.5 seconds

Returns `true` if the toast was queued, `false` if throttled / deduped /
empty. Safe to call repeatedly without overflowing the queue.

```lua
sacred.notify("Quest completed!")
ctx:notify("Same — `ctx` just forwards")
```

How it works:
- During bake, your `sacred.on_trigger(name, fn)` calls register handlers
  in the persistent Lua state.
- After bake completes, the state is handed to `runtime_triggers` (never
  closed). Two trampolines patch FUN_004915a0 (SelfTriggerQuest) and
  FUN_00491170 (Dialog-Check) — the two functions Sacred uses to fire
  triggers by name.
- When a trigger fires, the trampoline reads the trigger name from the
  interpreter context (`[ECX + 0xa460]`), calls the C-side dispatcher,
  which looks up handlers and runs each in `pcall`. Errors are logged but
  don't break the chain.

Current limitations:
- Triggers fired from area volumes / scripted cutscenes may not reach
  these two dispatchers — coverage grows as more callers are mapped.
- Re-bakes during a session replace the Lua state's handlers (mods
  re-register from scratch each bake).

### Inventory & reward

Native Sacred bytecode predicates and effects — the engine evaluates
them at game-time, no runtime hook needed.

```lua
-- Check: does the hero carry item resource 17562?
q.has_item(17562)               -- predicate (tag-0x3a ConditionalEval) alone

-- if/then convenience: predicate + body wrapper (tag-0x42 BlockReader)
q.if_has_item(17562, {
  d.line(T "You brought it back!", "btn_ok"),
  q.give_gold(500),
  q.set_hero_qbit(9512),
})

-- Pure gold ops:
q.give_gold(500)                -- positive amount → grant gold
q.charge_gold(200)              -- alias for give_gold(-200)
q.give_gold_from_var "RewardX"  -- amount stored in named global, looked up
                                -- at game-time (mirrors vanilla's
                                -- Belohnung_Whiskey pattern)
```

Conditional ELSE (the partner branch fired when the predicate is false)
is partly reverse-engineered — see `examples/05_conditional_dialog.lua`
for the raw STACK_96/STACK_97 pattern. A `q.if_else_has_item(...)` helper
lands once that encoding is modeled cleanly.

### Dialog primitives (from `lib/dialog.lua`)

```lua
d.trigger(name)              -- tag 0x1a QuestTrigger
d.line(res_id, button_action)-- tag 0x3c ResRef (one renderable line)
d.emit(channel, target)      -- tag 0x42 BlockReader
d.scene(trigger, res, btn, channel, target)
                             -- convenience: trigger + line + emit triple
```

### Quest composer

```lua
q.script { rec1, rec2, list_of_recs, … }
   -- Flattens nested record lists into the format the baker expects.
   -- Pass nil/false in the list to skip an entry — useful with `cond and rec`.
```

---

## 6. Workflow

1. Edit `custom/lua/bin/<rel>/<name>.lua` in any editor.
2. Save.
3. Relaunch Sacred (or click Rebake all .lua in the SacredSDK overlay).
4. The new `custom/bin/<rel>/<name>.bin` is generated and read by Sacred.
5. Observe in-game.

Watch `sdk/logs/sdk_loaded.log` if something doesn't bake — it shows the
exact Lua error and which file failed.

The overlay (toggle F11 to interact) shows live bake stats:
- `baked files` — number of mods loaded this session
- `baked records` — total records emitted
- `status` — last action / error

---

## 7. What works today vs. what's coming

### Working

- Done — Lua 5.4 embedded in DLL, full standard library available
- Done — Author from scratch using `lib/quest.lua` builders
- Done — Load + mutate vanilla via `lib/vanilla.lua`
- Done — Byte-perfect round-trip (vanilla → Lua → vanilla, verified on 132 of 132 vanilla `.bin` files)
- Done — `require` works for `custom/lua/lib/`-relative modules
- Done — `sacred.log(msg)` writes to `sdk_loaded.log` and the overlay ring
- Done — `sacred.read_file(rel)` reads any file under `<game>/`
- Done — `sacred.write_file(rel, bytes)` writes under `custom/` (used by text.lua)
- Done — Inline strings via `T"..."`, auto-baked into `custom/scripts/us/global.res`
- Done — `q.has_item(item)` / `q.if_has_item(item, body)`, native bytecode predicate
- Done — `q.give_gold(amount)` / `q.charge_gold(amount)`, native gold ops
- Done — `sacred.on_trigger(name, fn)`, Lua callback fires at game-time
  when Sacred dispatches a matching trigger (two trampolines on
  FUN_004915a0 / FUN_00491170)
- Done — `ctx:gold()` / `ctx:give_gold(N)` inside trigger handlers, direct
  hero-struct write + cosmetic event for the coin sound
- Done — `ctx:has_item(id)`, equipment-slot scan (backpack scan TBD)
- Done — `ctx:notify(text)`, top-of-screen toast banner via overlay
- Done — `q.fsm.define{…}`, declarative quest state machines (linear
  ordering, guards, per-step on_enter, cross-quest gates). See
  `examples/08_questfsm.lua`.
- Done — `sacred.state_get/state_set/state_dump` + `ctx:get_var/set_var`,
  read / overwrite Sacred's named-state store (`hq_uw`, `dq_belohnung`,
  custom vars declared via `q.var()`). Backed by the cQuestManager array
  at `+0x334..+0x338`. See `examples/09_state_vars.lua`.
- Done — `sacred.questbook_register(quest_id)`, append a brand-new entry
  to Sacred's quest-display registry by calling its underlying
  `vector::resize`. Lets a mod ship new quests (not just reskins); the
  bake-time `q.log_entry` records then write text into the new entry.
  Pair with `sacred.on_world_load(fn)` so the register happens before the
  walker dispatches your tag-0x35 records. See
  `examples/10_register_quest.lua`.
- Done — `sacred.on_world_load(fn)`, fires once per save load, the moment
  the FunkCode walker captures cQuestManager. Hook for any "do X exactly
  once after world load" setup.
- Done — Shared `lua_State` across the whole bake; modules accumulate
  state, so `T()` calls in different mods dedupe through one combined
  global.res
- Done — Persistent Lua state, survives the bake. Handlers registered via
  `sacred.on_trigger` live until Sacred exits.

### Limitations (today)

- Partial — `if/else` from Lua: only the `if-then` form is fully
  helper-wrapped. The `else` branch via STACK_96/STACK_97 markers needs
  `raw.rec` until the encoding is modeled cleanly. See
  `examples/05_conditional_dialog.lua`.
- Blocked — Live game-state access (hero gold, inventory, position) from
  Lua: Sacred evaluates checks natively; Lua only emits the bytecode.
- Blocked — Hot reload: relaunch Sacred to pick up Lua edits.
- Blocked — Custom opcodes: every record must use one of Sacred's 156
  native opcodes (the bytecode is executed by Sacred's own interpreter).
- Blocked — Drop-item-into-inventory: `tag-0x37 ItemDrop` is the
  suspected encoding but not fully verified. Not yet exposed via a
  helper.

### Planned

- Planned — Wider `lib/quest.lua` coverage: more recognised vanilla
  patterns mean more readable decompiled scripts and shorter mods.
- Planned — Hot reload via file-watcher.
- Planned — Source-level decompiler (`funkcode_decompile_semantic.py`)
  reaching >50 % pattern coverage so vanilla quests can be edited as
  scripts.

---

## 8. Troubleshooting

### "The mod didn't apply in game"

1. Check `sdk/logs/sdk_loaded.log` for `[lua_bake] baked '…' -> '…'`.
2. Check `<game>/custom/bin/<rel>/<name>.bin` exists and has plausible size.
3. If your mod transforms vanilla, make sure the matching pre-decompiled
   snapshot is at `<game>/custom/lua/_vanilla/<rel>.lua`.
4. Restart Sacred — the bake runs once, on attach.

### "Game crashes after I save my mod"

You probably emitted invalid bytecode. Sacred's interpreter is not
forgiving. Roll back your edit, relaunch, then iterate in smaller steps.

If you're using `raw.rec` with unknown payloads, check that:
- The record's payload is non-empty (just `{tag, flags}` is fine — empty
  body — but make sure that's what you want).
- The bytes don't reference NPC/item/resource ids that don't exist.

### "I get a Lua syntax error"

`sdk/logs/sdk_loaded.log` shows `[lua_bake] <file>: lua error: <msg>`,
including file path and line number.

---

## 9. Examples

Drop these into `custom/lua/bin/<class>/<name>.lua`:

- `examples/01_hello.lua` — minimal mod, just declares state
- `examples/02_text_swap.lua` — bulk-rewrite vanilla via gsub
- `examples/03_dialog_block.lua` — author one dialog scene from scratch
- `examples/04_full_quest.lua` — small standalone quest
- `examples/05_conditional_dialog.lua` — native Sacred branching skeleton
- `examples/06_sidequest.lua` — full side-quest template: inline strings,
  journal, NPC scenes, inventory check, gold reward, appended to an
  existing vanilla NPC class. Copy, tweak constants, ship.
- `examples/07_runtime_triggers.lua` — `sacred.on_trigger` patterns:
  react to vanilla trigger names, stack multiple handlers, capture
  upvalues.
- `examples/08_questfsm.lua` — declarative multi-step quest state machines
  via `q.fsm.define{ … }`: linear ordering, guards, per-step `on_enter`,
  cross-quest gates. Use when a quest has more than one "this happens
  next" beat.
- `examples/09_state_vars.lua` — read / write Sacred's named-state store
  from Lua (`sacred.state_get/state_set` + `ctx:get_var/set_var`). Pair
  bake-time `q.var()` with runtime `ctx:set_var` for cross-session
  persistence — the engine saves these vars into the save game.
- `examples/10_register_quest.lua` — register a brand-new quest_id with
  the engine's display registry, so vanilla mutators (tag-0x35
  log_entry, kompass markers, …) accept your records. Brand-new quests
  are possible, not just reskins of vanilla ones.

---

## 9b. Runtime NPCs & quests (addon layer)

Spawn living creatures, set archetypes (guard / aggressive / passive /
immortal), suppress vanilla quests, override the hero spawn — all at
runtime, no FunkCode edits:
- `sdk/docs/23-npc-runtime.md` — NPC spawn/behavior API + RE addr/offsets
- `sdk/docs/24-storyline.md` — storyline layer: FunkCode grammar
  (102 tags / ~127 script keywords), names, "?!" quest-giver icon,
  ground items, NPC teleport, equip; + the scratch-report index
- `custom/lua/examples/14_npc_runtime.lua` — NPC spawn/behavior example
- `custom/lua/examples/15_storyline.lua` — quest-giver / item / teleport
- libs: `npcobj` (OOP wrapper), `npc` (474 creature constants),
  `classes` (8 hero classes)

## 10. Reverse-engineering reference

To understand what each record/opcode means:
- `sdk/docs/05-funkcode-grammar.md` — record framing
- `sdk/docs/18-funkcode-tag-table.md` — what each tag does (as known)
- `sdk/tools/funkcode_disasm.py` — the disassembler that defines the
  authoritative opcode table
