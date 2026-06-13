# 03 — `bin\TYPE_NPC_*\FunkCode.bin` (script bytecode)

Per-class scripted-content files. Path-template from `Sacred.exe`: `%s\FunkCode.bin` where `%s` ∈ {`TYPE_NPC_SERAPHIM`, `_GLADIATOR`, `_MAGICIAN`, `_ELVE`, `_DARKELVE`, `_DAEMONIN`, `_VAMPIRELADY`, `_ZWERG`}.

Two parallel sets exist:

| Set | Path | Size | Per-class distinct? |
|---|---|---|---|
| Base | `bin\TYPE_NPC_<class>\FunkCode.bin` | ~3.96 MB | yes (slightly) |
| Addon | `bin\Addon\TYPE_NPC_<class>\FunkCode.bin` | 2 504 248 B | no — all 8 byte-identical (SHA `475a0361…`) |

Same byte-identity holds for `Addon\*\StartCode.bin` (`b838d0f5…`) and `Addon\*\Vectoren.bin` (`47800c5a…`). The Underworld content is loaded once; the per-class folder is a lookup convenience.

## Framing

Flat TLV stream, no nesting: `[tag:u8][size:u16 BE][payload: size-3 bytes]` repeated until EOF.

SERAPHIM base parses cleanly into 125 055 records that tile the entire 3 962 707 B file with zero leftover.

`size` is **big-endian**. The byte at +2 is the size LSB, +1 is always 0x00 (no record reaches 64 KB).

## Top-level tag inventory (SERAPHIM base, 125 055 records)

| Tag | Char | Count | Probable role |
|---|---|---|---|
| `0x73` | `s` | 22 762 | most common — likely `statement` / generic stmt node |
| `0x64` | `d` | 11 498 | `declare` / data |
| `0x08` | — | 6 774 | numeric token (probably argcount or type-tag inside expr) |
| `0x01` | — | 6 093 | "name follows" subtag |
| `0x42` | `B` | 5 391 | `Block` (paired with `b`) |
| `0x33` | `3` | 5 309 | digit-like — likely literal int |
| `0x43` | `C` | 4 845 | `Const` decl (variable type declaration) |
| `0x35` | `5` | 4 509 | digit-like |
| `0x1a` | — | 4 303 | structural |
| `0x3c` | `<` | 3 200 | open / target |
| `0x3a` | `:` | 2 756 | label |
| `0x3b` | `;` | 2 733 | end-of-statement |
| `0x03` | — | 2 580 | int literal? |
| `0x3e` | `>` | 2 574 | close / jump |
| `0x69` | `i` | (in dump) | `init` — variable initialization |
| `0x68` | `h` | (in dump) | likely `hook`/`handle` |
| `0x7d` | `}` | seen | sentinel block (`0x9F` + `0xFEEDBEEF` magic) |

Distinct ASCII tag values seen: 70 (` ! # $ ) * + , . 0-9 : ; < > ? @ B C D E G H I J K L N O V W X Y Z [ \ ] ^ _ \` a c d g h i l m o p q s t u v w x y z { | } ~`) — the size of the language's node-kind alphabet.

## Decoded sample (first 0x300 bytes of SERAPHIM)

```
@ 0x000  C  size=23   sub=01 name="dq_belohnung"          trail=[0b 00000000]
@ 0x017  C  size=27   sub=01 name="dq_belohnung_typ"      trail=[0b 00000000]
@ 0x032  C  size=29   sub=01 name="dq_belohnung_level"    trail=[0b 00000000]
@ 0x04f  i  size=28   sub=01 name="dq_belohnung"          trail=[0b 00000001 0b 0000007f]
@ 0x06b  i  size=32   sub=01 name="dq_belohnung_typ"      trail=[0b 00000000 0b 0000000a]
@ 0x08b  :  size=5    payload=[00 96]                     ← label id 0x96
@ 0x090  i  size=34   sub=01 name="dq_belohnung_level"    trail=[0b 00000001 0b 0000000a]
@ 0x0b2  B  size=5    payload=[00 97]                     ← block id 0x97
@ 0x0b7  i  size=34   sub=01 name="dq_belohnung_level"    trail=[0b 0000000b 0b 00000014]
@ 0x0d9  B  size=5    payload=[00 98]
@ 0x0de  i  size=34   sub=01 name="dq_belohnung_level"    trail=[0b 00000015 0b 0000001e]
@ 0x100  B  size=5    payload=[00 99]
@ 0x105  i  size=34   sub=01 name="dq_belohnung_level"    trail=[0b 0000001f 0b 00000028]
@ 0x127  B  size=5    payload=[00 9a]
@ 0x12c  i  size=34   sub=01 name="dq_belohnung_level"    trail=[0b 00000029 0b 00000032]
@ 0x14e  ;  size=4    payload=[00]                        ← end
@ 0x152  >  size=4    payload=[00]
@ 0x156  }  size=68   sentinel: 9F FEEDBEEF "dq_belohnung" 9F FEEDBEEF …
@ 0x19a  }  size=19   [0b 04 0b 01 0b 05]                 ← (4,1,5) triple
@ 0x1ad  }  size=19   [0b 08 0b 01 0b 05]                 ← (8,1,5)
@ 0x1c0  }  size=19   [0b 10 0b 01 0b 05]                 ← (16,1,5)
@ 0x1d3  }  size=19   [0b 20 0b 01 0b 05]                 ← (32,1,5)
@ 0x1e6  :  size=32   payload="RTYPE_NPC_GLADIATOR" …      ← class-tag label
@ 0x206  ?  size=38   name "HQ_3_1_4_glad_NPC_Auftr…"      ← Hauptquest name
@ 0x22c  h  size=38   name "HQ_3_1_4_glad_NPC_Auftr…"      ← hook into same quest
@ 0x252  <  size=21   ["res:1024", "ok_hq"]                ← resource ref + label
@ 0x267  B  size=24   "RTYPE_NPC_GLADIATOR"                 ← Block for that class
@ 0x27f  ?  size=38   "HQ_3_1_4_glad_NPC_Auftr…"
@ 0x2a5  h  size=38   "HQ_3_1_4_glad_NPC_Auftr…"
@ 0x2cb  <  size=25   ["res:1024", "trigger99"]
```

### What this shows

1. Serialized symbol-table + bytecode tree, not raw bytecode. Variable names (`dq_belohnung` = "daily-quest reward"), quest names (`HQ_3_1_4_glad_NPC_Auftrag…` = mainquest 3.1.4 gladiator NPC mission), labels (`RTYPE_NPC_GLADIATOR`), and resource refs (`res:1024`) all live in plain text.
2. Typed values. A trailer `[0b XX XX XX XX]` is `(type=0x0b, u32 value)`. Records like `i / dq_belohnung_level / [0b min 0b max]` declare a range-typed integer variable (e.g. `dq_belohnung_level ∈ [0xb..0x14]`).
3. Control-flow primitives: `:` = label, `B` = block, `;` = end-of-stmt, `<`/`>` = open/close.
4. Quest content lives here: `HQ_3_1_4_glad_NPC_Auftrag…` with `res:1024` + `ok_hq` are quest hooks tied to localized dialogue resources in `global.res`.
5. `0x9F 0xFEEDBEEF` sentinel bookends some named blocks — a structural/debug fence usable as a locator.

A FunkCode → readable text dumper is reachable. A symmetric text → FunkCode re-serializer would give a modding pipeline that requires no EXE patching.

## Per-class divergent region

Comparing two base files byte-by-byte:

```
SERAPHIM vs GLADIATOR:
  common prefix      = 1 700 496 bytes (0..0x19F290)
  common suffix      ≈ 2 255 670 bytes
  per-class region:
      SERAPHIM:   0x19F290 .. 0x1A0C1D   ( 6 541 bytes)
      GLADIATOR:  0x19F290 .. 0x1A1D49   (10 937 bytes)
```

Less than 0.3% of FunkCode.bin is class-specific; the rest is shared global script content (overworld quests, NPCs) compiled into every class file.

First 64 bytes of the per-class region:

```
SERAPHIM @ 0x19F290:
  35 00 1f 00 0b 09 00 00 00 0b 01 00 00 00 01 72 65 73 3a 4e 51 5f 4c 4f 47 5f 51 65 6e 64 00 …
  → tag '5', size 0x1F, ref to "res:NQ_LOG_Qend" (Niob-quest log end?)

GLADIATOR @ 0x19F290:
  2e 00 1a 00 04 fe ff ff ff 74 70 74 61 72 67 65 74 5f 67 5f 30 31 00 …
  → tag '.', size 0x1A, ref to "tptarget_g_01" (teleport target gladiator 01)
```

Per-class slot content: class-specific quest log entries and class-specific teleport targets / trigger names.

## Hashes (base)

| Class | SHA-256 head | Size |
|---|---|---|
| SERAPHIM | `fa40baab1977fd10…` | 3 962 707 |
| GLADIATOR | `f2581403b7c391ee…` | 3 967 103 |
| MAGICIAN | `b3a5ec8cff39d364…` | 3 962 453 |
| ELVE | `188cde9659a75410…` | 3 961 814 |
| DARKELVE | `f2bde6ca116b3d1f…` | 3 961 940 |
| DAEMONIN | `ed13387510134cb6…` | 3 962 049 |
| VAMPIRELADY | `68bf2d31151ba6c1…` | 3 963 314 |
| ZWERG | `88a71fdb494f889a…` | 3 960 242 |

All 8 Addon variants: `475a0361…` (identical).

## Tooling

- `sdk\tools\funkcode_walker.py` — TLV walker with the correct BE size parser. Dumps tag inventory, first 30 records, per-class divergence stats. Recursive mode disabled (top-level walk covers 100% of the flat stream).

## Next probes

1. Build a tag→meaning table: isolate every distinct tag, dump 5 examples of each, compare payload shapes. Current `C/i/B/:/;/></}` mappings are heuristic.
2. Dump every record holding `subtag=0x01 + cstr` → full symbol list. Tag `0x01` count = 6 093, close to the number of named entities.
3. Walk the type tags. `0x0b` = "u32 integer with value". Find what `0x04`, `0x05`, `0x08`, `0x49`, `0x4e`, `0x4f`, `0x14` mean — likely string-ref, float, bool, enum, list.
4. Cross-reference `res:1024` strings with `scripts\us\global.res` — confirm resource ID format and that quest names hook into localized text.
5. Simplest text mod: change `dq_belohnung_level` max range from 0x32 (50) to larger, rerun, check whether a quest reward level cap changes. Validates that FunkCode is read live (not baked at install).
6. Decode the per-class 6 541-byte slot for each character — smallest, highest-leverage region for class-balance mods.
