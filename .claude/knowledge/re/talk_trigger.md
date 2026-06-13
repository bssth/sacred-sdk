# Talk-trigger mechanism — DECODED (2026-06-10, static RE)

**This is the missing piece for HANDOFF §5 "THE BLOCKER".** Decoded statically
from `Sacred_decrypted.exe` (capstone scans in this folder: `talk_fmt_xref.py`,
`talk_handler_dis.py`, `talk_classify.py`, `talk_jt.py`, `trigger_container.py`).
NOT yet live-probed — that's the next build (see "Next" below). But it is a
genuinely NEW path: none of the DEAD-PATHS catalog functions are involved.

## The chain (how a vanilla NPC's talk fires a quest)

1. **Per-tick creature dialog state machine = `FUN_0052AB70`.**
   - `__thiscall`, ECX = cCreature. Reached through the creature update vtable
     from `FUN_005375C6` (per-tick think; calls ms-timers `0x3e8`/`0x7d0`) at
     call site `0x00539976`. No direct callers of `0x5375C6` → it's a vtable
     slot, i.e. runs every tick for EVERY creature, ours included.
   - Dispatches on the **sub-state byte word `[cre+0x152]`** (range 0..6) via
     jump table `@0x0052C9F8`:
     | state | target | meaning |
     |---|---|---|
     | 0 | 0x52ABC9 | idle/init |
     | 1 | 0x52BED1 | |
     | 2 | 0x52C14F | |
     | 3 | 0x52C63B | |
     | 4 | 0x52C9DE | (ret/none) |
     | 5 | 0x52C758 | |
     | **6** | **0x52C29C** | **ACTIVE CONVERSATION — the talk-fire case** |
   - The major state lives at `[cre+0x150]` (set to `6` when a conversation
     opens, see `0x52C2CA: mov word [ebp+0x150], 6`). Dialog id at `[cre+0x15c]`.

2. **Case 6 (`0x52C29C` …) builds the talk key and operates the trigger map.**
   - `0x52C30D call 0x44A1C0` → the creature **refKey** (this is the SAME
     `FUN_0044A1C0` that yields Captain Miles' stable `F0F8698A` — HANDOFF §14).
   - `0x52C34C call 0x44A200` → the creature **display-name** C-string.
   - `0x52C359 push "Talk_%s_Dlg_%d"` + `sprintf (0x849AE1)` →
     **`Talk_<NpcName>_Dlg_<dlgId>`** (dlgId from `[cre+0x15c]`).
   - `0x52C364 mov eax,[0xAB44AC]` then `ecx=0xAB44AC` + `call 0x4C5DB0`
     (and siblings `0x4BFA20`, `0x4C61A0`): operate the **global named-trigger
     container at `0xAB44AC`** with the talk key. `0x4C5DB0` = find/insert a
     node {string key @+4, int-vector @+0x84}; the vector holds the quest
     record indices to fire for that name.

3. **The same container `0xAB44AC` is ALSO read by the quest/region drivers**
   `FUN_0055DD0B`, `FUN_00593F42`, `FUN_00597F41`, `FUN_007E7ADE` (all do
   `ecx=0xAB44AC; call 0x4C5DB0` then walk the returned int-vector and fire
   each via `[0xAAB708]`-indexed handlers). So `0xAB44AC` is THE name→quest
   dispatch table. `Talk_*`, `SelfTriggerQuest%d` (`0x52C302`), region/sector
   triggers all key into it.

## Why our runtime-spawned NPC's talk produces no catchable signal

The container `0xAB44AC` is populated **only at quest-script load time** by the
quest-script parser `FUN_0045A370` (the `.\Scripts\%s` / `DefQuest:` / `Talk` /
`Dialog:` text parser; sole caller `0x45C0B6`; writes `sgq.bin`/`sgqp.bin`
caches at `0x469B50`/`0x469C3E`). A runtime CreateNPC spawn never registers a
`Talk_<our name>_Dlg_<id>` key → case-6's lookup misses → nothing fires. This
matches HANDOFF §6's theory ("name-keyed binding the engine can route to" is
absent for runtime spawns) and now PINS the exact table + key format.

## Why the earlier `[dlgpoll]` saw "no change" (false negative)

The dead poll (HANDOFF §6) watched `+0x14 / +0x245 / +0xc / +0x204`. The
conversation state machine lives at **`+0x150` (major=6), `+0x152` (sub 0..6),
`+0x15c` (dlg id)** — none of which the poll watched. So "creature doesn't
change on talk" was measuring the wrong fields. These three are the right
watch targets.

## Two concrete unblock paths (next session)

**A. Observe + fire (preferred, low-risk).** Hook `FUN_0052AB70` case-6 at the
point the key is formatted (`~0x52C359`, after the `sprintf`), read the
`Talk_<name>_Dlg_<id>` buffer; if `<name>` matches a bound SDK NPC, call the
proven `fire("DLGANS:<name>")` path so existing `sacred.on_trigger` gates work
unchanged. Pure read of a stack buffer — no container mutation, can't corrupt.
Prologue of `0x52AB70` is the standard `6A FF 68` SEH frame (install_hook-
compatible); but we want a mid-fn hook at the format point, so patch a clean
boundary there (capstone-exact, red-team first per §7).

**B. Register the key (heavier).** At NPC bind, insert a node into `0xAB44AC`
via `0x4C5DB0` keyed `Talk_<name>_Dlg_<id>` whose int-vector points at a quest
record we control. Needs the record-index machinery (the `qm+0x31c`-indexed
path that crashed the 0x76 replay), so riskier — keep as plan B.

## Decisive next probe (one build, deterministic — do this FIRST)

Before hooking anything, confirm our captain reaches the talk state:
1. Lua `on_tick` poll of `[cre+0x150]/+0x152/+0x15c]` on the bound captain
   (read-only `sacred.npc_peek`-style), logging on change.
2. Walk up + talk. If `+0x152` hits **6**, path A is correct and the hook
   target is proven. If it never reaches 6, the conversation for a runtime
   spawn runs through a different object → widen to HW-BP on `+0x150`.
3. Optionally also dump the `Talk_<name>_Dlg_<id>` the engine builds (HW-BP on
   the sprintf dst, or just read `[cre+0x15c]` for the dlg id) to get the exact
   key string to register/match.

## Key VAs (this pass)
- `FUN_0052AB70` dialog state-machine tick (ECX=cCreature, SEH `6A FF 68`).
- jump table `0x0052C9F8` on `[cre+0x152]`; **case 6 = `0x0052C29C`**.
- `Talk_%s_Dlg_%d` fmt str `0x0094E140`; used at `0x0052C359` (talk SM) and
  `0x004632E8` (dialog-bind helper `FUN_00463240` = our FIX-A2 binder!).
- refKey `FUN_0044A1C0`; name-getter `FUN_0044A200`.
- named-trigger container head `0xAB44AC` (+ cache `0xAB44B0`); op `0x4C5DB0`
  (find/insert), readers `0x55DD0B/0x593F42/0x597F41/0x7E7ADE`.
- quest-script parser `FUN_0045A370` (sole pop. of `0xAB44AC`), caller
  `0x45C0B6`; cache files `bin\sgq.bin`/`bin\sgqp.bin` (both currently 1 byte
  on disk — empty, i.e. no custom quest scripts compiled).
- per-tick think caller `FUN_005375C6`, talk-SM call site `0x00539976`.

## REFINEMENT — what FIX-A2's `FUN_00463240` actually is (read in full)

`FUN_00463240` (our FIX-A2 binder, HANDOFF §11) is **the engine's OWN
"register creature as a talk/dialog NPC" routine.** Full disasm `fix_a2_dis.py`:
1. `0x464bd0` resolves the creature → name C-string (`[esp+0x2c]`).
2. sprintf **`Talk_<name>_Dlg_<dlgId>`** into `[esp+0x34]` (dlgId from arg
   `[esp+0x148]`); also sprintf **`SelfTriggerQuest<n>`** (n from `[esp+0x160]`).
3. `0x549920` (DlgNPC-index getter) → writes the creature's DlgNPC vector
   entry `[qm+0x755c]+idx*0x50+0x4c` (this is exactly the HW-BP slot!).
4. **Looks the `Talk_<name>_Dlg_<id>` string up in the trigger-NAME table at
   `0xAAB708`** (stride **0x54**; loop `0x463442–0x463493` strcmp via
   `0x859690`). Found → its index; not found → `ebp = -1` (`0x463499`).
5. Registers the creature into a **per-object dialog container at `qm+0x752c`**
   via `0x4C5880` (find) / `0x4BFA20` (insert) keyed by that name.
6. `0x463697: or dword [cre+0x14], 0x80000` — sets the dialog/nameplate gate
   (the persistent FIX-A2 bit HANDOFF §6 documented).

**So there are TWO tables, both filled ONLY by the quest-script parser
`FUN_0045A370` at load:**
- `0xAAB708` — trigger-NAME → id (stride 0x54). The `Talk_*`/`SelfTriggerQuest*`
  names live here. **Our NPC's name is absent → step-4 lookup returns -1 → the
  bind registers an EMPTY quest vector** (window opens via the gate bit, but no
  quest linkage). THIS is the precise root cause of the blocker.
- `0xAB44AC` — runtime dispatch container (name → quest record idx vector),
  consumed by the talk-SM case 6 (`0x52C29C`) and the region/quest drivers.

### Sharpened unblock (best path, uses the engine's own wiring)
Register `Talk_<name>_Dlg_<id>` (and/or `SelfTriggerQuest<n>`) as a NAME in
`0xAAB708` *before* calling the bind `FUN_00463240`, pointing at a quest-record
index we own. Then step-4's lookup succeeds, the engine wires our talk to that
quest, and case-6 dispatch fires it natively — `sacred.on_trigger` works with
zero new hooks. We already control the bind call; we now know the one table
(`0xAAB708`, stride 0x54) that must contain the key. Live-probe FIRST that
`[cre+0x152]` reaches 6 on talk (cheap poll), then prototype the `0xAAB708`
insert (guarded, revertible). Fallback = path A (hook case-6, fire ourselves).

Tables to dump next session to design the insert: `0xAAB708` entry layout
(name@? id@? stride 0x54) and `0xAB44AC` node layout (key@+4, vec@+0x84 per
`0x4C5DB0`). Both empty-ish now since `bin\sgq.bin`/`sgqp.bin` are 1 byte.

## LIVE DUMP of 0xAAB708 (2026-06-13, `sacred.trigger_table_dump`)

**DECISIVE: Path A is the compiled-script wall.** Runtime dump of the live
table (DLL `ed7823ad`, `sacred.trigger_table_dump(substr)`):

- Table is LIVE: **23495 entries**, stride 0x54. `begin=*(0xAAB708)`,
  `end=*(0xAAB70C)`, `cap=*(0xAAB710)`. (So the vanilla campaign DOES populate
  it — earlier "sgq.bin 1 byte" was misleading; the symbols live in the
  in-memory script blob `[0xAACF4C]`, not the disk cache.)
- **Entry layout (0x54 bytes):**
  - `+0x00 .. +0x3F` : name, inline NUL-terminated char[64].
  - `+0x40` : u32 — BYTE OFFSET into the compiled script bytecode blob.
  - `+0x44` : u32 — LENGTH of that record.
  - `+0x48` : u32 — link/next = `0xFFFFFFFF` (none).
  - `+0x4C`, `+0x50` : u32 = 0.
- **`Talk_` prefix: 0 matches.** So `Talk_<name>_Dlg_<id>` (built by the binder
  `FUN_00463240` and the talk SM case 6) is NOT how vanilla talk fires — that
  lookup always misses. Dead end for the "register Talk_ key" idea.
- **`Dialog:` family: 1022 matches** — the real dialog/talk trigger family.
  Offsets are CONTIGUOUS+chained, proving they index one packed bytecode blob:
  ```
  Dialog:grabsteintext    +40=0x810e +44=0x4f   (off,len)
  Dialog:grabsteintext_1  +40=0x815d +44=0x22   0x815d = 0x810e+0x4f
  Dialog:grabsteintext_2  +40=0x817f +44=0x25   0x817f = 0x815d+0x22
  ```
  So each `Dialog:<name>` = name → (offset,len) into compiled quest bytecode.

**Conclusion:** registering a NAME in `0xAAB708` is insufficient — the engine
jumps to the bytecode at `+0x40`, which our runtime mod doesn't produce.
Native Path A therefore needs BOTH (a) a name entry AND (b) a compiled dialog/
quest bytecode record appended to the script blob `[0xAACF4C]` for it to point
at. That is the parked cScriptCompiler / bytecode-injection path (docs 20/22) —
related to the SDK's FunkCode work but a multi-step effort, not a table poke.

### Revised recommendation
- **Path B (pragmatic, recommended):** hook the talk handler. `FUN_0056B130`
  fires on the HERO (h=1) exactly at talk (live-confirmed 2026-06-13); enhance
  that hook to read the dialog-target NPC and fire `DLGANS:<name>` via the
  proven `fire()` path → existing `sacred.on_trigger` gates work, NO script
  tables involved.
- **Path A2 (heavy, native):** append a compiled dialog record to the script
  blob + register a `Dialog:<name>` entry pointing at it + link the NPC's
  CreateNPC dialog field. Leverages FunkCode expertise; multi-session.

New diagnostic: `sacred.trigger_table_dump([substr])` in `runtime_triggers.cpp`
(read-only; logs count + first 3 entries' layout + up to 20 substring matches
with their 5 payload dwords).

## TALK SIGNAL SOLVED + dialog-text root cause (2026-06-13)

**Blocker closed:** the general talk signal is `cCreature+0x200 bit 0x400`
(pulses on player interaction; validated 4/4, no false positives). SDK API:
`sacred.npc_in_dialog(h)` + npcobj `o:in_dialog()` / `o:on_talk(fn)`. Q1 talk
steps now driven by it (confirmed end-to-end). See memory `talk-signal-0x200`.

**Two polish issues remain, both characterized:**

1. **Dialog text shows random strings.** ROOT CAUSE found via the decompile
   (`triggers_dialog_move.md` §B + disasm of the Dialog handler `FUN_0048f9e0`):
   the engine does NOT set dialog content by a raw write to `entry+0x4c`. It
   calls **`FUN_00465220`** (the content setter) with a RESOLVED resource
   handle taken from `[edi+0xc]`:
   ```
   0x48FB2C  mov edx,[edi+0xc]    ; resolved content handle
   0x48FB33  call 0x465220        ; <- content setter (what we bypass)
   0x48FB3D  call 0x5498f0        ; activate(idx,1)
   0x48FB49  or  [edi+0x14],0x80000
   ```
   Our `o:say()` / `dialog_arm` writes `sacred_hash(text_key)|0x80000000`
   straight into `entry+0x4c` (and `cre+0xc`), bypassing `FUN_00465220` and the
   resource resolution → the renderer reads the wrong handle form → random
   vanilla string. (Bake is VERIFIED correct: `check_dlgtext.py` shows
   CAPMILES_GREET/ROCHEFORD_GREET are in global.res at the right `sacred_hash`
   idents, index sorted. Writing the hash to BOTH the by-objIdx AND by-handle
   DlgNPC slots did NOT fix it — so it's the resolution, not the slot.)
   FIX direction: either resolve `text_key` to a real content handle via the
   engine resource manager (`FUN_006725e0`/`FUN_006726f0`) THEN write it, or
   call `FUN_00465220` the way `FUN_0048f9e0` does. DECISIVE next step: a live
   BP/probe on `FUN_0048f9e0` (or log its args) while talking to a VANILLA
   dialog NPC to capture the real `[edi+0xc]` content-handle format + the
   `FUN_00465220(ctx, idx, handle)` call convention, then replicate.

2. **Companions follow/fight but no roster panel portrait.** `npc_roster_add`
   only stamps the panel when a LIVE quest-NPC entry exists at `qm+0x31c` with
   spare member-vector capacity; our runtime quest creates none, and the engine
   array-grow ABI is unpinned/crash-risky (parked). Needs that grow path, or a
   custom overlay panel.
