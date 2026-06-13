# Quest-journal render gate — 2026-05-15

## TL;DR

**Journal list builder: `FUN_006b07e0` @ `0x006b07e0`** (confidence **HIGH**).
It walks the whole registry `DAT_00aad3a4..DAT_00aad3a8` (stride 0x174)
and renders an entry into the quest-log UI **only if BOTH**:

1. `*(u32*)(entry+0x24) != 0`  — log-text slot 0 must be a resolved
   non-zero text ref (written by tag-0x35 `FUN_00496080`).
2. `*(u16*)(entry+0x16C) == page+1`  (or, on page 0, `== 0`) — the
   journal category/page id, written by tag-0x57 `FUN_004a6ea0`.

Our `resize()+stamp(quest_id@+8)` entry is zero everywhere except +8,
so **+0x24 == 0 → the builder skips it on every page**. That is the
whole reason 9512 never renders. The per-class table is **not**
consulted by the journal builder (see below).

## The gate, verbatim (FUN_006b07e0, decompiled lines 95–98)

```c
local_c8 = 0;                              // byte offset = idx*0x174
do {
  if ((*(int *)(iVar13 + 0x24 + local_c8) != 0) &&          // (1) +0x24 != 0
     ((uVar11 = *(ushort *)(iVar13 + 0x16c + local_c8),       // (2) +0x16C u16
        (uint)uVar11 == iVar8 + 1U ||                          //     == page+1
       ((iVar8 == 0 && (uVar11 == 0)))))) {                    //  or page0 && ==0
     ... build one quest-log row for this entry ...
  }
  local_c8 += 0x174;
} while (local_dc < (DAT_00aad3a8 - DAT_00aad3a4)/0x174);
```

Outer loop `local_c0/iVar8` = 0..6 → 7 journal pages/categories.
A vanilla entry passes because tag-0x35 set +0x24 (resolved string
ptr `*(cQuestMgr+0xa880)`, always non-zero on a real QuestLogSet) and
tag-0x57 set +0x16C to the page index (`*(cQuestMgr+0xa860)`+1, or 0).

Other per-entry fields the builder *reads but does not gate on*:
`+0x00` (==3 → "completed" styling), `+0x04` (type: 1/2/100/0x65 →
icon/colour), `+0x0C` bit0 (highlight), `+0x28..+0x4C` (extra log
slots), `+0x16C` low/high also used as sub-id. None of these block
display; only +0x24 and +0x16C do.

## What our entry is missing vs a vanilla entry

| Field | Vanilla | Our resize()+stamp | Effect |
|---|---|---|---|
| +0x08 quest_id | set | **set (9512)** | scan key OK |
| +0x24 logtext0 | non-0 (tag-0x35) | **0** | **builder skips (gate 1)** |
| +0x16C page id  | set (tag-0x57) | **0** | only shows on page 0 even if +0x24 fixed (gate 2) |
| +0x04 type      | 1/2/100/0x65 | 0 | row would render but with default/no icon |
| +0x10 state     | 0xeeeeeeee/-1/coords | 0 | only matters to kompass FUN_004a5980, not the list |

You already emit tag-0x35 for 9512 — but `FUN_00496080` writes +0x24
**only after** its own `for(... entry+8 != quest_id ...)` scan finds
the entry, AND only when `local_a0 % 10 == 0` (record sub-field 4 ==
the "slot 0" case) AND `unaff_EBX (*(cQuestMgr+0xa880))` is non-zero.
If your QuestCode.bin tag-0x35 record doesn't carry a valid resolved
string in field 0xa880 / lands on a non-zero sub_idx, +0x24 stays 0.
Verify the runtime value of `entry+0x24` for 9512 with F8 dump — that
single dword tells you whether tag-0x35 actually landed.

## Per-class table — NOT a journal gate

`[cQuestMgr+0x3a4 + (class-1)*8]` / `[+0x3a0 + class*8]` (== globals
`DAT_00aad31c` / `DAT_00aad320`) are written by tag-0x75
`FUN_0048d930` with the entry **index**. They are consumed **only by
the kompass/marker resolver `FUN_004a5980`** (the on-screen compass
arrow + minimap dot), not by the text-list builder `FUN_006b07e0`.
So class registration affects the COMPASS MARKER, not journal text
visibility. (Confidence HIGH — FUN_006b07e0 never reads +0x3a0/+0x3a4
nor DAT_00aad31c/320; FUN_004a5980 does, and it's a coordinate→screen
projector, returns 1 only after computing marker XY.)

## SDK fix recipe

After `resize()` + `entry+8 = quest_id`, the entry must additionally
receive the two gate fields. Cleanest = let the engine's own mutators
fill them by ensuring 9512 has BOTH a tag-0x35 (QuestLogSet) **and** a
tag-0x57 (Subsys_57 / page) record that resolve, since those scan by
+8 and will now find the entry. If you must stamp directly:

```c
uint8_t* e = *(uint8_t**)0x00aad3a4 + idx*0x174;   // your slot
*(uint32_t*)(e + 0x08) = 9512;                     // already done
*(uint32_t*)(e + 0x24) = <resolved text ref>;      // GATE 1: must be != 0
*(uint16_t*)(e + 0x16C) = page_id;                 // GATE 2: 0 = page0,
                                                   //   else page = id-1
*(uint32_t*)(e + 0x04) = 1;                        // type: 1 = normal
                                                   //   quest (icon)
*(uint32_t*)(e + 0x10) = 0;                        // no kompass marker
```

- **+0x24** is the only hard blocker. It must be the same kind of
  value tag-0x35 writes: `*(cQuestMgr+0xa880)` — a resolved text/handle
  for the quest-log line. Easiest correct source: drive it via a
  working tag-0x35 record (Option-B-style) rather than synthesising
  the handle. Stamping a bogus non-zero there will render a row but
  likely crash/garble when the row text is dereferenced. **Confidence
  HIGH it's the gate; MEDIUM on the exact handle type — needs a
  runtime read of a vanilla entry's +0x24 to confirm format.**
- **+0x16C** = page. To appear on the first journal tab, leave it 0;
  for tab N set it `N+1`. Confidence HIGH (direct read of the
  comparison + the tag-0x57 writer).
- No call needed for visibility. `FUN_0048d930` (tag-0x75,
  __thiscall, ECX=cQuestMgr=0x00AACF80, 1 stream arg) is only needed
  if you also want the **compass marker** for 9512.

## Confidence summary

| Claim | Confidence |
|---|---|
| FUN_006b07e0 @0x006b07e0 is the journal list builder | **HIGH** — only fn that loops whole registry building 7 UI page lists |
| Gate = (+0x24 != 0) AND (+0x16C == page+1 \|\| (page0 && +0x16C==0)) | **HIGH** — direct decompile, lines 95–98 |
| +0x24 written by tag-0x35 FUN_00496080 | **HIGH** — decompile lines 102–118 |
| +0x16C written by tag-0x57 FUN_004a6ea0 | **HIGH** — decompile line 60 |
| Per-class table is kompass-only, not a journal gate | **HIGH** — FUN_006b07e0 never touches +0x3a0/3a4; FUN_004a5980 does |
| Exact byte-format of the +0x24 text handle | **MEDIUM** — confirm via runtime read of a vanilla entry's +0x24 |

## One runtime experiment to close the loop

F8-dump `entry+0x24` and `entry+0x16C` for quest_id 9512 vs a vanilla
entry (id 1 or 9) after world load. If 9512's +0x24 is still 0, your
tag-0x35 record isn't resolving (check sub_idx %10 and the 0xa880
field); if +0x24 is non-0 but it still doesn't show, the only
remaining gate is +0x16C page mismatch — set/scan tabs accordingly.

## Files

- `.claude/knowledge/re/scan_registry_refs.py` — byte-scanner for refs to
  DAT_00aad3a4/3a8/cQuestMgr (35/14/208 hits)
- `re/ghidra/decompiled/006b07e0_FUN_006b07e0.c` — **the journal builder**
- `re/ghidra/decompiled/004a5980_FUN_004a5980.c` — kompass/marker resolver (per-class table consumer)
- `re/ghidra/decompiled/00496080_FUN_00496080.c` — tag-0x35, writes +0x24
- `re/ghidra/decompiled/004a6ea0_FUN_004a6ea0.c` — tag-0x57, writes +0x16C
- `re/ghidra/decompiled/0048d930_FUN_0048d930.c` — tag-0x75, writes per-class slot (kompass only)

> Note: the SDK `run_headless.bat process-decrypted ...` did NOT work
> when launched from PowerShell (it fell into the `import` branch and
> re-imported the encrypted Sacred.exe). Worked by calling
> `D:\ghidra_12.1_PUBLIC\support\analyzeHeadless.bat` directly with
> `<projdir> sacred_decrypted -process Sacred_decrypted.exe
> -postScript DecompileFunc.java <VAs> -noanalysis`. Worth fixing the
> .bat's arg handling for future runs.
