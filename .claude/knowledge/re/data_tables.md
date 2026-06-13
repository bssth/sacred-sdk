# Reusable modding data tables

Extracted, verified data tables for the Sacred Gold SDK. Each lives as a
well-formed Lua module under `custom/lua/lib/data/` and `return`s a table.
This doc records, per table: source file, schema, **verified** row count, id
encoding, and how the SDK consumes it (Lua now; a future C++ `constexpr`
port is tagged `TODO(port):` with its target header).

All row counts below were verified by re-parsing the source and counting the
emitted Lua keys (not estimated).

---

## 1. `creatures.lua` — creature / character type table

- **Source:** `refs/sacred_modding - characters.csv`
- **Rows:** **474** (verified: 474 `[id] = {...}` entries)
- **Schema (CSV):** `ID Dec, ID Hex, Character, Note`
- **Lua shape:** `M.types[id] = { name=, band=, hex=, note? }`, plus
  `M.get(id)`, `M.by_hex(hex)`, `M.byteswap16(v)`.

### Id encoding (IMPORTANT)
The **key** is `ID Dec`, the real creature type id the engine compares
against. The CSV `ID Hex` column is the **byte-swapped 16-bit** form:

```
id_hex = byteswap16(id) = ((id & 0xFF) << 8) | (id >> 8)
```

Examples: Seraphim `1 -> 0x0100`, Skeleton `32 -> 0x2000`,
Shaddar'Rim `256 -> 0x0001`. **Verified for all 474 rows** (0 mismatches).
`hex=` is stored alongside each entry, and `by_hex()` builds a reverse index.

### Bands (derived from id range)
| band           | id range | contents |
|----------------|----------|----------|
| `hero`         | 1..9     | playable classes (Vampiress = 6 human / 7 vampire) |
| `monster`      | 32..255  | hostile creatures & named bosses |
| `humanoid_npc` | 256..368 | humanoid NPCs (soldiers, mages, thieves, named) |
| `animal`       | 512..601 | animals & mounts (wolves, horses, cows, bats) |
| `townsfolk`    | 640..    | farmers, citizens, nobles, novices, quest NPCs |

(The prompt's band hints — 1..9 heroes, 32.. monsters, 256.. humanoid NPCs,
512.. animals/mounts, 640.. townsfolk — match the CSV id ranges exactly.)

- **TODO(port):** `engine/data/creatures.h` — emit as
  `constexpr CreatureType kCreatureTypes[]` keyed by id; `byteswap16` becomes
  a `constexpr` helper; band as an `enum class Band`.

---

## 2. `combat_arts.lua` — combat-art / spell id table

- **Source:** `refs/sacred_modding - combat_arts.csv`
- **Rows:** **156** (verified)
- **Schema (CSV):** `ID, Character, Name, Note`
- **Lua shape:** `M.arts[packed] = { name=, class=, packed=, hi=, lo=, note? }`,
  plus `M.get(id)`, `M.split(id) -> hi, lo`.

### Id encoding
The **key** is the packed 32-bit `ID` (decimal in the CSV). It packs two
16-bit words:

```
hi = (id >> 16) & 0xFFFF   -- combat-art opcode / spell id
lo =  id        & 0xFFFF   -- modifier / variant selector
```

Examples: `65536 = 0x0001_0000` (Phase Shift, hi=0x0001 lo=0);
`65732609 = 0x03EB_0001` (Stomping Jump, hi=0x03EB lo=0x0001). The `hi`
word matches `ID1` in `hero_tables.lua > CombatArts`. The prompt's
"`packed = (modifier<<16)|opcode`" describes the same 32-bit word; here it is
stored verbatim as the key, with `hi`/`lo` pre-split. `class` echoes the CSV
character ("?" = unknown/generic, e.g. monster spells).

- **TODO(port):** `engine/data/combat_arts.h` — `constexpr CombatArt
  kCombatArts[]` keyed by packed id; `split()` is `{id>>16, id&0xFFFF}`.

---

## 3. `companions.lua` — per-class companion resources

- **Source:** `refs/sacred_modding - companions.csv`
- **Rows:** **4** (verified: Battle Mage, Dark Elf, Wood Elf, Vampiress)
- **Schema (CSV):** `Character, Model, Name 1, Name 2, Name 3`
- **Lua shape:** `M.by_class[class_name] = { model_res=, name_res={...} }`,
  plus `M.get(class_name)`.

### Id encoding
All values are **`global.res` handles** in the `0x0019xxxx` / `0x001Axxxx`
band (companion model + name-string resources). They are **not** creature
type ids — resolve them through `global.res`. Battle Mage / Dark Elf /
Wood Elf list 2 name handles; Vampiress lists 3.

This module unblocks the **companion roster panel** (see session-state memo).

- **TODO(port):** `engine/data/companions.h` — `constexpr Companion
  kCompanions[]` keyed by class id (reuse `creatures` hero ids 1..9), with
  `res_handle_t model_res` and a fixed-size `name_res[]`.

---

## 4. `hero_tables.lua` — hero progression / definition tables

- **Source:** community *Sacred Character Modifier* v0.15.0.16 (public),
  by LinkZ (GPLv2), under
  `refs_extract/sacred_charmodif_src/sacred_charmodif_v0.15.0.16 (public)/`.
- **Lua shape:** one sub-table per dataset on `M`, plus helpers
  `M.xp_for_level(level)` and `M.survival_bonus_pct(v)`.

| field           | source file              | symbol                | rows (verified) | notes |
|-----------------|--------------------------|-----------------------|-----------------|-------|
| `ExpTable`      | `ExpTable.pas`           | `cExpTable`           | **206**         | cumulative XP per level, **1-based** (`ExpTable[1]=0`) |
| `Chars`         | `CharsTable.pas`         | `cStr_CharacterClass` | **10** (0..9)   | class id -> name; 0/7 = `-`/dash slots |
| `Skills`        | `SkillsTable.pas`        | `c_st_Skills` (EN col) | **34** (0..33)  | skill id -> English name; index 30 = unused addon ("") |
| `ClassSkills`   | `SkillsTable.pas`        | `c_st_CCSkills`       | **17 x 9**      | `[row][class_id]` -> skill id (col 1..9) |
| `CombatArts`    | `tableCombatArts.pas`    | `table_CombatArts`    | **123**         | `{class,num,catype,id1,id2,name}` |
| `SurvivalBonus` | `tableSurvivalBonus.pas` | `table_SurvivalBonus` | **79** (0..78)  | `{min,max,p}`; p = bonus % for death-count range |

### Notes on encoding / gotchas
- **ExpTable count:** `ExpTable.pas` *declares* `array[1..206]` and indeed
  initialises exactly **206** values (last = `2117050200` at level 206). The
  source line span (18..223) is the 17-line header offset, not 223 entries.
- **CombatArts `catype`:** `1` = spell (RunenMagie), `2` = combat art
  (Kampftechnik). `id1` is the primary opcode (= `hi` word of a packed
  `combat_arts.lua` id); `id2` is the secondary opcode, `0xFFFF` = none.
- **Class ids** (used by `Chars`, `ClassSkills` cols, `CombatArts.class`):
  1 Seraphim, 2 Gladiator, 3 Battle Mage, 4 Dark Elf, 5 Wood Elf,
  6 Vampiress, 7 `-` (vampire form / dash), 8 Dwarf, 9 Demon. These match the
  save-file class byte and `creatures.lua` hero band ids (note: the separate
  `classes.lua` uses class *bitmasks* 1,2,4,...; these are the *index* ids).

- **TODO(port):** `engine/data/hero_tables.h` — `constexpr int32_t
  kExpTable[206]`, `constexpr SurvivalBonusRange kSurvivalBonus[79]`, a
  `constexpr CombatArtDef kCombatArts[123]`, and `kClassSkills[17][9]`. Names
  (`Chars`, `Skills`) become `constexpr const char* const[]`.

### Gaps / not sourced
- None of the five requested hero datasets are missing — all were present in
  the charmodif sources and are included.
- Skills/Chars are transcribed as the **English** column only. German/Russian
  columns exist in `SkillsTable.pas` (`c_st_Skills` cols 0/2) and could be
  added later as `Skills_DE` / `Skills_RU` if localized UI is needed.

---

## 5. `names.lua` — quest / NPC / item string resources

- **Source:** `refs/sacred_modding - names.csv`
- **Rows:** **823** (verified)
- **Schema (CSV):** `ID, Name, Note`
- **Lua shape:** `M.strings[id] = "name"` **or** `{ name=, note= }` when a note
  exists; plus `M.get(id)` which normalizes both forms to the name string.

### Decision: transcribed (not skipped)
`names.csv` is **structured string-resource data**, not procedural name
fragments. It maps engine string-resource ids to fixed English labels for
named NPCs, quest givers, quest items, and some quest UI/status messages.
Ids fall in two bands: **17000..** (main quest) and **19500..** (Underworld).
It is useful for naming spawned quest NPCs by id, so it is fully transcribed
rather than summarized. Notes seen include `Forces model`, `Unused`, and
colour/variant hints.

- **TODO(port):** likely **not** ported to C++ — these are localized strings
  that should resolve through the engine's existing `global.res` string
  system. Keep as Lua data (or move into a `.res` overlay) rather than a
  compiled table.

---

## Provenance / regeneration
- CSVs: `E:/Downloads/refs/sacred_modding - *.csv`.
- Hero tables: `HeroDump_Quellcode.zip` (not needed — no tables; it's a
  save-file dumper) and `sacred_charmodif_src_v0.15.0.16_public.rar`, both
  already extracted under `E:/refs_extract/`. The `.rar` extracted cleanly;
  no missing-tool blockers were hit.
- The five `.lua` modules were generated by transcribing these sources
  verbatim (no hand-typed values); row counts above were re-verified against
  the emitted Lua.
