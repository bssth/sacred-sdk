# Quest storyline subsystems — A) quest lifecycle, B) NPC display name,
#                               C) overhead "?!" quest-giver marker
# 2026-05-16

Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000,
no ASLR (file off = VA-0x400000). cQuestMgr singleton (runtime-verified in
`logs/sdk_loaded.log`) = **0x00AACF80** (= ECX/`in_ECX` in all quest
handlers). Object manager singleton = **`*(void**)0x00AD5C40`**;
`arr=*(om+4)`, `creature=*(arr+handle*4)` (from runtime_spawn.md /
quest_polish.md, runtime-confirmed).

This report builds on (does NOT repeat) `quest_lifecycle.md`,
`quest_fanfare.md`, `quest_marker_pos.md`, `quest_polish.md`,
`questbook_render.md`, `questbook_resolver.md`, `questbook_recon.md`,
`questbook_inserter.md`, `npc_model.md`. New RE this pass: the overhead
marker renderer + selector (C) and the two NPC-name arrays (B).

---

## The three quest-NPC arrays on cQuestMgr (key to B and C)

| array | begin | end | stride | entry layout (verified) | role |
|---|---|---|---|---|---|
| **NameArrA** | `qm+0x358` | `qm+0x35c` | **0x44** | `[+0]=creature handle`, `[+4..+0x43]=ASCIIZ name` | CreateNPC name buffer (opcode 0x01/0x05) |
| **DlgNPC**  | `qm+0x755c`| `qm+0x7560`| **0x50** | `[+0]=NPC obj handle`, `[+4..]=ASCIIZ name`, **`[+0x48]=overhead marker sprite id`**, `[+0x4c]=state u32` | dialog/quest-NPC registry (drives the "?!" icon) |
| QstNpcIdx   | `qm+0x31c` | `qm+0x320` | 0x34 | `[+0x14]=flag`, `[+0x1c..+0x20]=obj-handle subvector` | QUESTNPC/DLGNPC token resolver scratch (npc_model.md) |
| (save copy) | `qm+0x7520`| `qm+0x7524`| 0x44 | mirror of NameArrA, serialized by FUN_00465690 | savegame only |

Count of any vector = `(*end - *begin)/stride`. Evidence:
- NameArrA push (CreateNPC): `00482510` L1231-1276 — memcpy name into
  `auStack_4d8`, then `auStack_4d8[0]=local_68c` (the creature handle
  from `cObjectManager_create_005fba40`, L902), then 0x11-dword (0x44 B)
  copy into `*(qm+0x35c)` (the end ptr, advanced by `+0x11`). HIGH.
- DlgNPC layout: lookup-by-name = `FUN_00859690(name, qm+0x755c +4 +i*0x50)`
  (strcmp on `entry+4`) — `00482510` L409/L415, `00491170` L104,
  `0048f9e0` L55, `004a7760` L66, `00465280` L87. Lookup-by-handle =
  `*(entry+0)==handle` — `00482510` L457, `00491170` L138,
  `00465280` L121. Marker sprite = `entry+0x48` (FUN_00499e90 L31,
  written by FUN_004a1a50 L101). State = `entry+0x4c` (cleared by many
  handlers). HIGH.

---

# A) QUEST LIFECYCLE  (already solved — pointers only)

Fully covered by the prior reports; nothing changed. Summary of the
runtime recipe (all HIGH unless noted):

**Registry** = global vector `DAT_00aad3a4`(begin)/`DAT_00aad3a8`(end),
stride **0x174**, key `quest_id` @ `entry+0x08`. Same memory as
`qm+0x424/+0x428`.

```c
// --- create a registry entry at runtime (questbook_inserter.md) ---
typedef void (__thiscall *resize_t)(void* thisVec, unsigned n); // FUN_004b5370
#define QREG_BEGIN (*(uint8_t**)0x00AAD3A4)
#define QREG_END   (*(uint8_t**)0x00AAD3A8)
static inline unsigned qreg_count(){return (unsigned)((QREG_END-QREG_BEGIN)/0x174);}
uint8_t* register_quest(unsigned quest_id, const char* title_res /* no "res:" */){
    unsigned idx = qreg_count();
    ((resize_t)0x004B5370)((void*)0x00AAD3A4, idx+1);   // ECX=&vector(0x00AAD3A4)
    uint8_t* e = QREG_BEGIN + idx*0x174;
    *(uint32_t*)(e+0x08) = quest_id;                     // scan key
    // render gate (questbook_render.md): +0x24 must be a resolved handle
    typedef unsigned (__cdecl *qbr_t)(const char*);      // FUN_00672740
    *(uint32_t*)(e+0x24) = ((qbr_t)0x00672740)(title_res); // hash|0x80000000 or 0
    *(uint16_t*)(e+0x16C)= 0;        // journal page (0 = first tab)
    *(uint32_t*)(e+0x04) = 1;        // type 1 = normal (100 = side-quest)
    return e;
}
// --- set journal text / state ---
// log line 0 (render gate):       *(u32*)(e+0x24) = qb_resolve("MYQUEST_LINE1");
// log lines 1..10:                *(u32*)(e+0x28 + k*4) = qb_resolve(...);
// step open/done bullet:          *(u8 *)(e+0x0C) &= ~1;  // open ; |=1 done
// "completed/greyed" (stays):     *(u32*)(e+0x00) = 3;     // §3a quest_lifecycle
// side-quest row style:           *(u32*)(e+0x04) = 100;   // quest_polish Q3
// --- mark solved (vanish + banner + chime) ---
//   call tag-0x4d handler FUN_0048e600 (__thiscall, ECX=0x00AACF80) with a
//   tag-0x4d FunkCode record; it pops the 0x174 entry + emits 0x1ba(journal
//   refresh) + 0x1bd(fanfare). Reward = scripted (no engine quest-reward
//   field). Full recipe + fallback in quest_fanfare.md. HIGH.
```
`qb_resolve` = `(uint(__cdecl*)(const char*))0x00672740`, returns
`hash31(name)|0x80000000`, or 0 if the name isn't a loaded resource
(then the journal row stays hidden). Pass the bare name (strip `res:`).

Quest↔NPC binding: a creature is "the quest NPC" when
`*(i16*)(cCreature+0x96) == DAT_00aacf98` (active quest id @ 0x00AACF98).
`+0x96` is stamped by the quest-context path, not a plain CreateNPC
field (npc_model.md). For a fully runtime NPC just write it directly:
`*(int16_t*)(creature+0x96) = quest_id;` (MED — confirm with BP that no
consumer needs the +0x31c subvector entry too).

---

# B) CUSTOM NPC DISPLAY NAME  (the known hard item)

### What CreateNPC does with the name (HIGH)

`FUN_00482510` (tag 0x01) creates the cCreature via
`cObjectManager_create_005fba40` → handle in `local_68c` (L902). The
name string (opcode `0x05`→`acStack_214`, or `0x01`) is **NOT written
onto the cCreature struct**. Instead it is pushed as a **0x44-byte
record into NameArrA** at `qm+0x35c`:

```
00482510 L1231-1276:
  memcpy(auStack_4d8 /*0x44 bytes*/, <name cstr>);
  auStack_4d8[0] = local_68c;                 // overwrite dword0 = handle
  // append the 0x44-byte record to the std::vector at (qm+0x358..+0x360):
  end = *(qm+0x35c); ... copy 0x11 dwords ... *(qm+0x35c) = end + 0x11;
```

So **NameArrA entry = {u32 creature_handle; char name[0x40];}**, stride
0x44, begin `qm+0x358`, end `qm+0x35c`. The dialog/quest system's parallel
**DlgNPC** array (`qm+0x755c`, stride 0x50) also carries the name at
`entry+4` and is the one the dialog window / QUESTNPC token consult
(strcmp `entry+4`, lookup `entry+0`==handle — see array table above).

### Which one the on-screen name renderer reads — status

The overhead **creature UI render** is `FUN_00599910` (entry 0x00599910,
ECX=cCreature; spans 0x599910-0x59a3d0). It draws the **healthbar**
(HP via `FUN_00428ce0` @0x599d12, colour ramp 0x599c-0x599e) and the
**overhead marker** (see C), but it does **NOT** draw the floating
text name — the name is a separate **tooltip/hover** draw, which was
not isolated to a single function in this static pass (no sprintf
"%s name" format; Sacred draws it directly from the object's name
accessor). This remains the open sub-item.

What is certain (HIGH):
- The only runtime-writable name strings tied to a spawned creature
  are **NameArrA `entry+4`** (written by CreateNPC for the spawn's own
  handle) and **DlgNPC `entry+4`** (the dialog-NPC registry). Both are
  keyed by the creature handle at `entry+0`.
- Generic creatures with no array entry display their **type name**
  (Balance.bin / `TYPE_NPC_*` resource string), not an array string.
- For a quest NPC the engine's own dialog/QUESTNPC code reads the name
  from DlgNPC `entry+4` (strcmp at `00482510:409`, `00491170:104`,
  `00465280:87`, `0048f9e0:55`, `004a7760:66`). This is the
  authoritative "this NPC's name" string the quest/dialog UI uses.

### Runtime recipe — set_npc_display_name(handle, str)

Write the name into **both** name arrays for that handle (whichever the
hover renderer reads, it is covered; harmless to set both). Find the
entry by scanning for `entry[0]==handle`, exactly as the engine does
(`00482510:457` / `00491170:138`), and overwrite the in-place name
buffer (do not realloc — names are fixed-size inside the record):

```c
#define QM 0x00AACF80
static void set_npc_display_name(uint32_t handle, const char* name){
    // ---- DlgNPC array (qm+0x755c, stride 0x50, name@+4) ----
    uint8_t* b = *(uint8_t**)(QM + 0x755c);
    uint8_t* e = *(uint8_t**)(QM + 0x7560);
    for (uint8_t* p=b; p+0x50<=e; p+=0x50)
        if (*(uint32_t*)p == handle){
            strncpy((char*)(p+4), name, 0x44);   // entry+4 = ASCIIZ name
            ((char*)(p+4))[0x43]=0;
            break;
        }
    // ---- NameArrA array (qm+0x358, stride 0x44, name@+4) ----
    uint8_t* b2 = *(uint8_t**)(QM + 0x358);
    uint8_t* e2 = *(uint8_t**)(QM + 0x35c);
    for (uint8_t* p=b2; p+0x44<=e2; p+=0x44)
        if (*(uint32_t*)p == handle){
            strncpy((char*)(p+4), name, 0x40);
            ((char*)(p+4))[0x3f]=0;
            break;
        }
}
```
Confidence: array offsets & handle key **HIGH** (multiple independent
decompiled lookups agree). Which array the *hover/tooltip* renderer
samples — **MEDIUM** (write both; or BP-confirm, below).

If the spawn has **no DlgNPC entry yet** (CreateNPC only makes a
NameArrA entry; DlgNPC entries are created by the dialog-definition
tag), give it a unique CreateNPC name (opcode `0x01`) and emit a dialog
record referencing that name so the engine creates the DlgNPC entry —
then the name shows in dialog/tooltip. The pure-runtime alternative is
to append a DlgNPC entry yourself: grow the `qm+0x755c` std::vector by
one 0x50 record, set `[+0]=handle`, `strncpy([+4],name)`, zero the
rest, advance `*(qm+0x7560)+=0x50`. (Mirror of the inserter pattern;
MED — the DlgNPC element ctor/vtable not reversed this pass, so prefer
strncpy-into-existing-entry over synthesising a new one.)

### Needs a runtime breakpoint (to close B exactly)
- Hover an NPC and BP-trace the text draw: set a HW-read BP on the
  bytes of a known DlgNPC `entry+4` (locate it via `entry[0]==handle`)
  while the NPC's name is on screen. The function that reads it is the
  display-name renderer; pin its VA. Equivalently BP `0x00428ce0`
  (HP getter, fires every frame for the on-screen NPC) inside
  `FUN_00599910` and step to the adjacent tooltip call to find the
  name source. This single capture turns B from MEDIUM to HIGH.

---

# C) OVERHEAD "?!" QUEST-GIVER MARKER

> **CORRECTION (2026-05-16):** the "0x22 = ?! quest-giver" conclusion
> below is **WRONG**. 0x22 `npc_dialog_combo.tga` is the combat-arts-
> MASTER class/trainer icon and `+0x200|=0x4000` causes the red flame
> aura. The real story marker is SetIcon value **0x0b →
> NPC_DIALOG_02.TGA** via a DlgNPC entry. See the authoritative
> **"## Red-FX side-effect analysis (2026-05-16)"** section at the end
> of this file. The text below is kept only for the renderer/selector
> mechanics (which are correct); ignore its sprite-id/recipe claims.

### Renderer: `FUN_00599910` @ 0x00599910 (__thiscall, ECX=cCreature)

The per-creature overhead UI pass. Note Ghidra renders the cCreature as
`int in_ECX[]`, so `in_ECX[N]` = byte offset `N*4`:
`in_ECX[5]`=+0x14 (flags), `in_ECX[0x7d]`=**+0x1F4** (faction/side word,
== npc_model.md's faction field), `in_ECX[0x10]`=+0x40 /
`in_ECX[0x11]`=+0x44 (screen XY), `+0x261`=u16 type/anim index.

Marker block (decompile L243-271, disasm 0x599f2e-0x599fce):

```
L243  if ((creature[+0x14] & 0x80000) == 0) {            // NOT "always-show"
        if (FUN_00428460()) goto FORCE;                  // engine predicate
        if (!(GetAsyncKeyState(0x12) & 0x8000)) -> NO_MARKER;   // ALT held?
        if ((creature[+0x1F4] & 0x1006dcf8) == 0) -> NO_MARKER;  // faction gate
        if (FUN_00426a30()) -> NO_MARKER;
        v = *(u32*)(0x00959830 + creature[+0x261]*4);
        if (v < 1 || v > 0x5a) -> NO_MARKER;
      } else {                                           // +0x14 bit 0x80000 set
   FORCE:
        objIdx = FUN_00549920();                         // 0x00549920 (ECX=creature)
        sprite = FUN_00499e90(objIdx, creature);          // 0x00499E90 (ECX=qm)
      }
      FUN_00401660(...screen XY from +0x40/+0x44...);
      FUN_004090d0(sprite, ...);                          // 0x004090D0 draw sprite
   NO_MARKER:
      if (creature[+0x1F4] & 0x40000000) FUN_0040a690();  // (the "placed" extra blip)
```

So the marker is shown when **`cCreature+0x14` bit `0x80000` is set**
(unconditional quest-marker), OR (player holds **ALT** AND
`cCreature+0x1F4 & 0x1006dcf8 != 0` AND a couple of predicates pass).
The simplest runtime trigger = set the `+0x14` bit `0x80000`.

### Sprite selector: `FUN_00499e90` @ 0x00499E90 (__thiscall, ECX=qm)

`undefined4 FUN_00499e90(uint objIdx, int creaturePtr)` — full body:
```
if (objIdx > 159999)            return 4;          // default / none
if (creaturePtr) {
   f = *(u32*)(creaturePtr + 0x200);               // cCreature flag word @ +0x200
   if (f & 0x1000)    return 0x20;                  // -> npc_dialog_smith.tga
   if (f & 0x2000)    return 0x21;                  // -> npc_dialog_trader.tga
   if (f & 0x4000)    return 0x22;                  // -> npc_dialog_combo.tga  ("?!")
   if (f & 0x4000000) return 0x23;                  // -> npc_dialog_trader.tga
}
if (objIdx < (qm+0x7560 - qm+0x755c)/0x50)
   return *(u32*)(qm+0x755c + objIdx*0x50 + 0x48);  // DlgNPC entry+0x48 = sprite id
return 0;
```
i.e. **the marker sprite is chosen by `cCreature+0x200` flag bits**
(priority: 0x1000/0x2000/0x4000/0x4000000), and otherwise by the
**DlgNPC `entry+0x48`** sprite id. Sprite ids map through the UI sprite
getter `FUN_004090d0` @0x004090D0 (case = sprite id):
`0x20 npc_dialog_smith.tga`, `0x21 npc_dialog_trader.tga`,
`0x22 npc_dialog_combo.tga`, `0x23 npc_dialog_trader.tga`,
`0x40-0x4a sim_talk_*.tga`, `0x4b sim_quest.tga` (the *minimap* quest
pin, not overhead). The classic "?!" quest-giver glyph = **case 0x22
`npc_dialog_combo.tga`** (combined exclam+question). HIGH (direct
decompile of FUN_00499e90 + the 004090d0 switch + the disasm of the
0x599f2e-0x599fce marker block).

`FUN_00549920` (0x00549920, ECX=creature) returns the DlgNPC/quest
object **index** for that creature (scans the `DAT_00ab44e8` 0x58-stride
table for `entry[0x54]==typeid && entry[0]==creature+0x10`,
returns `entry+0`); it is gated by the same `+0x1F4 & 0x1006dcf8 &&
!(+0x1F4 & 0x40000)` faction test and creature subtype
`+0x1F0 ∈ {3,7,8,0xc,0xe,0xf}` (matches npc_model.md +0x1F0 subtype /
+0x1F4 faction). The DlgNPC `entry+0x48` sprite is **set by the tag
handler `FUN_004a1a50` @ 0x004A1A50** (ECX=qm): it parses a FunkCode
stream, takes a value (opcode 0xb) and an optional name (opcode 1); if
no name it uses the "current NPC index" `*(int*)(qm+0xa458)` and writes
`*(qm+0x755c + idx*0x50 + 0x48) = value` (L101), then emits a 0x1c8
net/save event. With a name it strcmps DlgNPC `entry+4` to find idx.

### Runtime recipe — set_quest_giver_icon(handle, on)

Two independent ways; option 1 is the most robust (no DlgNPC entry
needed, drives the renderer's primary gate directly):

```c
// resolve cCreature* from a handle (om singleton path, runtime-proven)
static uint8_t* creature_of(uint32_t h){
    uint8_t* om = *(uint8_t**)0x00AD5C40;
    uint8_t* arr= *(uint8_t**)(om+4);
    return *(uint8_t**)(arr + h*4);
}
void set_quest_giver_icon(uint32_t handle, int on){
    uint8_t* c = creature_of(handle);
    if (!c) return;
    if (on){
        // (1) force the renderer to draw a marker unconditionally:
        *(uint32_t*)(c + 0x14)  |= 0x80000;        // FUN_00599910 L243 gate
        // (2) pick the "?!" combo glyph via the +0x200 flag the
        //     selector reads first (FUN_00499e90):
        *(uint32_t*)(c + 0x200) |= 0x4000;         // -> sprite 0x22 = npc_dialog_combo.tga
        // (faction word must let FUN_00549920 resolve an index; a
        //  spawned quest NPC normally already has +0x1F4 & 0x1006dcf8.)
    } else {
        *(uint32_t*)(c + 0x14)  &= ~0x80000;
        *(uint32_t*)(c + 0x200) &= ~(0x1000|0x2000|0x4000|0x4000000);
    }
}
// glyph choice: +0x200 bit 0x1000=smith, 0x2000=trader,
//               0x4000=combo "?!", 0x4000000=trader.
// Alternative (engine-faithful): instead of (2), set the DlgNPC
// entry's sprite field:  find idx with entry[0]==handle in the
// qm+0x755c/0x50 array, then *(u32*)(entry+0x48)=0x22;  (matches
// FUN_004a1a50's write).  Leave +0x200 clear so the selector falls
// through to entry+0x48.
```
Confidence: **HIGH** on the field map and the draw path. The exact
faction-bit subset that lets `FUN_00549920` return a valid index for a
*fully synthetic* runtime creature is **MEDIUM** — a vanilla-spawned
quest NPC already satisfies it (it has `+0x1F4 & 0x1006dcf8`); for a
hand-spawned monster you may also need a DlgNPC entry so
`FUN_00549920`'s `DAT_00ab44e8` table lookup hits. If the icon doesn't
appear, the `+0x14|0x80000` + `+0x200|0x4000` combo still forces
`FUN_00499e90` down the flag path (returns 0x22 from `creature+0x200`
*before* the index check), so option (1)+(2) is the safe choice.

### Needs a runtime breakpoint (confirm C end-to-end)
- BP `0x00599FA6` (`call FUN_00499e90`) on a vanilla quest giver:
  dump ECX(=0x00AACF80), the two args (objIdx, creature*), and EAX
  (sprite id) — expect 0x22 for a "?!" NPC. Then BP `0x00499E90`
  entry and read `creature+0x200` to confirm which bit vanilla sets.
- BP `0x004090D0` with sprite==0x22 to confirm it loads
  `npc_dialog_combo.tga` (cache global `DAT_00aa4368`).

---

## Files / VAs touched this pass

- `decompiled/00599910_FUN_00599910.c` — **creature overhead UI render**
  (healthbar + marker), entry 0x00599910, marker block L243-271.
- `decompiled/00499e90_FUN_00499e90.c` — **overhead marker sprite
  selector** (reads cCreature+0x200 flags / DlgNPC entry+0x48).
- `decompiled/004090d0_FUN_004090d0.c` — UI sprite getter; cases
  0x20-0x23 = `npc_dialog_{smith,trader,combo,trader}.tga`,
  0x4b = `sim_quest.tga` (minimap). Cache globals 0xAA4360-0xAA440C.
- `decompiled/00549920_FUN_00549920.c` — creature→quest-object-index
  (+0x1F4 faction gate, +0x1F0 subtype gate, DAT_00ab44e8 0x58-table).
- `decompiled/004a1a50_FUN_004a1a50.c` — **tag handler that SETS the
  marker**: writes DlgNPC `entry+0x48`, uses `qm+0xa458` current idx,
  emits 0x1c8 event.
- `decompiled/00482510_FUN_00482510.c` — CreateNPC; NameArrA push
  L1231-1276 (handle@+0, name@+4, stride 0x44, vec qm+0x358/+0x35c);
  DlgNPC lookups L405-462.
- `decompiled/0046cbe0_FUN_0046cbe0.c`, `00472bc0_FUN_00472bc0.c`,
  `00465280/00491170/0048f9e0/004a7760` — DlgNPC name(+4)/handle(+0)
  lookup confirmations.
- scratch scanners: `scan358b.py`, `scanabs.py` (array-ref sweeps).
- **NEW** `.claude/knowledge/re/seticon_scan.py` — walks all 20
  `bin/**/FunkCode.bin`, decodes the 18 082 tag-0x56 (SetIcon) records'
  op-0x0b operand → vanilla icon-value distribution (0x0b dominant =
  NPC_DIALOG_02.TGA marker; 0x0d/0x08 = clear). Evidence for §(c).
- `0048f9e0_FUN_0048f9e0.c` — tag-0x1f QUESTNPC/DlgNPC-bind: writes
  `entry+0x4c`=handle and sets `cCreature+0x14|0x80000` (L95/L186).
- `00482510_FUN_00482510.c` L1774-1797 — CreateNPC class-flag block:
  proves `+0x200` bits 0x1000/0x2000/0x4000/0x4000000 = smith/trader/
  combo/trader2 *class* identity (each also sets `+0x14|0x80000`).
- New helper script: `re/ghidra/DecompileForce.java` (creates a
  function at an undefined VA then decompiles — needed because
  FUN_00599910 was not auto-defined).

## Confidence summary

| Claim | Conf |
|---|---|
| A) registry/journal/solve/fanfare recipes | HIGH (prior reports, unchanged) |
| NameArrA = qm+0x358/0x44, `[0]=handle [4]=name` (CreateNPC) | HIGH |
| DlgNPC = qm+0x755c/0x50, `[0]=handle [4]=name [0x48]=sprite [0x4c]=state` | HIGH |
| B) set name = strncpy into entry+4 of both arrays, key entry[0]==handle | HIGH on offsets; MED on *which* array the hover renderer samples |
| C) renderer FUN_00599910, gate `cCreature+0x14 &0x80000` (or ALT+faction) | HIGH |
| C) sprite selector FUN_00499e90: `cCreature+0x200` bits → 0x20-0x23, else DlgNPC+0x48 | HIGH |
| C) "?!" = sprite 0x22 = npc_dialog_combo.tga | HIGH |
| C) set_quest_giver_icon = `+0x14|=0x80000; +0x200|=0x4000` | **WRONG — superseded, see Red-FX section below** |
| quest↔NPC bind via cCreature+0x96==active id | HIGH (npc_model.md); MED if NPC also needs +0x31c subvector |

---

## Red-FX side-effect analysis (2026-05-16)

In-game test showed the prior `npc_quest_icon` recipe (`+0x14|=0x80000;
+0x200|=0x4000`) produces the **combo-arts-master class icon** plus an
unwanted red/orange swirling flame aura. Root cause + the real
quest-giver marker, fully re-derived from the decompiled engine:

### (a) The red-FX culprit — DO NOT write cCreature+0x200

**`cCreature+0x200` bits 0x1000/0x2000/0x4000/0x4000000 are NPC *class*
identity flags, not icon hints.** Decisive evidence in CreateNPC
`FUN_00482510` (`re/ghidra/decompiled/00482510_FUN_00482510.c`),
where `pTVar12`=cCreature and `pTVar12[0x40].pVFTable` = byte **+0x200**,
`pTVar12[2].spare` = byte **+0x14**:

```
L1774  if (local_6e0 & 0x10) { +0x200 |= 0x1000;    +0x14 |= 0x80000; }  // SMITH class
L1779  if (local_6e0 & 0x04) { +0x200 |= 0x2000;    +0x14 |= 0x80000; }  // TRADER class
L1784  if (local_6e0 & 0x08) { +0x200 |= 0x4000;    +0x14 |= 0x80000; }  // COMBO/combat-arts MASTER class
L1789  if (local_6e0 & 0x20) { +0x200 |= 0x4000000; +0x14 |= 0x80000;     // TRADER2 class
                               FUN_005498f0(); }
```

So `+0x200 |= 0x4000` makes the engine treat the creature as a
**MASTER_OF_COMBAT_ARTS** (combat-arts trainer) class NPC — the exact
same write CreateNPC performs for that class. The combo-trainer class
identity is what drives BOTH the `npc_dialog_combo.tga` icon (sprite
0x22, via `FUN_00499e90` L21-23: `if (+0x200 & 0x4000) return 0x22`)
AND that class's native presentation/aura FX = the observed red/orange
swirling flame aura. The MASTER_OF_COMBAT_ARTS creature type shows the
combo icon natively anyway, so the write was both wrong and the FX
cause. **The earlier `quest_storyline.md` claim "0x22 = '?!'
quest-giver glyph" is RETRACTED — 0x22 `npc_dialog_combo.tga` is the
combo-arts-master class/trainer icon, NOT the story quest marker.**

→ **Fix: never touch `cCreature+0x200`.** Leave all four class bits
clear so the selector falls through to the DlgNPC `entry+0x48` value.
(Confidence: **HIGH** — direct CreateNPC class-flag decompile +
FUN_00499e90 selector + in-game confirmation.)

### (b) Full `FUN_004090d0` sprite-id → .tga table (complete switch)

Entry `0x004090D0` (file `004090d0_FUN_004090d0.c`). **Guard L49:**
`if (param_5 != 8 && param_5 != 0xd) { …draw… }` — sprite ids **0x08
and 0x0D draw NOTHING** (function early-returns) = "no overhead
marker / clear". All decoded cases (string consts at exe .rdata
0x4e85xx-0x4e86xx, runtime cache globals 0xAA42xx-0xAA44xx):

| id | .tga | id | .tga | id | .tga |
|---|---|---|---|---|---|
| 0x00 | NPC_DIALOG_01.TGA | 0x40 | sim_talk.tga | 0x50 | sim_walk.tga |
| 0x01 | NPC_DIALOG_02.TGA | 0x41 | sim_talk_romance.tga | 0x51 | sim_nervous.tga |
| 0x04 | NPC_DIALOG_05.TGA | 0x42 | sim_talk_music.tga | 0x52 | sim_romance.tga |
| 0x05 | NPC_DIALOG_07.TGA | 0x43 | sim_talk_weather.tga | 0x53 | sim_animal_where.tga |
| **0x08** | **(none — guard)** | 0x44 | sim_talk_politics.tga | 0x54 | sim_visit.tga |
| 0x0a | NPC_DIALOG_01.TGA | 0x45 | sim_talk_money.tga | 0x55 | sim_going_home.tga |
| 0x0b | NPC_DIALOG_02.TGA | 0x46 | sim_talk_war.tga | 0x56 | sim_bbq.tga |
| **0x0d** | **(none — guard)** | 0x47 | sim_talk_gamble.tga | 0x57 | sim_door_locked.tga |
| 0x0e | NPC_DIALOG_05.TGA | 0x48 | sim_listen.tga | 0x58 | sim_door_wrong.tga |
| 0x0f | NPC_DIALOG_07.TGA | 0x49 | sim_fight.tga | 0x59 | sim_farm.tga |
| 0x20 | npc_dialog_smith.tga (SMITH class) | 0x4a | sim_guard.tga | 0x5a | sim_follow.tga |
| 0x21 | npc_dialog_trader.tga (TRADER class) | 0x4b | sim_quest.tga (**minimap pin, not overhead**) | 0x7e | (none — goto skip) |
| 0x22 | npc_dialog_combo.tga (COMBO/combat-arts-MASTER class) | 0x4c | sim_sleep.tga | 0x80 | sim_physical.tga |
| 0x23 | npc_dialog_trader.tga (TRADER2 class) | 0x4d | sim_panic.tga | 0x81 | sim_fire.tga |
| (default→0x7f) | NPC_DIALOG_02.TGA | 0x4e | sim_door_where.tga | 0x82 | sim_magic.tga |
| | | 0x4f | sim_door_panic.tga | 0x83 | sim_poison.tga |

There is **no dedicated "!"/"?"/"?!" overhead glyph** in this switch.
The story quest-giver marker is one of the **NPC_DIALOG_0N.TGA**
sprites (0x00/0x0a→01, 0x01/0x0b→02, 0x04/0x0e→05, 0x05/0x0f→07);
these are the generic dialog/story-NPC "talk to me" overhead bubbles.
(Confidence: **HIGH** — full switch decompile + exe string table
cross-check at 0x4e85ac-0x4e8618.)

### (c) The REAL vanilla quest-giver marker

**Catalog evidence (NEW scan).** `.claude/knowledge/re/seticon_scan.py`
walks all 20 `bin/**/FunkCode.bin`, isolates the 18 082 tag-0x56
(SetIcon / `FUN_004a1a50`) records, and decodes the embedded opcode
sub-stream's **opcode 0x0b** dword operand (the icon value;
`FUN_00472bc0` case 0xb L673-730: 4-byte dword → `*(qm+0xa860)`;
`FUN_004a1a50` L92/L101 writes it to DlgNPC `entry+0x48`). Result
(value : record count):

```
0x0b : 7290   -> NPC_DIALOG_02.TGA   (marker PRESENT — the quest "talk to me" bubble)
0x0d : 4331   -> (guard: NO DRAW)    (marker CLEARED)
0x0a : 3270   -> NPC_DIALOG_01.TGA   (alternate marker state)
0x08 : 2633   -> (guard: NO DRAW)    (marker CLEARED)
0x00 :  101   -> NPC_DIALOG_01.TGA
0x01 :   82   -> NPC_DIALOG_02.TGA
0x0e :   33   -> NPC_DIALOG_05.TGA
0x04 :   27   -> NPC_DIALOG_05.TGA
(the 0x40x sequential values are string-ref-path artifacts, not icons)
```

So the vanilla story quest-giver overhead marker = **SetIcon value
`0x0b`** → sprite 0x0b → **`NPC_DIALOG_02.TGA`**; quests clear it with
`0x0d` (or `0x08`). Context dumps (NetScript/FunkCode.bin) show the
0x56 record carries an optional op01 name (`ziel210`, `auftrag210`,
`NPC_Dialog_Novizin` — DlgNPC entry names / quest-marker labels) then
`00 0b <dword> 00…`; named form targets that DlgNPC entry, nameless
form (`00 0b 0d 00 00 00`) targets the *current* NPC index
`*(qm+0xa458)`. The marker is NOT a static class icon and NOT gated by
`+0x200`; it is the DlgNPC `entry+0x48` value, toggled 0x0b↔0x0d by
the quest's HQ_/NQ_ state nodes (`Qziel/Qoffen/Qend` blocks) as the
quest opens/closes.

**The field that drives it & the binding requirement.** The renderer
`FUN_00599910` only reaches the marker-draw (`LAB_00599f96` →
`objIdx=FUN_00549920(); sprite=FUN_00499e90(objIdx,creature);
FUN_004090d0(sprite)`) when **`cCreature+0x14 & 0x80000`** is set
(L243). With `+0x200` class bits clear, `FUN_00499e90` falls through
to **`return *(u32*)(qm+0x755c + objIdx*0x50 + 0x48)`** = the DlgNPC
entry's SetIcon value. The force bit `+0x14|0x80000` is **set
automatically by the QUESTNPC / DlgNPC-bind handler**, not by the
script directly:

- `FUN_0048f9e0` (tag **0x1f** "QUESTNPC"/Quest1f): finds the DlgNPC
  entry by name (`FUN_00859690` strcmp `entry+4`), writes the bound
  creature handle into DlgNPC **`entry+0x4c`**, and sets
  `cCreature+0x14 |= 0x80000` when bound (L95, L186), or clears it
  (`& 0xfff7ffff`) when unbound (L92). (file `0048f9e0_FUN_0048f9e0.c`)
- CreateNPC `FUN_00482510` also sets `+0x14|0x80000` for the class
  NPCs (smith/trader/combo) — that is the *class* path, not the
  story-quest path.

→ **The story quest "!" requires a DlgNPC entry** (qm+0x755c, stride
0x50) whose `entry+0x48` = 0x0b and whose `entry+0x4c` = the NPC
handle, **plus** `cCreature+0x14 |= 0x80000`. Our runtime-spawned NPCs
have **no DlgNPC entry** (CreateNPC tag 0x01 only makes a NameArrA
entry at qm+0x358; DlgNPC entries are made by the dialog-definition /
QUESTNPC tags). **Honest conclusion: you cannot get the engine-faithful
marker without a DlgNPC entry.** Two viable paths below.
(Confidence: **HIGH** — selector/renderer/setter/binder all decompiled;
catalog scan over 18 082 vanilla records.)

### (d) Corrected, side-effect-free recipe

```c
#define QM 0x00AACF80
static uint8_t* creature_of(uint32_t h){
    uint8_t* om = *(uint8_t**)0x00AD5C40;
    uint8_t* arr= *(uint8_t**)(om+4);
    return *(uint8_t**)(arr + h*4);
}

// PATH A (preferred, engine-faithful): synthesize a DlgNPC entry, bind
// it to the handle, set its icon, and force-draw. NO +0x200 write ->
// no class identity -> no red flame aura.
void npc_quest_icon(uint32_t handle, int on){
    uint8_t* c = creature_of(handle); if(!c) return;

    // 1) ensure a DlgNPC entry exists for this handle (scan entry[0]==handle)
    uint8_t* b = *(uint8_t**)(QM+0x755c);
    uint8_t* e = *(uint8_t**)(QM+0x7560);
    uint8_t* ent = 0; uint32_t idx = 0, i = 0;
    for (uint8_t* p=b; p+0x50<=e; p+=0x50, ++i)
        if (*(uint32_t*)p == handle){ ent=p; idx=i; break; }
    if (!ent){
        // append one 0x50 record (mirror of questbook_inserter grow
        // pattern). Element ctor/vtable NOT reversed -> zero-init is the
        // MED-confidence part; prefer giving the spawn a CreateNPC name
        // + a QUESTNPC(tag 0x1f) script record so the engine builds the
        // entry itself (then this whole block is unnecessary).
        idx = (uint32_t)((e-b)/0x50);
        // resize qm+0x755c vector by one (use the engine's vector-grow
        // helper as in register_quest; ECX=&vector at QM+0x755c), then:
        b = *(uint8_t**)(QM+0x755c); ent = b + idx*0x50;
        for (int k=0;k<0x50;k+=4) *(uint32_t*)(ent+k)=0;
    }
    *(uint32_t*)(ent+0x00) = handle;          // bound NPC handle
    *(uint32_t*)(ent+0x4c) = handle;          // state/bound-creature (set by tag 0x1f)
    *(uint32_t*)(ent+0x48) = on ? 0x0b : 0x0d;// 0x0b=NPC_DIALOG_02 (marker) ; 0x0d=clear

    // 2) force the renderer down the entry+0x48 path (the bit tag 0x1f
    //    sets automatically). NEVER write +0x200.
    if (on) *(uint32_t*)(c+0x14) |=  0x80000;
    else    *(uint32_t*)(c+0x14) &= ~0x80000;
}
// REMOVED vs old recipe: the `*(c+0x200) |= 0x4000` line. That line was
// the red-aura/combo-trainer bug. Do not reintroduce any +0x200 write.
```

**PATH B (most robust, zero RE risk — recommended):** give the spawn a
unique CreateNPC name (opcode 0x01) and emit a vanilla-shaped script
fragment: a `QUESTNPC`/DlgNPC definition + `SetIcon` (tag 0x56,
op0x0b=0x0b) referencing that name. The engine's tag-0x1f handler then
creates the DlgNPC entry, writes `entry+0x4c`, sets
`cCreature+0x14|0x80000` itself, and tag-0x56 writes `entry+0x48=0x0b`
— byte-identical to vanilla quest givers, with **no manual struct
pokes and no FX side-effects**. Toggle off by emitting SetIcon
op0x0b=0x0d. This is exactly how every shipped HQ_/NQ_ quest does it
(18 082 catalog records).

Caveats / open BP items (carried, unchanged in nature):
- The DlgNPC element layout beyond {+0,+4,+0x48,+0x4c} is not fully
  reversed → PATH A's hand-synthesised entry is **MED** (zeroed
  unknown fields may misbehave in dialog code). PATH B is **HIGH**.
- `FUN_00549920` (the `objIdx` arg) only matters for the DlgNPC
  fallthrough index; for PATH A it must resolve to `idx` — it scans
  the `DAT_00ab44e8` 0x58-table and is faction/subtype gated. If the
  marker doesn't appear for a synthetic NPC, that index lookup is the
  suspect → confirm with BP `0x00599FA6` (call FUN_00499e90): dump
  args + EAX, expect EAX=0x0b on a vanilla story giver, then BP
  `0x004090D0` to confirm `NPC_DIALOG_02.TGA` loads (cache
  `DAT_00aa42e4`/`DAT_00aa42e0`).

---

## Red-FX side-effect analysis (2026-05-16)

### TL;DR — the red swirling pillar is caused by `cCreature+0x14 |= 0x80000`

The unwanted fiery aura is **NOT** the marker sprite mis-selected. It is a
separate animated particle column drawn by the **3D model renderer
`FUN_0044b230`** because the very bit `npc_quest_icon` sets to gate the
overhead marker — **`cCreature+0x14` bit `0x80000`** — is *overloaded* and
in the model renderer means "draw the highlight/aura swirl". `+0x200`
bit `0x4000` is innocent (marker/minimap only). The fix is to **stop
writing `cCreature+0x14`** and drive the icon the way vanilla does:
via the **DlgNPC `entry+0x48`** sprite id (what `FUN_004a1a50` writes),
leaving the creature struct untouched.

### 1. The bit/field causing the red FX  (HIGH)

`cCreature+0x14` bit `0x80000` is read in **two** unrelated render paths:

- **`FUN_00599910` @0x00599910 L243** (overhead UI): `if ((in_ECX[5] &
  0x80000U) == 0)` — gates the "?!" marker draw. *(this is the wanted
  effect)*
- **`FUN_0044b230` @0x0044b230 L197** (per-creature **3D model** render,
  called from `FUN_00599910` L242 immediately before the marker block):

  ```
  0044b230 L195-211:
    FUN_0044a9d0(param_1,iVar12,iVar6,uVar15);          // normal model draw
    uVar11 = (undefined4)uVar15;
    if ((in_ECX[5] & 0x80000U) == 0) {                  // +0x14 bit 0x80000 CLEAR
        uVar11 = 0x44b8c4;
        cVar5 = FUN_00428460();                          // ALT-highlight predicate
        if ((cVar5 == '\0') || (piVar7 != (int *)0x0)) goto LAB_0044b947;  // skip aura
    }
    // --- reached ONLY when +0x14&0x80000 is SET (or ALT highlight active) ---
    fStack_14 = (float)in_ECX[0x37] + (float)in_ECX[0x3a];   // bbox extents
    fStack_10 = (float)in_ECX[0x38] + (float)in_ECX[0x3b];
    local_24  = ((float)in_ECX[0x36] + (float)in_ECX[0x39]) * 0.5;  // center
    ...
    FUN_004107c0(&local_24);
    FUN_00401660(param_1);
    FUN_00408b70(uVar11,iVar12,iVar6,uVar14);            // <-- THE SWIRL EMITTER
  ```

  When `+0x14 & 0x80000` is **set**, the `if` body is skipped, so the
  function does **not** take the `goto LAB_0044b947` early-out and
  unconditionally falls into the `FUN_00408b70` call.

**`FUN_00408b70` @0x00408b70 is the swirling-pillar particle emitter** (HIGH):
- a 5–8 iteration loop (`local_30`) computing per-particle XYZ from
  `fsin()` of a time accumulator (`local_40 = engine_ms + param_5`,
  `fVar2 = local_40 * 0.0005`) — i.e. an **animated swirl** of radius
  ~5 units about the creature bbox center `(param_2,param_3,param_4)`
  passed in from `FUN_0044b230`;
- binds **two** creature effect textures `FUN_0065ed90(*(cCreature+0xbc))`
  and `FUN_0065ed90(*(cCreature+0xc0))` and draws two layered
  `DrawPrimitive(5,0x1e2,...)` quad strips (additive billboard column).
  The colour/look comes from the creature's own +0xbc/+0xc0 effect
  textures, which for a synthetic/!immortal NPC render as the
  red/orange flame column observed.

Evidence VAs: `0044b230` L197 (`in_ECX[5] & 0x80000`), L211
(`FUN_00408b70` call); `00408b70` L194-260 (sin-swirl particle loop,
two-texture draw). Confidence **HIGH** (direct decompile of both).

### 2. Is the marker sprite mis-selected?  — NO  (HIGH)

`FUN_00499e90` returns **`0x22`** cleanly for `+0x200 & 0x4000`
(L21-23, before any objIdx/DlgNPC fallthrough), and `FUN_004090d0`
case `0x22` loads `npc_dialog_combo.tga` — the correct "?!" glyph.
A garbage `objIdx` from `FUN_00549920` is irrelevant here because the
`+0x200` flag check short-circuits *before* the objIdx range/DlgNPC
path. `FUN_00549920` returning junk (or its `0xd431` "NPCUWR speech"
side path) does not feed any FX. So the marker itself is correct; the
red column is a wholly separate draw in `FUN_0044b230`, not a
mis-scaled/mis-id'd marker sprite. The `0x80`/`0x81`(`sim_fire.tga`)…
cases in `FUN_004090d0` are *not* reachable from the marker selector
(it only ever returns 0/4/0x20-0x23 or a DlgNPC `entry+0x48` value).

`+0x200` bit `0x4000` is **marker-only / minimap-only** and safe:
its only readers are the selector `FUN_00499e90` (→ sprite 0x22) and
the minimap pin gate `FUN_006ccbd0` L1303 (`& 0x4007000` → draws the
quest dot on the radar — a *wanted* side effect). No particle/aura
path keys off `+0x200 & 0x4000`. Confidence **HIGH**.

### 3. What vanilla quest-givers actually do  (HIGH)

The vanilla marker tag handler **`FUN_004a1a50` @0x004A1A50**
(ECX=cQuestMgr) sets the icon by writing **only the DlgNPC
`entry+0x48` sprite id** and emitting a 0x1c8 net/save event:

```
004a1a50 L97-101 (no-name path):
  uVar7 = *(uint*)(qm+0xa458);                       // "current NPC idx"
  if (idx in range of qm+0x755c .. qm+0x7560 / 0x50)
     *(u32*)(uVar7*0x50 + 0x48 + *(qm+0x755c)) = unaff_EBP;   // DlgNPC entry+0x48 = sprite
004a1a50 L137-165 (by-name path): same write after strcmp on entry+4.
```

It **never** touches `cCreature+0x14` and **never** touches
`cCreature+0x200`. Real quest-givers therefore have `+0x14 & 0x80000`
== 0; their overhead icon is produced by the renderer's *other* gate
(`FUN_00428460()` returning true at `00599910` L245 — a per-handle
registry table at `param_1*0x80`, bit `0x80000`, populated by the
quest/dialog engine, **not** the creature flag) combined with the
DlgNPC `entry+0x48` sprite that `FUN_00499e90` returns when no
`+0x200` flag is set. That path does **not** trip `FUN_0044b230`'s
aura (which keys strictly on `cCreature+0x14 & 0x80000`).

### Corrected side-effect-free `npc_quest_icon` recipe

```c
// resolve cCreature* from a handle (om singleton path, runtime-proven)
static uint8_t* creature_of(uint32_t h){
    uint8_t* om = *(uint8_t**)0x00AD5C40;
    uint8_t* arr= *(uint8_t**)(om+4);
    return *(uint8_t**)(arr + h*4);
}
#define QM 0x00AACF80

// Show the overhead "?!" with NO red aura.
// Strategy: DO NOT touch cCreature+0x14 (that bit drives the
// FUN_0044b230 swirl). Set the "?!" sprite on the NPC's DlgNPC
// registry entry exactly like the vanilla handler FUN_004a1a50,
// and (optionally) set the marker-only +0x200 bit 0x4000 so the
// selector returns 0x22 even before the DlgNPC fallthrough.
static int npc_quest_icon(uint32_t handle, int on){
    uint8_t* c = creature_of(handle);
    if (!c) return 0;

    // 1) DlgNPC entry+0x48 = sprite 0x22 (npc_dialog_combo.tga = "?!").
    //    Find the entry by handle, same key the engine uses (entry[0]==handle).
    uint8_t* b = *(uint8_t**)(QM + 0x755c);
    uint8_t* e = *(uint8_t**)(QM + 0x7560);
    uint8_t* ent = 0;
    for (uint8_t* p=b; p+0x50<=e; p+=0x50)
        if (*(uint32_t*)p == handle){ ent = p; break; }
    if (ent) *(uint32_t*)(ent + 0x48) = on ? 0x22 : 0;   // 0x22 = combo "?!"

    // 2) marker-only flag so FUN_00499e90 returns 0x22 deterministically
    //    (also lights the minimap quest dot via FUN_006ccbd0 — desired).
    //    This bit has NO FX consumer (verified: only FUN_00499e90 +
    //    FUN_006ccbd0 read +0x200 & 0x4000).
    if (on) *(uint32_t*)(c + 0x200) |=  0x4000;
    else    *(uint32_t*)(c + 0x200) &= ~(0x1000|0x2000|0x4000|0x4000000);

    // 3) DO NOT WRITE cCreature+0x14.  Specifically NEVER do
    //    *(u32*)(c+0x14) |= 0x80000  — that is the bit FUN_0044b230
    //    L197 reads to draw the red swirling particle column
    //    (FUN_00408b70).  Removing this single write eliminates the FX.
    return 1;
}
```

**What to AVOID writing (the actual bug):**
`*(uint32_t*)(cCreature + 0x14) |= 0x80000;`  — drop this line entirely.
It was added to force `FUN_00599910`'s marker gate, but the same bit
forces `FUN_0044b230`'s aura swirl. The marker still appears without
it because (a) `+0x200 & 0x4000` makes `FUN_00499e90` return 0x22 and
(b) the renderer's non-`0x80000` branch still draws a marker when the
quest/dialog engine's `FUN_00428460` registry bit (or the
ALT+faction path) is satisfied for a registered DlgNPC quest NPC.

**If the marker fails to appear without `+0x14|0x80000`** (e.g. a
fully synthetic NPC with no DlgNPC entry and `FUN_00428460` registry
bit unset), the correct fix is to **register the NPC in the DlgNPC
array / quest system** (so vanilla's `FUN_00428460` gate lights up),
NOT to re-add the `+0x14` write. Cheapest alternative that still
avoids the FX: gate the marker via the renderer's other accepted
condition rather than `+0x14 & 0x80000`. Re-adding `+0x14|0x80000`
will always re-introduce the red column — they are the same bit.

Confidence: **HIGH** that `cCreature+0x14 & 0x80000` is the FX cause
(`FUN_0044b230` L197/L211 + `FUN_00408b70` decompile); **HIGH** that
`+0x200 & 0x4000` is FX-free; **HIGH** that vanilla (`FUN_004a1a50`)
sets only DlgNPC `entry+0x48`. **MED**: whether dropping `+0x14`
alone still yields the marker for a *fully synthetic* NPC depends on
its DlgNPC registration / `FUN_00428460` registry bit — confirm with
a BP on `0x00599F49` (`call FUN_00428460`) for the target NPC.

#### Files / VAs added this pass
- `decompiled/0044b230_FUN_0044b230.c` — creature 3D model render;
  **L197** `in_ECX[5]&0x80000` gate, **L211** `FUN_00408b70` aura call.
- `decompiled/00408b70_FUN_00408b70.c` — **swirling-pillar particle
  emitter** (sin-driven loop, two +0xbc/+0xc0 textures, DrawPrimitive).
- `decompiled/00428460_FUN_00428460.c` — per-handle registry bit
  `0x80000` predicate (`param_1*0x80+base`), vanilla marker gate.
- `decompiled/00549000_FUN_00549000.c`, `00549080_FUN_00549080.c` —
  +0x1F4&0x80000 secondary-blip helpers (unrelated to the red FX).
- `decompiled/004a1a50_FUN_004a1a50.c` (re-read) — vanilla marker
  handler writes ONLY DlgNPC `entry+0x48`; never +0x14/+0x200.
- evidence: `006ccbd0_FUN_006ccbd0.c` L1303 (`+0x200 & 0x4007000`
  minimap-pin gate — confirms +0x200&0x4000 is marker/minimap only).

---

## Thrall-glow vs marker — definitive (2026-05-16 pm)

### TL;DR (this SUPERSEDES every earlier red-FX attribution in this file)

The in-game A/B test (rebuild with the explicit `cCreature+0x14 |= 0x80000`
write removed) is ground truth and overturns the prior conclusions:

- **Marker disappeared** ⟹ `cCreature+0x14 & 0x80000` **is** the overhead
  marker gate (`FUN_00599910` L243 / disasm `0x599f2e`). Restored — keep it.
- **Glow persisted** ⟹ the glow is **NOT** `cCreature+0x14 & 0x80000`,
  **NOT** `FUN_00428460`, and **NOT** `FUN_00408b70` (the
  `FUN_0044b230` model-swirl). All three earlier hypotheses
  (afcf303b "Red-FX = +0x80000", the +0x200 combo-class theory, and the
  "FUN_0044b230 L197 swirl" theory) are **EMPIRICALLY WRONG** — every one
  of those paths shares the exact same `(+0x14&0x80000) || FUN_00428460()`
  gate the marker uses, so they would all have vanished with the marker.

**The thrall glow is `FUN_004087e0` @0x004087E0**, emitted from the
overhead-UI renderer **`FUN_00599910`** (NOT the model renderer
`FUN_0044b230`), in a code block far below the marker block and gated by a
completely independent predicate. Disasm-exact (`0x59a058`–`0x59a0b9`,
L303 path):

```
0059a058  test [ebp+0x1f4], esi      ; esi=0x80000  -> cCreature+0x1F4 & 0x80000
0059a05e  je   0x59a0be              ; bit clear -> NO glow
0059a060  mov ecx,ebp; call 0x549080 ; FUN_00549080: true iff cCreature+0x4d8 != 0
0059a067  test al,al; je 0x59a0be    ;            (and +0xfc!=9 && +0x150!=6)
0059a06b  call 0x7d84a0              ; local player/net object
0059a070  mov eax,[eax+0x14]         ; eax = local player handle
0059a073  mov ecx,ebp; push eax
0059a076  call 0x549000              ; FUN_00549000(playerHandle): faction-relation OK
0059a07b  test al,al; je 0x59a0be
... 0x59a0b9 call 0x4087e0           ; <-- THE THRALL AURA EMITTER
```

There is a second, equivalent entry to the same emitter at
`FUN_00599910` L282 / disasm `0x599fff`–`0x59a053`, gated by
**`cCreature+0xb0 & 0x400`** instead of `+0x1F4 & 0x80000` (same
`FUN_00549080` + `FUN_00549000` conjuncts, same `FUN_004087e0` call).

### Exact glow predicate (HIGH — full disasm of both paths)

The aura draws every frame iff **either** of:

1. `(cCreature+0x1F4 & 0x80000) != 0`  **AND** `cCreature+0x4d8 != 0`
   **AND** `cCreature+0xfc != 9` **AND** `cCreature+0x150 != 6`
   **AND** `FUN_00549000(localPlayerHandle)` (this creature is
   faction-related to the player — `FUN_00423580` on `+0xc`).
2. `(cCreature+0xb0 & 0x400) != 0`  **AND** (same `+0x4d8`/`+0xfc`/`+0x150`
   + `FUN_00549000`) conjuncts.

`FUN_00549080` @0x00549080 (4-branch leaf, disasm verified): returns 1
**iff** `+0xfc != 9 && +0x150 != 6 && +0x4d8 != 0`. `FUN_00549000`
@0x00549000: resolves the passed handle to a cCreature and returns 1 iff
that target is alive/!talking and `FUN_00423580` faction-relates it to
`this+0xc`. `FUN_004087e0` @0x004087E0 is the sin-driven billboard
column emitter (same family as `FUN_00408b70`, drawn at HP-bar height
`FUN_00428ce0()>>1`) — the **"creature is a bound thrall related to the
player" aura**. This is pixel-identical to the Vampiress raise-dead
column because raise-dead uses this very state.

Note `+0xfc==9` / `+0x150==6` is the *"currently in a live conversation"*
state (set by the dialog-start serializer `FUN_00465690` at `0x465ba7`/
`0x465bb7`); it **suppresses** the aura while the talk window is open.
The aura is the *idle, permanently-bound* state: `+0x4d8 != 0` (a
standing owner/relationship handle) with `+0xfc/+0x150` NOT in the
talk state. That is exactly a charmed/summoned/raised thrall.

### Which write in the bind path lights it — RANKED

The new evidence kills suspects #1 and #2 from the brief outright:

- **#1 DlgNPC `entry+0x4c=handle` / `FUN_00465220` — NOT the cause.**
  `entry+0x4c` (and `entry+0x44`) are pure dialog-conversation *state*
  fields. A full ref sweep of the 0x755c/0x50 array shows `entry+0x4c`
  is **only ever written, never read by any render/FX path** (readers:
  FUN_0048f9e0/00463240/00461540/00475680 — all bind/state, none
  render). It cannot gate `FUN_004087e0`. **Ruled out.**
- **#2 `cCreature+0x245` (`FUN_005498f0`) — NOT the cause.** Byte-scan
  of the whole `.text` for `[reg+0x245]`: the only render-relevant
  reader is `FUN_00549920` (the *marker* objIdx resolver, fast-path
  `return *(c+0x245)`). It feeds the **marker**, never `FUN_004087e0`.
  (`FUN_005498f0`'s only side effect besides `+0x245=idx` is
  *clearing* `+0x14&0x80000` when idx<1 — irrelevant to the aura.)
  **Ruled out.**
- **#3 faction `cCreature+0x1F4` — THE CAUSE, with the exact mechanism
  the brief asked us to verify.** `make_guard` writes `+0x1F4 = 1`
  (faction=ally) and does **not** glow because bit `0x80000` is clear
  and `+0x4d8 == 0`. The thrall/summon path additionally **ORs bit
  `0x80000` into `+0x1F4` on top of faction=1**, and sets the
  relationship handle `+0x4d8`. Decisive disasm in the engine's
  summon/recruit-to-team handler **`FUN_0047c610`** (entry 0x0047C610;
  the same function that sets `+0xb0|=0x400` and `+0xae|=0x40` for
  raised/special creatures — i.e. the raise-dead/charm constructor),
  at `0x47ea26`:

  ```
  0047ea26  mov  dword [edi+0x1f4], 1        ; faction = 1 (ally/player side)
  0047ea30  mov  edx,  [edi+0x1f4]           ; edx = 1
  0047ea39  or   edx, 0x80000                ; <-- sets the THRALL bit
  0047ea4c  mov  [edi+0x1f4], edx            ; cCreature+0x1F4 = 0x00080001
  0047ea52  call 0x564d60                    ; FUN_00564d60 = team/relationship register
  ```

  So **`cCreature+0x1F4` bit `0x80000` (on top of faction=1) is the
  single flag that produces the thrall aura**, exactly matching the L303
  gate `test [ebp+0x1f4], 0x80000`. The companion `+0x4d8` (owner /
  relationship handle, written e.g. at `0x465ba1`
  `mov [edi+0x4d8], esi`) must also be non-zero for `FUN_00549080` to
  pass; raise-dead/charm/recruit sets both. (The L282 alternate uses
  `+0xb0&0x400`, which `FUN_0047c610` *also* sets for raised undead —
  consistent with "pixel-identical to raise-dead".)
- **#4 `FUN_00408b70` model swirl — NOT the on-screen glow.** Re-derived
  exact (disasm `0x44b8ac`–`0x44b942`): reached iff
  `(cCreature+0x14 & 0x80000) || (FUN_00428460(cCreature+0x10) && edi==0)`,
  where `FUN_00428460` reads bit `0x80000` of
  `*( *(0x00AAB5E4) + handle*0x80 )` (ECX=`*0xAAB5E4`, the per-handle
  registry table — **identical** predicate to the marker's `FUN_00428460`
  fallback at `0x599f3e`). Since the marker vanished, that table bit is
  0 for our NPC, so this swirl is NOT drawn. The `FUN_0044a9d0`
  FX-RESSKIN tint is also not it: on the self-render path the engine
  passes `param_5 = 0` (`0x44b896`: `push 0`), so none of its
  `& 0x4000/0x2000/0x1000/0x800` colour overlays fire (those need the
  *other-object* path `piVar7!=0`, `+0xb0 & 0x40007800`).

The talk/quest DlgNPC binders themselves are clean: `FUN_00463240`
(L282) and `FUN_0048f9e0` (L95/L186) write **only** `cCreature+0x14
|= 0x80000` — they never touch `+0x1F4`, `+0x4d8`, or `+0xb0&0x400`.
**Conclusion: a pure engine DlgNPC bind does not glow; the glow is
introduced by the SDK's own faction/team ("ally"/immortal-passive)
write that mirrors `FUN_0047c610` — it sets `cCreature+0x1F4` with bit
`0x80000` set (and/or `+0xb0&0x400`) plus a non-zero `+0x4d8`.**

### Corrected dlgnpc_bind recipe (keeps marker + dialog, kills glow)

Confidence **HIGH** on the gate; the only knob is which SDK write sets
`+0x1F4&0x80000` / `+0x4d8` / `+0xb0&0x400`.

1. **KEEP** `cCreature+0x14 |= 0x80000` (restored) — it is the marker
   gate (`FUN_00599910` L243); removing it kills the marker. It does
   **not** affect the glow.
2. **KEEP** the DlgNPC entry + `entry+0x4c=handle` + `FUN_00465220` +
   `FUN_005498f0(+0x245=idx)` — none of these touch the aura gate;
   they drive the name + marker-objIdx and are required for the
   working dialog bind. `entry+0x4c` and `+0x245` are proven aura-inert.
3. **THE FIX — faction write:** set faction **without** bit `0x80000`.
   When the SDK marks the bound NPC as ally/non-hostile, write
   `cCreature+0x1F4 = 1` (or whatever pure faction value, per
   npc_model.md) **and explicitly mask off `0x80000`**:
   `*(u32*)(c+0x1F4) = (faction_value) & ~0x80000;`  Never OR in
   `0x80000`. Equivalent: after any faction/team helper, do
   `*(u32*)(c+0x1F4) &= ~0x80000;`.
4. **Also clear the L282 alternate and the relationship handle** to be
   safe (they are set only by the summon/raise path, so normally already
   0 for a plain spawn, but defensive):
   `*(u32*)(c+0xb0) &= ~0x400;`  and, only if your bind set it,
   `*(u32*)(c+0x4d8) = 0;`  (zeroing `+0x4d8` also makes `FUN_00549080`
   fail, which independently kills the aura even if a stray
   `+0x1F4&0x80000` survives — it is the cheapest single-line kill, but
   verify `+0x4d8` isn't needed by your follow/relationship behaviour).
5. Do **NOT** route the bound NPC through `FUN_0047c610` /
   `FUN_00564d60` summon-to-team; that path sets `+0x1F4=1|0x80000`
   + `+0xb0|0x400` and is the raise-dead constructor by design.

Minimal corrected core:

```c
// after spawn + DlgNPC bind + set_npc_name + marker (+0x14|0x80000 kept):
uint8_t* c = creature_of(handle);
*(uint32_t*)(c + 0x1F4) &= ~0x80000;   // <-- THE FIX: clear thrall bit
*(uint32_t*)(c + 0x14)  |=  0x80000;   // KEEP: marker gate (FUN_00599910 L243)
*(uint32_t*)(c + 0xb0)  &= ~0x400;     // defensive: L282 alternate gate
// (optional, only if your bind sets it) *(uint32_t*)(c+0x4d8) = 0;
// DlgNPC entry+0x4c=handle, +0x48=0x0b, FUN_005498f0(+0x245) — UNCHANGED.
```

### Cheap live BPs to confirm (1–2, pick first)

1. **BP `0x0059a058`** (`test [ebp+0x1f4], esi` in FUN_00599910) on the
   glowing NPC: dump `[ebp+0x1f4]` — expect bit `0x80000` SET (and
   `[ebp+0x4d8]!=0`). After applying the `&= ~0x80000` fix, the same BP
   should show the bit clear and the `je 0x59a0be` taken (no
   `0x4087e0` call). Single capture, definitive.
2. **BP `0x004087E0`** (aura emitter entry): if it fires for the bound
   NPC the glow is this function; read ECX (=cCreature), confirm
   `[ECX+0x1f4]&0x80000` or `[ECX+0xb0]&0x400`. Should NOT fire after
   the fix. (Pair with BP `0x005498F0` only to re-confirm `+0x245`
   is aura-inert if doubted.)

### Files / VAs this pass
- `decompiled/00599910_FUN_00599910.c` L282-323 (re-read) — the TWO
  `FUN_004087e0` thrall-aura blocks; disasm `0x599fff`-`0x59a0b9`.
- `decompiled/00549080_FUN_00549080.c` — aura conjunct: `+0x4d8!=0 &&
  +0xfc!=9 && +0x150!=6` (disasm 0x549080, leaf).
- `decompiled/00549000_FUN_00549000.c` — aura conjunct: target↔player
  faction relation (FUN_00423580 on +0xc).
- `decompiled/00465690_FUN_00465690.c` L2777-2781 / disasm 0x465ba1 —
  conversation-start sets `+0x4d8`, `+0xfc=9`, `+0x150=6`
  (talk state SUPPRESSES the aura; idle bound = aura).
- `0047c610_FUN_0047c610.c` / disasm **0x47ea26-0x47ea52** — the
  summon/raise/recruit constructor: `+0x1F4 = 1`, `or 0x80000`,
  `FUN_00564d60` team register; also `+0xb0|=0x400`, `+0xae|=0x40`
  (matched the "untot/raised" type-id branch). **The write that lights
  the thrall aura.**
- disasm **0x44b8ac-0x44b942** (FUN_0044b230) + `0x599f2e-0x599f49`
  (FUN_00599910) — proves the model-swirl `FUN_00408b70` and the
  marker share the identical `(+0x14&0x80000)||FUN_00428460()` gate
  (so neither is the persisting glow). `FUN_00428460` ECX = `*0xAAB5E4`,
  arg = `cCreature+0x10`, bit 0x80000, stride 0x80 (disasm 0x428460).
- scratch: ad-hoc `.text` byte-scanners for `[reg+0x245]`, `[reg+0x4d8]`,
  `[reg+0x1f4]` operands (ruled out #1/#2, located the #3 write).

### Confidence
| Claim | Conf |
|---|---|
| Glow = `FUN_004087e0`, gated in FUN_00599910 (NOT FUN_0044b230) | **HIGH** (full disasm both aura paths) |
| Gate = `+0x1F4&0x80000` (or `+0xb0&0x400`) && `+0x4d8!=0` && `+0xfc!=9` && `+0x150!=6` && FUN_00549000(player) | **HIGH** |
| `+0x14&0x80000` = marker gate only, NOT the glow | **HIGH** (in-game A/B + shared-gate disasm) |
| Suspects #1 (entry+0x4c) and #2 (+0x245) ruled out | **HIGH** (ref sweeps: write-only / marker-only) |
| The lighting write = `+0x1F4 |= 0x80000` (FUN_0047c610 0x47ea39 pattern), mirrored by SDK faction/ally code | **HIGH** for the engine path; **MED** that the SDK's exact line is the faction write (confirm with BP 0x59a058) |
| Fix = faction write `&= ~0x80000` (+ defensive `+0xb0&~0x400` / `+0x4d8=0`), keep `+0x14|0x80000` | **HIGH** |

---

## Thrall-glow vs marker — definitive (2026-05-16 pm)

This pass re-derived the FX gate from a *full* decompile of `FUN_0044b230`
(not the truncated quote prior reports relied on), traced the engine's
spell-summon dispatcher, and disassembled the exact gate bytes. **The
prior "vanilla quest-givers don't have +0x14&0x80000 so they don't glow"
claim is FALSE and is retracted.** New, decisive conclusions below.

### 0. Executive answer

- The red swirling pillar is `FUN_00408b70`, called **only** from
  `FUN_0044b230` L211. Its draw gate is, byte-exactly (disasm @
  `0x0044b8ac`): **`if (cCreature+0x14 & 0x80000) → DRAW unconditionally;
  else if (FUN_00428460(handle) && piVar7==0) → DRAW; else skip`.**
- This is the **same predicate** the overhead marker uses
  (`FUN_00599910` L243-246). **The marker and the glow are driven by the
  identical bit `cCreature+0x14 & 0x80000` (+ the same `FUN_00428460`
  registry fallback).** They are NOT separable by choosing a different
  gate — there is no "+0x14-free" marker path that vanilla uses; vanilla
  quest-givers set the SAME bit (proof in §3).
- Therefore the glow cannot be removed by toggling a different flag.
  It must be removed by **zeroing the swirl's textures `cCreature+0xbc`
  and `cCreature+0xc0`** (the only per-creature inputs to `FUN_00408b70`)
  OR by not setting `+0x14&0x80000` / not letting `FUN_00428460` be true
  (which also kills the marker — unacceptable). Zeroing +0xbc/+0xc0 is
  the recipe (§5).

### 1. The raise-dead / summon aura field  (VA evidence, HIGH)

There is no standalone "resurrect" function string-tagged; the Vampiress
raise-dead is one **case in the combat-art / spell-effect dispatcher
`cObjectFX_dopSystemAction` @ 0x005B2600** — a giant `switch` over
spell-action ids (0x30b, 0x364, 0x39b, 0x3ef, …) each calling
`cObjectManager_create_005fba40` to spawn the summoned/raised creature
(`decompiled/005b2600_cObjectFX_dopSystemAction_005b2600.c`).

- **The "raised/summoned thrall" flag = `cCreature+0x1F4` bit `0x80000`**
  (Ghidra `piVar21[0x7d]`, byte +0x1F4 = the faction/side word from
  `npc_model.md`). Set on the spawned minion: L384
  `*(uint*)(iVar11+500) |= 0x80000;` (500=0x1F4) in the type-0x91 summon
  branch; read as the "is-already-a-thrall / summon-cleanup &
  damage-immunity" guard at L2237 and L2260
  (`if ((piVar21[0x7d] & 0x80000) != 0) return;`). The summoned minion
  also inherits the caster's faction byte: case 0x39b L1270
  `*(char*)(piVar21+0x39) = (char)*(int*)(iVar19+0x14);` (owner-link).
- **But the *visible aura* the user sees is NOT keyed off +0x1F4&0x80000.**
  `FUN_0044b230` (the per-creature 3D model render) reads **only
  `in_ECX[5]` = `cCreature+0x14`** — it never reads +0x1F4 (grep of the
  full decompile: every `0x80000`/`0x*0000` test is `in_ECX[5]`,
  i.e. +0x14; zero `in_ECX[0x7d]` reads). So the swirl is gated by
  `+0x14&0x80000`, the *marker-force* bit.
- Why a Vampiress-raised corpse glows: the raise/animate path turns the
  corpse into an engine-controlled NPC and the engine sets the same
  `+0x14&0x80000` marker/force bit on it (the corpse becomes a "bound /
  engine-driven" creature). The column itself is **generic**:
  `FUN_00408b70` always textures from the creature's own
  `+0xbc/+0xc0` effect-texture slots and always draws the sin-swirl
  geometry — so it looks identical on ANY creature that reaches it
  (resurrected corpse, or our bound NPC). The user's "identical to the
  Vampiress thrall aura" observation is exactly right and confirms the
  column is the engine's generic bound-creature highlight, not a
  raise-specific FX. Confidence **HIGH** (full 0044b230 + 005b2600
  decompile; +0x1F4&0x80000 = summon flag is also HIGH but is a *red
  herring* for the visual — the visual is +0x14&0x80000).

### 2. Exhaustive gate for FUN_0044b230 → FUN_00408b70  (HIGH)

Full control flow (`decompiled/0044b230_FUN_0044b230.c` L143-212;
disasm `0x0044b8ac`):

```
... pass L143 (DAT_0182ebec / anim) and L149 FUN_00426610()==0 ...
LAB_0044b896:  (normal-NPC path, piVar7==0)
   FUN_0044a9d0(param_1, in_ECX[0x18], in_ECX[0x1a], in_ECX[0x1b]);  // model+skin overlays
0044b8ac:  test  dword [creature+0x14], 0x80000
0044b8b3:  jnz   0044b8d4         ; +0x14&0x80000 SET -> jump straight to swirl
0044b8b5:  mov   edx,[creature+0x10]          ; edx = creature handle
0044b8b8:  mov   ecx,[0x00AAB5E4]             ; ECX = global per-handle registry base
0044b8be:  push  edx
0044b8bf:  call  FUN_00428460                 ; -> registry[handle*0x80] bit 0x80000
0044b8c4:  test  al,al
0044b8c6:  jz    LAB_0044b947                 ; registry bit clear -> SKIP swirl
0044b8c8:  test  edi,edi                      ; edi = piVar7 (global picked obj)
0044b8ca:  jnz   LAB_0044b947                 ; piVar7 != 0 -> SKIP swirl
0044b8d4:  ... bbox center from creature+0xd8.. ; FUN_004107c0; FUN_00401660
           FUN_00408b70(param_1, .., .., CONCAT(creature+0xc *100, creature+0x6c))
```

**FUN_00408b70 (the red column) draws iff:**

> **`(cCreature+0x14 & 0x80000) != 0`  — unconditional (no piVar7 escape),**
> **OR  `FUN_00428460(handle) != 0`  AND  `piVar7 == 0`.**

where `FUN_00428460(h)` = `*( *(0x00AAB5E4) + h*0x80 ) >> 0x13 & 1`
(per-handle registry bit 0x80000; ECX=`*(0x00AAB5E4)`, h=`creature+0x10`),
and `piVar7` = the global *picked/hovered* object
(`FUN_0084a961(getData(FUN_00449920(0,cObject,cObject3D,0)))`) — the
same value for all creatures in a frame, NOT this creature's attachment.

The inner inputs to `FUN_00408b70` are **only**: `param_1` (render ctx),
`creature+0x18/+0x1a` (granny model handles), `creature+0x6c` (animation
phase, `local_40*0.0005`), and the two effect textures
`FUN_0065ed90(*(creature+0xbc))` / `FUN_0065ed90(*(creature+0xc0))`.
There is **no subtype / +0x1F0 / +0x1F4 / +0x200 / "summoned" predicate**
inside or guarding `FUN_00408b70` — the *only* per-creature gate is
`+0x14&0x80000` (or the registry bit). The column geometry
(`DrawPrimitive(5,0x1e2,…)`, two layered quad strips) is always emitted
when entered; the *textures* come from `+0xbc/+0xc0`.

**Precise field to neutralise the glow WITHOUT breaking the NPC:** zero
`cCreature+0xbc` and `cCreature+0xc0`. `FUN_0065ed90(0)` →
`if (0xffff < p) p=0; iVar5=*(ECX+0x1c+p*4); if (iVar5==0) … ` returns
a null/empty texture handle; `FUN_00408b70` then binds texture `0`
(`if (local_44==0) uVar4=0`) for both layers. The swirl quads are then
drawn with a null/empty texture → no visible flame column, while the
marker (separate sprite via `FUN_004090d0`) is unaffected and the NPC
behaves normally (+0xbc/+0xc0 are *only* read by FUN_00408b70 and the
`FUN_0044b230` aura path — they are the creature's "highlight/aura
effect texture" slots, not gameplay/model fields). Confidence **HIGH**
on the gate and the +0xbc/+0xc0→FUN_0065ed90→texture chain; **MED** that
zeroing them has zero other side-effects (one cheap BP confirms, §6).

### 3. Marker WITHOUT the glow — they are the SAME bit (HIGH)

Our current build does NOT write `+0x14|0x80000` explicitly, and
`FUN_005498f0(c, idx, 0)` stamps `cCreature+0x245 = idx` and **only
clears** `+0x14&0x80000` when `idx < 1` (`decompiled/005498f0`: 
`*(c+0x245)=idx; if(idx<1) *(c+0x14)&=0xfff7ffff;`). It never *sets*
0x80000. So with our path, if `idx>=1`, `+0x14&0x80000` is whatever it
already was (CreateNPC does NOT set it for a plain non-class spawn → it
is **clear**).

Trace of `FUN_00599910` marker gate (L240-271):
```
LAB_00599f23: FUN_0044b230();                  // <-- the glow happens HERE first
   if ((creature+0x14 & 0x80000) == 0) {
       if (FUN_00428460(handle) != 0) goto LAB_00599f96;   // registry path
       if (!(GetAsyncKeyState(0x12)&0x8000) || (creature+0x1F4 & 0x1006dcf8)==0) -> NO_MARKER;
       if (FUN_00426a30() || sprite_lut[creature+0x261] out of [1,0x5a]) -> NO_MARKER;
   } else { LAB_00599f96:
       objIdx = FUN_00549920();                 // returns *(creature+0x245) on fallback
       sprite = FUN_00499e90(objIdx, creature);  // -> DlgNPC entry+0x48
   }
   FUN_004090d0(sprite);                         // draw the ?!/bubble sprite
```

So the marker draws when **`+0x14&0x80000`** OR **`FUN_00428460(handle)`**
OR (ALT held + faction). **`FUN_00549920`'s `0 < *(creature+0x245)`
gate does NOT, by itself, draw the marker** — it only determines the
*sprite id* once the gate is already passed. The gate is the
`+0x14&0x80000` / `FUN_00428460` pair — i.e. **exactly the same two
conditions that draw the glow.** There is no marker path that does not
also satisfy the glow gate. Conclusion: **you cannot get the marker
glow-free by picking a different gate; the marker and the glow are the
same bit.** The only way to keep the marker and lose the glow is to
suppress the glow *at the FUN_00408b70 side* → zero `+0xbc/+0xc0`
(§2/§5). (Stamping `+0x245` via FUN_005498f0 is still required so
`FUN_00499e90`→`entry+0x48` yields the right sprite; keep it.)

Confidence **HIGH** (full FUN_00599910 + FUN_0044b230 + FUN_005498f0 +
FUN_00549920 + FUN_00499e90 decompiles; disasm of the shared gate).

### 4. Cross-check: vanilla quest-givers DO set +0x14&0x80000  (HIGH)

`FUN_0048f9e0` (tag **0x1f** QUESTNPC bind, the canonical vanilla
quest-NPC binder) explicitly does, on the bound creature `param_2`:
- L90/L183 `FUN_005498f0(uVar9,1)`  (stamp +0x245, MP-notify)
- L95/L186 **`*(uint*)(param_2+0x14) |= 0x80000;`**  when the matched
  DlgNPC index `uVar9 != 0`
- L92/L107 clears it (`& 0xfff7ffff`) only when `uVar9 == 0` or the
  dialog name isn't found.

So a **real vanilla quest-giver bound at DlgNPC index ≥ 1 carries
`cCreature+0x14 & 0x80000`** — identical to what we'd set — and by §2
its model render reaches `FUN_00408b70` every frame too. Vanilla
quest-givers therefore **also enter the swirl emitter**; they do not
visibly glow because their **`+0xbc/+0xc0` effect-texture slots are 0**
(generic townsfolk/quest NPC types have no aura texture assigned in
Balance.bin), so `FUN_00408b70` binds null textures and the column is
invisible. A *spawned monster type* (e.g. SKELETON/our test NPC) or a
Vampiress-raised corpse has non-zero `+0xbc/+0xc0` (creature-type FX
texture, e.g. its hit/aura skin), so the same code path renders a
visible red column. This fully reconciles the user's parallel: same
code, same bit, different `+0xbc/+0xc0` ⇒ visible vs invisible.
(`FUN_0048f9e0` L90-96/L182-188; `FUN_00408b70` L60-61/L244-258;
`FUN_0065ed90` null path. Catalog: 0048f9e0 is the only DlgNPC-bind
that touches +0x14. Confidence **HIGH** on the +0x14 write; **MED** on
"+0xbc/+0xc0 are 0 for vanilla quest NPC types" — BP-confirmable, §6.)

`FUN_0048f9e0/004a1a50/00463240` set `+0x14|0x80000` on the *bound
creature state*; `FUN_004a1a50` (tag 0x56 SetIcon) writes ONLY DlgNPC
`entry+0x48` and never +0x14 — consistent with the gate being the
bind-handler's job, the sprite being SetIcon's job.

### 5. Definitive corrected dlgnpc_bind recipe

KEEP (engine-faithful, all required for the marker):
- DlgNPC entry (PATH A/B from `dlgnpc_bind.md`): `entry+0=handle`,
  `entry+4=name`, `entry+0x48=0x0b` (sprite), `entry+0x4c=handle`.
- `FUN_005498f0(c, idx, 0)` → stamps `cCreature+0x245=idx` (drives
  `FUN_00549920`→`FUN_00499e90`→`entry+0x48` sprite). REQUIRED.
- `cCreature+0x14 |= 0x80000` (same write every vanilla bind does;
  required for the marker gate; `FUN_00428460` registry path is the
  only alternative and is not under our control).

ADD (the actual fix — kills the glow, keeps everything else):
```c
// After the bind, zero the two swirl effect-texture slots so
// FUN_00408b70 binds null textures (invisible column). These slots
// are read ONLY by the FUN_0044b230 aura path + FUN_00408b70.
*(uint32_t*)(c + 0xbc) = 0;
*(uint32_t*)(c + 0xc0) = 0;
```

DROP / NEVER write: `cCreature+0x200` (class bits → combo-trainer
identity + its own FX, the older bug — already removed, keep it out).
Do **not** rely on "not setting +0x14&0x80000" to avoid the glow: that
also removes the marker (they are the same gate, §3).

Note: if a later engine tick re-derives `+0xbc/+0xc0` from the
creature type (e.g. on anim/state change), re-zero them on our
per-tick NPC maintenance pass (same place we keep `+0x1F4` bit0 /
stance). MED — BP §6.2 tells us if a one-shot zero suffices.

### 6. Cheap live BPs to confirm (1-2)

1. **Glow gate / texture slots (the key one).** BP `0x0044b8ac`
   (`test [esi+0x14],0x80000` in FUN_0044b230) with the test NPC on
   screen. Confirm: ESI = our cCreature; `[ESI+0x14] & 0x80000` is set
   (proves we hit the unconditional swirl path), and read
   `*(uint*)(ESI+0xbc)` / `*(uint*)(ESI+0xc0)` **before** our fix
   (expect non-zero → why it glows) and **after** (expect 0 → no
   column). Single capture proves §2 + §5.
2. **Vanilla comparison.** Same BP `0x0044b8ac` on a real vanilla
   quest-giver (e.g. a town quest NPC with the ! marker): expect
   `[ESI+0x14]&0x80000` ALSO set (proves §3/§4 — same bit) but
   `*(ESI+0xbc)==0 && *(ESI+0xc0)==0` (proves the invisible-because-no-
   texture explanation). If vanilla's bit is clear instead, then its
   marker comes via `FUN_00428460` — BP `0x0044b8bf`
   (`call FUN_00428460`) to read EAX; either way the §5 +0xbc/+0xc0
   fix still holds (it suppresses the column for any gate).

### Files / VAs added this pass
- `decompiled/0044b230_FUN_0044b230.c` (FULL re-decompile) — swirl gate
  L197-211, disasm `0x0044b8ac` (the unconditional `+0x14&0x80000`
  jump + FUN_00428460/piVar7 fallback).
- `decompiled/00408b70_FUN_00408b70.c` — swirl emitter; textures
  `FUN_0065ed90(*(c+0xbc))`/`(*(c+0xc0))` L60-61, null-texture path
  L244-258; phase = c+0x6c.
- `decompiled/0044a9d0_FUN_0044a9d0.c` — model+overlay render;
  `+0x14&0x200000` (invuln) skips the FX_RESSKIN_TGA overlay block
  (L35); `FUN_0040a690` aura blips keyed off param_5 bits.
- `decompiled/005b2600_cObjectFX_dopSystemAction_005b2600.c` — spell
  /combat-art summon dispatcher; `+0x1F4|=0x80000` (summon/thrall flag)
  L384; +0x1F4&0x80000 read L2237/L2260; owner-faction copy +0x39
  L1270 (case 0x39b multi-summon).
- `decompiled/0065ed90_FUN_0065ed90.c` — per-creature effect-texture
  cache getter (param=+0xbc/+0xc0 id; 0 → null).
- `decompiled/00426610_FUN_00426610.c` — L149 gate (per-handle
  registry +0x2e byte ∈ {2,3,0x20}); `00428460` (registry bit 0x80000,
  ECX=*(0x00AAB5E4)); `00449920`/`0084a961` (global picked-obj +
  dynamic_cast → piVar7).
- `decompiled/00454420_FUN_00454420.c` — keyword→opcode token table
  (SetOwner→0x63, DaemonSummon→0x8d, IsGhost→0xa1; not an FX path).
- Confidence summary: gate identity (marker==glow==`+0x14&0x80000` /
  `FUN_00428460`) **HIGH**; fix = zero `+0xbc/+0xc0` **HIGH** on
  mechanism, **MED** on no-side-effect / persistence (BP §6).

---

## Captain glow (definitive) + yellow ?! marker (2026-05-16)

This pass RE-DERIVES both items from raw disasm with the new live
`[dump:CAP_now]` as ground truth, and OVERTURNS the immediately-preceding
"zero +0xbc/+0xc0" conclusion (it is wrong - see Q1.4).

Live dump (glowing captain, type 297): `+10=129 +14=40680000
+b0=0 +bc=0 +c0=0 +fc=0 +150=0 +1f0=7 +1f4=00000001 +200=0
+244=0007FD00 +4d8=0x20A`. Decoded: `+0x14` bit `0x80000` SET
(0x40680000>>19&1==1) AND bit `0x200000` SET (invuln). `+0x1f4 & 0x80000
== 0`. `+0xb0 & 0x400 == 0`. `+0xbc=+0xc0=0`. `+0x4d8=0x20A` (=522, the
level-20 HP, nonzero).

### Q1 - the glow: PROVEN

**Glow = `FUN_00408b70` @0x00408B70** (the additive-blended billboard
"swirl/pillar" emitter), **called from the model renderer
`FUN_0044b230` at `0x0044B942`**, **gated at `0x0044B8AC` solely by
`cCreature+0x14 & 0x80000`**. Exact disasm (ESI = cCreature):

```
0044b8a7  call 0x44a9d0                  ; FX-resskin (param5=0 on self, no tint)
0044b8ac  test dword [esi+0x14], 0x80000 ; <-- THE GATE
0044b8b3  jne  0x44b8cc                  ; bit SET -> ALWAYS draw the swirl
0044b8b5  mov  edx,[esi+0x10]            ; bit clear -> registry fallback:
0044b8b8  mov  ecx,[0xaab5e4]
0044b8bf  call 0x428460                 ; FUN_00428460(handle): per-handle tbl bit0x80000
0044b8c4  test al,al; je 0x44b947        ; reg 0 -> SKIP swirl
0044b8c8  test edi,edi; jne 0x44b947     ; edi(piVar7=selected obj)!=0 -> SKIP
0044b8cc  ... compute HP-bar-height pos ...
0044b942  call 0x408b70                 ; <-- THE GLOW (swirl emitter)
0044b947  (skip)
```

`FUN_00408b70` internals (decompiled): additive blend
(`FUN_00643430(1,1)` + `(0x800,1)` + `(8,1)`), reads its skin textures
`local_44=FUN_0065ed90(*(c+0xbc),0)` and `local_48=FUN_0065ed90(*(c+0xc0),0)`,
then **unconditionally** issues two `DrawPrimitive` billboard layers
(decomp L243-259). When `*(c+0xbc)==0` it sets texture handle = 0
(L244 `if(local_44==0) uVar4=0`) but **STILL DRAWS** - an untextured
additive quad with the hard-coded vertex-colour constants
(`&DAT_00ffe539/00ffd539/00bbbbbb...`) = a flat translucent coloured
column. **THIS is why `+0xbc=+0xc0=0` in the dump yet it still glows.**
The textures only *skin* the swirl; nulling them does NOT remove it
(refutes the previous section's "fix = zero +0xbc/+0xc0").

**`FUN_00408b70` has exactly ONE caller (0x0044B942) in the whole
.text** (verified by e8-rel scan) - it is *only ever* the
bound/charmed-creature model swirl. Nothing else uses it (HP bars,
marker sprite, FUN_004080c0 ally cone, FUN_004087e0 thrall column are
all separate code).

**The single differentiator captain(297, glow) vs guard(286, no glow):**
`cCreature+0x14 & 0x80000`. The captain gets it because it receives a
**DlgNPC dialog bind** (`dlgnpc_bind`); that bit is set both by the
SDK's explicit write (player_state.cpp `c+0x14 |= 0x80000`, the marker
gate) AND by the engine's own bind handlers `FUN_0048f9e0`(tag0x1f
L95/L186) / `FUN_00463240`(L282). Guards get NO dialog bind ->
`+0x14&0x80000` stays clear -> `FUN_0044b230` takes the
`je 0x44b947` skip -> no swirl. Type 297 has **no** intrinsic per-type
aura: the swirl gate is purely `+0x14&0x80000` / `FUN_00428460`, with
no creature-type-table FX emitter anywhere on this path; the
MASTER_OF_COMBAT_ARTS precedent was a `+0x200` class-bit effect, not
relevant here (`+0x200=0`). `set_invulnerable` (`+0x14|0x200000`) and
`set_stationary` (`+0x2B7|0x08`) are NOT read by the swirl path
(verified, irrelevant). `+0x4d8` / `+0x1f4&0x80000` / `+0xb0&0x400`
gate the OTHER two emitters (`FUN_004087e0` thrall column
@0x59a053/0x59a0b9, `FUN_004080c0` ally cone @0x599f1f) - those are NOT
lighting the captain (dump: `+0x1f4&0x80000=0`, `+0xb0&0x400=0`; the
SDK already clears both). **The visible captain glow is the
`FUN_00408b70` model swirl, gate `+0x14&0x80000`, full stop.**

Why the earlier in-game A/B ("removed the +0x14|0x80000 write, glow
persisted") was a FALSE NEGATIVE: removing only the *SDK's* redundant
write does not clear the bit, because the **engine's own DlgNPC bind
handler re-sets `+0x14|0x80000`** every bind (FUN_0048f9e0 L95/L186 /
FUN_00463240 L282). The bit was still set at render time -> swirl
stayed -> wrongly exonerated. (HIGH confidence: single-caller + exact
gate disasm + fully dump-consistent.)

#### Q1 - the exact fix

Marker (`FUN_00599910` LAB_00599f96, a *different* function) and the
swirl (`FUN_0044b230` 0x44b8ac) BOTH key on `+0x14&0x80000` (and the
same `FUN_00428460` registry fallback) for our NPC, so **they cannot be
separated by toggling that bit or by the registry-bit path** (verified:
in the bit-clear branch, registry-true still draws the swirl unless the
unrelated global `piVar7`!=0, which we cannot control).

The clean, lowest-risk fix - **neutralise `FUN_00408b70` (the swirl
emitter) at its entry**, since it has exactly one caller and is *only*
the bound/charmed swirl. Keeps invuln + stationary + name + overhead
marker + HP bar + the legit ally cone / thrall column (different
functions) fully intact; same class of change as the shipped
MASTER_OF_COMBAT_ARTS FX swap:

- Patch the `0x00408B70` prologue (`55 8B EC 81 EC 94 01 00 00`) to an
  early return. `FUN_00408b70` is `__thiscall`, `void(int*,float,
  float,float,int)` = **4 stack args**; this engine is callee-cleans,
  so write **`C2 10 00`** (`ret 0x10`) at file offset `0x00008B70`
  (.text raw = VA - 0x400000, no ASLR; VA 0x00408B70).
  Net: the swirl is never drawn anywhere; everything else unchanged.
  (If a live test shows a stack imbalance - engine actually
  caller-cleans this one - use `C3` (`ret`) instead. `ret 0x10` is the
  predicted-correct choice; 1 BP settles it, below.)
- There is NO script-only fix that keeps the engine marker: marker and
  swirl are gate-coupled through `+0x14&0x80000`. Script-side you could
  only either not dialog-bind (lose marker+name) or accept the glow.
  Hence the 3-byte exe patch is the recommended fix.

Confidence: glow identity + gate **HIGH** (single-caller, exact
disasm, dump-consistent, explains every prior falsification). Patch
byte (`ret 0x10` vs `ret`) **MED** - one cheap BP settles it.

### Q2 - the yellow "?!" secondary-quest marker: there is none in this engine

Full `FUN_004090d0` switch re-decompiled (all cases). The only combined
exclam+question glyph is **case `0x22` -> `npc_dialog_combo.tga`**
(cache `DAT_00aa4368`). BUT `0x22` is **NOT reachable via DlgNPC
`entry+0x48`** for a story NPC - it is emitted only by `FUN_00499e90`'s
`+0x200` CLASS-bits path (COMBO / combat-arts-master), i.e. exactly the
`+0x200` write that causes the red combo-master aura bug (already
banned). cases `0x08`/`0x0d` early-return (no draw = clear);
`0x0a/0x00`->NPC_DIALOG_01, `0x0b/0x01`->NPC_DIALOG_02,
default->NPC_DIALOG_02.

**NEW catalog cross-check** - HQ_/NQ_/GQ_/DQ_-classified SetIcon scan,
all 20 `bin/**/FunkCode.bin`, 18 082 tag-0x56 records, op-0xb operand =
the value written to `entry+0x48`, classed by the nearest preceding
quest-state token in the stream:

```
class  total   dominant entry+0x48 values
NQ_    6572    0x0d:4040  0x0b:2317  0x08:111  0x0a:93    (SECONDARY)
HQ_    1030    0x08:326   0x0b:124   0x00:83   0x01:80 0x0d:66 (MAIN)
GQ_     349    0x0d:214   0x0b:75    0x0e:33   0x08:18
DQ_   10131    0x0b:4774  0x0a:3168  0x08:2178 0x0d:11
```

**Result: vanilla SECONDARY (NQ_) quest-givers use the SAME sprite as
main quests - `entry+0x48 = 0x0b` (-> NPC_DIALOG_02.TGA) to SHOW the
marker, `0x0d`/`0x08` to CLEAR it.** There is **no NQ-specific value**
and **no `0x22`** anywhere in the per-NPC SetIcon stream of any quest
class. The engine does **not** differentiate main vs secondary with a
yellow "?!" overhead glyph - both are the same white NPC_DIALOG_02
"talk" bubble; the main/secondary distinction lives only in the
journal/quest-log category, not in the head sprite.

**Answer:** the correct `entry+0x48` to show the vanilla quest-giver
marker (the only one Sacred Gold has, used identically for NQ_ and HQ_)
is **`0x0b`**. The "in-progress" vs "has-quest" states are the SAME
sprite - vanilla only toggles `0x0b` (show) <-> `0x0d`/`0x08` (clear)
as the quest opens/closes; it never changes the glyph. A distinct
yellow "?!" combo glyph (`npc_dialog_combo.tga`, id 0x22) exists in the
atlas but is wired only to the combat-arts COMBO class via `+0x200` and
is unreachable / unsafe for a quest NPC (reintroduces the red aura).
**Keep `entry+0x48 = 0x0b`; a yellow "?!" is not an engine feature
here.** Confidence **HIGH** (full switch decompile + 18 082-record
class-filtered catalog; this scan and the older unfiltered
seticon_scan.py agree).

### Cheap in-game probes (SDK: npc_field_dump / scan_creatures / dump_vanilla_of)

1. **Zero-risk single-cause confirm (do this first):** BP `0x0044B8AC`;
   dump `[ESI+0x14]` for the glowing captain (expect bit 0x80000 SET)
   and for a guard (expect CLEAR). Proves the lone differentiator with
   no code change.
2. **Settle the patch + prove swirl==glow:** BP `0x0044B942`
   (`call 0x408b70`) on the captain -> confirm it FIRES. Apply the
   `ret 0x10` patch, reload: BP `0x0044B947` -> confirm ESP unchanged
   across the stubbed call and the glow is GONE while overhead marker +
   name + invuln still work. One capture each; definitive (if ESP is
   off by 0x10, switch the patch byte to `C3`).

---

## Runtime NPC display-name source (2026-05-16)

Closes the long-open MED in §B. The static result is **decisive and
negative for our prior recipe**: writing a raw string into DlgNPC
`entry+0x04` (or NameArrA `entry+0x04`) can *never* change the
nameplate, for two independent disasm-grounded reasons proven below.

### 1. The renderer reads the name from NONE of the name arrays

Full-binary `[reg+disp]` displacement scan (byte-accurate ModRM decode,
`scratch/disp_scan.py`) of every reference to the name stores:

| store | qm off | total `[reg+off]` refs | VA range of refs |
|---|---|---|---|
| DlgNPC vec begin | `+0x755c` | ~120 | **0x45f000–0x49fa64 only** |
| NameArrA begin    | `+0x358`  | (reg-rel) ~40 | **0x460000–0x4a76xx only** |
| NameArrA end      | `+0x35c`  | ~12 | 0x461000–0x4a75xx, 0x722ce6 |
| name scratch      | `+0xa460` | ~60 | 0x46xxxx–0x48exxx only |
| group reg         | `+0x31c`  | ~40 | 0x46xxxx–0x49xxxx, 0x6b6xxx (ctor) |

**Zero references in the render/UI range (0x59xxxx, 0x5axxxx,
0x42xxxx HUD).** The per-creature overhead pass **`FUN_00599910`**
(0x00599910, __thiscall ECX=cCreature; full disasm 0x599910–0x59a3d0
re-walked this pass) draws **only** the healthbar (`FUN_00428ce0` HP
getter @0x599d12, colour ramp) and the overhead marker (block
0x599f2e–0x599fce: `FUN_00549920`→`FUN_00499e90`→`FUN_004090d0`, exactly
as in dlgnpc_bind.md). **It contains no name/text draw and no name-array
read.** The nameplate/tooltip name is produced elsewhere, by an
accessor reached **via the `0x00AAB5E4` world-manager singleton and/or a
cCreature vtable call** — which is precisely why no static `[reg+off]`
displacement to the name stores exists for it (a `call [vt+N]` /
cached-`char*` read is invisible to a displacement scan).

Corollary, also proven: the unified res resolver `FUN_006726f0` has
**13 callers, all 0x45xxxx–0x47xxxx**; the raw dict lookups
`FUN_0080eaf0`/`FUN_0080e780` have **0 callers in render range**. So the
renderer does **not** resolve a res handle at draw time either — the
display string is resolved once (at spawn/register) and the renderer
reads a **cached pointer/handle**, not any array and not the resolver.

### 2. DlgNPC `entry+0x04` is a DIALOG KEY, not a display name

Catalogued all **16 460** vanilla tag-0x28 DlgNPC records from
`bin/**/StartCode.bin` (payload offset +1, stride 0x50). `entry+0x04`
strings are **dialog-script identifiers**: `dlg_UW12`,
`dlg_Hellseher9516`, `dlg_Auftrag9513`, `auftrag210`, `ziel210`,
`geschlossen101` … never a human-readable name. They are matched by the
case-insensitive strcmp `FUN_00859690` to *select which dialog plays*
(dlgnpc_bind.md / dialog_runtime.md). Writing "Captain Miles" here only
re-keys a dialog lookup; nothing renders it.

The **actual display name** comes from the **CreateNPC (tag 0x01)
opcode-0x01/0x05 name**, and that vanilla value is a **global.res key**,
not literal text. Sampled vanilla tag-0x01 records:
`01 "Res:Sam1_Knappe" …`, `01 "res:19588" …`, `01 "res:19589" …`.
`FUN_00482510` (decompile L1226–1276) copies that ASCIIZ verbatim into a
0x44 NameArrA record (`auStack_4d8`, `[0]=handle`, `[4..]=the res key`),
appended at `qm+0x35c`/`+0x360`. It does **not** call any resolver
itself (verified: no `0x672740/0x6726f0/0x80eaf0` call inside
0x482510) — the `res:`/`Res:` string is resolved to a localized display
string **later, by the name accessor the renderer calls**, via the
standard hash-or-ptr path `FUN_006726f0` =
`(h&0x80000000)?FUN_0080eaf0(h&0x7fffffff):FUN_0080e780(h)`
(questbook_resolver.md / dialog_runtime.md). Generic creatures with no
NameArrA entry fall back to the **type name** (Balance/`TYPE_*`
resource) — which is exactly the "default creature name" we keep seeing.

### Answers to the deliverable

1. **Renderer fn + field:** the nameplate/tooltip text is **NOT** drawn
   by `FUN_00599910` and is **NOT** read from DlgNPC `+0x04`, NameArrA
   `+0x04`, `qm+0x31c`, or `qm+0xa460` (all proven render-range-free).
   It is emitted by an accessor invoked through the `0x00AAB5E4`
   world-manager / a cCreature vtable slot, reading a **cached resolved
   string/handle** that originates from the **CreateNPC name
   (NameArrA `entry+0x04`, keyed by handle at `entry+0x00`, vector
   `qm+0x358`/`+0x35c`/`+0x360`, stride 0x44)**. Exact accessor VA is a
   vtable/cached read → must be pinned with the one HW BP below
   (structurally unfindable by static displacement scan; stating this
   honestly per project rule rather than guessing a VA).
2. **String type:** **global.res handle, NOT a raw char\***. Vanilla
   CreateNPC names are `res:NNNN`/`Res:NAME` keys; the engine resolves
   them through `FUN_00672740` (`hash31|0x80000000`, __cdecl, 1 arg) /
   `FUN_006726f0`. **Custom names MUST go through the SDK global.res
   text path** (`custom/lua/lib/text.lua`, format in
   globalres_format.md). A literal C string in any array will at best
   show as-is only if the resolver's "not a handle → treat as ptr"
   branch is hit — unreliable and not how vanilla works; do not rely on
   it.
3. **Minimal engine-faithful WRITE (recipe):** make the spawn's
   **CreateNPC name be a registered global.res key**, then ensure the
   per-handle NameArrA entry carries that key:
   - Authoring: register the display text in global.res via the SDK
     `text.lua` rebuild under a key, e.g. `MYNPC_CAPTAIN_MILES`
     (value = "Captain Miles"). Confirm `qb_resolve("MYNPC_CAPTAIN_
     MILES")` (`((unsigned(__cdecl*)(const char*))0x00672740)`) returns
     nonzero.
   - Spawn path: give the engine-CreateNPC the **name = that key**
     (CreateNPC tag 0x01 opcode 0x01 ASCIIZ = `"MYNPC_CAPTAIN_MILES"`,
     no `res:` prefix — the generic branch strips/handles it; pass the
     bare key). The engine writes it into NameArrA itself.
   - Pure-runtime alternative (no re-spawn): overwrite the NameArrA
     entry for the handle in place — base `*(char**)(qm+0x358)`, end
     `*(char**)(qm+0x35c)`, stride **0x44**, key `entry[0]==handle`,
     name at `entry+0x04` (size 0x40) — with the **key string**
     (`"MYNPC_CAPTAIN_MILES"`), not literal text. Code = the
     `set_npc_display_name` NameArrA loop already in §B (use it for the
     **NameArrA** array; the DlgNPC write in that helper is irrelevant
     to the nameplate and can be dropped).
   - **No engine helper does a clean "set display name" write.** The
     only writer is `FUN_00482510`'s inline NameArrA append (not
     callable in isolation; needs the full CreateNPC frame). So the
     SDK's own in-place NameArrA strncpy (key string) is the minimal
     faithful write; pair it with the global.res registration.
   - Do **not** also need DlgNPC `+0x04` for the *name* (that stays the
     dialog key from dlgnpc_bind.md). Marker/dialog recipe in
     dlgnpc_bind.md is unaffected and still correct.

### Confidence

| Item | Conf | Why |
|---|---|---|
| Renderer reads name from none of the qm name arrays / resolver | **HIGH** | byte-accurate full-.text disp scan: 0 refs in render range for `+0x755c/+0x358/+0x35c/+0xa460/+0x31c`; `FUN_006726f0` 13 callers all 0x45–0x47xxxx; `FUN_00599910` fully disasm'd = healthbar+marker only |
| DlgNPC `entry+0x04` = dialog key, not display name | **HIGH** | 16 460-record StartCode catalog: all `dlg_*`/`auftrag*` keys |
| Display name originates from CreateNPC name = global.res key (NameArrA `entry+0x04`, stride 0x44, vec qm+0x358) | **HIGH** | `FUN_00482510` L1226–1276 decompile + vanilla tag-0x01 samples `res:NNNN`/`Res:NAME`; matches questbook/dialog_runtime resolver convention |
| Custom name must go through global.res text path | **HIGH** | resolver returns 0 for unknown keys → fallback to type name (the bug we observe) |
| Exact accessor VA (the draw call) | **MED** | vtable/cached read, not statically pinnable by disp scan — settle with the BP below |
| In-place NameArrA write (key string) makes the name appear | **MED** | array+key+offset HIGH; that the renderer re-reads NameArrA *post-spawn* (vs caching only at create) needs the BP |

### One cheap live BP to close both MEDs (single capture)

HW-**read** BP on the bytes of a **vanilla quest NPC's NameArrA
`entry+0x04`** (find it: scan `*(qm+0x358)`, stride 0x44, for
`entry[0]==handle` of an on-screen named NPC like a quest giver) while
that NPC's nameplate/tooltip is on screen. The instruction that traps
is the display-name accessor — record its VA and the cCreature/vtable
offset it was reached through. Same BP after our in-place key write on
the synthetic NPC confirms (a) the renderer re-reads NameArrA at draw
time (turns the last MED → HIGH) and (b) it then funnels the key
through `FUN_006726f0` (set a 2nd BP at 0x006726F0 to see our resolved
handle). If the trap shows the value is read once at spawn and cached
(no per-frame re-read), the recipe becomes "set NameArrA key **before**
the engine finishes spawning" (CreateNPC-name path), which the recipe
already prefers.

### Files / VAs this pass
- scratch: `disp_scan.py` (byte-accurate ModRM disp scanner — supersedes
  the desync-prone linear-sweep scanners for whole-.text xrefs),
  `qm_xref.py`, `callers.py` (E8/E9 rel32 caller finder),
  `find_off.py`, `nm_find.py`.
- re-walked disasm: `FUN_00599910` (0x599910–0x59a3d0, name-draw-free
  confirmed), `FUN_00465220`, `FUN_0049f5a0`/`0049f750` (NameArrA/DlgNPC
  writers, not getters), `FUN_00428460`/`FUN_00426a30` (0xAAB5E4 marker
  gates, handle*0x80 flag table), `FUN_00603e30` (objmgr handle accessor).
- decompile (re-read): `00482510` L1226–1276 (NameArrA append = res key
  string, keyed by handle), L1500–1740 (`qm+0x31c` = group registry,
  NOT name).
- data: 16 460 StartCode tag-0x28 (DlgNPC names = dialog keys); vanilla
  tag-0x01 CreateNPC names = `res:NNNN`/`Res:NAME` global.res keys.

---

## Yellow "?!" marker — re-evaluated (2026-05-16)

Re-opened because the glow root-cause changed: the red column is now
PROVEN to be `FUN_00408b70` (model swirl) gated **solely** by
`cCreature+0x14 & 0x80000` / `FUN_00428460` registry (section "Captain
glow (definitive)", live dump `+0x200=0` yet glowing). It is **NOT**
`cCreature+0x200`. So every prior "0x22/+0x200 is banned because it
causes the red aura" line is RETRACTED as based on the disproven
`+0x200` combo-class glow theory. This pass re-derives the glyph chain
from the raw decompiles with that correction.

### 1. Full glyph-selection chain — every input enumerated

**Gate** (`FUN_00599910` L240-271, disasm 0x599f23-0x599fce): the
marker is DRAWN iff `(cCreature+0x14 & 0x80000) != 0` **OR**
`FUN_00428460(handle)` (per-handle registry `*(*0xAAB5E4)+h*0x80` bit
0x80000) **OR** (ALT held AND `+0x1F4 & 0x1006dcf8 != 0` AND
`!FUN_00426a30()` AND `sprite_lut[+0x261] ∈ [1,0x5a]`). Our captain
satisfies it via `+0x14&0x80000` (set by the DlgNPC bind). The gate
does NOT pick the glyph — it only decides show/hide.

**objIdx** = `FUN_00549920()` (ECX=cCreature): fast-path returns
`*(cCreature+0x245)` (the dlg index our bind stamps via
`FUN_005498f0`), else scans the `DAT_00ab44e8` table. Range-checked by
the selector.

**Selector** `FUN_00499e90(objIdx, cCreature*)` @0x00499E90 — full body
re-read, exhaustive priority order:
| # | precondition (first match wins) | returns sprite id |
|---|---|---|
| 1 | `objIdx > 159999` | `4` |
| 2 | `cCreature != 0 && (+0x200 & 0x1000)` | `0x20` |
| 3 | `cCreature != 0 && (+0x200 & 0x2000)` | `0x21` |
| 4 | `cCreature != 0 && (+0x200 & 0x4000)` | **`0x22`** |
| 5 | `cCreature != 0 && (+0x200 & 0x4000000)` | `0x23` |
| 6 | `objIdx < (qm+0x7560 − qm+0x755c)/0x50` | `*(u32*)(qm+0x755c + objIdx*0x50 + 0x48)` = **DlgNPC `entry+0x48`** |
| 7 | else | `0` |

So the ONLY two ways the glyph is chosen: the `cCreature+0x200`
class-bit word (cases 2-5, priority before everything except the
160000 cap), else the DlgNPC `entry+0x48` byte (case 6). No
quest-state / `qm` registry input reaches the glyph — quest state only
toggles `entry+0x48` via the SetIcon tag handler `FUN_004a1a50`.

**Glyph switch** `FUN_004090d0(...,param_5=sprite id)` @0x004090D0 —
complete switch (all cases read):
- `param_5 == 8` or `== 0xd`: **early return, NOTHING drawn** (clear).
- `case 0` / `0x0a`: `NPC_DIALOG_01.TGA`
- `case 1` / `0x0b` / **default**: `NPC_DIALOG_02.TGA`  ← the plain "!" talk bubble (our current 0x0b)
- `case 4`/`0x0e`: `NPC_DIALOG_05.TGA`; `case 5`/`0x0f`: `NPC_DIALOG_07.TGA`
- `case 0x20`: `npc_dialog_smith.tga`; `0x21`: `npc_dialog_trader.tga`;
  **`0x22`: `npc_dialog_combo.tga`** (the combined exclam+question
  glyph); `0x23`: `npc_dialog_trader.tga`
- `0x40-0x5a`: `sim_*` AI-mood icons; `0x4b`: `sim_quest.tga` (minimap
  pin, not overhead); `0x7e`: skip; `0x80-0x83`: `sim_{physical,fire,
  magic,poison}.tga`.

Color is **NOT** a separate tint/palette arg. `FUN_004090d0`'s draw
uses a fixed vertex-colour block copied from `DAT_00cdca1c+0x250` and a
`fsin`-pulsed grey alpha (`local_228[3]`); `param_4` is only an
animation **phase offset** (`(iVar3+param_4)*0.005`), not color. The
look of each marker is entirely baked into its `.tga`. There is no
per-call yellow/secondary recolor path.

### 2. The "?!" glyph and its color

`0x22 → npc_dialog_combo.tga` is **verified** as the only combined
exclam+question ("?!") sprite in the entire switch. Its color is
whatever is painted in `npc_dialog_combo.tga` itself — there is no
engine "yellow secondary" variant: no second combo TGA, no tint arg,
no palette index. The 18 082-record SetIcon catalog (NQ_/HQ_/GQ_/DQ_
classified, prior pass) shows vanilla secondary quests (NQ_) use the
SAME `entry+0x48 = 0x0b` (→ NPC_DIALOG_02) as main quests; `0x22`
appears in **zero** per-NPC SetIcon streams of any class. The
main/secondary distinction lives only in the journal category, never
in the head glyph.

**Reaching case 0x22 — exactly two routes:**
- (A) `cCreature+0x200 & 0x4000` set (selector case 4). Wins before
  the DlgNPC fallthrough — deterministic regardless of objIdx.
- (B) DlgNPC `entry+0x48 = 0x22` AND all `+0x200` class bits
  (0x1000/0x2000/0x4000/0x4000000) clear AND objIdx in range
  (selector case 6). This is the engine-faithful route (same field
  the vanilla SetIcon handler `FUN_004a1a50` writes) — it never
  occurs in vanilla data but the code path is fully valid.

There is **no** quest-registry / `qm` state that yields 0x22.

### 3. What `cCreature+0x200 & 0x4000` actually does (decompiled)

Set ONLY by `FUN_00482510` (CreateNPC) L1784-1788 for the
`local_6e0 & 8` = **COMBO / combat-arts-MASTER class** NPC:
`+0x200 |= 0x4000` together with `+0x14 |= 0x80000` (the latter is the
marker gate, set anyway by our bind). Readers of `+0x200 & 0x4000`,
exhaustively:
- `FUN_00499e90` selector → returns sprite `0x22` (the marker glyph). 
- `FUN_006ccbd0` L1303: `(+0x200 & 0x4007000) != 0` gates the
  **minimap quest dot** (a *wanted* extra, harmless).
No other reader. **No FX / aura / particle / behaviour emitter keys
off `+0x200`.** The red glow is `FUN_00408b70`, gated by
`+0x14&0x80000` (PROVEN, live dump had `+0x200==0` and still glowed) —
fully independent of `+0x200`. The MASTER_OF_COMBAT_ARTS "aura" the
old notes feared was that same disproven theory. For a stationary
immortal quest NPC, setting `+0x200 & 0x4000` therefore has **exactly
two visible effects: the overhead glyph becomes the "?!"
(npc_dialog_combo.tga) and a minimap quest dot appears.** No combat,
no trainer dialog (training is driven by the dialog/tag system, not
this bit), no FX. Side-effect-free for our use.

### 4. Decisive recipe for the "?!" marker (NO red-glow side effect)

The red glow is a SEPARATE problem already root-caused to
`FUN_00408b70` / `+0x14&0x80000` (see "Captain glow (definitive)"
section: fix = 3-byte stub at 0x00408B70, or accept it). It is
**orthogonal** to the glyph choice — changing the marker glyph cannot
add or remove the glow.

To make the bound captain show the **"?!" glyph** (the closest the
engine has — there is no yellow secondary variant; this glyph's color
is whatever `npc_dialog_combo.tga` ships):

PREFERRED (engine-faithful, mirrors vanilla SetIcon `FUN_004a1a50`):
```c
// DlgNPC entry+0x48 = 0x22, keep ALL +0x200 class bits clear.
uint8_t* b = *(uint8_t**)(0x00AACF80 + 0x755c);   // qm+0x755c
uint8_t* e = *(uint8_t**)(0x00AACF80 + 0x7560);
for (uint8_t* p=b; p+0x50<=e; p+=0x50)
    if (*(uint32_t*)p == handle) { *(uint32_t*)(p+0x48) = 0x22; break; }
// requires: cCreature+0x245 = dlg idx (already stamped by our bind via
// FUN_005498f0) so FUN_00549920 returns an in-range objIdx → selector
// case 6 reads entry+0x48. +0x14&0x80000 already set by the bind.
```
Confidence **HIGH** that this yields sprite 0x22 → npc_dialog_combo.tga
(direct decompile of the full selector + complete switch + the vanilla
write site `FUN_004a1a50`). One MED: that our bind's objIdx
(`+0x245`) is in `[0, (qm+0x7560−qm+0x755c)/0x50)` for the synthetic
NPC — settle with a single BP `0x00599FA6` (`call FUN_00499e90`) on the
captain: read arg `objIdx`, arg `cCreature*`, and EAX (expect `0x22`).

ALTERNATIVE (deterministic, also side-effect-free now):
`*(uint32_t*)(cCreature + 0x200) |= 0x4000;` — selector case 4 returns
0x22 unconditionally (independent of objIdx/DlgNPC), plus a minimap
quest dot. Proven not to touch any FX path (§3). Use this if the BP
shows objIdx out of range. Do NOT also set the other `+0x200` bits.

**Bottom line:** the engine has NO yellow "?!" secondary-quest marker —
only the plain "!" (`0x0b`/`0x01`→NPC_DIALOG_02), the combined "?!"
(`0x22`→npc_dialog_combo.tga, color baked in the TGA, no yellow
variant), the smith/trader class icons, and the AI-mood `sim_*` set.
The closest achievable is the "?!" combo glyph via `entry+0x48=0x22`
(preferred) or `+0x200|=0x4000` (deterministic) — **both are now
side-effect-free** (the glow is the unrelated `+0x14&0x80000` swirl,
handled separately). Confidence **HIGH** on the glyph map, the
no-yellow-variant conclusion, and `+0x200&0x4000`'s only readers being
the selector + minimap.

### Files / VAs this pass
- re-read `decompiled/00499e90_FUN_00499e90.c` (full selector, 7-case
  priority table), `decompiled/004090d0_FUN_004090d0.c` (complete
  switch, color = fixed DAT_00cdca1c+0x250 block, param_4 = phase only),
  `decompiled/00599910_FUN_00599910.c` L240-271 (gate, glyph-agnostic),
  `decompiled/00482510_FUN_00482510.c` L1784-1788 (`+0x200|=0x4000` =
  COMBO class, co-sets `+0x14|0x80000`), `decompiled/006ccbd0` L1303
  (`+0x200 & 0x4007000` = minimap pin, only other +0x200 reader).


---

## Overhead-icon atlas — definitive (2026-05-16)

**This section SUPERSEDES and FALSIFIES every prior "?!"/`npc_dialog_combo`
claim in this file**, specifically `## Yellow "?!" marker — re-evaluated
(2026-05-16)` and the Q2/2/4 parts of `## Captain glow (definitive) +
yellow ?! marker`. Those concluded `+0x200&0x4000` -> selector case 4 ->
glyph 0x22 -> npc_dialog_combo.tga = a "?!" combined glyph,
"side-effect-free". **WRONG in-game** (rendered the yellow
combat-arts-master HUMANOID figure). Root cause of the prior error: the
agent ASSUMED the visual content of npc_dialog_combo.tga from its
filename without ever extracting the texture. Corrected here by
extracting+rendering every marker TGA from pak/texture.pak (ground
truth) plus a clean re-read of the full pipeline.

### Pipeline (disasm-exact, all three fns re-read)

1. **Gate** FUN_00599910 block LAB_00599f23..0x599fce (decompile
   L240-271): draws the swirl FUN_0044b230, then for the marker takes
   LAB_00599f96 iff `cCreature+0x14 & 0x80000` (in_ECX[5]&0x80000), or
   the FUN_00428460 registry / ALT-key fallback. The gate ONLY decides
   show/hide; it does NOT pick the glyph.
2. **objIdx** = FUN_00549920() (ECX=cCreature): fast-path returns
   `*(cCreature+0x245)` (the idx our bind stamps via FUN_005498f0).
3. **Selector** FUN_00499e90(objIdx, cCreature*) @0x00499E90 — verbatim
   from decompiled/00499e90_FUN_00499e90.c, exhaustive priority (first
   match wins):

   | # | precondition | returns sprite-id |
   |---|---|---|
   | 1 | objIdx > 159999 | 4 |
   | 2 | cCreature && (cCreature+0x200 & 0x1000) | 0x20 |
   | 3 | cCreature && (cCreature+0x200 & 0x2000) | 0x21 |
   | 4 | cCreature && (cCreature+0x200 & 0x4000) | 0x22 |
   | 5 | cCreature && (cCreature+0x200 & 0x4000000) | 0x23 |
   | 6 | objIdx < (qm+0x7560 - qm+0x755c)/0x50 | *(u32*)(qm+0x755c + objIdx*0x50 + 0x48) = DlgNPC entry+0x48 |
   | 7 | else | 0 |

4. **Glyph draw** FUN_004090d0(...,param_5=sprite-id) @0x004090D0. Guard
   L49: `if (param_5 != 8 && param_5 != 0xd)` — **8 and 0xd are the
   CLEAR / no-draw values** (returns drawing nothing). Else a
   switch(param_5) PRE-LOADS the texture cache slot, then **L338
   `local_22c = FUN_0065ed90((&DAT_00aa42e0)[param_5]);`** draws the
   billboard with the texture at the FLAT array DAT_00aa42e0 indexed by
   the RAW sprite-id (stride 4: case 0x22 fills DAT_00aa4368 =
   DAT_00aa42e0 + 0x22*4; the default case first sets param_5=0x7f so
   the index stays consistent). One billboard at the model Bip01 Head
   bone. Colour = FIXED vertex block DAT_00cdca1c+0x250 + fsin grey
   pulse; param_4 = animation PHASE only. **No per-call tint/recolor —
   the look is 100% baked into the .tga.**

### TGA -> VISUAL ground truth (extracted from pak/texture.pak)

pak/texture.pak magic TEX\x03; flat dir entries name[32] | w:u16 | h:u16
| fmt:u8 | csize:u24-LE, payload = zlib (78 9c) of a 32x32 ARGB4444
image. Rendered each (/tmp/NPC_DIALOG_*_4444.png):

| sprite .tga | WHAT IT ACTUALLY LOOKS LIKE |
|---|---|
| NPC_DIALOG_01.TGA | **"?!" combined question+exclamation glyph** <-- the REAL "?!" |
| NPC_DIALOG_02.TGA | plain **"!"** exclamation (talk / quest-giver bubble) |
| NPC_DIALOG_05.TGA | a **scroll / parchment** (quest turn-in / objective) |
| NPC_DIALOG_07.TGA | a downward **chevron / arrow** (follow / destination) |
| npc_dialog_smith.tga | blacksmith **hammer** |
| npc_dialog_trader.tga | trader **money-bag** |
| npc_dialog_combo.tga | a solid **YELLOW ARMORED HUMANOID FIGURE** = the combat-arts-MASTER (COMBO) class icon. **NOT a "?!".** Exactly the in-game sprite the user saw after the falsified +0x200 poke. |

### ICON -> exact precondition table (definitive)

| ICON (visual) | sprite-id | drawn TGA | EXACT precondition |
|---|---|---|---|
| **"?!"** question+exclam | 0 or 0xa | NPC_DIALOG_01 | gate on AND all cCreature+0x200 class bits clear AND DlgNPC entry+0x48 in {0x00,0x0a}, objIdx in range (selector case 6) |
| **"!"** exclam (quest-giver/talk) | 1/0xb/default | NPC_DIALOG_02 | as above with entry+0x48 in {0x01,0x0b} (or any value not otherwise cased -> default -> NPC_DIALOG_02) |
| **scroll** (objective) | 4/0xe | NPC_DIALOG_05 | entry+0x48 in {0x04,0x0e} (also selector case 1 if objIdx>159999) |
| **chevron** (follow) | 5/0xf | NPC_DIALOG_07 | entry+0x48 in {0x05,0x0f} |
| **NO MARKER** (cleared) | 8 or 0xd | (early return) | entry+0x48 in {0x08,0x0d} = vanilla "hide marker" values |
| **smith hammer** | 0x20 | npc_dialog_smith | cCreature+0x200 & 0x1000 (CreateNPC class flag local_6e0&0x10) |
| **trader bag** | 0x21 | npc_dialog_trader | cCreature+0x200 & 0x2000 (local_6e0&0x04) |
| **YELLOW FIGURE** (combat-arts master) | 0x22 | npc_dialog_combo | cCreature+0x200 & 0x4000 (CreateNPC COMBO class local_6e0&0x08, FUN_00482510 L1784-88). Only practical route to 0x22; selector case 4 wins over the DlgNPC channel. |
| **trader bag** (variant) | 0x23 | npc_dialog_trader | cCreature+0x200 & 0x4000000 (local_6e0&0x20) |
| sim_* AI-mood | 0x40-0x5a | sim_*.tga | only via entry+0x48 / objIdx>159999; not used by quest NPCs |

+0x200 class-bit values: 0x1000=smith, 0x2000=trader,
**0x4000=COMBO/combat-arts-master (YELLOW FIGURE)**,
0x4000000=trader-variant. All four set ONLY by FUN_00482510 (CreateNPC)
from template class nibble local_6e0 (L1776/1781/1786/1792) and co-set
+0x14|0x80000. Only other +0x200 reader = FUN_006ccbd0 L1303 minimap
dot. No FX/aura/combat reader (the prior "red aura from +0x200" theory
remains disproven; glow is the separate FUN_00408b70 swirl).

### Vanilla entry+0x48 usage (cross-checked, real data)

Walked bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin tag-0x56 SetIcon records
(handler FUN_004a1a50 writes DlgNPC entry+0x48 = opcode-0xb value), 1804
records: 0x0b(737) 0x0d(359) 0x0a(359) 0x08(289) 0x00(10) 0x01(9)
0x0e(3) 0x04(3), plus ~50 large res-id values (-> default -> "!"). So
vanilla DOES ship/use the "?!": **entry+0x48 = 0x0a (and 0x00) ->
NPC_DIALOG_01 = the genuine "?!" glyph** (e.g. DlgShareef71 uses 0x00).
0x0b/0x01 = "!"; 0x08/0x0d = clear. The earlier "secondary quests reuse
0x0b" claim is true only for the "!"; the engine absolutely has a
separate, vanilla-used "?!" and it is NPC_DIALOG_01, NOT
npc_dialog_combo.

### Decisive answers

1. **Does a true quest "?!" glyph exist separate from the yellow master
   figure? YES.** It is NPC_DIALOG_01.TGA, reached purely via the DlgNPC
   entry+0x48 channel (selector case 6) with sprite-id 0x00 or 0x0a — NO
   +0x200 write. npc_dialog_combo.tga is the yellow combat-arts-master
   figure, irrelevant to quests.
2. **Correct side-effect-free recipe for a genuine quest "?!"** over our
   bound runtime NPC (mirrors vanilla SetIcon FUN_004a1a50; NO
   cCreature+0x200 write):

       // Prereq: dlgnpc_bind already ran (cCreature+0x245 = dlg idx via
       // FUN_005498f0; cCreature+0x14 |= 0x80000 gate). NEVER poke +0x200.
       uint8_t* b=*(uint8_t**)(qm+0x755c); uint8_t* e=*(uint8_t**)(qm+0x7560);
       for(uint8_t* p=b; p+0x50<=e; p+=0x50)
         if(*(uint32_t*)p==(uint32_t)handle){ *(uint32_t*)(p+0x48)=0x0A; break; }
       //   0x0A -> NPC_DIALOG_01 = "?!"   |  0x0B -> NPC_DIALOG_02 = "!"
       //   0x08 (or 0x0D) -> CLEAR (no marker drawn)

   Confidence **HIGH** (full selector + full switch + L338 flat-array
   draw + vanilla FUN_004a1a50 write site + texture ground-truth +
   1804-rec catalog all agree). Residual MED (our bind objIdx in range
   for the synthetic NPC) closed by one BP 0x00599FA6 (call
   FUN_00499e90): read arg objIdx, arg cCreature*, EAX (expect
   0x00/0x0a). For npc_quest_icon: 0x0A (=?!) or 0x0B (=!) for on, 0x08
   for off. The current SDK already correctly avoids +0x200.

### Files / VAs this pass
- re-read full: decompiled/00499e90 (selector), decompiled/004090d0
  (switch + the L338 flat-array (&DAT_00aa42e0)[param_5] draw — the
  detail the prior pass missed), decompiled/00599910 L240-271 (gate),
  decompiled/004a1a50 (SetIcon tag-0x56 -> entry+0x48 writer),
  decompiled/00482510 L1774-1797 (+0x200 class bits <- local_6e0).
- texture ground truth: extracted+rendered NPC_DIALOG_01/02/05/07,
  npc_dialog_smith/trader/combo from pak/texture.pak (TEX\x03 / zlib /
  32x32 ARGB4444) -> the icon->visual table above.
- data: bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin 1804 tag-0x56 records.
