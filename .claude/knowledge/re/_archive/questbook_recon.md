# Quest-book registry recon — 2026-05-15

## TL;DR
The "quest registry" is a **runtime in-memory array** held at offset
`+0x424` of a context object (presumably `cQuestManager` or
`cInGameContext`). Each entry is **0x174 bytes (372 B)** wide. The first
field at `entry+8` is the **quest_id**. Six FunkCode handlers (incl.
the tag-0x35 log-write) **mutate** entries, none of them **insert** —
that's why new quest_ids silently no-op.

## The structure — verified

VA `0x00496080` is the tag-0x35 (`QuestLogSet`) handler called by
walker `0x00475680`. Decompile says:

```c
size = ([ECX+0x428] - [ECX+0x424]) / 0x174;        // entry count
ptr  = [ECX+0x424];                                 // first entry
for (i = 0; *(int*)(ptr + i*0x174 + 8) != quest_id; i++)
    if (i >= size) RETURN_WITHOUT_WRITING;          // <-- the gate
// only here do log-text fields get written
```

Six handlers share the exact same scan pattern (stride 0x174, key at
`entry+8`):

| VA | Tag | Action on found entry |
|---|---|---|
| `0x00496080` | 0x35 QuestLogSet | write log-text refs at `+0x24..+0x4C` |
| `0x004a6ea0` | 0x57 Subsys_57   | write `+0x16C` |
| `0x0049ac80` | 0x40 QuestKompassOBJ | write `+0x10/+0x14/+0x20` |
| `0x0049a4b0` | 0x3f QuestKompassPos | write `+0x10/+0x14/+0x20` |
| `0x0048d930` | 0x75 Subsys_75   | write per-class slot at `[ECX+0x3a4 + class_idx*8]` |
| `0x0048e600` | 0x4d Subsys_4d   | **delete** entry (last-over-target + decrement `[ECX+0x428]`) |

So the contract is: **the array is a pre-populated table; FunkCode
opcodes mutate or delete; nothing in FunkCode appends.**

Entry layout inferred from the writes (partial):
```
+0x000  type/header (4 B), then short at +0x6
+0x008  u32 quest_id          ← scan key
+0x010  u32 ?                 ← `0xeeeeeeee` placeholder, also state
+0x014  u32 ?
+0x018  u32 ?                 ← read by 0x40 handler
+0x020  u32 ?                 ← position/marker
+0x024  u32 log slot 0  (when sub_idx % 10 == 0)
+0x028..0x4C  10 × u32 log slots (sub_idx mod 10)
+0x16C  u32 (written by tag 0x57)
+0x174  next entry
```
Class-restriction lookup at `[ECX+0x3a4 + class*8]` and
`[ECX+0x3a8 + class*8]` confirms section I-pre2 of community-refs:
the registry **is** class-gated. `if (quest_id < 100)` branch in
FUN_0048d930 means quest_ids < 100 are special (UI / non-class).

## Where entries are CREATED — not yet found

Confidence: **medium**. Not in any of the 6 scanned handlers, not in
DQ_QuestSetup (tag 0x77 at `0x00490500` — that one operates on a
DIFFERENT array at `[ECX+0x31c]` stride 0x34, which is the daily-quest
pool, not the main registry).

Three live hypotheses:

1. **(Best)** A FunkCode tag we haven't decompiled yet — most likely
   one of `0x1a` `QuestTrigger`, `0x17` `QuestStateA` (FUN_00478780),
   `0x6c` `QuestStateB`, or `0x70` `QuestStateC`. Of these,
   QuestStateA is the prime candidate because the existing docs link
   it to the `0x416780/0x4166f0` set-state chain that 0x35 also calls
   when a record is being broadcast over network.
2. Boot-time read of a binary — but `bin\TYPE_NPC_*\QuestCode.bin` is
   only 40-53 B, way too small. No external "quest registry" file
   exists. Reject this path.
3. Hardcoded static table in `.rdata` copied during cQuestManager
   ctor. Possible but no string evidence found.

Community sources (Resacred-master, Inoff Patch, SacredGameTools,
SacredModloader, Cheat Engine table) **do not name this structure**.
Only the IDA databases (`Sacred_2.idb` / `Sacred_3.idb` in
Resacred-master) might have a label, but those need IDA Pro to open.

## Cross-references

- `FUN_007d84a0` returns active-hero context (`+0x14 = class_idx`),
  used at runtime to pick the per-class slot in the
  `[ECX+0x3a4 + class*8]` / `[ECX+0x3a8 + class*8]` arrays.
- The DAT_0182ebec gate in every handler is "is this a real game
  session, not editor". When 0, only the array-mutate happens; when 1,
  network event 0x1bd / 0x1c7 also fires (for save-sync).
- The walker `0x00475680` is __thiscall — ECX (the cQuestManager
  pointer) flows from its caller `0x0046f9b0` (the FunkCode.bin loop)
  unchanged through every dispatched handler.

## Confidence summary

| Claim | Confidence |
|---|---|
| Registry is at `[cQuestManager+0x424..0x428]`, stride 0x174 | **high** — 6 independent handlers do the exact same arithmetic |
| Scan key is `entry+8` u32 quest_id | **high** — all 6 handlers compare to this offset |
| New quest_ids silently fail because `for-loop` exits | **high** — direct read of FUN_00496080 |
| Class-gating via `[ECX+0x3a4 + class*8]` | **high** — visible in FUN_0048d930 + FUN_0049ac80 |
| Entries are NOT created from FunkCode opcodes we've decoded | **medium-high** — we've checked 6 of the 100+ handlers |
| Entry creation lives in `0x17` QuestStateA (FUN_00478780) | **educated guess** |
| No external `.bin` file holds the registry | **high** — verified by file sizes |

## Recommended next step (one experiment)

Install an x64dbg / runtime hardware-write breakpoint on
`[ECX+0x428]` (the array-end pointer) right after game world load.
Concretely:

1. Find the cQuestManager singleton at runtime: hook `FUN_00475680`
   entry, log ECX once, store it.
2. From SDK Lua: read `*(u32*)(saved_ecx + 0x428)` — that's the
   end-of-array pointer.
3. Set hardware-write BP on those 4 bytes. Whatever code increments
   that pointer **is the entry inserter**. Most likely it'll resolve
   to one of the QuestState* handlers above, plus a routine called
   during world boot.
4. Once the inserter VA is known, the runtime patch is trivial: call
   it directly from the SDK with our own quest_ids before any tag-0x35
   record fires for them.

Backup plan if the BP doesn't trigger during normal play (because
entries are all created at boot before our hook installs): hook
later in the ctor chain — search for `new[]` allocations with a
multiple of `0x174` as the size argument near cQuestManager init,
which is reachable via xref of any of the 6 handler VAs above.

## Files referenced

- `E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\re\ghidra\decompiled\00496080_FUN_00496080.c`  (tag 0x35 — the smoking gun)
- `E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\re\ghidra\decompiled\0048d930_FUN_0048d930.c`  (class-gating proof)
- `E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\re\ghidra\decompiled\00475680_FUN_00475680.c`  (walker dispatch)
- `E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\re\ghidra\decompiled\00490500_FUN_00490500.c`  (daily-quest pool — different structure, ruled out)
- `E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\docs\community-refs.md`  (section I-pre2 — unsolved problem statement)
