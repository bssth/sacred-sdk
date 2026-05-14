# 08 — Quest decompilation: distance & state

This document answers the question "how far are we from being able to *decompile* Sacred's quests?". Short answer: **much closer than I expected at the start of the session** — the two long-form formats that quests live in are now both readable. What's missing for true decompilation is mainly semantic labelling and one hash function.

## What "decompile a quest" should mean

Concretely, the goal is, given a quest token like `HQ_3_1_4_glad_NPC_Auftrag_Qstart`:

```
quest HQ_3_1_4_glad_NPC_Auftrag (main, gladiator):
  title:  "..."                ← from global.res
  header: "..."                ← from global.res
  Qstart: "Find Corvin's amulet in the catacombs."
  Qoffen: "Catacombs entry is east of Bellevue."
  Qend:   "You found the amulet! Return to Corvin."
  steps:
    - go to <position>
    - kill <monster type>      ← from FunkCode bytecode
    - return to <NPC>
    - reward: <gold>
```

The **text** comes from `scripts\us\global.res`. The **logic** comes from `bin\TYPE_NPC_*\FunkCode.bin`. We need both, plus a binding (symbolic name → numeric id) between them.

## What we have

### A. Quest names (full inventory) ✅

`sdk\tools\quest_inventory.py` enumerates every quest token across all 8 base FunkCode files:

| Bucket | Distinct | Total citations | Examples |
|---|---|---|---|
| MainQuest `HQ_*` | **287** | ~2 600 / class | `HQ_3_1_4_glad_NPC_Auftrag_Qstart`, `HQ_7_1_1_NPC_Auftrag_Qstart` |
| NamedQuest `NQ_*` | **242** | ~440 / class | `NQ_5001_LOG_TITLE`, `NQ_6001_AUFTRAG_QOFFEN`, `NQ_UW9521_LOG_OFFEN2` |
| DailyQuest vars `DQ_*` | **2 400** | ~19 000 / class | `DQ_NUM`, `DQ_Suchrichtung`, `DQ_RG16_DUNGEONQUEST3` |
| DailyTemplate `DQ#_*` | **176** | ~4 900 / class | `DQ1_TOETE_NPC_ZIEL`, `DQ23_BRINGE_NPC` |
| ResRef `res:*` | **2 772** | ~8 400 / class | `res:1024` (×7627), `res:HQ_3_2_3_Log_Title`, `res:NQ_Log_Qend` |
| TPTarget | 6 | 1–12 | `tptarget_g_01`, `tptarget_r_02` |
| RType | 10 | 400–860 | `RTYPE_NPC_SERAPHIM`, `RTYPE_NPC_DWARF` |

Quest-name naming convention is regular:

```
HQ_<chapter>_<section>_<step>[_<class>]_<role>_<state>
  state ∈ { Qstart, Qoffen, Qend, Qsieg, Qfail, Log_Title, Log_Header, Log_Qstart, … }
  role  ∈ { NPC_Auftrag (NPC mission), PRENPC_PRESTART, AUFTRAG, … }
  class ∈ { glad, sera, mage, helf, vamp, DEM, DWA }
```

So one quest carries up to ~10 string keys (the role/state combinations). Resolving them all yields a complete quest definition's *text*.

### B. `global.res` decoder ✅

Cracked in this session. `sdk\tools\globalres_resolve.py` parses the format end-to-end:

```
+0x00    16-byte index records, each:
              u32 (raw_len — describes the *previous* entry's text size, off-by-one
                              encoder quirk; ignore and derive from offsets instead)
              u32 id          (low: linear from 49; high: hash of symbolic name)
              u32 offset      (points 4 bytes BEFORE the actual UTF-16 text)
              u32 zero
+0x5A530 text blob (UTF-16 LE, packed back-to-back, no separators)
```

Empirical formulas:

```
text_start = entry.offset + 4
text_end   = next_entry_by_offset.offset + 4   (file_end for the last)
text       = data[text_start : text_end] decoded as UTF-16 LE
```

The resolver works on **all 23 123 valid index entries**. Sample resolutions:

| id | text |
|---|---|
| 49 | `Seraphim` |
| 50 | `Gladiator` |
| 51 | `Battle Mage` |
| 588 830 397 | `Shield` |
| 80 042 359 | `Paternus' Boots` |
| 900 652 506 | `What an arena!` |
| 2 024 470 250 | `Destroy the skeleton` |
| 1 626 898 883 | `Slave Dealer` |
| 75 725 982 | `Scroll: The Nuk-Nuk` |
| 1 898 360 169 | `Useless bunch! Do I have to do everything myself ...` |

The id space has two regions:
- **49..few hundred**: linear ids manually allocated to UI strings (class names, attributes, common nouns).
- **~67M..~2.1B**: 32-bit values that look like hashes of symbolic names. These are what `res:HQ_3_1_4_Log_Title` and friends resolve through.

### C. `FunkCode.bin` grammar ✅

From earlier docs (03/05/06):
- Framing 100 % pinned (`[tag:u8][size:u16 BE][payload]`, 125 055 records, no leftover).
- Payload grammar 100 % (`flags:u8 (type:u8 value)*`).
- Value-type dictionary: 53 entries covering ~76 % of all payload bytes.
- 982 distinct named symbols extracted (variable / quest / NPC identifiers).
- 110 daily-quest templates extracted in full.

## What's missing for true decompilation

### Missing piece 1: hash function for symbolic `res:` ids

FunkCode references `res:HQ_3_1_4_Log_Title` as a plain ASCII cstr inside its payload. At runtime, Sacred must convert this string to the same 32-bit `id` that `global.res` indexes by. We need to discover the hash function (CRC32? FNV? a custom Sacred one?) and reproduce it.

Approaches in increasing cost:
1. **Bruteforce**: hash all 982 symbol names with ~10 common functions, look for hits in the global.res id set. **Cheap (1–2 hours), high success probability**.
2. **Pattern-match by content**: search `global.res` for strings that look like the title of `HQ_3_1_4` (e.g. by language pattern — "Auftrag" appears in German titles), then read the index entry's id and back-correlate to the symbolic name. Slower but works if hash bruteforce fails.
3. **Ghidra**: find the function in `Sacred.exe` that takes a cstr and looks up in the resource pool. Read its hash directly.

### Missing piece 2: tag semantics for control flow

Pinning the FunkCode tag alphabet — knowing that `s` is "statement" or `B` is "block" — is the difference between "we can read it" and "we can decompile it". Right now we have probabilistic guesses from bigram analysis. The remaining ~24 % of unparsed payload bytes is a symptom: a couple of types still have ambiguous widths and the parser misaligns downstream. See [06-funkcode-types.md](06-funkcode-types.md) for the alignment-cascade discussion.

Cheapest fix is, again, **read the parser in `Sacred.exe`** (step 5 in the roadmap). One Ghidra session on the function that reads `%s\FunkCode.bin` gives us:
- exact type-encoding for each value type byte,
- exact tag semantics for each record kind,
- the runtime data structure each record fills in.

### Missing piece 3: numeric `res:` ids 1024, 17631, etc.

The top-referenced ids from FunkCode (1024, 1037, 1038, 17631, 17643, 17656, 18007, 18237, …) are **not in the global.res index** despite being cited thousands of times. Hypotheses:

1. They aren't text resources at all — they reference a different table (sound profiles, sprite ids, item types). `res:1024` may map to a `Bin\*.bin` data table or an in-memory enum.
2. They're packed into `global.res` differently (a sub-table starting later in the index).
3. They go through *yet another* indirection (`res:1024` is symbolic too — maybe an alias).

Bench-marking work: peek at hot occurrences in FunkCode context. If they appear near sound effects → `res:1024` is a sound id. If near monster references → it's a creature id. This is a 1-evening probe.

## Distance estimate (revised)

| Goal | Effort | Status |
|---|---|---|
| List every quest in the game | trivial | ✅ already have, 287 main + 242 named |
| Resolve any hashed `res:` id from global.res to text | trivial | ✅ done (all 23 123 entries) |
| Resolve every symbolic `res:HQ_*` reference to text | 2–4 hours | 🟡 needs hash bruteforce |
| Resolve low-number `res:1024`-style ids | 1 evening | ❌ unknown table; need probe |
| Print a quest as title + body + log entries (no logic) | 1 day | 🟡 within reach once hash is solved |
| Decode a quest's full control flow (steps, conditions, rewards) | 1–2 weeks | ❌ needs FunkCode tag semantics (Ghidra) |
| Roundtrip text-edit → recompile FunkCode | 1–2 months | ❌ needs symmetric encoder + tag certainty |

## Recommended next experiments

In order of leverage:

1. **Bruteforce hash function** for symbolic `res:` names. Worth ~2 hours; if it works it unlocks dumping every quest's text directly from the symbolic name. Implementation: hash all ~982 known symbol names with FNV-1a, CRC32, DJBX33A, Pearson, etc., intersect with the global.res id set, keep the function with the most hits.
2. **Probe `res:1024` cluster.** Take 20 records around an occurrence of `res:1024` in FunkCode and read what's nearby. If they're sound-flavoured (volume, panning), `res:NNN` is a sound id. If they're textual (other resource refs), it's a sibling localisation table.
3. **First quest extractor** (using only what's confirmed). Takes a quest prefix (`HQ_3_1_4`), finds every record in FunkCode that mentions any of `HQ_3_1_4_*`, dumps decoded payload with our 53-type dictionary. Even without text resolution this is a useful "this is what the game does for this quest" view for someone literate in Sacred.
4. **Ghidra triage** of `Sacred.exe`. Xref `FunkCode.bin` string in `.rdata`, follow into the parser, read the value-type switch table. Closes piece 2 above and gives us 100 % of FunkCode for free.

## Tools relevant to this doc

- [`sdk\tools\quest_inventory.py`](../tools/quest_inventory.py) — enumerates HQ/NQ/DQ/`res:` names across all 8 base FunkCode files.
- [`sdk\tools\globalres_peek.py`](../tools/globalres_peek.py) — header/strings probe of `scripts\us\global.res`.
- [`sdk\tools\globalres_resolve.py`](../tools/globalres_resolve.py) — full resolver: parse index, resolve id → text. Importable: `from globalres_resolve import all_ids, get_text`.
