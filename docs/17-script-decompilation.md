# Script-level decompilation — full pipeline

This document describes the **end-to-end FunkCode decompilation pipeline**
that's now operational. Together with the text-side workflow (docs 14, 15)
this gives full read access to every quest in Sacred Gold at both the text
AND script level.

## What you can now do

For ANY quest prefix:

```bash
# TEXT side (resolves all hashed string IDs to readable text)
python sdk/tools/quest_dump.py HQ_3_1_4

# SCRIPT side (dumps every FunkCode bytecode record that mentions this quest)
python sdk/tools/quest_script.py HQ_3_1_4

# BULK (all 435 quests at once)
python sdk/tools/quest_book.py            # → sdk/logs/questbook.md         (332 KB)
python sdk/tools/quest_script_book.py     # → sdk/logs/quest_scripts.md     (12.5 MB)
```

## How the pipeline came together

### 1. Bytecode interpreter (FUN_00472bc0)
2522 lines, 162 cases, 40 functional groups. Decoded as an
**operand-decoder**, NOT an action-executor: it returns the opcode value to
the caller, who then dispatches per-opcode.

See [16-interpreter.md](16-interpreter.md).

### 2. Outer walker (FUN_00475680)
2204 lines, **132 record-tag cases**. THE central routing table — given a
FunkCode record's `tag` byte, dispatches to one of **116 subsystem
functions**. We mapped the full table via Ghidra xref + decompile of all
99 unique target functions.

The 10 most-cited subsystem callers are the ones we identified by name in
docs/16. With the outer walker decoded, we now know the full 116-tag
universe and can label each record.

### 3. Subsystem identification
For each subsystem function, we extracted:
- Its first non-trivial FUN call
- Its referenced string literals
- The opcodes it handles via `if (rv == N)` chains

53 of 116 subsystems have at least one identifying string. Examples:

| tag | name | derived from |
|---:|---|---|
| `0x01` | `CreateNPC` | `'CreateNPC failed: vx=%d, Type=%d'` |
| `0x08` | `CreateOBJ` | `'CreateOBJ: %s mehrfach -> ...'` |
| `0x21` | `HideTmpToDo` | `'HideTmpToDo: Pool=%d, ToDo=%d'` |
| `0x2e` | `Teleport` | `'Teleport failed: resnum=%d'` |
| `0x35` | `QuestLogSet` | live data: refs to `res:DQ_*_LOG_*` |
| `0x40` | `QuestKompassOBJ` | `'QuestkompassOBJ failed: Quest=%d'` |
| `0x44`, `0x45` | `HeroQBit_set/clear` | `'HeroQBit'` literal |
| `0x4e` | `ChestSetup` | `'Truhe %s falscht benannt'` |
| `0x68`, `0x7f`, `0x80` | `PlayFX_*` | `'SOUND_FX_'` |
| `0x6d`, `0x71` | `PoolClear_*` | `'PoolEmpty'` |
| `0x77` | `DQ_QuestSetup` | `'DQ_Quest'` |
| `0x84` | `ShowImage` | `'witz2.bmp'` |

Full table: `sdk/tools/funkcode_tags.py` and `sdk/logs/subsystem_label.txt`.

### 4. Tag-labelled disassembler
`sdk/tools/funkcode_disasm.py` walks records and prints them with
human-readable subsystem labels:

```
00033b2b  RECORD tag=0x35 '5'  [QuestLogSet]  size=38  payload=35B
    flags=0x00
    +0001  0b  CMD_0B   (complex; size unknown — synthesised +1)
    +0002  a5  UNK
    +0003  3a  CMD_3A
    ...
    +000b  01  DLG_OP_a   'res:DQ_15013_LOG_TITEL', '...'
```

The shape `tag=0x35 [QuestLogSet] ... 'res:DQ_15013_LOG_TITEL'` is
**human-readable** = "set quest 15013's title-log entry."

### 5. Quest script extractor (the payoff)
`sdk/tools/quest_script.py` finds every record across the 8 class FunkCode
files that mentions a quest prefix, and dumps them in execution order with
all the labels above.

Concrete demo for DQ_15013 ("Green Plague" — kill 20 goblins for Frank
Shepherd):

```
0003393b  tag=0x1a'?' [InlineHandler_1a]  size=20  payload=17B
    flags=00; opcodes 1e 44 51 5f 31 35 30 31 33 ...
    (3 identical records — 3 quest stages with same trigger structure)

00033a1f  tag=0x35'5' [QuestLogSet]  size=37
    +000b  01  DLG_OP_a   'res:DQ_15013_LOG_ZIEL', '...'      ← log: completion
00033aa7  tag=0x35'5' [QuestLogSet]  size=38
    +000b  01  DLG_OP_a   'res:DQ_15013_LOG_OFFEN', '...'     ← log: in-progress
00033b2b  tag=0x35'5' [QuestLogSet]  size=38
    +000b  01  DLG_OP_a   'res:DQ_15013_LOG_TITEL', '...'     ← log: title
```

Read alongside the TEXT side dump:

```
DQ_15013 quest card:
  _LOG_TITEL  : 'Green Plague'
  _LOG_OFFEN  : 'I have eliminated 20 goblins ...'
  _LOG_ZIEL   : 'Frank Shepherd was overjoyed and handed me a generous reward.'
```

→ The 3 QuestLogSet records SET those exact text references. We've
**linked logic to text** for the first time.

## Files added in this round

| File | Purpose |
|---|---|
| `tools/funkcode_tags.py` | 116-tag → name dictionary (source of truth) |
| `tools/funkcode_disasm.py` | tag-labelled record/opcode disassembler |
| `tools/quest_script.py` | per-quest script extractor |
| `tools/quest_script_book.py` | bulk dump of every quest's script |
| `tools/walker_dispatch.py` | parses outer-walker case table |
| `tools/subsystem_label.py` | extracts strings/opcodes per subsystem |
| `tools/interpreter_strings.py` | resolves DAT_*/s_* refs to text |
| `tools/extract_subsystem_addrs.py` | feed list to DecompileFunc |
| `tools/ghidra/XrefsToMulti.java` | batch xref helper |
| `logs/walker_dispatch_table.txt` | full 132-case table |
| `logs/subsystem_label.txt` | per-subsystem opcodes + identifier strings |
| `logs/quest_scripts.md` | **12.5 MB bulk script dump** of all 435 quests |
| `docs/16-interpreter.md` | updated with split-VM finding |
| `docs/17-script-decompilation.md` | this document |

## Open work for full pseudo-code emission

Three remaining gaps to "decompile DQ_15013 to readable Python":

1. **Identify remaining ~63 unlabeled subsystems** (mostly the medium/small
   ones, ~50-300 lines each). Each ~10 minutes of Ghidra reading. Can be
   automated by clustering similar bodies or extracting more specific
   signals (FUN call patterns, memory access offsets).
2. **Decode complex opcodes** (0x16, 0x1D, 0x1E, 0x3A, 0x6F, 0x71, 0x7A,
   0x95) used inside subsystem bodies — they're variable-width and our
   disassembler currently treats them as +1 byte placeholder. Need to walk
   the case bodies in FUN_00472bc0 to extract their parameter encodings.
3. **Decode 0x01 setup-payload variants** — the part after the 2 cstrings
   is target-type-dependent (3 ints for CPOS:hero, etc.). Mapping these
   gets us full operand resolution.

Once those are done, we can emit:

```python
quest("DQ_15013", "Green Plague"):
    on_setup:
        log_title  = res:DQ_15013_LOG_TITEL
        log_open   = res:DQ_15013_LOG_OFFEN
        log_done   = res:DQ_15013_LOG_ZIEL
    on_trigger(killed=goblin, count=20):
        award_reward(<reward_id>)
        set_log(log_done)
```

For now we can do a "narrative reading" of any quest by combining TEXT card
+ SCRIPT dump side-by-side.

## How a future text-mod or logic-mod would work

**Text-mod (works now)**:
```bash
python sdk/tools/quest_dump.py NQ_5001                       # find target
python sdk/tools/globalres_modify.py --by-name NQ_5001_LOG_TITLE --to "..."
# Patch 1 in our DLL serves the modified file from disk on next launch
```

**Script-mod (future, requires #1-#3 above)**:
```bash
python sdk/tools/quest_script.py NQ_5001                     # see structure
# edit FunkCode bytecode in `bin/TYPE_NPC_*/FunkCode.bin`
# loaded via Patch-N-style hook (analogous to Patch 1 for global.res)
```
