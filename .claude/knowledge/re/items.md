# Sacred Gold — ITEM / LOOT system — RE report

Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000,
no ASLR (file offset == VA − 0x400000), x86. Evidence: Ghidra decompiles in
`sdk/re/ghidra/decompiled/`, community refs (Resacred src, Weapon.pak
Editor `MainModule.bas`), in-binary string xrefs.

---

## TL;DR — the big result

**Items and creatures share ONE object-create path and ONE type-id space.**
There is no separate "item engine". An item is just an object whose
*definition record* has a different class byte. So the runtime recipe for
spawning a ground item is the SAME as the creature spawn recipe in
`runtime_spawn.md` — only the `type` id changes.

Three runtime entrypoints (all `__thiscall`, ECX = object, verified bodies):

| What | Function (VA) | Signature |
|---|---|---|
| **Create object by type id** (creature OR item) | `cObjectManager_create_005fba40` | `int create(ECX=objmgr, ushort* posBuf, type, char doNet, sendFlag, 0)` |
| **Create item, no position** (inventory/equip use) | `cObjectManager_create_005fb530` | `ref create(ECX=objmgr, uint type, char a3, char a4)` |
| **Equip item ref into creature slot** | `cCreature_equipment_equip_00555e00` | `ref equip(ECX=creature, uint slot, int itemRef, char sendNet)` |
| **Give creature an item by name (create+equip)** | `cEngine_creature_equipItem_00611560` | `void equipItem(ECX=engine, uint slot, int creatureRef, char* itemTypeName)` |
| **Drop existing item ref on ground (pickup-able)** | `cEngine_dropItem_00611620` | `int dropItem(ECX=engine, int itemRef, uint* posBuf, uint* posBuf2)` |
| **Add item to creature inventory** | `cCreature_inventory_putItem_00549260` | `ref putItem(ECX=creature, p1, type, itemRef, container)` |
| **Resolve item type-id from name** | `FUN_0043cd90` | `int getItemType(char* name)` |

Confidence: **HIGH** on the create-path unification, equip slot mechanism,
and dropItem entrypoint (decompiled bodies + string evidence). **MED** on
exact net-sync args and the equip-slot enumeration semantics (needs a BP).

---

## 1. Unified object create path & type-id space

`cObjectManager_create_005fba40` (`runtime_spawn.md`'s creature create) calls
`cObjectManager_create_005fb530` (`005fba40:33`). That inner function
(`005fb530`):

```
iVar7   = FUN_0043fc40(param_1, type)            // 005fb530:83  type-id remap (few special ids)
local_c8= FUN_00425ea0(iVar7)                    // 005fb530:84  -> definition record
puVar8  = cObjectManager_allocate_005fafe0(local_c8[0x2e], iVar7, slot)  // 005fb530:89
```

* **`FUN_00425ea0(id)` returns `id * 0x80 + base`** (`00425ea0`:`param_1*0x80
  + in_ECX`, guard `0 < id < 0x7e60`). A **128-byte-stride definition
  table**. This is the same record family Resacred decodes as
  `PakItemType` (`rs_file.h:386 static_assert(sizeof(PakItemType)==128)`,
  loaded from `items.pak`). **HIGH.**
* **`local_c8[0x2e]` = the class/category byte** at def-record offset
  `+0x2E`. It is passed as the class selector to
  `cObjectManager_allocate_005fafe0` → `cObjectFactory_create_00451300`
  (`005fafe0:94`), which instantiates the concrete C++ class
  (cCreature / cItemBase / cObject3D…). The npc_model report independently
  saw `*(byte)(iVar2+0x2e)` tested `1<x<4` on the creature path — same byte.
  **HIGH** that `+0x2E` is the object-class discriminator; **MED** on the
  exact value→class map (creature vs weapon vs armor vs static).
* **One type-id namespace.** `FUN_0043cd90` (`getItemType(name)`) and the
  creature name resolver both index a `TYPE_*` name table at
  **`0x008EC32C`, stride `0x44`**, parallel id array at **`0x008EC328`,
  stride `0x44` (id = first dword)**. Dumped entries: id0=`TYPE_INVALID`,
  id1=`TYPE_NPC_SERAPHIM`, id2=`TYPE_NPC_GLADIATOR`, … — i.e. creature
  `TYPE_NPC_*` and item `TYPE_*` names live in the SAME sequential table
  feeding the SAME `create`. Ids are sequential from 0. **HIGH.**
* Definition-table base global used by `FUN_00425ea0`/`FUN_0043cd90`:
  `DAT_00aab5e4` (the loaded items/Balance def block; `0043cd90:23`
  walks `uVar6 + 0x37 + DAT_00aab5e4`, 0x80 stride, comparing the name
  field — confirms 128B records, name near `+0x37`). **HIGH.**

**Conclusion for the SDK:** to spawn an item we do *exactly* the
`runtime_spawn.md` recipe but pass an **item type id** (resolved via
`FUN_0043cd90("TYPE_…")` or a CSV-baked id) instead of a creature id. The
engine's class byte `+0x2E` makes `cObjectFactory_create_00451300`
instantiate a `cItemBase`/`cObject3D` instead of a `cCreature`; from there
`cObjectManager_create_005fba40` parks it at the resolved tile just like a
creature, and it is a pickup-able world item.

`FUN_0043fc40` is only a small id-remap (`0x13c0..0x13c8 → …`); pass-through
for normal ids. Not a concern.

---

## 2. FunkCode tags that place / give items

FunkCode tag-name table = `FUN_00452370` (index = tag id → name string):

| idx | tag name | item relevance |
|---|---|---|
| 0 | `CreateNPC` | creature spawn (`FUN_00482510`) |
| **1** | **`CreateObj`** | **spawn a world object/item** |
| **2** | **`EquipNPC`** | **give an NPC equipment (visible weapon/armor)** |
| … | `FillChest` | put items in a chest |
| … | `Waffenpool` | weapon-pool (random drop set) |
| … | `SetDrop` | loot table on a creature/region |
| … | `SetOnKill` / `SetOnCollect` | death-drop / pickup hooks |

The FunkCode record walker `FUN_00475680` dispatches on the record's
class word (`local_a0c.pVFTable & 0xffff`): case 1→`FUN_00482510`
(CreateNPC), case 2→`FUN_0048ae90`, case 3→`FUN_0048bb40`, case 8→
`FUN_00485c10`. `FUN_00485c10` is the **generic placement handler**: it
builds a `Pos` buffer with the SAME primitives as the creature path
(`FUN_006224b0`/`FUN_006224e0` → `FUN_00635c40` sector resolve →
`FUN_00634840`) and then calls `cObjectManager_create_005fb530` /
`cObjectManager_create_005fba40` (`00485c10:1266,1313,1358,1411,1595,
1616,1639,3944`). This is the data-driven equivalent of our runtime recipe
and proves the create path is type-agnostic. **HIGH.**

CreateNPC's loot opcode **`0x67`** (`00482510:659`) stashes `local_694`
and writes it to **`cCreature[0x56].spare+2`** and **`[0x4c].pVFTable+1`**
(`00482510:1502-1507`) = the creature's **loot/drop-set id** (which items
drop on death), NOT a visible weapon. (Ghidra index `[N]` = byte `N*8`;
`[0x56].spare+2` = `cCreature + 0x2B2`, `[0x4c].pVFTable+1` = `+0x261`.)
Use `EquipNPC` / `creature_equipItem` for the visible weapon, `SetDrop`/
opcode 0x67 for what drops.

---

## 3. Equip — make an NPC visibly wield a weapon/armor

`cEngine_creature_equipItem_00611560(ECX=engine, slot, creatureRef,
itemTypeName)` (string `cEngine::creature_equipItem(%d,%d,%s)` @0x95EA64):

```c
obj = cObjectManager_getData_005fe000(creatureRef);          // resolve cCreature
if (RTTI is cCreature) {
    typeId   = FUN_0043cd90(itemTypeName);                    // name -> item type id
    itemRef  = cObjectManager_create_005fb530(typeId, 0,1,0); // create the item object
    cCreature_equipment_equip_00555e00(slot, itemRef, 1);     // equip + visual + net
    // slot 0xC / 0xD also trigger weapon-hand FUN_005d8730 refresh
}
```

`cCreature_equipment_equip_00555e00(ECX=creature, slot, itemRef, sendNet)`
(string `cCreature::equipment_equip(slot=%d,ref=%d,...)` @0x9567CC):

* `slot == 0x12` (18) → **mount/horse**: stored at `cCreature[0x7B]`
  (= `creature + 0x1EC`), sets `creature[5] |= 0x80`.
* `slot < 0x13` (0..18) → **equipment slot**: prior item in slot is
  destroyed (`cObjectManager_destroy_005fbdb0`), then the item ref is
  stored at **`*(int*)(cCreature + (slot & 0xffff)*4 + 0x1A4)`** (decomp
  `in_ECX + (slot&0xffff) + 0x69`, dword units → byte `0x69*4 = 0x1A4`).
  Then `FUN_00555300(slot)` rebuilds the visible model and a net event
  (opcode `0x11e`) is broadcast. The old item is unlinked from the item
  data manager via `cItemDataMgr_push_005fc8b0` and its world pos cleared
  (`+0x1C/+0x20/+0x24/+0x18 = 0`, `00555e00:187-190`). **HIGH** on the
  slot-array base `cCreature+0x1A4` (stride 4) and the destroy/refresh
  mechanism; **MED** on which slot index = which body part (need a BP;
  weapon hand appears to be slot `0xC`/`0xD` per `creature_equipItem`'s
  `FUN_005d8730(0,0/1,itemRef,1)` special-case @`00611560:31-37`).

So **`npc_equip(handle, "TYPE_…")`** ≈ call
`cEngine_creature_equipItem_00611560` with the engine singleton, or do the
two steps manually (create_005fb530 then equipment_equip).

---

## 4. Drop a pickup-able item on the ground

`cEngine_dropItem_00611620(ECX=engine, itemRef, posBuf, posBuf2)` (string
`cEngine::dropItem (%d) failed!` @0x95EA8C, `cChest3D::dropItems` reuses it):

* `posBuf` / `posBuf2` are the standard 16-byte `Pos` buffers
  (`{u16 sector; u32 X; u32 Y; u8 level}`) as in `runtime_spawn.md`. If
  `posBuf2==NULL` it scatters around `posBuf` with a small random offset
  and `FUN_00635c40` sector-resolves it (`00611620:100-109`) — same
  resolver as creature spawn.
* It searches up to 8 nearby free tiles (`00611620:122-149`,
  `cWorld_getParentObject_006354d0`/`FUN_00634c50`/`FUN_00636c10`) so the
  item lands on a walkable, unobstructed cell (true ground loot, not
  stacked).
* `iVar9 = cObjectManager_getData_005fe000(itemRef)` resolves the item
  object; `cItemDataMgr_push_005fc8b0(itemRef)` detaches it from any
  inventory/container linked list (`+0x14 |= 2` = "in world / free");
  then it is placed and net-broadcast (opcode `0x140`,
  `00611620:238-261`). The item now lies on the ground and the hero can
  walk over and pick it up. **HIGH.**

So **`spawn_item(type, kx, ky)`** = create the item object via the
`runtime_spawn.md` path (which already parks it at the tile), OR create it
position-less with `create_005fb530` and then call `dropItem` with a built
Pos buffer for the nicer "scatter onto nearest free tile" behaviour.

`cCreature_inventory_putItem_00549260` adds to a creature's inventory
(auto-creates the item via `create_005fb530` if `itemRef`/type slot is
empty, `00549260:28-32`); on "no empty slot" it falls back to dropping
into the world (string `cCreature::inventory_putItem() <no empty slots /
drop into world>` @0x9563D8).

---

## 5. Item property / record model

Two distinct structures:

**(a) Item *type definition*** — 128-byte record in the `items.pak`/def
table (`FUN_00425ea0`, base `DAT_00aab5e4`). Resacred `PakItemType`
(`rs_file.h:359`):

| off | field |
|---|---|
| +0x00 | flags |
| +0x08 | itemTextureId |
| +0x10 | mixedId (3D model / mixed-data link) |
| +0x18 | spawnInfoId |
| +0x24 | soundProfileId |
| **+0x2D** | category/family (`u8`) — *near the `+0x2E` class byte the engine reads* |
| +0x37 | nameStr[32] (the `TYPE_…` name `getItemType` matches) |
| +0x4E.. | someVectorId, marginX/Y |

(Resacred field offsets are approximate; the engine's class discriminator
is the byte at **def+0x2E** per `005fb530:89`.)

**(b) Item *instance*** — the per-item runtime/save struct, documented by
Weapon.pak Editor `SacredItem` (`MainModule.bas:123`), this is what gets
created as the object and stored in inventory/equip slots / Hero saves:

```
ID, ModellID, Flags1, Flags2, ItemTyp(u8), SkillID(u8), SkillValue(u8),
Sockel[8](u8),
RequireMinLevel/Strength/Dexterity/Charisma/Endurance(u8), ItemLevel(u8),
Phys/Fire/Magic/Poison DmgMin[i16], …DmgMax[i16],
Offensive[i16], Defense[i16], Phys/Fire/Magic/PoisonRes[i16],
BonusID[8](i32), BonusValue[8](i16)        // the modifier/bonus list
```

So an item is defined by: **type id** (selects model/category/base via the
128B def record) + an **instance** carrying `ItemLevel`, requirement
stats, damage/defense, sockets, and up to 8 `(BonusID,BonusValue)`
modifiers. The engine create path produces the instance from the type id
with defaults; bonus/quality fields are then set on the instance object
(offsets to be confirmed at runtime against a live created item — they map
onto the `cItemBase` object, not the def table). **MED** (layout from
community editor, not yet byte-verified against this build's live object).

---

## 6. Proposed RUNTIME recipe (C pseudo-code)

Mirrors `runtime_spawn.md`. `OBJMGR = 0x00AD5C40`, `cWorld = 0x00AD3560`.

```c
// shared helpers (from runtime_spawn.md)
struct Pos { uint16_t sector; uint32_t X; uint32_t Y; uint8_t level; }; // use 16 bytes

static void* objmgr(void){ return *(void**)0x00AD5C40; }
static void* cengine(void){ /* engine singleton — resolve once; the
    equip/drop calls take ECX=cEngine. Easiest: reuse the SDK's existing
    engine ptr if it has one, else BP one creature_equipItem call to grab
    ECX. */ }

static int resolve_pos(Pos* b, uint32_t X, uint32_t Y){
    FUN_006224b0(/*ECX*/b, /*sector*/0, X, Y, /*level*/0);   // __thiscall ret0x10
    void* w = *(void**)0x00AD3560;
    if(!w){ w = (*(void***)0x00AD5C40)[0]; *(void**)0x00AD3560 = w; }
    return FUN_00635c40(/*ECX*/w, (uint16_t*)b) && b->sector; // 0 => off-map
}

// type id: either bake from the TYPE_* CSV, or resolve by name at runtime:
//   int type = ((int(__cdecl?)(...))0x0043cd90)("TYPE_SWORD_xxx");
//   NOTE: 0043cd90 is __thiscall-ish? It takes only (char* name) in EAX/stack;
//   call as plain func(name); ECX unused in body. Verify w/ BP.

// 1) spawn_item(type, kx, ky): identical to spawn creature, item type id
int spawn_item(uint32_t type, uint32_t kx, uint32_t ky){
    Pos b;
    if(!resolve_pos(&b, kx, ky)) return 0;             // off-map guard
    // create_005fba40(ECX=objmgr, &posBuf, type, doNet, sendFlag, 0)
    int ref = cObjectManager_create_005fba40(/*ECX*/objmgr(), &b, type, 0, 1, 0);
    return ref;   // object now lies on the tile, pickup-able
}

// 1b) variant using the nicer "scatter to nearest free tile" drop:
int spawn_item_drop(uint32_t type, uint32_t kx, uint32_t ky){
    int ref = cObjectManager_create_005fb530(/*ECX*/objmgr(), type, 1, 0); // no pos
    if(!ref) return 0;
    Pos b; resolve_pos(&b, kx, ky);
    cEngine_dropItem_00611620(/*ECX*/cengine(), ref, (uint32_t*)&b, 0);
    return ref;
}

// 2) npc_equip(creatureRef, type/name, slot): visible weapon/armor
void npc_equip(int creatureRef, const char* typeName, uint slot){
    // simplest: one call does create + equip + visual + net
    cEngine_creature_equipItem_00611560(/*ECX*/cengine(), slot, creatureRef, typeName);
    // manual equivalent:
    //   int t  = FUN_0043cd90(typeName);
    //   int it = cObjectManager_create_005fb530(objmgr(), t, 0,1,0);   // ECX=objmgr
    //   cCreature_equipment_equip_00555e00(/*ECX*/creatureObj, slot, it, 1);
    //   (creatureObj = *(void**)(*(void**)(objmgr()+4) + creatureRef*4))
    // slot: <0x13 = equipment; 0x12 = mount; weapon hand ≈ 0xC/0xD (verify w/ BP)
}
```

### Lua-facing API (maps to the above)

```
spawn_item(type, kx, ky)            -> create_005fba40 at resolved tile
give_item(kx,ky, type)              -> spawn_item_drop (scatter)
npc_equip(npc_handle, type, slot)   -> creature_equipItem
npc_set_loot(npc_handle, dropset)   -> CreateNPC op 0x67 OR write cCreature+0x2B2
list_item_types()                   -> walk TYPE_* table @0x008EC32C/0x008EC328
item_type_id(name)                  -> FUN_0043cd90
```

---

## Open items needing a runtime breakpoint

1. **`def+0x2E` class-byte value → object class map** (which value =
   creature / weapon / armor / consumable / static). BP read at
   `005fb530:89` (`cObjectManager_allocate_005fafe0` arg1) while creating a
   known creature vs a known weapon; record the byte. (HIGH priority.)
2. **Equip slot enumeration.** BP `00555e00` entry; call
   `creature_equipItem` with various slots and observe which body part /
   model bone updates. Confirm weapon hand = 0xC/0xD (from
   `00611560:31-37` `FUN_005d8730(0, 0|1, itemRef, 1)`).
3. **`cEngine` singleton address.** The equip/drop calls need ECX=cEngine.
   BP `0x006115A1` (inside `creature_equipItem`) and read ECX; or find the
   global the SDK already uses. (`cObjectManager` @0x00AD5C40 is known.)
4. **`FUN_0043cd90` calling convention.** Body uses only `param_1` (name);
   no ECX use — likely `__fastcall`/`__cdecl` single arg. Verify arg
   register at `0x0043CD90` before calling from the DLL.
5. **Item-instance bonus/quality field offsets** on the live `cItemBase`
   object (BonusID/BonusValue, ItemLevel, requirements) — map the
   Weapon.pak `SacredItem` layout onto the created object via BP after
   `create_005fb530`. (For exposing item properties to Lua.)
6. **`create_005fba40` exact arg order for items.** Reuse the verified
   creature call shape from `runtime_spawn.md` (same function); sanity-BP
   first item spawn to confirm the item isn't parked at the default
   KompassPos.
