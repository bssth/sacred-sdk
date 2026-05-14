# Pseudo-code decompilation — DONE

Sacred FunkCode is now readable as **quest-script pseudo-code**. We can take
any quest's bytecode records and emit a near-source-level rendering of what
the quest does.

## Sample: first 12 records of GLADIATOR FunkCode.bin

```
RECORD tag=0x43 'C' [VarDecl_C]
    01 DLG_OP_a 'dq_belohnung', type=0x0b              # declare quest-reward as int
    END

RECORD tag=0x43 'C' [VarDecl_C]
    01 DLG_OP_a 'dq_belohnung_typ', type=0x0b          # declare reward-type as int
    END

RECORD tag=0x43 'C' [VarDecl_C]
    01 DLG_OP_a 'dq_belohnung_level', type=0x0b        # declare reward-level as int
    END

RECORD tag=0x69 'i' [VarAssign_int]
    01 DLG_OP_a 'dq_belohnung', type=0x0b
    END
    0b U32 u32=127                                     # dq_belohnung = 127

RECORD tag=0x69 'i' [VarAssign_int]
    01 DLG_OP_a 'dq_belohnung_typ', type=0x0b
    END
    0b U32 u32=3                                       # dq_belohnung_typ = 3

RECORD tag=0x3a ':' [ConditionalEval]
    96 STACK_96                                        # if (condition_96):

RECORD tag=0x69 'i' [VarAssign_int]
    01 DLG_OP_a 'dq_belohnung_level', type=0x0b
    END
    0b U32 u32=10                                      #   dq_belohnung_level = 10

RECORD tag=0x42 'B' [BlockReader]
    97 STACK_97                                        # else / next-branch

RECORD tag=0x69 'i' [VarAssign_int]
    01 DLG_OP_a 'dq_belohnung_level'
    0b u32=20                                          #   dq_belohnung_level = 20

RECORD tag=0x42 'B' [BlockReader]
    98 STACK_98                                        # next branch

RECORD tag=0x69 'i' [VarAssign_int]
    01 DLG_OP_a 'dq_belohnung_level'
    0b u32=30                                          #   dq_belohnung_level = 30
```

Reads as:

```python
declare dq_belohnung       : int
declare dq_belohnung_typ   : int
declare dq_belohnung_level : int

dq_belohnung     = 127
dq_belohnung_typ = 3

if   condition_A: dq_belohnung_level = 10
elif condition_B: dq_belohnung_level = 20
elif condition_C: dq_belohnung_level = 30
...
```

A daily-quest reward configuration table — three reward tiers gated on
condition opcodes 96/97/98 (some level/region check).

## The complete opcode coverage

Of the 162 opcode cases / 40 functional groups in the dispatcher
`FUN_00472bc0`, we now decode:

| Kind | Opcodes | What |
|---|---|---|
| `END` | 0x00, 0x2b | record-end / clause separator |
| `STACK_*` | 0x06..0xa1 (69) | stack/condition operators (no payload) |
| `HALT/BREAK` | 0x37, 0x4b | terminator |
| `MATH_*` | 0x05, 0x09, 0x41, 0x60, 0x83 | 1-byte math |
| `U32_*` | 0x02, 0x0b, 0x11, 0x1c, 0x36, 0x38, 0x3b, 0x3c, 0x53-57, 0x5f, 0x73, 0x75, 0x7e, 0x7f, 0x84, 0x86, 0x8c, 0x90 | u32 const (4-byte) |
| `U32PAIR_*` | 0x15, 0x33, 0x34, 0x79, 0x87-0x89 | u32 pair (8-byte) |
| `XYZ_*` / `U32_TRIPLE` | 0x19, 0x20, 0x2a, 0x4d | u32 triple (12-byte, coords) |
| `U32_QUAD` | 0x3d | u32 quad (16-byte) |
| `U16` | 0x16 | u16 (2-byte) |
| `U8` | 0x28, 0x8b | u8 (1-byte) |
| `C3_*` | 0x1f, 0x9f | 3-byte payload |
| `STR_REF` | 0x1e | 1 cstring (resource/variable reference) |
| `STR_LOOKUP_*` | 0x47, 0x52, 0x7d, 0x8f | string lookup |
| `VAR_LOOKUP_*` | 0x3e, 0x77, 0x81, 0x82, 0x67 | variable lookup (cstring + 1 byte type) |
| `RES_LOOKUP_*` | 0x40, 0x95, 0x1d, 0x3a, 0x6f, 0x7a | resource lookup (various combos of u32+cstring) |
| `DLG_OP_*` | 0x01, 0x04, 0x0c, 0x0d, 0x29, 0x63, 0x68, 0x69, 0x6a, 0x9d | 2 cstrings (name + target) |
| `BlockMarker` | 0x16 | 1 cstring + magic sentinel (block start) |
| `EMIT_*` | 0x48-0x4a, 0x5d, 0x5e, 0x6d, 0x6e | u32 + cstring (text emission) |
| (still complex) | a handful inside inline-walker bodies | 0x3b/0x42 in walker etc. |

Approximate coverage: ~95 % of all opcodes in the dispatcher now have a
correct width and a readable label. Misalignments inside complex records
should be rare.

## Quest tag coverage

79 of 112 tags labelled, including all the **most-frequent** ones:
- 0x43 / 0x69 = **VarDecl / VarAssign** (the variable system)
- 0x42 = **BlockReader** (if/then body wrapper)
- 0x3a = **ConditionalEval** (if expression)
- 0x3b = **ELSE_jump** (else / forward skip)
- 0x35 = **QuestLogSet** (writes quest-log entries)
- 0x1a = **QuestTrigger** (registers a quest event by symbolic name)
- 0x40 = **QuestKompassOBJ** (quest-marker on map)
- 0x44/0x45 = **HeroQBit_set/clear**
- 0x6d/0x71 = **PoolClear**, 0x85 = **PoolGetPos**, 0x77 = **DQ_QuestSetup**
- 0x68/0x7f/0x80 = sound / music
- 0x84 = **ShowJokeImage** (witz1/2/3.bmp)

## What this unlocks

1. **Read any quest's logic**, not just text. Run
   `python sdk/tools/quest_script.py HQ_3_1_4` and see the if/else tree
   that gates the quest.
2. **Bulk read all quests' logic**: `sdk/logs/quest_scripts.md` now
   contains the labelled bytecode of all 435 quests.
3. **Modify quest logic** (future patch-N work): once we have a script-mod
   workflow analogous to Patch 1 for `global.res`, we can edit the FunkCode
   bytecode through a runtime hook. The encoder is mirrored from the
   decoder.

## Remaining 5 % of work for true source-level emission

- A few inline-walker bodies (0x3b, 0x42, 0x7a etc. in `FUN_00475680`)
  still need their byte widths confirmed.
- 33 still-unlabelled `Subsys_NN` tags (mostly 50-300 line bodies with
  no distinctive identifier strings).
- The "magic sentinel" cstring path for opcodes 0x0b/0x3c (rare cases
  where the u32 == `-0x12d687` / `-2` and a cstring follows). Currently
  treated as u32 only for safe alignment.
- The 0x01 dialog-dispatcher's target-type-dependent tail bytes (3 ints
  for CPOS:hero, more for CPOS:RES, etc.). Currently the 2 cstrings are
  decoded, the tail isn't deeply parsed.

None of these block reading quest logic; they only refine display for
edge-case records.
