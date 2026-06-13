# Sacred Gold — ITEM equip ABI, start-sword, full catalog — RE report

Target: Steam 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000,
no ASLR (file off = VA − 0x400000), x86 LE. Evidence: Ghidra decompiles
in `sdk/re/ghidra/decompiled/`, raw disasm of prologues, the in-EXE
TYPE table, decoded `bin/TYPE_NPC_VAMPIRELADY/StartCode.bin`. Companion:
`items.md` (create-path unification), `npc_model.md` (cCreature map).

---

## TL;DR

1. **Catalog: 5624 TYPE_\* entries**, one shared creature+item id space.
   Table `@0x008EC328`, stride `0x44`, `+0x00 u32 id`, `+0x04` inline
   ASCIIZ name. **The id is NOT the row index** (rows 0..9 = hero models
   id 0..9, then jumps to id 32 and stays non-contiguous). End row 5623
   = `TYPE_LAST_ITEM` id 0x1FFF; cross-checked byte-exact against the
   `getItemType` scan bound `&PTR_DAT_0094990c` = `0x008EC32C +
   5624*0x44`. Generator `sdk/re/py/gen_item_lua.py` →
   `custom/lua/lib/items_gen.lua`. Breakdown: WEAPON 2761, ARMOR 970,
   OBJECT 918, NPC 404, FX 210, SMOVE 96, NATURE 73, MINIOBJECT 46,
   SPELL 44, BLACKSMITH 36, SHIELD 35, HORSEBIT 23, misc 7. **HIGH.**

2. **Vampiress starting sword = `TYPE_WEAPON_VAMP_SWORD_NORMAL`, type
   id 1871** (0x74F). **MED-HIGH** (static evidence + vanilla
   convention; the one cheap runtime probe below confirms it 100%).
   The basic level-1 vampire blade. Alternatives in the same family:
   `TYPE_WEAPON_VAMP_SWORD_NORMAL01`=1872, `..._BLUTSCHWERT`=1868,
   `TYPE_WEAPON_SWORD_VAMPIRE`=1771.

3. **Equip ABI nailed (decompiled + disasm-verified):**
   - `cObjectManager_create_005fb530` — `__thiscall`, **ECX = objmgr**
     (`*0x00AD5C40`). Args `(uint type, char a3, char a4, …)`; call as
     **`create_005fb530(ECX=om, type, 0, 1, 0)`** (type, then 0,1,0).
   - `cCreature_equipment_equip_00555e00` — `__thiscall`, **ECX =
     cCreature obj**. Args **`(uint slot, int itemRef, char sendNet)`**.
     Stores `itemRef` at **`cCreature + slot*4 + 0x1A4`** (verified:
     `in_ECX + (slot&0xffff) + 0x69` int-units = `+0x1A4` byte).
   - `cEngine_creature_equipItem_00611560` — **self-contained, needs NO
     cEngine pointer**: its prologue does `mov ecx,[0x00AD5C40]`
     internally, body never reads the caller's ECX. Effective sig
     **`__cdecl equipItem(int creatureRef, uint slot, char* itemName)`**.
     Does name→id (`FUN_0043cd90`) + create + equip + weapon-hand
     refresh + net in one call. Simplest entry point.
   - **Weapon main hand = slot `0x0D`** (off/left hand `0x0C`),
     confirmed by `creature_equipItem`'s `if(slot==0xd)
     FUN_005d8730(0,1,…); if(slot==0xc) FUN_005d8730(0,0,…)` and the
     hero equip table in `player_state.cpp::read()`
     (`+0x1D4`=weapon_l slot 0xC, `+0x1D8`=weapon_r slot 0xD — same
     `cCreature+0x1A4+slot*4` base equipment_equip uses). **HIGH.**

---

## 1. Full item TYPE catalog

`getItemType` (`FUN_0043cd90`) name→id resolver, decompiled:

```
LAB_0043ce7c:
  ppuVar7 = s_TYPE_INVALID_008ec32c + uVar5*0x44;       // name at +4
  do { if (strcmp(ppuVar7, name)==0)
          return (&DAT_008ec328)[uVar5*0x11];            // <-- +0x00 id field
       ppuVar7 += 0x11; uVar5++;                          // 0x11 dw = 0x44
  } while (ppuVar7 < &PTR_DAT_0094990c);                  // hard end bound
```

So the value the engine uses as the *type id* is the **`+0x00` dword of
the matched record**, not its position. `0x008EC32C + 5624*0x44 ==
0x0094990C` exactly ⇒ **5624 rows**, the last (`TYPE_LAST_ITEM`, id
0x1FFF) is the sentinel, row 5624 is past-end garbage. (`FUN_0043cd90`
also has a *first* path for the few special Balance-name prefixes that
returns a sequential def-record index off `DAT_00aab5e4`; that path is
not the TYPE_* item path and is irrelevant to the catalog.)

`create_005fb530` then does `id2 = FUN_0043fc40(id)` (a tiny remap of
only ids 0x13C0..0x13C8 — pass-through otherwise) → `FUN_00425ea0(id2)`
= `id2*0x80 + DAT_00aab5e4` (128-byte def record, guard
`0<id2<0x7e60`). So **the catalog id feeds create directly**; the def
record's class byte `+0x2E` decides cCreature vs cItemBase vs object3D.

**Deliverables.** `sdk/re/py/gen_item_lua.py` reads the EXE directly
(no CSV — the binary *is* the source) and emits
`custom/lua/lib/items_gen.lua`: `M.<KEY> = id` constants (KEY =
TYPE_-stripped name), `M.by_id`, `M.by_name`, `M.by_cat`,
`M.weapons()/armor()/shields()`, `M.find(sub)`, `M.all_matching(sub)`.
5624 entries, ids 0..8191, no key/id collisions, mirrors `npc.lua`'s
shape. Human-readable cross-check: the in-EXE `TYPE_*` names are already
the authoritative descriptive names (e.g. `TYPE_WEAPON_VAMP_SWORD_NORMAL`,
`TYPE_ARMOR_DAEM_BODY`, `TYPE_SHIELD_KITE_Shield_VAMPIRE01`); the
community Weapon.pak editor is only an *instance* struct tool and the
sacred_modding pack ships no item table, so no extra name source was
needed or available.

Equippable families a modder arms an NPC with: **WEAPON 2761, ARMOR
970, SHIELD 35** (+ HORSEBIT 23 for mounts).

---

## 2. The Vampiress starting weapon

The Vampiress is the *player hero* (CreateNPC Type 6, class bit 32).
Her starting kit is not given by a `StartCode` `EquipNPC` record — the
76 `EquipNPC` (FunkCode tag **0x07** → `FUN_00481d80`) records in
`bin/TYPE_NPC_VAMPIRELADY/StartCode.bin` all target `res:NNNNN` script
objects and equip **horse bridles/saddles** (`TYPE_HORSEBIT_*`) onto
the start-region horses, not the hero. The hero's default loadout comes
from the hero-class default (Balance / new-game template), so the only
fully authoritative source is a runtime read.

**Static narrowing → `TYPE_WEAPON_VAMP_SWORD_NORMAL`, id 1871
(0x74F).** The Vampiress weapon family in the type table is exactly:

| id | name | role |
|---|---|---|
| 1771 | `TYPE_WEAPON_SWORD_VAMPIRE` | generic "vampire sword" item |
| 1868 | `TYPE_WEAPON_VAMP_BLUTSCHWERT` | "blood sword" (special) |
| 1869 | `TYPE_WEAPON_VAMP_LANZE` | lance |
| 1870 | `TYPE_WEAPON_VAMP_MORGENSTERN` | morningstar |
| **1871** | **`TYPE_WEAPON_VAMP_SWORD_NORMAL`** | **the basic / starter blade** |
| 1872 | `TYPE_WEAPON_VAMP_SWORD_NORMAL01` | normal-sword variant 01 |
| 1873 | `TYPE_WEAPON_VAMP_2H_SWORDLONG` | two-hander |

`_SWORD_NORMAL` is the canonical level-1 one-hand vampire sword and
matches vanilla Sacred (the Vampiress starts with a plain vampire sword
in the main hand). Confidence **MED-HIGH**; pin to **HIGH** with the
probe below.

### Cheap in-game probe (definitive — already in the SDK)

`player_state.cpp::hero_weapon_dump()` (a pure-read diag, already
written) resolves the hero via `*0x00AD5C40 → om`, `*0x0182EBE8 → ctx`,
`*(ctx+0x14)` = hero index, then logs every equip slot
`hero+0x1A4+slot*4` → item obj → its **type id at obj+0x10**. Start a
new Vampiress game, fire `hero_weapon_dump()`, read the line for
**slot 0x0D** (weapon_r / main hand): the `type=` it prints is the
ground-truth start-sword id (expected 1871). Slot 0x0C is the off hand.

### Reading ANY creature's currently-equipped item type at runtime

```
om   = *(uintptr*)0x00AD5C40
arr  = *(uintptr*)(om + 4)              // object handle array
creat= *(uintptr*)(arr + handle*4)      // the cCreature object
ref  = *(uint32*)(creat + 0x1A4 + slot*4)   // equip slot -> ITEM handle
if ref != 0:
   item = *(uintptr*)(arr + ref*4)      // resolve the item object
   itemTypeId = *(uint32*)(item + 0x10) & 0xFFFF   // item's TYPE id
   itemFlags  = *(uint32*)(item + 0x14)
```

**The item object's type-id offset is `+0x10` — the SAME offset as a
creature's type id** (confirmed: `equipment_equip_00555e00` and
`hero_weapon_dump()` both read item `+0x10` for type, `+0x14` flags;
`create_005fb530` writes `piVar2[4]=type` and the obj's `+0x10` carries
it identically to the creature path). Slot map below; main weapon =
slot **0x0D**. **HIGH.**

---

## 3. The exact equip ABI

### 3a. `cObjectManager_create_005fb530`  (create an item object)

`__thiscall`, **ECX = cObjectManager** (`*0x00AD5C40`). Decompiled sig
`(uint type, char a3, char a4, …)`; the verified live call (from
`creature_equipItem` `00611560:23` and `EquipNPC` `00481d80:179`):

```c
int itemRef = create_005fb530(/*ECX=*/om, /*type=*/typeId,
                              /*a3=*/0, /*a4=*/1, /*a5=*/0);
//  EquipNPC variant passes (typeId,0,0,0); creature_equipItem passes
//  (typeId,0,1,0). a3=create-flag, a4=doNet-ish, a5=0. Use (type,0,1,0).
```

`type` is the catalog id (`items_gen.lua` value or
`FUN_0043cd90("TYPE_…")`). Returns an object handle (item ref) or 0.
The item is created **position-less** (inventory/equip use); to drop it
on the ground instead, see `items.md §4` (`cEngine_dropItem_00611620`).

### 3b. `cCreature_equipment_equip_00555e00`  (put item ref in a slot)

`__thiscall`, **ECX = cCreature object**. Sig
**`(uint slot, int itemRef, char sendNet)`**:

```c
equipment_equip_00555e00(/*ECX=*/creatObj, /*slot=*/slot,
                         /*itemRef=*/itemRef, /*sendNet=*/1);
```

Body (verified): `slot==0x12` → mount (`cCreature[0x7B]`=`+0x1EC`);
`slot<0x13` → destroys any prior item in the slot
(`cObjectManager_destroy_005fbdb0`), stores `itemRef` at
`*(int*)(in_ECX + (slot&0xffff)*4 + 0x1A4)`, calls `FUN_00555300(slot)`
to rebuild the visible model, and (if `sendNet`) broadcasts net opcode
`0x11e`. `slot>=0x13` → logged & ignored.

### 3c. Slot index → body part  (HIGH — two independent sources)

`cCreature + 0x1A4 + slot*4` (stride 4). Cross-confirmed by
`player_state.cpp::read()` (hero equip table) AND the `equipment_equip`
store math AND `creature_equipItem`'s weapon-hand special-cases:

| slot | byte off | body part |
|---|---|---|
| 0x00 | +0x1A4 | helmet |
| 0x01 | +0x1A8 | cuirass / body |
| 0x02 | +0x1AC | belt |
| 0x03 | +0x1B0 | arms |
| 0x04 | +0x1B4 | legs |
| 0x05 | +0x1B8 | shoes |
| 0x06 | +0x1BC | gauntlets |
| 0x07..0x0A | +0x1C0..+0x1CC | rings 1..4 (per player_state) |
| 0x0B | +0x1D0 | (amulet/ring4 region) |
| **0x0C** | **+0x1D4** | **weapon_l (off / left hand)** |
| **0x0D** | **+0x1D8** | **weapon_r (MAIN weapon hand)** |
| 0x0E | +0x1DC | cannon / ranged |
| 0x0F | +0x1E0 | shoulders |
| 0x10..0x11 | +0x1E4..+0x1E8 | (aux equip) |
| 0x12 | +0x1EC | **mount / horse** (special path) |

Exact ring/amulet sub-labels (0x07..0x0B) are MED (from
`player_state.cpp`'s field names); the **load-bearing facts — base
0x1A4, stride 4, weapon main = 0x0D, off = 0x0C, mount = 0x12 — are
HIGH** (decompiled + SDK source agree). For "arm a guard with a sword"
use **slot 0x0D**.

### 3d. The exact correct call sequence for `npc_equip`

```c
// reb = (module base) - 0x00400000   (0 with no ASLR / file mapping)
// handle = creature object handle (npc.lua spawn handle)
// item_type = catalog id (items_gen.lua), e.g. 1871
// slot = 0x0D for the main weapon hand

void* om = *(void**)0x00AD5C40;
void* creat = *(void**)( *(void**)((char*)om + 4) + handle*4 );

typedef int   (__thiscall* fn_create)(void* om, unsigned type,
                                       char a3, char a4, unsigned a5);
typedef int   (__thiscall* fn_equip )(void* creat, unsigned slot,
                                       int itemRef, char sendNet);

fn_create mk = (fn_create)0x005FB530;
fn_equip  eq = (fn_equip )0x00555E00;

int itemRef = mk(om, item_type, 0, 1, 0);     // create the item object
if (itemRef) eq(creat, slot, itemRef, 1);     // equip + visual + net
```

This is exactly what `player_state.cpp::npc_equip(handle,type,slot)`
already does (verified against both decompiles). It needs **only the
objmgr global + the creature object** — no cEngine pointer, no name
string. **HIGH.**

### 3e. The even simpler one-call alternative

`cEngine_creature_equipItem_00611560` is *simpler still* and self-
contained. Disasm of its prologue:

```
00611560: 53                 push ebx
00611561: 8B0D 405CAD00      mov  ecx, [0x00AD5C40]   ; ECX := *objmgr (internal!)
00611567: 55 56 57           push ebp/esi/edi
0061156B: 8B7C2414           mov  edi, [esp+0x14]     ; arg0 = creatureRef
...                          (6A00 ... push slot, name; calls getData/getItemType)
```

The body **never references the caller's ECX** (Ghidra showed zero
`in_ECX` uses; it loads the objmgr itself). So it is effectively:

```c
typedef void (__cdecl* fn_equipItem)(int creatureRef, unsigned slot,
                                     const char* itemTypeName);
fn_equipItem ei = (fn_equipItem)0x00611560;
ei(handle, 0x0D, "TYPE_WEAPON_VAMP_SWORD_NORMAL");
```

(If the build's actual convention is `__thiscall` with an ignored
`this`, passing any value as ECX is harmless since it's dead. Both the
two-step `npc_equip` in 3d and this share identical net/visual
behaviour because `creature_equipItem` just calls `getItemType` +
`create_005fb530(id,0,1,0)` + `equipment_equip_00555e00(slot,ref,1)` +
the 0xC/0xD `FUN_005d8730` hand refresh.) **There is NO cEngine
singleton to find** for equip — the call resolves the objmgr
(0x00AD5C40) on its own. The `cObjectManager` global remains
`0x00AD5C40`; `cWorld` `0x00AD3560`; active-context `0x0182EBE8`.

**Recommendation:** keep the two-step `npc_equip` (3d) as the primary —
it is fully decompiled, dependency-minimal (objmgr + creature only),
and lets you choose the slot explicitly. Use `creature_equipItem`
(0x00611560) only when you want by-name + auto weapon-hand handling.

---

## 4. Confidence summary & probes

| Claim | Conf | Cheap probe |
|---|---|---|
| Catalog = 5624 TYPE_* @0x008EC328 /0x44, id=+0x00 (≠ index) | HIGH | bound `0x94990C` == base+5624*0x44 (done) |
| `getItemType` returns +0x00 id field | HIGH | decompiled |
| create_005fb530(ECX=om, type,0,1,0) | HIGH | decompiled + 2 live call sites |
| equipment_equip(ECX=creat, slot, ref, sendNet) | HIGH | decompiled |
| equip slot base cCreature+0x1A4, stride 4 | HIGH | decompile + player_state.cpp agree |
| main weapon = slot 0x0D, off = 0x0C, mount 0x12 | HIGH | 0xC/0xD special-cases + hero table |
| item obj type id at +0x10 (same as creature) | HIGH | hero_weapon_dump + equipment_equip read +0x10 |
| creature_equipItem self-resolves objmgr, no cEngine needed | HIGH | prologue `mov ecx,[0xAD5C40]` |
| Vampiress start sword = id 1871 TYPE_WEAPON_VAMP_SWORD_NORMAL | MED-HIGH | `hero_weapon_dump()` slot 0x0D on a fresh game |
| ring/amulet sub-slot labels 0x07..0x0B | MED | BP equip those slots, observe model bone |

**One probe to run** (closes the only MED-HIGH item): new Vampiress
game → `hero_weapon_dump()` → read slot `0x0D` `type=` → expect **1871**.
If it differs, it will be one of {1872, 1868, 1771} and `items_gen.lua`
already has all four — just point `npc_equip(guard, IT.<that>, 0x0D)`.

### Lua-facing usage (with items_gen.lua + existing npc_equip)

```lua
local IT = require "items_gen"
-- arm a runtime-spawned guard with the Vampiress start sword:
npc_equip(guard_handle, IT.WEAPON_VAMP_SWORD_NORMAL, 0x0D)   -- main hand
npc_equip(guard_handle, IT.by_name["TYPE_ARMOR_DAEM_BODY"].id, 0x01)
for _, w in ipairs(IT.weapons()) do ... end          -- all 2761 weapons
IT.find("VAMP_SWORD")          --> {id=1871, name=..., key=..., cat="WEAPON"}
```
