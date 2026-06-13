# Sacred Gold — FunkCode TAG MASTER TABLE — RE report

Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`, image base
`0x00400000`, no ASLR (file offset == VA − 0x400000, little-endian x86).

Evidence base:
- Record walker `FUN_00475680` fully decompiled
  (`sdk/re/ghidra/decompiled/00475680_FUN_00475680.c`, 2205 lines,
  the top-level `switch(tag)`).
- Field/opcode reader `FUN_00472bc0`
  (`decompiled/00472bc0_FUN_00472bc0.c`).
- Per-tag handler decompiles + handler string-literal scans
  (`sdk/.claude/knowledge/re/strhunt.py`, in-handler `push imm32` literal scan).
- TLV walk + tag histogram over the real Vampiress storyline
  `bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin` (3,963,314 bytes,
  **125,060 records, parses byte-exactly end-to-end**) —
  `sdk/.claude/knowledge/re/funkcode_walk.py`.
- NPC model doc `sdk/.claude/knowledge/re/npc_model.md` (tag 0x01 internals).

---

## 0. TL;DR / framing (HIGH)

**Record framing (TLV):**

```
tag  : u8
size : u16 BIG-ENDIAN    ; size INCLUDES the 3-byte header
payload : (size-3) bytes
```

Confirmed two independent ways:
1. The whole 3.96 MB Vampiress stream parses with **zero** size/overrun
   errors only under `size=(d[off+1]<<8)|d[off+2]` (big-endian, header
   included). `funkcode_walk.py` → "CLEAN (full file consumed)".
2. Walker `FUN_00475680:156` reads the record into stack buffer
   `local_a0c`; tag = `(ushort)local_a0c.pVFTable` (payload byte 0),
   size = `local_a0c.pVFTable._2_2_` (bytes 2-3). The streamed-file path
   (`*param_2==-1` → `FUN_00849986`) reads 4-byte then `size-4` chunks
   identically.

**Dispatch:** `FUN_00475680` is `switch((ushort)tag)`. Valid story
tags are `1..0x8c`; `0` and `0x2b` are no-ops/padding; `default:`
(e.g. `0x3e`) falls through to `switchD_00475821_caseD_6` = "consume &
continue, do nothing". A few tags (`0x2a`) are intercepted *before* the
switch via the `DAT_00ab7898` skip latch (see §3).

**Operand stream:** most handlers loop
`op = FUN_00472bc0(); ... ; while(op!=0)`. `FUN_00472bc0` returns the
**wire opcode byte verbatim** (except `0`=END, `0x20`=resolved hero
pos) and stages the decoded value at fixed context slots:
`in_ECX+0xa860/0xa864/0xa868` = X/Y/sector i32 triple,
`in_ECX+0xa880` = generic u32/handle, `in_ECX+0xa460` = ASCIIZ scratch.
Operand width is fixed *by the opcode* (see npc_model.md §"Field/opcode
table"): e.g. wire `0x01/0x29/0x63/0x68/0x69/0x6a/0x9d` → ASCIIZ;
`0x02/0x11/0x36/0x75` → +i32; `0x03/0x0a/...` → +u16;
`0x04/0x0c/0x0d` → +i32 (−2 ⇒ ASCIIZ position string follows);
`0x0b`/`0x04` → coordinate/value into the 0xa860 triple.

`in_ECX` throughout = the cInterpretSQW/quest-manager context block
(holds the parse cursor, the named-pos store at +0x334, the NPC array
at +0x31c, the journal array at +0x424, etc.).

---

## 1. IF / ELSE / ELSEIF / BlockReader jump mechanics (HIGH — *why we can't edit*)

This is the load-bearing reason the stream cannot be edited.

**There are NO stored jump offsets in the IF/ELSE records themselves.**
Skipping is done by *re-walking the TLV stream forward and summing the
big-endian sizes* until a terminator tag is hit. The cursor is the
shared byte offset `*param_2` into the whole FunkCode buffer.

### Condition records (tags `0x0a 0x0b 0x0c 0x0d 0x0e 0x18 0x29 0x85`)

These evaluate a predicate, and on FALSE do
`goto switchD_00475821_caseD_0` → return value `0`. The caller (the
quest/region driver) treats return `0` as "the following block is
inactive"; subsequent records up to the matching `0x3b`/`0x42` are
still walked but their handlers no-op because the active-flag is off.
Concretely (walker lines):

- `0x0a` (475680:239): read name/id via `FUN_00472bc0`; look it up in
  the flag table `DAT_00aabf18` (stride **0x124**, flag byte at
  entry+0x120); if `(flag & 2)==0` → skip block. = **IF <name> NOT set**.
- `0x0b` (314): same table; skip if `(flag & 2)!=0`. = **IF <name> set**.
- `0x0c` (390): same table, mask `& 4`, skip if `==0`.
- `0x0d` (465): same table, mask `& 4`, skip if `!=0`.
- `0x0e` (540): name → flag store `DAT_00aab708` (stride **0x54**,
  byte at entry+0x50); **SETs** `entry+0x50 = 1`. = **SET <name>**.
- `0x18` (657): `cVar6=FUN_0048c860()`; if true → caseD_6 (pass),
  else caseD_0 (skip). Generic boolean condition.
- `0x29` (978): NPC-array gate — selects NPC entry `*(i16)(creature
  +0x94)` in array `in_ECX+0x31c` stride **0x34**, sub-switch on
  `local_a0c.spare._0_2_` (1..5) testing flag bits 1/2/4/8/0x10 of
  entry+0x10; sets the `DAT_00ab7898` skip latch (see §3).
- `0x85` (2100): `FUN_0047a0c0()` then pass (skip-style cond).

The predicate vocabulary (debug/trace strings, blob 0x0094f2d4..):
`if IsInRgn:%s`, `if IsLocked:%s`, `if IsNotLocked:%s`,
`if IsFighting:%s`, `if IsDead:%s`, `if IsNotVarBit:%s=%d`,
`QuestBit %d gesetzt / nicht gesetzt`,
`Gamelevel gleich/ungleich {Bronze,Silver,Gold,Platin,Niob}`,
`(false NPC nicht da)`. These are emitted by the condition handlers
listed above (and the 0x30/0x31/0x32 quest-flag readers).

### `0x3a` = **IF** (open block) — walker 1439

`FUN_004987b0()` evaluates the condition. On TRUE → caseD_6 (fall
into block, keep walking). On FALSE → it **scans forward**:

```
puVar18 = param_1 + *param_2;            // current record
uVar4   = *(u16*)(puVar18+2);            // BE size of next rec
while (uVar4 < 0x1f5) {                   // 0x1f5 = 501 = max rec size
    copy uVar4 bytes into local_a0c;
    *param_2 += uVar4;                    // advance cursor by size
    tag = (u16)local_a0c.pVFTable;
    if (tag==0 || tag>0x8c) break;        // corrupt → "IF mit offenem Ende"
    if (tag==0x42) { if(FUN_004987b0()) goto caseD_6; }   // ELSEIF: re-test
    if (tag==0x3b) goto caseD_6;          // ELSE: enter else-branch
    uVar4 = *(u16*)(*param_2+param_1+2);
}
// fell off end → printf("IF mit offenem Ende (%s)\n", ...)  (0094eac4)
```

So **IF skips by summing record sizes forward until it finds the
matching `0x3b` (ELSE) or `0x42` (ELSEIF) or block end.** There is no
back-patched offset anywhere. If you insert/delete a byte the BE sizes
no longer tile the stream → the `uVar4 < 0x1f5` walk desyncs, lands
mid-record, reads garbage tag/size → "IF mit offenem Ende" log and the
intro hangs.

### `0x3b` = **ELSE** — walker 1501

When reached *while a branch is active* (i.e. the IF branch ran), ELSE
must skip its own block:

```
iVar8 = *param_2;
uVar13 = *(u16*)(iVar8+2+param_1);       // this ELSE record's BE size
if (uVar13 < 0x1f5) { *param_2 = iVar8+uVar13; return 1; }  // skip just it
else  printf("ELSE mit offenem Ende (%s)\n", ...) (0094ea88)
```

(The streamed path, `*param_2==-1`, walks via `FUN_00849986` and the
same `tag!=0 && tag<0x8d` terminator test.) Net: **ELSE either enters
(reached from a failed IF/ELSEIF) or skips by its own size.**

### `0x42` = **ELSEIF / generic BlockReader** — walker 1573

```
while (next BE size < 0x1f5) {
    copy rec; *param_2 += size; tag=(u16)...;
    if (tag==0 || tag>0x8c) -> "ELSEIF mit offenem Ende" (0094eaa4)
    if (tag==0x3b) goto LAB_004766f8;     // hit ELSE → advance past & enter
    next size...
}
```
On the active-branch side it re-evaluates `FUN_004987b0` (see the
0x3a loop above, the `tag==0x42` case). `LAB_004766f8` then advances
the cursor past the `0x3b` by its size and enters. = **chained
condition; same forward-size-summation skip.**

### `0x3d` = block terminator / scope-pop — walker 1542

Consumes its `FUN_00472bc0` operand stream and returns 1 (no effect).
Acts as an explicit "end of statement group".

**Consequence (already known, now mechanically proven):** every block
boundary is resolved purely by walking `tag:u8 + sizeBE:u16` triples
from the *current cursor*. Inserting or deleting any byte shifts all
following records; the size-tiling breaks; IF/ELSE/ELSEIF land
mid-record → "… mit offenem Ende" → load hang. **The SDK must replay
behaviors at runtime via DLL hooks, never patch the .bin.**

---

## 2. Tag → handler master table

VA = handler entry. "subsystem" / name from: walker control flow,
handler decompile, and in-handler string literals (cited). Confidence:
HIGH = control flow + naming string; MED = behavior clear, name
inferred; LOW = handler not yet decompiled, role from context only.

Counts = occurrences in the Vampiress `FunkCode.bin`.

| tag | count | name (recovered) | handler VA | subsystem / payload grammar | conf |
|----|------:|------------------|-----------|------------------------------|------|
| 0x00 | 0 | END / pad | — | loop terminator only | HIGH |
| 0x01 | 6118 | **CreateNPC** | FUN_00482510 | NPC spawn; opcode stream (see npc_model.md). `0x02`=Type/sub-id, `0x01`=name, `0x04`=POSITION, side ops, `0x12`=awake | HIGH |
| 0x02 | 198 | **CreateOBJ-lite / dialog-bind** | FUN_0048ae90 | refs `Dialog (%s) … vorhanden`; binds/show object w/ dialog | MED |
| 0x03 | 2587 | **DialogShow** | FUN_0048bb40 | `Dialog (%s/%d) nicht … vorhanden`; checks target Group alive (`Group (%d) is dead`, `Creature (%d) alive`) then shows dialog. Opcode stream | HIGH |
| 0x04 | 274 | rec04 | FUN_004819a0 | opcode stream; group/ref helper | LOW |
| 0x05 | 570 | rec05 | FUN_004817f0 | opcode stream | LOW |
| 0x07 | (rare) | rec07 | FUN_00481d80 | — | LOW |
| 0x08 | 6779 | **CreateOBJ** (item/object placement) | FUN_00485c10 | `CreateOBJ: %s mehrfach -> … ResNum`, `CreateOBJ warning -> deleting existing OBJ`, `NON_UNIQUE`, `Take:`. Sibling of CreateNPC for world objects/items/chests. Opcode stream like 0x01 | HIGH |
| 0x0a | 0 | **IF \<name\> NOT set** | inline 475680:239 | flag table DAT_00aabf18 stride 0x124, mask&2; skip if 0 | HIGH |
| 0x0b | 0 | **IF \<name\> set** | inline 314 | same, skip if !=0 | HIGH |
| 0x0c | 0 | IF flag&4 == 0 | inline 390 | DAT_00aabf18 mask&4 | HIGH |
| 0x0d | 0 | IF flag&4 != 0 | inline 465 | DAT_00aabf18 mask&4 | HIGH |
| 0x0e | 0 | **SET \<name\>** | inline 540 | flag store DAT_00aab708 stride 0x54, sets entry+0x50=1 | HIGH |
| 0x0f | 607 | rec0f | FUN_0048ee20 | param_3-driven (alias of 0x36) | LOW |
| 0x11 | (rare) | rec11 (NPC group) | FUN_0048d480 | refs `hero`; team/group | MED |
| 0x12 | (rare) | **QIS_Trigger (def)** | FUN_0048da40 | `QIS_Trigger%d`, `hero`; quest-item-script / area trigger define | MED |
| 0x13 | (rare) | QIS_Trigger | FUN_0048dd10 | `QIS_Trigger%d`, `hero` | MED |
| 0x14 | 1361 | **QIS_Trigger + dialog** | FUN_0048f030 | `QIS_Trigger%d`, `SOUND_`, `DQ_Quest`, `POOL_RG%d_`, dialog | MED |
| 0x15 | 557 | rec15 (DefPos rect?) | FUN_0048f7f0 | opcode stream | LOW |
| 0x16 | 1971 | rec16 (dialog-ref) | FUN_004813d0 | `Dialog (%s) … vorhanden` | MED |
| 0x17 | 1555 | **RegisterNamedPos / Pool** (DefPos) | FUN_00478780 | `PoolEmpty`, `Clear Pool`. Writes name+3×i32 into store `in_ECX+0x334` stride **100**; entry: +0 flag, +4 name, +0x44/+0x48/+0x4c xyz. op1=name, op0xb=coords. Net-broadcast 100-byte rec | HIGH |
| 0x18 | 61 | cond (generic bool) | FUN_0048c860 | true→pass, false→skip | HIGH |
| 0x19 | 22 | rec19 | FUN_00490a30 | — | LOW |
| 0x1a | 4298 | scope/flag rec | inline 668 | opcode stream; on op 0x16/0x1d/0x1e sets `iVar7+0x200 \|= 0x400`; op 0x7c sets `ctx+0x15=1`; clears ctx+0x15 at end | MED |
| 0x1f | 1870 | **DialogShow2 / DynamicQuest dialog** | FUN_0048f9e0 | `Dialog …`, `DQ_Quest`, `POOL_RG%d_`, `RegionsID falsch und Pool leer` | HIGH |
| 0x20 | 1870 | rec20 | FUN_00490be0 | — | LOW |
| 0x21 | 1870 | rec21 | FUN_00490cc0 | — | LOW |
| 0x23 | 3 | rec23 | FUN_0047c610 | (alias of 0x33) | LOW |
| 0x24 | 1 | rec24 | FUN_0048e4d0 | — | LOW |
| 0x25 | (rare) | rec25 | FUN_0048f330 | — | LOW |
| 0x26 | (rare) | rec26 | FUN_0048f330 | (alias of 0x25) | LOW |
| 0x27 | (rare) | rec27 | FUN_004918b0 | — | LOW |
| 0x28 | (rare) | **NetEvent broadcast/queue** | inline 740 | builds a 0x50-byte event into `ctx+0x755c..0x7560` ring, NetworkManager_receive_event_007d8950 | MED |
| 0x29 | 550 | **NPC-array cond gate** | inline 978 | sets DAT_00ab7898 latch; tests NPC-array entry flags (see §1,§3) | HIGH |
| 0x2a | 550 | **skip-latch consumer** | pre-switch 187 | if latch DAT_00ab7898 set, clears it & returns 1 (block consumed) — see §3 | HIGH |
| 0x2b | 551 | no-op (case 0/0x2b) | — | separator | HIGH |
| 0x2c | 55 | clear-net-slot | inline 1063 | FUN_00549920/005498f0; clears ctx net ring slot; clears iVar7+0x14 bit 0x80000 | MED |
| 0x2d | (rare) | rec2d | FUN_00491b30 | — | LOW |
| 0x2e | 665 | rec2e | FUN_00491d40 | — | LOW |
| 0x2f | (rare) | **resource-anchor select** | inline 1089 | matches name against resource table `ctx+0x752c`; sets `ctx+30000` (current resource) | MED |
| 0x30 | 1157 | **Quest state / register** | FUN_00496520 | `quest[%d] questid[%d] type[%d] flags[%x]` | HIGH |
| 0x31 | (rare) | **Quest state (+objs)** | FUN_004968a0 | `quest[…]`, `questobj ref[%d] flag[%d]`, journal fmt `%45.45s - %25s` | HIGH |
| 0x32 | 1250 | **Quest state (+objs)** | FUN_00496f20 | same strings as 0x31 | HIGH |
| 0x33 | 5309 | rec33 | FUN_0047c610 | (alias of 0x23) | LOW |
| 0x34 | (rare) | QIS_Trigger | FUN_0048e280 | `QIS_Trigger%d`, `SOUND_`, `hero` | MED |
| 0x35 | 4502 | **QuestLog / Journal entry** | FUN_00496080 | writes handle into journal array `ctx+0x424` stride **0x174**; entry located by `*(i32)(entry+8)==questid`; sub%10==0 → title slot +0x24, else 10-slot line array +0x28. op1/0x16/0x1d/0x1e=text handle, op4=(questid,sub) | HIGH |
| 0x36 | 857 | rec36 | FUN_0048ee20 | (alias of 0x0f) | LOW |
| 0x37 | (rare) | rec37 | FUN_00497f80 | — | LOW |
| 0x38 | (rare) | rec38 | FUN_004982d0 | — | LOW |
| 0x39 | (rare) | rec39 | FUN_004985e0 | — | LOW |
| 0x3a | 2756 | **IF (open block)** | inline 1439 (FUN_004987b0) | forward size-sum skip to 0x3b/0x42; "IF mit offenem Ende" | HIGH |
| 0x3b | 2733 | **ELSE** | inline 1501 | enter (from failed IF) or skip self by BE size; "ELSE mit offenem Ende" | HIGH |
| 0x3c | 3195 | **QuestkompassPos** (set quest-compass marker → position) | FUN_00499ba0 | `QuestkompassPos failed: vx=%d, vy=%d`, `QuestkompassOBJ failed: Quest=%d`, `RES:%d`, `rad:`, `UI_WND_TASKBAR` | HIGH |
| 0x3d | (rare) | block terminator/scope-pop | inline 1542 | consume opcode stream, return 1 | HIGH |
| 0x3f | 2056 | **QuestkompassOBJ** (compass → object/NPC) | FUN_0049a4b0 | `QuestkompassOBJ failed: Quest=%d, resnum=%d`, `rad:`, `atmo_rg` | HIGH |
| 0x40 | 1245 | **Questkompass + HeroQBit** | FUN_0049ac80 | `QuestkompassOBJ failed …`, `HeroQBit`, `HERO`, `atmo_rg` | HIGH |
| 0x41 | (rare) | HeroQBit/region helper | FUN_0049b2b0 | `atmo_rg`, `HeroQBit`, `HERO` (alias of 0x43) | MED |
| 0x42 | 5381 | **ELSEIF / BlockReader** | inline 1573 | forward size-sum skip; re-eval FUN_004987b0; "ELSEIF mit offenem Ende" | HIGH |
| 0x43 | 4845 | HeroQBit/region helper | FUN_0049b2b0 | (alias of 0x41) | MED |
| 0x44 | 1138 | **HeroQBit** (set hero quest/progress bit) | FUN_0049b840 | string-compares operand vs `"HeroQBit"` (0094f504), bit-name table `ctx+0x7550` stride 0x24; op1=name, op0xb=bit idx; writes cStatsManager bitfield | HIGH |
| 0x45 | 169 | HeroQBit helper | FUN_0049c160 | `HeroQBit`, `HERO`, `res`, `atmo_rg` | MED |
| 0x46 | (rare) | rec46 | FUN_0049daf0 | — | LOW |
| 0x47 | 4 | rec47 | FUN_0049dcf0 | — | LOW |
| 0x48 | 195 | rec48 | FUN_0049e210 | — | LOW |
| 0x49 | (rare) | rec49 | FUN_0049e760 | — | LOW |
| 0x4a | 18 | rec4a | FUN_0049fc50 | — | LOW |
| 0x4b | 305 | rec4b (dialog/region) | FUN_0049c930 | `atmo_rg`, `Dialog … vorhanden`, `HERO` | MED |
| 0x4c | 711 | rec4c (dialog/region) | FUN_0049cec0 | `atmo_rg`, `Dialog … vorhanden`, `HERO` | MED |
| 0x4d | (rare) | QIS_Trigger + dialog | FUN_0048e600 | `QIS_Trigger%d`, `SOUND_`, `Dialog …` | MED |
| 0x4e | 1721 | **Chest** (open/fill chest) | FUN_004a02a0 | `Truhe %s falscht benannt` ("chest %s wrongly named"); `Chest`/`Fill`/`CHEST`/`FILL` keyword family (0094f5b0..) | HIGH |
| 0x4f | 44 | rec4f | FUN_004a15a0 | — | LOW |
| 0x56 | 1804 | **Blacksmith / Inventory UI open** | FUN_004a1a50 | `UI_WND_BLACKSMITH`, `UI_WND_INVENTORY`, `schmiede`, `Dialog …`, `HERO` | HIGH |
| 0x57 | 27 | rec57 | FUN_004a6ea0 | — | LOW |
| 0x58 | (rare) | KernelEvent rec | inline 1701 | builds cKernel event id 0x18, sets `ctx+0xa45c\|=1` | MED |
| 0x59 | (rare) | rec59 | FUN_004a4040 | `HERO` | LOW |
| 0x5a | 16 | rec5a | FUN_004a1f20 | — | LOW |
| 0x5b | 48 | rec5b | FUN_004a2550 | — | LOW |
| 0x5c | (rare) | **direct Teleport (no listener)** | FUN_004a2b40 | `Figur wird direkt Teleportiert weil ohne Listner`, `HERO` | MED |
| 0x5d | 1 | rec5d | FUN_004a4310 | — | LOW |
| 0x5e | 44 | rec5e | FUN_004a6950 | — | LOW |
| 0x5f | 272 | rec5f | FUN_004a7760 | — | LOW |
| 0x60 | 351 | **Weather / Setup Sector** | FUN_004a79d0 | `Setup Sector:%d,%d`, `rain/regen`, `snow/schnee`, `thunder/gewitter`, `fog/nebel`, `butterfly` | HIGH |
| 0x61 | 15 | rec61 | FUN_004a81f0 | — | LOW |
| 0x62 | (rare) | rec62 | FUN_004a8390 | — | LOW |
| 0x63 (99) | (rare) | rec63 | FUN_004a8bb0 | — | LOW |
| 0x64 (100) | 11498 | **Group/Sound spawn (weapon pool)** | FUN_004a9670 | `SOUND_FX_`, `bin\wea.bin`, `Schwerer fehler im Group/Spawn vector`, `COUNT_MIDDLE`, `HERO`. High-frequency ambient/group spawn | HIGH |
| 0x67 | (rare) | rec67 | FUN_0048df30 | — | LOW |
| 0x68 | 956 | **Group/Sound spawn (param_4 variant)** | FUN_004a9730 | same strings as 0x64; branches on param_4 (net vs local) | HIGH |
| 0x69 | 213 | rec69 (dialog/region) | FUN_0049d450 | `atmo_rg`, `Dialog …`, `HERO` | MED |
| 0x6a | (rare) | **debug/cheat dispatch** | FUN_00465280 | `cheat basis %d`, `cheat SkillLevel %d`, `Dialog …` | MED |
| 0x6b | 0 | store vec → ctx+0x7684 | inline 1813 | op 0xb pushes 0xa860 i32 into `ctx+0x7684[]` list | HIGH |
| 0x6c | 831 | **GetPoolPos** (spawn-pool → pos) | FUN_004790c0 | `GetPoolPos from %s to %s`, `Clear Pool`, `PoolEmpty`, `GetPoolPos failed. Pool (%s) is empty` | HIGH |
| 0x6d | 331 | **GetPoolPos** | FUN_004793d0 | same GetPoolPos strings | HIGH |
| 0x6e | (rare) | **GetPoolPos** | FUN_00478d80 | same GetPoolPos strings | HIGH |
| 0x6f | (rare) | rec6f (marker, uVar9=3) | inline 1848 | returns code 3 (special loop signal) | MED |
| 0x70 | (rare) | **GetPoolPos → Region target** | FUN_0047b480 | `GetPoolPos from %s to Rgn(%s,%s) with Target(%s)` | HIGH |
| 0x71 | (rare) | **GetPoolPos → Region target** | FUN_0047b770 | same Rgn GetPoolPos string | HIGH |
| 0x72 | (rare) | sound/effect by idx | inline 1861 | op2→ptr 0xa880, op0xb→idx 0xa860 (0..0xff); FUN_004c1140 ring at ctx+0x448+idx*0xc | MED |
| 0x73 | 22762 | rec73 (most frequent tag) | FUN_004ab940 | no strings; tiny opcode-stream record. **Dominant storyline filler** — likely per-step state advance / no-op marker | LOW |
| 0x74 | 34 | rec74 | FUN_004abb60 | — | LOW |
| 0x75 | 10 | rec75 | FUN_0048d930 | — | LOW |
| 0x76 | 1870 | **DynamicQuest ToDo hide / SelfTrigger** | FUN_0048ff10 | `HideTmpToDo: Pool=%d, ToDo=%d`, `SelfTriggerQuest%d`, `DQ_Quest`, `POOL_RG%d_`, `Dialog …` | HIGH |
| 0x77 | (rare) | DynamicQuest ToDo / SelfTrigger | FUN_00490500 | same strings as 0x76 | HIGH |
| 0x78 | 1 | rec78 | FUN_006a1660/006a16e0 | UI/script-state flush | LOW |
| 0x79 | 30 | rec79 | FUN_004ac740 | — | LOW |
| 0x7a | 1870 | **NPC_FieldSet** | inline 1943 | NPC array `ctx+0x31c` stride **0x34**, idx=`*(i16)(creature+0x94)`; wire field 0x38 → entry+0x18 = `ctx+0xa860`. (npc_model.md §"NPC_FieldSet") | HIGH |
| 0x7b | 1870 | rec7b | FUN_00491090 | — | LOW |
| 0x7c | 24 | **sound play (cued)** | inline 1971 | op0xb=sound idx (0..0xff); FUN_0061d450/0061db10 (cMSS audio); fires cMSS event 0x35 | MED |
| 0x7d | 439 | **Teleporter + sound** | FUN_004ac940 | `cInterpretSQW::Teleporter(hex=%x)`, `SOUND_FX_`, `DEFAULT_BUSYDLG` | HIGH |
| 0x7e | 2 | rec7e | FUN_006770e0/00696060 | quest text flush | LOW |
| 0x7f | 20 | rec7f | FUN_004adaa0 | — | LOW |
| 0x80 | 11 | rec80 | FUN_004add60 | — | LOW |
| 0x81 | (rare) | rec81 | FUN_006770e0/006791c0 | quest text + journal flush | LOW |
| 0x82 | 26 | **Teleporter** | cInterpretSQW_Teleporter_004ae040 | `cInterpretSQW::Teleporter(hex=%x)`, `DEFAULT_BUSYDLG`, `witz{1,2,3}.bmp` (loading-screen). Hero teleport between regions | HIGH |
| 0x83 | 3 | rec83 (DX7 light mgr) | FUN_006413e0/00641720 | `cDX7LightMgr::init` — toggles renderer lights by vec; **not story** | MED |
| 0x84 | 374 | rec84 (loading-screen jokes) | FUN_004ae350 | `witz{1,2,3}.bmp` | MED |
| 0x85 | 44 | cond (skip-block) | FUN_0047a0c0 | true→pass else skip | HIGH |
| 0x86 | 2 | KernelEvent rec2 | inline 2104 | cKernel event id 0x19; `ctx+0xa45c\|=3`; objmgr cleanup | MED |
| 0x87 | 13 | rec87 | FUN_004af190 | — | LOW |
| 0x88 | 17 | rec88 | FUN_004af8c0 | — | LOW |
| 0x89 | 36 | rec89 | FUN_004afff0 | — | LOW |
| 0x8a | 1 | rec8a | FUN_004b0790 | — | LOW |
| 0x8b | 1 | rec8b | FUN_004b0970 | — | LOW |
| 0x8c | (rare) | rec8c | FUN_004b0c00 | — | LOW |
| 0x3e | 2572 | (default → pass-through no-op) | — | falls to switchD_…_caseD_6; consumes nothing extra | HIGH |

Total distinct tags actually present in Vampiress: **102**.
Histogram (full) reproducible via `python funkcode_walk.py`.

---

## 3. The `0x29`/`0x2a` skip-latch (NPC-array conditional block)

`0x29` (walker 978) sets global `DAT_00ab7898 = 1`, then evaluates an
NPC-array predicate. If the predicate is **satisfied** (`!bVar24`), it
clears the latch (`DAT_00ab7898 = 0`) and returns 1 immediately
(block runs). If not satisfied, the latch stays `1`. On every
subsequent record the walker top (lines 186-193) checks:
`if (DAT_00ab7898 != 0) { if (tag==0x2a){latch=0; return 1;} else
goto caseD_6; }` — i.e. while the latch is set, **all records are
no-op'd until a `0x2a` terminator is met**, which clears the latch.
So `0x29 … 0x2a` is a self-contained conditional block delimited by
tag `0x2a`, *also* using forward walking (no offsets). Same edit
fragility as IF/ELSE.

`local_a0c.spare._0_2_` (the 0x29 payload's sub-selector, 1..5) picks
which NPC-array entry flag bit is tested (entry+0x10 bits
1/2/4/8/0x10), e.g. 1 = "is spawned/alive", 2 = "is type≠6 special",
3/4/5 = bits 4/8/0x10. Cases 1/2 additionally repair the entry's
object-manager link before testing.

---

## 4. Storyline-relevant tag quick map

| concern | tag(s) | handler |
|---|---|---|
| spawn NPC | **0x01** | FUN_00482510 |
| spawn object/item/chest | **0x08**, 0x02 | FUN_00485c10 / FUN_0048ae90 |
| show dialog | **0x03**, **0x1f**, 0x16, 0x4b, 0x4c, 0x69 | FUN_0048bb40 / FUN_0048f9e0 |
| quest state / register / solve | **0x30**, **0x31**, **0x32** | FUN_00496520 / 004968a0 / 00496f20 |
| journal / quest-log text | **0x35** | FUN_00496080 (array ctx+0x424 ×0x174) |
| hero quest bit | **0x44**, 0x40, 0x41/0x43, 0x45 | FUN_0049b840 |
| set quest marker (compass) | **0x3c** (pos), **0x3f**/**0x40** (obj) | FUN_00499ba0 / 0049a4b0 / 0049ac80 |
| register named position / pool | **0x17** | FUN_00478780 (store ctx+0x334 ×100) |
| spawn-pool → position | **0x6c**,0x6d,0x6e,**0x70**,0x71 | FUN_004790c0 … |
| condition / trigger (flag) | **0x0a/0x0b/0x0c/0x0d**, 0x18, 0x29, 0x85 | inline / FUN_0048c860 |
| set flag | **0x0e** | inline (DAT_00aab708 ×0x54) |
| IF / ELSE / ELSEIF | **0x3a / 0x3b / 0x42** | inline FUN_004987b0 |
| area/item trigger (QIS) | **0x12,0x13,0x14,0x34,0x4d** | FUN_0048da40 … |
| NPC scripted move / teleport | **0x82**, **0x7d**, 0x5c | cInterpretSQW_Teleporter_004ae040 |
| set NPC field | **0x7a** | inline (NPC array ctx+0x31c ×0x34, +0x18) |
| weather / sector setup | **0x60** | FUN_004a79d0 |
| ambient group / sound spawn | **0x64**, 0x68 | FUN_004a9670 / FUN_004a9730 |
| net event broadcast | 0x28, 0x58, 0x86 | inline + NetworkManager |
| set NPC name | (no dedicated tag — name is the `0x01` op-`0x01` ASCIIZ in CreateNPC; NPC identity = its entry in the quest NPC array ctx+0x31c, see npc_model.md §"Quest-NPC binding") | — |

"Set quest marker" = **tag 0x3c** (QuestkompassPos, position) or
**0x3f/0x40** (QuestkompassOBJ, lock onto an object/NPC), confirmed by
the `Questkompass*` failure strings inside those handlers.

---

## 5. Script-language keyword ↔ tag mapping (status)

There is **no single global keyword→tag table** for FunkCode commands.
Two compiler layers exist:

1. **Inner expression VM** — table `DAT_00964268`, stride 0x38,
   count `FUN_00672fb0()`=0x18 (24 entries). Layout: +0x00 opcode u32,
   +0x04 flag, +0x08 8-byte inline mnemonic, then operand-type dwords.
   Mnemonics (dumped via `kwtable.py`): `exit nop ret rsp cmp cmpi
   rspx jne … sub add mul div mov movi xchg rand callRPC`. This is the
   `cmp/jne/mov` micro-VM used inside script blocks, **not** the
   FunkCode story commands. (HIGH — table dumped verbatim.)
2. **Statement / resource compiler** — `cScriptCompiler_parseStatement
   _0066fdf0` handles language scaffolding only: `for`, `while`,
   `return`, `pragma`, `pragma SCRIPT_EDITOR/SCRIPT_USER`;
   `loadScriptedSequenceR_006714c0` / `parseResource_00670ea0` handle
   `pragma resources`, `resource <type> <name>`, `float`. The actual
   story verbs are emitted by per-command parser routines that are
   selected by **string compare against literals embedded in the
   command handlers themselves**, not a lookup table.

Recovered command keywords (from handler string literals + script
blob 0x0094e000..0x00950000, `strhunt.py`):

| keyword / token | tag | evidence string |
|---|---|---|
| `CreateNPC` | 0x01 | `CreateNPC failed: vx=%d, Type=%d` (0094eca8) |
| `CreateOBJ` | 0x08 | `CreateOBJ: %s mehrfach … ResNum` (0094ee4d) |
| `DefDialog:` / `Talk_%s_Dlg_%d` | 0x03/0x1f | dialog show; 0094e008 / 0094e140 |
| `OnQuestState:` | 0x30/0x31/0x32 | 0094e014; `quest[%d] questid[%d] type[%d]` |
| `Funktion:` / `WorkFunktion` | (script fn call, inner VM) | 0094e024 / 0094e698 |
| `HeroQBit` | 0x44 | literal compared at FUN_0049b840 (0094f504) |
| `Questkompass` (Pos/OBJ) | 0x3c / 0x3f / 0x40 | `QuestkompassPos failed …` (0094f43c) |
| `Teleporter` | 0x82 / 0x7d | `cInterpretSQW::Teleporter(hex=%x)` (0094f710) |
| `Pool` / `GetPoolPos` / `Clear Pool` | 0x6c-0x71, 0x17 | `GetPoolPos from %s to %s` (0094eb28) |
| `Chest`/`CHEST`/`Fill`/`FILL` | 0x4e | `Truhe %s falscht benannt` (0094f514) |
| `QIS_Trigger%d` / `SelfTriggerQuest%d` | 0x12-0x14,0x34,0x4d / 0x76,0x77 | trigger names |
| `CreateDynamicQuest` (`DQ_Quest`,`POOL_RG`) | 0x1f/0x76/0x77 | `CreateDynamicQuest: Pool=%d, Header=%d, Typ=%d, ToDo=%d` (0094e6f0) |
| `changeDefPos` | 0x17 | `changeDefPos(%s, px=%d, py=%d) OK!` (0094e75c) |
| IF / ELSE / ELSEIF | 0x3a / 0x3b / 0x42 | `IF/ELSE/ELSEIF mit offenem Ende` |
| condition preds (`IsInRgn/IsLocked/IsDead/IsFighting/IsNotVarBit`, `QuestBit`, `Gamelevel`) | 0x0a-0x18,0x29,0x30-0x32 | `if Is*:%s`, `QuestBit %d gesetzt` |
| position tokens `CPOS:hero`,`CPOS:RES:`,`DLGNPC`,`QUESTNPC`,`QUESTFELLOW`,`RES:%d` | operand of 0x01/0x04/pos-bearing recs | FUN_00472bc0 (npc_model.md §POSITION) |

Confidence: keyword↔tag rows above are HIGH where the literal lives
inside that exact handler (CreateNPC, CreateOBJ, HeroQBit, Teleporter,
Questkompass, GetPoolPos, Chest, IF/ELSE) and MED for the
`OnQuestState`/`DefDialog`/`Funktion` script-source keywords (those are
.txt source labels; the byte-tag binding is by handler behavior, not a
table, so the mapping is by string co-residence not a hard pointer).

---

## 6. Tooling left in scratch/

- `funkcode_walk.py` — TLV walker + tag histogram + clean-parse check
  (full Vampiress file consumes byte-exactly). Run: `python
  funkcode_walk.py [path]`. Embeds the tag→name map from §2.
- `strhunt.py` — pulls named string literals by VA and scans the
  command-string blob 0x0094e000..0x00950000.
- `kwtable.py` — dumps the inner-VM keyword table `DAT_00964268`.
- (existing) `npc_decode2.py` — opcode-faithful tag-0x01 decoder;
  `npc_model.md` — CreateNPC/faction/quest-binding deep dive.

---

## 7. Open items / lower-confidence

- **0x73 (22,762×, the single most common tag)** and 0x64 (11,498×):
  no embedded strings; 0x73 = FUN_004ab940 not yet decompiled. Given
  frequency these are almost certainly per-script-step bookkeeping
  (state-advance / "yield" markers) rather than visible actions. Needs
  FUN_004ab940 decompile + a runtime BP to confirm semantics.
- LOW-confidence handlers (0x04,0x05,0x07,0x19,0x1f-fam helpers,
  0x46-0x4a, 0x57-0x5f, 0x74-0x80, 0x87-0x8c): handler not decompiled;
  role inferred from walker dispatch only. Decompile batch suggested:
  `analyzeHeadless … -postScript DecompileFunc.java 0x004ab940
  0x004a9670 0x0047c610 0x004790c0 0x0048f9e0 0x00499ba0 0x0049b840`.
- The exact `0x29` sub-selector → predicate-name mapping (1..5) and
  the DAT_00aabf18 (0x124) vs DAT_00aab708 (0x54) flag-store roles
  (quest-bit table vs named-flag table) should be pinned with a BP on
  the flag reads during a known quest step.
- Confirm the BE-size claim is also honoured by the network-replicated
  100-byte (0x17) / 0x174-byte (0x35) sub-records — those are internal
  structs, not TLV, so unaffected by the edit constraint.
```
