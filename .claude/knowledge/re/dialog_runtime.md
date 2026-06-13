# Runtime dialog-show recipe — making the bound NPC speak (multi-line text,
# response buttons, optional voice), from a DLL with NO FunkCode edit
# 2026-05-16

> **⚠ LIVE-REFUTED CLAIMS IN THIS DOC (added 2026-05-18). Cross-check
> against `HANDOFF.md §6 DEAD-PATHS CATALOG` before trusting anything
> below — the following marked-HIGH claims were proven WRONG live:**
>
> - **"FUN_0048FF10 is the dialog-answer/SelfTriggerQuest handler
>   (Rocheford-reward-proven)"** — REFUTED. Unconditional entry log
>   on the installed hook produces 0 hits even for vanilla quest NPCs.
> - **"The talk-route NPC's FunkCode is re-replayed through
>   FUN_00475680 on answer, jumping to FUN_0048FF10 via jt[0x76]"** —
>   The dispatcher path is correct as disasm but is NOT triggered by
>   the player's answer in this build for ANY NPC live-tested. Either
>   the path is gated by something undocumented or the answer mechanism
>   is fundamentally different from what this doc describes.
> - **"Conv-window pump = FUN_00461540"** — REFUTED live; never fires
>   on talk with our FIX-A2 NPC (user-confirmed window opened+closed).
> - **POLL of +0x14&0x80000 / +0x245 already correctly REFUTED by the
>   red-team section here AND additionally confirmed by live `[dlgpoll]`
>   (no creature-field change on open/close at all).**
>
> Treat the "HIGH" labels in this doc as STATIC-disasm confidence, NOT
> live behaviour. Live probe results in `HANDOFF.md` are the ground
> truth. The principled remaining instrument is the HW data BP on
> `entry+0x4c` documented in `HANDOFF.md §9`.

Target: Steam 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000, no
ASLR (file off = VA-0x400000). `qm = 0x00AACF80` (ECX/in_ECX in every
storyline handler). `om = *(void**)0x00AD5C40`; `arr=*(om+4)`;
`cCreature = *(arr + handle*4)`.

Builds on (does NOT repeat) `dlgnpc_bind.md` (DlgNPC entry create +
`cCreature+0x245` stamp), `triggers_dialog_move.md` (§B Dialog tag 0x1f /
voice tag 0x68 / ring type 0xe), `quest_storyline.md`,
`questbook_resolver.md` (the `res:NAME` → handle resolver
`FUN_00672740`), `globalres_format.md` (custom-text authoring),
`funkcode_tags.md` (the record walker `FUN_00475680`). New RE this pass:
the full Dialog/Button/Talk arming chain end-to-end and the **honest
conclusion that there is NO standalone "pop a dialog box" engine call** —
dialog is *state armed on the DlgNPC entry*, replayed only through the
record dispatcher `FUN_00475680`.

---

## TL;DR — the answer

1. **`FUN_0048f9e0` (Dialog tag 0x1f) does NOT draw anything.** It is a
   pure *arming* primitive: it finds the DlgNPC entry (by name field-1 /
   id field-0xb / field-0x16), writes the **dialog content handle into
   `DlgNPC entry+0x4c`** (`= *(record+0xc)`), sets the owning context's
   `+0x14 |= 0x80000` "dialog active" bit, calls
   `FUN_005498f0(idx,1)` (stamps `cCreature+0x245`, also fires the
   `0x11a` notify), and in MP posts a `0x11c` replication event. The
   on-screen conversation window is a separate GUI that *renders the
   armed state* (`DlgNPC entry+0x4c` content + the `+0x14&0x80000` gate
   + the per-context button arrays). **You arm; the engine's GUI shows.**
2. **The displayed TEXT is a `res:NAME`/`res:NNNN` global.res string
   handle**, not inline text. The handle stored at `entry+0x4c` is the
   value the record's opcode-1 string resolved to via the standard
   resolver **`FUN_00672740(name)` = `hash31(name)|0x80000000`** (same
   convention as quest-log lines — see `questbook_resolver.md`). So
   custom dialog text MUST be a registered global.res entry; the SDK's
   existing global.res rebuild (`text.lua`, `globalres_format.md`) is
   the authoring path. You CANNOT pass a raw C string.
3. **Response buttons** = the SetButton handler `FUN_00499ba0` (tag
   0x3c). It fills three parallel per-context arrays on `qm`:
   - `qm+0x9a00 + slot*4`  (i32)  = the button's **action res-id**
     (`*(qm+0xa880)` = the resolved opcode-0xb id of the follow-up).
   - `qm+0x9a14 + slot*0x200` (char[0x200]) = button **label string**
     (a `RES:%d` string or a literal — built via `s_RES__d_0094f434`).
   - `qm+0x9b14 + slot*0x200` (char[0x200]) = the button's **second
     string** (target / dialog-jump key).
   Max **5** slots (loop bails at `iVar3 > 4`); slot = first row whose
   `qm+0x9a14` byte is `\0`. Buttons attach to *the current dialog*
   (the context whose `+0x14&0x80000` is set), not to a specific entry.
4. **Voice** = PlaySound tag 0x68 → `FUN_004a9730`. Minimal runtime
   path: resolve the SOUND_FX id once (`FUN_00676170("SOUND_FX_<NAME>")`
   or read the static table base `0x00964870` stride 0x44), then push a
   **type-0xe command on the speaking creature's ring**
   (`cCreature+0x588`/`+0x58c`, stride 0x44; `elem[0]=0xe`,
   `*(u32*)(elem+0x40)=id`). Copy-paste recipe below.
5. **Cleanest runtime trigger.** There is no per-frame "if player clicks
   bound NPC, engine auto-opens our dialog" hook we can satisfy with
   data alone — the conversation is only ever armed by a Dialog/TalkTo
   **record going through the dispatcher `FUN_00475680`** (or its
   net/save twins `FUN_0045f220`/`FUN_00807760`). Two equivalent runtime
   ways to fire it from a DLL, both engine-faithful:
   - **R-A (call the handler directly):** ECX=qm, build a record-shaped
     buffer, call `FUN_0048f9e0` (then `FUN_00499ba0` per button, then
     `FUN_004a9730` for voice). MED — buffer layout below.
   - **R-B (replay a record blob, preferred):** push a tag-0x1f (+ 0x3c
     + 0x68) TLV blob through `FUN_00475680` exactly like vanilla. HIGH
     — byte-identical to every shipped conversation.

---

## How dialog actually works (the chain, all decompiled)

### Record dispatch — `FUN_00475680` @ 0x00475680  (ECX = qm)
`switch((u16)record[0])`. Reads the raw TLV record into a stack buffer
`local_a0c` (size = `record[2..3]` BE, dword-copied from
`recordbytes`), then calls the per-tag handler with the convention
**`param_1 = param_2 = &local_a0c`** (the parsed record buffer) and
**ECX = qm**. Cases of interest:
- `case 0x1f` → `FUN_0048f9e0()`  (Dialog: arm content)
- `case 0x3c` → `FUN_00499ba0()`  (SetButton: add a response button)
- `case 0x68` → `FUN_004a9730()`  (PlaySound: voice/SFX)
- `case 0x06` → `FUN_00491170()` → `FUN_00463240()` (TalkTo: full talk
  binder — arms dialog from `*(cCreature+0xc)` + registers a
  SelfTriggerQuest function block; sets `cCreature+0x14|0x80000`)

The record buffer field used by the dialog handler: **`local_a0c+0xc`**
(= `param_2+0xc`) = the dialog **content handle** (the resolved
`res:NAME` id). It is produced when the record's opcode sub-stream is
parsed by `FUN_00472bc0` (opcode 1 ASCIIZ → resolved via the
`questbook_resolver.md` chain into `*(qm+0xa880)` / a880-style handle;
the dispatcher copies the relevant resolved value into the buffer
before the handler runs).

### Dialog arm — `FUN_0048f9e0` @ 0x0048F9E0  (ECX = qm)
Decompiled in full (`decompiled/0048f9e0_FUN_0048f9e0.c`). Per opcode
field returned by `FUN_00472bc0(&local_e8,param_1)`:
- field **1** / **0x16** (ASCIIZ): scan DlgNPC array (`qm+0x755c` begin,
  `qm+0x7560` end, **stride 0x50**), name compare `FUN_00859690(entry+4,
  qm+0xa460)`. On match index `uVar9`:
  - clear the *previously active* entry: `entry[oldIdx]+0x4c = 0` (old
    idx from `FUN_00549920()`), `FUN_0045ee20(3,0,0)` (dirty/notify).
  - `entry[uVar9]+0x4c = *(param_2+0xc)`  ← **the content handle**.
  - `FUN_005498f0(uVar9,1)`  ← stamp `cCreature+0x245`, fire `0x11a`.
  - `uVar9==0` ? `param_2+0x14 &= ~0x80000` : `param_2+0x14 |= 0x80000`
    (the **dialog-active gate** the GUI reads; idx 0 = "close dialog").
  - If `DAT_0182ebec` (MP) and `*(qm+0x14)==0`: post net event **0x11c**
    carrying `(content=*(param_2+0xc), idx=uStack_ec, flags=
    *(param_2+0x14))` via `FUN_007d84a0`/`NetworkManager_receive_event_
    007d8950`. The receiver trampoline `FUN_00807760` →
    `FUN_00463240(...)` re-arms on the remote side. (So 0x11c == "show
    this dialog on all clients incl. self" in MP; in SP the local arm is
    enough and the GUI picks it up next frame.)
  - not found → log `"Dialog %s nicht oder nocht nicht…"`, clear gate.
- field **0xb** (i32): same but match `*(int*)entry == id`; uses
  `FUN_00465220(idx, *(param_2+0xc))` to write `entry+0x4c`.

`entry+0x4c` is the *single* "what this NPC is currently saying"
content slot. The GUI renders the string for that handle (resolved
through the global.res hash-or-ptr resolver `FUN_006726f0`:
`(h&0x80000000)? dict[h&0x7fffffff] : ptr`).

### Full talk binder — `FUN_00463240` @ 0x00463240  (ECX = qm)
(`decompiled/00463240_FUN_00463240.c`) Log `"Talk %s Dlg %d"`,
`"SelfTriggerQuest %d"`. Resolves the creature, writes
`entry+0x4c = *(creature+0xc)` (the creature's own attached dialog
content), `FUN_005498f0(idx,1)`, registers a function block in the
`DAT_00aab708` (stride 0x54) table keyed by the SelfTriggerQuest name,
and `*(creature+0x14) |= 0x80000`. This is the path a *TalkTo* script
record (tag 6) takes; it shows whatever dialog content the creature
object carries at `+0xc`.

### Button add — `FUN_00499ba0` @ 0x00499BA0  (ECX = qm)
(`decompiled/00499ba0_FUN_00499ba0.c`) Parses opcode fields:
- opcode-1 with low byte `local_208==0` → copies the name from
  `qm+0xa460` into the **label scratch** and reads the action id
  `unaff_EBX = *(qm+0xa880)`.
- opcode-1 with low byte `!=0` → copies into a **second** scratch
  (`acStack_108`, the target/jump string).
After parsing, if the high byte of `local_208` is set, it finds the
first free button slot by walking `qm+0x9a14` in **0x200** strides
(`while *p != 0; p += 0x200; bail if slot index > 4`), then:
- if `unaff_EBX < 1`: strcpy the label into `qm + slot*0x200 + 0x9a14`.
- else: `FUN_00849ae1(qm+slot*0x200+0x9a14, "RES:%d", unaff_EBX)`
  (label is the localized string `RES:<id>`).
- `*(int*)(qm + 0x9a00 + slot*4) = unaff_EBX`  ← button **action id**.
- strcpy the second string into `qm + slot*0x200 + 0x9b14` ← jump key.
So a button = `{ labelStr@(0x9a14+slot*0x200), actionId@(0x9a00+slot*4),
jumpKey@(0x9b14+slot*0x200) }`, slots 0..4. Selecting a button makes
the engine run the follow-up keyed by `actionId`/`jumpKey` (the next
quest/dialog node) — the SDK supplies those as further records.

### Voice — `FUN_004a9730` @ 0x004A9730  (ECX = qm)
(`decompiled/004a9730_FUN_004a9730.c`, also triggers_dialog_move §B):
opcode-1 #1 = sample name (prefix `"SOUND_FX_"` if absent),
`uStack_1dc = FUN_00676170(name)` (table lookup → numeric id), opcode-1
#2 / 0x16 = target NPC (`res:NNNN`). If a creature resolves, it
**reserves one ring slot** (`FUN_004be490(end, need, tmpl)` or shift
`FUN_004b9900`) on `cCreature+0x588/+0x58c` and writes a zeroed 0x44
element with `elem[0]=0xe`, `*(int*)(elem+0x40)=id`. Otherwise it plays
globally via `FUN_006770e0`+`FUN_00693fe0` (Miles).

---

## Where the on-screen TEXT comes from (Q1 answer, HIGH)

`entry+0x4c` holds a **global.res string handle** = the value
`FUN_00672740("NAME")` returned for the record's opcode-1 string
(`hash31(name) | 0x80000000`), exactly the quest-log convention
(`questbook_resolver.md`). Resolution at draw time is the unified
hash-or-ptr resolver `FUN_006726f0`:
`(h & 0x80000000) ? FUN_0080eaf0(h & 0x7fffffff) : FUN_0080e780(h)`.

Consequences:
- We **can** point a dialog at our own text **iff** that text exists in
  the loaded global.res dictionary. The SDK already has the byte-exact
  global.res rebuild (`globalres_format.md` + `text.lua`) — registering
  a custom `MYDLG_LINE1` string there is the authoring path. After it
  is in the dictionary, `qb_resolve("MYDLG_LINE1")` returns a nonzero
  handle to store in `entry+0x4c`.
- Multi-line: one global.res string can itself contain newlines (the
  dialog box word-wraps/renders the whole string). For a sequence of
  *separate* lines with their own voice + buttons, emit a sequence of
  Dialog records (each re-arms `entry+0x4c`), the standard storyline
  pattern (Dialog → PlaySound → Wait → next Dialog …).
- Unknown name → resolver returns 0 → `entry+0x4c=0` → the GUI shows
  nothing / closes. Cannot be faked without a real dictionary entry.

Helper (from questbook_resolver.md, reused verbatim — `__cdecl`, no ECX):
```c
typedef unsigned (__cdecl *resolve_t)(const char* name);  // 0x00672740
#define qb_resolve ((resolve_t)0x00672740)
// unsigned h = qb_resolve("MYDLG_LINE1");  // hash|0x80000000, 0 if absent
```

---

## Does the engine auto-show on talk, or must we pop it? (Q4, HIGH)

**Neither a pure data poke nor a magic "open dialog" call.** Decompile
of the whole chain shows the conversation window is driven *only* by a
Dialog/TalkTo **record processed through `FUN_00475680`** (the same
function the engine runs for scripts, save replay `FUN_0045f220`, and
the net 0x11c receiver `FUN_00807760`). The bound NPC having a DlgNPC
entry + `+0x245` makes it a real dialog NPC (name, marker, the engine
*will* route a talk to it), but the *content* shown is whatever a
Dialog/TalkTo record last armed into `entry+0x4c`. So the minimal path
to make our NPC say something is to **fire a tag-0x1f (Dialog) record
for it** from the DLL — via R-A or R-B below — at the moment we want
(e.g. on spawn, on a quest step, or from our own click hook if the SDK
adds one). Buttons (0x3c) and voice (0x68) are additional records fired
right after, against the same active dialog.

Dialog text MUST be a global.res entry (Q4 second half): **yes** — the
SDK's global.res text registration is the required authoring path; the
runtime side only stores/passes the resolved handle.

---

## RUNTIME RECIPE

Prereq: the NPC already has a DlgNPC entry bound to its handle and
`cCreature+0x245` stamped — i.e. `dlgnpc_ensure(handle,name,sprite)`
from `dlgnpc_bind.md` has run. Custom strings already registered in
global.res (so `qb_resolve` returns nonzero).

### R-B — replay a Dialog/Button/Voice record blob (PREFERRED, HIGH)

Engine-faithful, zero struct guessing: build the same TLV the compiler
emits and run it through the dispatcher. The dispatcher is `__thiscall`,
**ECX = qm = 0x00AACF80**, signature (from dlgnpc_bind.md / funkcode):
`int FUN_00475680(int* cursor, int bufBase, ?, undefined4* param4,
char param5)`. The simplest call mirrors how StartCode replay invokes
it: place the record bytes in a buffer, set the cursor to its start,
`param5=0`. (Use the exact same call shim the SDK already uses to
replay tag-0x28 in `dlgnpc_bind.md` PATH B — that shim is proven; just
feed it 0x1f/0x3c/0x68 records instead of/after the 0x28.)

Record byte shape (tag, u16-BE size incl. 3-byte header, then the
opcode sub-stream that `FUN_00472bc0` decodes; opcode 1 = ASCIIZ
field, 0xb = i32, 0 = END):

```
; ---- Dialog: NPC <name> says <textResName> ----
1F <size:u16 BE>
   01 "<DlgNPCname>\0"        ; opcode 1  -> selects the bound DlgNPC entry by name
   01 "<MYDLG_LINE1>\0"       ; opcode 1  -> text res name; engine resolves -> entry+0x4c
   00                         ; END
; ---- one response button (repeat, max 5) ----
3C <size:u16 BE>
   01 "<buttonLabelOrRES>\0"  ; label  (or "RES:<id>" form)
   0B <actionId:i32>          ; follow-up action/quest id (-> qm+0x9a00[slot])
   01 "<jumpKey>\0"           ; target/next-node key (-> qm+0x9b14[slot])
   00
; ---- optional voice on the speaker ----
68 <size:u16 BE>
   01 "SOUND_FX_<VOICE>\0"    ; sample name
   01 "<DlgNPCname>\0"        ; target NPC (so it rings type 0xe on that creature)
   00
```

Notes:
- Size field is big-endian and **includes the 3 header bytes** (matches
  funkcode_tags.md and the 16 460-record catalog).
- The opcode-1 *text* string is run through `FUN_00672740` by the
  parse; it must be a registered global.res name (no `res:` prefix —
  the generic branch strips it; pass the bare name).
- To **close** the dialog: emit a Dialog record selecting DlgNPC index
  0 (`uVar9==0` clears the `+0x14&0x80000` gate) or simply arm
  `entry+0x4c=0` (see R-A close).
- Voice without dialog = just the 0x68 record (or the ring poke below).

### R-A — call the handlers directly (MED, fewer moving parts)

If the SDK prefers not to synthesize TLV bytes, build the parsed
**record buffer** the dispatcher would have produced and call the
handler with ECX=qm. The handler re-parses its opcode sub-stream from
`param_1` via `FUN_00472bc0`, and reads the content handle from
`param_2+0xc`. The buffer is `local_a0c`-shaped: `[+0]=u16 tag`,
`[+2]=u16 size`, `[+0xc]=u32 contentHandle`, followed by the opcode
sub-stream the handler walks. Because the exact intermediate buffer
layout (offsets of the embedded opcode stream vs the `+0xc` slot) is
only partially reversed, **R-A is MED**; R-B avoids it entirely. If R-A
is used, the safest sub-form is to skip name lookup and use the
**by-id** branch: build an opcode stream with `0B <dlgIdx>` and set
`buf+0xc = qb_resolve("MYDLG_LINE1")`, then `((dlg_t)0x0048F9E0)(buf,
buf)` with ECX=qm. Confirm the `+0xc` offset with a one-shot BP on
`0x0048F9E0` entry while a vanilla quest dialog fires (read
`[param_2+0xc]` == the resolved text handle).

### Voice — copy-pasteable C (HIGH for table+id; MED for ring helper)

```c
#define QM 0x00AACF80
static uint8_t* creature_of(uint32_t h){
    uint8_t* om = *(uint8_t**)0x00AD5C40;
    uint8_t* arr= *(uint8_t**)(om+4);
    return *(uint8_t**)(arr + h*4);
}
typedef int (__cdecl *getSnd_t)(const char*);            // FUN_00676170
#define get_snd_id ((getSnd_t)0x00676170)

// Play SOUND_FX_<name> on the NPC's command ring (positional, like vanilla).
// Call on the main game thread. Pass name WITH or WITHOUT "SOUND_FX_".
void npc_say_voice(uint32_t handle, const char* sfxName){
    char full[128];
    if (strncmp(sfxName,"SOUND_FX_",9)!=0){
        _snprintf(full,sizeof full,"SOUND_FX_%s",sfxName); sfxName=full;
    }
    int id = get_snd_id(sfxName);                 // 0 if unknown
    if (!id) return;
    uint8_t* c = creature_of(handle); if(!c) return;

    // ring: begin=*(c+0x588), end=*(c+0x58c), stride 0x44, elem[0]=type.
    // Reserve one slot the way FUN_004a9730 does: if there is spare
    // capacity FUN_004b9900 shifts, else FUN_004be490 grows. The robust
    // DLL-side approach is to call the engine reservation exactly as the
    // handler does (see FUN_004a9730 L221-241). Minimal in-place form
    // (only valid when the ring has spare cap — verify with the BP below):
    uint8_t* end = *(uint8_t**)(c+0x58c);
    // zero the new 0x44 element, set type 0xe and the sound id at +0x40:
    for (int k=0;k<0x44;k+=4) *(uint32_t*)(end+k)=0;
    *(uint32_t*)(end+0x00) = 0xE;                 // PlaySound command
    *(int32_t*) (end+0x40) = id;                  // SOUND_FX numeric id
    *(uint8_t**)(c+0x58c)  = end + 0x44;          // commit
}
```
The in-place form is the MED part (it assumes spare ring capacity and
no realloc). The HIGH/zero-risk form is to call the same
`FUN_004be490`/`FUN_004b9900` reservation the handler uses — capture
its exact ABI with the BP listed below, or just emit the 0x68 record
(R-B) and let `FUN_004a9730` do the ring bookkeeping. For non-positional
SFX, the global Miles path is `FUN_006770e0(name,DAT_017e7d34,
retaddr,1)` then `FUN_00693fe0(name,DAT_017e7d34,retaddr,1)`.

### Buttons — runtime structure (HIGH on layout, via R-B)

Easiest: include 0x3c records in the R-B blob (above). The arrays they
fill live on qm: label `qm+0x9a14+slot*0x200`, action id
`*(int*)(qm+0x9a00+slot*4)`, jump key `qm+0x9b14+slot*0x200`, slots
0..4, slot = first row whose `qm+0x9a14` byte is `\0`. Direct pokes are
possible but the GUI also needs the dialog active (`+0x14&0x80000`) and
expects the action-id resolved like the handler does — prefer the 0x3c
record so `FUN_00499ba0` does the resolve + RES:%d formatting.

---

## Proposed Lua API shape (for the SDK author — design only)

```lua
-- text strings must be pre-registered in global.res (text.lua) and
-- referenced by their resource NAME (no "res:").
o:say("MYDLG_LINE1")                       -- arm one dialog line, no voice
o:say("MYDLG_LINE1", "MY_VOICE_01")        -- + positional voice on o
o:dialog{
  text   = "MYDLG_GREET",                  -- res name
  voice  = "HQ_MY_GREET_01",               -- optional, SOUND_FX_ prefix auto
  buttons = {                              -- max 5
    { label = "MYBTN_YES", action = 4201, jump = "node_yes" },
    { label = "MYBTN_NO",  action = 0,    jump = "node_bye" },
  },
}
o:dialog_close()                           -- arm DlgNPC idx 0 / entry+0x4c=0
```
Implementation = build the 0x1f (+0x3c per button +0x68) TLV blob and
push it through the SDK's existing `FUN_00475680` replay shim (R-B).
`o:say` is the thin form (single 0x1f, optional 0x68). The SDK's
global.res registration remains the text-authoring entry point;
`o:say`/`:dialog` take the *resource name*, resolve to a handle
internally if a raw handle is ever needed (`FUN_00672740`).

---

## Confidence / what is HIGH vs needs a BP

| Item | Conf | Note |
|---|---|---|
| `FUN_0048f9e0` is arm-only, writes `entry+0x4c=*(rec+0xc)`, sets `+0x14&0x80000`, no window draw | **HIGH** | full decompile; only caller is `FUN_00475680` case 0x1f |
| Dialog text = global.res `res:NAME` handle (`FUN_00672740` hash\|0x80000000), resolved by `FUN_006726f0` | **HIGH** | resolver chain decompiled; matches questbook_resolver.md |
| Custom text MUST be a registered global.res entry (SDK text.lua path) | **HIGH** | resolver returns 0 for unknown names → no display |
| Button arrays: label `qm+0x9a14+slot*0x200`, id `qm+0x9a00+slot*4`, jump `qm+0x9b14+slot*0x200`, ≤5 slots | **HIGH** | full `FUN_00499ba0` decompile |
| Voice: SOUND_FX table 0x00964870/0x44, `FUN_00676170` getSndType, ring type 0xe @ `+0x588/+0x58c` `elem+0x40=id` | **HIGH** | `FUN_004a9730` decompile + triggers_dialog_move §B |
| Conversation only armed via a record through `FUN_00475680`/`0045f220`/`00807760`; no standalone pop call | **HIGH** | exhaustive xref of the chain |
| `0x11c` net event = "show this dialog on all clients (incl. self in MP)"; SP local arm suffices | **HIGH** | `FUN_0048f9e0` MP block + receiver `FUN_00807760`→`FUN_00463240` |
| R-B (replay 0x1f/0x3c/0x68 blob through dispatcher) is byte-identical to vanilla | **HIGH** | same path the compiler/save/MP use |
| Record buffer `+0xc` = content handle for R-A direct-call | **MED** | offset inferred from handler reads; confirm with entry BP |
| In-place ring poke (no realloc) for voice | **MED** | only valid with spare cap; use `FUN_004be490`/`FUN_004b9900` or R-B for HIGH |
| GUI reads `+0x14&0x80000`+`entry+0x4c`+button arrays to render the box | **MED** | inferred from what the arming writes; the GUI fn itself (large) not pinned this pass |

### Live BPs to close the MEDs (cheap, one capture each)
1. BP `0x0048F9E0` entry while a vanilla quest NPC dialog opens → dump
   `param_2`, `[param_2+0xc]` (== resolved text handle), `[param_2+0x14]`
   bit 0x80000, and the matched DlgNPC `entry+0x4c` after return. Pins
   the R-A buffer layout (turns R-A MED→HIGH).
2. BP at the `call FUN_006726f0`/text formatter inside the dialog GUI
   (find via a HW-read BP on a known `entry+0x4c` while the box is on
   screen) → pins the conversation-window renderer VA and confirms it
   reads `entry+0x4c` + `+0x14&0x80000` (turns the GUI row HIGH).
3. BP `0x004A9730` L221-241 while a vanilla voiced line plays → capture
   the `FUN_004be490`/`FUN_004b9900` arg convention for a HIGH
   DLL-side ring append (or just use R-B).
4. BP `0x00499BA0` on a vanilla multi-choice dialog → confirm slot
   selection (`qm+0x9a14` stride 0x200) and that selecting a button
   runs the `qm+0x9a00[slot]` action.

## Files / VAs added or confirmed this pass
- decompiled (new): `00471000` (QstNpcIdx processor, emits 0x1be),
  `006be400` (hero dialog-state SM — not the window), `0052ab70`
  (QstNpcIdx subvector iterator), `00807760` (0x11c net-receiver
  trampoline → `FUN_00463240`), `00807760`/`007dc260` (net dispatch).
- decompiled (re-read, load-bearing): `0048f9e0` (Dialog arm — proven
  arm-only), `00499ba0` (SetButton arrays), `004a9730` (voice/ring),
  `00463240` (full talk binder), `004a7760` (Popup/NPC_TalkTo arm),
  `00465220` (`entry+0x4c` setter), `005498f0` (`+0x245` stamp),
  `00475680` (dispatch convention: ECX=qm, param1=param2=&record buf).
- xrefs: `FUN_005498f0` (24 callers), `FUN_0048f9e0` (1: dispatcher
  only), `FUN_00463240` (2: tag-6 + 0x11c receiver), `FUN_004a7760`
  (1: dispatcher). Confirms dialog is record-driven, not poll-driven.
- consumed docs: `dlgnpc_bind.md`, `triggers_dialog_move.md`,
  `quest_storyline.md`, `questbook_resolver.md`, `globalres_format.md`,
  `funkcode_tags.md`.

---

## Dialog-arming call ABI — pinned (2026-05-16)

All ABI below is from **direct capstone disassembly** of
`Sacred_decrypted.exe` (base 0x400000), not decompiler inference.
Ghidra renders the handler calls as `FUN_xxxx()` with no args because
it never settled the convention — the real convention is in the
prologues + call sites disassembled here. Every claim is byte-cited.

### 1. The record dispatcher `FUN_00475680` — EXACT ABI (HIGH)

**Convention: `__thiscall`, ECX = qm = `exe_base+0x00AACF80`, SIX
stack args, callee-pop, `ret 0x18`.**

Prologue (0x00475680): `push -1 / push 0x85f4a0 / push fs:[0]`
(SEH, 12 B) `; sub esp,0xc2c ; push ebx/ebp/esi/edi (16 B) ;`
**`mov ebx,ecx`** @0x4756b2 (ECX saved → ebx = qm, used as `esi+0x755c`
DlgNPC vector etc. throughout). Arg slots (retaddr @ esp+0xc48):

| param | esp off | role (from disasm) |
|---|---|---|
| param_1 | +0xc4c | **buffer base** (record bytes). `esi=eax+[esp+0xc4c]` @0x475742 |
| param_2 | +0xc50 | **int\* cursor** (byte offset into buffer). `eax=*[param_2]` @0x4756cc; if `==-1` → stream via `FUN_00849986`; else record at `param_1+*cursor`; `*cursor += BEsize` @0x47578b |
| param_3 | +0xc54 | aux ptr (used only by tag-6/0x10 pre-switch helpers `FUN_004915a0`/`FUN_00491170`) |
| param_4 | +0xc58 | `edi`; net/replication ptr. **case 0x68 branches on `edi==0`** (0x475e7e): edi==0 → arg2=0xffff; else arg2=`*(u16)(edi+0xc)` |
| param_5 | +0xc5c | `char`; if `!=0` runs the tag-6/0x10 TalkTo pre-binder (0x475796). **Pass 0** for plain Dialog/Button/Sound replay |
| param_6 | +0xc60 | extra dword (present: `ret 0x18` = 24 = 6 dwords) |

Cursor self-init: NO. Caller supplies `int cursor` and a pointer to
it. Size read: **`size = *(u16 BE)(buffer + cursor + 2)`** @0x47573d
(`mov cx, word[eax+edx+2]` — eax=cursor, edx=buffer base) → big-endian,
**includes the 3-byte header**, must be `>0` and (`==0x800` or `<0x800`)
else bail. The record is `rep movs`-copied into stack buf
`local_a0c` (`lea edi,[esp+0x23c]` @0x47575f) and the per-tag handler
gets a pointer to **that copy** (`lea ecx,[esp+0x23c]`), NOT the
caller's buffer. So the on-disk record needs the **`tag, sizeBE_hi,
sizeBE_lo`** 3-byte header then a **leading `0x00` flags/align byte**
then the opcode stream (vanilla-confirmed below; same shape as the
proven tag-0x28 path in `dlgnpc_bind.md`).

Per-tag handler call sites (jump table @0x004784c0[tag*4]):

```
; case 0x01 CreateNPC (0x475828) — the PROVEN createnpc_engine pattern:
  lea ecx,[esp+0x23c] ; push ebp ; push ecx ; mov ecx,ebx ; call 0x482510
; case 0x1f Dialog -> FUN_0048f9e0 (0x477b81):
  lea ecx,[esp+0x23c] ; push ebp ; push ecx ; mov ecx,ebx ; call 0x48f9e0
; case 0x3c SetButton -> FUN_00499ba0 (0x47616c):
  lea eax,[esp+0x23c] ; mov ecx,ebx ; push eax        ; call 0x499ba0
; case 0x68 PlaySound -> FUN_004a9730 (0x475e7e):
  test edi,edi ; je .z
  mov dx,[edi+0xc] ; lea eax,[esp+0x23c] ; push edx ; push eax ; mov ecx,ebx ; call 0x4a9730
 .z: lea ecx,[esp+0x23c] ; push 0xffff ; push ecx ; mov ecx,ebx ; call 0x4a9730
```

Real in-exe call sites of `FUN_00475680` (single-record replay form):
```
; @0x0046bd49 (6 args, ECX=qm):
  mov ecx,0xaacf80
  push edi ; push [esp+0x409c] ; push [esp+0x4098]      ; p6,p5,p4
  push [esi+eax+0x48]                                    ; p3
  push <&cursor lea[esp+0x20]> ; push <&buf lea[esp+0x94]>; p2,p1
  call 0x475680
; @0x00470b03 (the StartCode/save replay path — cleanest):
  mov ecx,0xaacf80
  push 0 ; push 0 ; push 0 ; push -1                     ; p6,p5,p4,p3
  push <&cursor lea[esp+0x44]> ; push edx(=*[esi]=buf)   ; p2,p1
  call 0x475680
; @0x00470aea: identical, push 0/0/-1/&cur/buf, ecx=0xaacf80
```
Conclusion: replay one record with `FUN_00475680(buf, &cursor, -1, 0,
0, 0)`, ECX=qm, where `cursor` is an `int` initialized to **0** (the
record starts at offset 0 of `buf`; the on-disk/save form streams when
cursor==-1, but the in-memory buffer form indexes `buf+cursor` and we
control the buffer so cursor=0 points at our record). Handler advances
`*cursor` by the BE size; one record processed then returns 1.

### 2. EXACT TLV bytes (verified vs vanilla Vampiress `FunkCode.bin`)

Real path: `…\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin`
(3 963 314 B, walks byte-exact: 125 060 records, full file consumed
under `size=(d[+1]<<8)|d[+2]`, header-inclusive). The blueprint's `sdk\bin\…`
path does not exist — the bins live under `bin\` not `sdk\bin\`.

Framing (re-confirmed): `tag:u8 | size:u16 BE (incl. 3-byte header) |
payload`. **payload[0] = `0x00`** leading flags/align byte (walker
copies from buf+0, handlers parse the opcode stream from payload+1 —
identical to the tag-0x28 DlgNPC-def shape in `dlgnpc_bind.md`).

Real vanilla examples (hexdump + decode):

- **0x1f @ file off 0x08c78b** (size 0x0019=25):
  `1f 00 19 | 00 | 1c 00000000 | 16 "DQ1_BRINGE_NPC" 00`
  → leading 00, opcode `0x1c`(i32 content/dialog id =0), opcode
  `0x16`(ASCIIZ DlgNPC name), END. (op 0x16 ≡ op 0x01 for the
  name-select branch in `FUN_0048f9e0`; both hit the
  `FUN_00859690(entry+4,…)` name compare.)
- **0x3c @ file off 0x000252** (size 0x0015=21):
  `3c 00 15 | 00 | 01 "res:1024" 00 | 01 "ok_hq" 00 | 00`
  → opcode-1 string #1 = action/RES key, opcode-1 string #2 = jump key.
- **0x3c @ off 0x0002cb** (0x19): `…01 "res:1024" 01 "trigger99"…`
- **0x68 @ file off 0x00022c** (size 0x0026=38):
  `68 00 26 | 00 | 01 "HQ_3_1_4_glad_NPC_Auftrag_Qoffen" 00 | 00`
- **0x68 @ off 0x000329** (0x2f):
  `68 00 2f | 00 | 01 "SOUND_FX_HQ_3_4_2_DELF_NPC_AUFTRAG_QOFFEN" 00 | 00`
  (sample name with or without the `SOUND_FX_` prefix — handler
  prefixes it if absent, `FUN_004a9730` @0x4a97b6).

Records the SDK emits (self-verified: BE size == total length,
re-parses under the walker rule):

```
; (a) Dialog: NPC <DlgName> says <textResName>
1F 00 1D  00  01 "CaptMiles\0"  01 "MYDLG_LINE1\0"  00
   bytes: 1f001d00 01 436170744d696c657300 01 4d59444c475f4c494e453100 00
; (b) SetButton (label/RES, jump key) — vanilla 0x3c shape
3C 00 16  00  01 "res:1024\0"  01 "ok_hq\0"  00
   bytes: 3c001600 01 7265733a3130323400 01 6f6b5f687100 00
; (c) PlaySound (voice sample on the speaker)
68 00 1B  00  01 "SOUND_FX_MY_VOICE_01\0"  00
   bytes: 68001b00 01 534f554e445f46585f4d595f564f4943455f303100 00
```
Size field: **u16 big-endian, inclusive of the 3-byte header** (every
vanilla 0x1f/0x3c/0x68 above satisfies `BEsize == record length`;
matches `funkcode_tags.md` §0). Opcode `0x01`/`0x16` = ASCIIZ;
`0x0b`/`0x1c` = i32; `0x00` = END.

NOTE on text routing (carry from §"Where the TEXT comes from"): the
opcode-1 *second* string in the 0x1f is parsed by `FUN_00472bc0` and
its resolved handle is what the dispatcher deposits where
`FUN_0048f9e0` reads it. `FUN_0048f9e0` reads the content handle from
**arg2+0xc** (`mov edx,[edi+0xc]` @0x48fb2c/0x48fb80/0x48fd36/0x48fda2,
edi = arg2 = [esp+0x100]). In the dispatcher both pushes for case 0x1f
are `push ebp`(arg2) `push ecx`(arg1=record copy); **the dispatcher
owns ebp** (set once @0x4756c0 from `FUN_0084a961`) — we do **not**
construct arg2 in R-B. This is the decisive reason R-B is HIGH and
direct-call R-A is MED (R-A must fabricate the arg2+0xc content slot,
whose producer is the dispatcher's own ebp setup).

### 3. Recommended SDK implementation — **R-B, HIGH**

Use the proven `createnpc_engine` invocation style (player_state.cpp
L813): same `__thiscall`/ECX=qm/SEH-record-buffer pattern, just target
`FUN_00475680` and feed it our 0x1f(+0x3c+0x68) blob one record at a
time. R-B is byte-identical to vanilla/save/MP and never touches the
dispatcher-private arg2. Confidence **HIGH** (ABI fully disasm-pinned;
2+ real call sites; vanilla TLV byte-matched).

```cpp
// FUN_00475680: __thiscall, ECX=qm, 6 args, ret 0x18. Replay ONE record.
// p1=buffer base, p2=&cursor(int, =0 -> record at buf+0), p3=-1,
// p4=0 (no net), p5=0 (no TalkTo pre-bind), p6=0. Engine advances
// *cursor by the BE size and returns 1. Modeled on createnpc_engine.
typedef int (__thiscall* fn_funk_dispatch)(
    void* qm, const void* buf, int* cursor,
    int p3, int p4, char p5, int p6);
#define QM_RVA 0x00AACF80
#define DISP_RVA 0x00475680

static bool funk_replay_one(uintptr_t reb, const uint8_t* rec, size_t n){
    void* qm = (void*)(reb + QM_RVA);
    uint8_t buf[0x208] = {0};            // <0x1f5 per walker guard
    if (n == 0 || n > sizeof(buf)) return false;
    for (size_t k=0;k<n;++k) buf[k]=rec[k];
    int cursor = 0;                       // record starts at buf+0
    __try {
        ((fn_funk_dispatch)(reb + DISP_RVA))(
            qm, buf, &cursor, -1, 0, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// sacred.dialog_arm(npc_dlg_name, text_res_key[, buttons][, voice]):
//  1. emit 0x1f { 01 <npc_dlg_name\0>  01 <text_res_key\0> 00 }
//  2. per button (<=5): emit 0x3c { 01 <label_or_RES\0> 01 <jump\0> 00 }
//  3. if voice: emit 0x68 { 01 <SOUND_FX_<voice>\0> 00 }
//  Each via funk_replay_one (separate call, fresh cursor=0). Prereq:
//  dlgnpc_bind already ran (entry+4 == npc_dlg_name, +0x245 stamped);
//  text_res_key registered in global.res (text.lua) so it resolves.
```

Record builder (TLV; size = u16 BE, includes 3-byte header):
```cpp
static size_t put_rec(uint8_t* o, uint8_t tag,
        const char* s1, const char* s2 /*nullable*/){
    size_t p = 3; o[p++] = 0x00;                 // lead flags byte
    o[p++] = 0x01; while(*s1) o[p++]=(uint8_t)*s1++; o[p++]=0;
    if (s2){ o[p++]=0x01; while(*s2) o[p++]=(uint8_t)*s2++; o[p++]=0; }
    o[p++] = 0x00;                                // END
    o[0]=tag; o[1]=(uint8_t)(p>>8); o[2]=(uint8_t)(p&0xff);  // BE size
    return p;                                     // == total length
}
// Dialog : put_rec(b,0x1f, npc_dlg_name, text_res_key)
// Button : put_rec(b,0x3c, label_or_RES, jump_key)
// Voice  : put_rec(b,0x68, sfx_name, nullptr)   // "SOUND_FX_" auto-added
```

Do NOT call `FUN_00672740` from the DLL to pre-resolve text — the SDK
already established (runtime_triggers.cpp L867-899) that the engine
resolver crashes uncatchably if hit before its cache exists. R-B does
not need it: the dispatcher's own `FUN_00472bc0` parse resolves the
opcode-1 string at replay time (when the dict is loaded), exactly like
vanilla. The text key just has to be a registered global.res name
(SDK text.lua path) — pass the **bare name**, no `res:` prefix.

Threading/lifetime: call on the **main game thread tick** (same thread
the engine runs `FUN_00475680` on), after `dlgnpc_bind` and after the
global.res dict is loaded. One record per `funk_replay_one` call,
fresh `cursor=0` each (the dispatcher processes exactly one record and
returns; do not batch multiple records in one buffer unless you also
loop the cursor — single-record calls are simpler and crash-proof).

Per-claim confidence:
- `FUN_00475680` = __thiscall/ECX=qm/6 args/`ret 0x18`, p1=buf,
  p2=&cursor, p4 net, p5 TalkTo-gate — **HIGH** (prologue + jump table
  + 3 real call sites all disassembled).
- case 0x1f→0x48f9e0 (push ebp,push recbuf,ecx=qm), 0x3c→0x499ba0
  (push recbuf,ecx=qm,`ret 4`), 0x68→0x4a9730 (push p2,push recbuf,
  ecx=qm,`ret 8`) — **HIGH** (each handler prologue + terminal
  epilogue + call site disassembled; FUN_0048f9e0 clean `ret 8`,
  FUN_00499ba0 frame reads single arg @esp+0x21c then `ret 4`,
  FUN_004a9730 epilogue `add esp,0x1ec; ret 8`).
- TLV byte shapes (BE size incl 3 hdr, payload[0]=00 lead, op1/op16
  ASCIIZ) — **HIGH** (matched to ≥2 real vanilla 0x1f/0x3c/0x68
  records, full-file byte-exact walk).
- R-A direct-call (arg2+0xc content slot) — **MED**, not recommended;
  R-B avoids it.

Residual uncertainty + the single cheap BP that closes it: the only
unproven point is whether the in-memory-buffer path (`cursor=0`, not
the `cursor==-1` stream path the save replay uses) tiles correctly for
a lone record. Static evidence is strong (size read = `*(u16
BE)(buf+cursor+2)`, handler copies `BEsize` bytes from `buf+cursor`,
sets `*cursor += BEsize`), and caller @0x46bd49 uses exactly this
in-memory `&cursor`/`buf` form (not -1). **One BP** settles it with
zero ambiguity:

> BP `0x00475742` (`mov esi,[eax+edx]`, i.e. record-fetch) on the
> *first* `funk_replay_one` call. Dump `EAX` (= *cursor, expect 0),
> `EDX` (= buffer base = our `buf`), and `word[EAX+EDX+2]` (= the BE
> size, expect e.g. 0x1d for the 0x1f record). If EAX==0,
> EDX==&buf, and the size matches the emitted record → R-B is
> confirmed end-to-end and the BP can be removed permanently. (No
> other BP needed; the handler ABIs are statically closed.)


---

## Why the dialog window does not open — R-B gap (2026-05-16)

Decisive, raw-disasm root cause for: dlgnpc_bind OK, `[dialog_arm] ...
-> armed` logged, CAPMILES_GREET registered, yet NO conversation window
in-game. **The pinned-ABI doc's flagged suspicion is CONFIRMED, and it
is NOT R-A-only — it breaks R-B too.** The `-> armed` log is a false
positive: funk_replay_one returns true because the dispatcher ran
without crashing, but the Dialog handler silently no-op'd.

### Root cause #1 — FUN_0048f9e0 no-ops because param_2 == 0 (PROVEN)

The pinned ABI doc said the dispatcher case 0x1f does
`lea ecx,[esp+0x23c]; push ebp; push ecx; mov ecx,ebx; call 0x48f9e0`
and that "the dispatcher owns ebp". Raw disasm of who sets ebp
(0x475680 prologue):

    004756a6 push 0
    004756a8 push 0x8eb660        ; TypeDescriptor ".?AVcCreature@@"
    004756ad push 0x8eb648        ; TypeDescriptor ".?AVcObject@@"
    004756b2 mov  ebx, ecx        ; ebx = qm
    004756b4 push 0
    004756b6 push edi             ; edi = param_4  (dispatcher arg #4, [esp+0xc58])
    004756bb call 0x84a961        ; FUN_0084a961 == __RTDynamicCast
    004756c0 mov  ebp, eax        ; ebp = dynamic_cast<cCreature*>((cObject*)param_4)

So **ebp = __RTDynamicCast(param_4, 0, &TD("cObject"), &TD("cCreature"),
0)** — i.e. the dispatcher casts its 4th arg to cCreature*. FUN_0084a961
L25: `if (param_1 == 0) return 0;`.

FUN_0048f9e0 prologue + body (raw disasm, confirms decompile):

    0048f9fe mov edi, [esp+0x100]   ; edi = param_2 = the pushed ebp
    0048fa07 mov esi, ecx           ; esi = ECX = qm
    0048fa09 cmp edi, ebx           ; ebx = 0
    0048fa13 je  0x48fef2           ; param_2 == 0  -> JUMP TO EXIT, body skipped
    ... all content/gate use edi: [edi+0xc] (-> entry+0x4c), [edi+0x14] (0x80000 gate)
    0048fb2c mov edx,[edi+0xc]   0048fb42 mov eax,[edi+0x14]
    0048fd83 and dword [edi+0x14],0xfff7ffff   0048fda2 mov edi,[edi+0xc]

funk_replay_one (player_state.cpp L880) calls the dispatcher with
**p4 = 0**:
`((fn_funk_dispatch)(reb+0x475680))(qm, buf, &cursor, -1, 0, 0, 0);`
=> param_4 = 0 => __RTDynamicCast(0,...) = 0 => ebp = 0 => param_2 = 0
=> `je 0x48fef2` => **FUN_0048f9e0 returns immediately, does nothing**:
never scans the DlgNPC array, never writes entry+0x4c, never touches
cCreature+0x14&0x80000, never stamps +0x245. The conversation window
has no armed content and no active gate => never opens.

This also corrects the prior docs: param_2 is NOT "the record buffer"
and NOT "the owning record's +0x14". param_2 = the dispatcher's
dynamic-cast of param_4 to **cCreature***; `param_2+0xc` = **cCreature
+0xc** (the creature's own attached dialog content), `param_2+0x14` =
**cCreature+0x14** (the 0x80000 gate is set ON THE CREATURE). The
record buffer is param_1 (=&local_a0c); FUN_0048f9e0 uses it only via
FUN_00472bc0 to read the opcode-1 DlgNPC NAME (into qm+0xa460) for the
array name-match. The DIALOG TEXT key is NOT used by FUN_0048f9e0 at
all — the content written into entry+0x4c is *(cCreature+0xc), not the
resolved text handle. (The text-handle-from-record model in the older
"Where the TEXT comes from" / triggers_dialog_move L265-269 sections is
WRONG for 0x1f; superseded here.)

### Root cause #2 — TalkTo (tag 6) also skipped (param_5 == 0)

The other arming primitive, tag-6 TalkTo -> FUN_00491170 -> FUN_00463240
(self-contained: it pulls the creature from its OWN opcode-1 into
qm+0xa880, casts that, writes entry+0x4c = *(creature+0xc), sets
creature+0x14|0x80000, stamps +0x245). But the dispatcher only routes
tag 6 in the **param_5 pre-switch** (raw disasm 0x47578d):

    0047578d mov al,[esp+0xc5c]   ; param_5
    00475794 test al,al
    00475796 je  0x4757de         ; param_5 == 0  -> SKIP tag-6/0x10 entirely
    004757a4 cmp eax,6 / je 0x4757c7 ... call 0x491170  (TalkTo)

funk_replay_one passes **p5 = 0**, so a tag-6 record would also be
skipped. With p3=-1, p4=0, p5=0 (the save/StartCode replay form), the
dispatcher can replay CreateNPC/SetIcon/Sound/etc but **cannot arm an
interactive conversation** — by design: dialogs are armed only on the
live talk-interaction route @0x46bd49 (the FunkCode block executor),
which supplies a real cObject (the dialog owner) as param_4 and the
SelfTriggerQuest ptr as param_3.

### Root cause #3 — content source is cCreature+0xc (likely 0 on a spawn)

Even if armed correctly, BOTH FUN_0048f9e0 (name branch L86-87) and
FUN_00463240 (L106-107) set `entry+0x4c = *(cCreature+0xc)` — the
creature's own attached dialog-content object. For a vanilla story NPC
this is populated at creation from its TYPE/StartCode dialog binding.
Our runtime createnpc_engine spawn has no such attachment, so
cCreature+0xc is almost certainly 0 => entry+0x4c set to 0 => window
still empty. (MED — needs the BP below to confirm the spawn's +0xc; but
it is a hard prerequisite regardless of how arming is invoked.)

### The fix recipe

There is NO data-only poke and NO save-replay-form record that opens
the window; arming requires a cCreature* delivered to the handler. Two
engine-faithful options, both on the main game tick, post-bind:

**FIX A (PREFERRED, HIGH) — call FUN_00463240 directly.** It is the
self-contained talk binder; it resolves the creature itself, sets
entry+0x4c = *(creature+0xc), creature+0x14|0x80000, stamps +0x245, and
registers the SelfTriggerQuest block — exactly what a vanilla TalkTo
does. ABI (from FUN_00491170 L155 call + FUN_00463240 prologue,
__thiscall ECX=qm):

    typedef void(__thiscall* fn_talkbind)(void* qm, uint32_t npcHandleOrObj,
        const char* dlgName, unsigned dlgIdx, const char* selfTrigQuest);
    // npcHandleOrObj : value FUN_00464bd0 accepts (the creature handle,
    //                  <0x11 fast-path or object-mgr id); the SAME handle
    //                  dlgnpc_bind used.
    // dlgName        : entry+0x04 string dlgnpc_bind wrote ("Captain Miles").
    // dlgIdx         : the DlgNPC index dlgnpc_bind returned.
    // selfTrigQuest  : a SelfTriggerQuest key string (non-null; "" ok).
    ((fn_talkbind)(reb+0x00463240))((void*)qm, (uint32_t)handle,
                                    dlgName, (unsigned)dlgIdx, "");

Confidence HIGH on the path/writes (full FUN_00463240 + FUN_00491170
decompile + the cCreature dynamic_cast disasm). The ONE gating
unknown is Root cause #3 (is *(cCreature+0xc) a valid dialog content
for our spawn?). If it is 0, the window opens but is empty — then
entry+0x4c must be pointed at a real global.res content handle
explicitly (write the resolved CAPMILES_GREET handle into
entry+0x4c AFTER FUN_00463240, or set cCreature+0xc to a content obj
before calling). 

**FIX B (engine-faithful replay) — give the dispatcher the creature.**
Call FUN_00475680 the way the live talk route does, NOT the save form:
pass **param_4 = the NPC's cObject* (= npc_creature(handle))** and
**param_5 = 1**, replaying a tag-6 TalkTo TLV (or tag-0x1f). Then
ebp = dynamic_cast<cCreature*>(param_4) is non-null and the handler
runs. Modify funk_replay_one to take a creature ptr:

    ((fn_funk_dispatch)(reb+0x00475680))(
        qm, buf, &cursor, /*p3 selfTrig*/ -1,
        /*p4*/ (int)npc_creature(handle), /*p5*/ 1, /*p6*/ 0);

with a tag-6 record `06 00 <sz> 00  01 "<creature res/handle key>\0"
09 "<DlgNPC name>\0" 00` (op1 -> qm+0xa880 creature, op9 -> DlgNPC
name). MED: the exact opcode-9 vs opcode-1 split and what op1 must
encode to make FUN_00464bd0 resolve OUR spawn need one capture.

### Cheapest single BP (settles Root cause #3 + FIX choice)

BP `0x0048FA13` (the `je 0x48fef2`) while walking up to a VANILLA story
NPC that opens its window: confirm param_2 (EDI) != 0 and dump
`[EDI+0xc]` (the content handle the engine uses) and `[EDI+0x14]`
bit 0x80000. Then BP `0x004632XX` (FUN_00463240 L106, the
`entry+0x4c = *(iVar3+0xc)` store) on our bound spawn after FIX A:
read `*(creatureObj+0xc)`. If nonzero -> FIX A is sufficient (HIGH). If
zero -> also need to point entry+0x4c at a resolved global.res content
handle (the CAPMILES_GREET handle) explicitly, or attach a content obj
at cCreature+0xc, after the bind. One capture each; definitive.

### Files / VAs this pass
- raw disasm: 0x475680 prologue (ebp = __RTDynamicCast(param_4) of
  param_4 -> cCreature; TD ptrs 0x8eb648 ".?AVcObject@@" / 0x8eb660
  ".?AVcCreature@@"), 0x47578d param_5 pre-switch (tag-6 gate),
  0x46bd49 vs 0x470b03 call sites (live-talk passes real obj as p4;
  save form passes 0), 0x48f9e0 prologue + `cmp edi,ebx / je 0x48fef2`
  + all [edi+0xc]/[edi+0x14] uses.
- decompile re-read: 0048f9e0 (L44 `if(param_2!=0)` gate), 00475680
  (record copy into local_a0c, case 0x1f), 00491170 (TalkTo tag-6 ->
  FUN_00463240 L155), 00463240 (creature cast + entry+0x4c=*(cre+0xc) +
  creature+0x14|0x80000 + SelfTriggerQuest), 00472bc0 (opcode parse ->
  qm+0xa880/+0xa860/+0xa460; resolves text to qm+0xa880, NOT to
  entry+0x4c for 0x1f), 0084a961 (__RTDynamicCast, returns 0 for null
  param_1), 00464bd0 (handle->object resolver used by FUN_00463240).


---

## Talk-route — making the window open for a runtime NPC (2026-05-16)

Definitive, **raw-capstone-only** pin of `FUN_00463240` (the self-
contained "Talk_%s_Dlg_%d" / "SelfTriggerQuest%d" binder), its two real
in-exe call sites, the handle resolver `FUN_00464bd0`, and the exact,
crash-proof recipe. Every byte cited; no decompiler inference.
Format strings verified: `0x0094e140 = "Talk_%s_Dlg_%d"`,
`0x0094ddcc = "SelfTriggerQuest%d"` — this is unambiguously the binder.

### 1. `FUN_00463240` — EXACT ABI (HIGH, fully disasm-pinned)

**Convention: `__thiscall`, ECX = qm = `exe_base+0x00AACF80`, FOUR
stack args, callee-pop, `ret 0x10`.** Prologue @0x00463240:
`push -1 / push 0x85e913 / push fs:[0]` (SEH) ; `sub esp,0x124` ;
`push ebx/ebp/esi` ; **`mov esi,ecx` @0x46325e** (ECX→esi = qm; used as
`[esi+0x755c]/[esi+0x7560]` DlgNPC vector begin/end throughout) ;
`mov ecx,[esp+0x140]` @0x463260 = **arg1**. Epilogue @0x004636b6
`ret 0x10` (4 dwords). Stack-arg slots (caller frame, retaddr @esp+0x13C
at 0x463260):

| param | caller off | reg at site | type | meaning (from disasm) |
|---|---|---|---|---|
| arg1 | esp+0x140 | `push edx`/`push ecx`(net) | u32 | **creature reference key**. `push ecx;push &out[esp+0x28];mov ecx,esi(qm);call 0x464bd0` @0x46326f-76; `test al,al; je 0x46369e` (resolver fail → whole fn no-ops). Then `edx=[esp+0x2c]`(=resolved om idx), `ecx=[0xad5c40]`(om), `push edx; call 0x5fe000`(om get-by-idx) → `__RTDynamicCast`(0x84a961) to **cCreature\*** → `ebx`; `je 0x46369e` if null |
| arg2 | esp+0x144 | `push ecx`=`lea[esp+0x98]` | char\* | **DlgNPC name** ASCIIZ. Fed as `%s` to `Talk_%s_Dlg_%d` (`0x849ae1(buf,0x94e140,dlgIdx,name)` @0x4632ee) |
| arg3 | esp+0x148 | `push ebx` | i32 | **DlgNPC array index**. `%d` of Talk_…_Dlg_%d; drives the `entry+0x4c` write + `FUN_005498f0(idx,1)` |
| arg4 | esp+0x14C | `push esi`/`push <net>` | i32 | **SelfTriggerQuest id** (int, `%d` of "SelfTriggerQuest%d" @0x463302). 0/"" is safe (just names an empty trigger block) |

**What it writes (all confirmed in disasm):**
- `oldIdx = FUN_00549920(ECX=cCreature)` @0x463312. If `0 < oldIdx <
  count`: `*(qm+0x755c + oldIdx*0x50 + 0x4c) = 0` ; `FUN_0045ee20(3,0,0)`
  @0x463340-59 — **clears the previously-active DlgNPC entry's +0x4c**.
- `ebp = *(cCreature+0xc)` @0x46335c (`mov ebp,[ebx+0xc]`).
- If `0 < arg3 < count`: `*(qm+0x755c + arg3*0x50 + 0x4c) = ebp` i.e.
  **`DlgNPC[arg3]+0x4c = *(cCreature+0xc)`** ; `FUN_0045ee20(3,0,0)`
  @0x463386-9c. (Content source is the creature's own +0xc — see
  residual #3.)
- `FUN_005498f0(ECX=cCreature, idx=arg3, net=1)` @0x4633a4-a9 —
  **stamps `cCreature+0x245 = arg3`**, fires 0x11a; clears
  `+0x14&0x80000` *iff* arg3<1 (per dlgnpc_bind.md).
- SelfTriggerQuest: builds `"SelfTriggerQuest%d"` from arg4, walks the
  `DAT_00aab708..0aab70c` table (stride 0x54) name-matching via
  `FUN_00859690`, registers/finds a function block (`FUN_006e6650` /
  `FUN_0065bf20` / ring helpers). With arg4=0 it just keys an empty
  block — **no deref of arg4 as a pointer** (it is an int → %d only),
  so 0 is crash-safe.
- **`*(cCreature+0x14) |= 0x80000` @0x463693-97 — UNCONDITIONAL, at the
  very end** (`mov eax,[esp+0x30](=saved cCreature); or [eax+0x14],
  0x80000`), AFTER FUN_005498f0. So even though FUN_005498f0 would
  clear 0x80000 for idx<1, the binder re-sets it here for every
  non-early-exit path. **Passing arg3 ≥ 1 means the draw gate is set,
  never cleared.** This structurally avoids the prior dispatcher-replay
  "idx 0 = close dialog" regression — FUN_00463240 has no by-name idx-0
  branch at all; arg3 is supplied by us directly.

**Preconditions:** arg1 must resolve through `FUN_00464bd0` to a live
om object that `__RTDynamicCast`s to `cCreature*` (else both
`je 0x46369e` early-exits → total no-op, no crash). arg2 should be the
bound DlgNPC name (used only for the log string; not load-bearing for
arming). arg3 must be the valid DlgNPC index (`0 < arg3 < count`).

### 2. The arg1 handle convention — `FUN_00464bd0` (HIGH)

`FUN_00464bd0`: `__thiscall`, ECX=qm, `(out*, key)`, `ret 8`.
`mov ebx,[esp+0x14]` = `key` ; **`cmp ebx,0x10; ja 0x464bf0`** —
*key ≤ 0x10*: `*out=key; return 1` (literal passthrough). *key > 0x10*:
red-black-tree lookup in `*(qm+0x7668)` (node key @+0x10, value @+0x14);
on miss, brute-scan every om object: `FUN_005fe6d0(om)` count,
`FUN_005fe000(om,i)` get, `FUN_0044a1c0(ECX=obj)` → the object's
**reference key**, `cmp key,that`; on match `*out = i` (the om index).

`FUN_005fe000(ECX=om,idx)` = `ecx=*(om+4); return *(ecx + idx*4)` —
**byte-identical addressing to the SDK's `npc_creature`**
(`arr=*(om+4); c=*(arr+handle*4)`). So **the om index == the SDK
`handle`**. Therefore `FUN_00464bd0` maps *reference key → handle*.
Conclusion: **arg1 must be the creature's reference key, NOT the raw
handle** (unless it happens to be ≤0x10). The vanilla caller gets it
from `*(qm+0xa880)` (op-1 `res:NNNN` resolution). The DLL-faithful
equivalent: read it from our own spawn via the engine's own accessor
**`FUN_0044a1c0` (`__thiscall`, ECX=cCreature, no args, `ret`)** which
returns `FUN_00428830(*(cCreature+0x3c)?... : *(cCreature+0x10))` — the
object's stable reference id. (HIGH on the chain; the only thing to
confirm live is that our spawn's `FUN_0044a1c0` value round-trips
through 464bd0 to our handle — settled by the single BP below, or
sidestepped by FIX A2.)

### 3. The player→NPC interaction route (HIGH on the gate fields)

The on-screen conversation window is the GUI that renders the *armed*
state; it is gated by exactly the fields FUN_00463240 sets, read every
frame off the **creature the player is talking to**:
- `cCreature+0x245` (the DlgNPC index, stamped by `FUN_005498f0`) — the
  marker/objIdx resolver `FUN_00549920` returns it (dlgnpc_bind.md
  linchpin); the dialog GUI uses the same objIdx to find the entry.
- `cCreature+0x14 & 0x80000` — the "dialog active / dialog NPC" gate
  (`FUN_00599910` marker draw L243; the conversation GUI's open test).
- `DlgNPC[idx]+0x4c` — the content handle the window renders
  (`FUN_006726f0` hash-or-ptr resolve).
- `*(cCreature+0xc)` — the creature's own attached dialog content,
  which is the *source* FUN_00463240 copies into `entry+0x4c`.

A vanilla story NPC gets all four via its TYPE/StartCode TalkTo (tag 6)
→ `FUN_00491170` → `FUN_00463240`, which also attaches `+0xc` at
creature creation. Our `createnpc_engine` runtime spawn has the DlgNPC
entry + `+0x245` + name + sprite (dlgnpc_bind ran) **but never went
through the tag-6 binder**, so it lacks: (a) the `+0x14&0x80000` gate
asserted *by the binder* (we set it manually but the prior `dialog_arm`
pure-poke path is fragile and is what failed), and crucially (b) a
populated `*(cCreature+0xc)` content object — and (c) the
SelfTriggerQuest registration the talk-route expects. **The precise
missing call a vanilla NPC has and our spawn does not is exactly
`FUN_00463240` itself** (the engine never invoked it for us because no
tag-6 record targets our runtime spawn, and the save/StartCode replay
form passes p5=0/p4=0 so the dispatcher skips it — see "R-B gap").

### 4. THE recipe — minimum, engine-faithful, crash-proof

**FIX A1 (PREFERRED, HIGH on ABI; MED on arg1 until 1 BP) — call
`FUN_00463240` directly**, modeled on the proven
`createnpc_engine` __thiscall/ECX=qm/SEH-guarded shim in
player_state.cpp:

```cpp
// FUN_00463240: __thiscall, ECX=qm, 4 args, ret 0x10.
//   arg1 = creature REFERENCE KEY (FUN_0044a1c0 of our cCreature),
//          NOT the raw handle (unless <=0x10).
//   arg2 = bound DlgNPC name (entry+0x04 string; log-only, pass it).
//   arg3 = the DlgNPC index dlgnpc_bind stamped (= cCreature+0x245).
//          MUST be >= 1 (idx 0 path would clear the gate before the
//          unconditional re-set; >=1 keeps +0x14&0x80000 ON).
//   arg4 = SelfTriggerQuest id; pass 0 (int, %d only — safe).
typedef void (__thiscall* fn_talkbind)(void* qm, uint32_t refKey,
        const char* dlgName, int dlgIdx, int selfTrigId);
typedef uint32_t (__thiscall* fn_refkey)(void* cCreature); // 0x0044a1c0

bool talk_route_open(int handle, const char* dlg_name) {
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = (uintptr_t)exe - 0x00400000;
    void*  qm  = (void*)(reb + 0x00AACF80);
    void*  cre = (void*)npc_creature(handle); if (!cre) return false;
    int oidx=-1, cnt=-1;
    if (!dlg_entry_by_objidx(reb, handle, &oidx, &cnt)) return false;
    if (oidx < 1 || oidx >= cnt) return false;          // arg3 guard
    uint32_t refKey = 0;
    __try { refKey = ((fn_refkey)(reb+0x0044a1c0))(cre); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!refKey) refKey = (uint32_t)handle;             // <=0x10 fallthrough
    __try {
        ((fn_talkbind)(reb+0x00463240))(qm, refKey, dlg_name, oidx, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
```
Call **once, on the main game tick, AFTER `dlgnpc_bind`** (so the entry
+ `+0x245` exist and `oidx` is valid) and after global.res text is
registered. This sets `entry[oidx]+0x4c = *(cCreature+0xc)`, stamps
`+0x245`, registers the SelfTriggerQuest block, and **leaves
`cCreature+0x14&0x80000` SET** (unconditional re-OR at 0x463697; arg3≥1
so no transient clear matters). It never touches `+0x200`. Confidence:
ABI **HIGH** (prologue + epilogue `ret 0x10` + 2 real call sites
[FUN_00491170 @0x491406, net receiver FUN_00807760 @0x80777e] all
disassembled and cross-agree on 4 args / arg order / ECX=qm).

**Residual (#3, MED): content source.** FUN_00463240 sets
`entry+0x4c = *(cCreature+0xc)`. For our spawn `+0xc` was set by the
existing `dialog_arm` to `sacred_hash("CAPMILES_GREET")|0x80000000`
(ground-truth probe confirms `+0xc = 0x9B13FA50`). So the binder will
copy that resolvable handle into `entry+0x4c` itself — **no extra
write needed**. If a future spawn has `+0xc == 0`, additionally do
`*(uint32_t*)(entry+0x4c) = sacred_hash(key)|0x80000000;` AFTER the
FUN_00463240 call (FIX A2 below makes this unconditional & removes the
arg1 dependence entirely).

**FIX A2 (HIGH, fully static, no BP) — belt-and-braces.** Because the
gate fields are now exactly known, the engine-faithful minimum that
needs *no* live confirmation: call FUN_00463240 as A1 for the
SelfTriggerQuest registration + gate, then immediately re-assert the
two GUI-read fields the way the binder itself does, at the entry the
selector reads (by-objIdx, the SDK's `dlg_entry_by_objidx`):
```cpp
talk_route_open(handle, dlg_name);            // FIX A1
uintptr_t oe = dlg_entry_by_objidx(reb, handle, nullptr, nullptr);
uint32_t content = sacred_hash(text_key) | 0x80000000u;
if (oe) safe_write<uint32_t>(oe + 0x4c, content);   // GUI text
uint32_t v14=0; if (safe_read<uint32_t>((uintptr_t)cre+0x14,&v14))
    safe_write<uint32_t>((uintptr_t)cre+0x14, v14 | 0x80000u); // gate
```
This is idempotent with what FUN_00463240 already wrote and removes
both residual #3 and the arg1-roundtrip uncertainty: even if
`FUN_00464bd0` fails to map our refKey (→ binder early-exits as a
harmless no-op), the explicit `entry+0x4c` + `+0x14|0x80000` writes
still arm the window. **The SelfTriggerQuest block only registers if
the binder ran; it is not required for the window to OPEN** (it gates
quest-trigger-on-talk, not the GUI). So FIX A2 is the robust answer:
window opens with content even in the worst arg1 case. Confidence
**HIGH** (every field/offset statically pinned in §1/§3 above).

**Do NOT** use the dispatcher save/replay form (p4=0/p5=0) — proven
no-op (R-B gap §). **Do NOT** clear `+0x14&0x80000` — neither A1 nor A2
ever does (A1: binder re-ORs unconditionally with arg3≥1; A2: explicit
re-OR).

### 5. The single cheap BP (optional — only to upgrade A1 arg1 MED→HIGH)

Not needed if FIX A2 is used (it is arg1-failure-tolerant). If you want
to confirm A1's arg1 round-trip and residual #3 in one capture:

> BP `0x0046335c` (`mov ebp,[ebx+0xc]`) during the first
> `talk_route_open` on Captain Miles. Dump: `EBX` (= resolved
> cCreature\* — must equal `npc_creature(handle)`; proves arg1/refKey
> round-tripped), `[EBX+0xc]` (= the content the binder copies — expect
> `0x9B13FA50`, proves residual #3 OK), `EDI` (= arg3 dlgIdx, expect
> ==`cCreature+0x245`). One hit, then remove. If EBX matches and
> [EBX+0xc]≠0 → FIX A1 alone is sufficient and HIGH; if EBX is wrong →
> use FIX A2 (which already handles it).

### Per-claim confidence
- FUN_00463240 = __thiscall/ECX=qm/4 args/`ret 0x10`; arg1 refKey,
  arg2 name, arg3 dlgIdx, arg4 selfTrigInt — **HIGH** (prologue +
  `ret 0x10` epilogue + 2 real call sites disassembled & agree).
- Writes entry[arg3]+0x4c=*(cCreature+0xc), stamps +0x245 via
  FUN_005498f0, registers DAT_00aab708 block, **unconditionally
  `cCreature+0x14|=0x80000` @0x463697** (never clears with arg3≥1) —
  **HIGH** (full body disasm).
- arg1 = reference key (FUN_00464bd0 maps refKey→handle; om idx ==
  SDK handle via FUN_005fe000==npc_creature) — **HIGH** (resolver +
  om-accessor disasm); the only live unknown = our spawn's refKey
  round-trips — **MED**, eliminated by FIX A2.
- Window-open gate = cCreature+0x14&0x80000 + +0x245 + DlgNPC[idx]+0x4c
  + content from cCreature+0xc — **HIGH** (binder writes + dlgnpc_bind
  render chain).
- FIX A2 opens the window with content with zero residual, no BP —
  **HIGH** (all fields statically pinned; idempotent, arg1-tolerant).

### Files / VAs this pass
- raw disasm: 0x463240 full (prologue arg-slot math @0x463260
  `[esp+0x140]`=arg1; FUN_00464bd0 call @0x463276; RTDynamicCast block
  @0x46328d-a5; oldIdx clear @0x463312-59; `ebp=*(cre+0xc)` @0x46335c;
  entry+0x4c write @0x463398; FUN_005498f0 @0x4633a4; SelfTrigQuest reg
  @0x4634a6-end; **`or [cre+0x14],0x80000` @0x463693**; `ret 0x10`
  @0x4636b6), call site FUN_00491170 @0x4913ee-0x49140b (push order =
  creature/&name/dlgIdx/net, ecx=qm), net receiver FUN_00807760
  @0x807760-84 (payload[0..2]+&payload[3], ecx=0xaacf80, ret 8 over
  ret 0x10), FUN_00464bd0 full (≤0x10 passthrough; qm+0x7668 RB-tree;
  om brute-scan via FUN_0044a1c0), FUN_005fe000 (om get == npc_creature
  addressing), FUN_0044a1c0/FUN_00428830 (reference-key accessor).
- fmt strings: 0x94e140 "Talk_%s_Dlg_%d", 0x94ddcc
  "SelfTriggerQuest%d" (confirms identity of FUN_00463240).

---

## Runtime "talked to NPC X" signal (2026-05-17)

All ABI below = **direct capstone disassembly** of
`Sacred_decrypted.exe` (base 0x400000, file-off = VA−0x400000) +
Ghidra C of the named functions. No inference where it says HIGH.

### 1. The full path on talk/answer of an SDK-armed DlgNPC (HIGH)

The conversation GUI (reads `entry+0x4c` content + `cCreature+0x14 &
0x80000` gate, set by our FIX-A2 arm) renders the box. When the player
**answers/clicks**, the NPC's dialog-tree FunkCode is replayed through
the record dispatcher `FUN_00475680` (`__thiscall`, ECX=qm=0x00AACF80,
6 stack args; ABI pinned in the "Dialog-arming call ABI" section).
The answer node carries a **tag-0x76 SelfTriggerQuest record**.

Jump-table dispatch (table @ `0x004784C0[tag*4]`, capstone-verified):
- `jumptable[0x76] = 0x00478470`. Handler block bytes:
  ```
  0x00478470: 8d84243c020000  lea eax,[esp+0x23c]   ; &parsed record buf
  0x00478477: 55              push ebp              ; arg2 = cCreature*
  0x00478478: 50              push eax              ; arg1 = &record buf
  0x00478479: 8bcb            mov ecx,ebx           ; ECX = qm
  0x0047847b: e8907a0100      call 0x0048ff10       ; FUN_0048FF10
  0x00478480: b801000000      mov eax,1
  0x00478485: e9ffdeffff      jmp 0x00476389        ; one record -> loop
  ```
  (`jumptable[0x1f]=0x00477b81` = Dialog/FUN_0048f9e0, for contrast.)

**`ebp` provenance (the answered creature) — capstone-pinned in
`FUN_00475680` prologue:**
```
0x004756a6: push 0
0x004756a8: push 0x8eb660            ; cCreature RTTI src type
0x004756ad: push 0x8eb648            ; cObject RTTI dst type
0x004756b2: mov  ebx,ecx             ; ebx = qm (kept all fn)
0x004756b6: push edi                 ; edi = param_4 ([esp+0xc58])
0x004756bb: call 0x0084a961          ; __RTDynamicCast wrapper
0x004756c0: mov  ebp,eax             ; ebp = (cCreature*) of param_4
```
`ebp` is callee-saved and **untouched** between 0x4756c0 and the
0x478479 `push ebp` (verified: no `mov ebp,*` / `pop ebp` on the
fall-through path; the only writes are in unrelated tag branches).
So **arg2 to FUN_0048FF10 = the cCreature\* of the NPC being talked
to** (= dispatcher param_4, dyn-cast to cCreature).

In `decompiled/0048ff10_FUN_0048ff10.c` this is `unaff_retaddr`:
- `:126 if (unaff_retaddr != 0)` — skips everything if creature null.
- `:127 sVar2 = *(i16*)(unaff_retaddr + 0x94)` = quest-NPC array idx
  (same field as quest_lifecycle Q2/Q3).
- `:142 *(int*)(... ) == *(int*)(unaff_retaddr + 0xc)` — guard keyed
  by the creature's current dialog-content handle (cCreature+0xc).
- `in_ECX` = qm (ebx). `param_1` (`puStack_104`) = parsed record buf.

This is the engine's own once-per-(creature,content) reward guard
(quest_lifecycle.md §Q3): `bVar3=true` if a `{[0]==*(cre+0xc),
[1]==0x100}` sub-record already exists in the `qm+0x31c` array entry
`[*(i16)(cre+0x94)]`; if absent it appends it and runs the reward/
broadcast block. **It runs once per distinct (creature, cCreature+0xc)
answer** — i.e. exactly once per answer UNLESS `+0xc` is being
rewritten every tick (our dialog_arm bug, Q3) which makes it refire.

**Single best interception point = entry of `FUN_0048FF10`
(VA 0x0048FF10).** It means precisely "the player just answered a
SelfTriggerQuest dialog node of NPC = (cCreature*)arg2". This is the
only place that fact is materialised with the creature in hand, and it
is exactly the path the live Rocheford-reward fact proved executes.
(The named `read_name_*` self_trigger/dialog_check hooks do NOT fire
for an SDK-armed dialog — ground truth — because our arm path is
FIX-A2/`FUN_00463240`, not the tag-9/10 by-name `FUN_00491170`
branch; confirmed: `FUN_00491170` only reaches `FUN_00463240` after a
name/`a880` match, which our arm bypasses.)

### 2a. POLL option — per-creature, no trampoline (HIGH; PREFERRED)

`FUN_005498f0` (`__thiscall`, ECX=cCreature; called by the arm path
`FUN_0048f9e0`/`FUN_00463240` and 22 others) is the *only* writer of
two adjacent per-creature fields:
```
FUN_005498f0(param_1=dlgIdx, param_2=fireFlag):
  *(int*)(cCreature + 0x245) = param_1;                  // DlgNPC idx
  if (param_1 < 1)
      *(u32*)(cCreature + 0x14) &= 0xFFF7FFFF;            // clr 0x80000
  if (param_2) FUN_0054d760(0x11a);
```
So, per the **bound NPC's own cCreature**:
- **`*(i32*)(cCreature + 0x245)`** = its active DlgNPC entry index
  (`>0` while a dialog is armed/open on it; FIX-A2 stamps it).
- **`*(u32*)(cCreature + 0x14) & 0x80000`** = "dialog active on this
  creature" gate. SET by arm (`FUN_00463240:282`,
  `FUN_0048f9e0:108` when idx!=0). CLEARED by `FUN_005498f0` when
  armed with idx`<1` (the "close"/idx-0 arm) and by the
  0xFFF7FFFF clears at `0x48fb5d/0x48fd73/0x48fd86` (FUN_0048f9e0
  close branch) and `0x549901` (FUN_005498f0 itself).

Map to our handle: `cCreature*` from the object manager exactly as the
existing `creature_of(handle)` helper (`om=*0x00AD5C40; arr=*(om+4);
cre=*(arr+handle*4)`, dialog_runtime "Voice" C). Then
`npc_in_dialog(h) == ((*(u32*)(cre+0x14) & 0x80000) != 0)` and/or
`*(i32*)(cre+0x245) > 0`.

CAVEAT: this is "dialog is **armed/open** on NPC X", not the discrete
"just answered" edge. Because our FIX-A2 re-arms every tick it stays
HIGH for the whole conversation; a state-machine that wants "talked"
can latch on the **rising edge** of `cre+0x14&0x80000` (was 0 → now 1)
seen from a per-tick poll, which is crash-free (pure reads, SEH-
guardable). Clears when the box closes (idx-0 arm). HIGH: the two
fields + their sole writer are fully decompiled; the only MED is
whether a *vanilla* close always routes through the idx<1
`FUN_005498f0` path vs the `FUN_0048f9e0` direct 0xFFF7FFFF clear —
both clear the same bit so the poll is robust either way.

### 2b. HOOK option — trampoline `FUN_0048FF10` (HIGH)

- **Target VA: `0x0048FF10`.** Prologue bytes (capstone):
  `6A FF 68 F1 09 86 00` = `push -1 ; push 0x8609F1` — the standard
  SEH `6A FF 68` signature the existing `install_hook()` already
  validates; first 7 bytes are position-independent (no rel/RIP), so
  the existing `build_trampoline`(PROLOGUE_BYTES=7)+`patch_jump`
  machinery works unchanged.
- **Calling convention: `__thiscall`, callee-cleaned.** `ECX = qm`
  (=0x00AACF80). Stack at the `CALL` (before our thunk pushes):
  `[esp+0] = retaddr`, `[esp+4] = arg1 = &parsed record buffer`,
  `[esp+8] = arg2 = cCreature*` of the answered NPC. (Pushed
  `push ebp; push eax` → eax=arg1=buf, ebp=arg2=creature.)
- **Recover the talked-to NPC at hook entry:** the creature is
  **arg2 = `[esp+8]`** (before our prologue pushes). It is already a
  cCreature* (post-`__RTDynamicCast`); may be 0 (replay paths with
  param_4=0 — e.g. dispatcher site 0x470b08) — **must null-check**,
  matching `FUN_0048FF10:126`. Handle = `*(u32*)(creature + 0xc0)`?
  No — use the same identity the engine uses: `*(creature + 0xc)` is
  the content handle (NOT the object handle); the **object handle is
  `*(u32*)(creature + 0x10)`** is the *type id*. The portable handle
  for our API is the one we already bind by: reverse-map via the
  object manager, OR (cheaper) compare `creature` pointer to
  `creature_of(boundHandle)`. Quest-NPC array idx =
  `*(i16*)(creature + 0x94)`; active-quest id stamp =
  `*(i16*)(creature + 0x96)` (npc_model.md) — usable to name it.
- **Frequency: once per answer per distinct `*(creature+0xc)`** (the
  engine guard at `:142`). With the Q3 `+0xc`-rewrite fix it is
  exactly once per answer; without it, once per dialog (re)open.
  Hook should therefore do its own edge/de-dup (mirror the existing
  `on_trigger_once`).
- Cheapest BP to settle the residual MED (object-handle recovery):
  HW-exec **`0x0047847B`** (the `call 0x48ff10`) filtered to our
  bound NPC; one hit: dump `ebp` (=creature), then `*(i16)(ebp+0x94)`,
  `*(i16)(ebp+0x96)`, `*(u32)(ebp+0x10)`, `*(u32)(ebp+0xc)` and
  compare `ebp` to `creature_of(handle)` — confirms the pointer→handle
  map. (Alternative: BP entry `0x0048FF10`, same dump from `[esp+8]`.)

### 3. Minimal SDK recipe (modeled on existing patterns)

**Preferred — POLL (no patch, zero crash risk):**
```c
// reuse the existing creature_of(handle) helper (dialog_runtime Voice C)
static bool npc_in_dialog_impl(uint32_t h){
    __try {
        uint8_t* c = creature_of(h); if(!c) return false;
        return (*(uint32_t*)(c + 0x14) & 0x80000) != 0;   // dialog gate
        // (optionally also require *(int32_t*)(c+0x245) > 0)
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}
// Lua: sacred.npc_in_dialog(handle) -> bool   (register exactly like
// l_on_trigger in the lua_pushcfunction/lua_setfield block ~:1808).
// State machine advances on the FALSE->TRUE (or TRUE->FALSE on close)
// edge it sees across ticks — engine-faithful, SEH-guarded reads only.
```

**HOOK — `sacred.on_dialog_answer(handle_or_name, fn)`** (if a true
event edge is wanted): add `DIALOG_ANSWER_RVA = 0x0048FF10 -
0x00400000`; `g_tramp_dialog_answer`; an `install_hook(base+RVA,
&g_tramp_dialog_answer, hook_dialog_answer)` call in
`install_trampolines()` (it is a `6A FF 68` SEH prologue → the
existing 7-byte `install_hook` works as-is). Naked thunk modeled on
`hook_self_trigger_quest`, but pass **arg2** (`[esp+8]`) — the
creature — not ECX:
```asm
__declspec(naked) static void __cdecl hook_dialog_answer(){ __asm{
    pushfd
    push edx
    push ecx
    push dword ptr [esp+0x10]   ; orig [esp+8] = cCreature* (after
                                ;  pushfd/edx/ecx = +12, +4 ret? -> +0x10)
    call read_dialog_answer
    add  esp,4
    pop  ecx
    pop  edx
    popfd
    jmp  dword ptr [g_tramp_dialog_answer]
}}
extern "C" void __cdecl read_dialog_answer(uintptr_t creature){
    if(!creature) return;                       // mirror :126 null-check
    __try {
        // map creature* -> bound handle by comparing creature_of():
        // iterate our small SDK-bound handle set, fire matching name.
        // de-dup like on_trigger_once.
        fire_dialog_answer(creature);
    } __except(EXCEPTION_EXECUTE_HANDLER){ return; }
}
```
Stack-offset note: with `pushfd;push edx;push ecx` (12 B) on top of
the thunk's own `call` retaddr (4 B), the original `[esp+8]` is at
`[esp+8+12+? ]`; verify the exact displacement with the BP above
before shipping (this is the one number to confirm — same care the
`hook_funnel` comment took with `[esp+20]`).

### Confidence / residual

| claim | conf |
|---|---|
| tag-0x76 → 0x478470 → `call 0x0048FF10`, ECX=qm, arg1=&rec, arg2=ebp | **HIGH** (capstone) |
| ebp = `__RTDynamicCast(param_4)` = answered cCreature*, callee-saved to the call | **HIGH** (capstone, prologue + no-clobber) |
| `cCreature+0x14&0x80000` = dialog-active gate; sole-cleared via FUN_005498f0/FUN_0048f9e0 | **HIGH** (decompile + 0xFFF7FFFF site scan) |
| `cCreature+0x245` (i32) = active DlgNPC idx, set by FUN_005498f0 | **HIGH** (decompile) |
| FUN_0048FF10 runs once per (creature,+0xc) answer | **HIGH** (the `:142` guard) |
| 0x0048FF10 prologue is 7-byte hookable `6A FF 68` | **HIGH** (capstone) |
| creature* → SDK handle exact recovery offset | **MED** — close with BP `0x0047847B`/`0x0048FF10` (one hit) |
| thunk `[esp+8]` displacement after 12-byte save | **MED** — confirm with same BP / static recount before ship |

**Cheapest single BP (covers both MEDs):** HW-exec **`0x0047847B`**
filtered to the bound NPC; one answer → dump `ebp` + the offsets
above + stack layout. Until then, **ship the POLL option** (§2a/§3):
no patch, pure SEH-guarded reads, cannot crash the game.

---

## Talk-signal recipe — red-team verdict (2026-05-17)

Adversarial pass. All cites = capstone on `Sacred_decrypted.exe`
(base 0x400000) + Ghidra C of the named fns. I went looking for the
disasm that BREAKS the recipe.

### CLAIM 1 — Option (a) POLL (`+0x14&0x80000` / `+0x245` edge-latch)
### → **REFUTED.** Do NOT ship the poll.

The recipe asserts `+0x14&0x80000` and `+0x245` are "set by arm,
cleared on close … edge-latch across ticks = talked". The disasm of
every writer shows they are **bind-persistent**, never per-conversation:

- **Sole clear instr** for the gate bit: `0x005498fe
  and dword[ecx+0x14],0xfff7ffff` inside `FUN_005498f0`
  (`__thiscall`, ECX=cCreature, [esp+4]=dlgIdx, [esp+8]=fireFlag).
  Capstone-exact: `0x005498f0 mov eax,[esp+4]; mov [ecx+0x245],eax;
  jg 0x549905` — the AND clear is reached **only when dlgIdx <= 0**.
  i.e. the bit is cleared ONLY by an explicit "arm with index 0"
  (deliberate teardown), never by a normal answer/close.
- The Dialog handler `FUN_0048f9e0` (tag-0x1f, the record replayed
  on every open/answer): when the DlgNPC name matches (`uVar9>0`,
  the normal case) it runs `:90 FUN_005498f0(uVar9,1)` then
  `:95 *(param_2+0x14) |= 0x80000` — it **RE-SETS** the bit every
  replay. The `&0xfff7ffff` clears at `:92/:107/:173`
  (`0x48fb5c/0x48fd83`) are reached ONLY on `uVar9==0` / name-match
  FAIL. A normally-bound NPC never hits them.
- Bind site `FUN_00463240:282` (`0x00463697 or [eax+0x14],0x80000`)
  sets it once at arm and the fn never clears it.
- All 7 `and …,0xfff7ffff` sites + all 24 `FUN_005498f0` callers
  enumerated; none is a per-conversation "conversation closed"
  event on the bound creature with idx>0.
- Independent confirmation in our own tree: SDK `dialog_clear`
  (`player_state.cpp:1289-1297`) **deliberately refuses** to clear
  `+0x14&0x80000` (comment: clearing it makes the NPC lose its name)
  — exactly LIVE FACT (d). `+0x245` likewise: `FUN_005498f0` stores
  the full dword unconditionally and only the bit-clear is gated;
  the idx persists at its bound value.

⇒ `+0x14&0x80000` and `+0x245` are **PERSISTENT bind-state**. A poll
would read "in dialog" forever after the first arm → it can NEVER
produce a "talked" edge for our FIX-A2 NPCs (we re-arm every tick AND
the bits never fall). **Option (a) is dead. The recipe's "PREFERRED,
zero-crash" label is wrong** — it is crash-free but semantically
inert. There is **no** per-creature or cQuestMgr "a conversation
window is currently open on NPC X" field that toggles per
conversation; the engine tracks the *armed DlgNPC slot* (persistent),
not a live "talking now" state. Searched: no such set/clear pair
exists.

### CLAIM 2 — Option (b) HOOK ABI (arg2 = answered cCreature*)
### → **CONFIRMED** (capstone-exact), with one displacement correction.

- Jump table: `[0x4784C0 + 0x76*4] = 0x00478470` (verified by raw
  dword read). Block bytes verbatim:
  `8d84243c020000 / 55(push ebp) / 50(push eax) / 8bcb(mov ecx,ebx) /
  e8907a0100(call 0x48ff10)`. ECX=ebx=qm, arg1=&recbuf(esp+0x23c),
  **arg2=ebp**.
- ebp provenance: `0x0047569f mov edi,[esp+0xc58]` (param_4) →
  `0x004756b6 push edi; 0x004756bb call 0x84a961` (__RTDynamicCast
  cObject→cCreature) → `0x004756c0 mov ebp,eax`. So ebp = the
  dyn-cast cCreature* of dispatcher **param_4**.
- **No-clobber proof:** scanned 0x4756c0..0x475828 (the entire
  linear fall-through to the jump-table dispatch `0x00475821 jmp
  [eax*4+0x4784c0]`, which is the only way tag-0x76 reaches
  0x478470). The ONLY ebp-destination write in that whole range is
  the initial `0x004756c0 mov ebp,eax`. The many other `mov ebp,*`
  (0x476660, 0x476a11, 0x477xxx, 0x478029 …) are inside OTHER tag
  branches, never on the 0x76 path. ebp survives unclobbered to
  `0x00478477 push ebp`. **CONFIRMED.**
- Inside `FUN_0048FF10`: `0x0049007e mov eax,[esp+0x108];
  test eax,eax; je 0x4904e1; movsx ecx,word[eax+0x94]` — this IS the
  decompiler's `unaff_retaddr` (creature) → `[esp+0x108]` in the
  body == the entry-time `[esp+8]` == arg2. The null-check (`:126`)
  and `+0x94`/`+0xc` accesses all use arg2. **arg2 = answered NPC's
  cCreature\*: CONFIRMED.** (param_4 may be 0 on non-dialog replay
  paths → ebp=__RTDynamicCast(0)=0 → must null-check; engine itself
  does at 0x00490085. For our FIX-A2 answer path the GUI supplies
  the talked NPC as param_4, same path the live Rocheford-reward
  fact proves executes.)
- **7-byte patch safety:** prologue = `6A FF`(2) `68 F1 09 86 00`(5)
  = exactly 7 bytes, two whole instrs, immediate is absolute (no
  rel32/RIP) → position-independent. Patch boundary is clean
  (next instr `64 a1 …` at 0x0048ff17). The existing
  `install_hook`/`build_trampoline(7)` works unchanged. **CONFIRMED.**
- **Body displacement (recipe said arg2=[esp+0x108] "after prologue"
  — that's the BODY view, correct): for the trampoline THUNK the
  number that matters is arg2 at HOOK ENTRY = `[esp+8]` (before any
  prologue byte runs, since we patch at 0x0048FF10 itself).** The
  recipe's thunk does `pushfd;push edx;push ecx` (12B) on top of its
  own `call` retaddr (4B) before reading the saved arg2. Exact:
  original `[esp+8]` at thunk entry → after `call read_*`(+4),
  `pushfd`(+4), `push edx`(+4), `push ecx`(+4) = **+0x10**, so the
  thunk must `push dword ptr [esp+0x10+8] = [esp+0x18]`, NOT the
  `[esp+0x10]` the draft asm shows. **CORRECTION:** the draft thunk
  comment "(orig [esp+8] … -> +0x10)" undercounts by the thunk's own
  return address; ship `[esp+0x18]` (verify with the BP below before
  flashing — this is the single must-confirm number).

### CLAIM 3 — does FUN_0048FF10 run exactly once per answer?
### → **CONFIRMED it runs on EVERY answer** (valid signal); the
### reward block is MP-gated, irrelevant to the signal.

- `0x004901d8 mov cl,[0x182ebec]; test cl,cl` gates the entire
  reward/broadcast tail (`:190+`). `DAT_0182ebec` = the host/MP flag
  (same gate as quest_lifecycle Q2's `je 0x450DFD; ret 8` in SP).
  In single-player the reward/network tail is **skipped**.
- What runs unconditionally per call (SP included): the `:142`
  guard `cmp [iVar12+iVar8]==*(creature+0xc)` + the `:163-188`
  sub-record append. With the Q3 idempotent-`+0xc` fix the guard
  now matches after the first answer so the append is suppressed —
  **but `FUN_0048FF10` is still ENTERED on every answer** (entry is
  before the guard). The interception point is the function ENTRY,
  not the reward block, so the idempotent-+0xc change does NOT
  weaken it as a "talked" signal: tag-0x76 is replayed → 0x0048FF10
  is called → arg2 = the NPC, every answer. LIVE FACT (c) (reward
  every answer) is the *old* pre-fix behaviour; post-fix the
  *function still executes every answer*, it just no longer
  re-appends/re-rewards. **Signal validity: CONFIRMED, unaffected.**

### CORRECTED RECIPE — what is safe to ship

**Ship Option (b), the HOOK, NOT the poll.** The poll is REFUTED.

- Target `0x0048FF10`, 7-byte `6A FF 68`-SEH prologue, existing
  `install_hook` machinery, naked thunk modelled on
  `hook_self_trigger_quest`.
- Recover the NPC from **arg2**. At hook entry (we patch the very
  first byte) arg2 = `[esp+8]`. In the recipe's thunk
  (`pushfd;push edx;push ecx` + own retaddr) read it as
  **`[esp+0x18]`** (the draft's `[esp+0x10]` is off by 4 — the
  thunk's own return address). Null-check arg2 (engine does at
  0x00490085). Identify the NPC by pointer-compare to
  `creature_of(boundHandle)` (or `*(i16)(arg2+0x96)` quest id /
  `*(i16)(arg2+0x94)` array idx). De-dup in the hook (the engine's
  own once-guard is now MP-only + suppressed by idempotent +0xc, so
  the SDK must own the edge — mirror `on_trigger_once`).
- Do NOT use `*(creature+0x10)` (type id) or `+0xc` (content handle)
  as the object handle — neither is the object handle; reverse-map
  via the bound-handle set / object manager.

### UNRESOLVED → single cheapest BP

One number is still static-only: the thunk stack displacement after
the 12-byte save (`[esp+0x18]` by my recount vs the draft's
`[esp+0x10]`). Confirm with **HW-exec BP `0x0047847B`** (the
`call 0x48ff10`) filtered to the bound NPC: one answer → dump `ebp`
(= creature, compare to `creature_of(handle)`), `[esp+4]`/`[esp+8]`
at the call, and `*(i16)(ebp+0x94)`, `*(i16)(ebp+0x96)`. That single
hit closes both the pointer→handle map and the exact thunk offset.
Until that BP confirms, hard-code the null-check and validate arg2
points into the object-manager creature array before any deref
(extra SEH-guarded sanity, since the poll fallback is gone).
