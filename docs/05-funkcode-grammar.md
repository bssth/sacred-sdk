# 05 — FunkCode.bin grammar (synthesis)

Human synthesis on top of the auto-generated tag table in [04-funkcode-tags.md](04-funkcode-tags.md). Covers framing, payload grammar, type-tag dictionary, symbol inventory, and deterministic sub-record chains.

Evidence comes from `bin\TYPE_NPC_SERAPHIM\FunkCode.bin` (125 055 records, 3 962 707 bytes); cross-checks with the other 7 base classes noted where they apply.

---

## 1. Framing

```
File   := Record*
Record := tag:u8  size:u16BE  payload:byte[size-3]
```

The whole 4 MB file tiles cleanly into 125 055 records with zero leftover. No nesting at the framing level — the stream is flat.

Size width is 16-bit **big-endian**. Records under 256 bytes have a `0x00` high byte, which initially looked like a 3-byte size; it is not.

## 2. Payload grammar

```
payload   := flags:u8  field*
flags     := 0x00          (almost always; rare records carry other values)
field     := type:u8  value
value     := <encoding determined by `type`>
```

This rule fully consumes 117 681 of 117 681 non-marker payloads (100%); every byte is accounted for. No length-prefixed blocks at this level — the next `type:u8` determines how many bytes the value occupies.

## 3. Type-tag dictionary (confirmed)

| Type | Encoding | Count seen | Notes |
|---|---|---|---|
| `0x0b` | `u32 LE` | 81 523 | integer literal. Most common value type. |
| `0x01` | `cstr` (ASCIIZ) | 49 602 | symbol reference: variable / function name. Introduces named-record payloads. |
| `0x1c` | `u32 LE` | 9 350 | integer, distinct role (frequently a key/index in chain records). |
| `0x38` | `u32 LE` | 2 038 | integer, smaller values; e.g. `0x000005a0` (=1440) in `z` records. |
| `0x16` | `cstr` (ASCIIZ) | 1 871 | string **literal** (vs. `0x01` name reference). |
| `0x49` | `u32 LE` | 1 486 | integer; e.g. value 12. |
| `0x53` | `u32 LE` | 1 676 | integer (inferred — always 4-byte payload). |
| `0x4a` | `u32 LE` | 434 | integer; e.g. value 22. |
| `0x86` | `u32 LE` | (seen in `N`) | integer, sequence/counter role. |

Types still opaque (byte width not a clean u32, role unclear): `0x02` (33 786, mostly 35/36/31-byte payloads — likely length-prefixed strings or arrays), `0x36` (5536, large variable payload), `0x04`, `0x1e`, `0x7d`, `0x48`, `0x2a`, `0x28` (always 2 bytes), `0x7c`, `0x52`, `0x75`, `0x09`, `0x8f`, `0x31`, etc. Resolving them is the next step (probe specific record/type combinations).

## 4. Tag categories

Auto-derived from size variance; see [04-funkcode-tags.md](04-funkcode-tags.md) for the full table.

### 4.1 Markers (size = 4, payload = `00`)
8 tags, ~4000 records total — no payload other than the flags byte.

| Tag | Char | Count | Likely role |
|---|---|---|---|
| `0x3e` | `>` | 2 574 | close / jump-end |
| `0x2b` | `+` | 551 | open-bracket (start of triplet `+ * )`) |
| `0x2a` | `*` | 550 | middle-bracket |
| `0x6f` | `o` | 115 | small terminator |
| `0x59` | `Y` | 76 | small terminator |
| `0x58` | `X` | 72 | small terminator |
| `0x2c` | `,` | 53 | separator |

`+ → * → )` always appears as a strict 12-byte triplet (550 of each, perfect alignment) — a bracketed-expression sentinel.

### 4.2 Fixed-9 (one `(type,value)` field, 9 bytes total)

17 tags share `[tag:1][size=0x0009][flags:1=00][type:1][value:4]`.

| Tag | Char | Count | Sample first byte after flags |
|---|---|---|---|
| `0x4e` | `N` | 1 720 | `0x86` (u32 counter) |
| `0x14` | — | 1 365 | `0x0b` (u32 int) |
| `0x15` | — | 557 | (u32 int) |
| `0x39` | `9` | 80 | |
| `0x18` | — | 61 | |
| `0x79`–`0x8b` etc. | — | small | rare opcodes |

Single-operand opcodes / one-field nodes.

### 4.3 Fixed-14 / Fixed-19 (two or three `(type,value)` fields)

| Tag | Size | Count | Field shape |
|---|---|---|---|
| `0x7a` `z` | 14 | 1 870 | `1c key` + `38 cnt` |
| `0x21` `!` | 14 (×1166) / 19 (×704) | 1 870 | `1c key` + `0b int` (sometimes + `0b int`) |
| `0x76` `v` | 14 | 1 870 | `0b int` + `0b int` |
| `0x20` ` ` | 19 | 1 870 | `1c key` + `0b 1` + `0b 2` (very uniform) |
| `0x7b` `{` | 19 | 1 870 | `1c key` + `0b 1` + `0b 2` (identical to `0x20`) |
| `0x64` `d` | 19 | 11 498 | `0b 50` + `0b X` + `0b Y` |
| `0x67` `g` | 25 | 91 | |

### 4.4 Variable-size structural records

`0x73 's'` (22 762 — most frequent statement-like), `0x64 'd'` (data), `0x42 'B'` (block), `0x43 'C'` (constant decl), `0x69 'i'` (init), `0x3a ':'` (label), `0x3b ';'` (end), `0x3c '<'` (open), `0x68 'h'` (hook), `0x7d '}'` (sentinel-block), `0x1f` (template-name), `0x1a` (class-tag), … Approximately 60 of these. Their inner field-streams obey §2 and decode with the type-tag dictionary.

## 5. Deterministic sub-record chain (1870 daily-quest templates)

Bigram analysis surfaces a strict cycle of 6 record types, each occurring exactly 1870 times:

```
0x7a z  →  0x20 SP  →  0x7b {  →  0x21 !  →  0x76 v  →  0x1f
```

Every transition is 100% deterministic, except `21 ! → 76 v` which holds 1848/1870 (99%), and the chain exit `1f → (01 or 7a)`.

Decoding one slice (offset `0x08c7d5`):

| Tag | Bytes (payload) | Decoded |
|---|---|---|
| `7a z` | `00  1c 00 00 00 00  38 a0 05 00 00` | key=0, magic=1440 |
| `20  ` | `00  1c 00 00 00 00  0b 01 00 00 00  0b 02 00 00 00` | key=0, params=(1,2) |
| `7b {` | `00  1c 00 00 00 00  0b 01 00 00 00  0b 02 00 00 00` | key=0, params=(1,2) |
| `21 !` | `00  1c 00 00 00 00  0b 01 00 00 00` | key=0, weight=1 |
| `76 v` | `00  0b 01 00 00 00  0b 01 00 00 00` | (level=1, count=1) |
| `1f` | `00  1c 00 00 00 00  16 "DQ1_TOETE_NPC"\0` | key=0, template=`DQ1_TOETE_NPC` |

Daily-quest templates extracted (110 distinct, 1870 total). Most-common forms:

```
DQ<n>_BRINGE_NPC    (×23 each)   — bring something to NPC
DQ<n>_ESCORT_NPC    (×22 each)   — escort an NPC
DQ<n>_TOETE_NPC     (×?)         — kill an NPC
DQ<n>_BRINGE_ITEM   (×?)         — bring an item
```

`n` ranges 1..23 (DQ14 absent — a numbering hole). Multiplicities (22, 23) likely correspond to per-region replication since Sacred has ~23 region zones.

## 6. Named-symbol inventory

Records with `payload[1] == 0x01` carry a declared symbol name. Tags `C` (0x43) and `i` (0x69) account for 982 distinct identifiers. Naming language is German with mixed English; cases are PascalCase, snake_case, ALL_CAPS.

### 6.1 Top 15 by occurrence

| Count | Name | Apparent role |
|---|---|---|
| 616 | `DQ_NUM` | global daily-quest counter |
| 421 | `GNr` | `Gegner Nummer` — enemy number |
| 421 | `GTO` | enemy-related, paired with `GNr` |
| 352 | `DQ_Suchrichtung` | search direction (4 cardinal?) |
| 176 | `Tmp` | scratch variable |
| 155 | `GNr2` | second enemy number |
| 126 | `SNr` | another numeric slot |
| 119 | `MonsterType` | monster class id |
| 49 | `atmos_rgx` | atmosphere region-x |
| 27 | `welche_rgx` | "which region-x" |
| 26 | `Belohnung_Whiskey` | reward: whiskey (literal joke item) |
| 22 | `PoolEmpty` | quest-pool empty flag |
| 20 | `DQ_RG16_DUNGEONQUEST{1..4}` | region 16 dungeon quests |
| 17 | `DQ_RG{2,9,12,…}_DUNGEONQUEST{1..4}` | per-region dungeon quests |

### 6.2 Naming conventions observed

- `DQ_*` — daily quest globals
- `HQ_<chapter>_<section>_<step>_<class>_NPC_<role>` — main-quest hooks (e.g. `HQ_3_1_4_glad_NPC_Auftrag` = mainquest 3.1.4 gladiator NPC mission)
- `NQ_*` — secondary/named quests (e.g. `NQ_LOG_Qend`)
- `RTYPE_NPC_<class>` — labels keyed off the running character class
- `RG<n>` — region index
- `tptarget_<class>_<id>` — class-specific teleport targets
- `Belohnung_*` — reward bindings (Whiskey, etc.)
- `MonsterType`, `GNr/GNr2/SNr/GTO/Tmp` — short engine vars

## 7. Implications for modding

| Want | Path |
|---|---|
| Tweak existing daily-quest weights | Find the `0x64 'd'` records whose two varying integers fall into your target level band; change them. |
| Add a new reward variable | Append a `0x43 'C'` record `{ flags=0, 0x01 + "MyVar" + 0x00, 0x0b + initial_value }` at a syntactically safe point. |
| Change template selection probabilities | The `0x76 'v'` pair `(level, count)` per chain entry — likely weights. |
| Rename or rebind a quest hook | `0x68 'h'` records reference quest names by string; change the embedded cstr. |
| Hot-edit at runtime | Pending: find where `Sacred.exe` reads `%s\FunkCode.bin` and whether the loaded table stays mutable in memory. |

## 8. Open problems

1. Resolve the remaining ~30 type tags (especially `0x02`, `0x36`, `0x04`, `0x1e`) — they account for ~50 K of the 117 K parsed fields.
2. Cross-reference template names with `scripts\us\global.res` — `0x16` literals like `SOUND_FX_VERRUECKTER_SFX` likely resolve to localized resources.
3. Locate the parser in `Sacred.exe` by xref'ing the `DQ_NUM` / `GNr` strings (they appear in `.data`). Gives field-type interpretations confirmed by the compiler/parser.
4. Symmetric encoder — once §1–§3 are solid, a *FunkCode → text → FunkCode* round-trip is reachable.

## 9. Tools

- [`sdk\re\py\funkcode_tagmap.py`](../tools/funkcode_tagmap.py) — produces [04-funkcode-tags.md](04-funkcode-tags.md) (tag table, category breakdown, deterministic chains, samples).
- [`sdk\re\py\funkcode_grammar.py`](../tools/funkcode_grammar.py) — proves the §2 payload grammar by parsing every record's field stream against the known opcode table; reports unrecognised tails. (Supersedes the earlier `funkcode_typemap.py` probe, removed in the tools cleanup.)
