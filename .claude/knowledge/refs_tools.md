# Community Modding-Tool Source Mining — `refs_tools.md`

Mined from four community repos extracted to `E:/refs_extract/`:
`SacredGameTools-main`, `SacredUtils-master`, `SacredUnderworldHelper-master`,
`SacredLumina-master`. Focus: file formats, engine offsets, data tables, reusable
algorithms. Game EXE base `0x00400000`, no ASLR (file offset = VA − 0x400000).

> **Bottom line:** SacredGameTools is the single highest-value repo — it contains
> the *complete, authoritative* C++ source for the PAX/PTX hero-save format
> (compile + decompile), the inventory/item/placement binary layout, the hero
> stat-block offsets, the 236-entry spell-id table, and Sacred's TINCAT2 network
> CRC32. The other three are C#/JS app-level tools that add: the `Settings.cfg`
> key list, the per-class `startcode.bin` spawn-position offsets, the live-memory
> HP pointer chain, and the `global.res ↔ CSV` round-trip recipe.

---

## Per-repo summary

### 1. SacredGameTools-main — C++ (MFC / Win32), VC6-era, by SonicMouse (2005)
A bundle of Sacred tools. Sacred-specific, reusable parts:

| Sub-tool | Lang | What it gives us |
|---|---|---|
| **`shlib/`** | C++ | **Reusable DLL** implementing the whole PAX hero-save container: `CHero::DecompileHero` / `CompileHero` (`Hero.cpp/.h`), `CZlibWrapper` (zlib inflate/deflate), `CPointerArray`. **This is the canonical save-format reader/writer.** |
| **`SacredItemManager/`** | C++ MFC | `CItemManager` (`ItemManager.cpp/.h`), `CItem`, `CHero` wrapper — full inventory/item/placement binary parse+rebuild; `SITFile.cpp` = `.SIT` single-item export format. |
| **`SacredHeroManager/plugins/`** | C++ DLLs | Stat-block offset truth: Gold, Level+Exp, Skills+Attr, Spells+Arts (with `Spells.h` = 236 spell-id→name table), Dump (raw section dump). Plugin ABI = `seaseven_plugin_entry`. |
| **`SacredLevelUnlock/`** | C++ CLI | Difficulty-unlock byte in section `0xC3` (HEROATAGLANCE) @ `+0xEE`. |
| **`SacredSugarDaddy/`** | C++ MFC | Gold editor — confirms gold in section `0xC7` @ `+0x3D3`. |
| **`SacredMSB/`** | C++ MFC | "Max Survival Bonus" — writes section `0xC4` @ `+0x08` = `0x0030FFFF`. |
| **`SacredFilter/`** | C++ MFC | LAN-lobby filter/proxy with a VBScript/JScript plugin engine (`msscript.ocx`). **Contains `SacredCRC32.h` = TINCAT2.DLL network CRC32** (table + algo). |
| `SacredCDKeyFix` | C++ | Deletes `HKCU\Software\Ascaron Entertainment\Sacred` to fix CD-key bug. |
| `SacredRAW`, `SacredTestRes` | C++ | Hex/section raw viewer; res tester. Low value. |
| `CxImage/`, `hexeditctrl/` | C++ | 3rd-party libs (image codecs, hex control). **Ignore.** |

### 2. SacredUtils-master — C# WPF (.NET 4.7.2, Material Design), by MairwunNx
A big game *configurator*. Reusable knowledge (not the UI):
- **Full `Settings.cfg` key list** (game config is line-based ASCII `KEY : value`).
- Component-DLL manifest (`cfg/ch.files.txt`) confirms core game DLLs:
  `ijl15.dll, tincat2.dll, protect.dll, mss32.dll, unicows.dll, libxml2.dll, …`
  — directly relevant to our **ijl15.dll proxy** injection vector.
- `global.res ↔ global.csv` via bundled `Sacredres2Csv.exe`
  (`-csv -oglobal.csv global.res` / `-res -oglobal.res global.csv`).
- Launches sibling editors by path: `CreatureEditor.exe`, `WeaponEditor.exe`,
  `SHR.exe` (Hero Resetter), `she.exe` (Hero Editor).
- Reads `Settings.cfg` as ASCII lines, value = substring after `"KEY : "`, bool = `"1"`.

### 3. SacredUnderworldHelper-master — C# WinForms, by (GitHub) author
Game launcher + fullscreen-emulation + **"pot bot"** (auto-drink potion). Reusable:
- **Live-memory HP pointer chain** (Sacred Underworld `sacred.exe`):
  - max HP: `base + 0x6D3BC0 → +0x4 → +0x4 → +0x4D4` (i32)
  - cur HP: `base + 0x6D3BC0 → +0x4 → +0x4 → +0x4D8` (i32)
- Multilevel-pointer read helper (`WinTweak.ReadPointer`), `ReadProcessMemory`.
- Fullscreen emulation = hide `Shell_TrayWnd` + `ChangeDisplaySettings` + black form.
- Detects install: `Sacred.exe` present in dir.

### 4. SacredLumina-master — JS / Vue2 + Electron, by MairwunNx
**Hero start-spawn-position editor** for Sacred Underworld 2.28 (Steam). Reusable:
- **Per-class `startcode.bin` spawn X/Y byte offsets** (table below) + the
  **internal NPC type folder names** (`TYPE_NPC_*`).
- `ByteUtils.js` = read/write little-endian int (mostly written as 16-bit) at a
  raw byte offset, preserving the rest of the file.

---

## Consolidated file-format & engine-fact table

### PAX/PTX hero-save container (authoritative, from `shlib/Hero.cpp` + `Hero.h`)
Confirms & extends our `docs/community-refs.md §C`. Concrete constants:

```
Header (file start):
  u32 version       0x07484D41 ("AMH\x07")  = HERO_HEADER7   (classic)
                    0x1B484D41 ("AMH\x1B")  = HERO_HEADER27  (v27 / Underworld)
  u32 totalSections
  u8  fluffHeader[0xF8]        ← so section index begins at file offset 0x100
Section index entry (SECTION_DESC, 12 bytes, packed):
  u32 type
  u32 offset        (0 = empty/absent section)
  u32 sizeinflated  (decompressed size)
Per-section payload @ offset:
  - SPECIAL: if type==0xC4  → exactly 0x40 raw bytes, NO compression header.
  - else read u32 secDesc:
      if secDesc == 0xBAADC0DE (COMPRESSED_CODE):
          u32 compressedSize
          skip 0x18 bytes (zeroed nullBuffer)
          <compressedSize> bytes of zlib stream → uncompress() to sizeinflated
      else: section is uncompressed; reread from `offset`, length = sizeinflated.
Write order (CompileHero): header → fluff(0xF8) → placeholder index → section
  payloads → seek 0x100 → write real index.
```
Compression = **standard zlib** `uncompress()` / `compress2(level 9)` (see
`ZlibWrapper.cpp`). Marker `0xBAADC0DE`.

### Hero section types (from `SacredItemManager/Hero.cpp`)
| Type | Const | Notes |
|---|---|---|
| `0xC3` | `SECTION_HEROATAGLANCE` | name/level/portrait; difficulty-unlock @ `+0xEE` |
| `0xC4` | (appearance / survival) | **exactly 0x40 bytes, uncompressed**; survival-bonus @ `+0x08` |
| `0xC7` | `SECTION_HERO` | main stat block (offsets below) |
| `0xC8` | `SECTION_INVENTORY` | item array (layout below) |
| `0xCA` | `SECTION_PLACEMENT_BP` | backpack grid |
| `0xCB` | `SECTION_PLACEMENT_CH` | character/equip grid |

### `0xC7` SECTION_HERO stat-block offsets (Classic — **corroborates our doc exactly**)
From the HeroManager plugins (operate directly on the decompressed 0xC7 buffer):
| Field | Type | Offset | Source |
|---|---|---|---|
| experience | i32 | `0x399` | LevelAndExp plugin |
| attribute points | u8 | `0x3B0` | SkillsAndAttr plugin |
| free skill points | u16 | `0x3CF` | SkillsAndAttr plugin |
| gold | u32 | `0x3D3` | Gold plugin + SugarDaddy (`TARGET_SECTION=0xC7`, `GOLD_OFFSET=0x3D3`) |
| level | u16 | `0x3E3` | LevelAndExp plugin (**note:** our doc calls this u32; low byte is the level) |
| combat-arts count | u8 | `0x483` | SpellsAndArts plugin |
| combat-arts array | 22B/entry | `0x489` | SpellsAndArts plugin |

**Combat-art entry (22 bytes each, starting `0x489`):**
`u16 spellId` @ +0; `u8 level` @ +2; `u8 guts[19]` @ +3. So spell N at
`0x489 + 22*N`. Count byte at `0x483`. (Underworld build: add the `+0x48` shift
our doc records, e.g. count → `0x4CB`, array → `0x4D1`.)

### `0xC4` survival-bonus (from `SacredMSB`)
40-byte section; max survival bonus = `*(u32*)&sec[0x08] = 0x0030FFFF`.

### `0xC3` difficulty unlock (from `SacredLevelUnlock`)
`*(u32*)&sec[0xEE]` — `0x00000000`=Bronze/Silver, `0x00000001`=+Gold,
`0x00000100`=+Platinum, `0x00010000`=+Niobium, `0x01000000`=all.

### Inventory section `0xC8` (from `ItemManager.cpp` + `ItemManager.h`) — packed structs
```c
// First u32 in section = total item count, then that many records.
#define ITEM_TYPE      0xFEEDF00D   // feedfood magic = real item
#define NULL_ITEM      0xBAADF00D   // filler record, size = 5*u32 = 20 bytes
#define DEFAULT_ITEM_PAYLOAD_SIZE        0x82   // normal item body
#define DEFAULT_ITEM_PAYLOAD_SIZE_LARGE  0x379  // itemtype 257 (and big type-385)
#define TYPE_385_SIZE_INCLUDED 33554432         // (=0x02000000) type-385 size-prefixed

struct ST_ITEM_HEADER {        // packed; precedes each real item
  u32 itemdescid;              // 0 = uncounted/skip(4 bytes); 0xBAADF00D = filler
  u32 feedfood;               // must == 0xFEEDF00D else parse error
  u16 itemtype;               // 257=large, 385=variable, 386=standard default
  u32 sixtyfour;              // written as 64
  u32 itemid;                 // unique instance id (links to placement grid `index`)
  u32 itemdescid2;            // == itemdescid
  u8  body[0xE9];
  u8  slotcount;              // number of socketed sub-items
};
// After header: (slotcount+1) payloads, each DEFAULT_ITEM_PAYLOAD_SIZE bytes
//   (slotcount sub-items first, then the main item last), then a 3×u32 tail.
//   itemtype 257 → payload 0x379; type 385 with descid==0x02000000 →
//   max(*(u32*)&payload[12], 0x379).
```
Placement grid (`0xCA`/`0xCB`), from `ST_PLACEMENT` / `ST_PLACEMENT_ITEM`:
```c
struct ST_PLACEMENT {          // 12-byte header + 1024 fluff, then grid
  u16 unknown1; u32 unknown2; u16 unknown3;
  u16 width; u16 height;       // grid dims
  u8  fluff[1024];
  /* then grid of ST_PLACEMENT_ITEM[width*height], row-major (y*width+x), then tail */
};
struct ST_PLACEMENT_ITEM {     // 12 bytes
  u32 index;                   // == item's itemid; 0 = empty cell
  u8  slotused;
  u8  width, height;           // set only in the item's top-left cell
  u8  unknown;
  u32 itemid;                  // descid (icon res lookup)
};
```
`.SIT` single-item export (`SITFile.h`): header `"AAH\001"` (4 bytes), zlib stream
`0xDA78`, then name/desc/author short-strings + CxImage picture + serialized CItem.

### TINCAT2 network CRC32 (from `SacredFilter/SacredCRC32.h`)
Used on Sacred's TCP packets. Standard CRC32 lookup table (poly 0xEDB88320,
table[1]=0x77073096) **BUT non-standard framing**:
```
crc = 0;                       // init = 0 (NOT 0xFFFFFFFF)
for each byte b:
    crc = (crc >> 8 & 0x00FFFFFF) ^ table[(crc ^ b) & 0xFF];   // SAR 8, AND 0x00FFFFFF
return crc;                    // NO final XOR / inversion
```
Table extracted from TINCAT2.DLL v2.0.24.0 @ file offset `0x0004986C`.

### Internal NPC type folders & per-class spawn offsets (from `SacredLumina`)
`startcode.bin` lives per type under `<gameDir>\<TYPE>\startcode.bin`. X/Y spawn
coords are little-endian ints (effectively u16) at these byte offsets:
| Class | Folder | X off | Y off |
|---|---|---|---|
| Gladiator | `TYPE_NPC_GLADIATOR` | 551846 | 551851 |
| Seraphim | `TYPE_NPC_SERAPHIM` | 550551 | 550556 |
| Magician/Mage | `TYPE_NPC_MAGICIAN` | 550551 | 550556 |
| Wood Elf | `TYPE_NPC_ELVE` | 550551 | 550556 |
| Dark Elf | `TYPE_NPC_DARKELVE` | 550551 | 550556 |
| Vampiress | `TYPE_NPC_VAMPIRELADY` | 550551 | 550556 |
| Dwarf | `TYPE_NPC_ZWERG` | 550551 | 550556 |
| Daemon | `TYPE_NPC_DAEMONIN` | 550551 | 550556 |
(Gladiator's offsets differ — its startcode.bin is laid out differently. Stride
X→Y = 5 bytes everywhere.) These German folder names match our existing
`refs_formats_data.md` creature-type naming.

### Settings.cfg keys (game config, ASCII `KEY : value`, from SacredUtils)
`ACCEPT_LICENSE, AUTOSAVE, AUTOTRACKENEMY, CHAT_ALPHA, CHAT_DELAY, CHAT_LINES,
COMBINE_SLOTS, COMPAT_VIDEO, DEFAULT_SKILLS, DETAILLEVEL, FIRST_LOGIN, FONT,
FONTAA, FONTFILTER, FORCE_BLACK_SHADOW, FSAA_FILTER, FULLSCREEN, GFX32,
GFXLOADING, GFXSTARTUP, GFX_LIMIT128, LANGUAGE, LOGGING, MINIMAP_ALPHA,
MUSICVOLUME, NETWORK_SPEEDSETTINGS, NIGHT_DARKNESS, PICKUPANIM, PICKUPAUTO,
SCREEN_QUAKE, SFXVOLUME, SHOWEXTRO, SHOWMOVIE, SHOWPOTIONS, SHOW_ENEMYINFO,
SHOW_HEROINFO, SOUND, SOUNDQUALITY, TASKBAR_ICONS, UNIQUE_COLOR, VIOLENCE,
VOICEVOLUME, WAITRETRACE, WARNING_LEVEL` (bool values are `"0"`/`"1"`).

### Spell-id → name table (from `Spells.h`, 236 entries)
Format `"<id>=<Name>"`, 1-indexed. Sample: 1=Phase Shift, 2=Stoneskin,
3=Companion of the woods, 5=Fireball, 7=Spiritual Healing, 8=Ball Lightning,
17=Rotating Blades of Light, 26=Meteor Storm, 46=Golem … (full list in the file).
`TOTAL_SPELLS 236`. Cross-check against our `sacred_modding - combat_arts.csv`.

---

## C++-port / refactor candidates

- **TODO(port): PAX/PTX save container reader+writer.** Port `shlib/Hero.cpp`
  (`DecompileHero`/`CompileHero` + `ZlibWrapper`) into a clean modern C++ module
  (`sacred::HeroSave`) with `load(path)` → `map<u32 type, vector<u8>>` and
  `save(path)`. Handles the 0x100 index, 0xBAADC0DE zlib framing, and the 0xC4
  40-byte special case. This is the cleanest existing implementation and unblocks
  the long-planned `sacred.read_save / write_save` SDK API in our backlog.
- **TODO(port): Inventory/item/placement codec.** Port `CItemManager`
  (`LoadItemInventory`/`CompileItemInventory`/`LoadPlacement`/`CompilePlacement`)
  as `sacred::Inventory` over the 0xC8/0xCA/0xCB sections, using the packed
  structs + magic constants above. Includes the `FindRoomForItem` 2D bin-packing
  (a genuinely reusable grid-fit algorithm) and `ImportItem`.
- **TODO(port): Hero stat accessors.** Tiny typed getters/setters over the 0xC7
  buffer using the offset table (gold 0x3D3, exp 0x399, level 0x3E3, skill pts
  0x3CF, attr pts 0x3B0, CA count 0x483 / array 0x489×22B). Trivial once the
  container is ported; expose to Lua.
- **TODO(port): TINCAT2 CRC32.** ~10-line function (init 0, no final XOR) — port
  if/when the SDK ever speaks Sacred's LAN protocol. Table already in our tree.
- **TODO(port): Multilevel-pointer memory reader.** `WinTweak.ReadPointer` pattern
  → reuse for any live-memory probe; pair with the HP chain
  `base+0x6D3BC0→+4→+4→+0x4D4/0x4D8` (UW `sacred.exe`) for a runtime HP read.
  **Caveat:** this offset is for the standalone UW `sacred.exe`, not necessarily
  the Steam Sacred Gold build we hook — verify before trusting.
- (Low priority) `global.res ↔ CSV` is already handled by the external
  `Sacredres2Csv.exe`; our `res2csv` notes cover it — no port needed.

---

## Overlaps / corrections vs our existing RE

- **CONFIRMS** `docs/community-refs.md §C` PAX format end-to-end: section table @
  `0x100`, zlib marker `0xBAADC0DE`, section types `0xC3/0xC4/0xC7/0xC8/0xCA/0xCB`,
  item magic `0xFEEDF00D` / filler `0xBAADF00D`, and the Classic 0xC7 offsets
  (gold 0x3D3, exp 0x399, skill-pts 0x3CF, attr-pts 0x3B0, CA count 0x483 /
  array 0x489). Our doc was correct.
- **CLARIFIES `0xC4`:** our doc tags it "appearance / AAGlance, 40 bytes" — the
  MSB tool shows it *also* holds the survival bonus (`u32 @ +0x08`), and confirms
  the **exact 0x40-byte uncompressed** special-case in the container parser.
- **MINOR (level width):** our doc lists `level u32 @0x3E3`; the LevelAndExp plugin
  treats it as **u16 @0x3E3**. Both work (high bytes ~0); note it's effectively u16.
- **NEW vs our docs:** the explicit `0xC3 +0xEE` difficulty-unlock dword, the
  22-byte combat-art entry breakdown (`u16 id / u8 level / u8 guts[19]`), the
  `.SIT` export header `"AAH\001"`, the full `Settings.cfg` key list, the
  per-class `startcode.bin` spawn offsets + `TYPE_NPC_*` folder map, and the
  TINCAT2 CRC32 framing (init 0 / no final XOR).
- **MAGIC RECONCILE:** header version `0x07484D41`/`0x1B484D41` = ASCII `AMH\x07`
  / `AMH\x1B` — our docs referenced an "HMAM/AMH" magic; this is the precise value.
