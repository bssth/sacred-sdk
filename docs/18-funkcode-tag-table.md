# FunkCode TAG → Subsystem reference table

Source of truth: `sdk/tools/funkcode_tags.py` (importable as Python).

The tag byte (first byte of every record header) selects which game subsystem
processes that record. The outer walker `FUN_00475680` does the dispatch.
Subsystems then call the bytecode interpreter `FUN_00472bc0` in a loop and
handle the returned opcodes per their own action vocabulary.

Coverage: **79 of 112 tags meaningfully labelled (71 %)**. The remaining
33 are smaller bodies that didn't yield distinctive strings on first pass.

## High-frequency tags (top 30 by usage in TYPE_NPC_GLADIATOR FunkCode.bin)

| count | tag | subsystem | meaning |
|---:|:---:|---|---|
| 22,762 | `0x73` | **StatementBuild** | accumulator: gathers parameters into DAT_00ab7854 list (base building block) |
| 11,498 | `0x64` | **SectorPosOp** | tile-coord op (uses `DAT_0094e08c << 6` shift) |
|  6,775 | `0x08` | **CreateOBJ** | spawn object: `'CreateOBJ: %s mehrfach -> %s ResNum(0x%X)'` |
|  6,111 | `0x01` | **CreateNPC** | spawn NPC: `'CreateNPC failed: vx=%d, Type=%d'` |
|  5,401 | `0x42` | **BlockReader** | reads u16 size, copies block bytes — IF/THEN body wrapper |
|  5,309 | `0x33` | **BigSubsys_33_cleanup** | shares case body with 0x23 |
|  4,845 | `0x43` | **AtmoRegion_b** | aliases AtmoRegion_a |
|  4,514 | `0x35` | **QuestLogSet** | sets quest log entry text reference (LOG_TITEL/OFFEN/ZIEL) |
|  4,323 | `0x1a` | **InlineHandler_1a** | inline pool/quest-state setup |
|  3,218 | `0x3c` | **ResRef** | `'RES:%d'` resource reference |
|  2,762 | `0x3a` | **ConditionalEval** | `'if IsDead:%s'`, `(true)/(false)` — branch evaluator |
|  2,738 | `0x3b` | **ELSE_jump** | `'ELSE mit offenem Ende'` — control-flow ELSE / forward skip with u16 offset |
|  2,587 | `0x03` | **DialogShow_v1** | dialog display variant 1 |
|  2,577 | `0x3e` | (unmapped) | absent from current dispatch dict |
|  2,063 | `0x3f` | **QuestKompassPos** | quest-compass marker position |
|  1,974 | `0x16` | **ObjMgrOp_16** | cObjectManager_getData |
|  1,870 | `0x76` | **Subsys_76** | quest-state-related (calls QuestStateA family) |
|  1,870 | `0x1f` | **DialogShow_v2** | dialog display variant 2 |
|  1,870 | `0x7a` | **NPC_FieldSet** | sets field +0x18 in NPC array entry — NPC param mutator |
|  1,870 | `0x20` | **Subsys_20** | 64 lines, unmapped purpose |
|  1,870 | `0x7b` | **Subsys_7b** | 64 lines |
|  1,870 | `0x21` | **HideTmpToDo** | `'HideTmpToDo: Pool=%d, ToDo=%d'` |
|  1,806 | `0x56` | **DialogShow_v4** | dialog display variant 4 |
|  1,720 | `0x4e` | **ChestSetup** | `'Truhe %s falscht benannt'` — chest setup |
|  1,555 | `0x17` | **QuestStateA** | quest state setter A |
|  1,363 | `0x14` | **Subsys_14** | 182 lines |
|  1,255 | `0x40` | **QuestKompassOBJ** | quest map marker — `'QuestkompassOBJ failed'` |
|  1,248 | `0x32` | **MiscAction** | calls FUN_00463f50 |

## All meaningful labels

### Object/NPC creation
- `0x01` CreateNPC, `0x08` CreateOBJ, `0x49` CreateLake (`See:`), `0x4a` ObjectCreate

### Dialog / text
- `0x03` DialogShow_v1, `0x1f` DialogShow_v2, `0x47` DialogShow_v3, `0x56` DialogShow_v4, `0x5f` DialogShow_v5
- `0x35` QuestLogSet (sets log title/open/done text refs)
- `0x3c` ResRef (resource reference)

### Quest state / tracking
- `0x17` QuestStateA, `0x6c` QuestStateB, `0x70` QuestStateC
- `0x44` HeroQBit_set, `0x45` HeroQBit_clear (per-hero quest bits)
- `0x21` HideTmpToDo (hide temporary objective)
- `0x40` QuestKompassOBJ (map marker), `0x3f` QuestKompassPos (marker position)
- `0x77` DQ_QuestSetup (daily-quest)
- `0x6d` PoolClear_v1, `0x71` PoolClear_v2 (clear quest pool)
- `0x85` PoolGetPos (get pool position)

### Triggers & events
- `0x02` TriggerSetState (`'Take:'`)
- `0x31` TriggerReset (`'Trg'`)
- `0x18` CheckGroup
- `0x4f` EventHandler_4f, `0x59` HeroEvent, `0x79` EventHandler_79, `0x7d` EventBroadcast (`cKernel_receive_event`)
- `0x3a` ConditionalEval (`if IsDead:%s`, `(true)/(false)`)
- `0x3b` **ELSE_jump** (control-flow!)
- `0x42` **BlockReader** (IF/THEN body wrapper)

### Hero ops
- `0x11..0x13` HeroOp_a/b/c, `0x24` HeroOp_d, `0x34` HeroOp_e, `0x46` HeroTargetCheck
- `0x48` HeroOp_f, `0x5a` HeroOp_g, `0x63` HeroOp_h, `0x67` HeroOp_i

### Atmospheric / region
- `0x41` AtmoRegion_a, `0x43` AtmoRegion_b, `0x4b` AtmoRegion_b, `0x4c` AtmoRegion_c, `0x69` AtmoRegion_d

### UI windows
- `0x3f` QuestKompassPos, `0x5b` UI_InventoryOp (`UI_WND_INVENTORY/BLACKSMITH`)
- `0x8a`, `0x8b` UI_TaskbarOp_b/c (`UI_WND_TASKBAR`)

### Sound / music
- `0x25`, `0x26` PlaySound_a/b (`SOUND_`)
- `0x68` PlayFX_HeroSound (`SOUND_FX_` + HERO)
- `0x7f` **PlayMusic** (cMSS_playMusic — Miles Sound System)
- `0x80` PlayFX_c

### Special / misc
- `0x00` END, `0x2b` END_ALT
- `0x0a..0x0e` InlinePoolWalker_a-e (inlined pool iteration)
- `0x1a` InlineHandler_1a, `0x3d` InlineHandler_3d, `0x6b` InlineHandler_6b
- `0x2c` NPC_UWR_dispatch (Underworld NPC)
- `0x2e` Teleport (`'Teleport failed: resnum=%d'`)
- `0x37` ItemDrop (`cItemDataMgr_push`)
- `0x4e` ChestSetup
- `0x5c` DirectTeleport (`'Figur wird direkt Teleportiert'`)
- `0x5e`, `0x8c` Hideout_a/b (`'versteck %d, %d, %d'`)
- `0x62` SetupSector (`'Setup Sector:%d,%d'`)
- `0x64` SectorPosOp
- `0x73` **StatementBuild** (the most common — base parameter accumulator)
- `0x84` ShowJokeImage (`'witz1/2/3.bmp'`)
- `0x29` ObjMgrGetData (direct call)
- `0x32` MiscAction
- `0x7a` NPC_FieldSet (modifies NPC array entry)

## Unlabelled (still TODO)

`Subsys_NN` placeholders for tags whose body didn't reveal an obvious
identifier on first pass:

`0x04, 0x05, 0x07, 0x0f, 0x14, 0x15, 0x19, 0x20, 0x23, 0x27, 0x28,
0x2d, 0x30, 0x33, 0x36, 0x38, 0x39, 0x4a, 0x4d, 0x57, 0x60, 0x61, 0x63,
0x6a, 0x6e, 0x73, 0x74, 0x75, 0x76, 0x78, 0x7b, 0x80, 0x87, 0x88, 0x89`

Most are 50-300 line bodies. Identifying them is the remaining ~60 % of
opcode-naming effort.
