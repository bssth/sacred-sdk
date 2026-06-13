# Quest lifecycle — making 9512 functional — 2026-05-15

Singleton `cQuestMgr` @ **0x00AACF80** (= ECX in all handlers; its
`+0x424/+0x428` == globals `DAT_00aad3a4`/`DAT_00aad3a8`). Entry stride
0x174, key = quest_id u32 @ entry+0x08. Active class =
`*(int*)(FUN_007d84a0()+0x14)` (1..16); call it `C`.

---

## 1. ON-MAP & MINIMAP MARKER  (confidence HIGH)

**Per-class table layout (verified, FUN_0048d930 / FUN_004a5980 /
FUN_0049da50 / FUN_0049dab0):** base `cQuestMgr+0x3a0`, **stride 8**,
indexed by class. Three parallel u32 columns:

| addr | global | meaning | written by |
|---|---|---|---|
| `cQuestMgr+0x3a0 + C*8` | DAT_00aad320-4 | "active/current quest" entry **index** | FUN_0049dab0 |
| `cQuestMgr+0x3a4 + C*8` | DAT_00aad31c | **class-quest marker slot** = entry **index** | tag-0x75 FUN_0048d930 |
| `cQuestMgr+0x3a8 + C*8` | — | secondary marker slot = entry index | tag-0x3f/0x40 |

FUN_0048d930 stores the **entry index** (`uVar4`, the 0..count loop
counter) — NOT quest_id, NOT a coord — into `+0x3a4 + C*8` for
quest_id≥100 (or `+0x3a4 + (C-1)*8` for id<100). It locates the entry
by scanning `entry+8 == iVar3` where iVar3 is the tag's stream arg 0xb.

**FUN_004a5980(slot,&outU,&outV,&outColor,doScan)** — `__thiscall`,
ECX=cQuestMgr. `slot 0`: class-quest marker — reads index from
`(&DAT_00aad31c)[C*2]` (= `+0x3a4 + C*8`), requires that entry's
`+8 ≤ 100` and `+4 < 99`. Then reads the entry's marker source from
**+0x10 / +0x14 / +0x18 / +0x1C / +0x20**:
- `+0x10 == 0xEEEEEEEE` → resolve object id in +0x14 (NPC marker)
- `+0x10 == -1` (0xFFFFFFFF) → object in +0x14, follows live pos
- else → treat **+0x10 = world X**, **+0x14 = world Y** literal coord
  (the `FUN_006224b0(0,+0x10,+0x14,0)` path); +0x20 = priority/range.

Callers: **FUN_006e3f70** (minimap/radar + compass arrows, calls slots
0,1,2,3) and **FUN_006c8140 / FUN_006ccbd0 / FUN_00615550** (world
map). Minimap slot 0 → green arrow; the world-map uses slot 3 (the
`cQuestMgr+0x7704` single-target path, set via FUN_004a63b0, NOT the
per-class table).

**Recipe for quest_id 9512 (literal-coord marker, simplest):**
```c
uint8_t* base = (uint8_t*)0x00AACF80;
uint8_t* arr  = *(uint8_t**)0x00AAD3A4;
uint32_t idx  = <index of 9512 in registry>;       // your resize() slot
uint8_t* e    = arr + idx*0x174;
int C = *(int*)(*(int(__stdcall**)())0x007D84A0)() ... ; // class 1..16
*(uint32_t*)(e + 0x10) = (uint32_t)worldX;   // 0..6279
*(uint32_t*)(e + 0x14) = (uint32_t)worldY;   // 0..6307
*(uint32_t*)(e + 0x20) = 0;                  // priority
// register the marker slot (entry index) for this class:
*(uint32_t*)(base + 0x3a4 + C*8) = idx;      // == DAT_00aad31c[C*2]
```
GATE CAVEAT: slot-0 path requires `entry+8 <= 100`. Our id 9512 fails
that → use slot-3 single-target instead: call **FUN_004a63b0(&xyrec)**
(ECX=cQuestMgr, `__thiscall`; xyrec built like FUN_006c8140 auStack:
u16 mapX, u32 worldX@+? ) **OR** stamp `cQuestMgr+0x7704=0xFFFFFFFF /
+0x7708=objId` (NPC) or `+0x7704=worldX,+0x7708=worldY,+0x7718/+0x771c
!=0` directly — FUN_004a5980 slot 3 reads those. **Slot 3 is the
reliable path for a high quest_id.** [coord-literal & slot-3 fields:
HIGH; exact FUN_004a63b0 record layout: MEDIUM — verify with BP.]

---

## 2. OBJECTIVE / TASK PROGRESSION  (confidence HIGH)

The journal builder FUN_006b07e0 renders, per entry, one row whose
bullet/counter come from these entry fields (lines 96–215):
- **+0x24** log-text slot 0 (must be !=0 — render gate).
- **+0x28** = `local_b8`, copied verbatim into the row record
  (`local_40[1]`) — this is the **"1/1"-style counter / sub-id text**.
- **+0x2C..+0x4C** (10 u32 slots) = the de-duplicated sub-step text
  handle list (loop at 228–243 collects 9 dwords from +0x2C up).
- **+0x04** type: switch → icon set 0x49a (type 1/2), 0x49c (100),
  0x49b (0x65); also chooses bullet glyph base
  (`(-(bVar17)&8)+0x18` | 1, or |0x41 for type 100/0x65).
- **+0x0C bit0**: if set → bullet glyph `|= 0x80` (the **filled /
  highlighted "current objective"** marker vs hollow). This is the
  open-vs-done bullet.
- **+0x00 == 3** (`bVar17`) → "completed/struck" styling (greys the
  row, alt icon `... & 0xfffffffc`).
- "current objective" highlight also fires when
  `local_dc (=this entry index) == FUN_0049da50()` AND NOT type-3
  → `local_d4 = 0x499` (the active-quest header string).

**Mutators:** all sub-step text is written by **tag-0x35 FUN_00496080**
(0x00496080, walker-driven, scan key entry+8): record sub-field
`local_a0` (`*(cQuestMgr+0xa864)`); `local_a0 % 10 == 0` → writes
+0x24, else writes into the +0x28.. slot array (`uVar5*0x5d` row,
+0x28+i*4). So **emit repeated tag-0x35 records with increasing
sub-index** to add steps. Page id +0x16C set by tag-0x57 FUN_004a6ea0.

**To drive a step directly from the DLL:**
```c
*(uint32_t*)(e + 0x24) = qb_resolve("MYQUEST_TITLE");   // line 0 (gate)
*(uint32_t*)(e + 0x28) = qb_resolve("MYQUEST_STEP1");    // counter/sub
*(uint32_t*)(e + 0x2C) = qb_resolve("MYQUEST_STEP1TXT"); // step list[0]
*(uint16_t*)(e + 0x16C)= page;          // 0 = first tab, else tab N+1
*(uint32_t*)(e + 0x04) = 1;             // type 1 = normal
*(uint8_t *)(e + 0x0C) &= ~1;           // step OPEN (hollow bullet)
```
Mark a step **done** = set `*(uint8_t*)(e+0x0C) |= 1` (fills bullet),
or advance to next step by rewriting +0x28/+0x2C handles. `qb_resolve`
= `(uint(__cdecl*)(const char*))0x00672740` (returns hash|0x80000000,
0 if name not in loaded dict). [HIGH for field semantics; the exact
+0x28-slot ↔ on-screen "1/1" text mapping: MEDIUM — confirm by BP.]

---

## 3. COMPLETION / SOLVED  (confidence HIGH)

Two distinct mechanisms, both usable directly:

**(a) "Completed" styling, quest stays in book:** set
`*(uint32_t*)(e + 0x00) = 3`. FUN_006b07e0 `bVar17 = (+0x00==3)` →
greyed/struck row + alternate (done) icon. Reversible. This is the
in-place "solved but still listed" look. The active-quest header
(string 0x499) only shows while `index==FUN_0049da50()` and `+0x00!=3`,
so setting +0x00=3 also removes the "current quest" emphasis.

**(b) Remove from journal entirely (vanilla "quest solved" → gone):**
tag-0x4d handler **FUN_0048e600 @ 0x0048E600**. It finds entry by
`+8==quest_id`, copies the LAST entry over it, then
`*(cQuestMgr+0x428) -= 0x174` (shrinks the vector). To do this from
the DLL without the FunkCode stream, replicate:
```c
uint8_t* arr = *(uint8_t**)0x00AAD3A4;
uint8_t* end = *(uint8_t**)0x00AAD3A8;
uint32_t cnt = (end-arr)/0x174;
memcpy(arr + idx*0x174, arr + (cnt-1)*0x174, 0x174); // last over target
*(uint8_t**)0x00AAD3A8 -= 0x174;                      // pop
```
(Mirror of FUN_0048e600 lines 258–295; ordering/duplication of any
quest_id you still need must be re-checked after.) Prefer **(a)** for a
"completed tab" look, **(b)** to make it vanish. [HIGH — direct
decompile of both paths.]

There is **no separate qbit/hero flag** the journal consults for
completion; it is purely the +0x00==3 field and vector membership.
(Engine *gameplay* completion is script/qbit-driven elsewhere, but the
journal display is just these.)

---

## 4. REWARD  (confidence HIGH)

No engine field on the 0x174 entry grants a reward. None of the 7
quest handlers (0x35/0x57/0x40/0x3f/0x75/0x4d) touch gold/inventory;
reward is **entirely FunkCode/script-driven** (give-gold / give-item
opcodes act on the hero struct, exactly the path our existing
`runtime_triggers` ctx:give_gold / has_item already drives). **Correct
approach: grant the reward from our trigger system** (on the same event
that flips completion in §3), not via any quest-entry write. There is
no engine "grant quest reward keyed off entry" path. [HIGH.]

---

## PRIORITIZED IMPLEMENTATION ORDER

For a visibly functional 9512 (marker + 1 objective + completion +
scripted reward), implement in this order:

1. **Objective text (already half-done).** Ensure +0x24 !=0 via
   `qb_resolve` and +0x16C = page. Add +0x28 (counter) + one +0x2C
   step handle, +0x04=1, +0x0C bit0 clear. → quest renders with a
   real, open sub-step. *Lowest risk, no engine calls.*
2. **Marker.** Use slot-3 single-target: stamp `cQuestMgr+0x7704 =
   worldX, +0x7708 = worldY, +0x7718 = 1, +0x771c = 0` (literal-coord
   branch in FUN_004a5980 sVar3==3). Verify with a runtime BP on
   0x004a5980 (slot==3) that outU/outV are sane before trusting the
   field set. *Marker on minimap + world map.*
3. **Completion.** On the trigger that finishes the quest: set
   `*(uint32_t*)(e+0x0C) |= 1` (step done) then
   `*(uint32_t*)(e+0x00) = 3` (quest greyed/solved). Optionally later
   call the §3(b) pop to remove it.
4. **Reward.** From the same trigger, call existing
   `runtime_triggers ctx:give_gold` / give-item. No new RE needed.

## NEEDS RUNTIME BREAKPOINT (flagged)

- Exact FUN_004a63b0 / `cQuestMgr+0x7704` xy record byte layout
  (slot-3) — BP 0x004A5980 with slot==3, dump `in_ECX+0x7704..0x7720`.
- The +0x28..+0x4C slot ↔ on-screen step/counter text mapping — BP
  0x006B07E0 inner loop, dump `local_40` vs rendered row for a vanilla
  multi-step quest (e.g. id 1).
- Confirm `qb_resolve` string form (with/without `res:`) — BP
  0x00672740, dump arg + EAX (already noted in questbook_resolver.md).

---

## Quest class + companion roster + reward-refire (2026-05-17)

Raw-disasm / Ghidra-C only (base 0x400000, file-off=VA−0x400000).
Key decompiles read this pass: journal builder `FUN_006b07e0`, tab
setter `FUN_004a6ea0` (tag 0x57), active-idx `FUN_0049da50/0049dab0`,
roster-add `FUN_00450c50` (capstone, not in decompiled set), CreateNPC
QuestNPC-join `FUN_00482510:1500-1760` + name-append `:1240-1349`,
talk-binder `FUN_00463240`, SelfTriggerQuest handler `FUN_0048ff10`
(tag 0x76). SDK side: `runtime_triggers.cpp` `questbook_set_log_impl`,
`player_state.cpp` `npc_make_companion`/`dialog_arm`.

### (Q1) MAIN vs SECONDARY quest classification — CONF: HIGH

**THE field: quest-entry `+0x00` (u32) = quest CATEGORY. `3` = MAIN /
story, `4` = SIDE / secondary.** (Stride 0x174 entry, registry begin
`*0x00AAD3A4`.) Ground truth is already in our own tree: live F8 dump
of vanilla MAIN quest id 1 vs SIDE quest 15001 → +0x00 is 3 vs 4
(documented verbatim at `runtime_triggers.cpp:951-957`).

**Root cause of "9550 shows as secondary":** `questbook_set_log_impl`
(`runtime_triggers.cpp:958`) hard-stamps `*(u32*)(e+0x00) = 4` (side).
That single write demotes every SDK quest to the side category.

Engine confirmation in journal builder `FUN_006b07e0`:
- `:109` `bVar17 = *(int*)(begin + local_c8) == 3;` — reads `+0x00`,
  tests `==3`. `bVar17` then selects the icon set
  (`:113-119` base `0x49a/0x49b/0x49c`, `(-(uint)bVar17 & ~3)` offset)
  and the active-quest header string `0x499` (`:122-124`, only for
  non-100/non-0x65 kind). So `+0x00==3` IS the "main quest" visual.
- TAB placement is a *separate* field: `+0x16C` (u16). Builder
  `:96-98` shows entry on journal tab `local_c0` (0..6) iff
  `+0x24!=0` AND (`+0x16C == local_c0+1` OR (`local_c0==0 &&
  +0x16C==0`)). Tab 0 (header res-string 9000, table `0x009E07CC`
  `[lang*7 + page]`, 7 tabs) is the story tab. `+0x16C` is written by
  tag-0x57 `FUN_004a6ea0:60` from wire arg 0xb. Our
  `questbook_set_log_impl:965` already writes `+0x16C = (page<=0?0:
  page+1)` — so `page=0` ⇒ tab 0 (story tab). Good; the only bug is
  the category byte.

**RECIPE (SDK fix):** in `questbook_set_log_impl` change
`*(uint32_t*)(e + QB_ENTRY_OFF_TYPE) = 4;` → `= 3;` (MAIN). Keep
`+0x04=2` (active state — vanilla bumps to 100 only when solved; see
`questbook_mark_solved_impl`), `+0x0C=1`, `+0x16C` from `page` with
`page=0` for the story tab. i.e. for quest 9550:
```
*(uint32_t*)(e + 0x00) = 3;     // CATEGORY = MAIN/story  (was 4)
*(uint32_t*)(e + 0x04) = 2;     // active
*(uint32_t*)(e + 0x0C) = 1;     // vanilla active bullet
*(uint16_t*)(e + 0x16C)= 0;     // page 0 = story tab
```
No id-range gate is involved in classification (id ranges only gate
the *marker* slot-1 path, §1 caveat). BP if it still looks side:
HW-exec **`0x006B0760`** (= `006b07e0:109`, the `cmp …==3`) filtered
to our entry addr; one hit confirms `+0x00` is read as 3.

### (Q2) Companion ROSTER UI — CONF: HIGH (mechanism), MED (exact renderer VA)

The follow/fight bits (`+0x1F4&0x4`, `+0x251=hero_slot`) are a
cCreature-local AI concern (`FUN_00423480`/`00542b20`) and are
**completely independent** of roster membership. The on-screen
escort/companion panel is driven by the **cQuestMgr quest-NPC array**,
NOT by those bits — which is exactly why our companions are invisible
in the panel.

**Registry:** `cQuestMgr+0x31c` = begin, `cQuestMgr+0x320` = end,
stride **0x34** (count = `(+0x320 − +0x31c)/0x34`). Each 0x34 entry
holds, at `+0x1c/+0x20/+0x24`, a std::vector of 0x2c-byte sub-records
(one per member creature); `+0x14 != 0` = slot live. A creature's
membership back-pointer is **`cCreature+0x94` (i16) = its index into
that array** (and `cCreature+0x96` (i16) = the active quest id, the
quest-NPC identity stamp — npc_model.md).

**THE add call: `FUN_00450C50`** — `__thiscall`, `ECX = cCreature`,
1 stack arg = the array index (i16, validated 0..count via the
`[0x00AAD29C..0x00AAD2A0]` globals — these MIRROR the quest-NPC array
begin/end `cQuestMgr+0x31c/+0x320`; the divide constant 0x4EC4EC4F>>4
is exactly **/0x34**, the same stride, confirming it is the SAME
`+0x31c` array, analogous to how `0xAAD3A4/0xAAD3A8` mirror the
questbook `+0x424/+0x428`). Disasm-proven: its only persistent
effect is `0x450CB6  mov word ptr [edi+0x94], bp` →
`*(i16*)(cCreature+0x94) = index`. Everything after `0x450CBF
je 0x450DFD` is a network broadcast (event id 0x173) gated by
`DAT_0182EBEC` (host/MP) — **skipped in single-player** (`ret 8`).

Vanilla path that calls it (CreateNPC `FUN_00482510:1657/1725`, the
"QuestNPC … join DQ" path): it (1) scans `+0x31c` for the right slot
`pTVar15`, (2) `*(u32*)(creature+0x200) |= 0x200` (registered-questNPC
marker, `:1659`), (3) `FUN_00450C50(ECX=creature, pTVar15)` to stamp
`+0x94`, (4) pushes the creature's handle (`*(creature+0xc)`) as a new
0x2c-byte sub-record into array-entry `+0x1c..+0x24` via the
vector-grow `FUN_004B82E0` (`:1689/1737`). The panel iterates the
`+0x31c` array and renders a face per live sub-record.

**ADD recipe (after `npc_make_companion`):**
1. pick/ensure a quest-NPC array slot `idx` (reuse the active class's
   slot, or the slot the quest was registered under; entry must have
   `+0x14!=0`).
2. `*(u32*)(cCreature+0x200) |= 0x200;`
3. `FUN_00450C50(ECX=cCreature, (i16)idx);`  // sets +0x94
4. `*(i16*)(cCreature+0x96) = (i16)quest_id;` // quest-NPC identity
5. push a 0x2c sub-record `{[0]=*(cCreature+0xc) handle, [1]=0x100,
   [2](i16)=idx}` into array-entry `idx`'s vector at `+0x1c`
   (`begin = *(qm+0x31c)+idx*0x34`; vec begin/end/cap = entry
   `+0x1c/+0x20/+0x24`; replicate `FUN_0048ff10:163-188` push:
   if `*(+0x20)==*(+0x24)` call `FUN_004B82E0`, else copy 0xb dwords &
   `*(+0x20)+=0x2c`).

**REMOVE (dismiss):** clear `+0x200 &= ~0x200`, set
`*(i16*)(cCreature+0x94) = -1`, and erase that creature's 0x2c
sub-record from the array-entry vector (compact: copy last over it,
`*(entry+0x20) -= 0x2c`). Inverse of the add.

Confidence: registry + `FUN_00450C50` semantics HIGH (capstone-exact;
single store). Sub-record push shape HIGH (`FUN_0048ff10:163-188` and
`FUN_00482510:1727-1738` agree: 0xb-dword/0x2c-byte, `[1]==0x100`,
`[2]==i16 idx`). **Cheapest BP:** HW-exec **`0x00450CB6`** (the
`mov [edi+0x94],bp`) during a vanilla escort quest where an NPC joins
— one hit dumps ECX(=creature), `bp`(=idx) and proves the panel-add
path + index; set the same on dismiss to see the removal counterpart.

### (Q3) Rocheford repeatable-reward — CONF: HIGH

**Cause = (c) with a (b) component; NOT (a).** Our `dialog_arm`
(`player_state.cpp:1108`) calls `FUN_00463240` with `selfTrigId = 0`,
so we do not inject a SelfTriggerQuest id ourselves — (a) ruled out.

Trace:
- `FUN_00463240` (talk-binder) `:86-89` registers a
  `"SelfTriggerQuest_%d"` block; `:101-110` writes the creature handle
  into the trigger ring slot `param_3*0x50 + 0x4c + *(ctx+0x755c)`;
  `:282` sets `*(creature+0x14) |= 0x80000` (dialog gate). It sets
  **no completion qbit**.
- The SelfTriggerQuest record handler **`FUN_0048FF10`** (tag 0x76),
  which the dialog-answer path runs, is the once-only guard:
  - `:127` `sVar2 = *(i16*)(creature+0x94)` (the same quest-NPC array
    index as Q2), locate array entry.
  - `:138-150` scan that entry's 0x2c-stride sub-vector for a record
    with `*(rec+4)==0x100` AND **`*(rec+0)==*(creature+0xc)`**
    (the creature's current dialog-content handle). Found ⇒
    `bVar3=true`.
  - `:163` `if(!bVar3)` → append the guard record AND fall into the
    reward/broadcast block (`:190+`). If `bVar3` (already present) the
    reward block is skipped. **So the engine's "give reward once"
    guard is keyed by `cCreature+0xc`.**

Why it refires: `dialog_arm:1122` writes
`*(u32*)(cCreature+0x0c) = sacred_hash(text_key)|0x80000000` **every
call** (it is called every tick after `dlgnpc_bind`). The guard
sub-record was stored under the *previous* `+0xc`; rewriting `+0xc`
makes `*(rec+0) != *(creature+0xc)` so `bVar3` is never true again →
`FUN_0048FF10` re-appends and **re-runs the reward block on every
dialog open**. Compounded by (b): we spawn a fresh type-323 Rocheford
whose vanilla one-shot HeroQBit (tag-0x44, set by his native
dialog-4301 quest script) is never established, and his native
dialog tree's reward node is what the SelfTrigger drives.

**CLEAN FIX (engine-faithful), pick one — recommended = 1+3:**
1. **Stop rewriting `+0xc` after the first arm.** In `dialog_arm`,
   write `cCreature+0xc` (and entry+0x4c) ONCE; on subsequent ticks
   skip the `+0xc` write if it already equals the desired content
   handle. This keeps the `FUN_0048FF10` guard valid → reward fires
   exactly once (the engine's own once-semantics then work).
2. **Set the completion bit ourselves after first answer:** call the
   existing `dialog_clear(handle)` (`player_state.cpp:1138` — zeroes
   entry+0x4c, sets +0x48=0x08, leaves the +0x14 gate) on the
   quest-advance event so the window/trigger cannot re-open.
3. **Do not arm a custom dialog on a real quest-NPC type.** Use a
   neutral non-quest creature type for SDK dialog NPCs (no native
   tag-0x44/SelfTrigger reward script attached), OR before arming
   clear the creature's quest-NPC link: `*(i16*)(cCreature+0x96)=−1`
   and `*(u32*)(cCreature+0x200) &= ~0x200`, so his native
   SelfTriggerQuest reward record (`FUN_0048FF10` path keyed off the
   array entry) is not reachable.

The reward itself must stay script/trigger-driven from our own
`runtime_triggers` give-gold/give-item on the quest-advance event
(per §4 of this doc), gated by an SDK-owned "done" flag — never by
re-opening Rocheford's native dialog.

**Cheapest BP:** HW-exec **`0x0048FFA3`** (≈ `FUN_0048FF10:142`, the
`cmp [...]==*(creature+0xc)` guard compare) filtered to Rocheford's
creature. One reward-grant hit shows `bVar3=false` and the stored
`*(rec+0)` vs current `*(creature+0xc)` diverging — confirms the
`+0xc`-rewrite invalidation as the refire cause; after fix #1 the
same BP shows `bVar3=true` (reward skipped) on the 2nd open.

### Residual MED / flagged

- Q2 exact on-screen renderer VA (which UI fn walks `+0x31c` to draw
  faces) not pinned — mechanism (membership = `+0x31c` entry +
  `cCreature+0x94`/sub-record) is HIGH from the add path; the
  renderer just consumes it. Close with the `0x00450CB6` BP above
  (proves a vanilla join makes the face appear ⇒ that registry is the
  source).
- Q2 sub-record full 0x2c-byte layout beyond `[0]=handle, [1]=0x100,
  [2]=i16 idx`: remaining bytes are zero-init in both writer sites
  (`FUN_0048ff10:154-157` fills 8 dwords 0xffffffff then copies
  unaff_EBX dwords) — low risk, but BP `0x004B82E0` arg dump on a
  vanilla join confirms exact template if a face renders blank.
- `FUN_00450C50`'s `idx` bounds check is against
  `[0x00AAD29C/0x00AAD2A0]` = the `+0x31c/+0x320` mirror globals,
  stride **0x34** (divide const 0x4EC4EC4F>>4 == /0x34, verified) —
  i.e. the SAME quest-NPC array, so a valid `idx` is just any live
  `+0x31c` slot (0..count−1). Don't fabricate an out-of-range index.

---

## Map-marker main-vs-secondary (2026-05-17)

Raw capstone + Ghidra-C only (base 0x400000, file-off = VA−0x400000).
Read this pass: `FUN_004a5980` (marker resolver, decompiled),
`FUN_006e3f70` (minimap/radar/compass-arrow renderer, decompiled),
`FUN_006ccbd0` (world-map renderer, decompiled),
`FUN_00615550` (= cheat-console parser, NOT a renderer — discard),
and capstone on `FUN_004974b0`, `FUN_004972e0`, `FUN_00497620`
(the world-map quest-icon SPRITE / COLOUR / list-membership trio),
plus `runtime_triggers.cpp` `questbook_set_log_impl`/
`questbook_set_marker_impl`.

### (1) THE field that selects MAIN vs SECONDARY map marker — CONF: HIGH

It is the SAME field as the journal: **quest-entry `+0x00` (u32) ==
3 ⇒ MAIN, else ⇒ SECONDARY.** Two independent draw helpers both
re-read `+0x00` at draw time on the WORLD-MAP quest-icon list path:

**(a) SPRITE row — `FUN_004974b0(entryIdx)`** (`__thiscall`,
ECX=cQuestMgr, stack arg = entry index). Capstone-exact:
```
004974ea  mov  esi,[eax+edx*4]   ; esi = *(arr + idx*0x174 + 0x00)  (entry+0x00)
004974ed  cmp  esi,3
004974f1  setne cl
004974f4  add  ecx,0x8A          ; return 0x8A if +0x00==3 (MAIN), else 0x8B
004974fc  ret  4
```
Return value `iVar6` indexes the world-map icon sprite table
`(&DAT_009eced8/dc/e0/e4/e8/ec/f0/fc/00)[iVar6*0x15]` in
`FUN_006ccbd0:986-1031`. So MAIN gets the 0x8A sprite row, SIDE the
0x8B row — different icon. Disasm-proven.

**(b) base/outline COLOUR — `FUN_004972e0(entryIdx, mode)`** (same
sig). Mode-0 (the colour; `arg2==0`):
```
0049732c  mov  esi,[eax+edx*4]   ; esi = entry+0x00
00497332  cmp  esi,3
00497338  sete al                ; al = (entry+0x00 == 3)
00497335  mov  ecx,[ecx+4]       ; ecx = entry+0x04
0049733b  dec  ecx ; cmp ecx,0x64 ; ja default(0xFFFF0000)   ; gate +0x04 in 1..0x65
00497343  mov  dl,[ecx + 0x4973D4]        ; byteTbl[ entry+0x04 - 1 ]
00497349  jmp  [edx*4 + 0x4973C4]
; byteTbl@0x4973D4 (idx=+0x04-1): [0]=0 [1]=0 [2..98]=3 [99]=1 [100]=2
; jt@0x4973C4: 0->0x497350  1->0x497365(0xFF00FF00)  2->0x497371(0xFFFF0000)  3->default
0x497350:  neg al; sbb eax,eax; and eax,0xFF9281; add eax,0xFF006CFF
           ; al=1 (entry+0x00==3, MAIN) -> 0xFFFFFF80
           ; al=0 (entry+0x00!=3, SIDE) -> 0xFF006CFF
```
So the MAIN/SIDE colour split (**0xFFFFFF80 vs 0xFF006CFF**) is made
ONLY in the `case 0` arm, which is reached ONLY when
`byteTbl[entry+0x04-1] == 0`, i.e. **entry+0x04 ∈ {1,2}**. For
`+0x04 ∈ {3..99}` the table is 3 ⇒ default red, no MAIN/SIDE split;
`+0x04==100` ⇒ red, `==101` ⇒ green. (Mode-1, `FUN_004972e0(idx,1)`,
keys only off `+0x04`: {1,2}->white 0xFFFFFFFF, {100,101}->grey
0xFFAFAFAF, else red — it does NOT read +0x00.)

**List membership — `FUN_00497620`** (`__thiscall`, builds the
world-map icon list): iterates the visible map-cell objects (source
`*0x00AD5C40`), and for each coord-resolvable quest does, at
`0x497752`: `cmp dword[arr + idx + 4],2 ; sete al` → an entry is
included in the world-map icon list ONLY if **entry+0x04 == 2** (when
visible). So the icon list path requires `+0x04 == 2`.

Net: on the WORLD-MAP icon path, MAIN-ness = `entry+0x00 == 3`
(re-read at draw, both sprite & colour), AND the entry only appears
at all if `entry+0x04 == 2`. The slot-1/slot-3 cQuestMgr columns and
`+0x7704/+0x7708/+0x7718/+0x771C` are NOT used by this list — they
feed the COMPASS-ARROW / minimap path only (`FUN_006e3f70` slots
0..3, each a HARDCODED colour: slot0 red `0xFFFF0000`, slot1 green
`0xFF00FF00`, slot2 blue, slot3 white — these never encode MAIN vs
SIDE; vanilla main & side both use green slot-1 arrow). The map-marker
"type/colour/icon" the user sees IS the world-map icon = (1a)+(1b).

### (2) Why our 9550 marker shows as SECONDARY — CONF: HIGH (mechanism), MED (which path actually rejected)

`questbook_set_log_impl` (`runtime_triggers.cpp:963-965`) already
writes `+0x00 = 3` and `+0x04 = 2`. By (1) that is EXACTLY the MAIN
combo for the world-map icon list, so the icon-list path is not the
demotion. The demotion is the **marker registration path**:
`questbook_set_marker_impl` (`:1107-1114`) does NOT touch the
world-map icon list at all — it writes the literal coord to
`+0x10/+0x14`, registers the entry index into the **slot-1 per-class
column** `mgr+0x3a0+C*8`, and kills slot-3 (`+0x7704=0`,`+0x7718=0`).
Consequences:
- The slot-1 path it lights up (`FUN_004a5980` slot 1 → compass arrow
  in `FUN_006e3f70:48-56`) is the GREEN-arrow path with a HARDCODED
  colour — it can never look "main". For id 9550 the slot-1 gate
  (`FUN_004a5980:73-95`: +8≥100 ✓, +4<99 ✓, NOT 8999<id<9500 — 9550
  is outside that band ✓) passes, so we get the generic green arrow,
  which reads as "a tracked side waypoint", not a story marker.
- Slot-3 (the WHITE primary single-target marker) is deliberately
  zeroed by `:1113-1114`, removing the only "primary" arrow.
- The real MAIN-style world-map ICON (sprite 0x8A + colour
  0xFFFFFF80) is produced by the `FUN_00497620` list, which keys off
  `+0x00==3 & +0x04==2` and the entry's `+0x10/+0x14` coord — fields
  we DO set. If the icon still shows side-styled, the only remaining
  divergence is the coord-resolve cull in `FUN_00635c40` /
  `FUN_006224b0` rejecting our literal `+0x10/+0x14` (so the entry is
  dropped from the list and only the green slot-1 arrow remains) —
  that is the MED residual to confirm by BP.

### (3) RECIPE — make 9550's map marker render MAIN — CONF: HIGH

Keep the journal-correct writes (`+0x00=3`, `+0x04=2`, `+0x0C=1`,
`+0x16C=0`). The map fix is entirely in `questbook_set_marker_impl`;
no NEW field is needed for MAIN-ness (the icon path already reads
`+0x00==3`). Change the registration so the marker uses the
world-map ICON path / slot-3 PRIMARY instead of the green slot-1
arrow:

```c
// entry coords already set:
*(uint32_t*)(e + 0x10) = (uint32_t)wx;   // literal world X
*(uint32_t*)(e + 0x14) = (uint32_t)wy;   // literal world Y
*(uint32_t*)(e + 0x20) = 0;              // priority
// MAIN visual is driven by +0x00 (already 3 from set_log) — do NOT
// overwrite it here; ensure set_log ran first (or also set +0x00=3,
// +0x04=2 here for safety):
*(uint32_t*)(e + 0x00) = 3;              // MAIN sprite 0x8A + colour 0xFFFFFF80
*(uint32_t*)(e + 0x04) = 2;              // required for icon-list membership

// DO NOT register slot-1 (green generic arrow). Instead use slot-3
// (white PRIMARY single-target) so the compass/minimap arrow matches
// a main-story target, and let the world-map ICON list (keyed by
// +0x00==3) draw the MAIN icon:
*(uint32_t*)(mgr + 0x7704) = (uint32_t)wx;   // slot-3 literal X
*(uint32_t*)(mgr + 0x7708) = (uint32_t)wy;   // slot-3 literal Y
*(uint32_t*)(mgr + 0x7718) = 1;              // slot-3 enable
*(uint32_t*)(mgr + 0x771C) = 0;
// and DO NOT write mgr+0x3a0+C*8 (drop the slot-1 registration).
```
Rationale: the visible MAIN/SIDE icon is `+0x00==3`-driven on the
`FUN_00497620` icon-list path (proven §1a/§1b). Our `+0x00` is
already 3; the bug was registering slot-1 (hardcoded green, no
category) and zeroing slot-3 (the primary arrow). Switching to
slot-3 restores the primary compass arrow, and the icon list will
draw sprite 0x8A + 0xFFFFFF80 because `+0x00==3 & +0x04==2`.

Confidence HIGH that `+0x00==3` is THE main-vs-side selector for both
sprite & colour (capstone-exact, two independent helpers). MED only
on whether our literal `+0x10/+0x14` survives the
`FUN_00635c40/FUN_006224b0` map-cull so the icon actually lists.

**Single cheapest BP (MED residual):** HW-exec
**`0x004974ED`** (`cmp esi,3` in `FUN_004974b0`) filtered to our
entry addr. One hit ⇒ the entry reached the world-map icon sprite
selector and `esi` shows our `+0x00` (expect 3 ⇒ sprite 0x8A). NO
hit while a vanilla MAIN quest's icon draws fine ⇒ our entry is being
culled before the list (coord/`+0x10` issue) — then fall back to BP
`0x00497758` (`cmp [edi+edx+4],2`, the `+0x04==2` membership gate) /
`0x00497745` (the `FUN_00635c40` cull call) to see which test drops
it.
