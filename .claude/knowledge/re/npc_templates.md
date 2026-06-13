# Sacred Gold — CreateNPC dev templates + runtime engine-CreateNPC recipe

Target: Steam 2.0.2.28, `sdk\Sacred_decrypted.exe`, base `0x00400000`, no
ASLR (file off == VA − 0x400000), x86 LE. Evidence: 6,118 tag-0x01 records
in `bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin` decoded with the
`npc_decode2.py`/`npc_model.md` grammar; decompiles
`00475680` (walker), `00482510` (CreateNPC), `00472bc0` (field reader);
SDK runtime layer `sdk/player_state.cpp` (already replays engine record
handlers using `qm = exe_base + 0x00AACF80` as the context object).

Deliverables produced:
- `sdk/.claude/knowledge/re/npc_templates_extract.py` — the extractor (reuses the
  `npc_decode2.py` grammar; clusters all 6,118 CreateNPC records into
  archetypes; emits the Lua + a stats file).
- `sdk/.claude/knowledge/re/npc_templates_stats.txt` — full archetype/side-opcode
  statistics (regenerated each run).
- `custom/lua/lib/npc_templates.lua` — named templates (exact ordered
  opcode lists) + `M.build(archetype, opts)` byte builder + accessors.

---

## A. Archetype catalog

6,118 CreateNPC records: **4,182 hand-placed** (dev-authored, fixed
position / unique name) + **1,936 dynamic-quest-generated** (DQ pool spawns,
`res:DQ*` name + `aaa`/`POS_AUFTRAG*` position — excluded from templates).

### Side / flag opcode frequency (all 6,118 records)

| op | meaning (npc_model.md) | count | reading |
|----|------------------------|------:|---------|
| `0x08` | ally (`local_6c8=1`) | 2665 | ally / friendly-leaning side |
| `0x0e` | neutral (`local_6c8=0`) | 1495 | class-default faction (most townsfolk, dormant enemies) |
| `0x12` | awake → WakeUp | 2196 | pre-activated AI (proactive). Absence ⇒ dormant until trigger/aggro |
| `0x09` | link existing dlg NPC | 720 | this record's creature is bound to a dialog node by name |
| `0x6b` | stationary (`>0` ⇒ sleep/hold) | 925 | holds post / no patrol |
| `0x2b` | side `EBX=8` | 635 | townsperson side A (Farmers dominate) |
| `0x42` | side `EBX=0x400` | 583 | townsperson side B (Citizens/Children) |
| `0x46` | aux EBX bit | 430 | secondary team/aux bit (squad members) |
| `0x72`/`0x74`/`0x1a` | aux `local_6e0` bits | 770/157/525 | FX/aux flags (non-essential) |
| `0x2e`/`0x30`/`0x32` | side `EBX 0x10/0x20/0x80` | 165/163/41 | animal / minor faction sides |
| `0x2f`/`0x31` | ENEMY (`EBX=0x40`) | 11/20 | explicit forced-hostile (rare; most enemies use class default) |
| `0xa1` | invulnerable / essential | 13 | quest-critical immortal NPCs |

**Key inference.** Dev enemies are almost never tagged with an explicit
"enemy" side opcode (`0x2f/0x31` total = 31 of 6,118). Hostility comes from
the **creature type's class default** (the hostility matrix, `combat_init.md`
§4). The side opcodes that matter for dev placement are:
`0x08` ally, `0x0e` neutral/class-default, `0x12` awake, `0x6b` stationary,
`0x2b`/`0x42` the two townsperson sides, `0x09` dialog link.

### The dev archetypes (canonical minimal opcode templates)

Each = the cleanest ordered opcode list the archetype uses (verified by
scanning the simplest real records; full per-cluster stats in
`npc_templates_stats.txt`). `<>` = per-instance hole filled by the Lua
builder. All payloads = `flags:u8=0x00` then this stream then nothing more
(the trailing `0x00` is END).

| template | ordered opcodes | vanilla n | typical types | semantics |
|----------|-----------------|----------:|---------------|-----------|
| **friendly_town_guard** | `02<TYPE> 04<POS> 02<SUBID> 08 12 00` | (soldier+0x08+0x12 subset of 130) | Demon Soldier, Sharuka Captain, Valorian Soldier | ally side + pre-awake ⇒ proactive defender |
| **patrol_soldier** | `02<TYPE> 02<SUBID> 04<POS> 11<GROUP> 08 12 00` | 130 | DeMordreyan/Valorian Soldier, Infantry | guard + a team/group id (squad) |
| **ally_companion** | `02<TYPE> 02<SUBID> 04<POS> 08 12 00` | 1091 | Frost Goblin, Poisonous Troll, Ghost (raised/escort) | non-soldier awake ally that fights alongside |
| **bellevue_enemy** | `02<TYPE> 04<POS> 12 00` | 43 (+ most of 954) | Brigand, Orc Warrior, bosses | hand-placed hostile, pre-awake, class-default hostility |
| **dormant_enemy** | `02<TYPE> 02<SUBID> 04<POS> 0e 00` | 954 | Orc Warrior, Valorian Swordsman, Skeleton | placed asleep; wakes on aggro/trigger (no `0x12`) |
| **townsperson** | `02<TYPE> 04<POS> 11<GROUP> 0e 00` | 1330 | Farmer, Citizen, Child, Chicken | neutral, grouped, ambient |
| **quest_npc** | `01<NAME> 02<TYPE> 04<POS> 09<LINK> 0e 00` | 474 | Farmer, Citizen, Nobleman (named NPCs) | unique name + dialog-node link, neutral |
| **ambient_animal** | `01<NAME> 02<TYPE> 04<POS> 0e 00` | 15 | Horse, Cow, Chicken | named neutral animal |

Notes:
- The **2nd `0x02`** (SUBID) is the NPC sub/instance id (npc_model.md;
  appended to the name buffer `local_654`). Optional — many guards/enemies
  carry it for squad/dialog identity; omit it for a one-off.
- **POSITION** `0x04`: `i32 == -2` then ASCIIZ token (`CPOS:HERO`,
  `CPOS:RES:<r>`, a DefPos key, `DLGNPC`/`QUESTNPC`), OR a numeric `i32`
  vector index (`vx`). Many hand-placed dev records use a **numeric vx**
  (e.g. `04=2510`); the Lua builder accepts either (`pos="CPOS:HERO"` or
  `pos=<int>`). To spawn at an arbitrary KompassPos see §B "Position".
- **friendly_town_guard vs dormant_enemy is the side/awake combo, not the
  type.** Same Skeleton type is a hostile with `bellevue_enemy` or a
  defending ally with `friendly_town_guard`. The engine resolves
  hostility through the type's matrix class (`combat_init.md` §4) blended
  with the `0x08`/`0x0e` side and the `0x12` awake bit — which is exactly
  why driving the engine's own handler (§B) is correct: no struct guessing.

---

## B. Runtime spawn via the engine's OWN CreateNPC (the key)

### B.0 Why this is feasible (and the exact ABI)

`FUN_00482510` (CreateNPC) is **`__thiscall`**: `ECX = the parser/context
block`, plus **one stack argument = the record buffer pointer**. The
walker `FUN_00475680` (case 1, `00475680:198-203`) calls it with the
record copied into its `local_a0c` stack buffer and ECX = the walker's own
`this` (the cInterpretSQW / cQuestMgr context). Decompile facts:

- `FUN_00482510(undefined4 param_1)` — `param_1` is the **record buffer**.
  Internally: `local_568 = 0x4` (the byte **cursor**, init 4),
  `ppvStack_6fc = &local_568` (cursor-ptr arg slot),
  `uStack_6f8 = param_1` (buffer arg slot), then `FUN_00472bc0()`
  (`00482510:271,299-303`).
- `FUN_00472bc0(uint *param_1 = &cursor, int param_2 = recordBuffer)`,
  `in_ECX = context`. It reads the opcode at
  `*(byte*)(param_2 + *param_1)` and bounds-checks against
  `*(u16*)(param_2 + 2)` (the record's size field, read **native-LE**),
  staging decoded values at `in_ECX + 0xa460` (cstr scratch),
  `in_ECX + 0xa860/0xa864/0xa868` (X/Y/sector), `in_ECX + 0xa880`
  (generic u32). `00472bc0:57,69,73-75`.
- The context object is the **cInterpretSQW / cQuestMgr singleton at
  `exe_base + 0x00AACF80`** — already proven and used by the SDK:
  `player_state.cpp` `dlgnpc_bind`/`set_npc_name` use
  `qm = reb + 0x00AACF80` as ECX for `FUN_004BB1D0`/`FUN_00465220` and
  it owns the +0x755c DlgNPC vector, +0x358 NameArrA, +0x31c NPC array,
  +0x334 DefPos store — i.e. it is the `in_ECX` every record handler
  (incl. FUN_00482510 / FUN_00472bc0) uses. Confidence **HIGH**
  (decompile + the SDK already drives two engine handlers through it).

### B.1 Record buffer layout to synthesize

Build the **whole TLV record** (not just the payload) in a heap/stack
buffer, exactly as the on-disk format, then point CreateNPC at it with
cursor=4:

```
offset  bytes                       meaning
  0     0x01                        tag = CreateNPC
  1..2  size  (BIG-ENDIAN, incl 3-byte hdr)   ── see CAVEAT below
  3     0x00                        payload flags byte (always 0x00)
  4..   <opcode stream>             the template (T.build output minus its
                                    own leading flags byte — see note)
  end   0x00                        END opcode (last byte)
```

`custom/lua/lib/npc_templates.lua` `M.build(arch, opts)` returns
`flags(0x00) + opcode-stream + END`. The runtime driver must prepend the
3-byte header `01 <sizeBE>` where `size = 3 + len(build_output)`.

**CAVEAT (HIGH, decompiled + hexdumped).** `FUN_00472bc0:69` reads the
size guard as `*(ushort*)(param_2+2)` — a **native little-endian** u16 at
record offset 2, i.e. bytes `[2],[3] = (size_lo, size_hi_of_BE)`. For all
real CreateNPC records the BE size high byte (record byte 1) is `0x00`
(records are < 256 bytes), so `byte[1]=0x00, byte[2]=sizeLo, byte[3]=0x00`
and the LE read at +2 yields `sizeLo == size`. **Therefore: keep the
synthesized record < 256 bytes (every template is ~25–60 bytes) and write
the header as `01 00 <size&0xff>`** (tag, BE-hi=0x00, size low byte). The
LE guard then reads exactly `size`, cursor 4 lands on the first opcode
(`0x02`), and the loop terminates correctly at END. Verified
byte-exactly against vanilla record @0x008372 and via a build simulation
(`01 00 20 00 02 …`, u16@2 LE = 32 = size, rec[4]=0x02). Records ≥ 256 B
would desync this guard — not a concern here, but do not batch.

### B.2 The runtime call (proposed SDK host fn `sacred.createnpc_engine`)

This is the next host function to add to `runtime_triggers.cpp` /
`player_state.cpp` (mirrors the existing `dlgnpc_bind` pattern). C++:

```c
// sdk/player_state.cpp  (new): drive the engine's own CreateNPC handler.
// payload = bytes from npc_templates.lua M.build() (flags + opcodes + END).
typedef void (__thiscall* fn_createnpc)(void* ctx_ecx, void* recordBuf);

bool createnpc_engine(const uint8_t* payload, size_t plen) {
    HMODULE exe = g_attach.exe_module;
    if (!exe || plen == 0 || plen > 200) return false;     // <256 guard
    uintptr_t reb = (uintptr_t)exe - 0x00400000;
    void* ctx = (void*)(reb + 0x00AACF80);                  // in_ECX
    size_t size = plen + 3;                                 // +TLV header
    uint8_t rec[256] = {0};
    rec[0] = 0x01;                                          // tag
    rec[1] = 0x00;                                          // BE-hi (==0)
    rec[2] = (uint8_t)(size & 0xFF);                        // BE-lo == LE size
    memcpy(rec + 3, payload, plen);                         // flags+ops+END
    __try {
        ((fn_createnpc)(reb + 0x00482510))(ctx, rec);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
```

Why ECX = `reb + 0x00AACF80` and not the walker's stack `this`: the
walker IS that singleton's method; `FUN_00482510` only reads from ECX the
scratch slots `+0xa460/+0xa860/+0xa880` (filled by `FUN_00472bc0`), the
DefPos store `+0x334`, the NPC array `+0x31c`, the DlgNPC vector `+0x755c`
— all of which live in the `0x00AACF80` block the SDK already writes to.
No separate cursor/buffer needs to be primed: CreateNPC sets
`local_568 = 4` itself and reads the buffer we pass; the only context
state it consumes is the (transient, per-opcode) scratch that
`FUN_00472bc0` writes as it decodes — self-contained per call.

### B.3 Position — controlling where it spawns

Three options, in order of confidence:

1. **`pos = "CPOS:HERO"`** (HIGH) — `FUN_00472bc0` resolves it to the
   hero's tile via the hero creature `+0x1C/+0x20/+0x24` and returns the
   special code `0x20`; CreateNPC writes those into the new creature.
   Spawns on the player. Zero extra setup. Use this first to validate.
2. **`pos = "<DefPosKey>"`** (MED) — resolved against the named-position
   store at `ctx+0x334`. Requires the key to have been registered (a
   vanilla `tag 0x17`/`changeDefPos`, or the SDK pre-seeding `+0x334`).
   Good for fixed authored spawn points.
3. **Arbitrary KompassPos (X,Y)** (MED) — `FUN_00472bc0` has no "raw tile"
   position token. Two sub-options:
   a. Pre-stage the resolved coords: before the call, write
      `*(i32*)(ctx+0xa860)=X`, `+0xa864=Y`, `+0xa868=sector` (resolve
      sector via `runtime_spawn.md`'s `FUN_00635c40`), and use a position
      token that makes `FUN_00472bc0` return the "already in 0xa860"
      path. The cleanest is to **skip the `0x04` opcode entirely** in the
      template and pre-seed `0xa860/64/68` — but CreateNPC's default when
      no position resolved is the KompassPos(2850,3860) fallback, so this
      needs a BP to confirm it consumes pre-seeded `0xa860`.
   b. Simpler/robust: spawn with `CPOS:HERO`, then call the already-built
      `sacred.npc_teleport(handle, kx, ky)` (engine `FUN_0054d9d0`) to
      move it to the target tile. **Recommended** — fully engine-driven,
      no struct poking, reuses a proven path.

   Returning the new creature handle: CreateNPC registers the spawn into
   the object manager; the handle is recoverable immediately after the
   call by diffing the cObjectManager creature array
   (`om=*(0x00AD5C40); arr=*(om+4)`) for the new entry, or by reading the
   NPC-array tail entry `ctx+0x31c` (the record just appended). A BP at
   `00482510:962` (`cObjectManager_getData_005fe000(handle)`) reveals the
   handle in a register for a one-time confirmation of the cheapest grab.

### B.4 If invoking FUN_00482510 directly proves infeasible

Fallback = the SAME engine leaves CreateNPC calls, in order, on a bare
`cObjectManager::create_005fba40`'d creature `c`
(`om=*0x00AD5C40; arr=*(om+4); c=*(arr+handle*4)`). This is what the
SDK's `npc_make_combatant` already does and what the live diff vs a
vanilla Valorian Soldier (type 257) corrected
(`combat_init.md` "Ally-team binding"):

| step | engine call / write | VA / ABI |
|------|---------------------|----------|
| B level | `*(u8*)(c+0x24) = level` | — |
| C AI/faction class | `FUN_0052e420(ECX=c, 1, ai_class)` → `c+0x1F0` | `0x0052E420` __thiscall(self,int,u32) |
| D faction word | `*(u32*)(c+0x1F4) = 0x00400000` (the **live vanilla soldier value** — NOT 0x1) | — |
| E WakeUp | `FUN_0059f580(ECX=c)` (needs `c+0x200&0x40000==0`, `c+0xfc==0`) | `0x0059F580` __thiscall(self) |
| F arm AI | leave `c+0x200 == 0` (vanilla steady-state; WakeUp activates) | — |
| + un-stationary | `*(u8*)(c+0x2B7) &= ~8` | — |

`ai_class` per the corrected hostility matrix (`combat_init.md` §"Ally-team
binding": matrix byte **0x00 = attack, 0x01 = ignore**, polarity was
inverted in earlier notes): **3 or 7 = ally defender** (attacks the
monster cluster, NEVER the class-1 hero/allies — matches CreateNPC's
no-side default `00482510:1114 uVar29=3`); 2 = a monster class (wrongly
attacks the hero — do not use for allies); 13 = immune non-combatant
(town/quest). This fallback is the *current* `sacred.npc_make_combatant`;
it works but is the per-bit reconstruction the strategic pivot wants to
retire. **Prefer §B.2** (the engine does all of C–F itself, type-correct,
incl. the combat-art loadout and HP scale the fallback can only
approximate).

### B.5 Confidence

| claim | conf |
|-------|------|
| CreateNPC = `FUN_00482510`, `__thiscall`, ECX=context, stack arg=record buffer | HIGH (decompile `00475680:198-203`, `00482510:300-303`) |
| Context (`in_ECX`) = `exe_base + 0x00AACF80` (cInterpretSQW/cQuestMgr) | HIGH (decompile + SDK `dlgnpc_bind` already drives 2 handlers through it) |
| Record buffer = on-disk TLV; cursor inits to 4 (first opcode at byte 4) | HIGH (`00482510:271`, hexdump of @0x008372: `01 00 28 00 02 …`) |
| Size guard reads `*(u16*)(buf+2)` native-LE ⇒ write hdr `01 00 <lo>`, keep <256 B | HIGH (decompile `00472bc0:69` + byte-exact sim) |
| `M.build()` output is structurally correct (TLV + guard + cursor verified) | HIGH (Python sim of the Lua builder vs canonical record) |
| `CPOS:HERO` position resolves & spawns on the player | HIGH (`00472bc0` CPOS:hero case → code 0x20; npc_model.md) |
| Engine does full correct init (HP/AI/faction/combat-arts) once handler runs | HIGH (CreateNPC post-create block is exactly what the fallback only approximates) |
| Arbitrary-tile spawn via pre-seeded `0xa860` triple | MED — needs a BP; use spawn@HERO + `npc_teleport` instead |
| New-creature handle grab site (array diff vs NPC-array tail) | MED — one BP at `00482510:962` confirms cheapest |

### B.6 Cheap in-game probes (SDK has `npc_field_dump`/`scan_creatures`/`dump_vanilla_of`; we run the game)

1. **Spawn-correctness (decisive, ~3 min).** Add `createnpc_engine`,
   call it with `T.build('bellevue_enemy', {type=NPC.ORC_WARRIOR,
   pos='CPOS:HERO'})`. Expect: an Orc spawns on the hero, has correct
   scaled HP, real combat-arts, attacks the hero (matrix-hostile),
   survives multiple hits — i.e. behaves like a vanilla hand-placed Orc
   (compare with `dump_vanilla_of(ORC_WARRIOR,...)` `+0x1F0/+0x1F4/+0x200/
   +0x4d4/+0x4d8`). No struct writes needed = the engine did it. If it
   spawns but is the inert stub (1-hit, no combat-art) the handler didn't
   run → recheck ECX/buffer.
2. **Guard archetype.** `T.build('friendly_town_guard',
   {type=NPC.VALORIAN_SOLDIER, pos='CPOS:HERO', sub_id=1})` near a
   monster + within sight of the hero. Expect: walks to & attacks the
   monster, NEVER swings at the hero, tanky. Confirms the `0x08+0x12`
   dev combo yields the correct ally defender with zero per-bit fixes
   (the whole point of the pivot). Dump its `+0x1F0` — expect the
   ally-cluster value (3/7), matching a vanilla Valorian Soldier.
3. **Handle grab.** BP `0x00482510` line ~962 once; read the handle the
   engine assigns, confirm it matches the cObjectManager array tail —
   wire that into `createnpc_engine`'s return so Lua gets an `npcobj`.

---

## C. Files

- Parser/extractor: `sdk/.claude/knowledge/re/npc_templates_extract.py`
  (run: `python sdk/.claude/knowledge/re/npc_templates_extract.py`).
- Stats: `sdk/.claude/knowledge/re/npc_templates_stats.txt` (regenerated).
- Lua: `custom/lua/lib/npc_templates.lua` — `require 'npc_templates'`;
  `M.build(arch, {type=, pos=, name=, level=, group=, sub_id=, link=})`
  → CreateNPC payload bytes; `M.build_hex` for baking/inspection;
  `M.names()`, `M.get(arch)`. The byte output is ready for the proposed
  `sacred.createnpc_engine` host fn (§B.2) — that one C++ addition (not
  in scope to write here; SDK .cpp must not be modified per the task)
  completes the engine-driven path.

No SDK .cpp/.h or existing lua libs were modified. New files only:
`npc_templates_extract.py`, `npc_templates_stats.txt`, `npc_templates.md`,
and `custom/lua/lib/npc_templates.lua`.

---

## Engine-CreateNPC invocation — corrected (crash fix, 2026-05-16)

The §B.2 `createnpc_engine` crashes the process (faults on the call,
manifests as DLL_PROCESS_DETACH right after world-load). **Root cause: a
stack-imbalance from a wrong calling-convention prototype, NOT a bad
context or buffer.** Disasm-proven below. The fix is one extra dummy stack
arg. Context (`0x00AACF80`) and the TLV buffer/cursor logic in §B were
correct.

### 1. The exact call site (disasm, not Ghidra C)

`FUN_00475680` (walker) prologue:
```
00475695  sub esp, 0xc2c
0047569b  push ebx / ebp / esi / edi
004756b2  mov ebx, ecx                 ; EBX = walker's incoming ECX (context)
004756bb  call 0x84a961                ; RTTI/parse-state helper
004756c0  mov ebp, eax                 ; EBP = its return value (a parse-state obj)
```
case-1 dispatch (`switch` jump table at `0x4784c0`, index = tag):
```
00475828  lea ecx, [esp + 0x23c]       ; &local_a0c  = the RECORD BUFFER copy
0047582f  push ebp                     ; >>> stack arg #2 (parse-state obj)
00475830  push ecx                     ; >>> stack arg #1 = record buffer ptr
00475831  mov  ecx, ebx                ; ECX = EBX = context  (this)
00475833  call 0x482510                ; CreateNPC
00475838  mov  eax, 1                  ; (== Ghidra's local_c54=0x475838)
```
Every sibling handler (cases 2/3/8/0x10 …) uses the **identical 4-instr
shape**: `lea ecx,[esp+0x23c]; push <obj>; push ecx; mov ecx,ebx; call`.

**So `FUN_00482510` is `__thiscall` with ECX = context and TWO 4-byte
stack args, in push order: arg1 = record buffer, arg2 = a parse-state
object (EBP).** The §B.0/§B.2 claim of "one stack argument" is WRONG.

### 2. CreateNPC consumes only ECX + arg1; arg2 is ignored; `ret 8`

`FUN_00482510` prologue/epilogue (disasm):
```
00482525  sub esp, 0x6d8
00482534  mov ebp, ecx                 ; EBP(local) = ECX = context
00482603  mov dword [esp+0x18c], 4     ; the CURSOR, init = 4 (Ghidra local_568)
00482685  mov edx, [esp+0x6f8]         ; arg1 (record buffer) — FIRST ref = a READ
0048268c  lea eax, [esp+0x18c]         ; &cursor
00482693  push edx                     ; recordBuffer
00482694  push eax                     ; &cursor
00482695  mov  ecx, ebp                ; ECX = context
0048269b  call 0x472bc0                ; field reader
...
00485a64  ret 8                        ; <<< pops 8 bytes = 2 stack args
```
- Arg1 = `[esp+0x6f8]` = record buffer. First reference is a **read**
  (`00482685`) → genuine input.
- Arg2 = `[esp+0x6fc]`. First reference is a **write**
  (`00483c72  mov byte [esp+0x6fc], 9`) → CreateNPC reuses that arg slot
  as a local state-machine var; **the passed value (EBP) is never read.**
  Pass any dword (0 is fine).
- Cursor self-inits to 4 internally (`00482603`); buffer is passed, not in
  ECX. `FUN_00472bc0(uint* &cursor, int recBuf)`, `in_ECX = context`:
  size guard `if ((uint)*(u16*)(recBuf+2) <= cursor) return 0;`
  (`00472bc0:69`, native-LE u16 @ +2) → §B.1 header `01 00 <size&0xff>`
  with record <256 B is **correct, unchanged**.

`ret 8` is the smoking gun: the SDK typedef
`void(__thiscall*)(void* ctx, void* recBuf)` makes the compiler push only
**4 bytes**; CreateNPC's `ret 8` pops **8** → caller ESP off by +4 → the
SDK host returns to garbage → fault surfacing as DLL_PROCESS_DETACH right
after world-load. Pure ABI/stack bug. Buffer & context were never wrong.

### 3. Context (`in_ECX`) is byte-exactly `exe_base + 0x00AACF80`

All 3 walker call sites (`FUN_0046ba90`, `FUN_0046f9b0` ×2) do, immediately
before `call 0x475680`:
```
0046bd49  mov ecx, 0xaacf80   →  call 0x475680
00470ad3  mov ecx, 0xaacf80   →  call 0x475680
00470b03  mov ecx, 0xaacf80   →  call 0x475680
```
Walker `mov ebx,ecx` then `mov ecx,ebx` into CreateNPC ⇒ **CreateNPC ECX =
0x00AACF80** (no-ASLR → runtime `reb + 0x00AACF80`). Identical to the SDK's
proven `dlgnpc_bind`/`set_npc_name` (`qm = reb + 0x00AACF80`). The field
reader writes scratch to `in_ECX + 0xa460/0xa860/0xa864/0xa868/0xa880`,
NPC array `+0x31c`, DefPos via the same ctx; `CPOS:hero`
(`s_CPOS_hero_0094e998`, `00472bc0:181`) resolves a creature `+0x1c`
into `+0xa860/64/68`. **No extra context priming, cursor seeding, or
"current record" field is needed** — CreateNPC sets its own cursor and
reads the buffer we pass. Confidence **HIGH** (decompile + disasm + the
SDK already drives 3 handlers through `0x00AACF80`).

### 4. Safest correct entry — call FUN_00482510 directly (corrected ABI)

Direct call is correct and minimal — drive CreateNPC, not the walker. The
walker only adds the on-disk copy + jump-table dispatch; CreateNPC already
self-inits its cursor and reads our buffer. Just match the ABI (2 stack
args, `__thiscall`). Driving the walker would additionally require faking
its `local_a0c` copy + the `FUN_0084a961` parse-state object — strictly
more surface, no benefit.

**Corrected, copy-pasteable C (replace the typedef + the `__try` call in
`createnpc_engine`, sdk/player_state.cpp ~808/835; everything else in that
fn — om/arr snapshot, header build, handle diff — stays):**
```c
// FUN_00482510 is __thiscall, ECX = ctx (reb+0x00AACF80), and pushes
// TWO stack args (ret 8): arg1 = TLV record buffer, arg2 = unused scratch
// (engine passes EBP/parse-state; CreateNPC overwrites the slot before
// any read — pass 0). Prototype MUST declare both or the stack unbalances
// (4 pushed vs ret 8) and the SDK host returns into garbage -> crash.
typedef void (__thiscall* fn_createnpc)(void* ctx, void* recBuf,
                                        void* unused_arg2);
...
    __try {
        ((fn_createnpc)(reb + 0x00482510))(ctx, rec, (void*)0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
```
(`ctx = (void*)(reb + 0x00AACF80)`, `rec` = the `01 00 <size&0xff>` +
payload buffer already built at lines 829–834 — unchanged and correct.)

**Handle recovery:** the existing post-call cObjectManager array diff
(`om=*(reb+0x00AD5C40); arr=*(om+4); arr_end=*(om+8)`; new = a `want_type`
creature index absent from the pre-snapshot, scanning from the top) is
sound and needs no change — it just never ran because the call faulted.
Keep it. Alternatively the engine appends the spawn to the NPC array at
`ctx+0x31c` (`00472bc0:144-174` walks `(*(ctx+0x320)-*(ctx+0x31c))/0x34`
entries, stride 0x34) — the tail entry is the freshest; the array diff is
simpler and already coded, prefer it.

### 5. In-game probes & breakpoints

1. **Decisive (~3 min).** With the fixed typedef, call
   `createnpc_engine(T.build('bellevue_enemy',{type=NPC.ORC_WARRIOR,
   pos='CPOS:HERO'}), plen, NPC.ORC_WARRIOR)`. Expect: an Orc spawns on
   the hero, scaled HP, real combat-arts, attacks (matrix-hostile),
   survives multi-hit — i.e. behaves like a vanilla hand-placed Orc.
   Verify with `dump_vanilla_of(ORC_WARRIOR)` vs `npc_field_dump` on the
   returned handle (`+0x1F0/+0x1F4/+0x200/+0x4d4/+0x4d8`). Read
   `sdk/logs/sdk_loaded.log`. **No crash on world-load = the ABI fix is
   the whole story.** If it returns 0 (handle not found) but no crash →
   buffer/size issue (re-check `rec[2]` & template), not ABI.
2. **Guard archetype.** `T.build('friendly_town_guard',
   {type=NPC.VALORIAN_SOLDIER,pos='CPOS:HERO',sub_id=1})` near a monster:
   expect ally defender (attacks monster, never the hero), tanky;
   `+0x1F0` ≈ vanilla Valorian Soldier (3/7).
3. **BP only if the handle grab is unreliable.** One-time software BP at
   `0x00482510` (entry) to confirm ESP balance: step over `call 0x482510`
   from the SDK call site and verify ESP returns to its pre-call value
   (proves the `ret 8` now matches the 2 pushed args). For the cheapest
   handle site, BP `0x0047fxxx`-region is not needed — instead BP the
   array-append in the field-reader path at `0x00472bc0:147`
   (≈ VA where `*(ctx+0x31c)+iVar14` is written) to read the index the
   engine assigns and cross-check the array-diff result. Only needed once
   to validate; the array diff is self-sufficient thereafter.

### Confidence

| claim | conf |
|-------|------|
| Crash = stack imbalance: typedef 1-arg vs `ret 8` (2 args) | **HIGH** (disasm `00485a64 ret 8`; site pushes 2: `0047582f-30`) |
| `FUN_00482510` = `__thiscall`, ECX=ctx, arg1=recBuf, arg2=unused | **HIGH** (disasm; arg2 `[esp+0x6fc]` first-ref is a WRITE `00483c72`) |
| ctx = `reb + 0x00AACF80` | **HIGH** (3 sites `mov ecx,0xaacf80`; SDK already drives it) |
| Fix = add dummy 2nd arg, pass 0; buffer/cursor §B.1 unchanged | **HIGH** (cursor self-init `00482603`; size guard `00472bc0:69`) |
| Direct call preferred over walker | **HIGH** (walker adds only copy+dispatch CreateNPC doesn't need) |
| Existing array-diff handle grab works once call succeeds | **MED** (logic sound; unverified live only because call crashed) |
| `CPOS:HERO` spawns on player | **HIGH** (`00472bc0:181` hero `+0x1c` → `+0xa860/64/68`) |
