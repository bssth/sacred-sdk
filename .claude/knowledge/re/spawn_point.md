# Sacred Gold — Hero starting spawn position (RE report)

Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000,
no ASLR (file offset == VA − 0x400000).

> Supersedes the earlier VAMPIRELADY-only draft. Key correction: the prior
> draft concluded "DefPos is a 0xCC-padded string table, not the 100-byte
> SERAPHIM layout". That was true *only for VAMPIRELADY*, which uniquely
> uses a different serializer (magic 0x781). The **other 7 classes use the
> documented `magic==1234` 3-array format with a 100-byte array0 record**
> (decoded & confirmed below). The runtime anchor from that draft
> (F7 overlay KompassPos=(2796,2261) for the vampiress spawn, hero_world ≈
> (150076,121365)) is retained and consistent with the findings here.

---

## TL;DR

- **DefPos.bin = compiled cache of named map positions.** Format
  (`FUN_0046f9b0` special branch, decompiled): `u32 magic(==1234/0x4D2)`
  then **three length-prefixed arrays**: array0 stride **100**, array1
  stride **0x50=80**, array2 stride **0x4c=76**. array0 is loaded into
  `cQuestManager+0x334` (the named-state store the SDK already drives).
- array0 record: `name` cstring at **+0x04** (≤60-byte buffer), **X=value0
  i32 LE @ +0x44**, **Y=value1 i32 LE @ +0x48**, in **TILE units**
  (0..~6300, signed). +0x4C/+0x50=value2/3, +0x54/+0x58=sentinels.
- Upstream source = **StartCode.bin tag 0x17** records:
  `00 01 <name> 00  0B <X:i32 LE>  0B <Y:i32 LE>` (0x0B = FunkCode tagged
  int). DefPos.bin is just StartCode replayed & serialized.
- **No dedicated hero-spawn record.** SP campaign places the hero by
  FunkCode **teleporting the hero creature** to a `tpstart_*` named
  position (Teleport tag 0x2e `FUN_00491d40`; DirectTeleport tag 0x5c
  `FUN_004a2b40`; SetupSector tag 0x62 `FUN_004a8390`). The handler writes
  active hero `cCreature+0x1C (X)/+0x20 (Y)` in **hero-world** units.
- Transform: `hero_world = (tile + 0.5) * 53.66563034057617`;
  inverse `tile = hero_world/53.66563034057617 − 0.5` (== SDK
  `world_pos`). Verified vs F7 anchor: vampiress live (2796,2261) KompassPos.
- **VAMPIRELADY DefPos.bin is the exception**: magic **1921 (0x781)**,
  1.1 MB, 0xCC-padded keyed records — the `==0x4d2` loader branch rejects
  it and rebuilds the store from StartCode.bin. Its `wegweiser_MPStart*`
  keys are zeroed placeholders; the spawn is resolved at runtime.
- **Safest override = runtime write of active hero `cCreature+0x1C/+0x20`**
  after the campaign teleport (Option A). Data-file edits (Options B/C) work
  for the 7 standard classes but not vampiress and risk DefPos-cache shadow.

---

## DefPos.bin format

Decompiled DefPos branch: `sdk/re/ghidra/decompiled/0046f9b0_FUN_0046f9b0.c`
lines ~181–349. Parsers: `sdk/.claude/knowledge/re/defpos_decode{,2,3,4}.py`.

```
u32 magic                                    ; must == 0x4d2 (1234)
if magic == 0x4d2:
    u32 count1 ; fread(dest=[obj+0x334], size=0x64, count1)   ; stride 100
    u32 count2 ; fread(dest=[obj+0x755c], size=0x50, count2)   ; stride 80
    u32 count3 ; fread(dest=DAT_00ab7820,  size=0x4c, count3)   ; stride 76
; else: branch skipped (FUN_00849850 cleanup), store rebuilt from StartCode
```
Save path in same fn writes back with `(end-start)/100`, `/0x50`, `/0x4c`.
SERAPHIM parse ends exactly at filesize 0x92718 — layout confirmed.

### array0 record (stride 100 = 0x64) — the position store

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | u32 | status (0 active; nonzero tombstone → skipped) |
| +0x04 | char[~60] | **name**, NUL-terminated (max 30 chars seen) |
| +0x40 | i32 | const 40 (0x28) |
| **+0x44** | **i32 LE** | **X = value0, TILE units (signed)** |
| **+0x48** | **i32 LE** | **Y = value1, TILE units (signed)** |
| +0x4C | i32 | value2 (often −1) |
| +0x50 | i32 | value3 (often −1) |
| +0x54 | i32 | reserved (22 / sentinel) |
| +0x58 | i32 | const 4096 (0x1000) |

Byte-identical to the `cQuestManager+0x334` named-state struct documented in
`runtime_triggers.cpp` ~L1547. DefPos array0 *is* that store at load time.
Examples (SERAPHIM): `hauptmann3`→(3349,2534), `tpstart_g_01`→(3290,2715),
`MPStart3_raus_pos`→(2224,5696). array1 (stride 80): named region/MP markers,
name@+0x05, X/Y @ +0x44/+0x48 (e.g. `wegweiser_MPStart2`→(342,1043)).
array2 (stride 76): unnamed waypoint/vector pairs, two i32 @ +0x00/+0x04.

### Per-class magic survey (all 8)

| Class | size | magic | array0 cnt |
|---|---:|---|---:|
| DAEMONIN | 601164 | 1234 | 1910 |
| DARKELVE | 599756 | 1234 | 1908 |
| ELVE | 599756 | 1234 | 1908 |
| GLADIATOR | 604768 | 1234 | 1945 |
| MAGICIAN | 600640 | 1234 | 1910 |
| SERAPHIM | 599832 | 1234 | 1908 |
| ZWERG | 599672 | 1234 | 1908 |
| **VAMPIRELADY** | **1146348** | **1921 (0x781)** | n/a (other format) |

VAMPIRELADY: the prior draft decoded it as key-string + 0xCC-padded
0x40-wide name buffer + `i32 X`,`i32 Y` in raw KompassPos units (e.g.
`pos_da1502` @0x2ac04, X@0x2ac44=2796 == F7 KX). Consistent: it's a
different serializer the 0x4d2 branch rejects.

### Coordinate space (HIGH)

Standard classes store **tile** ints. `cCreature+0x1C/+0x20` is
**hero-world**. `B = 53.66563034057617`:
`hero_world = (tile+0.5)*B` ; `tile = hero_world/B − 0.5`.
e.g. `tpstart_g_01` (3290,2715) → hero_world ≈ (176587,145729).
VAMPIRELADY DefPos `pos_*` already stores KompassPos(=tile) units directly.

---

## Hero spawn data source

No dedicated record. Flow (HIGH on model; MED on exact named target — needs BP):

1. `FUN_0046f9b0` loads the 6 per-class files; populates
   `cQuestManager+0x334` from DefPos.bin (magic 1234) or by replaying
   StartCode.bin tag-0x17 records.
2. StartCode.bin **tag 0x17** = named position:
   `00 01 <name> 00  0B <X:i32 LE>  0B <Y:i32 LE>` — upstream of array0.
   Proof: `hauptmann3` 0x17 payload `0B 15 0D 00 00 0B E6 09 00 00` →
   X 0x0D15=3349, Y 0x09E6=2534 (matches DefPos array0 exactly).
3. Campaign FunkCode (walker `FUN_00475680`, interpreter `FUN_00472bc0`)
   teleports the hero creature to a named position:
   - tag 0x2e Teleport → `FUN_00491d40` (`Teleport failed: resnum=%d`,
     str VA 0x94f00c).
   - tag 0x5c DirectTeleport → `FUN_004a2b40`; branch ~VA 0x4a3e7a calls
     `FUN_0054d9d0()` (per-creature pos/sector setter) then logs
     `Figur wird direkt Teleportiert` (str VA 0x94f564).
   - tag 0x62 SetupSector → `FUN_004a8390` (`Setup Sector:%d,%d`
     str VA 0x94f66c).
4. Handler resolves the name from `cQuestManager+0x334` and writes the
   active hero `cCreature+0x1C/+0x20` (hero-world). Active hero chain:
   `om=*(0x00AD5C40); ctx=*(0x0182EBE8); idx=*(ctx+0x14);
   hero=*(*(om+4)+idx*4)`.

Savegame/continue is separate: `cObjectManager::loadHero` /
`LoadHero (%d)` (str VAs 0x95d90c/0x95d948/0x95da64; `LoadHero` ref
VA 0x60596d, fn `FUN_00605800` / 0x6058a0) restores saved
`cCreature+0x1C/+0x20`.

---

## Recommended override hook

### Option A (RECOMMENDED) — runtime write of active hero `cCreature+0x1C/+0x20`

Space = hero-world. For desired tile `(TX,TY)`:

```c
const double B = 53.66563034057617;
int32_t hwx = (int32_t)((TX + 0.5) * B);
int32_t hwy = (int32_t)((TY + 0.5) * B);
uintptr_t reb = (uintptr_t)exe_module - 0x00400000;
uintptr_t om  = *(uintptr_t*)(reb + 0x00AD5C40);
uintptr_t ctx = *(uintptr_t*)(reb + 0x0182EBE8);
uint32_t  idx = *(uint32_t*)(ctx + 0x14);
uintptr_t arr = *(uintptr_t*)(om + 4);
uintptr_t hero= *(uintptr_t*)(arr + idx*4);
*(int32_t*)(hero + 0x1C) = hwx;   // X
*(int32_t*)(hero + 0x20) = hwy;   // Y
```

Timing: the SDK `on_world_load` fires *before* the script's teleport tag is
dispatched, so writing there is clobbered. Write on the **first `on_tick`
after world load**, or hook the **return of `FUN_004a2b40`** (VA
0x004A2B40, return ~0x4a3fa0) and overwrite. (HIGH on target+transform; MED
on no-clobber timing — confirm with a BP that value sticks 1–2 s post-load.)
Also covers savegame continue (write after `FUN_00605800`).

### Option B — patch StartCode.bin tag-0x17 target (7 standard classes)

`bin\TYPE_NPC_<CLASS>\StartCode.bin`: parse TLV (tag byte, u16 BE size,
size includes 3-byte header). Find tag-0x17 record whose name matches the
campaign start key (e.g. `tpstart_g_01`). In its payload, scan from the
name NUL: first `0x0B` → next 4 bytes = X (i32 LE), next `0x0B` → 4 bytes =
Y. Overwrite with desired **tile** coords (NO transform). SERAPHIM example
offsets: `tpstart_g_01` rec @file 0x014DA, `tpstart_g_02` @0x014F6,
`MPStart3_raus_pos` @0x0A2DE. (HIGH on encoding.) Caveat: loader prefers
DefPos.bin when magic==1234 → a StartCode-only edit may be shadowed by the
stale DefPos cache; pair with Option C or delete DefPos.bin so it rebuilds.

### Option C — patch DefPos.bin array0 (7 standard classes)

Record `i` at file offset `8 + 100*i`. Match name@+0x04; write desired
**tile** X→+0x44, Y→+0x48 (i32 LE). Not valid for VAMPIRELADY (magic
0x781 — use its own keyed layout: locate `pos_<name>\0`, skip the 0xCC
name buffer, then `i32 X`,`i32 Y` in raw KompassPos units, no scaling).

---

## Open items needing a runtime breakpoint

1. **Exact named position the SP campaign teleports the hero to at
   new-game**, per class. BP `FUN_004a2b40` (0x004A2B40) / `FUN_00491d40`
   on first world load; read resolved name + `(x,y)` into `FUN_0054d9d0`.
2. **No-clobber timing** for Option A: watch `cCreature+0x1C/+0x20` for the
   last engine write in the first ~2 s post-load.
3. **VAMPIRELADY (0x781)**: confirm `FUN_0046f9b0` falls through the
   `==0x4d2` test and the store is rebuilt from StartCode; the F7 anchor
   (2796,2261) is the runtime-resolved `wegweiser_MPStart*` (zeroed keys),
   never persisted — so for vampiress only Option A works reliably.
4. **DefPos vs StartCode precedence** for the same name (does loaded DefPos
   array0 shadow a StartCode tag-0x17 edit?). Determines whether Option B
   alone suffices.
5. Confirm `FUN_0054d9d0` is the canonical `(creature,x,y,sector)` setter
   writing hero-world to +0x1C/+0x20 (BP + inspect args).
