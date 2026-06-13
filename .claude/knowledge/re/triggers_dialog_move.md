# Sacred Gold — Triggers / Dialog+Voice / NPC Move+Teleport — RE report

Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000,
no ASLR (file offset == VA − 0x400000). Evidence: Ghidra decompiles in
`sdk/re/ghidra/decompiled/`, the static keyword-resolver tables in the
EXE, and TLV decoding of retail `bin\...\FunkCode.bin` streams.

Builds on `sdk/.claude/knowledge/re/npc_model.md` (CreateNPC tag 0x01 / field
reader `FUN_00472bc0` / record walker). The "record walker" is
**`FUN_00475680`** — a `switch((u16)tag)` dispatcher; each high-level
script keyword compiles to a TLV record whose tag is that switch case.

---

## TL;DR

- **Script keywords → record tag** is a static lookup done by 3 resolver
  functions (compile time): `FUN_00452370` (60 statement/action keywords),
  `FUN_00451be0` (61 more), `FUN_00452910` (quest-block / single-line
  macro keywords `Text:`/`Dialog:`/`Belohnungen:`/`Delay` time units).
  Each is a parallel `names[]` / `opcodes[]` array; the function returns
  `opcodes[i]` for the matched name. **Full table below (HIGH).**
- **Runtime dispatch**: `FUN_00475680` reads each record
  (`tag:u8 size:u16-BE incl-3-hdr`), copies the payload into a scratch
  struct, then `switch(tag)` → a per-tag handler `FUN_004xxxxx`. The
  payload is an opcode stream decoded by `FUN_00472bc0` (returns the wire
  field id: **1**=ASCIIZ name, **0xb**=i32/id, **0x16**=2nd ASCIIZ,
  **2**=type/handle, **3**=u16, **4/0xc/0xd**=POSITION triple
  (`+0xa860`X/`+0xa864`Y/`+0xa868`sector), **0x20**=resolved CPOS:hero,
  **0x38**=NPC-array field, **0**=END). HIGH.
- **Conditions** are control-flow records `IF`(0x3a) / `ELSEIF`(0x42) /
  `ELSE`(0x3b) plus the *predicate* records they wrap
  (`QuestDone`0x0a / `QuestInProgress`0x0c / `GroupIsDead`0x18 /
  `CheckForQuestPool`0x19 / area/kill/talk via `SetBaseTrigger`0x04 +
  `SetOnKill`0x87 / `SetOnCollect`0x88 / `MouseEvent`0x5e / timers
  `SetTimer`0x38). The walker evaluates the predicate, then 0x3a/0x42/
  0x3b skip or run the block (`FUN_00475680` cases 0x3a/0x3b/0x42). HIGH.
- **Dialog** = record `Dialog`(tag 0x1f → `FUN_0048f9e0`); it resolves the
  dialog NPC by name (field 1) or id (field 0xb) in the per-script NPC
  dialog array (`ctx+0x755c`, stride **0x50**), and arms its dialog state
  (`entry+0x4c`, flag word `param_2+0x14` bit 0x80000). The dialog *text*
  is a referenced resource (`res:NNNN` / a `res:NAME`). HIGH.
- **Voice-over** = a separate `PlaySound`(tag 0x68 → `FUN_004a9730`)
  record placed next to the Dialog, whose **field 1 = the voice sample
  name** and **field 0x16 / 2nd field-1 = the target NPC** (`res:NNNN`).
  The handler prepends `"SOUND_FX_"` (str @0x0094f680) if absent, resolves
  the name → numeric sound id via **`FUN_00676170` (getSndType)** against
  the static SOUND_FX table (base **0x00964870**, stride **0x44**, 6869
  entries, id at +0, name at +4), and either plays it directly
  (`FUN_006770e0`+`FUN_00693fe0`, Miles/MSS) or queues a sound command
  (type **0xe**) on the target creature's command ring at
  `cCreature+0x588/+0x58c` (stride 0x44, value at +0x40 = sound id). HIGH.
- **NPC move** = `NPC_Goto`(0x48 → `FUN_0049e210`): resolve NPC by name,
  read destination tile (field 4 → X/Y/sector, or field 0x20 = CPOS),
  then issue a move order via the creature **vtable slot +0x18**
  (`(*(*creature+0x18))(&cmd)`), cmd struct has vtable `PTR_FUN_0089095c`,
  X/Y/sector/mode; in MP/listener mode it instead queues command **type
  1** on the ring `creature+0x588/+0x58c`. `GroupGoto`(0x4a),
  `NPC_TalkTo`(0x47, makes two creatures face/talk + arm dialog) similar.
  HIGH.
- **Teleport** = `Teleport`(0x2e → `FUN_00491d40`): resolve NPC, read dest
  (field 4/0xc/0xd or 0x20). SP path validates tile `FUN_0054d9d0`, sets
  `creature+0x14 |= 0x40000000` ("placed/teleported"), spawns the
  **teleport FX** (`FUN_006224b0`→`FUN_0063d1c0`+`cItemDataMgr_push_
  005fc8b0`, plus the cos/sin dust-ring loop at `LAB_004923ec`); MP path
  queues command **type 9** on the ring (+0x40 X, +0x3c Y, +0x38 sector,
  +0x34 aux). `Teleporter`(0x82) is the trigger-style variant. HIGH.
- **cCreature scripted-command ring**: `+0x588`=begin ptr, `+0x58c`=end
  ptr, element stride **0x44**. Element[0]=command type
  (**1**=MoveTo, **9**=Teleport, **0xe**=PlaySound), then +0x40/+0x3c/
  +0x38/+0x34/+0x30 = type-specific args (see per-handler detail). This
  ring is the single runtime hook point to drive an NPC from a DLL
  without editing FunkCode. HIGH.

---

## (A) Trigger / Event / Condition / Action enumeration

### How the mapping was recovered

`cScriptCompiler_parseStatement_0066fdf0` proves the *bytecode VM* keyword
table is `DAT_00964268` (stride 0x38, count `FUN_00672fb0`==0x18) — that
table is only the low-level VM ops (`exit/nop/ret/cmp/jmp/mov/rand/
callRPC/…`, opcodes 0,1,2,3,4,5,0x10,0x11,0x12,0x20,0x21…0xFF). It is
**not** the storyline statement set.

The storyline statements live in three resolver functions that each hold
a parallel `char* names[]` + `int opcodes[]` and return `opcodes[i]` on a
name match (matcher `FUN_00451ad0`, accepts terminator
` (:{[,.;\0`). The keyword strings sit at 0x0094CE78–0x0094D520.

| Resolver VA | covers |
|---|---|
| `FUN_00452370` | 60 statement/action keywords (CreateNPC…GiveSkill) |
| `FUN_00451be0` | 61 keywords (AddExp…MachVersteck) |
| `FUN_00452910` | quest-block / single-line: `Text:`0x1a `Dialog:`0x1f `Belohnungen:`0x20 `HideTmpToDo:`0x21 `SolveTime:`0x7a `Strafen:`0x7b ; also parses `Delay`/time units Minute/Stunde/Tag/Monat (field 0x38 scaling ×60/×3600/×86400/×~2.5M) |

### Master table: keyword → record tag → runtime handler

Handler column = `FUN_00475680` `switch(tag)` target (the record
interpreter). “evt/cond/act” = role. Confidence HIGH for keyword↔tag
(static tables) and tag↔handler (decompiled dispatch); payload-field
semantics MED unless a handler was decompiled (marked ✓).

| Keyword | tag | handler (FUN_) | role / notes |
|---|---|---|---|
| CreateNPC | 0x01 | 00482510 ✓ | spawn NPC (see npc_model.md) |
| SetObjState | 0x02 | 0048ae90 | act: set object state |
| SetNPCState | 0x03 | 0048bb40 | act: set NPC state |
| SetBaseTrigger | 0x04 | 004819a0 | **register area/base trigger** (the event source) |
| DelBaseTrigger | 0x05 | 004817f0 | remove trigger |
| TalkTo | 0x06 | (default/compile) | start talk (resolved at compile) |
| EquipNPC | 0x07 | 00481d80 | act: equip NPC |
| CreateObj | 0x08 | 00485c10 | act: spawn object |
| StartScript | 0x09 | (default) | run another script block |
| QuestDone | 0x0a | walker case 0x0a ✓ | **cond**: quest finished (scans quest store `DAT_00aabf18` stride 0x124, byte+0x120 &2) |
| QuestNotDone | 0x0b | walker case 0x0b ✓ | **cond**: quest NOT finished |
| QuestInProgress | 0x0c | walker case 0x0c ✓ | **cond**: quest active (bit &4) |
| QuestNotInProgress | 0x0d | walker case 0x0d ✓ | **cond**: quest not active |
| DeleteFunktionBlock | 0x0e | walker case 0x0e ✓ | remove a function/timer block by name (`DAT_00aab708` stride 0x54) |
| ExitQuest | 0x0f | 0048ee20 | act: abort quest |
| MoveOver | 0x10 | (default) | walk-over trigger (compile-time wiring) |
| AddExp | 0x11 | 0048d480 | act: give XP |
| AddGold | 0x12 | 0048da40 | act: give gold |
| AddGems | 0x13 | 0048dd10 | act: give gems |
| TriggerQuest | 0x14 | 0048f030 | **act**: start/advance a quest (the storyline driver) |
| SetUpQuest | 0x15 | 0048f7f0 | act: declare quest |
| CallFunktion | 0x16 | 004813d0 | act: call a named function block |
| DefPos | 0x17 | 00478780 ✓ | declare a named position (DefPos store `+0x334`) |
| GroupIsDead | 0x18 | 0048c860 ✓ | **cond**: monster group dead (returns bool, gates block) |
| CheckForQuestPool | 0x19 | 00490a30 | **cond**: quest-pool availability |
| Text | 0x1a | walker case 0x1a ✓ | dialog/info text record; field 0x7c sets `ctx+0x15` |
| Dialog | 0x1f | 0048f9e0 ✓ | **start dialog** (see part B) |
| Belohnungen | 0x20 | 00490be0 | quest rewards block |
| HideTmpToDo | 0x21 | 00490cc0 | quest UI |
| FillRegion | 0x23 | 0047c610 | act: populate region |
| AddHandel | 0x24 | 0048e4d0 | act: add merchant stock |
| SetRG / UnsetRG | 0x25 / 0x26 | 0048f330 | act: set/clear region gate flag |
| StartPosition | 0x2d | 00491b30 | act: set hero start pos |
| **Teleport** | 0x2e | **00491d40** ✓ | **act: teleport NPC (+FX)** (part C) |
| CreateTrigger | 0x30 | 00496520 | **define a script trigger** (event source) |
| SetTriggerState | 0x31 | 004968a0 | arm/disarm a trigger |
| TriggerPatch | 0x32 | 00496f20 | patch trigger |
| FillSector | 0x33 | 0047c610 | act: populate sector |
| SetHP | 0x34 | 0048e280 | act: set HP |
| QuestBook | 0x35 | 00496080 | act: write quest-log entry |
| LoseQuest | 0x36 | 0048ee20 | act: fail quest |
| DelNPC / DelOBJ | 0x37 | 00497f80 | **act: despawn NPC/object** |
| SetTimer | 0x38 | 004982d0 | **cond/act: timer/delay** (fires block after delay) |
| DelTimer | 0x39 | 004985e0 | cancel timer |
| **IF** | 0x3a | walker case 0x3a ✓ | **conditional**: skip/run wrapped block; spans to matching ELSE/ELSEIF (tag 0x42 sub-pred) / 0x3b |
| ELSE | 0x3b | walker case 0x3b ✓ | conditional else |
| SetButton | 0x3c | 00499ba0 | dialog UI button |
| ChDlgText | 0x3d | walker case 0x3d ✓ | change dialog text |
| QuestKompassPos/Obj | 0x3f/0x40 | 0049a4b0 / 0049ac80 | act: quest compass marker |
| DefNum | 0x41 | 0049b2b0 | declare named number var |
| ELSEIF | 0x42 | walker case 0x42 ✓ | conditional else-if (carries a sub-predicate) |
| SetVar | 0x43 | 0049b2b0 | act: set script var |
| SetVarBit | 0x44 | 0049b840 | **act: set flag bit** (HeroQBit-style) |
| UnsetVarBit | 0x45 | 0049c160 | act: clear flag bit |
| SetFocus | 0x46 | 0049daf0 | act: camera/UI focus on entity |
| **NPC_TalkTo** | 0x47 | **0049dcf0** ✓ | **act: two NPCs converse** (face + arm dialog) |
| **NPC_Goto** | 0x48 | **0049e210** ✓ | **act: NPC walk to point** (part C) |
| SetGroupState | 0x49 | 0049e760 | act: set monster-group AI state |
| GroupGoto | 0x4a | 0049fc50 | act: move a whole group |
| IncVar / DecVar | 0x4b / 0x4c | 0049c930 / 0049cec0 | act: ++/-- var |
| UnTriggerQuest | 0x4d | 0048e600 | act: reset quest trigger |
| FillChest | 0x4e | 004a02a0 | act: fill container |
| Morph | 0x4f | 004a15a0 | act: morph/transform an entity |
| SetST/GS/WI/RP/RM/CH | 0x50–0x55 | (varied) | act: set NPC stat fields |
| SetIcon | 0x56 | 004a1a50 | act: minimap icon |
| SetQuestInfo | 0x57 | 004a6ea0 | act: quest info string |
| SetAnimMode | 0x58 | walker case 0x58 ✓ | act: anim mode (kernel event) |
| SetPlayMode | 0x59 | 004a4040 | act: play/cinematic mode |
| **Wait** | 0x5a | 004a1f20 ✓ | **act: wait** (used to hold for voice/anim) |
| PlayAnim | 0x5b | 004a2550 | **act: play NPC animation** |
| Attack | 0x5c | 004a2b40 | act: force attack |
| ActObj | 0x5d | 004a4310 | act: activate object |
| MouseEvent | 0x5e | 004a6950 | **cond/act: click/use trigger** |
| Popup | 0x5f | 004a7760 | act: popup message |
| ReplaceSpawn | 0x60 | 004a79d0 | act: swap spawn |
| SetMapIcon | 0x61 | 004a81f0 | act: world-map icon |
| SecInfo | 0x62 | 004a8390 | act: sector info |
| Partikel | 0x63 | 004a8bb0 | **act: spawn particle FX** (used around teleports/dialog) |
| SpawnValues | 0x64 | 004a9670 | act: tune spawn params |
| SetCV | 0x67 | 0048df30 | act: set client var |
| **PlaySound** | 0x68 | **004a9730** ✓ | **act: play sound / VOICE** (part B) |
| RndVar | 0x69 | 0049d450 | act: randomize var |
| SetRgnDialog | 0x6a | 00465280 | act: bind region ambient dialog |
| BalanceSpawnItem | 0x6b | walker case 0x6b ✓ | act: spawn balanced item (collects ints to `ctx+0x7684`) |
| AddPoolPos | 0x6c | 004790c0 | act |
| GetPoolPos | 0x6d | 004793d0 | act |
| RenPos | 0x6e | 00478d80 | act: rename position |
| AddPoolRgn/GetPoolRgn | 0x70/0x71 | 0047b480 / 0047b770 | act |
| Waffenpool | 0x72 | walker case 0x72 ✓ | act: weapon pool (reads field2 handle, field 0xb idx) |
| MakeGroup | 0x73 | 004ab940 | act: create monster group |
| RegionChange | 0x74 | 004abb60 | **cond/act: region-enter** transition |
| ActivateQuest | 0x75 | 0048d930 | act: activate quest |
| SetClientVars/GetClientVars | 0x76/0x77 | 0048ff10 / 00490500 | act |
| GameEnd | 0x78 | 006a1660 | act: end game |
| AutoSave | 0x79 | 004ac740 | act: autosave |
| SolveTime | 0x7a | walker case 0x7a ✓ | quest time limit (writes NPC-array+0x18) |
| Strafen | 0x7b | 00491090 | quest penalty block |
| RegionFrei | 0x7c | walker case 0x7c ✓ | act: free region + play sample 0x35 (MSS) |
| Gewinn | 0x7d | 004ac940 | act: win |
| RejectQuest | 0x7e | walker case 0x7e ✓ | act: reject quest |
| PlayMusic | 0x7f | (handler) | **act: play music track** |
| PlayJingle | 0x80 | (handler) | act: play jingle |
| SetScriptSoundControle | 0x81 | (handler) | act: sound control |
| Teleporter | 0x82 | (handler) | **act: teleporter (trigger variant of Teleport)** |
| GameActive | 0x83 | (handler) | cond |
| InfoPlayer | 0x84 | (handler) | act: info to player |
| GetPoolPosition | 0x85 | (handler) | act |
| SetCinemaMode | 0x86 | walker case 0x86 ✓ | act: cinematic camera |
| SetOnKill | 0x87 | (handler) | **event hook: on-NPC-killed** |
| SetOnCollect | 0x88 | (handler) | **event hook: on-item-collected** |
| SetDrop | 0x89 | (handler) | act: set drop |
| GiveStat / GiveSkill | 0x8a / 0x8b | (handler) | act: grant stat/skill |
| MachVersteck | 0x8c | (handler) | act: hide |

(Tags with “(handler)” are dispatched in the FUN_00475680 tail beyond the
slice read; VA recoverable the same way — each `case` is
`FUN_00475680` `local_c50=...; FUN_004xxxxx(); uVar9=1; break;`.)

### Condition / event model (how triggers fire)

There are two trigger families:

1. **Engine event triggers** registered by `SetBaseTrigger`(0x04),
   `CreateTrigger`(0x30) (+ `SetTriggerState`0x31 to arm) and the
   on-event hooks `SetOnKill`(0x87) / `SetOnCollect`(0x88) /
   `MouseEvent`(0x5e) / `RegionChange`(0x74) / `MoveOver`(0x10). These
   bind an area/kill/use/region/walk-over event to a function block;
   when the engine raises the event it re-enters `FUN_00475680` on that
   block. (Trigger string table @0x0094D0E0+ has `Trigger`,
   `TriggerState`, `TriggerType`, `TriggerQuest%d`.)
2. **Inline predicates + IF/ELSEIF/ELSE** (0x3a/0x42/0x3b): the walker
   evaluates a predicate record (`QuestDone`0x0a, `QuestInProgress`0x0c,
   `GroupIsDead`0x18, `CheckForQuestPool`0x19, var compares via the
   bytecode VM, `GameActive`0x83, timer `SetTimer`0x38) and the
   0x3a/0x42/0x3b cases skip the wrapped record span (they re-scan TLV
   headers until the matching ELSE/ENDIF, code at `FUN_00475680`
   lines ~1439–1626). This is the scripted if/else used for
   quest-state / hero-level / have-item gating.

Maximum trigger variety for a modder = the union of the “event hook”
tags (0x04,0x10,0x30,0x31,0x32,0x5e,0x74,0x87,0x88) as the *when*, plus
the predicate/IF tags (0x0a–0x0d,0x18,0x19,0x38,0x3a,0x42,0x3b,0x83) as
the *condition*, plus every act-tagged row as the *do*.

---

## (B) Dialog record + Voice-over binding

### Dialog record (tag 0x1f → `FUN_0048f9e0`)

Payload opcode stream (read by `FUN_00472bc0`):

- field **1** (ASCIIZ) = dialog NPC name / dialog resource key
  (e.g. `"res:17810"`). Handler scans the per-script NPC-dialog array at
  `ctx+0x755c` (end `ctx+0x7560`), **stride 0x50**, matching name; entry
  index `uVar9`.
- field **0xb** (i32) = same target by numeric id (matches
  `*(int*)entry == id`).
- field **0x16** = the dialog content/handle pulled from `param_2+0xc`.

On match the handler:
- writes the dialog handle into `entry+0x4c`
  (`*(u32*)(idx*0x50+0x4c + *(ctx+0x755c)) = *(u32*)(param_2+0xc)`),
- sets dialog-active flag: `param_2+0x14 |= 0x80000` (or clears it for
  entry 0 / “close”),
- calls `FUN_005498f0(idx,1)` (activate) and, in network mode, posts a
  `0x11c` dialog event via `NetworkManager_receive_event_007d8950`.

So a **dialog node** = `{ name/id, content-handle@+0x4c, state bits in
the owning record’s +0x14 (0x80000 = showing) }` inside the script’s NPC
dialog table (`ctx+0x755c`, 0x50-byte entries: `+0`=npc id/handle,
`+0x4c`=current dialog content, `+0x50`-span the rest). The displayed
**text** is a referenced game resource (`res:NNNN` or `res:NAME` — e.g.
`res:HQ_5_6_LOG_TITLE`); text strings are not stored inline in FunkCode,
they are localization resource ids resolved by the resource manager
(`FUN_006725e0`/`FUN_006726f0` seen in the script-resource path).

### Voice-over mechanism (tag 0x68 PlaySound → `FUN_004a9730`)

Voiced lines are produced by emitting a **PlaySound record next to the
Dialog record**. Payload:

- field **1** #1 (ASCIIZ) = the **sound/voice sample name**. If it does
  not begin with `"SOUND_FX_"` the handler logically prepends that prefix
  (compares `s_SOUND_FX__0094f680`). Then
  **`uStack_1dc = FUN_00676170(name)`** (`getSndType`) looks the full
  name up in the static **SOUND_FX table** and returns its numeric id.
  - Table: base **0x00964870**, entry stride **0x44** (0x11 dwords),
    `entry+0` = sound id (i32), `entry+4` = ASCIIZ name; 6869 entries
    spanning 0x00964874…0x009D6908. Voice lines are normal entries
    (e.g. id 16741 `SOUND_FX_HQ_3_3_1_HELF_NPC_AUFTRAG_QSTART`).
- field **1** #2 / field **0x16** (ASCIIZ) = target NPC
  (`res:NNNN`); resolves the creature via `FUN_00465070`/`FUN_00464bd0`.

Playback:
- If a target creature is found, the handler **queues a sound command on
  that creature’s scripted-command ring** (`creature+0x588`/`+0x58c`,
  stride 0x44): element type = **0xe**, `element+0x40` = the sound id.
- Otherwise (no creature / global) it plays immediately through Miles:
  `FUN_006770e0(name,…)` then `FUN_00693fe0(…)` (the MSS path; cf.
  `cMSS_receive_event_0067a7a0`, `ijl15.dll`/`_AIL_*` imports at
  0x008E500B). Sub-strings `"SOUND_FX_"` handling at
  `FUN_004c9d80`/`FUN_004c2b10`.

A `Wait`(0x5a → `FUN_004a1f20`) record after the PlaySound holds the
sequence while the voice plays (it reads an NPC name + duration and
queues a wait), and `Partikel`(0x63)/`PlayAnim`(0x5b) add lip/FX.

**Recipe to reproduce voiced dialog (runtime DLL, no FunkCode edit):**
1. Resolve sound id once: `id = call FUN_00676170("SOUND_FX_<NAME>")`
   (or read the SOUND_FX table directly: scan base 0x00964870 stride
   0x44 for the name, take +0).
2. Show the dialog text resource through the normal dialog path
   (`FUN_0048f9e0`-equivalent: set the NPC dialog-array entry +0x4c =
   text-resource handle, set owner +0x14 |= 0x80000, call
   `FUN_005498f0(idx,1)`).
3. Play the voice: easiest = push a ring command on the speaking
   creature — get `c = *(arr + npc_idx*4)` (om=`*(0x00AD5C40)`,
   arr=`*(om+4)`), then append to the ring at `c+0x588/+0x58c` an
   element (zero 0x44 bytes) with `elem[0]=0xe`, `*(u32*)(elem+0x40)=id`,
   bump `*(c+0x58c)+=0x44` (grow per `FUN_004be490` if full). Or call the
   global Miles path `FUN_006770e0`/`FUN_00693fe0` for non-positional.
4. Optionally hold with a Wait-equivalent until the sample ends.

Confidence: HIGH for the table, getSndType, the ring command type 0xe,
and the SOUND_FX_ prefix; MED for the exact ring-append helper to call
from a DLL (use `FUN_004be490`/`FUN_004b9900` as the engine does, or BP
`FUN_004a9730:line 239-241` to capture the exact append).

---

## (C) NPC scripted movement / teleport + Alcalata case study

### Shared mechanism — cCreature command ring

`cCreature+0x588` = ring begin ptr, `cCreature+0x58c` = ring end ptr,
element **stride 0x44**. Engine grows it with `FUN_004be490` (reserve) /
`FUN_004b9900` (shift). Element layout (offsets within the 0x44 block):

| type@+0 | meaning | +0x40 | +0x3c | +0x38 | +0x34 | +0x30 |
|---|---|---|---|---|---|---|
| **1** | MoveTo (NPC_Goto) | X | Y | sector | — | run/walk mode |
| **9** | Teleport | X | Y | sector | aux handle | — |
| **0xe** | PlaySound/Voice | sound id | — | — | — | — |

(seen identically in `FUN_0049e210` MP branch, `FUN_00491d40` MP branch,
`FUN_004a9730` creature branch — the offsets are written relative to the
ring tail `(*(c+0x58c)-iVar)/0x44*0x44 + (-0x44+...)`.)

In single-player the handlers instead drive the creature directly:
- **NPC_Goto** (`FUN_0049e210`): builds a move-order struct (vtable
  `&PTR_FUN_0089095c`, fields: +X +Y +sector +mode `iStack_118`) and
  calls the creature **vtable method at +0x18**:
  `(**(code**)(*piVar5+0x18))(&cmd)`. It first stops the current action
  (`vtable+0x28`) and resets speed (`vtable+0x2c`, `FUN_005467a0`).
  Fields: field 1 = NPC name; field 4 = dest via `FUN_006224b0` →
  X(`uStack_13c`)/Y(`local_138`)/sector(`unaff_EBX`); field 0x20 =
  CPOS:hero coords; field 0x66 sets mode = 0x1000000 (run vs walk).
- **Teleport** (`FUN_00491d40`): field 1 = NPC; fields 4/0xc/0xd or 0x20
  = dest tile (X `+0xa860`, Y `+0xa864`, sector `+0xa868`); field 3 =
  aux (`iStack_1a0`). SP path: `FUN_0054d9d0(x,y,sec,1)` validates the
  destination; on success `creature+0x14 |= 0x40000000`
  (placed/teleported bit), then **teleport FX**: `FUN_006224b0` →
  `FUN_0063d1c0` + `cItemDataMgr_push_005fc8b0(creature+0xc)`, and the
  swirl placement loop at `LAB_004923ec` (random angle, `fcos`/`fsin`
  ring of FX objects around the target via `cObjectManager_getData_
  00603e30`). On invalid tile it retries with random ±3/±6 offsets
  (logs `s_Teleport_failed__vx__d_..._0094efe4`). MP path: ring
  command type 9.
- **Despawn / vanish** = `DelNPC`(0x37 → `FUN_00497f80`); a “teleport
  away then delete” is the common vanilla idiom (Teleport to an
  off-screen DefPos, then DelNPC) — see Alcalata below.
- **NPC_TalkTo** (0x47 → `FUN_0049dcf0`): two NPC names, arms the dialog
  array entry, calls `FUN_0054cbb0(creature,1/0)` to make both creatures
  face/talk, stops their actions.

### Alcalata the Wise — worked example

Name string in EXE: `SOUND_FX_HQ_0_1_NPC_KOMMENTAR_ALCALATA1`
@0x009C4054 (short form `HQ_0_1_NPC_KOMMENTAR_ALCALATA1` @0x009C405D —
proves the `SOUND_FX_` prefix rule). DefPos key `posAlcalata81`.

Retail evidence — `bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin` @~1810537
(clean TLV chain), and `bin\NetScript\FunkCode.bin` @~98721:

Spawn + teleport block (VAMPIRELADY @1810557+):
```
@1810557 0x3A IF        "I 01 00 00 00" "81"      ; gate (quest/state cond, map ctx "81")
@1810569 0x2E Teleport  1:"res:18104"  4:-2 "posZIELTeleportDämon74"   ; teleport NPC to DefPos
@1810644 0x2E Teleport  1:"Res:18237"  4:-2 "posThronsaal_Shareefa"    ; teleport to throne room
@1810726 0x01 CreateNPC 1:"res:17309" 2:type 4:-2 "posAlcalata81" 0x6b…; spawn Alcalata at DefPos
```
Voiced-dialog block (NetScript @98942+):
```
@98942 0x68 PlaySound  1:"SOUND_FX_THUNDER01"                1:"res:17810"  ; ambient SFX on NPC
@98977 0x68 PlaySound  1:"HQ_0_1_NPC_KOMMENTAR_ALCALATA1"    1:"res:17810"  ; ALCALATA VOICE-OVER
@99024 0x5A Wait       1:"res:17810" 1:"res:17810" <i32>                    ; hold for the line
@99055 0x63 Partikel   … "res:17810" …                                     ; cast/teleport FX
… (earlier: 0x1f Dialog "res:17810" sets the on-screen text; 0x46 SetFocus
  "res:17810" aims the camera; 0x35 QuestBook writes the log)
```
So Alcalata teleports in via `Teleport`(0x2e) to a `posAlcalata81`/
`posThronsaal_Shareefa` DefPos with the standard `FUN_00491d40` FX swirl,
speaks via a `PlaySound`(0x68) voice line (`SOUND_FX_HQ_0_1_NPC_
KOMMENTAR_ALCALATA1`) bound to dialog NPC `res:17810` with the text shown
by `Dialog`(0x1f), and the sequence is paced by `Wait`(0x5a). To make her
vanish the script teleports her to an unused DefPos and/or `DelNPC`(0x37).

**Runtime recipe (DLL):** resolve creature → for move: build the
move-order struct and call `creature.vtbl[0x18]` (or push ring type 1);
for teleport: call `FUN_0054d9d0(x,y,sec,1)`, set `+0x14|=0x40000000`,
then trigger the FX via the `FUN_006224b0/FUN_0063d1c0` path (or push
ring type 9 and let the engine do FX); for voice: ring type 0xe with the
SOUND_FX id (part B). All three avoid editing the FunkCode stream.

---

## Open items needing a runtime breakpoint

1. Exact predicate→bool for each condition tag (BP each `FUN_00475680`
   case 0x0a/0x0c/0x18/0x38 and read `param_2` payload + return).
2. The precise DLL-callable ring-append helper (BP `FUN_004a9730`
   ~0x4a98xx and `FUN_0049e210`/`FUN_00491d40` MP branch to capture the
   `FUN_004be490`/`FUN_004b9900` arg convention).
3. Confirm `cCreature+0x588/+0x58c` ring offsets on a live creature
   (om=`*(0x00AD5C40)`, arr=`*(om+4)`, c=`*(arr+idx*4)`), and the
   move-order vtable index +0x18 / stop +0x28 / speed +0x2c.
4. The Dialog `param_2+0xc` content handle origin (which prior record
   loads the text resource id) — BP `FUN_0048f9e0` entry.
5. Tags 0x7f/0x80/0x82/0x83/0x84/0x85/0x87/0x88/0x89/0x8a/0x8b/0x8c
   handler VAs (read the FUN_00475680 tail; same `case` pattern).
