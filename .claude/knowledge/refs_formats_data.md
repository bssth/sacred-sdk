# Sacred Gold — Reusable Data & File-Format Reference (from E:/Downloads/refs)

Source: community modding CSVs + format-editor source archives (VB6, Delphi). Game EXE base
`0x00400000`, no ASLR (file offset = VA − 0x400000). This doc captures DIRECTLY-USABLE data
(ID tables) and the on-disk format structs the SDK should learn to read/write.

Concrete C++-port / refactor candidates are tagged `TODO(port):` — there are **7** of them
(P1..P7), summarized at the bottom.

---

## Part A — CSV data (convert to Lua/C++ tables NOW)

All four CSVs live in `E:/Downloads/refs/sacred_modding - *.csv`. They are clean, human-curated
ID→name maps already validated against the live game by the modding community. These are the
fastest win: no parsing of game binaries needed, just transcribe.

### A1. `sacred_modding - characters.csv` — Creature / NPC type IDs
- **Columns:** `ID Dec, ID Hex, Character, Note`  (Note = variant/flavor: "Human", "Vampire",
  "Cut/unused", "Forces model", "Will attack you", etc.)
- **Rows:** 713 data rows (header + 713). IDs run 1..713 but are **sparse** (gaps, e.g. 010..031,
  110..111, 138..139 missing).
- **ID encoding is the key insight:** `ID Hex` is the **little-endian byte pair** of the decimal ID.
  - Dec 1 → Hex `0100`, Dec 2 → `0200`, Dec 9 → `0900`, Dec 32 → `2000`, Dec 256 → `0001`,
    Dec 512 → `0002`. So `hex = bswap16(dec)`. i.e. low byte first.
  - Decode: `dec = (hexbyte0) | (hexbyte1 << 8)` reading the 4-hex-digit string as two bytes
    `b0 b1` → `dec = b0*256 + b1`? **No** — verified: `0100`→1 means b0=0x01,b1=0x00 →
    `dec = b0 + b1*256`. Treat the 16-bit value as **little-endian**: the stored hex string is the
    raw LE u16 as it appears in the file; decimal = that u16 with bytes swapped from display.
    Practically: `dec = (str[0..1] hex) + (str[2..3] hex)*256`. Confirmed across 1/2/9/32/256/512.
  - This matches the in-file Creature.pak `ID As Long` field (see Part B Creature struct).
- **Ranges / semantic bands** (useful for classifying spawns):
  - `001..009` (hex `0100..0900`) = the 8 playable hero classes + Daemon (Seraphim, Gladiator,
    Battle Mage, Dark Elf, Wood Elf, Vampiress[human], Vampiress[vampire], Dwarf, Daemon).
  - `032..` = monsters; `256..368` (hex `..01`) = humanoid NPCs/soldiers/mages;
    `512..601` (hex `..02`) = animals/mounts; `640..714` = townsfolk/citizens/named NPCs.
- **Maps to our data as:** the canonical monster/NPC **type registry**. This is the same numbering
  space the SDK already touches via `cCreature` (the talk-signal work used `cCreature+0x200`).
  - `TODO(port): P1` — emit `sdk/lua/data/creature_types.lua` (or a generated C++
    `static const struct { uint16_t id; const char* name; const char* variant; } kCreatureTypes[]`)
    from this CSV. Use for: spawn debugging, NPC labeling in diagnostics overlays, companion roster
    panel (pending polish item), and naming the "player talked to NPC" target.

### A2. `sacred_modding - combat_arts.csv` — Combat Art / Spell IDs
- **Columns:** `ID, Character, Name, Note`. `Character` is the owning class (or `?` for
  unattributed engine spells, or a monster like "Sakkara Demon", "Goblin Shaman").
- **Rows:** 156 data rows.
- **ID encoding (two distinct families):**
  - **Spell family:** small IDs that are exact multiples of `65536` (0x10000). e.g.
    Phase Shift=65536 (1<<16), Stoneskin=131072 (2<<16), Fireball=327680 (5<<16). So
    `spell_index = ID / 65536`, i.e. the meaningful value lives in the **high 16 bits**; low 16 = 0.
    These line up with the `speditor.ini [SpeEff]/[Battle Mage]` low codes (Fireball `$0005`,
    Phase Shift `$0001`, Stoneskin `$0002`) — i.e. `ID = code << 16`.
  - **Skill/attack family:** large IDs like `65732609` (Stomping Jump), `66191361` (Back-breaker),
    `68288513` (Cobra). These are `0x0400_xxxx`-range packed codes; the speditor.ini hex (e.g.
    Cobra `$0412`, Back-breaker `$03f2`) is the **low 16 bits**, with `0x04xx_0000`-style high bits
    encoding the class/slot. Cross-check: ini gives the per-class CA opcode table; CSV gives the
    fully-packed 32-bit ID as it appears in save/balance data.
- **Maps to our data as:** the **combat-art / spell registry**, keyed by packed 32-bit ID.
  - `TODO(port): P2` — emit `combat_arts.lua` / `kCombatArts[]` with fields
    `{ uint32_t id; uint8_t class_id; const char* name; }`. Cross-reference with
    `speditor.ini` opcode tables (Part C) to recover the `code<<16` / low-16 split. Use for: buff
    detection, CA-cast logging, the dialog/skill diagnostics.

### A3. `sacred_modding - companions.csv` — per-class summon/companion model IDs
- **Columns:** `Character, Model, Name 1, Name 2, Name 3`
- **Rows:** 4 (Battle Mage, Dark Elf, Wood Elf, Vampiress).
- **Values are 32-bit hex resource handles** into global.res / model tables, e.g.
  Wood Elf Model=`0019FA3D`, Name1=`0019FA36`, Name2=`0019F8D7`. Vampiress has a 3rd name
  (`0019F418`). These are `global.res` string-resource IDs (the `0019xxxx` band).
- **Directly relevant** to the *pending companion roster panel* polish item: these are the model +
  display-name resource IDs for each class's summonable companion.
  - `TODO(port): P3` — emit `companions.lua` mapping `class -> {model_res, name_res[]}`. Resolve
    `name_res` through global.res (see res2csv format, Part C) to get localized display strings.

### A4. `sacred_modding - names.csv` — quest/NPC/item string-resource IDs
- **Columns:** `ID, Name, Note` (Note flags `Unused`, `Forces model`, etc.)
- **Rows:** ~625 data rows. IDs are **decimal global.res string IDs**, two clusters:
  `17000..17837` (Ancaria campaign NPCs, quest items, messages) and `19500..19600`
  (Underworld content). These are *not* creature type IDs — they are **text/string resource IDs**
  (same namespace res2csv dumps).
- Contains literal UI strings too (e.g. ID 17678 is a multi-line `<colorw=...>` formatted
  difficulty-unlock message; 17642 "Quest Completed"), confirming this is the **global.res text
  table**, not a creature table.
- **Maps to our data as:** a partial **string-table dump** we can use without parsing global.res.
  - `TODO(port): P4` — emit `string_ids.lua` (id→english). Lets the SDK show human-readable quest/
    NPC/item names in diagnostics without a live global.res lookup. Note the inline markup tokens
    `<colorw=RRGGBBAA>`, `<n>` (newline) — the renderer/text path understands these.

---

## Part B — On-disk format structs (RECOVERED FROM EDITOR SOURCE — high confidence)

These come from the VB6 / Delphi editor sources, which read/write the real game files. Field
layouts are authoritative (they round-trip the actual `.pak` files).

### B1. Generic resource PAK (SND.pak / TEX.pak / MDL.pak) — `PakExtractor` (Delphi, `SEMain.pas`)
Cleanest spec in the whole ref set. Layout:
```
offset 0x00  char[3]   magic  = "SND" | "TEX" | "MDL"   (ASCII, no NUL)
offset 0x03  byte      (pad/version, skipped — code seeks to 0x04)
offset 0x04  uint32    entryCount
offset 0x08 .. 0xFF    (reserved/unused header padding)
offset 0x100           entry table begins (FIXED at 0x100 / 256)
  each entry = 12 bytes, little-endian:
    uint32 type     (resource subtype; for SND: 0x20=wav, 0x21=mp3)
    uint32 offset   (absolute file offset of payload)
    uint32 length   (payload byte length)
  entries with type==0 && offset==0 && length==0 are skipped (holes)
payload blobs live at their `offset`, raw, uncompressed.
```
- Extractor names output files `<index padded>.<ext>` where ext from type (`.20`→`.wav`,
  `.21`→`.mp3`, else `.<hex type>`). Index width = digits of entryCount.
- **No compression** — payloads are stored verbatim. Reading is trivial.
- `TODO(port): P5` — port this 12-byte-entry PAK reader to C++ (`PakArchive::open/iterate/extract`).
  ~40 lines. Enables SDK to enumerate/extract sounds, textures, models at runtime. Magic-dispatch
  on the 3-char tag. **This is the most reusable format in the set.**

### B2. `Creature.pak` (Balance creature table) — `Creature.pak Editor` (VB6)
Different layout from the resource PAKs above (this is a flat record array, not an offset table):
```
offset 0  char[3]  Signatur = "CIF"      (NOT "SCR"; verified in MainForm.frm line 386)
offset 3  ???      Version  (== 0 required)         [VB reads Signatur then Version then Cnt+Unknown]
          uint32   CreaturesCnt
          byte[4]  Unknown
offset 0x101 (257) record array begins   (Seek FNr, 257  → 1-based VB seek = byte offset 256)
  record size = 86 bytes each. Layout (VB `Type SacredCreature`):
    Long   ID            (4)   ← matches characters.csv decimal IDs
    Integer Class        (2)   ← see Class enum below
    Byte   Flags         (1)   ← FlFly=1 FlBig=2 FlNoShadow=16 FlGhost=32 FlBanane=64 FlKurve=128
    Byte   Unknown1[1]   (1)
    Integer ExpA, ExpB   (4)   ← experience reward
    Byte   BaseStrength, BaseEndurance, BaseDexterity, BasePhysReg, BaseMagReg, BaseCharisma (6)
    Byte   Unknown2[2]   (2)
    Byte   Skills[18]    (18)  ← skill ids, see Skill enum (1..33)
    Integer BaseWalkSpeed, BaseRunSpeed (4)
    {Byte BonusLevel; Byte BonusType}[6]  Boni (12)
    Byte   BoniValue[6]  (6)
    Byte   Unknown3[26]  (26)
  → total 4+2+1+1+4+6+2+18+4+12+6+26 = 86 ✓
trailing: ReservedSpace = FileLen - (Cnt*86 + 256)  bytes of reserved padding (preserved on save).
```
- **Class enum** (`ClXxx`): 1 Hero, 2 Monster, 3 NPC, 4 Horse, 5 Undead, 6 Animal, 7 Mercenary,
  8 Goblin, 9 Demon, 10 Dragon, 11 Energy, 12 Elve, 13 Enemy, 14 Human, 15 Dryade.
- `TODO(port): P6` — port a `Creature.pak` reader/writer. Record layout is fully known; the SDK
  could read base stats/skills/exp for any creature ID at runtime (useful for balance mods and the
  diagnostics overlay). 86-byte fixed records make this trivial.

### B3. `Weapon.pak` (item/weapon table) — `Weapon.pak Editor` (VB6, `MainModule.bas`)
```
VB `Type SacredItem`:
    Byte   Dump[258]                      (leading blob — header/name area)
    String Name
    Long   ID
    Long   ModellID                       ← resource handle (cf. companions.csv Model field)
    Byte   Flags1, Flags2
    Byte   ItemTyp                         ← see ItemTyp enum below
    Byte   SkillID, SkillValue
    Byte   Sockel[8]                       ← socket slots
    Byte   RequireMinLevel/Strength/Dexterity/Charisma/Endurance, ItemLevel
    Integer PhysDmgMin/FireDmgMin/MagicDmgMin/PoisenDmgMin
    Integer PhysDmgMax/FireDmgMax/MagicDmgMax/PoisenDmgMax
    Integer Offensive, Defense
    Integer PhysRes, FireRes, MagicRes, PoisenRes
    {Byte AganstMod; Byte ForMod}[8]  BonusMod
    Long   BonusID[8]                      ← packed bonus IDs (see bonus map below)
    Integer BonusValue[8]
```
- **ItemTyp enum** (subset): 0 bad,1 Dolch(dagger),3 Schwert,4 Zweihandschwert,5 Axt,6 Zweihandaxt,
  7 Schild,8 Bogen,9 Armbrust,10 Faustwaffe,13 Rüstung,14 Ring,15 Amulett,16 Helm,17 Armschutz,
  18 Beinschutz,19 Gürtel,20 Schulterpanzer,21/23 Stangenwaffe,22 Wuchtwaffe,24 Stab,25 Zaumzeug,
  26 Schuhe,27 Handschuhe,28 Flügel,29 Sonderitem,30 Pistole,32 Kanone.
- **CharFlags** (item class restriction bitmask): Seraphim=1,Gladiator=2,Kampfmagier=4,Dunkelelf=8,
  Waldelfe=16,Vampirin=32,Zwerg=64,Dämonin=128.
- **Sockel/socket types:** Bronze=1,Silber=2,Gold=3,Grün=4.
- **Bonus ID map (HUGE, reusable):** `MainModule.bas: InitBonusNames()` is a ~520-entry
  `BonusName(id)=string` table mapping packed bonus IDs → German effect names. Bands:
  `1..73` spells, `601..627` skills, `801..867` stat/damage/utility mods, `1000..1097` combat-art
  bonuses, and scaled-stat bands `66337.., 131873.., 197409.., 262945.., 328481.., 394017..`
  (these high IDs = `(baseStat<<16) | modCode`, matching the combat_arts.csv `<<16` packing).
- `TODO(port): P7` — port the bonus-ID → name table (translate German→English using the speditor.ini
  /global.res strings). This is the **single most valuable lookup table** for item tooltips, drop
  logging, and any item-mod SDK feature. Also port the SacredItem record reader if item-table
  editing is ever needed.

### B4. `balance.bin` region offsets — `Balance.bin_Editor` (VB6, `SacredModule.bas`)
Hard-coded byte offsets into `balance.bin` for per-region spawn counts (UW = Underworld):
```
SOUTHKERN  cnt1=0x5779 cnt2=0x5781    NORTHKERN cnt1=0x57B9 cnt2=0x57C1
SUMPF      cnt1=0x57F9 cnt2=0x5801    WUSTE     cnt1=0x5839 cnt2=0x5841
NORD       cnt1=0x5879 cnt2=0x5881    LAVA      cnt1=0x58B9 cnt2=0x58C1
SHADDARNUR cnt1=0x58F9 cnt2=0x5901    UW_UPPER  cnt1=0x5939 cnt2=0x5941
UW_LOWER   cnt1=0x5979 cnt2=0x5981
```
- Regions are spaced 0x40 apart; cnt1/cnt2 are 8 bytes apart within each region block. Low-priority
  (only 9 regions) — record the offsets but no port needed yet.

---

## Part C — Tools/formats catalog (the editor archives)

| Archive | Type | Source incl.? | Format it edits | Reusable knowledge |
|---|---|---|---|---|
| `PakExtractor.rar` | Delphi exe + `.pas/.dpr/.dfm` | **YES** | SND/TEX/MDL `.pak` | **B1** — full PAK spec (best). |
| `Creature.pak_Editor.zip` | VB6 (.frm/.bas/.vbp) v0.1→0.3 | **YES** | `Creature.pak` | **B2** — "CIF" hdr, 86-byte records, Class/Skill/Flag enums. |
| `Weapon.pak_Editor.zip` | VB6 v0.1→0.2.3 | **YES** | `Weapon.pak` | **B3** — SacredItem struct + ~520-entry bonus map + ItemTyp/CharFlag enums. |
| `Balance.bin_Editor.zip` | VB6 v0.1 | **YES** | `balance.bin` | **B4** — region spawn-count offsets. |
| `res2csv.zip` | C/C++ exe + `res2csv.txt` | readme only | `global.res` ⇄ CSV | **C1** below — global.res text-table format. |
| `Sacred PaxFile Editor.7z` | Delphi exe + `speditor.ini` | ini only (no src) | save / PaxFile | **C2** — save-file offsets + CA opcode tables. |
| `Text_Editor_Sacred.zip` | exe + `.lng` (no src) | strings only | `global.res` text | **C3** — confirms global.res item/text editing; `<colorw>`,`<n>` markup. |
| `GlobalResViewer.zip` | exe only (36 KB) | **NO** | `global.res` | viewer only; no extractable spec. Catalog name; implies global.res is browsable as id→string. |

### C1. `global.res` text table — from `res2csv.txt` (readme is Russian, KOI8-R)
- `Res2Csv` round-trips `global.res` ⇄ CSV. Supports 3 languages in one row.
- **CSV row format (decoded from the readme):** `constant_ID  ger_str  eng_str  rus_str`
  (i.e. each string resource has a numeric constant ID + per-language text). This is exactly the
  namespace `names.csv` (A4) and `companions.csv` name fields (A3) point into.
- CLI: `Res2Csv -csv [-g<ger.res>] [-e<eng.res>] [-o<out.csv>] <res>` and the inverse `-res`.
- **Takeaway:** global.res is an **ID → {ger,eng,rus} string table**. The SDK already needs string
  lookups (dialog-text Path A roadmap). Knowing the CSV intermediate means we can pre-dump all
  strings offline and ship a Lua table instead of parsing global.res live — OR port a reader.

### C2. `speditor.ini` (Sacred PaxFile / save editor) — offsets + CA opcode tables
- **Save/PaxFile field offsets** (per the ini, for game v1.5 & 1.66):
  `NAME offset=0x134`, `CA (combat arts) offset=0x643`, `EXP offset=0x559`.
- **`[SpeEff]` special-effect codes** — 32-bit packed as `a1 a2 b1 b2`:
  `b1b2` = effect type, `a2` = attribute-source modifier
  (`00`=basic, `01`=+%str, `02`=+%dex, `03`=+%end, `04`=+%physreg, `05`=+%menreg, `06`=+%cha).
  e.g. `Attack += 0x00000329`; `Str.→attack = 0x00010329`; `Dex.→attack = 0x00020329`. This is the
  **same `(modifier<<16)|code` packing** seen in combat_arts.csv and the Weapon bonus map — one
  consistent scheme across the engine.
- **Per-class CA opcode tables** in the ini ([Seraphim],[Gladiator],[Battle Mage],[Dark Elf],
  [Wood Elf],[Vampiress]) give the **low-16 opcodes** (e.g. Fireball `$0005`, Cobra `$0412`,
  Turn into Vampire `$03fe`). Pair with combat_arts.csv (full 32-bit IDs) to fully decode CA IDs.

### C3. `Text_Editor_Sacred` (.lng) — confirms global.res editing + markup
- DracoBlue's editor; loads/saves `global.res`, exports results to CSV (`;`-separated).
- Confirms global.res holds item text + general UI strings, and the in-string markup tokens
  (`<colorw=...>`, `<n>`, "color next word/line/text", "add break") — relevant to rendering native
  dialog text (the Path A roadmap).

---

## Part D — Which formats the SDK should learn to read/write

Priority order for the C++ SDK:
1. **Resource PAK (SND/TEX/MDL)** — `B1`/`P5`. Tiny, no compression, immediately useful for asset
   enumeration/extraction at runtime. Highest ROI.
2. **global.res text table** — `C1`/`P4`. Either ship an offline CSV-dumped Lua table (fastest) or
   port a reader. Needed for human-readable names + native dialog text (Path A).
3. **Bonus-ID → name table** — `B3`/`P7`. Pure data; port the `InitBonusNames()` map (translate to
   EN). Powers item tooltips / drop logging.
4. **Creature.pak** — `B2`/`P6`. 86-byte records, "CIF" magic. Read base stats/skills/exp by ID.
5. **Weapon.pak SacredItem** + **balance.bin region offsets** — `B3`/`B4`. Lower priority; only if
   item/balance editing becomes a feature.

Data to transcribe to Lua/C++ tables right now (no binary parsing needed): all four CSVs →
`creature_types`, `combat_arts`, `companions`, `string_ids` (P1–P4).

---

## Notes / unknowns
- Creature.pak magic is **"CIF"** (not "SCR"); resource PAKs use **"SND"/"TEX"/"MDL"**. Two distinct
  PAK families with different layouts (offset-table vs flat-record) — don't conflate them.
- `ID Hex` in characters.csv is the LE u16 byte-pair of the decimal ID; the in-file `ID As Long`
  field uses the decimal value.
- The `(modifier << 16) | opcode` packing is engine-wide: same in combat_arts.csv, Weapon bonus IDs,
  and speditor.ini `[SpeEff]`. Decoding one decodes all.
- Editor sources are German/Russian; enum names kept verbatim (Skill*/ItemTyp*/Bonus*) for fidelity.
- Extracted sources staged at `E:/Downloads/refs/_extract/` (can be deleted; this doc captures the
  load-bearing details).
