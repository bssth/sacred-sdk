# 23. Community RE goldmine — `Downloads/refs/` mined index

Catalogue of everything three background agents extracted from
`C:\Users\bssth\Downloads\refs\` during the 2026-05-14 session. Recorded
here so the next /compact doesn't lose it — the original archives stay
in Downloads, the extracted copies were in `%TEMP%\refs_extract\` and
may be GC'd by Windows; refer to the **archive name** when re-mining.

We did NOT integrate most of these yet. Each section below tags **status**:

- ✅ already in SacredSDK
- 🟡 ready to integrate, not done
- 📋 backlog (V2/V3)

---

## A. Confirmed Sacred.exe addresses

Image base `0x00400000`, no ASLR / no `.reloc` → VAs stable. Build:
Steam 2006-10-13 (build 2.0.2.28).

### Already hooked / used by SDK

| VA | Symbol | Purpose | Status |
|---|---|---|---|
| `0x0080E680` | global.res loader detour | patch1 — serves custom/scripts/<lang>/global.res | ✅ |
| `0x00811440` | focus busy-wait | patch6 — stub returns TRUE | ✅ |
| `0x0080E780` | `sacred_hash(name) → hash` | runtime_triggers hook (catches every res:NNNN/name resolve) | ✅ |
| `0x0080EAF0` | dict lookup `(hash → ptr)` | text_logger trampoline | ✅ |
| `0x004915A0` | `SelfTriggerQuest` (Subsys_24) | runtime_triggers hook (0 fires for dialogs; dormant) | ✅ |
| `0x00491170` | `Dialog-Check` | runtime_triggers hook (0 fires for dialogs; dormant) | ✅ |
| `0x00463240` | `fire trigger by name` funnel | runtime_triggers hook (0 fires; dormant) | ✅ |
| `0x00808E50` | `cKernel::getSingleton(...)` | give_gold event-emit step 1 (cdecl, 3 ignored args, EAX=kernel) | ✅ |
| `0x008092F0` | `cKernel::receive_event(...)` | give_gold event-emit step 2 (__thiscall, ret 0xC) | ✅ |
| `0x00890B38` | event base vtable | static address used as `ev.vtable` for all engine events | ✅ |
| `0x00472BC0` | FunkCode interpreter dispatch | analysed but not hooked | ✅ docs only |
| `0x00475680` | outer FunkCode walker | analysed | ✅ docs only |
| `0x00671AD0` | `cScriptCompiler::*::loadScriptedSequenceR` | Path B PoC compile (FUN_00671ad0) | partially ✅ |
| `0x00671930` | cScriptCompiler sibling | partially ✅ |
| `0x0066EF40` | debug-log printer (variadic) | sacred_log_mirror — currently disabled (install race) | 🟡 needs SuspendThread fix |

### Found but NOT yet bound (V2 candidates)

| VA | Symbol | Purpose | Source | Status |
|---|---|---|---|---|
| `0x00669CA0` | `debug_output(const char*)` | NEW — alternative debug printer; simpler than 0x66EF40 | refs/Sacred_Modloader/Sacred 2.28 Beta.txt | 🟡 |
| `0x0057FD80` | unknown helper, takes `int amount` | Called from gold pickup + interpreter AddGold case BEFORE event emit. Likely the floating-text spawner OR coin sound | Agent A recon | 📋 worth a runtime probe |
| `0x0048DA40` | interpreter case 0x12 (AddGold) | full implementation of bytecode `AddGold N` | Agent A | ✅ docs |
| `0x0048DD10` | interpreter case 0x13 | adjacent to 0x12 | Agent A | 📋 |
| `0x0048F030` | interpreter case 0x14 | adjacent | Agent A | 📋 |
| `0x004ACF40` | loot drop / pickup handler | emits same GOLD_CHANGE event as 0x12 | Agent A | 📋 |
| `0x00558436` | sale completion | emits GOLD_CHANGE event | Agent A | 📋 |
| `0x0080E000` | freed by Inoff Patch 2 | space available for custom shellcode | Inoff 2.30 Source | 📋 |
| `0x0094DA7C` | ASCII `"HasItem:"` keyword | parsed by FunkCode parser at 0x00457306 | Agent A | 📋 — entry to runtime has-item |
| `0x004987B0` | tag 0x3a IF-handler | implements `HasItem:N` runtime check | Agent A | 📋 — bag walker via this path |
| `0x00451BE0` | FunkCode keyword→opcode table builder | maps `AddGold`/`Toete_NPC`/… to opcodes 0x50.. | Agent A | ✅ docs |
| `0x00451BF6` | "AddGold" keyword entry | inside above table | Agent A | ✅ docs |
| `0x00451F14` | opcode range start (0x50..) | inside above table | Agent A | ✅ docs |
| `0x0057FD80` | `Toete_NPC` leaf (NOT gold) | writes `[esi+0x4D8]=0` (HP), calls death anim FUN_0054D760(0x169) | Agent A | 📋 — useful for "kill NPC" mod |
| `0x0054D760` | death animation | takes anim id | Agent A | 📋 |
| `0x005FE000` | `cObjectManager::getData(itemid)` | item resolve by id | Agent A | 📋 |
| `0x004A15A0` | `cItem`-class handler (tag 0x4F) | reads `+0x1EC` (next-ptr) `+0x200` (flags) | Agent A | 📋 |

### Inoff Patch 2.30 — 10 documented patches (Thorium)

Source files (read for full asm before/after):
- `refs/Inoff_Sacred_Patch_Source/2.30 Patch/Patch Source Sacred.exe.txt` (828 lines)
- `refs/Inoff_Sacred_Patch_Source/2.29 Patch/2.29.12 Source.txt`

| # | Patch | VA(s) | Status |
|---|---|---|---|
| 1 | global.res from disk | varies | ✅ ours is patch1 |
| 2 | cheat-code clean removal (frees `~0x80E000` for custom code) | `~0x80E000` | 📋 useful staging space |
| 3 | Multi-Instancing (mutex bypass — run 2+ Sacred) | `0x848CC0`, `0x849014` | 📋 useful for testing co-op mods |
| 4 | chat-crash fix | `0x4E1FB0` | 📋 |
| 5 | version string change | `0xA28D80` | 📋 cosmetic |
| 6 | debugger-freeze (focus busy-wait) | `~0x811440` | ✅ ours is patch6 |
| 7 | WM_ACTIVATEAPP screen-clear off (NOP 2 bytes) | `0x812421`, `0x81243C` | 📋 cleaner windowed mode |
| 8 | **DLL injection via entry-point hijack** | EP `0x84C704` → JMP `0x6156E4` → push `<dll>`, call `LoadLibraryA`, JMP `0x84C709` | 📋 **alternative to ijl15.dll proxy** |
| 9 | console adjustment (drop error/version lines) | `0x6198B1`, `0x6198BB` | 📋 |
| 10 | window border in windowed mode | `0x813309` | 📋 |

---

## B. Hero struct offsets (from chain endpoint `[+0x6D5C40]→+4→+4→+0x3AC`)

Cross-verified across CT table, char.cpp (community), SacredGameTools shlib.

```c
struct cCreature {  // chain endpoint
    /*+0x010*/  u16  class_id;          // 1=Seraphim, 2=Glad, ..., 9=Daemon
    /*+0x01C*/  u32  pos_x;             // world coord — used by GOLD_CHANGE event
    /*+0x020*/  u32  pos_y;
    // ...
    /*+0x1A4*/  u32  equip_helmet;      // ┐
    /*+0x1A8*/  u32  equip_cuirass;     // │ 18 equip slots (4 bytes each)
    /*+0x1AC*/  u32  equip_belt;        // │
    /*+0x1B0*/  u32  equip_boots;       // │
    /*+0x1B4*/  u32  equip_gauntlets;   // │
    /*+0x1B8*/  u32  equip_bracers;     // │
    /*+0x1BC*/  u32  equip_amulet1;     // │
    /*+0x1C0*/  u32  equip_amulet2;     // │
    /*+0x1C4*/  u32  equip_ring1;       // │
    /*+0x1C8*/  u32  equip_ring2;       // │
    /*+0x1CC*/  u32  equip_ring3;       // │
    /*+0x1D0*/  u32  equip_ring4;       // │
    /*+0x1D4*/  u32  weapon_left;       // │
    /*+0x1D8*/  u32  weapon_right;      // │
    /*+0x1DC*/  u32  equip_cannon;      // │
    /*+0x1E0*/  u32  shoulders;         // │
    /*+0x1E4*/  u32  greaves;           // │
    /*+0x1E8*/  u32  wings;             // ┘
    // ...
    /*+0x3CC*/  u8   skill_ids[8];      // SkillType enum, see refs/Creature.pak_Editor
    /*+0x3D4*/  u8   skill_levels[8];   // NEW (Agent B)
    // ...
    /*+0x3EE*/  u32  gold;
    // ...
    /*+0x4D4*/  u32  max_health;        // NEW (Agent B)
    /*+0x4D8*/  i32  current_health;
};
```

**Unknown but very-likely**: backpack pointer / inline array. Located
somewhere past `+0x500` per static-analysis frequency scan, but no
community tool reads it live → needs **runtime trace** (x64dbg with
hardware-write BP on `[hero+0x1E8]` (last equip slot) or `[hero+0x3EE]`
(gold) while picking up an item; the caller VA of the inventory write
points at the bag).

---

## C. PAX save-file format (offline read/write source)

Magic + structure pulled from `SacredGameTools-main/shlib/Hero.cpp`,
`sacred_charmodif/classHero.pas`, `HeroDump_Quellcode/SacredHeroFile.cpp`
(three independent confirmations).

```
File magic:  0x07484D41 ("AMH\x07")  Sacred Classic 1.8.x
             0x1B484D41              Underworld 2.21+
Section table starts at file offset 0x100.
Compression: zlib-deflate, marker = 0xBAADC0DE.
```

Sections we care about:

| Type | Name | Compressed | Notes |
|---|---|---|---|
| `0xC3` | SECTION_HEROATAGLANCE | yes | level/name/portrait for import menu; **CheatFlag @0x95** (`0x81` = cheats used) |
| `0xC4` | appearance | no (40 bytes exact) | AAGlance/portrait data |
| `0xC7` | SECTION_HERO | yes | main hero data — full stat block, mirrors in-memory but with ~27-byte header shift |
| `0xC8` | SECTION_INVENTORY | yes | items as `ItemRecord[]`; `0xFEEDF00D` magic = real item, `0xBAADF00D` = filler |
| `0xCA` | SECTION_PLACEMENT_BP | yes | backpack 2D grid; `PlacementCell[width*height]` (12B each) + 10 weapon-set slots @ 0x100C |
| `0xCB` | SECTION_PLACEMENT_CH | yes | character slots placement |
| `0xCD` / `0xCE` | other | yes | filled in by sacred_charmodif |

### Hero data offsets in 0xC7 (decompressed, Classic vs UW)

```
Field                       Classic    UW
char_type        u32         0x0395    0x03DD
experience       i32         0x0399    0x03E1
attribute_points u8          0x03B0    0x03F8
strength         i16         0x039D    0x03E5  (and other 5 stats every +2)
skill_type[8]    u8          0x03B1..  0x03F9..
skill_level[8]   u8          0x03B9..  0x0401..
skill_bonus[8]   u8          0x03C1..  0x0409..
free_skill_pts   u16         0x03CF    0x0417
gold             u32         0x03D3    0x041B
level            u32         0x03E3    0x042B
combat_arts_count u8         0x0483    0x04CB
combat_arts_arr  22B/entry   0x0489    0x04D1
```

(In-memory gold at `+0x3EE` is the same field at runtime, with ~`+0x1B`
header shift over disk-layout `+0x3D3`.)

### Item record layout (inventory section 0xC8)

```c
struct ST_ITEM_HEADER {              // ~0x100 bytes per record
    u32 itemid;                       // 0 = empty filler, 0xBAADF00D = "bad food"
    u32 feedfood;                     // 0xFEEDF00D for real items
    u16 itemtype;                     // 257=large, 385=variable, 386=standard
    u32 sixtyfour;                    // typically 64
    u32 slotindex;                    // ← key linking to placement grid
    u32 itemid2;
    /* ... fluff buffers, slotid2/3, flags, randgarbage ... */
    u8  slotcount;                    // sockets count
};
struct PlacementCell {                // 12 bytes
    u32 index;                        // slotindex of item occupying this cell
    u8  slot_used;
    u8  width, height;                // only set in top-left cell
    u8  unknown;
    u32 item_id;                      // res-id (for icon lookup)
};
```

Slot colors (used by ST_ITEM_HEADER `usageid` fields):
`0=none / 1=bronze / 2=silver / 3=gold / 4=green / 5=platinum`.

---

## D. Catalogues / constants we can lift as Lua dictionaries

### Skill IDs (1..33)

Verified in `refs/Creature.pak_Editor` and `sacred_charmodif/SkillsTable.pas`:

```
 1=HeavenlyMagic    11=Armor           21=Trading        31=DwarvenLore
 2=WeaponLore       12=Meditation      22=Riding         32=Hellpower
 3=LongHandled      13=BladeCombat     23=Disarming      33=ForgeLore
 4=SwordLore        14=MagicLore       24=UnarmedCombat
 5=AxeLore          15=FireMagic       25=Concentration
 6=DualWielding     16=WaterMagic      26=Ballistics
 7=RangeCombat      17=EarthMagic      27=TrapLore
 8=Agility          18=AirMagic        28=Bloodlust
 9=Parrying         19=MoonMagic       29=WeaponTechnology
10=Constitution     20=Vampirism       30=TwoHandedWeapons
```

### Spell catalogue (236 entries)

Full enum in `refs/SacredGameTools-main/SacredHeroManager/plugins/SpellsAndArts/Spells.h`.

### Creature classes (15)

`Hero / Monster / NPC / Horse / Undead / Animal / Mercenary / Goblin /
Demon / Dragon / Energy / Elve / Enemy / Human / Dryade`.

### Character class flags (8-bit mask)

```
Seraphim=1   Gladiator=2   BattleMage=4   DarkElf=8
WoodElf=16   Vampiress=32  Dwarf=64       Daemon=128
AllClassic = 63 (Sacred Classic)
AllUW      = 192 (Underworld additions: Dwarf+Daemon)
```

### Weapon item types (`Weapon.pak`)

```
1=Dagger, 3=Sword, 4=TwoHandSword, 5=Axe, 6=TwoHandAxe,
7=Shield, 8=Bow, 9=Crossbow, 10=Fist, 13=Armor,
14=Ring, 15=Amulet
```

### Bonus-mod ID enums

Damage-vs-X / for-class / for-element. Full list in
`Weapon.pak_Editor/MainModule.bas`.

---

## E. Networking (LAN multiplayer mod foundation)

`refs/SacredGameTools-main/SacredFilter/` is a working **LAN proxy +
scripting engine** in C++. Use it as a reference for any future
networking-oriented SDK feature.

### Sacred LAN UDP broadcast packet (decoded by `SacredAncariaConnection-main`)

```
offset 0..1   ???
offset 2..3   u16 port (LE)
offset 4..7   u32 IP (reversed: data[7..4])
offset 8      u8  flags:
                bit 7 = Started
                bits 4-6 = GameMode (mask 0x07)
                bit 2 = Locked
                bit 1 = Pass-protected
offset 9      u8  difficulty
offset 12     u8  current player count
offset 13     u8  max player count
offset 14..   24 chars UTF-16LE = server name
```

Wrapped in 4-byte original-header + zlib-deflate body + Base64 for HTTP
relay (SacredAncariaConnection.com).

### TCP packet format

`SacredGameTools-main/SacredFilter/SacredSocket.cpp` has the header +
body layout. CRC32 table copied from `TINCAT2.DLL @ 0x4986C` (Sacred's
networking middleware) — see `SacredCRC32.h`.

### Config knobs

`Gameserver.cfg` → `NETWORK_PORT_LISTEN` (UDP)
`Settings.cfg` → `NETWORK_PORT_TCP`
In-game: Settings → Network → MODEM/ISDN sets retransmission mode.

---

## F. PAK file format internals (Resacred remake)

`refs/Resacred-master/src/rs_file.h` + `rs_file.cpp` = **complete** spec
of every `.pak` format with `static_assert(sizeof(...))`. Structures
defined:

```
PakHeader        256B
PakTextureHeader  80B
KeyxSector       0x300
WldxEntry         32B
PakStatic         64B
PakItemType      128B   ← itemTextureId, mixedId, spawnInfoId,
                          soundProfileId, category, marginX/Y, nameStr
FloorEntry        16B
PakTile           64B
SectorxData       (dynamic, zlib-compressed)
```

Loaded by Sacred:
- `Pak/Creature.pak` — creature stats
- `Pak/Weapon.pak` — weapon stats
- `Pak/static.pak` — static world props
- `Pak/iso/floor.pak` — tile floor data
- `Pak/iso/tile.pak` — tiles
- `Pak/mixed.pak` — texture atlas
- `Pak/world/sectors.keyx` + `sectors.wldx` — world streaming index

The **IDA databases** `refs/Resacred-master/ida_db/Sacred_2.idb` (~80 MB)
and `Sacred_3.idb` (~88 MB) contain renamed functions from a prior RE
session. Opening either in IDA Pro and exporting the symbol list would
be the single biggest information gain available to us. **Highest
priority for future session if anyone has IDA.**

---

## G. Other community formats / artifacts

### `SUWM` mod-format (Sacred Underworld Modloader)

`refs/Sacred_Modloader/Mod-Format.txt`:

```
PE-EXE +
ModHeader { SUWM, version=1, compressed_size, uncompressed_size, crc32 } +
zlib(infos + TOC + files) +
<offset DWord at-end>

infos: name(string), version(DWord), language(byte),
       sacred_version(byte), debug_message(string)
TOC:   count(DWord) + N × (filename, offset, size)
```

Useful if we want SacredSDK to import community mod packages.

### Cheat Engine table (`Sacred (Public) 1.0.CT`)

`refs/Sacred (Public) 1.0.CT` is XML — directly readable. Confirms our
pointer chain (`[+0x6D5C40]→+4→+4→+0x3AC`) and has a 4096-byte structure
dissect with auto-discovered fields (mostly unlabeled but useful as a
heat-map of "this offset is populated").

Notable extra finding: `Sacred.exe+0x1500DF` is the "is combat-art slot
N applicable to my class?" check (compare `ecx` against `0xA8`). Useful
if we want class-aware CA dispatch.

---

## H. Skipped (low value)

Quickly inspected, nothing reusable:

- `BHVS_*.zip` — hero vault, save-file UI only
- `SacredMagician-master.zip` — Kotlin balance.bin editor (we already know that fmt)
- `SacredUtils-master.zip` — C# launcher / settings configurator
- `SacredUnderworldHelper-master.zip` — windowed-mode wrapper
- `CheatersNightmare.7z` — binaries only, no sources
- `process_analyser.zip` — binary only
- `sacred_modding.zip` — Google Sheets HTML dumps; already have CSVs
- `suac_recoded_127.zip` — account-manager binaries
- `HeroChecker_0.9.9.0.zip`, `HeroCompare_*.zip`, `Sacred PaxFile Editor.7z`,
  `Char-Planer.zip`, `Charplaner*.zip` — save-file UI tools
- `STPSetup.exe` — installer
- `pureHD.tar`, `she.7z`, `char.7z`, `Chars.7z` — pre-built save files +
  art assets
- `*.rar` (Modeling Guide, PakExtractor, grn2gr2_conv, SacredMdlExtr,
  sacredtools3.3) — 3D-modeling Granny format (out of current scope)
- `drive-download-*.zip` — not opened (37 MB, may aggregate above)

---

## I-pre. NEW DISCOVERY (2026-05-14 evening session): native banner system

`res:17631..17637` are **Sacred's NATIVE top-of-screen banner resource
IDs**, NOT cutscene panels as I initially assumed. They're queried via
sacred_hash whenever Sacred wants to show one of its built-in banners:

- "Group eliminated" (after killing an enemy group)
- "Quest completed" / "Quest failed"
- The intro cutscene's 7 panels
- Possibly other system events

This means: instead of using our ImGui-rendered toast (`overlay::push_toast`),
we could call Sacred's **native** banner-show function — gives us a
visually-consistent feel with the rest of the engine (correct font,
fade animation, position) and works on any resolution.

The bytecode opcode that triggers a native banner is **tag 0x84
`ShowJokeImage`** (from the FunkCode interpreter). Find its handler in
`FUN_00475680 case 0x84` — that's `show_native_banner(res_id)` with
high probability.

If we can call that function with our own resource id (which we can
register via T() → global.res), we get a native banner with custom text.

**Priority: V2 task.** Effort: ~30 min (locate handler + bind in
`sacred_show_banner`). Replaces the ImGui toast queue.

## I-pre2. Quest book RE — NOT YET DONE (V2 task)

Setting an arbitrary qbit + emitting tag-0x35 QuestLogSet records does
**not** create a new visible journal entry. Sacred has a separate
quest book that registers each valid quest_id along with at minimum:
- resource_prefix (the `NQ_<N>`/`HQ_<...>`/`DQ_<...>` naming scheme
  used to look up Title/Header/Open/Sieg texts via global.res)
- class restriction (Sera/Glad/etc — quest 7615 "Scorpion Poison"
  appears to be class-gated; setting its qbit on Sera doesn't open
  the journal slot)

Tested 2026-05-15 evening:
- Set qbit 9512 (made-up id) + bytecode-perfect tag-0x35 records → no
  visible journal entry. Confirms 9512 not in the registry.
- Set qbit 7615 (vanilla quest "Scorpion Poison") + T.named override
  of NQ_7615_LOG_TITLE/HEADER/OFFEN/SIEG → still no visible entry on
  Sera. Suggests class-restriction in the registry.

Path to RE:
1. Find where Sacred reads the quest book during init. Candidates:
   - One of the `bin/*.bin` files (not yet decoded)
   - Inside Balance.bin or a sibling
   - Compiled into Sacred.exe (static array)
2. Once located, two approaches:
   - Patch the registry at boot (extend with our entries before
     Sacred reads it)
   - Provide a runtime Lua API that re-writes the in-memory registry
     after Sacred loads it

Effort estimate: ~1-2 hour focused agent + decompile pass.

**Until done**: brand-new visible quests not possible. Workable
alternative is **reskin via T.named** (HQ_3_2_1 → "Lost Tome" works
end-to-end), or chain off existing vanilla quest activation.

## I. Prioritised wishlist for future sessions

In rough effort-vs-impact order:

1. **Open Resacred IDA databases** → export symbol list. ~1 hour with
   IDA Pro. Single biggest source-of-truth for Sacred internals we
   could possibly get.
2. **Backpack walker via runtime trace** (x64dbg hw-write BP on
   `[hero+0x1E8]` while picking up an item). ~30 min. Unlocks
   `ctx:has_item` over full inventory + foundation for `ctx:give_item`.
3. **Implement `ctx:hero_class()`, `:hero_level()`, `:experience()`,
   `:skill_level(N)`, `:max_hp()`** — all reads, offsets already known.
   ~1 hour.
4. **Sacred-native debug printer (FUN_00669CA0)** → mirror to sdk_log.
   Replaces the disabled sacred_log_mirror (FUN_0066ef40). ~1 hour.
5. **Multi-Instancing patch** (Inoff Patch 3) → run two Sacreds for
   co-op mod testing. ~1 hour.
6. **PAX save SDK** — `sacred.read_save(path)` / `sacred.write_save(path, t)`
   from Lua. Re-uses zlib already in Lua state via lua-zlib. ~1 day.
7. **LAN message-relay hook** — bridge between mods on two LAN-connected
   Sacreds. Requires TINCAT2 reverse-engineering. ~1 week.
8. **Move SDK injection to Inoff Patch 8 path** (EP-hijack) → no more
   ijl15.dll proxy needed. ~half day. Optional polish.

---

## J. File path quick-reference

Original archives (do not delete):
`C:\Users\bssth\Downloads\refs\`

Extracted at time of mining (may be GC'd):
`C:\Users\bssth\AppData\Local\Temp\refs_extract\`

To re-extract a single one:
```
python -c "import zipfile; zipfile.ZipFile(r'C:\Users\bssth\Downloads\refs\X.zip').extractall(r'C:\tmp\X')"
pip install py7zr   # for .7z
python -c "import py7zr; py7zr.SevenZipFile(r'C:\Users\bssth\Downloads\refs\X.7z').extractall(r'C:\tmp\X')"
```

Top-3 most-valuable archive paths:
- `C:\Users\bssth\Downloads\refs\Resacred-master.zip` (37 MB) — RE project + 2 IDA DBs
- `C:\Users\bssth\Downloads\refs\SacredGameTools-main.zip` (848 KB) — save fmt + TCP networking
- `C:\Users\bssth\Downloads\refs\Inoff_Sacred_Patch_Source.zip` (18 KB) — 10 EXE patches with asm
