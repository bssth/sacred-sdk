# Native dialog text for runtime NPCs — Path A roadmap (2026-06-13)

> **CORRECTION (2026-06-13, session 2) — read `.claude/knowledge/re/dialog_store.md` first.**
> A deeper disassembly pass OVERTURNS the "per-qm dialogText store" conclusion below:
> - `FUN_006afd20` is just a `memcpy` helper (Ghidra merged symbols) — there is **no
>   per-handle text store to populate**. The "Step 2: populate the store" plan is void.
> - `DlgNPC entry+0x4c` actually wants the **talked-to creature's own handle**
>   (`creature+0xc`), NOT a content/text id (proven at `FUN_00463240:106-107`). Writing a
>   minted content id (0x207D) there showed coherent-but-wrong text because the slot
>   resolves a *creature*, and that creature's text rendered.
> - Displayed dialog text comes from the **creature's `res:NAME` at `creature+0x10`** →
>   `FUN_006726f0`/`FUN_0080e780` → `sacred_hash(name)` → **global.res** (which the SDK
>   bakes correctly). So global.res WAS the right store; the earlier "global.res is not
>   the node-text store" claim is WRONG.
> - **Fix = the baker route (Recipe B):** ensure the NPC has a `res:NAME` (creature+0x10)
>   baked in global.res, then emit `[03][size][00][01]"res:CAPMILES_GREET"\0[btn]` and
>   dispatch on talk via `FUN_00475680`→`FUN_0048bb40`→`FUN_00461540`. One read-only live
>   BP on `FUN_0080e780` remains to confirm name-vs-hash feed. Parked behind the SDK
>   foundation refactor (see `.claude/plans/MASTER_PLAN.md`).


The talk BLOCKER is solved (`o:on_talk`, cCreature+0x200 bit 0x400; see
`talk-signal-0x200` memory). Remaining: make a runtime-spawned NPC show the
RIGHT dialog text the NATIVE way (user chose Path A, not a custom overlay).

## Proven mechanism (exhaustively, do NOT re-probe)

Dialog text is **NOT in any per-NPC field we can write.** Live probes (DLL
`bc69043b` and predecessors) established:
- `say()`/`dialog_arm` writes `sacred_hash(key)|0x80000000` to the DlgNPC
  entry `+0x4c` AND `cre+0xc`. Result: random text. **`cre+0xc` on a vanilla
  dialog NPC = its own handle (small, e.g. 0x2A0=672), NOT a text hash** — so
  our write CORRUPTS it.
- The dialog-content setter `FUN_00465220` (called by the Dialog handler
  `FUN_0048f9e0`) **never fires on talk** — not for ambient NPCs, not for a
  quest-giver, not on a fresh quest. It runs at script-DISPATCH time.
- Talking is HERO-centric: `FUN_0056B130`/`FUN_00461540` fire on the hero
  (h=1, objIdx=0). The talked-to NPC's own DlgNPC entry is not the text source.
- The bake is CORRECT (`check_dlgtext.py`: CAPMILES_GREET/ROCHEFORD_GREET are
  in global.res at the right `sacred_hash` idents, index sorted).

**Conclusion:** vanilla dialog text comes from **compiled `Dialog:` (tag 0x1f)
script bytecode**, keyed by NPC/dialog name, referencing a `res:` resource,
dispatched by the record walker `FUN_00475680` → `FUN_0048f9e0` when the quest
script runs. There is no shortcut field. Native text REQUIRES producing +
dispatching a Dialog record.

## Assets we already have

- **FunkCode baker**: `custom/lua/bin/<class>/FunkCode.lua` → `FunkCode.bin`
  (`lua_bake.cpp`; `v.load` returns vanilla records, we can append). Name-keyed
  dialog/trigger records are appendable (HANDOFF: "undisputed"); only mid-stream
  CreateNPC append breaks jumps.
- **Decompiled vanilla** Dialog records as byte templates: `quest_scripts.md`,
  `triggers_dialog_move.md` §B, the 22 MB `_vanilla/bin/TYPE_NPC_VAMPIRELADY/
  FunkCode.lua`. Tag 0x1f payload (from §B / `FUN_00472bc0` field ids):
  - field **1** (ASCIIZ) = dialog NPC name / `res:` key
  - field **0xb** (i32)  = target by numeric id
  - field **0x16**       = dialog content/handle (from param_2+0xc)
- **on_talk** (general talk signal) to know WHEN to dispatch.
- **`fire(name)`** + the record-walker hook `FUN_00475680` + `0xAAB708` trigger
  table (layout known: name[0x40] + 5 dwords; entry → (offset,len) into the
  compiled script blob).

## ROOT CAUSE — fully resolved by static RE (2026-06-13, session 2)

The "random text" is now **definitively explained** end-to-end. Read this before
touching code; it overturns the earlier `sacred_hash|0x80000000` theory.

### The renderer-side resolver `FUN_006726f0(handle)` has TWO modes
- `handle & 0x80000000` set → `FUN_0080eaf0(handle & 0x7fffffff)` = **direct
  global.res lookup, key = the low 31 bits used AS the sacred_hash**.
- high bit clear → `FUN_0080e780(handle)` = convert id→string→sacred_hash→
  global.res (`FUN_0080e780` hashes with MUL=0x71, MOD=0x3b9ac9f7 = our exact
  `sacred_hash`; confirmed identical incl. the signed-mod dance).

So IF `entry+0x4c` were passed through `FUN_006726f0`, our current write
`sacred_hash(name)|0x80000000` (player_state.cpp:1237) WOULD resolve to our
global.res text. **It still shows random text → `entry+0x4c` is NOT routed
through `FUN_006726f0`.** This is the key negative result; it kills every
global.res-only fix.

### What `entry+0x4c` actually is
A **small per-script content handle** (vanilla values ~0x13F/319, and the
allocator hands out ids in **0x2076..0x20cf**). The dialog renderer resolves it
through the per-script content/dialog-text machinery, NOT global.res:
- **Content table** `qm+0x765c`(begin)/`qm+0x7660`(end) **stride 0x48**,
  name-keyed, handle stored at `entry+0x44`. Resolver/allocator =
  **`FUN_00465070(ECX=qm, char* name, char alloc)`** (118 L, clean): scans by
  name → returns `entry+0x44`; if `alloc!=0` and miss, allocates a new id in
  0x2076..0x20cf, registers name→id (`FUN_0066ef40`+`FUN_00464760`), returns it.
- **DlgNPC array** `qm+0x755c`/`+0x7560` stride 0x50, `entry+0x4c` = the active
  content handle, `entry+0x14`-owner bit 0x80000 = "showing". Setter =
  `FUN_00465220(idx, handle)` (writes `entry+0x4c`, then `FUN_0045ee20(3,0,0)`).
- **Runtime dialog-text store**: `FUN_00465690` (the quest **save/load
  serializer**; "dialogText"/"openQuests" are SAVE SECTION names, not files —
  on disk `scripts/<lang>/` has only global.res + SRglbl.res) loads the text
  blob into `qm+0x79f4` and a growable vector at `qm+0x99f4`/`+0x99f8`
  (`FUN_006afd20`). The small content handle indexes THIS store.

### tag 0x03 handler = `FUN_0048bb40` (the dialog-by-name processor)
Decompiled: loops `FUN_00472bc0` reading fields into `qm+0xa460`(str)/`+0xa860`
(pos)/`+0xa880`(dword); **case 1** sets content `local_160 = *(qm+0xa880)`, and
**if 0/-1 calls `FUN_00465070()` to resolve the name→handle**; **case 9/10**
locate the DlgNPC entry by name/id (else logs the German
`s_Dialog___s__nicht_oder_nocht_nic_0094e26c` "Dialog (%s) not present");
at END calls **`FUN_00461540`** (1233 L) = the apply/show. Note funkcode_tags
calls tag 0x03 "DialogShow_v1" (from its embedded "Dialog (%s) nicht vorhanden"
string) while the keyword table calls it SetNPCState — same VA 0x0048bb40; the
dialog-string evidence wins. (tag 0x1f=`FUN_0048f9e0` is the v2 variant.)

### Consequence for a runtime-spawned NPC
Our NPC has no content-table/dialogText entry, so no global.res trick can feed
the renderer. Native text REQUIRES: (1) `h = FUN_00465070(qm,name,1)` to mint a
handle, (2) **populate the per-qm dialogText store with our text under `h`**
(THE remaining unknown — which fn writes `qm+0x99f4`; needs a live BP), (3)
`FUN_00465220(idx,h)` + owner `+0x14|=0x80000` + `FUN_005498f0(idx,1)`.

### Next concrete step (Path A, step 2′ — supersedes the old step 2)
**Live probe, read-only**, to nail the store-populator + confirm the dispatch:
hook `FUN_00465070` (log ECX=qm, name, alloc, ret) and `FUN_0048bb40` entry;
then (a) play a VANILLA scripted dialog and (b) talk to our NPC, compare. This
tells us whether either fires on our NPC's talk, the real handle values, and —
by BP'ing the store writer around `FUN_006afd20`/`FUN_00461540` — the exact
text-insert call. THEN implement (1)-(3). The emit-a-tag-0x03-record-via-baker
plan (old steps 2-3 below) is the fallback if direct store-population is opaque.

## Roadmap

### Step 1 — pin the EXACT Dialog record bytes — DONE (2026-06-13)
Used `dump_dialog_records.py` (proper record walker, not a byte scan) on
`bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin`. Findings:
- **The dialog TEXT lives in a `tag=0x03` (DialogShow_v1) record**, NOT tag 0x1f.
  tag 0x1f (DialogShow_v2, 1870×) is just a BlockMarker (opcode 0x16 = dialog
  block name, e.g. 'DQ1_BRINGE_NPC'). tag 0x56 (DialogShow_v4) references the
  block name. The full dialog block = cluster {0x1f marker, 0x03 text, 0x56}.
- **MINIMAL text-record template** (smallest real one, @0x0210D6, 16 bytes):
  ```
  03  00 10  00  01  "res:17699"\0  39
  └tag └size └fl └DLG_OP_a(0x01) └res text   └trailing
  ```
  i.e. `[tag=0x03][size:u16 BE][flags=0x00][0x01 DLG_OP_a]["res:<NAME>"\0][btn]`.
  The `0x01` opcode (DLG_OP_a, "cstr2" — 2 cstrings) carries the `res:` text key
  + a 2nd cstring (button/voice; vanilla uses a single byte like '9'/'k').
- Vanilla uses numeric `res:17699`; WE emit `res:CAPMILES_GREET` — resolves via
  the SAME global.res `sacred_hash` mechanism we already bake correctly.
- Record-tag counts (full file): 0x01 CreateNPC ×6118, 0x1a QuestTrigger ×4298,
  0x03 DialogShow_v1 ×2587, 0x1f DialogShow_v2 ×1870, 0x56 ×1804, 0x5f ×272.

So the emit target is a `tag=0x03` record: `00 01 "res:CAPMILES_GREET" 00 <btn>`.
Tools: `funkcode_disasm.py` (`walk_records`/`disasm_payload`),
`funkcode_compile.py` (likely emits records), `funkcode_roundtrip_test.py`.

### Step 2 — emit our Dialog record via the baker
In `FunkCode.lua`, append (name-keyed, NOT mid-stream) a Dialog record bound to
our NPC name ("Captain Miles") referencing `res:CAPMILES_GREET`. Confirm the
bake produces a clean TLV (roundtrip via `funkcode_roundtrip_test.py`). Verify
the engine LOADS it (no fade-to-black / intro break).

### Step 3 — get it DISPATCHED on talk
Two options, try the cheaper first:
- (a) Register the record's trigger NAME in `0xAAB708` (we can write the table;
  layout known) pointing at the baked bytecode offset, then `fire()` it from
  `on_talk`. Needs the record's bytes appended to the in-memory script blob
  `[0xAACF4C]` at a known offset + the (offset,len) in the 0xAAB708 entry.
- (b) Call the record walker `FUN_00475680` directly from the DLL on `on_talk`,
  passing our Dialog record bytes + the NPC as param_2 (so `FUN_0048f9e0`
  resolves our NPC by name and shows `res:CAPMILES_GREET`). __thiscall ECX=qm,
  6 stack args, ret 0x18 (HANDOFF §14). This avoids touching the script blob —
  likely the cleaner path. Red-team the ABI before calling (param_4 dyn-cast to
  cCreature*; param_4=0 → no-op, so supply our NPC).

### Step 4 — wire + verify
`captain:on_talk(fn)` → dispatch the Dialog record → native window shows
"Halt, stranger…". Then advance the quest (already works). Same for Rocheford.

## Open questions for step 3
- Does `FUN_0048f9e0` resolve the text `res:` itself, or expect a pre-resolved
  content handle at param_2+0xc? (§B says field 0x16 = content from param_2+0xc;
  open item #4.) If it self-resolves from the record's field-1 `res:` name, (b)
  is clean. If it needs a pre-resolved handle, first resolve `res:CAPMILES_GREET`
  → handle via the resource path (`FUN_006725e0`/`FUN_006726f0`), which is also
  how CreateNPC resolves `res:CAPMILES_NAME` (that already works → reuse it).
- `FUN_00475680` exact arg convention for a single injected record (capture by
  BP'ing it once on a vanilla Dialog dispatch, or model from its decompile).

## Status
Probing phase DONE — mechanism proven. Next = implementation (steps 1-3).
Diagnostics added this round (keep, read-only): `sacred.trigger_table_dump`,
`sacred.npc_in_dialog`, dlg56b entry+0x4c dump, `[dlgcontent]` FUN_00465220
probe. The captain scene + Q1 talk-advance work end-to-end already.
