# 23 — Runtime NPC layer + quest suppression + spawn hijack

See also `24-storyline.md` — the storyline layer built on top of this: full FunkCode grammar (102 tags / ~127 keywords), custom names, "?!" quest-giver icon, ground items, NPC teleport/equip, and the scratch-report index.

Runtime API on the Steam build (no FunkCode edits). All of it is runtime because editing the vanilla record stream is structurally impossible: insert shifts the byte offsets that `0x42 BlockReader` / `0x3b ELSE` jumps use (new-game intro fade-hang); append never dispatches.

## Modder API (Lua)

### Spawning
```lua
local N   = require "npcobj"
local NPC = require "npc"                 -- 474 creature-type constants
local o = N.spawn(NPC.PRAETORIAN, kx, ky) -- KompassPos (F7 / hero_pos space)
local o = N.spawn_here(NPC.UNICORN)       -- on the hero
```
`N.spawn` uses the hero's (valid) sector, so it is reliable for spots in or near the player's area. Returns an `Npc` object or `nil,reason`.

### Archetypes (decoded 1:1 from vanilla CreateNPC FUN_00482510)
| call | behavior |
|---|---|
| `o:make_aggressive(level)` | hunts the player on sight (true enemy) |
| `o:make_guard(level)` | awake soldier — patrols, defends, friendly to player |
| `o:make_immortal_passive(level)` | won't strike first, invulnerable, holds post |
| `o:make_friendly()` | passive ally |
| `o:activate()` | class-default stance + AI on (passive: only retaliates) |

### Primitives / getters
`o:set_stance(mode,val)` (0=class-default, 1=explicit; 2 = aggressive),
`o:set_faction(v)` (cCreature+0x1F4, bit0 = awake/active),
`o:set_level(n)`, `o:set_invulnerable(on)`, `o:set_stationary(on)`,
`o:wake()`, `o:pos()→kx,ky`, `o:type()`, `o:faction()`, `o:alive()`,
`o:info()→{type,kx,ky,faction}`, `o:save("name")` / `npcobj.get("name")`
(persistent registry; or stash the object in a global).

Raw `sacred.*` equivalents exist for every method (see `examples/14_npc_runtime.lua` header).

### Quest suppression (campaign base, intro intact)
```lua
sacred.hide_vanilla_quests(true)   -- once; persists for the process
```
Hooks the journal builder `FUN_006b07e0` and zeroes the render gate (`entry+0x24`) + marker coords for every non-SDK quest right before it is read — race-free, scripts/intro vanilla. Map markers also hooked (`FUN_004a5980`). Marker still shows in some cases (open item, tactic A = block at registry-entry creation).

### Hero spawn override
```lua
sacred.on_world_load(function() sacred.arm_spawn_teleport(kx, ky) end)
```
Hijacks the engine's own start-teleport (`FUN_0054d9d0`) — one-shot, keeps the engine level, correct sector, no fade. Optional 3rd arg = level, 4th = rewrite-window ms (>0 = rewrite every hero tp for N ms; for multi-step MP spawns).

## RE facts — Steam 2.0.2.28, base 0x00400000

| What | Address / offset |
|---|---|
| cObjectManager singleton | `*(void**)0x00AD5C40` |
| cWorld / sector-map singleton | `*(void**)0x00AD3560` (fallback `*(*0x00AD5C40)`) |
| active-hero idx | `*( *(0x0182EBE8) + 0x14 )`; hero = `*(*(om+4)+idx*4)` |
| creature create | `cObjectManager::create_005fba40` 0x005FBA40 __thiscall(om, type, pos*, 0,1,0) |
| pos struct build | `FUN_006224B0` __thiscall(&buf,sector,X,Y,level); buf = {u16 sector@0, i32 X@4, i32 Y@8, u8 lvl@12} |
| sector resolve | `FUN_00635C40` __thiscall(cWorld,pos) — sector = grid[(X>>6)*0x80+(Y>>6)] @ cWorld+0x284 |
| KompassPos↔world | world = (k+0.5)·53.66563034057617 |
| cCreature: type | +0x10 |
| cCreature: world X/Y | +0x1C / +0x20 (i32); sector +0x18 (u16); level +0x24 (u8) |
| cCreature: stance | +0x1F0 — set by `FUN_0052E420` __thiscall(c,mode,val); mode0=class-default(`FUN_0043ADC0(type)`), mode1=val (2=aggressive) |
| cCreature: faction/side | +0x1F4 (bit0 = awake/active) |
| cCreature: invulnerable | +0x14 bit 0x200000 (CreateNPC op 0xa1) |
| cCreature: STATIONARY | +0x2B7 bit 0x08 (CreateNPC op 0x6b) |
| WakeUp (AI on) | `cCreature_WakeUp_0059F580` __thiscall(c) |
| level/rank | `FUN_0044DDC0` __thiscall(c, float lvl) — not yet effective at runtime (open) |
| journal builder (suppression hook) | `FUN_006B07E0` prologue `81EC DC000000 53` |
| marker resolver (hook) | `FUN_004A5980` prologue `83EC38 8B442448` |
| engine teleport (hijack hook) | `FUN_0054D9D0` prologue `81EC 9C000000 53` |
| quest registry | `DAT_00aad3a4`..`DAT_00aad3a8` stride 0x174, qid@+8, render gate +0x24, page +0x16C |

Hook framework: `install_hook_sig(va, &tramp, thunk, b0,b1,b2)` saves 7 position-independent prologue bytes + jmps back (runtime_triggers.cpp).

## Open items
- Runtime NPC display name (e.g. "Captain Miles") — needs the cCreature name-field RE (name in CreateNPC goes via the quest-NPC array `in_ECX+0x35c` stride 0x34, not a plain field).
- `npc_set_level` (`FUN_0044DDC0`) doesn't visibly change level at runtime.
- Vanilla quest map markers still show in some cases (journal text is fully suppressed). Tactic A = hook the registry-entry creator.
- Multi-world-load re-fire of `on_world_load` is flaky on a 2nd new game in the same process (deferred).
