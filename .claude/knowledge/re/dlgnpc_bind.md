# DlgNPC bind — making a runtime-spawned creature a real dialog/quest NPC
# (the #1 storyline blocker: custom name + overhead "?!" marker)
# 2026-05-16

Target: Steam 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000, no ASLR
(file off = VA-0x400000). cQuestMgr `qm = 0x00AACF80`. ObjectManager
`om = *(void**)0x00AD5C40`; `arr=*(om+4)`; `cCreature=*(arr+handle*4)`.

Builds on (does NOT repeat) `quest_storyline.md` (esp. the Red-FX section),
`npc_model.md`, `quest_*` reports. This pass reversed the DlgNPC
create/append + the bind→render index chain that those left open.

---

## TL;DR — the answer

1. **There is NO callable "register DlgNPC by name+handle" engine
   function.** DlgNPC entries are created in exactly ONE place: the
   FunkCode/StartCode dispatcher **`FUN_00475680` case `0x28`** (and its
   net/save twin `FUN_0045f220` `local_800==0x28`). Both just **memcpy a
   raw 0x50-byte blob** (the record payload) into the `qm+0x755c`
   std::vector — there is **no element ctor / no vtable**; the element is
   plain old data. So PATH A (hand-append a zeroed 0x50 record) is
   *engine-faithful*, not a hack.
2. The DlgNPC element is **pure data**, 0x50 bytes, ground-truthed from
   16 460 vanilla `StartCode.bin` tag-0x28 records (all payload size
   81 = 1+0x50):

   | off | type | meaning | vanilla-at-definition |
   |---|---|---|---|
   | +0x00 | i32 | bound creature **handle** (engine lookup `entry[0]==handle`) | **-1** (unbound) |
   | +0x04 | char[0x40] | **name** ASCIIZ (engine strcmp `entry+4`) | "dlg_UW12" etc |
   | +0x44 | u32 | dialog/content def id | sequential script id |
   | +0x48 | u32 | **overhead marker sprite id** | 0x0a/0x0b/0x0d/… |
   | +0x4c | i32 | bound handle / state (set by bind handlers) | **0** |
   | +0x50 | (end) | stride = 0x50 | |

   (Element bytes = on-disk record `payload[1 .. 1+0x50]`; payload[0] is a
   leading 0x00 alignment byte the walker skips — its copy source is
   `&local_a0c.spare` = the parsed buffer +4.)
3. **The marker render chain requires `cCreature+0x245` = the DlgNPC
   index.** New, decisive: every bind handler calls
   **`FUN_005498f0(dlgIdx, 1)` with ECX=cCreature**, which does
   `*(cCreature+0x245)=dlgIdx`. The overhead-marker objIdx resolver
   `FUN_00549920` returns `*(cCreature+0x245)` (its fast path / fallback),
   and `FUN_00499e90(objIdx,creature)` then returns
   `*(qm+0x755c + objIdx*0x50 + 0x48)` = our `+0x48` sprite. So we do NOT
   need the `DAT_00ab44e8` 0x58-table at all for a bound NPC — `+0x245`
   short-circuits it.
4. Minimal runtime recipe = **append a DlgNPC entry, point it at our
   handle, set its sprite, and stamp `cCreature+0x245`** (PATH A below).
   PATH B (emit a vanilla tag-0x28 + tag-0x1f script fragment) remains the
   zero-RE-risk fallback.

---

## The functions (VAs, prototypes, what they do)

### Creator (the only one): `FUN_00475680` case 0x28  @ 0x00475680
`void FUN_00475680(int* recPtrInOut, int param1Base, ... , char param5)`,
`ECX = qm (0x00AACF80)`. The FunkCode record dispatcher. Reads a record
into `local_a0c`; `(u16)local_a0c+0 = tag`. **case 0x28** (disasm
0x476a30-0x476abf):
```
puVar18 = *(u32**)(qm+0x7560);                 // DlgNPC end ptr
if (puVar18 == *(u32**)(qm+0x7564))            // no spare capacity
     FUN_004bb1d0(puVar18,&local_a0c.spare,&flag,1,1);   // realloc-grow +1
else { copy 0x14 dwords (0x50B) local_a0c.spare -> *end;
       *(qm+0x7560) += 0x50; }                 // in-place append
FUN_0045ee20(1,3,...);                          // dirty/notify
if ((*(qm+0x7560)-*(qm+0x755c))/0x50 == 1) {    // (first element special)
     ... append again ...; e=*(qm+0x755c);
     *(u32*)e = 0; *(u8*)(e+1) = 0;             // zero +0 dword & +4 byte
}                                               // of element[0] only
// then (DAT_0182ebec) emits net event 0x11c (MP replication)
```
Source element = `&local_a0c.spare` (the 0x50 payload bytes). **No
constructor, no vtable** — confirmed: it is a dword memcpy.

### Vector grow primitive: `FUN_004bb1d0` @ 0x004BB1D0
`std::vector<DlgNPC,0x50>::_insert_realloc`. `ECX = &vector header`
(the 3 ptrs at `qm+0x755c`/`+0x7560`/`+0x7564` = begin/end/cap).
Signature as used: `FUN_004bb1d0(insertPos, srcElem, &afterFlag,
count=1, after=1)`. Geometric growth (`new = old + max(old,n)`), alloc
`n*0x50` (FUN_008321a0 if <0x81 else operator_new), copy
[begin,insertPos) + n*srcElem + [insertPos,end), free old, update
begin/end/cap. **This invalidates cached begin/end** (see Risks).

### Vector resize: `FUN_004bb380` @ 0x004BB380
`std::vector<…,0x50>::resize(n)`, `ECX=&vector header`. `n<count` →
just `end = begin + n*0x50` (truncate, no free). `n>count` → grow
(realloc via FUN_004bb1d0 if cap exceeded) appending **caller-supplied
fill element from the stack** (uninitialised unless caller fills it —
FUN_0046f9b0 fills it before calling). Analogue of the quest-registry
`FUN_004b5370`.

### Bind-by-index helper: `FUN_00465220` @ 0x00465220
`void FUN_00465220(uint idx, u32 value)`, `ECX=qm`. Bounds-checks
`idx < count`, then `*(qm+0x755c + idx*0x50 + 0x4c) = value;
FUN_0045ee20(3,0,0);`. (Sets DlgNPC `entry+0x4c` = bound handle.)

### **Creature objIdx stamp: `FUN_005498f0` @ 0x005498F0  (the linchpin)**
`void FUN_005498f0(int idx, char netFlag)`, **`ECX = cCreature`**:
```
*(int*)(cCreature+0x245) = idx;
if (idx < 1) *(u32*)(cCreature+0x14) &= 0xfff7ffff;   // clears 0x80000
if (netFlag) FUN_0054d760(0x11a);                     // MP notify
```
Called by EVERY DlgNPC bind handler (FUN_0048f9e0 L90/L183,
FUN_00463240 L114, FUN_00491170 path) with `idx`=the DlgNPC array index
of the bound entry. **`cCreature+0x245` is the per-creature DlgNPC
index** that the marker renderer resolves through.

### objIdx resolver: `FUN_00549920` @ 0x00549920  `ECX=cCreature`
Gate (must ALL pass to even try): `*(c+0x245)>0` OR `(c+0x200>>6&1)` OR
`*(i16*)(c+0xfc)==9` OR `*(i16*)(c+0x150)==6` …; subtype
`*(c+0x1f0) ∈ {3,7,8,0xc,0xe,0xf}`; faction
`*(c+0x1f4) & 0x1006dcf8 != 0 && !(… & 0x40000)`. If it passes it
scans the `DAT_00ab44e8` 0x58-table; **else returns `*(c+0x245)`**
(line 317). For a *bound* NPC the very first gate term `0<*(c+0x245)`
is what we satisfy, and the function returns `*(c+0x245)` = our DlgNPC
index. (The `DAT_00ab44e8` table is populated only by the
save/state serializer `FUN_007e69b0`/`FUN_007e87f0`; irrelevant when
`+0x245` is set.)

### sprite selector: `FUN_00499e90` @ 0x00499E90  `ECX=qm`
`(objIdx,creature*)`. `+0x200` class bits → 0x20-0x23 (leave clear!).
Else `objIdx<count` → `return *(u32*)(qm+0x755c+objIdx*0x50+0x48)`.

### renderer: `FUN_00599910` @ 0x00599910  `ECX=cCreature`
Reaches the marker draw `LAB_00599f96` (objIdx=FUN_00549920;
sprite=FUN_00499e90; FUN_004090d0(sprite)) iff **`cCreature+0x14 &
0x80000`** is set OR `FUN_00428460(handleIdx)` (per-handle registry
bit, ECX=a `*0x80`-stride table) returns true OR (ALT + faction).

### name strcmp: `FUN_00859690` @ 0x00859690
Case-insensitive strcmp, returns 0 on equal. The DlgNPC name-lookup
callers pass `entry+4` → **confirms name @ entry+0x04**.

### Bind handlers (lookup-ONLY, never create — for reference)
- `FUN_0048f9e0` @0x0048F9E0 — tag **0x1f** QUESTNPC. case1=by-name
  (`FUN_00859690` on entry+4), case0xb=by-handle (`entry[0]==handle`),
  case0x16→by-name. On match: `entry+0x4c = *(rec+0xc)` (the bound
  handle) via FUN_00465220, `FUN_005498f0(idx,1)`, and
  `cCreature+0x14 |= 0x80000` (bound) / `&= ~0x80000` (idx==0/unbound).
  If NOT found → logs `"Dialog %s nicht oder nocht nicht…"`, clears bit.
- `FUN_00491170` @0x00491170 — tag **6** (op1 name / op9,10 handle) →
  `FUN_00463240` (full talk binder; L282 sets `cCreature+0x14|0x80000`).
- `FUN_004a1a50` @0x004A1A50 — tag **0x56** SetIcon. Writes ONLY
  `entry+0x48` (by-name via FUN_00859690, or nameless via current idx
  `*(qm+0xa458)`); emits 0x1c8 MP event. Never creates, never +0x14.

---

## MINIMAL RUNTIME RECIPE

We already have: `om=*0x00AD5C40`, the creature `handle`, `qm=0x00AACF80`,
the SDK's existing `set_npc_name` (strncpy entry+4) and `npc_quest_icon`
(entry+0x48=0x0b). The ONLY missing piece was a DlgNPC entry; create it.

### PATH A — direct append (preferred; engine-faithful, no scripts)

Confidence **HIGH** on the field map / index chain (all decompiled +
16 460-record catalog); **MED** only on the `FUN_00428460` registry-bit
fallback (BP item) — but we don't rely on it (we set +0x14 like every
vanilla bind handler does; the red-FX is intrinsic to the bind path and
is a *separate* +0xbc/+0xc0 texture issue tracked in quest_storyline.md).

```c
#define QM 0x00AACF80
static uint8_t* creature_of(uint32_t h){
    uint8_t* om = *(uint8_t**)0x00AD5C40;
    uint8_t* arr= *(uint8_t**)(om+4);
    return *(uint8_t**)(arr + h*4);
}
typedef void (__thiscall *resize_t)(void* vecHdr, unsigned n);   // FUN_004bb380
typedef void (__thiscall *bind4c_t)(void* qm, unsigned idx, uint32_t v); // FUN_00465220
typedef void (__thiscall *stampIdx_t)(void* cCreature, int idx, char net);// FUN_005498f0

// vector header = 3 ptrs at QM+0x755c (begin), +0x7560 (end), +0x7564 (cap)
#define DLG_BEGIN (*(uint8_t**)(QM+0x755c))
#define DLG_END   (*(uint8_t**)(QM+0x7560))
static inline unsigned dlg_count(){ return (unsigned)((DLG_END-DLG_BEGIN)/0x50); }

// Returns the DlgNPC index for `handle` (creating the entry if absent).
int dlgnpc_ensure(uint32_t handle, const char* name, uint32_t spriteOn /*0x0b*/){
    uint8_t* c = creature_of(handle);
    if (!c) return -1;

    // 1) find existing entry by handle (entry[0]==handle) — same key engine uses
    int idx = -1; unsigned n = dlg_count();
    for (unsigned i=0;i<n;i++)
        if (*(int32_t*)(DLG_BEGIN + i*0x50) == (int32_t)handle){ idx=(int)i; break; }

    // 2) if absent, grow the vector by one element using the ENGINE resize
    //    (ECX = &vector header at QM+0x755c). Do this ONCE, on the main
    //    thread tick, right after spawn. resize() appends a stack-garbage
    //    element, so we zero+fill it immediately after (begin may have moved
    //    due to realloc — re-read DLG_BEGIN AFTER the call).
    if (idx < 0){
        idx = (int)n;
        ((resize_t)0x004BB380)((void*)(QM+0x755c), n+1);   // ECX=QM+0x755c
        uint8_t* e = DLG_BEGIN + (unsigned)idx*0x50;        // RE-READ begin
        for (int k=0;k<0x50;k+=4) *(uint32_t*)(e+k)=0;      // zero the element
        *(int32_t*)(e+0x00) = (int32_t)handle;              // bound handle
        strncpy((char*)(e+0x04), name, 0x3f);               // name @ +4
        ((char*)(e+0x04))[0x3f]=0;
        *(uint32_t*)(e+0x44) = 0;                           // content id (unused at runtime)
        *(uint32_t*)(e+0x48) = spriteOn ? spriteOn : 0x0d;  // marker sprite (0x0b shows)
        *(int32_t*) (e+0x4c) = (int32_t)handle;             // bound handle/state
    } else {
        uint8_t* e = DLG_BEGIN + (unsigned)idx*0x50;
        *(int32_t*)(e+0x00) = (int32_t)handle;
        strncpy((char*)(e+0x04), name, 0x3f); ((char*)(e+0x04))[0x3f]=0;
        *(uint32_t*)(e+0x48) = spriteOn ? spriteOn : 0x0d;
        *(int32_t*) (e+0x4c) = (int32_t)handle;
    }

    // 3) make the engine bind path "official":
    //    (a) entry+0x4c = handle via the engine helper (also fires
    //        FUN_0045ee20 dirty/notify) — optional, we already wrote +0x4c:
    ((bind4c_t)0x00465220)((void*)QM, (unsigned)idx, handle);   // ECX=QM
    //    (b) stamp cCreature+0x245 = idx  (THE linchpin: makes
    //        FUN_00549920 return idx, and clears/sets +0x14&0x80000).
    ((stampIdx_t)0x005498F0)((void*)c, idx, 0 /*no MP event*/);  // ECX=c
    //    (c) ensure the renderer gate bit is set (idx>=1 keeps it; if
    //        idx==0 FUN_005498f0 cleared it, so force it):
    *(uint32_t*)(c+0x14) |= 0x80000;   // same write every vanilla bind does

    return idx;
}
```
After `dlgnpc_ensure`, the existing SDK calls are sufficient:
`set_npc_name` (strncpy entry+4) and `npc_quest_icon` (entry+0x48 =
0x0b show / 0x0d clear) both now hit a real entry → name resolves in
the dialog/quest UI and the overhead marker draws.

**Important: do NOT write `cCreature+0x200`** (class bits → red flame
aura, see quest_storyline.md Red-FX). The marker comes purely from
`entry+0x48` via the +0x245/objIdx path.

### PATH B — emit a vanilla script fragment (zero RE risk, fallback)

Give the spawn a unique CreateNPC name (tag 0x01 op 0x01). Then push,
into the same StartCode/FunkCode stream the engine replays through
`FUN_00475680`, a **tag 0x28** record (the DlgNPC definition) followed
by a **tag 0x1f** QUESTNPC bind referencing that name. Byte layout of
the tag-0x28 record (verified, 16 460 vanilla samples):
```
28  <size:u16 BE = 0x0054>   payload(0x51 bytes):
  00                          // 1 leading 0x00 (walker skips; copy src = +1)
  FF FF FF FF                 // element+0x00 handle = -1 (engine binds later)
  <name ASCIIZ, fill to 0x40> // element+0x04
  <u32 contentId>             // element+0x44 (any; pick an unused id)
  0B 00 00 00                 // element+0x48 = sprite 0x0b (NPC_DIALOG_02 = "talk/!")
  00 00 00 00                 // element+0x4c = 0
```
The engine then: tag-0x1f finds it by name, writes entry+0x4c=handle,
calls FUN_005498f0 (stamps +0x245) and sets +0x14|0x80000 — identical
to PATH A but done by the engine itself. Toggle the marker later with a
tag-0x56 SetIcon (op 0x0b operand = 0x0b show / 0x0d clear). This is
exactly how all 16 830 shipped quest NPCs do it. Use this if PATH A's
live test misbehaves.

---

## Field map (authoritative)

DlgNPC element, 0x50 bytes, vector hdr at qm+0x755c/+0x7560/+0x7564
(begin/end/cap), count=(end-begin)/0x50:

| off | name | written by | read by |
|---|---|---|---|
| +0x00 i32 | bound handle | tag0x28 def(-1); our PATH A | FUN_0048f9e0/491170 by-handle lookup `entry[0]==h` |
| +0x04 char[0x40] | NPC name | tag0x28 def; set_npc_name | FUN_00859690 strcmp (dialog/QUESTNPC) |
| +0x44 u32 | dialog content id | tag0x28 def | dialog dispatcher (not needed for marker/name) |
| +0x48 u32 | marker sprite id | FUN_004a1a50 / our PATH A | FUN_00499e90 → FUN_004090d0 |
| +0x4c i32 | bound handle/state | FUN_00465220 / bind handlers | dialog/quest state checks |

cCreature fields:

| off | name | set by | role |
|---|---|---|---|
| +0x14 bit 0x80000 | marker force gate | every bind handler; FUN_005498f0 clears if idx<1 | FUN_00599910 L243 (also FUN_0044b230 aura — known) |
| +0x245 i32 | **DlgNPC index** | **FUN_005498f0(idx,*)** | FUN_00549920 returns it → FUN_00499e90 objIdx |
| +0x200 class bits | (DO NOT WRITE) | CreateNPC class path | FUN_00499e90 0x20-0x23 + red aura |
| +0x1f4 faction | CreateNPC side ops | FUN_00549920 gate `&0x1006dcf8` | must be nonzero-in-mask for the table path; NOT required when +0x245>0 |

Sprite ids (FUN_004090d0): **0x0a→NPC_DIALOG_01**, **0x0b→NPC_DIALOG_02**
(the canonical quest "talk to me" bubble — 4339 vanilla defs use it,
runtime quests toggle 0x0b↔0x0d), 0x0d/0x08 → no-draw (clear). Use
**0x0b** to show, **0x0d** to clear. NO +0x200 class bits.

---

## What is static-certain vs needs a live BP

| Item | Conf | Note |
|---|---|---|
| DlgNPC created ONLY by FUN_00475680 case0x28 / FUN_0045f220; raw 0x50 memcpy, no ctor/vtable | **HIGH** | decompile + 0 tag-0x28 in FunkCode.bin, 16 460 in StartCode.bin |
| 0x50 element field map (+0,+4,+0x44,+0x48,+0x4c) | **HIGH** | 16 460-record catalog all size 81, all handle=-1/state=0 |
| Grow=FUN_004bb1d0, resize=FUN_004bb380, ECX=&hdr@qm+0x755c, stride0x50 | **HIGH** | decompile |
| FUN_005498f0 stamps cCreature+0x245=idx & gates +0x14 | **HIGH** | 42-byte fn, called by all binders |
| FUN_00549920 returns *(c+0x245) on the fast/fallback path | **HIGH** | decompile L21/L317 |
| FUN_00499e90 returns entry+0x48 when +0x200 clear & objIdx<count | **HIGH** | decompile |
| Name renderer (nameplate/tooltip) reads DlgNPC entry+4 | **MED** | carried from quest_storyline B; engine reads via name accessor — confirm with HW-read BP on entry+4 while NPC on screen |
| FUN_00428460 registry-bit fallback (non-+0x14 marker path) | **MED** | we don't depend on it; BP 0x00599F49 to characterise if going +0x14-free |
| resize() appends stack-garbage element | **HIGH** | FUN_004bb380 L37/L40 — hence we zero+fill immediately |

### Live BPs to confirm end-to-end (cheap, one capture each)
1. BP `0x005498F0` entry while a vanilla quest giver loads → confirm
   `[ECX+0x245]` receives the DlgNPC idx and ECX is the cCreature.
2. BP `0x00599FA6` (`call FUN_00499e90`) on a vanilla "!" NPC → EAX
   should be 0x0b; args = (objIdx=*(c+0x245), creature*). Then on our
   synthetic NPC after `dlgnpc_ensure` confirm same EAX=0x0b.
3. BP `0x004090D0` sprite==0x0b → loads NPC_DIALOG_02.TGA
   (cache DAT_00aa42e4/…e0).
4. (name) HW-read BP on our entry+4 bytes while the NPC nameplate is
   on-screen → pin the nameplate/tooltip reader VA (closes the last
   MED in quest_storyline §B).

---

## Risk: growing an engine std::vector from the DLL

`FUN_004bb1d0` (invoked by resize when capacity is exceeded)
**reallocates and frees the old buffer**, so any cached
`*(qm+0x755c)`/`+0x7560` pointers (e.g. the renderer's per-frame reads,
or our own loop) become dangling for that frame. Mitigations:
- Do the append **once, right after spawn, on the main game thread
  tick** (the same thread FUN_00475680 runs on) — never from a render
  callback or worker.
- **Re-read `DLG_BEGIN` AFTER the resize call** before computing the
  element pointer (the code above does this).
- Prefer reusing an existing entry (scan by handle first) so most calls
  do NOT grow the vector.
- The engine itself grows this vector during normal play (every map
  load via FUN_0046f9b0 / replay of StartCode tag-0x28), and the
  renderer tolerates count changes between frames, so a single
  main-thread append between frames is safe.
- After append, `cCreature+0x245` (not a vector pointer) is what the
  renderer dereferences plus a fresh `*(qm+0x755c)+idx*0x50` each frame
  — stable as long as we don't shrink/reorder the vector afterwards.
  Never remove DlgNPC entries at runtime (no engine path does).

## Files / VAs added this pass
- decompiled: `00475680` (case 0x28 = DlgNPC create), `0045f220`
  (net/save 0x28 twin), `004bb1d0` (vector realloc-grow), `004bb380`
  (vector resize), `00807e90` (vector insert n), `005498f0`
  (**cCreature+0x245 stamp — linchpin**), `00465220` (entry+0x4c bind),
  `00463240` (full talk binder, sets +0x14|0x80000), `00859690`
  (name strcmp → name@+4), `0048f9e0`/`00491170` (tag 0x1f/6 binders,
  lookup-only), `004a1a50` (tag 0x56 SetIcon, entry+0x48 only),
  `00499e90`/`00549920`/`00599910`/`00428460` (render chain),
  `0046f9b0` (quest-mgr state loader/replayer), `007e69b0`/`007e87f0`
  (DAT_00ab44e8 save/load — not needed when +0x245 set).
- scratch: `dlgnpc28_scan.py` (tag-0x28 catalog: 16 460 StartCode.bin
  records, locks the 0x50 field map).
