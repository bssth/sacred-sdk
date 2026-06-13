# Sacred Gold SDK — Reverse-Engineering backlog / roadmap

Survey date 2026-06-13. Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`,
base `0x00400000`, no ASLR (**file offset = VA − 0x400000**), x86 LE.
Corpus: **298** Ghidra C decompiles in `sdk/re/ghidra/decompiled/*.c`,
**30** RE reports in `sdk/.claude/knowledge/re/*.md`, 25 numbered docs in `sdk/docs/`.

This file is the single prioritized RE roadmap. It is split into:
(1) SOLVED — what is locked, with the key offsets/functions.
(2) OPEN — ranked targets, each with the concrete next experiment.
(3) CORPUS GAPS — referenced-but-not-yet-decompiled functions to add.

`qm` = `cQuestMgr` singleton = base + `0x00AACF80` (the OBJECT, ECX in all
script handlers). `OBJMGR` = `*(0x00AD5C40)`; creature resolve =
`om=*(0x00AD5C40); arr=*(om+4); c=*(arr+handle*4)`.

---

## (1) SOLVED — locked knowledge (do not re-derive)

### S1. Talk signal (THE blocker — SOLVED, live 4/4)
`cCreature+0x200` bit `0x400` pulses on every player talk/interact with that
NPC. Pollable for ANY bound NPC, no HW-BP/hooks. SDK: `o:on_talk(fn)` /
`o:in_dialog()` (rising-edge). Q1 talk-advance confirmed end-to-end.
Source: memory `talk-signal-0x200`, `scratch/talk_trigger.md`.
Caveat: validated for friendly/quest NPCs; for hostile NPCs bit `0x400`
may instead reflect attack-targeting — engine setter not yet named.

### S2. FunkCode record system (the script ABI)
- Record walker `FUN_00475680`: `__thiscall` ECX=qm, 6 stack args, `ret 0x18`.
  `switch((u16)tag)` → per-tag handler. Jumptable @ `0x004784C0`.
  `param_4` dyn-cast to cCreature* at prologue `0x004756c0` (param_4=0 → no-op).
- TLV framing: `tag:u8, size:u16 BIG-ENDIAN (incl 3-byte hdr), payload`.
- Field reader `FUN_00472bc0`: returns wire field id — **1**=ASCIIZ name,
  **0xb**=i32/id, **0x16**=2nd ASCIIZ, **2**=type/handle, **3**=u16,
  **4/0xc/0xd**=POSITION triple (`qm+0xa860`X/`+0xa864`Y/`+0xa868`sector),
  **0x20**=resolved CPOS:hero, **0**=END.
- Full **keyword→tag→handler** master table (≈140 tags) in
  `scratch/triggers_dialog_move.md §A`. Compile-time resolvers:
  `FUN_00452370`/`FUN_00451be0`/`FUN_00452910`.
- Native bake+fs_override pipeline works (`lua_bake.cpp`, `fs_override.cpp`):
  name-keyed dialog/trigger records appendable; mid-stream CreateNPC append
  is structurally dead (breaks 0x42/0x3b byte-relative jumps).

### S3. NPC spawn + combat init (DEFINITIVE — `scratch/combat_init.md`)
- Spawn via engine `CreateNPC` (`FUN_00482510`) from `npc_templates.lua`.
- Make a bare spawn a real combatant: set `+0x24`=level, `FUN_0052e420(1,3)`
  (sets `+0x1F0` AI/faction class; **3** or 7 = ally cluster), `+0x1F4=0x9`
  (bit0 awake + bit3 `0x8` real-side — THE proactive-aggro fix),
  `WakeUp FUN_0059f580`, `+0x200=0x40200000`.
- Hostility = 16×16 byte matrix @ `0x00890A30` (restored from scrambled
  `0x008EB548`), indexed `[A.+0x1F0*16 + B.+0x1F0]`. **Polarity: byte 0x00 =
  A ATTACKS B, 0x01 = ignore.** Player/ally cluster = {1,3,4,7};
  monster = {2,5,8,9,10,11,12,14,15}; class 13 = universal non-combatant.
- HP: `+0x4d8`=cur, `+0x4d4`=max; `+0xfc==9` dead, `+0x150==6` disabled.
  Scaled by `FUN_0052ab70` once active (don't poke `+0x4d8` alone — clamped).
- Detection radius = hardcoded ~800 world units (`FUN_00542b20:469`), NOT
  per-class. Picker `FUN_00542b20` runs only at AI state `+0xfc==0`.

### S4. global.res on-disk format (byte-exact, 23123 slots verified)
Index (16B/rec: `d0,ident,off,pad`) then UTF-16-LE blob. `ident =
sacred_hash(name)&0x7fffffff`, sorted ascending. Length of slot k lives in
`d0[k+1]>>1`; `off` absolute. Resolver `FUN_0080f5e0` (binary search), loader
`FUN_0080e680`. `sacred_hash` = MUL `0x71`, MOD `0x3b9ac9f7`. Full emit spec
in `scratch/globalres_format.md §4`. **text.lua rebuild is byte-exact.**

### S5. Renderer-side resource resolver `FUN_006726f0(handle)` — TWO modes
- `handle & 0x80000000` set → `FUN_0080eaf0(handle&0x7fffffff)` = direct
  global.res lookup, low 31 bits used AS the sacred_hash.
- high bit clear → `FUN_0080e780(handle)` = id→string→sacred_hash→global.res
  (`FUN_0080e780` hashes with the exact `sacred_hash`). **Negative result
  proven: the DlgNPC `entry+0x4c` content handle is NOT routed through this
  resolver** → no global.res-only trick can feed runtime-NPC dialog text.

### S6. Quest journal / marker / completion (HIGH — `scratch/quest_lifecycle.md`)
- Registry: `*(0x00AAD3A4)`=begin / `*(0x00AAD3A8)`=end, stride `0x174`,
  key=quest_id u32 @ `entry+0x08`. (Mirrors `qm+0x424/+0x428`.)
- `entry+0x00`: **3 = MAIN/story, 4 = SIDE** (drives journal AND world-map
  icon sprite `0x8A`/`0x8B` + colour, re-read at draw by `FUN_004974b0`/
  `FUN_004972e0`). `entry+0x04==2` required for world-map icon list membership.
- Per-class marker table `qm+0x3a0` stride 8 (slot1=`+0x3a4`, slot3 single-
  target `qm+0x7704/+0x7708/+0x7718`). Resolver `FUN_004a5980(slot,...)`.
- Objective text: `entry+0x24`=title (render gate), `+0x28`=counter,
  `+0x2C..+0x4C`=10-slot sub-step handle list, `+0x16C`=tab page. Mutated by
  tag-0x35 `FUN_00496080`. Complete = `entry+0x00=3`-styling or tag-0x4d
  `FUN_0048e600` removal. `qb_resolve` = `(uint(__cdecl*)(const char*))0x00672740`
  (returns `hash|0x80000000`).
- Reward has NO engine quest-entry field — must be script/trigger-driven.

### S7. Items / loot / equip (HIGH — `scratch/items.md`, `items_equip.md`)
- **Items and creatures share ONE create path and ONE type-id space.** Spawn
  an item = the creature spawn recipe with an item type id.
- Create by type+pos: `cObjectManager_create_005fba40(ECX=om,&posBuf,type,
  doNet,sendFlag,0)`; posless: `_005fb530(type,a3,a4)`. Def table:
  `FUN_00425ea0(id)=id*0x80+base`, 128B records, base `DAT_00aab5e4`, class
  discriminator byte `def+0x2E`.
- Name table: **5624 `TYPE_*` entries @ `0x008EC328` stride 0x44** (`+0`=id
  (NOT row index), `+4`=ASCIIZ name). Resolver `FUN_0043cd90(name)`.
  Generated to `custom/lua/lib/items_gen.lua`.
- Equip: `cEngine_creature_equipItem_00611560(ECX=engine,slot,creatureRef,
  typeName)` (create+equip+visual+net). Slot store `cCreature+0x1A4` stride 4;
  slot 0x12=mount, weapon hand ≈ 0xC/0xD. Drop: `cEngine_dropItem_00611620`.
- Item instance layout (Weapon.pak `SacredItem`): ItemLevel, requirements,
  Phys/Fire/Magic/Poison dmg+res, 8×(BonusID,BonusValue) modifiers. Offsets
  on the live `cItemBase` not yet byte-verified (see O5).

### S8. Voice / move / teleport (HIGH — `scratch/triggers_dialog_move.md §B,§C`)
- cCreature scripted-command ring: `+0x588`=begin, `+0x58c`=end, stride 0x44.
  type@+0: **1**=MoveTo, **9**=Teleport, **0xe**=PlaySound. The single runtime
  hook point to drive an NPC without editing FunkCode.
- SOUND_FX table @ `0x00964870` stride 0x44 (6869 entries, id@+0, name@+4).
  Resolver `getSndType FUN_00676170`. `"SOUND_FX_"` prefix auto-prepended.
- Move = vtable slot `+0x18` (SP) or ring type 1 (MP). Teleport SP =
  `FUN_0054d9d0` validate + `+0x14|=0x40000000` + FX swirl.

---

## (2) OPEN — ranked high-value targets (impact vs effort)

Ranking = modding impact ÷ effort. Each has the concrete next experiment.

### O1. Dialog-text store populator — RANK 1 (high impact, LOW remaining effort)
**The #1 polish blocker. Mechanism now fully decoded statically; only the
write-call needs a live confirm.** Native dialog text for a runtime NPC.

What is known (and NEWLY pinned this survey from the `FUN_00465690`
serializer decompile, lines 708-730 / 3097-3114):
- **Content table** `qm+0x765c`(begin)/`+0x7660`(end) stride **0x48**,
  name-keyed, handle @ `entry+0x44`. Resolver/allocator =
  **`FUN_00465070(ECX=qm, char* name, char alloc)`** (verified decompile):
  scans by name → returns `entry+0x44`; on miss with `alloc!=0` mints an id
  in **0x2076..0x20cf**, registers via `FUN_0066ef40`+`FUN_00464760`.
- **Runtime dialogText store** (save-section "dialogText"): the string/blob
  at **`qm+0x79f4`** (serialized as a `0x2000`-stride element block) and a
  growable u32 vector **`qm+0x99f4`(begin)/`+0x99f8`(end)**. The save/load
  serializer `FUN_00465690` (4347 L, a vtable-visitor: `param_1`=stream,
  `vtbl[+4]`=write, `vtbl[+8]`=read) **grows that vector with
  `FUN_006afd20()`** (line 719: `*(qm+0x99f8)=FUN_006afd20()`) and on load
  reads `count=(0x99f8-0x99f4)>>2` u32 elements into `*(qm+0x99f4)+i*4`
  (lines 3101-3114). The small content handle (`entry+0x4c` ~0x13F, allocator
  0x2076..) indexes THIS store.
- DlgNPC array `qm+0x755c`/`+0x7560` stride **0x50**, `entry+0x4c`=active
  content handle, `entry+0x14` owner bit `0x80000`="showing". Setter
  `FUN_00465220(idx,handle)`. Show/apply = `FUN_00461540` (1233 L).
- tag-0x03 handler `FUN_0048bb40` (the dialog-by-name processor) resolves
  name→handle via `FUN_00465070` (case 1, when content 0/-1) then
  `FUN_00461540`. tag-0x1f `FUN_0048f9e0` = v2 variant.

**Concrete next experiment (read-only LIVE PROBE, then implement):**
1. Decompile **`FUN_006afd20`** (NOT in corpus — see G1) to learn the exact
   `qm+0x99f4` vector-grow ABI (it returns the new end ptr). With `FUN_00465690`
   lines 715-721 as the call template, this likely pins the populator fully
   statically and may skip the BP.
2. If still ambiguous: hook `FUN_00465070` (log ECX=qm, name, alloc, ret) and
   `FUN_0048bb40` entry; BP the store writer around `FUN_006afd20`/
   `FUN_00461540`. Play a VANILLA scripted dialog vs talk to our NPC, compare —
   captures real handle values + the text-insert call.
3. Implement: (a) `h=FUN_00465070(qm,name,1)` mint, (b) populate the
   dialogText store at `qm+0x99f4` under `h` with our UTF-16 text (the call
   `FUN_006afd20` reveals), (c) `FUN_00465220(idx,h)` + `entry+0x14|=0x80000`
   + `FUN_005498f0(idx,1)`.
Fallback: emit a tag-0x03 record via the baker (`03 00 01 "res:NAME" 00 <btn>`,
template @ `dialog_native_roadmap.md` step 1) and dispatch on talk via
`FUN_00475680` direct call or `0xAAB708` trigger registration.
Source: `scratch/dialog_native_roadmap.md`, `triggers_dialog_move.md §B`.

### O2. Companion roster panel (`qm+0x31c` array) — RANK 2 (high impact, MED effort)
Companions follow/fight but don't show portraits in the right-side panel.
The follow/fight bits (`+0x1F4&0x4`, `+0x251`) are independent of roster
membership — the panel is driven by the cQuestMgr quest-NPC array.

Known (HIGH — `quest_lifecycle.md §Q2`): registry `qm+0x31c`(begin)/`+0x320`
(end) stride **0x34**; each entry has a std::vector of 0x2c-byte member
sub-records @ `entry+0x1c/+0x20/+0x24`; `entry+0x14!=0`=live. Creature
back-ptr `cCreature+0x94`(i16)=array index, `+0x96`(i16)=quest id.
**Add call `FUN_00450C50`** (`__thiscall` ECX=cCreature, arg=i16 idx) =
single store `*(i16)(cCreature+0x94)=idx` (rest is MP net broadcast, skipped
SP). Vanilla path = `FUN_00482510:1657/1725`: scan slot, `+0x200|=0x200`,
`FUN_00450C50`, push 0x2c sub-record `{[0]=handle,[1]=0x100,[2]=i16 idx}` via
`FUN_004B82E0`.

**Concrete next experiment:** HW-exec BP `0x00450CB6` (`mov [edi+0x94],bp`)
during a vanilla escort quest where an NPC joins → dumps ECX(creature) +
bp(idx), proves the panel-add path and which array slot to reuse. Then
implement the 5-step ADD recipe (`quest_lifecycle.md §Q2`). The on-screen
renderer VA (which UI fn walks `+0x31c` to draw faces) is the only MED gap —
the BP confirms the registry is the source.

### O3. Save/load serializer `FUN_00465690` — RANK 3 (HIGH impact, MED effort)
**Single most under-documented high-value function.** 4347 L, the unified
per-qm save/load visitor. Owns dialogText (O1), openQuests, DefPos store
(`qm+0x334/+0x338` stride 100), DlgNPC array (`+0x755c` stride 0x50), content
table (`+0x765c` stride 0x48), and the `qm+0x9a14` 0xa00-byte block. It is a
**vtable visitor**: `param_1`=stream, `vtbl[+4]`=write leaf, `vtbl[+8]`=read
leaf — every field is `(**(vtbl+4/8))(ptr,size)`.

Why it matters: it is the authoritative struct map for the entire scripted-
world state, AND the gate for persisting any SDK-injected dialog/quest data
across save/load (today our runtime mods are re-applied each launch and do
NOT survive a vanilla save→reload).

**Concrete next experiment:** annotate `FUN_00465690` fully — walk each
`(**vtbl+4)(ptr,size)` pair, record `(qm offset, element size, count
expression)` into a struct map. Cross-check the read path (`vtbl+8`, the
second half ~line 2900+) parses identically. Decompile `FUN_006afd20`,
`FUN_00608080`, `FUN_004bb380` (the vector/array helpers it calls). Deliver a
per-qm save-section table. No BP needed (pure static); BP `0x00465690` once
on a manual save only to confirm `param_1` vtbl identity.

### O4. Hostile-NPC talk-bit semantics — RANK 4 (MED impact, LOW effort)
S1's `+0x200 & 0x400` is validated only for friendly/quest NPCs. For hostile
NPCs the same bit may reflect attack-targeting. Find the engine setter of
`+0x200 |= 0x400` to name it precisely (so `o:on_talk` is safe on any NPC).
**Experiment:** XrefsTo writes of `cCreature+0x200` bit 0x400; or HW-write BP
on a friendly NPC's `+0x200` during talk vs a hostile NPC during attack,
compare the faulting EIP. Cheap, closes the one S1 caveat.

### O5. Item-instance property/bonus offsets — RANK 5 (MED impact, MED effort)
To expose item stats (ItemLevel, requirements, 8×bonus modifiers) to Lua, the
Weapon.pak `SacredItem` layout must be mapped onto the live `cItemBase`
object. Static gives the def-table (S7) but the instance offsets are
unverified for this build.
**Experiment:** BP after `cObjectManager_create_005fb530` (a known item
create), dump the new object; correlate against the `SacredItem` field order.
Also resolve `def+0x2E` class-byte → object-class map (BP `005fb530:89` for a
known creature vs weapon).

### O6. Per-class character-definition tables — RANK 6 (HIGH impact IF class
mod pursued, HIGH effort). A 9th class is infeasible (8-bit class mask fully
used — `class_mod_feasibility.md`), but TOTAL-CONVERSION of an existing slot
is feasible. The missing data: per-class start attributes, skill list, combat-
art roster, level-up curves. **NOT in Balance.bin** (global only).
**Experiment:** locate the table keyed by class id (`heroBase+0x10`, 1..9).
Search Sacred.exe data near where char-create reads class id; or a typed
`.res`. Find via XrefsTo the class-id read at hero spawn. Large scope —
schedule only if the user commits to a class conversion.

### O7. Combat-arts / spell system — RANK 7 (MED impact, HIGH effort, UNMAPPED)
No dedicated RE doc exists. `SPELL` = 44 entries in the TYPE table (S7).
Combat-art gating is part of the 8-bit class mask. Entirely un-RE'd: how a
combat art is cast, its rune/level model, and the per-creature CA inventory.
**Experiment (scoping only):** XrefsTo the `SPELL`-class def records; find the
cast entrypoint (likely a tag in the FunkCode set or a hero-input handler).
Defer until a mod needs it — currently lowest ROI.

### O8. Network / multiplayer ABI — RANK 8 (LOW impact for SP modding)
Only incidental references (`NetworkManager_receive_event_007d8950`, event ids
0x11c dialog / 0x140 drop / 0x173 roster / 0x11e equip). All SDK recipes
deliberately take the SP branch and skip net broadcasts. Document the event-id
table only if MP support becomes a goal. **Lowest priority.**

---

## (3) CORPUS GAPS — referenced funcs NOT yet decompiled (add to corpus)

These appear in scratch reports / other decompiles but have no
`<va>_FUN_<va>.c` in `sdk/re/ghidra/decompiled/`. Add via Ghidra export.
Ranked by the open target they unblock.

| VA | Unblocks | Why needed |
|---|---|---|
| **`FUN_006afd20`** | **O1, O3** | dialogText vector grow @ `qm+0x99f4` — THE store populator. Called at `FUN_00465690:719`. Decompiling it likely fully solves O1 statically. **HIGHEST.** |
| **`FUN_0059f580`** | S3/O5 | `cCreature_WakeUp` — used in every combat recipe; only described, never decompiled. |
| **`FUN_005fba40`** | S7/O5 | `cObjectManager_create` (create-by-type+pos) — the core spawn/item create; described from callers only. |
| **`FUN_005fb530`** | S7/O5 | `cObjectManager_create` (posless) — item/inventory create path. |
| **`FUN_00611560`** | S7/O5 | `cEngine_creature_equipItem` — equip ABI used by `npc_equip`. |
| **`FUN_00611620`** | S7 | `cEngine_dropItem` — ground-loot drop. |
| **`FUN_0056b130`** | dialog | per-creature dialog driver (REFUTED as talk handler but still the conv-state owner; `[cre+0x152]` FSM). Prologue `6A FF 68` confirmed. |
| **`FUN_0066ef40`** | O1 | name→id registrar used by `FUN_00465070` allocator + `FUN_00465690:710`. |
| **`FUN_00464760`** | O1 | content-table insert (called by `FUN_00465070:108`). |
| `FUN_00608080` | O3 | array/vector helper in serializer (`:728`). |
| `FUN_004bb380` | O3 | array helper in serializer (`:755`). |
| `FUN_00450C50` | O2 | roster-add (`mov [edi+0x94],bp`) — capstone-known, decompile for completeness. |
| `FUN_004B82E0` | O2 | sub-record vector grow for roster push. |
| `FUN_00542b20` PRESENT but verify | S3 | proactive picker — IS in corpus; listed only to confirm it covers the `:469` range gate. |
| `FUN_005f6290` | (cited in task) | referenced but origin unclear — locate its caller and classify before adding. |

Note: `FUN_00465070`, `FUN_00465690`, `FUN_00465220`, `FUN_0048bb40`,
`FUN_00461540`, `FUN_0048f9e0`, `FUN_00475680`, `FUN_006726f0`, `FUN_0080e780`,
`FUN_0080eaf0`, `FUN_0080e680`, `FUN_0080f5e0`, `FUN_00472bc0`, `FUN_00482510`,
`FUN_0052ab70`, `FUN_00539700`, `FUN_00423580`, `FUN_0052e420`, `FUN_0043adc0`,
`FUN_0043cd90`, `FUN_00463240`, `FUN_0044a1c0` ARE already in the corpus.

---

## C++-port / refactor candidates (concrete)

Reusable algorithms worth porting to clean C++ in the SDK (not just hooks):

1. **`sacred_hash`** (MUL 0x71, MOD 0x3b9ac9f7, signed-mod dance) — already
   ported in `text.lua`; lift to a C++ util shared by global.res + content
   table + sound table. (S4/S5)
2. **global.res index+blob emitter** — full byte-exact spec in
   `globalres_format.md §4`. TODO(port): replace text.lua's stale span-copy
   model with the re-derive emitter (handles appends, not just in-place).
3. **DlgNPC content-handle resolver** — port `FUN_00465070`'s name→handle
   scan + 0x2076..0x20cf allocator as a C++ helper once `FUN_006afd20` is
   known (O1). TODO(port).
4. **Hostility matrix lookup** — the 16×16 @ `0x00890A30` + the
   `FUN_00423580` predicate (incl hero-redirect) as a pure C++ function for
   faction queries. (S3) TODO(port).
5. **cCreature command-ring append** (type 1/9/0xe) — a single C++ helper to
   drive move/teleport/voice without FunkCode. Needs the `FUN_004be490`/
   `FUN_004b9900` grow ABI confirmed by BP. (S8) TODO(port).
6. **Quest-entry struct (0x174)** + journal field writers — a typed C++
   wrapper over the `*(0x00AAD3A4)` registry (marker/objective/completion).
   (S6) TODO(port).
7. **Item TYPE catalog walker** (`0x008EC328` stride 0x44, 5624 entries) —
   already generated to Lua; a C++ `item_type_id(name)` mirroring
   `FUN_0043cd90` avoids the runtime call. (S7) TODO(port).
8. **Per-qm save-section serializer map** — once O3/`FUN_00465690` is
   annotated, a C++ struct + read/write visitor enabling SDK state to persist
   across saves. TODO(port). Largest, highest-value port.

---

## Suggested next-push order
1. **O1** (dialog text) — decompile `FUN_006afd20` first; likely closes it.
2. **O2** (roster panel) — one BP, recipe already written.
3. **O3** (serializer map) — unlocks O1 persistence + the big C++ port #8.
4. **O4** (hostile talk bit) — cheap S1 caveat close.
5. O5 (item instance), then O6/O7/O8 only on demand.

**Concrete C++-port/refactor candidates catalogued: 8** (3 partially done, 5
TODO(port)-tagged). **Corpus gaps to add: 15 functions** (4 critical: 006afd20,
0059f580, 005fba40, 005fb530).
