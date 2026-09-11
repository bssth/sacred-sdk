# Sacred Gold — FunkCode TAG MASTER TABLE — RE report

> **Aligned 2026-09-11 (wave 2) with `sdk/re/py/funkcode_tags.py`.** Its labels
> are the **compiler's own keywords**, extracted mechanically from the three
> keyword → tag resolvers in the exe (§5); `disasm_tiling_check.py --verify-tags`
> re-extracts them and fails on any drift. What changed in this file:
> * **the `0x3c` / `0x3f` / `0x40` rows were shifted by one** (SDK_GAPS gap 16).
>   `0x3c` is **SetButton**, the dialog answer button (`FUN_00499ba0`, whose only
>   string is `RES:%d`); `0x3f` is **QuestKompassPos** (`QuestkompassPos failed:
>   vx=%d, vy=%d`); `0x40` is **QuestKompassObj** (`QuestkompassOBJ failed:
>   Quest=%d`);
> * every other name now follows the keyword table (110 of the 115 wave-1 labels
>   changed), and 19 tags were added;
> * handler corrections: `0x28` and `0x29` are **inline** in the walker
>   (`FUN_004bb1d0` is only the vector-grow helper of `0x28`; `FUN_00603e30` is
>   not the `0x29` handler), and `0x33` is `FUN_0047c610` (shared with `0x23`);
> * §1's reading of the "condition records" was wrong: a false guard **abandons
>   the whole section**, it does not "no-op the following block";
> * §5's keyword section is replaced by the resolver tables.
>
> For what each mechanic *does* in a quest, read
> `sdk/.claude/knowledge/quests/MECHANICS.md`; this file is the tag ↔ handler
> reference.

Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`, image base
`0x00400000`, no ASLR (file offset == VA − 0x400000, little-endian x86).

Evidence base:
- Record walker `FUN_00475680` (`sdk/re/ghidra/decompiled/00475680_FUN_00475680.c`,
  2,205 lines, the top-level `switch(tag)`). Walker case lines are in
  `funkcode_tags.WALKER_CASE`.
- Field/opcode reader `FUN_00472bc0`, decoded into `funkcode_disasm.GRAMMAR`
  (all 256 opcode values, 40 groups, checked against the reader's jump table by
  `disasm_tiling_check.py --verify-exe`).
- The compiler keyword resolvers `FUN_00452370`, `FUN_00451be0`, `FUN_00452910`
  (§5).
- Per-tag handler decompiles and the wave-2 reverse-engineering files:
  `quests/RE_lifecycle_journal.md`, `RE_dialog_buttons.md`,
  `RE_conditions_vars.md`, `RE_rewards_npc_world.md`.
- Tag histograms measured with `funkcode_disasm.walk_records` over
  `bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin` (3,963,314 bytes, **125,060 records**)
  and `StartCode.bin` (577,275 bytes, **15,457 records**), 2026-09-11.

---

## 0. Framing (HIGH)

**Record framing as the tools read it (TLV):**

```
tag  : u8
size : u16 BIG-ENDIAN    ; size INCLUDES the 3-byte header
payload : (size-3) bytes ; payload[0] = "flags" byte, always 0
```

**As the engine reads it.** The walker reads a u16 tag at bytes 0-1 and a u16
size at bytes 2-3, both little-endian (`FUN_00475680:156`; every scan reads
`*(ushort*)(rec+2)`, e.g. `:1446`, `:1524`, `:1575`). The two readings agree
because byte 1 and byte 3 are 0 in all 1,381,228 records of the 21 md5-distinct
FunkCode/StartCode blobs, and no record is longer than 169 bytes (T1). So the
"flags" byte is the high byte of the engine's size. **A tool that writes the
size big-endian must keep every record ≤ 255 bytes** (INFERRED from the two
readings; not tested). The walker's scans also refuse any size ≥ 0x1f5 (501).

**Dispatch.** `FUN_00475680` switches on the u16 tag. There are 123 walker cases
(`funkcode_tags.WALKER_CASE`).
- Tags with **no case** fall to `default:`, "consume and continue": `0x06 0x09
  0x10 0x1b-0x1e 0x22 0x2a 0x3e 0x50-0x55 0x65 0x66`.
- `0x00` and `0x2b` go to `caseD_0`: the walker returns 0 and the section stops.
- `0x2a` is also intercepted before the switch by the `0x29` skip latch (§3).
- The IF/ELSEIF scans treat a tag of 0 or above `0x8c` as the end of the stream.

**Operand stream.** Most handlers loop `op = FUN_00472bc0(); … while (op != 0)`.
The reader returns the opcode byte (0 = END; `0x20` = resolved `CPOS:hero`) and
stages the decoded value at fixed context slots:
- `+0xa460` ASCIIZ scratch (and `+0xa560` for the second string of `0x92`);
- `+0xa860/64/68` X/Y/Z or an INT value;
- `+0xa880` a u32, a handle or a resolved `res:` id.

**Operand widths are fixed per opcode**, and the authoritative table is
`funkcode_disasm.GRAMMAR` (`python sdk/re/py/funkcode_disasm.py --grammar`;
also printed in MECHANICS §6). The width lists in the wave-1 version of this file
and in MECHANICS were wrong for about 12 opcodes: `0x05/0x09/0x41/0x60/0x83` are
ASCIIZ, `0x1d` is u32, `0x19` and `0x35` are 12 bytes, `0x3c` is a position,
`0x92` is two ASCIIZ, and `0x37`/`0x4b` take a selector.

Tags `0x28`, `0x29` and `0x2f` are read raw by the walker and never go through
the reader; decode them with `tile_payload(payload, tag=…)` (`RAW_LAYOUTS`).

**`in_ECX`** in every handler is `cQuestMgr`, the fixed global **`0x00AACF80`**
(`sdk/engine/addresses.h:19`). It holds the file handles, the named-position
table at `+0x334`, the quest-NPC array at `+0x31c`, the journal at `+0x424`, the
variable table at `+0x7550`, the DlgNPC table at `+0x755c`, and so on
(MECHANICS §1.5 has the offset list).

---

## 1. IF / ELSE / ELSEIF and the other control records (HIGH — why the stream cannot be edited in place)

**There are no stored jump offsets.** Skipping is done by re-walking the stream
forward and summing record sizes. The cursor is the shared byte offset
`*param_2` into the section buffer; it already points past the current record
when its case runs (`:172`).

### `0x3a` IF — walker `:1439-1500`

```
if (FUN_004987b0(record)) continue;          // TRUE -> the next record runs
loop over the following records:             // FALSE -> scan
    copy it; *param_2 += size
    if (tag == 0 || tag > 0x8c) break        // -> "IF mit offenem Ende (%s)" (0094eac4), return 0
    if (tag == 0x42 && FUN_004987b0(it)) continue-after-it
    if (tag == 0x3b) continue-after-it       // the record after ELSE runs
```

### `0x3b` ELSE — walker `:1501-1536`

Reached by execution (the IF branch ran), it **skips exactly the next record**
(`:1524-1529`: `*param_2 = cursor + size(next)`). The log string is `ELSE mit
offenem Ende` (0094ea88).

### `0x42` ELSEIF — walker `:1573-1628`

Reached by execution, it scans forward to the `0x3b` (`:1574-1600`). Then
`LAB_004766f8` (`:2178-2186`) skips exactly one more record, the ELSE body. The
log string is `ELSEIF mit offenem Ende` (0094eaa4). When a failed IF's scan
reaches an ELSEIF, the scan re-evaluates it (the `0x3a` loop above).

### Authoring rules that follow

1. **An ELSE body is exactly one record.** Longer bodies go in a CallFunktion;
   `3b 3e` (ELSE + NOP) is vanilla's "no else".
2. **Every IF chain must end with `0x3b` plus one record.** Otherwise an
   IF-false scan runs off the section and returns 0, which abandons the section.
3. **IF does not nest.**

### Guard records — a FALSE result abandons the whole section

A case that returns 0 (`caseD_0`) makes WorkFunktion `FUN_0046ba90` stop the
section (`FUN_0046ba90:108-145`; `quests/RE_lifecycle_journal.md` §2.2). The
wave-1 version of this file said the following block merely "no-ops"; that was
wrong.

| tag | keyword | walker | test | records (11 blobs) |
|---|---|---|---|---:|
| `0x0a` | QuestDone | `:239-313` | quest registry `DAT_00aabf18` (stride 0x124) flags `+0x120 & 2` must be set | 0 |
| `0x0b` | QuestNotDone | `:314-389` | `& 2` must be clear | 0 |
| `0x0c` | QuestInProgress | `:390-464` | `& 4` must be set | 0 |
| `0x0d` | QuestNotInProgress | `:465-539` | `& 4` must be clear | 0 |
| `0x18` | GroupIsDead | `:657-662` → `FUN_0048c860` | FALSE while any member of the group is alive (`Group (%d) not found. So group is dead.`) | 646 |
| `0x00` / `0x2b` | END* / END_ALT* | `:195-196` | always stop | — |

### Other control records

- **`0x0e` DeleteFunktionBlock** (inline `:540-608`). This was "SET <name>" in
  wave 1.
  - With `01 <section name>` or `0b <section index>`, it sets that section's
    run-once byte `DAT_00aab708 + i*0x54 + 0x50 = 1` and returns 0.
  - With no operand, it returns 2: WorkFunktion latches the *running* section and
    stops, returning low byte 1.
  - 96 records in the 11 blobs, none in base:VAMPIRELADY.
- **`0x3d` ChDlgText** (inline `:1542-1556`) consumes its operand stream and
  returns 1. It is the "change dialog text" keyword, not a scope terminator.
  0 records.
- **`0x3e` NOP** has no walker case (default: continue). Vanilla uses it as the
  one-record ELSE body.
- **`0x6f` SectionEnd*** (inline `:1848-1850`, no keyword) returns 3.
  WorkFunktion then **restarts the section at record 0** after re-running the
  gate (`@0x46bd5c-0x46bd6c` → `@0x46bc21`): a retry loop. 115 records per base
  class, 114 of them not the section's last record.
- **`0x29` / `0x2a`** — the NPC-array skip latch (§3).
- **`0x85` GetPoolPosition** is *not* a condition (the wave-1 list had it). It
  draws pool positions and sets the variable `PoolEmpty`, which the next record
  tests (MECHANICS §2.23).

**Consequence (unchanged, now measured).** Every block boundary is resolved by
walking `tag + size` records from the current cursor, and every section is an
absolute `{start, len}` range in `Vectoren.bin`. Inserting or deleting a byte
shifts everything after it. The project policy:

> "Whole-file replacement at bake time is allowed and is how custom/bin/ works; in-place byte patching of a shipped .bin is not. A baked FunkCode.bin must preserve every Vectoren.bin section byte range, or ship a regenerated Vectoren.bin beside it."

(`quests/README.md` §7, `quests/MECHANICS.md` §1.6, `quests/SDK_GAPS.md` gap 18.)

---

## 2. Tag → handler master table

**Columns.**
- **label** = `funkcode_tags.py` `TAGS` label, i.e. the compiler keyword. A
  trailing `*` means the tag has no keyword and the name is descriptive.
- **walker** = the case line in `FUN_00475680.c` (`WALKER_CASE`), or `default`.
- **FC / SC** = records in base:VAMPIRELADY `FunkCode.bin` / `StartCode.bin`
  (measured 2026-09-11).
- **§** = the MECHANICS entry that documents the mechanic.
- **conf** = HIGH (handler read plus keyword or string), MED (keyword plus
  structure), LOW (keyword only; handler not decoded).

| tag | label | handler | walker | FC | SC | what it does | § | conf |
|---|---|---|---|---:|---:|---|---|---|
| 0x00 | END* | — | :195 | 0 | 0 | stop the section (return 0) | 2.31 | HIGH |
| 0x01 | CreateNPC | FUN_00482510 | :198 | 6118 | 331 | spawn an NPC; op `03` = facing angle, op `05` = on-death hook, op `60` = `See:` hook, extra op `02` = carried item types | 2.14 | HIGH |
| 0x02 | SetObjState | FUN_0048ae90 | :204 | 198 | 6 | object/door state: `06` lock, `07` unlock, `0f` open, `10` close, `41` `Take:` hook | 2.20 | HIGH |
| 0x03 | SetNPCState | FUN_0048bb40 | :210 | 2587 | 66 | NPC state: `09` bind a DlgNPC node, `0a 0` unbind, `37` follow, `39` release, `05` / `60` hooks | 2.10 | HIGH |
| 0x04 | SetBaseTrigger | FUN_004819a0 | :216 | 274 | 466 | register an area-trigger rectangle (`FUN_0063c970`) | 2.20 | HIGH |
| 0x05 | DelBaseTrigger | FUN_004817f0 | :221 | 570 | 0 | remove an area trigger (all sectors, or corner sector + 8 neighbours) | 2.20 | HIGH |
| 0x06 | TalkTo | — | default | 0 | 0 | keyword only; would reach `SelfTriggerQuest<id>` | 2.31 | HIGH |
| 0x07 | EquipNPC | FUN_00481d80 | :228 | 92 | 76 | equip an NPC | — | LOW |
| 0x08 | CreateObj | FUN_00485c10 | :233 | 6779 | 2004 | place an object; `61`/`62` quest item (auto-given without `8e`), `8e` place in world, `41` `Take:`, `83` `Use:` | 2.15 | HIGH |
| 0x09 | StartScript | — | default | 0 | 0 | keyword only | — | HIGH |
| 0x0a | QuestDone | inline | :239 | 0 | 0 | guard: quest flags `& 2` set (§1) | 2.2 | HIGH |
| 0x0b | QuestNotDone | inline | :314 | 0 | 0 | guard: `& 2` clear | 2.2 | HIGH |
| 0x0c | QuestInProgress | inline | :390 | 0 | 0 | guard: `& 4` set | 2.2 | HIGH |
| 0x0d | QuestNotInProgress | inline | :465 | 0 | 0 | guard: `& 4` clear | 2.2 | HIGH |
| 0x0e | DeleteFunktionBlock | inline | :540 | 0 | 0 | latch a section so it never runs again (§1) | 2.31 | HIGH |
| 0x0f | ExitQuest | FUN_0048ee20 | :609 | 607 | 0 | **solve**: fanfare, `QIS_OnExit`, journal `+0x04 = 100` | 2.2 | HIGH |
| 0x10 | MoveOver | — | default | 0 | 0 | keyword only; would reach `SelfTriggerQuest<id>` | 2.31 | HIGH |
| 0x11 | AddExp | FUN_0048d480 | :616 | 102 | 0 | give experience (`addExperience` `FUN_0057e160`) | 2.19 | HIGH |
| 0x12 | AddGold | FUN_0048da40 | :622 | 129 | 0 | give / charge gold (hero `+0x3EE` via `FUN_0057fd80`) | 2.18 | HIGH |
| 0x13 | AddGems | FUN_0048dd10 | :628 | 0 | 0 | give gems (not decoded) | 2.19 | LOW |
| 0x14 | TriggerQuest | FUN_0048f030 | :634 | 1361 | 1 | **enter** a set-up quest: run `QIS_Trigger`, create the journal entry, run `QIS_OnEnter` | 2.2 | HIGH |
| 0x15 | SetUpQuest | FUN_0048f7f0 | :640 | 557 | 137 | **arm** a quest: flags bit 0, run `QIS_OnSetUp` once, then `QIS_Trigger`; no entry | 2.2 | HIGH |
| 0x16 | CallFunktion | FUN_004813d0 | :646 | 1971 | 2 | call a named section (case-insensitive, equal length, first match) | 2.1 | HIGH |
| 0x17 | DefPos | FUN_00478780 | :652 | 1555 | 1440 | declare a named position in `qm+0x334` (stride 0x64; X `+0x44`, Y `+0x48`, R `+0x4c`, Z `+0x50`) | 2.13 | HIGH |
| 0x18 | GroupIsDead | FUN_0048c860 | :657 | 61 | 0 | guard: the monster group is dead (§1) | 2.35 | HIGH |
| 0x19 | CheckForQuestPool | FUN_00490a30 | :662 | 22 | 0 | quest-pool availability | 2.23 | LOW |
| 0x1a | Text | inline | :668 | 4298 | 0 | dialog text line; op `7c` = append; sets the talk signal `creature+0x200 \|= 0x400` | 2.8 | HIGH |
| 0x1f | Dialog | FUN_0048f9e0 | :692 | 1870 | 0 | `1c <u32> 16 <node>` in the DQ `ToDo:` sections | 2.10, 2.27 | MED |
| 0x20 | Belohnungen | FUN_00490be0 | :698 | 1870 | 0 | DQ ToDo reward block | 2.27 | LOW |
| 0x21 | HideTmpToDo | FUN_00490cc0 | :704 | 1870 | 0 | hide a temporary ToDo (pool counter) | 2.27 | MED |
| 0x23 | FillRegion | FUN_0047c610 | :710 | 3 | 0 | populate a region (shares the handler with `0x33`) | 2.30 | MED |
| 0x24 | AddHandel | FUN_0048e4d0 | :717 | 1 | 0 | **no effect in this build** (only resolves the name) | 2.19 | HIGH |
| 0x25 | SetRG | FUN_0048f330 | :723 | 0 | 3 | set a region gate flag | 2.24 | LOW |
| 0x26 | UnsetRG | FUN_0048f330 | :729 | 0 | 0 | clear a region gate flag | 2.24 | LOW |
| 0x27 | Subsys_27* | FUN_004918b0 | :735 | 0 | 661 | StartCode trigger binding: holds the index of section `OMO<quest><trigger>` (100 %) | 2.20, 2.31 | MED |
| 0x28 | DlgNPCDecl* | inline | :740 | 0 | 1022 | DlgNPC declaration: 80 raw bytes into `qm+0x755C` (stride 0x50) — **not** a net queue | 2.10 | HIGH |
| 0x29 | NpcFlagGuard* | inline | :978 | 550 | 0 | raw u16 selector 1..5 tests a quest-NPC flag bit; sets the skip latch (§3) | 2.7 | HIGH |
| 0x2a | NpcFlagGuardEnd* | — | pre-switch | 550 | 0 | closes the `0x29` skip | 2.7 | HIGH |
| 0x2b | END_ALT* | — | :196 | 551 | 0 | stop (return 0) | 2.31 | HIGH |
| 0x2c | NPC_UWR_dispatch* | FUN_00549920 | :1063 | 55 | 0 | builds `NPCUWR_<a><b>` dialog names, clears dialog flag `0x80000` | 2.29 | MED |
| 0x2d | StartPosition | FUN_00491b30 | :1077 | 0 | 1 | set the hero start position | 2.21 | LOW |
| 0x2e | Teleport | FUN_00491d40 | :1083 | 665 | 0 | teleport an NPC or `HERO` to a position | 2.21 | HIGH |
| 0x2f | Subsys_2f* | inline | :1089 | 0 | 0 | raw u32 + ASCIIZ; 0 records | — | MED |
| 0x30 | CreateTrigger | FUN_00496520 | :1389 | 1157 | 1944 | define a named script trigger object (`qm+0x765c`) | 2.20 | HIGH |
| 0x31 | SetTriggerState | FUN_004968a0 | :1394 | 146 | 2 | arm or disarm a trigger object | 2.20 | HIGH |
| 0x32 | TriggerPatch | FUN_00496f20 | :1399 | 1250 | 2464 | bind a trigger object onto ≤ 99 map cells (`FUN_00463f50`) | 2.20 | HIGH |
| 0x33 | FillSector | FUN_0047c610 | :711 | 5309 | 0 | populate a sector (ambient world) | 2.30 | MED |
| 0x34 | SetHP | FUN_0048e280 | :1405 | 82 | 1 | set HP to N % of max | 2.19 | HIGH |
| 0x35 | QuestBook | FUN_00496080 | :1411 | 4502 | 0 | journal line: sub%10 == 0 → title, else append (≤ 10) | 2.3 | HIGH |
| 0x36 | LoseQuest | FUN_0048ee20 | :1417 | 857 | 0 | **fail**: fanfare, `QIS_OnLose`, journal `+0x04 = 101` | 2.2 | HIGH |
| 0x37 | DelNPC/DelOBJ | FUN_00497f80 | :1424 | 317 | 0 | remove a named NPC or object; **never gives** | 2.16 | HIGH |
| 0x38 | SetTimer | FUN_004982d0 | :1429 | 119 | 0 | one-shot game-clock timer → a section | 2.26 | HIGH |
| 0x39 | DelTimer | FUN_004985e0 | :1434 | 80 | 0 | cancel a timer | 2.26 | HIGH |
| 0x3a | IF | FUN_004987b0 | :1439 | 2756 | 2 | condition (§1; predicates in MECHANICS §2.7) | 2.7 | HIGH |
| 0x3b | ELSE | inline | :1501 | 2733 | 2 | skip exactly one record when reached by execution | 2.7 | HIGH |
| 0x3c | **SetButton** | FUN_00499ba0 | :1537 | 3195 | 0 | **dialog answer button** (label `res:`, action = section name); 5 slots written, 4 shown | 2.9 | HIGH |
| 0x3d | ChDlgText | inline | :1542 | 0 | 0 | change dialog text (consumes operands) | — | MED |
| 0x3e | NOP | — | default | 2572 | 2 | nothing; the one-record ELSE body | 2.7 | HIGH |
| 0x3f | **QuestKompassPos** | FUN_0049a4b0 | :1557 | 2056 | 0 | quest compass → position | 2.11 | HIGH |
| 0x40 | **QuestKompassObj** | FUN_0049ac80 | :1562 | 1245 | 0 | quest compass → object; `'off'` clears | 2.12 | HIGH |
| 0x41 | DefNum | FUN_0049b2b0 | :1567 | 0 | 2 | declare a number (shares the handler with `0x43`) | 2.6 | HIGH |
| 0x42 | ELSEIF | inline | :1573 | 5381 | 0 | chained condition | 2.7 | HIGH |
| 0x43 | SetVar | FUN_0049b2b0 | :1568 | 4845 | 387 | set a script variable (`qm+0x7550`, stride 0x24) | 2.6 | HIGH |
| 0x44 | SetVarBit | FUN_0049b840 | :1629 | 1138 | 136 | set a variable bit; name `HeroQBit` → the hero's per-difficulty bitfield instead | 2.5 | HIGH |
| 0x45 | UnsetVarBit | FUN_0049c160 | :1634 | 169 | 0 | clear a variable bit | 2.5 | HIGH |
| 0x46 | SetFocus | FUN_0049daf0 | :1639 | 0 | 0 | camera focus on an entity (cut-scenes) | 2.36 | LOW |
| 0x47 | NPC_TalkTo | FUN_0049dcf0 | :1644 | 4 | 0 | NPC starts a conversation (cut-scenes) | 2.36 | LOW |
| 0x48 | NPC_Goto | FUN_0049e210 | :1649 | 195 | 0 | NPC walks to a position | 2.21 | MED |
| 0x49 | SetGroupState | FUN_0049e760 | :1654 | 99 | 0 | CreateNPC state ops for every group member; `51` = the group vanishes | 2.35 | HIGH |
| 0x4a | GroupGoto | FUN_0049fc50 | :1659 | 18 | 0 | move a group | 2.21 | LOW |
| 0x4b | IncVar | FUN_0049c930 | :1664 | 305 | 0 | var += n (never decreases; creates with n) | 2.6 | HIGH |
| 0x4c | DecVar | FUN_0049cec0 | :1669 | 711 | 0 | var = max(0, var − n) | 2.6 | HIGH |
| 0x4d | UnTriggerQuest | FUN_0048e600 | :1674 | 0 | 0 | delete a journal entry; **never used** (not the vanilla solve) | 2.2 | HIGH |
| 0x4e | FillChest | FUN_004a02a0 | :1680 | 1721 | 0 | fill a chest; op `53` = loot value budget | 2.17 | HIGH |
| 0x4f | Morph | FUN_004a15a0 | :1686 | 44 | 0 | transform an entity (`01 <res> 02 <type>`) | 2.29 | LOW |
| 0x50-0x55 | SetST / SetGS / SetWI / SetRP / SetRM / SetCH | — | default | 0 | 0 | keywords only | — | HIGH |
| 0x56 | SetIcon | FUN_004a1a50 | :1691 | 1804 | 0 | set a DlgNPC marker (`+0x48`); 8/13/126 draw nothing | 2.10 | HIGH |
| 0x57 | SetQuestInfo | FUN_004a6ea0 | :1696 | 27 | 0 | journal page of the **owner quest's** entry (`qm+0x18`) | 2.4 | HIGH |
| 0x58 | SetAnimMode | inline | :1701 | 72 | 0 | kernel event 0x18 | 2.36 | MED |
| 0x59 | SetPlayMode | FUN_004a4040 | :1724 | 76 | 0 | no name = cinema off; with an actor = queue a type-5 action | 2.36 | HIGH |
| 0x5a | Wait | FUN_004a1f20 | :1729 | 16 | 0 | queue a type-2 wait action | 2.36 | HIGH |
| 0x5b | PlayAnim | FUN_004a2550 | :1734 | 48 | 0 | NPC animation (`schmiede` opens the smith window) | 2.36 | HIGH |
| 0x5c | Attack | FUN_004a2b40 | :1739 | 142 | 0 | attack or cast (op `67`); `ECS_TELEPORT` = teleport (direct move if there is no listener) | 2.35 | HIGH |
| 0x5d | ActObj | FUN_004a4310 | :1744 | 1 | 0 | activate an object | — | LOW |
| 0x5e | MouseEvent | FUN_004a6950 | :1749 | 44 | 1267 | register a clickable hotspot → section (op `41`) | 2.29 | HIGH |
| 0x5f | Popup | FUN_004a7760 | :1754 | 272 | 1 | bind a DlgNPC node to the hero and start talking (signposts) | 2.10 | MED |
| 0x60 | ReplaceSpawn | FUN_004a79d0 | :1759 | 351 | 0 | spawn-replacement rule into `qm+0x394` — **not weather** | 2.25 | HIGH |
| 0x61 | SetMapIcon | FUN_004a81f0 | :1764 | 15 | 369 | world-map icon | 2.29 | LOW |
| 0x62 | SecInfo | FUN_004a8390 | :1769 | 0 | 0 | holds `Setup Sector:%d,%d`, `regen`, `gewitter`, `butterfly` | 2.25 | MED |
| 0x63 | Partikel | FUN_004a8bb0 | :1774 | 86 | 0 | particle FX at a position or on a creature (ARGB, fx id) | 2.29 | HIGH |
| 0x64 | SpawnValues | FUN_004a9670 | :1779 | 11498 | 0 | per-sector spawn triple (ambient) | 2.30 | MED |
| 0x67 | SetCV | FUN_0048df30 | :1784 | 91 | 1 | set a client variable | 2.29 | LOW |
| 0x68 | PlaySound | FUN_004a9730 | :1789 | 956 | 0 | play a sound via the compiled-in `getSndType` table | 2.24 | HIGH |
| 0x69 | RndVar | FUN_0049d450 | :1803 | 213 | 0 | var = rand in [lo, hi]; the first `0b` is lo | 2.6 | HIGH |
| 0x6a | SetRgnDialog | FUN_00465280 | :1808 | 0 | 128 | bind a region's ambient dialog node | 2.29 | LOW |
| 0x6b | BalanceSpawnItem | inline | :1813 | 0 | 1 | spawn a balanced item | 2.29 | LOW |
| 0x6c | AddPoolPos | FUN_004790c0 | :1833 | 831 | 1595 | add a pool position | 2.23 | HIGH |
| 0x6d | GetPoolPos | FUN_004793d0 | :1838 | 331 | 0 | take a pool position | 2.23 | HIGH |
| 0x6e | RenPos | FUN_00478d80 | :1843 | 0 | 0 | rename a position | 2.23 | MED |
| 0x6f | SectionEnd* | inline | :1848 | 115 | 0 | restart the section (return 3, §1) | 2.31 | HIGH |
| 0x70 | AddPoolRgn | FUN_0047b480 | :1851 | 88 | 0 | add a pool region | 2.22, 2.23 | MED |
| 0x71 | GetPoolRgn | FUN_0047b770 | :1856 | 110 | 0 | take a pool region | 2.22, 2.23 | MED |
| 0x72 | Waffenpool | inline | :1861 | 0 | 906 | weapon pool | 2.29 | LOW |
| 0x73 | MakeGroup | FUN_004ab940 | :1902 | 22762 | 0 | monster spawn-group table (0xA0-byte blocks) | 2.30 | MED |
| 0x74 | RegionChange | FUN_004abb60 | :1907 | 34 | 0 | region transition (effect UNKNOWN) | 2.21 | LOW |
| 0x75 | ActivateQuest | FUN_0048d930 | :1913 | 10 | 0 | make a quest the tracked journal quest | 2.2 | HIGH |
| 0x76 | SetClientVars | FUN_0048ff10 | :1918 | 1870 | 0 | DQ `ToDo:` sections only (not the "SelfTriggerQuest handler") | 2.27 | MED |
| 0x77 | GetClientVars | FUN_00490500 | :1924 | 110 | 0 | get client variables | 2.27 | LOW |
| 0x78 | GameEnd | FUN_006a1660 | :1930 | 1 | 0 | end the game | 2.29 | LOW |
| 0x79 | AutoSave | FUN_004ac740 | :1938 | 30 | 0 | autosave | 2.29 | MED |
| 0x7a | SolveTime | inline | :1943 | 1870 | 0 | DQ deadline in game minutes (clamped 1440..4320) | 2.26 | HIGH |
| 0x7b | Strafen | FUN_00491090 | :1965 | 1870 | 0 | DQ ToDo penalty block | 2.27 | LOW |
| 0x7c | RegionFrei | inline | :1971 | 24 | 0 | free a region (`0b` region 0..255; calls `FUN_0061d450/0061d540/0061db10`, sound 9/0x35) — **not a sound cue** | 2.25 | MED |
| 0x7d | Gewinn | FUN_004ac940 | :2015 | 439 | 0 | reward roll: (mask, level, tier) → gold, XP, ≤ 1 item | 2.33 | HIGH |
| 0x7e | RejectQuest | inline | :2020 | 2 | 0 | hero's refusal voice (`FUN_00696060`); no quest state | 2.9 | HIGH |
| 0x7f | PlayMusic | FUN_004adaa0 | :2034 | 20 | 0 | music / ambience track | 2.24 | HIGH |
| 0x80 | PlayJingle | FUN_004add60 | :2039 | 11 | 0 | story jingle (`MeetValor` …) | 2.24 | HIGH |
| 0x81 | SetScriptSoundControle | inline | :2044 | 31 | 0 | 1 = the script owns the music, 0 = back to the engine | 2.24 | HIGH |
| 0x82 | Teleporter | inline → cInterpretSQW_Teleporter_004ae040 | :2060 | 26 | 0 | region teleporter with loading screen | 2.21 | MED |
| 0x83 | GameActive | inline | :2065 | 3 | 0 | condition-like reader loop | — | LOW |
| 0x84 | InfoPlayer | FUN_004ae350 | :2095 | 374 | 2 | selector switch: 0/1 chime + on-screen text event (kernel type 0xb), 10 = `witz*.bmp` | 2.34 | HIGH |
| 0x85 | GetPoolPosition | FUN_0047a0c0 | :2100 | 44 | 0 | draw pool positions into named positions; sets `PoolEmpty` | 2.23 | HIGH |
| 0x86 | SetCinemaMode | inline | :2104 | 2 | 0 | cinema on (kernel event 0x13) | 2.36 | HIGH |
| 0x87 | SetOnKill | FUN_004af190 | :2143 | 13 | 0 | kill counter → section (consumer `FUN_004aa630`) | 2.32 | HIGH |
| 0x88 | SetOnCollect | FUN_004af8c0 | :2149 | 17 | 0 | pickup counter → section (consumer `FUN_004aaae0`) | 2.32 | HIGH |
| 0x89 | SetDrop | FUN_004afff0 | :2155 | 36 | 0 | creature → item drop with chance % | 2.32 | HIGH |
| 0x8a | GiveStat | FUN_004b0790 | :2161 | 1 | 0 | grant a stat (effect UNKNOWN) | 2.19 | LOW |
| 0x8b | GiveSkill | FUN_004b0970 | :2166 | 1 | 0 | grant a skill (effect UNKNOWN) | 2.19 | LOW |
| 0x8c | MachVersteck | FUN_004b0c00 | :2171 | 0 | 29 | hide spot (`versteck %d, %d, %d`) | 2.29 | MED |

**Totals** (base:VAMPIRELADY): FunkCode.bin 125,060 records in 102 distinct
tags; StartCode.bin 15,457 records in 34 distinct tags. The corpus-wide histogram over
the 11 FunkCode blobs is in MECHANICS §3.

---

## 3. The `0x29`/`0x2a` skip latch (NPC-array conditional block)

`0x29` NpcFlagGuard* (walker `:978-1062`) reads its payload raw. The payload is
a u16 selector 1..5 at payload byte 1, so pass `tag=` to the disassembler. The
record then:
- sets the global latch `DAT_00ab7898 = 1`;
- selects the quest-NPC entry `*(i16)(creature+0x94)` in the array `qm+0x31c`
  (stride `0x34`);
- tests one bit of that entry's `+0x10`: bits 1/2/4/8/0x10 for selectors 1..5;
  selectors 1 and 2 first repair the entry's object link.

If the test passes, it clears the latch and returns 1, so the block runs.
Otherwise the latch stays set. While it is set, the walker top (`:186-193`)
no-ops every record until a `0x2a` NpcFlagGuardEnd*, which clears it. The block
is delimited by forward walking, with no offsets, just like IF/ELSE. In
base:VAMPIRELADY all 550 pairs sit in `Dialog:` sections
(`quests/RE_dialog_buttons.md` §1.3).

---

## 4. Storyline-relevant tag quick map

| concern | tag(s) | MECHANICS |
|---|---|---|
| arm / enter / solve / fail a quest | **0x15**, **0x14**, **0x0f**, **0x36** | §2.2 |
| call a named section | **0x16** | §2.1 |
| journal text / page | **0x35**, 0x57 | §2.3, §2.4 |
| variables / quest bits | **0x43**, 0x4b, 0x4c, 0x69, **0x44**, 0x45 | §2.5, §2.6 |
| IF / ELSEIF / ELSE / NOP | **0x3a / 0x42 / 0x3b / 0x3e** (+0x29/0x2a) | §2.7 |
| dialog text / buttons / refusal | **0x1a**, **0x3c**, 0x7e | §2.8, §2.9 |
| DlgNPC node, marker, NPC state | 0x28 (StartCode), **0x03**, **0x56**, 0x5f, 0x1f | §2.10 |
| quest compass | **0x3f** (position), **0x40** (object) | §2.11, §2.12 |
| named positions | **0x17** | §2.13 |
| spawn NPC / object / remove | **0x01**, **0x08**, **0x37** | §2.14–§2.16 |
| rewards | **0x7d**, 0x12, 0x11, 0x4e | §2.33, §2.18, §2.19, §2.17 |
| area / script triggers, doors | **0x04**, **0x05**, 0x30, 0x31, 0x32, 0x02 (+ StartCode 0x27) | §2.20 |
| kill / collect objectives | **0x87**, **0x88**, 0x89, **0x18** | §2.32, §2.35 |
| timers / deadlines | **0x38**, 0x39, 0x7a | §2.26 |
| on-screen message | **0x84** | §2.34 |
| movement / teleport / follow | 0x2e, 0x82, 0x48, 0x5c (+ECS_TELEPORT), tag 0x03 ops 0x37/0x39 | §2.21, §2.22 |
| cut-scenes | 0x86, 0x59, 0x5a, 0x5b, 0x46, 0x47 | §2.36 |
| pools, DQ generator | 0x6c, 0x6d, 0x85, 0x70, 0x71; 0x1f, 0x20, 0x21, 0x76, 0x7a, 0x7b | §2.23, §2.27 |
| ambient world (not quests) | 0x73, 0x64, 0x33, 0x23 | §2.30 |

---

## 5. Keyword ↔ tag: the compiler's own tables

The wave-1 version of this section matched keywords to tags by "string
co-residence" in handler bodies. That produced most of the wrong labels: it put
"Questkompass" on `0x3c`, "Teleporter" on `0x7d` and "QIS_Trigger" on
`0x12/0x13/0x34/0x4d`. The exe contains the compiler itself, and its tables
settle every name.

1. **Statement keywords → tag byte.** Three static resolvers each fill a stack
   array of keyword pointers and a parallel array of tag bytes, and return the
   tag for the keyword that matched:
   - `FUN_00452370`, 60 keywords: `return local_f4[local_1ec];` (`:213`);
   - `FUN_00451be0`, 61 keywords: `return local_f8[iVar4];` (`:200`);
   - `FUN_00452910`, 6 keywords: `_DAT_00aab714 = (short)local_500[iVar9];`
     (`:109`). This is also where the unit words of `SolveTime:` / `SetTimer`
     are parsed (`Minute(n)/min/M` ×1, `Stunde(n)/st/S` ×60, `Tag(e)/T` ×1440,
     `Monat(e)` ×43200; `:305-345`).

   Together: 127 keywords on 125 tags, in `funkcode_tags.KEYWORDS`. `DelNPC` and
   `DelOBJ` both map to `0x37`; `Text` and `Text:` both map to `0x1a`.
2. **Condition-operand keywords → predicate opcode.** `FUN_004547b0` covers
   `HasFollow: IsHero: IsNPC: IsDead: IsFighting: IsLocked: IsNotLocked: IsInRgn:
   IsVar: IsVarEq: IsVarBit: IsNotVarBit: IsVarUPR: IsVarLWR: HasMinExp: HasItem:
   IsDay IsDlgNPCLoop IsNight IsDlgOwner:`. `FUN_00454420` covers `IsMultiPlayer
   HasGold: HasQuest: HasNoQuest: IsBronze IsSilber IsGold IsPlatin IsNiob IsGhost
   IsQBitSet: IsQBitNotSet: IsNotMultiplayer WhileVarBit: WhileTimer:`. Opcode
   values: MECHANICS §2.7.
3. **Top-level script structure.** The compiler `FUN_0045a370` handles
   `Funktion:, OnQuestState:, DefDialog:, RegionDef:, InitRegion:, EnterRegion:,
   ExitRegion:, SectorDef:, InitSector:, EnterSector:, ExitSector:, QuestPool:,
   DefQuest:` (`:357-381`).
   - Inside `DefQuest`, the sub-blocks `Trigger / OnEnter / OnExit / OnSetUp /
     OnLose` become sections named `QIS_<Kind><id>`.
   - Closing a `Trigger` block also emits a 9-byte `SelfTriggerQuest<id>` section
     (`:519-588`).

   Section naming is in MECHANICS §2.31.
4. **The inner expression VM** (table `DAT_00964268`, stride 0x38, 24 entries:
   `exit nop ret rsp cmp cmpi rspx jne … sub add mul div mov movi xchg rand
   callRPC`) is unrelated to FunkCode story records. That reading from wave 1
   stands.

The old evidence strings are still correct as **handler contents**; only their
use as names was wrong:

| string | lives in | tag |
|---|---|---|
| `CreateNPC failed: vx=%d, Type=%d` | `FUN_00482510` | `0x01` CreateNPC |
| `CreateOBJ: %s mehrfach …` | `FUN_00485c10` | `0x08` CreateObj |
| `QuestkompassPos failed …` | `FUN_0049a4b0` | `0x3f` QuestKompassPos |
| `QuestkompassOBJ failed …` | `FUN_0049ac80` | `0x40` QuestKompassObj |
| `RES:%d` | `FUN_00499ba0` | `0x3c` SetButton |
| `HeroQBit` | `FUN_0049b840` | `0x44` SetVarBit (the hero-bitfield branch) |
| `cInterpretSQW::Teleporter(hex=%x)` | `cInterpretSQW_Teleporter_004ae040` | `0x82` Teleporter |
| `witz1/2/3.bmp` | `FUN_004ae350` | `0x84` InfoPlayer, selector 10 |
| `Setup Sector:%d,%d`, `regen`, `gewitter` | `FUN_004a8390` | `0x62` SecInfo (0 records) |
| `GetPoolPos from %s to %s` | `FUN_004793d0` family | `0x6d` GetPoolPos |
| `Truhe %s falscht benannt` | `FUN_004a02a0` | `0x4e` FillChest |
| `IF/ELSE/ELSEIF mit offenem Ende` | walker | `0x3a/0x3b/0x42` |

---

## 6. Tooling

- `sdk/re/py/funkcode_tags.py`: `TAGS`, `KEYWORDS`, `WALKER_CASE`, `ALIASES`
  (every pre-2026-09-11 label, so old shard text stays searchable), `label_for`,
  `keyword_for`, `tag_for`. `python funkcode_tags.py` prints the table.
- `sdk/re/py/funkcode_disasm.py`: `GRAMMAR`, `tile_payload(payload, tag=)`,
  `disasm_payload(…, tag=)`, `walk_records`. `--grammar` prints the operand
  table.
- `sdk/re/py/disasm_tiling_check.py`:
  - `--verify-exe` re-derives the reader's jump table;
  - `--verify-tags` re-extracts the three keyword resolvers and the walker
    switch;
  - the default run tiles every record of the 21 distinct blobs
    (1,381,226 / 1,381,228; the 2-line residue is the `chestTrigger` authoring
    defect).
- `sdk/re/py/vectoren.py` and `sdk/re/py/startcode.py`: the section table,
  registry and pool table; positions, DlgNPCs, variables and trigger bindings.
- Frozen on purpose: `sdk/re/py/funkcode_ops.py` keeps the legacy label
  vocabulary, because `sdk/lua_bake_opcodes.inc` is a row-for-row copy of it.
  Do not relabel one without the other.
- The wave-1 scratch tools (`funkcode_walk.py`, `strhunt.py`, `kwtable.py`) embed
  the old names; use the modules above instead.

---

## 7. Open items

- **Handlers still not decoded** (keyword known, effect not):
  - `0x07` EquipNPC, `0x19` CheckForQuestPool, `0x20` Belohnungen, `0x7b`
    Strafen, `0x27` (StartCode binding record body);
  - `0x2d` StartPosition, `0x46` SetFocus, `0x47` NPC_TalkTo, `0x4a` GroupGoto,
    `0x4f` Morph, `0x5d` ActObj, `0x61` SetMapIcon;
  - `0x67` SetCV, `0x6a` SetRgnDialog, `0x6b` BalanceSpawnItem, `0x72`
    Waffenpool, `0x74` RegionChange, `0x77` GetClientVars;
  - `0x78` GameEnd, `0x83` GameActive, `0x8a` GiveStat, `0x8b` GiveSkill;
  - `0x82`'s target `cInterpretSQW_Teleporter_004ae040` has no decompile in
    `decompiled/`.
- **What `0x7c` RegionFrei changes.** The walker case calls
  `FUN_0061d450 / FUN_0061d540 / FUN_0061db10`, which are not identified
  (`quests/GQ_1.md` O4).
- **Runtime probes** for the remaining semantics are listed at the end of each
  wave-2 RE file: InfoPlayer's on-screen event, the base-trigger → section
  dispatcher, the TriggerPatch cell reader, message `0x10B` word 4, IsFighting's
  polarity.
- **Settled since wave 1:**
  - `0x73` = MakeGroup, the monster spawn-group table (MECHANICS §2.30);
  - the `0x29` sub-selector semantics (§3);
  - the `DAT_00aabf18` vs `DAT_00aab708` roles: quest registry vs section table
    (MECHANICS §1.3);
  - the network sub-records: 100-byte positions, 0x174-byte journal entries,
    0x124-byte registry entries (event 0x1ba) are internal structs, not TLV.
