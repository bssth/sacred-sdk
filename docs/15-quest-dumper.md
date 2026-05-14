# Quest text decompilation — DONE

The "missing pieces" listed in [08-quests.md](08-quests.md) are now closed:

| 08-quests piece | Status |
|---|---|
| Hash function for symbolic `res:` names | ✅ recovered, see [10-hash-cracked.md](10-hash-cracked.md) |
| Numeric `res:1024`-style ids | ✅ stringified-then-hashed, see same doc |
| Tag semantics for FunkCode control flow | ❌ still ahead of us — needs Ghidra on `FUN_00472bc0` |

So everything **text-level** about quests is fully resolvable now. What
remains is the *control flow* (when does the game show which text, what
conditions trigger which step, what gold reward), which lives in
`FunkCode.bin` bytecode and needs the interpreter dispatch table from
`FUN_00472bc0` to be read out of Ghidra.

## The tooling

### `sdk/tools/quest_dump.py`

Single-quest text card. Takes a prefix and prints every resolvable
`<prefix>_<suffix>` lookup. Picks suffixes both from FunkCode-literal
tokens (the real things Sacred references) and from a list of common
templates (catches strings Sacred composes at runtime).

```
python quest_dump.py HQ_3_1_4              # main quest
python quest_dump.py NQ_5001               # named quest
python quest_dump.py NQ_UW9521             # named, Underworld expansion
python quest_dump.py DQ_15013              # daily-quest instance (numeric)
python quest_dump.py RB_5082               # runebook quest

python quest_dump.py --list-prefixes HQ    # all main-quest root prefixes
python quest_dump.py --list-prefixes NQ
python quest_dump.py --list-prefixes RB
python quest_dump.py --list-prefixes DQ

python quest_dump.py --grep "the goblins"  # find quests by text content
```

Marks `[F]` on suffixes that appear literally in some class's
`FunkCode.bin` (vs. template-synthesised). Useful to see what Sacred
actually references versus what we just probed.

### `sdk/tools/quest_book.py`

Bulk-dump every quest to `sdk/logs/questbook.md`. Run once, get a
~330 KB single-file lookup of every quest's text in the game:
- 46 main quests (HQ)
- 21 named side quests (NQ)
- 9 runebook quests (RB)
- 359 daily-quest instances (DQ_<numeric_id>)

## Naming conventions empirically confirmed

After dumping all quests, the suffix vocabulary becomes clear:

```
HQ_<chapter>_<section>_<step>[_<class>]_<role>_<state>
  classes: glad, sera, mage, helf, vamp, DEM, DWA, delf
  roles:   NPC_Auftrag (NPC mission), PRENPC_PRESTART (intro), Log_*
  states:  Qstart, Qoffen, Qend, Qsieg, Qfail, Qziel

NQ_<id>_<role>_<state>
  Same as HQ minus the chapter/section/step axes. <id> is freeform.
  Region-tagged variants: NQ_UW<id> (Underworld), NQ_OW<id>, NQ_DW<id>...

RB_<id>_<state>
  Runebook quests. State is usually LOG_TITLE / LOG_HEADER / LOG_OK.

DQ_<id>_<state>
  Daily-quest instances. State subset: START, OFFEN, ZIEL, LOG_*.
  Note: DQ_<id> is the *instance* — a specific generated quest. DQs are
  templated by DQ<n>_<verb> (DQ1_TOETE, DQ5_BRINGE, ...) at quest type
  level, then instances inherit those templates.
```

## Workflow for text-mods

The full chain is now:

1. **Find** the quest you want to edit:
   ```
   python sdk/tools/quest_dump.py --grep "Mick the Swift"
   ```
2. **Identify** the suffix to change (e.g. `DQ_15013_LOG_TITEL`).
3. **Replace** in `global.res`:
   ```
   python sdk/tools/globalres_modify.py --by-name DQ_15013_LOG_TITEL \
                                        --to "Goblin Genocide"
   ```
4. **Restart Sacred** — Patch 1 reads the file from disk on every load.

Length-changing edits are fully supported (script rewrites the offset
table). Backup at `global.res.bak` on first modify.

## What remains for full decompilation

The control flow. To reconstruct e.g. "this quest gives 5000 gold when the
counter reaches 20 goblins killed", we need to read FunkCode bytecode
semantically. That requires:

1. Decompile `FUN_00472bc0` (the interpreter dispatcher). About 100 cases
   in a big switch; we've read ~40 in earlier sessions. Each case tells us
   what one bytecode tag does. See [06-funkcode-types.md](06-funkcode-types.md).
2. Find the outer walker (whoever calls `FUN_00472bc0` in a loop) — that
   gives us record boundaries and parameter passing.
3. With both, write a higher-level FunkCode dumper that emits readable
   "if killed(goblin, 20) then reward(5000, gold)" style output.

This is "1-2 weeks" effort per the original estimate in 08-quests.md.
Worth doing if/when we want a roundtrip "edit quest logic in text, recompile
back to FunkCode" workflow. For text-only mods, we don't need it.

## Sample output

Real quest dump (`HQ_3_1_4`):

```
=== HQ_3_1_4 ===
  11 distinct text entries:

  _Log_Title                 [F]   For King and Country
  _LOG_DWA_TITLE             [F]   For King and Country
  _Log_Header                [F]   Go and find Sergeant Treville at Porto Vallum
  _LOG_DWA_HEADER            [F]   Find Treville in the stronghold of Urkenburgh
  _Log_Qstart                [F]   A strange fate has led me out of the dark
                                   chambers of the arena and into this world. The
                                   crown seems to need my help. Rumor has it that
                                   there is unrest in Ancaria: King Aamum is old
                                   and weak, and the power-hungry barons are
                                   plotting to seize power. Prince Valor, the
                                   king's son, is fighting against the Orcs of
                                   Khorad-Nur in the south and needs brave
                                   fighters who have no fear of bloodshed. So,
                                   go and find this Treville, who is stationed
                                   at the Fortress of Urkenburgh in the southern
                                   borderlands.
  _DWA_NPC_AUFTRAG_QSTART    [F]   A Dwarf! Greetings to you! I must admit that
                                   I didn't believe in the existence of Dwarves.
                                   So, Ensign William has sent you? ...
  ... (per-class variants for Gladiator, completion text, etc.)

  appears in FunkCode for classes: DAEMONIN, DARKELVE, ELVE, GLADIATOR,
                                   MAGICIAN, SERAPHIM, VAMPIRELADY, ZWERG
```
