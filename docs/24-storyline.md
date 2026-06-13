# 24 — Storyline SDK: FunkCode grammar + runtime story layer

Sacred's entire storyline (quests, NPCs, dialog, voice, triggers, items, scripted moves/teleports) is authored as one binary FunkCode record stream per playable char (`bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin` = retail Vampiress: 3.96 MB, 125,060 records, 102 distinct tags). It is a compiled scripting VM.

Why everything is runtime, never a .bin edit (mechanically proven): there are no stored jump offsets anywhere. `IF`(0x3a) / `ELSE`(0x3b) / `ELSEIF`(0x42) and the 0x29/0x2a skip-latch skip blocks by re-walking the TLV stream forward and summing big-endian record sizes from the shared cursor until the terminator. Insert/delete one byte → size-tiling desyncs → IF/ELSE land mid-record → intro hang. So the SDK replays story constructs at runtime via the cObjectManager/cQuestMgr singletons; it never patches FunkCode. (Full proof: `.claude/knowledge/re/funkcode_tags.md`.)

## Source-of-truth scratch reports

| Report | Covers |
|---|---|
| `funkcode_tags.md` | record walker `FUN_00475680` full tag→handler table; framing; IF/ELSE size-resume mechanism; script-keyword↔tag map |
| `triggers_dialog_move.md` | ~127 script keywords→opcodes (3 resolvers `FUN_00452370/00451be0/00452910`); trigger/condition/action catalog; dialog 0x1f + voice (SOUND_FX table @0x00964870); cCreature command-ring +0x588/+0x58c stride 0x44 (type 1=MoveTo, 9=Teleport, 0xe=PlaySound); Alcalata case study |
| `quest_storyline.md` | quest registry recipe; the two cQuestMgr name arrays; overhead "?!" marker renderer `FUN_00599910`/selector `FUN_00499e90` |
| `items.md` | items & creatures share ONE create path & id space; `dropItem 00611620`; `equipItem 00611560`; equip slots `cCreature+0x1A4` |
| `quest_catalog.py` | parser that produces a full per-quest catalog of the retail campaign (the ~14k-line generated dump itself is not kept — regenerate on demand) |
| `npc_model.md` | CreateNPC (tag 0x01) grammar, cCreature offsets, faction model |

## The grammar in one screen

Record = `tag:u8 · size:u16 BE (incl 3-byte hdr) · payload`. Walker `FUN_00475680` = `switch((u16)tag)` → handler `FUN_004xxxxx`. Payload = opcode stream via `FUN_00472bc0` (field id: 1=ASCIIZ, 0xb=i32, 2=type/handle, 3=u16, 4/0xc/0xd=POSITION X/Y/sector, 0x20=resolved CPOS:hero, 0=END). Script keywords compile 1:1 to tags.

- NPC: CreateNPC 0x01, EquipNPC 0x07, DelNPC 0x37, NPC_Goto 0x48, NPC_TalkTo 0x47, Teleport 0x2e, Morph 0x4f, PlayAnim 0x5b.
- Quest: SetUpQuest 0x15, TriggerQuest 0x14, ActivateQuest 0x75, QuestBook 0x35, QuestKompass 0x3f/0x40, UnTriggerQuest 0x4d, Belohnungen 0x20, conditions QuestDone 0x0a / InProgress 0x0c.
- Dialog/voice: Dialog 0x1f, SetButton 0x3c, Text 0x1a, PlaySound 0x68 (voice = `SOUND_FX_<name>` → id via `FUN_00676170`), Wait 0x5a, Popup 0x5f, SetFocus 0x46.
- Triggers/events: SetBaseTrigger 0x04, CreateTrigger 0x30, SetOnKill 0x87, SetOnCollect 0x88, MouseEvent 0x5e, RegionChange 0x74, MoveOver 0x10; control IF 0x3a / ELSE 0x3b / ELSEIF 0x42; SetTimer 0x38.
- Items/world: CreateObj 0x08, FillChest 0x4e, SetDrop 0x89, Waffenpool 0x72, AddGold 0x12, AddExp 0x11.
- Flags/vars: DefNum 0x41, SetVar 0x43, SetVarBit 0x44 (HeroQBit), IncVar/DecVar 0x4b/0x4c, DefPos 0x17 (named position store).

## NPC spawning (engine CreateNPC + dev templates)

Do not reconstruct an NPC's combat state by poking cCreature bits (HP/AI/faction/combat-arts). That path produced edge bugs (1-hit death, Seraphim "BFG" combat-art, red thrall glow, no-retaliate, hero-aggro) because each field has cross-coupled side-effects and the bare `cObjectManager::create` skips the engine's init pass. Instead, spawn through the engine's own CreateNPC handler from a dev-authored template — the engine does type-correct init exactly like a hand-placed campaign NPC. Then apply at most one surgical delta for the difference you actually want.

```lua
local N = require "npcobj"
-- dev archetypes (npc_templates.lua, extracted from 4182 hand-placed
-- campaign CreateNPC records): friendly_town_guard, patrol_soldier,
-- bellevue_enemy, dormant_enemy, townsperson, quest_npc, ambient_animal,
-- ally_companion.
local o = N.spawn_template("friendly_town_guard",
            { type = NPC.VALORIAN_SWORDSMAN, pos = "CPOS:HERO", sub_id = 1 })
o:teleport(kx, ky)          -- engine move to the post (no struct writes)
o:stance(1, 7)              -- the ONLY delta: ally matrix class 7
o:equip(1729, 0x0D)         -- plain WEAPON_SWORD, main hand
```

- `sacred.createnpc_engine(payload, want_type)` (C++): synthesizes a TLV CreateNPC record from `npc_templates.lua` `M.build(arch,opts)` and calls the engine's `FUN_00482510` (`__thiscall`, ECX=ctx `0x00AACF80`, two stack args, `ret 8` — the 2nd arg is unused, pass 0; header `01 00 <size&0xff>`, record <256 B). The engine sets HP (scaled), AI controller, combat-arts, +0x200=0, FX textures.
- Why one delta: soldier TYPES (Valorian Swordsman, Knights, …) default to a HOSTILE hostility-matrix class — they are often enemy units. The engine resolves the `friendly_town_guard` template to an active class but still type-hostile; `o:stance(1,7)` flips `cCreature+0x1F0` to the ally cluster (7 == a real vanilla Valorian Soldier; friendly to the class-1 hero, fights monsters). Everything else stays engine-correct.
- Items: `custom/lua/lib/items_gen.lua` (5624 item types, npc.lua-style) + `o:equip(item_type, slot)` (slot 0x0D main weapon / 0x0C off / 0x12 mount). The Vampiress's live start objects are 7905/7906 but those are her BOUND special weapon (cause a hero energy-blast on another wielder) — use a plain catalog type (e.g. `WEAPON_SWORD`=1729).
- Glow/BFG root cause (closed): bare-spawn struct pokes (`+0x200=0x40200000`, FX-texture `+0xbc/+0xc0`, wrong `+0x1F4`). Gone entirely under the engine-CreateNPC path — never poke those.

## Runtime story layer (shipping)

`sacred.*` + `npcobj` methods. cQuestMgr singleton = `0x00AACF80`, cObjectManager = `*(0x00AD5C40)` (handle→cCreature as in earlier docs).

| API | Mechanism | Confidence |
|---|---|---|
| `sacred.spawn_item(type,kx,ky)` / `npcobj.spawn_item` | == proven creature spawn path with an item type id (one create path/id space) | High |
| `sacred.npc_quest_icon(h[,on])` / `o:quest_icon(on)` | `cCreature+0x14\|=0x80000` (force draw) + `+0x200\|=0x4000` ("?!" `npc_dialog_combo.tga`); pure memory, visual only | High |
| `sacred.npc_teleport(h,kx,ky)` / `o:teleport(kx,ky)` | engine `FUN_0054d9d0` (the Teleport-0x2e SP path) with ECX=NPC; non-destructive | High |
| `sacred.npc_set_name(h,str)` / `o:set_name(str)` | strncpy into DlgNPC(`qm+0x755c`/0x50) + NameArrA(`qm+0x358`/0x44) entry keyed by handle@+0, name@+4 | offsets High; no-op until the spawn has such an entry (open item) |
| `sacred.npc_equip(h,item,slot)` / `o:equip(item[,slot])` | `create_005fb530`+`equipment_equip_005 55e00`; slots `cCreature+0x1A4+slot*4` (0xC=weapon) | Med (ABI not BP-confirmed) — guarded, experimental |
| `o:make_quest_giver(name[,lvl])` | set_name + quest_icon + make_immortal_passive | composite |

(Plus the existing spawn/faction/stance/level/invuln/stationary layer from docs 23.)

## Open items (next BP session — see each report's "Open items")

1. NPC display name for a pure runtime spawn. CreateNPC builds the DlgNPC/NameArrA entry; `cObjectManager::create` spawns skip that, so `set_npc_name` no-ops. Fix = either append a DlgNPC entry (`qm+0x755c` vector grow — element ctor not yet reversed) or BP the nameplate/tooltip renderer to find the exact string source for a non-quest creature.
2. `npc_equip` ABI: BP `create_005fb530` / `equipment_equip_00555e00` to confirm arg count + slot→bodypart map (hero map says 0xC=weapon_l, 0xD=weapon_r, 0..0x12 per `player_state.cpp::read()`).
3. Item type-id table: build the `TYPE_*` item id list (table @0x008EC32C/0x008EC328 stride 0x44) the way `npc.lua` was generated, so modders pick items by name. (`item_type_id(name)` = `FUN_0043cd90`, conv unconfirmed.)
4. Quest registration/solve runtime path (registry vector `DAT_00aad3a4` stride 0x174 + `FUN_004b5370` resize, text via `FUN_00672740`/global.res hash): recipe documented in `quest_storyline.md` §A; not yet wired to Lua (touches an engine std::vector realloc — wants a BP before going live on the campaign).
5. Dialog show + voice playback (command-ring type 0xe / Miles MSS path) — recipe in `triggers_dialog_move.md` §B; ring-append helper needs a BP.
6. NPC walk-to (NPC_Goto SP: creature vtable+0x18 move-order struct) — recipe in `triggers_dialog_move.md` §C; teleport covers "go to point" for now.
