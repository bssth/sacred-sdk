# Native dialog-text store & resolution chain (RE, 2026-06-13)

Static RE settling the "what writes the dialog text store" blocker. EXE base
`0x00400000`, file offset = VA − 0x400000. `qm` = cQuestMgr @ VA `0x00AACF80`.
Decrypted EXE: `sdk/Sacred_decrypted.exe`; corpus: `sdk/re/ghidra/decompiled/`.

> **HEADLINE (overturns the roadmap's premise):** `DlgNPC entry+0x4c` is **NOT a
> content/text handle** — it holds the **talked-to creature's own handle
> (`creature+0xc`)**. The on-screen dialog text is owned by the *creature*, not by
> a writable per-slot text id. `FUN_006afd20` is **not** a store populator at all —
> it's a 3-arg `__cdecl` `memcpy`/uninitialized_copy helper. There is no
> "write our UTF-16 into a store under handle h" call, because the text isn't
> keyed by a mintable handle. See the recipe for the real path.

---

## 1. FUN_006afd20 — ABI + what it actually is  (HIGH)

Disassembled at file off `0x2AFD20`. The real function is only 0x28 bytes
(`0x6AFD20`–`0x6AFD47`); Ghidra's "FUN_006afd20" symbol visually swallows three
unrelated `ret 0xc`/`ret 4` ctors/dtors that follow (`0x6AFD50`, `0x6AFEA0`,
`0x6AFF60`, `0x6AFFD0`) — ignore those, they are a different class's vtable
helpers.

```
FUN_006afd20(void* first, void* last, void* dst):   // __cdecl, ECX untouched
    if (last == first) return dst;                   // empty -> dst unchanged
    size = (char*)last - (char*)first;               // BYTE count
    memcpy(dst, first, size);                         // 0x849b70 = MSVC memcpy
    return (char*)dst + size;                          // one-past-end
```

- **Calling convention: `__cdecl`**, 3 stack args `(first, last, dst)`, no `this`.
- `0x849b70` is the standard MSVC `memcpy` (rep-movsd + overlap guard + tail jump
  table). So `FUN_006afd20` == `std::uninitialized_copy`/`std::copy` for a
  trivially-copyable byte range; returns one-past-end of the destination.
- **It does NOT index any store, does not touch `qm`, and has no text logic.** In
  `FUN_00465690` (line ~719) it is used purely to bulk-copy the `qm+0x99f4`
  `vector<u32>` body during load; in `FUN_00463240` (line ~208) to materialize a
  trigger-name `vector<u32>`. It is a generic copy primitive.

**Consequence:** the roadmap step "(2) populate the qm dialogText store with our
text under h via FUN_006afd20" is void. That function cannot do it and no sibling
"insert text by handle" primitive exists at `qm+0x99f4`/`0x79f4` (those are not a
text-by-handle map — see §3).

---

## 2. FUN_00465070 (resolver) + FUN_00464760 (insert) — ABI & effect  (HIGH)

`FUN_00465070` — **`__thiscall`**, `ECX = qm`, 2 stack args
`(char* name, char alloc)`, returns `uint`:
- Rejects `name=="hero"` (`DAT_0094e118`) and `name=="NON_UNIQUE"`
  (`s_NON_UNIQUE_0094e228`) → returns `0xffffffff`.
- Copies `name` into a 0x100 local (the local is pre-seeded with the literal
  `"LRes:"` `DAT_0094e234`, but the strcpy **overwrites** it — the seed is dead;
  the lookup key is the raw `name`, no `LRes:` prefix in practice).
- Linear-scans content table `[qm+0x765c, qm+0x7660)` **stride 0x48**, entry name
  at `+0x00`, by `strcmp` (`FUN_00859690`). On hit returns **`entry+0x44`** (the
  content id).
- On miss with `alloc!=0`: walks the AVL at `qm+0x7668` to find the first free id
  in **`0x2076..0x20cf`**, logs the German `"Warnung! Temporäre Resource…"`
  (`DAT_0094e204`), then `FUN_0066ef40(&buf,name,&key)` + **`FUN_00464760`** to
  register, and returns the new id.

`FUN_00464760` — content-table **insert**, **`__thiscall`**, `ECX = qm`, visible
args `(char* name, ?, uint id, char ?)` (called as `FUN_00464760(&local_100, 0,
uVar5, 1)`):
- Re-scans `[qm+0x765c, qm+0x7660)` stride 0x48 by name; if absent, **appends a
  0x48-byte record** (grows the vector at `qm+0x765c`begin/`+0x7660`end/`+0x7664`
  cap), copying the 0x40-byte name into `+0x00`, writing the **id at `+0x44`**,
  and at `+0x40` an aux dword; it also calls `0x634150`/`0x415ef0` to register the
  name in a secondary global index (`[0xAD5C40]`).
- Net: name → id binding in the per-qm content table. **This only maps a name to a
  small id; it stores no text.** (Confirms the id is just an interned label.)

---

## 3. What `qm+0x79f4`, `qm+0x99f4`, `qm+0x79e8` really are  (HIGH/MED)

The roadmap guessed `qm+0x79f4` was "the dialog text store". RE says otherwise:

- **`qm+0x79f4` (0x2000 bytes)** — a **transient file-I/O scratch buffer**. In the
  serializer `FUN_00465690` it is reused as the 0x2000-chunk staging buffer when
  streaming the side files `bin/sgf.bin`, `bin/sgq.bin`, `bin/sgqp.bin`
  (`FUN_0084a5e0` in 0x2000 loops, lines ~1905-2036) and again as the read buffer
  for the save "dialogText" section (line ~711). **It does not persist dialog text
  between frames.** (HIGH)
- **`qm+0x99f4 / +0x99f8 / +0x99fc`** — a `std::vector<u32>` (**stride 4**;
  proven by `>>2 <<2` length math at lines 730 / 3101). Serialized as a
  length-prefixed u32 array paired with the 0x79f4 char blob — i.e. a small
  string-offset/index table, **not** a text-by-content-handle map. `FUN_006afd20`
  copies its body on load. (MED — exact semantic of each u32 not needed for the
  recipe.)
- **`qm+0x79e8 / +0x79ec / +0x79f0`** — the **per-NPC dialog HISTORY** vector,
  **stride 0xbc (188)**, read+written by the dialog apply `FUN_00461540`
  (lines ~988-1101). This is the "who has said what to whom" journal, keyed by a
  **creature handle**, not the live text source. Record layout (partial, from the
  writer):
  - `+0x0c` : matched creature id/handle (the talked-to NPC, from
    `cObjectManager_getData(...)+0xc`)
  - `+0x40` : `u8` line count
  - `+0x04 + (base+count)*4` : hero handle (`creature[3]` = hero `+0x0c`)
  - `+0x44 + (base+count)*4` : **`creature[4]` = talked-to creature `+0x10`** ← the
    content/resource reference that drives the displayed line
  - `+0x1c + …*4` : a dword from `cObjectManager_getData(creature[0x6a])+0x10`
  - `+0x34+count`,`+0x3a+count` : bytes (page idx, bubble idx = `creature+0x100`)
  - `+0x5c + count*0x10` : 0x10-byte position (`creature[6..9]`)
  - Appends a fresh 0xbc record (via `FUN_004bc3a0`) when the NPC has no entry yet.

So **the text reference the renderer cares about is `creature+0x10`** (call it the
creature's "dialog/name resource"), captured into the history at apply time.

---

## 4. The EXACT content-id → displayed-text chain  (settled)

Two independent facts settle it:

1. **`entry+0x4c` is a CREATURE handle, not a text id.**  (HIGH)
   The talk-dialog spawner **`FUN_00463240`** (formats `"Talk_%s_Dlg_%d"`
   `s_Talk__s_Dlg__d_0094e140` and `"SelfTriggerQuest_%d"`) writes, at line 106-107:
   `*(entry_idx*0x50 + 0x4c + *(qm+0x755c)) = *(creature + 0xc)` — i.e. it stores
   the **talked-to creature's own handle (`creature+0xc`)** into `entry+0x4c`, then
   sets `entry+0x14 |= 0x80000` (line 282) and `FUN_005498f0(idx,1)`. The setter
   `FUN_00465220(idx, val)` just does the same write + `FUN_0045ee20(3,0,0)`.
   → A live dialog slot points at a CREATURE; the engine then pulls text from that
   creature. This is why minting a *content* id (0x207D) and writing it to
   `entry+0x4c` produced a coherent-but-wrong vanilla line: the engine treated the
   number as a creature handle / fell back to whatever creature/record that index
   collided with.

2. **`entry+0x4c` is NOT routed through the global.res resolver `FUN_006726f0`.**
   (HIGH — prior project negative result, re-confirmed structurally)
   `FUN_006726f0(h)`: high-bit set → `FUN_0080eaf0(h&0x7fffffff)` (direct
   global.res by hash); high-bit clear → `FUN_0080e780(h)` =
   id→string→`sacred_hash`→global.res. `FUN_0080e780` builds the string via
   `FUN_005f6290` (a `cResString::operator=` that pulls the name out of a
   serialization/object context by index — **the registered resource NAME**, then
   `FUN_0080fab0` returns its C-string) and hashes it with MUL=0x71,
   MOD=0x3b9ac9f7 (== our `sacred_hash`). That chain is the **`res:` resource
   path used at CREATE time** (CreateNPC resolving `res:CAPMILES_NAME`), NOT the
   per-frame dialog-slot path. The proven negative (our
   `sacred_hash(name)|0x80000000` write to `entry+0x4c` still showed random text)
   kills any "feed text via entry+0x4c through global.res" idea.

**Settled resolution chain for a SCRIPTED dialog line:**
```
compiled Dialog record (tag 0x03, "res:NAME") ──dispatch FUN_0048bb40──▶
   FUN_00465070(qm,"NAME",1)               → interned content id (for the record)
   FUN_00461540 (apply)                     → locates talked-to creature, writes a
                                              0xbc history record keyed by
                                              creature+0xc, carrying creature+0x10
   render reads creature+0x10 (the res id set at CreateNPC from "res:NAME")
                                            → FUN_006726f0/FUN_0080e780
                                            → sacred_hash("NAME") → global.res text
```
The DlgNPC `entry+0x4c` only holds the **creature handle** to bind the open
window to that creature; the *string* always comes from
`creature+0x10` → global.res via the `res:` hash (which the SDK already bakes
correctly). There is **no separate per-handle text blob to populate** — the
earlier "store populator" hunt was chasing a structure (`0x79f4`/`0x99f4`) that
is I/O scratch + an index vector, not text storage.

---

## 5. Implementation recipe — make a runtime NPC show OUR native text  (ordered)

Because the displayed string is resolved from **`creature+0x10`** (the NPC's own
`res:` id) and NOT from any writable per-slot text store, the clean native path is
to **give our runtime creature a `res:` id whose `sacred_hash` we already bake
into global.res** — exactly the mechanism CreateNPC uses for `res:CAPMILES_NAME`.

### Recipe A — set the creature's dialog resource (preferred, no store hacking)
1. **Bake the text** into global.res under key `CAPMILES_GREET` at
   `sacred_hash("CAPMILES_GREET")` (already working — `check_dlgtext.py`).
2. **Resolve the res id** the same way CreateNPC resolves `res:` names:
   `id = FUN_006725e0/FUN_006726f0("res:CAPMILES_GREET" path)` *or* reuse the id
   the SDK already computes for the name resource. The displayed-line resolver is
   `FUN_0080e780(id)` (high-bit-clear) → so the value you want in `creature+0x10`
   is the **content id that stringifies to `CAPMILES_GREET`**. Two ways to get it:
   - (a) `h = FUN_00465070(qm, "CAPMILES_GREET", 1)` to intern the NAME, then make
     the renderer stringify `h` back to "CAPMILES_GREET" (the content table maps
     id→name). VERIFY with a live BP that `FUN_0080e780` receives `h` and that
     `FUN_005f6290` returns "CAPMILES_GREET" (needs one live probe — see §6).
   - (b) Or write `sacred_hash("CAPMILES_GREET") | 0x80000000` into `creature+0x10`
     so the **high-bit branch** `FUN_0080eaf0(hash)` hits global.res directly.
     This is the high-bit trick that FAILED on `entry+0x4c` only because that
     field isn't the text source — on **`creature+0x10`** (the actual source) it
     should resolve. (MED — confirm the field is read via `FUN_006726f0` and not a
     raw creature-handle dereference; one live BP settles it.)
3. **Trigger the dialog** on talk (talk signal already solved): call the apply
   `FUN_00461540` (via the tag-0x03 handler `FUN_0048bb40`, or by dispatching a
   baked tag-0x03 record per the roadmap's Recipe-b `FUN_00475680`), with the
   talked-to creature = our NPC. The apply writes the 0xbc history record carrying
   `creature+0x10`, and the window renders the global.res string.

### Recipe B — fully scripted (baker) fallback (already in roadmap step 2/3)
Emit a `tag=0x03` DialogShow_v1 record `[03][size:u16 BE][00][01]"res:CAPMILES_GREET"\0[btn]`
bound to the NPC name; dispatch via `FUN_00475680`→`FUN_0048bb40`→`FUN_00461540`.
This needs no creature-field write — the record's `res:` name is resolved through
the same `FUN_00465070`+`creature+0x10` path. This is the most "vanilla-faithful"
route and avoids guessing `creature+0x10` semantics.

### What NOT to do (disproven)
- Do **not** write a content/text id into `DlgNPC entry+0x4c` expecting text — that
  slot wants the **creature handle**; `FUN_00463240` proves it.
- Do **not** try to memcpy text into `qm+0x79f4`/`qm+0x99f4` — those are I/O
  scratch + a u32 index vector, not a text-by-handle store, and `FUN_006afd20` is
  just memcpy.

---

## 6. Confidence + remaining live-BP work

| Claim | Conf | Evidence |
|---|---|---|
| `FUN_006afd20` = `__cdecl` 3-arg memcpy/uninit_copy, not a store fn | HIGH | full disasm 0x6AFD20-47; callee 0x849b70 = MSVC memcpy |
| `FUN_00465070` `__thiscall(ECX=qm,name,alloc)` returns id@`+0x44`; mints 0x2076..0x20cf | HIGH | decompile 00465070 |
| `FUN_00464760` appends 0x48-byte name→id record; stores no text | HIGH | disasm 0x464760 + decompile context |
| `entry+0x4c` holds the **creature handle** (`creature+0xc`), not a text id | HIGH | `FUN_00463240` line 106-107 |
| `entry+0x4c` not routed via `FUN_006726f0`/global.res | HIGH | prior negative result + struct reading |
| Displayed text comes from **`creature+0x10`** via `res:`→`sacred_hash`→global.res | HIGH (path) / MED (exact creature+0x10 field id) | `FUN_00461540` writes `creature[4]` into history `+0x44`; `FUN_0080e780` hash == sacred_hash |
| `qm+0x79f4`=0x2000 I/O scratch; `qm+0x99f4`=`vector<u32>` index; `qm+0x79e8`=0xbc dialog history | HIGH / HIGH / MED | `FUN_00465690` lines 711,1905-2036,3097-3115; `FUN_00461540` 988-1101 |
| Recipe A step 2 (which exact value to put in `creature+0x10`) | MED | needs 1 live BP |

**Remaining live-BP (read-only), one probe settles Recipe A:**
- BP `FUN_0080e780` (0x80E780) **entry**: on a VANILLA scripted dialog, log the
  incoming id (`param_1`) and the string `FUN_005f6290`/`FUN_0080fab0` produce.
  Confirms whether the renderer is fed the content **name** (→ Recipe A-a) or a raw
  hash (→ A-b), and the exact value to place in `creature+0x10`.
- BP read of **`creature+0x10`** during a vanilla NPC's dialog render to confirm it
  IS the field consumed (vs `+0xc`). Compare to the value CreateNPC wrote from
  `res:NAME`.
- If both confirm: implement Recipe A (or the cleaner Recipe B baker path, which
  sidesteps the creature-field question entirely).

### Key VAs
- `FUN_006afd20` 0x6AFD20 · memcpy `0x849B70`
- `FUN_00465070` 0x465070 · `FUN_00464760` 0x464760 · `FUN_00465220` 0x465220
- `FUN_00461540` 0x461540 (apply) · `FUN_00463240` 0x463240 (talk spawn, writes
  entry+0x4c) · `FUN_0045f220` 0x45F220 (record loader, tag 0x28 appends DlgNPC)
- `FUN_0048bb40` 0x48BB40 (tag 0x03) · `FUN_00465690` 0x465690 (save/load serializer)
- resolver `FUN_006726f0` 0x6726F0 → `FUN_0080e780` 0x80E780 / `FUN_0080eaf0` 0x80EAF0
- id→string `FUN_005f6290` 0x5F6290 · cstr fetch `FUN_0080fab0` 0x80FAB0
- qm @ `0x00AACF80`; trigger table `0xAAB708` (stride 0x54)
