# Native dialog text for a runtime NPC — concrete implementation spec (2026-06-13)

> Goal: a runtime-spawned NPC shows **OUR** native dialog text (`res:CAPMILES_GREET`
> from `custom/scripts/us/global.res`) in the vanilla dialog window, on talk.
> EXE base `0x00400000`, no ASLR (file off = VA − 0x400000). `qm` = cQuestMgr
> @ VA `0x00AACF80`. Corpus: `sdk/re/ghidra/decompiled/`; EXE: `sdk/Sacred_decrypted.exe`.
>
> **DISPATCH CHOSEN: replay a baked `tag=0x03` DialogShow record through the
> engine's own record walker `FUN_00475680` (the existing `funk_replay_one` in
> player_state.cpp), passing our NPC's `cCreature*` as p4.** The walker's field
> parser `FUN_00472bc0` self-resolves the record's `res:NAME` through the SAME
> `FUN_006726f0`→`FUN_0080e780`→`sacred_hash`→global.res path the SDK already bakes
> correctly — so NO creature-field write and NO entry+0x4c content-handle hack is
> needed. This is verified static, not a guess (see §1–§3).
>
> **HEADLINE RECIPE:** in `dialog_arm`, replace the `entry+0x4c`/`creature+0xc`
> content-handle writes with one call: bake `[03][size BE][00][01]"res:<KEY>"\0[09]"<DlgName>"\0[btn]\0`
> and `funk_replay_one(reb, rec, n, cre, /*p5=*/1)`.

---

## 0. What overturned the old approach (why the record path is right)

The current `dialog_arm` (player_state.cpp:1042) writes a **content handle** into
`DlgNPC entry+0x4c` and `cCreature+0xc`. That is the disproven path:
`FUN_00463240:106-107` proves `entry+0x4c` wants the **creature's own handle**, not
a text id, and `entry+0x4c` is **not** routed through the global.res resolver
(prior negative result). Hence "coherent-but-wrong vanilla text".

**The record path sidesteps the whole question.** When a `tag=0x03` record is
walked, its **field 1** (`res:NAME`) string is read by `FUN_00472bc0` case 1, which
for any name where `FUN_0084b2e5(first_char)!=0` (i.e. a `res:` resource ref) calls
`FUN_006725e0`+**`FUN_006726f0`** to resolve it (00472bc0:324-331). `FUN_006726f0`
high-bit-clear → `FUN_0080e780(id)` → `FUN_005f6290` stringifies id→registered
NAME → hash (MUL 0x71, MOD 0x3b9ac9f7 = our `sacred_hash`) → `FUN_0080eaf0(hash)`
= global.res lookup (00672 6f0 / 0080e780 read directly, confirmed). The resolved
id lands in `qm+0xa880`; the apply `FUN_00461540` then shows it. **No reliance on
`creature+0x10`** — and indeed the SDK reads `cCreature+0x10` everywhere as the
creature TYPE id (player_state.cpp:209,757,827,1241,1261), so the dialog_store.md
"text comes from creature+0x10" claim is the WEAK link; the record's own field-1
`res:NAME` is the strong, self-contained text source. Confidence HIGH.

---

## 1. The EXACT tag-0x03 DialogShow record bytes  (HIGH — re-derived from a live bin)

Re-derived by walking `bin/NetScript/FunkCode.bin` (78,733 records, clean
TLV from offset 0; 1,208 tag-0x03 records). Record framing confirmed:

```
[tag:u8=0x03] [size:u16 BIG-ENDIAN, total incl 3-byte header] [payload...]
```

Payload = `[lead flags u8=0x00] [field-opcode stream] [END u8=0x00]`. Each field =
`[fid:u8] [value]`. Field encodings used by tag-0x03 (from `FUN_00472bc0`):
- **fid 0x01** = ASCIIZ — the dialog **text** `res:NAME` (case 1 → self-resolve via
  `FUN_006726f0`, result → `qm+0xa880`, name → `qm+0xa460`).
- **fid 0x09** = ASCIIZ — the **DlgNPC name** to bind/show on (handler
  `FUN_0048bb40` case 9 scans `qm+0x755c` stride 0x50 by name; logs German
  `"Dialog (%s) nicht..."` on miss). Optional.
- **trailing single byte** `0x39`/`0x37` ('9'/'7') = button/voice code (vanilla),
  OR **fid 0x0a** = u16 (3-byte) variant. Optional; smallest real records omit it.

### CONCRETE VANILLA EXAMPLES (cite these)

| VA off | size | raw bytes (hex) | decoded |
|---|---|---|---|
| `@821`  | 35 | `03 00 23 00 01 "res:17096"\0 09 "NPC_Dialog_Novizin"\0` | text + DlgNPC name |
| `@8181` | 34 | `03 00 22 00 01 "res:19508"\0 09 "dlg_UW14_AUFTRAG"\0 39` | text + name + btn '9' |
| `@8215` | 16 | `03 00 10 00 01 "res:19509"\0 39` | **minimal**: text + btn '9' |
| `@8472` | 17 | `03 00 11 00 01 "res:19511"\0 37 00` | text + btn '7' |
| `@8680` | 18 | `03 00 12 00 01 "res:19508"\0 0a 00 00` | text + fid-0x0a u16=0 |

Minimal (`@8215`) raw = `03 00 10 00 01 72 65 73 3a 31 39 35 30 39 00 39`.

### OUR record (emit this) — `res:CAPMILES_GREET` on DlgNPC "Captain Miles"

```
03                          ; tag
00 23                       ; size u16 BE = 35  (= 3 + 1 + 1 + 17 + 1 + 1 + 11 + 1 ... compute at bake)
00                          ; lead flags
01 "res:CAPMILES_GREET" 00  ; fid 1: text key  (17 chars + NUL)
09 "Captain Miles" 00       ; fid 9: DlgNPC bind name  (the dlgnpc_bind name)
39                          ; trailing button byte '9'  (vanilla follow-up button)
00                          ; END
```
Size is computed at bake time (`o[1]=len>>8; o[2]=len&0xff`). The existing
`dlg_put_rec` (player_state.cpp:905) ALREADY emits exactly this framing
(`[3]=0x00 lead, 0x01+s1+NUL, optional 0x01+s2+NUL, 0x00 END`) — but it uses fid
`0x01` for the second string; for a tag-0x03 NPC-bind we want fid **0x09** for the
DlgNPC name, OR omit the second field entirely and rely on p4=creature (see §2).

> NOTE: vanilla uses numeric `res:17699`; we use `res:CAPMILES_GREET`. Both
> resolve through the identical global.res `sacred_hash` mechanism — the resolver
> stringifies the interned id to its NAME and hashes the NAME, and a name that is
> already a literal resolves the same way. Our bake of `CAPMILES_GREET` at
> `sacred_hash("CAPMILES_GREET")` in `custom/scripts/us/global.res` is verified
> (`check_dlgtext.py`).

---

## 2. What must be set on the runtime NPC  (HIGH for record path)

**For the record path: nothing on the creature is required for the TEXT.** The text
is the record's field-1 `res:NAME`, resolved independently of any creature field.

What DOES matter for the dialog window to bind to OUR NPC:

1. **DlgNPC entry exists & is name-matchable.** `FUN_0048bb40` case 9 finds the
   slot by the field-9 name in `qm+0x755c` (stride 0x50, name@+0x04). The SDK's
   `dlgnpc_bind` already creates/sets this entry (player_state.cpp:592 `set_npc_name`
   writes entry+0x04 in both DlgNPC `qm+0x755c` and NameArrA `qm+0x358`). So pass
   the SAME name we bound with as the record's fid-9 string. **VERIFY our spawn
   actually populated a DlgNPC slot with that name** (it does via `dlgnpc_bind`).

2. **`entry+0x4c` = the creature's OWN handle (`creature+0xc`), NOT a content id.**
   This is what `FUN_00463240` sets and what the open window binds to
   (00463240:106-107). The existing `dialog_arm` route via `FUN_00463240`
   (player_state.cpp:1064) already does this correctly — KEEP that call; it arms the
   talk-route / gate (+0x14|0x80000) and SelfTriggerQuest. **REMOVE only the manual
   `entry+0x4c = content` and `creature+0xc = content` writes** (1100-1132, 1140-1143)
   — those overwrite the correct creature-handle binding with a bogus content id and
   are the source of the wrong text.

3. **`creature+0x10` — do NOT write it.** It is the creature TYPE id; the record
   path does not read it for text. (The dialog_store.md "write res id to +0x10"
   idea is unconfirmed/MED and unnecessary — skip it.)

So the per-NPC state is: DlgNPC slot named, `FUN_00463240` talk-route armed,
`+0x14|0x80000` gate on. All already produced by the existing route block; we just
stop clobbering `entry+0x4c`/`creature+0xc` and instead replay the text record.

---

## 3. The dispatch call  (HIGH — ABI already pinned & implemented)

**CHOSEN: `FUN_00475680` record walker** (robust; identical to vanilla/save/MP
replay; the parser self-resolves `res:` and the apply runs end-to-end). The SDK
ALREADY has this as `funk_replay_one` (player_state.cpp:921) with the pinned ABI:

```
FUN_00475680 : __thiscall, ECX = qm (= reb + 0x00AACF80)
  p1 = const void* buf      ; record bytes, cursor self-walks; < 0x800 (walker cap, 00475680:158)
  p2 = int* cursor          ; init 0 → record at buf+0; walker does *cursor += BE size
  p3 = -1                   ; (only matters for tag 6/0x10; irrelevant to 0x03)
  p4 = (int)cCreature*      ; OUR NPC. Walker RTDynamicCasts p4→cCreature* (local_c50/c54
                            ;   = cCreature/cObject RTTI, 00475680:134-135); 0 → context NULL.
  p5 = char                 ; pre-pass gate: param_5!=0 runs tag-6/0x10 pre-resolve
                            ;   (00475680:174-185). Pass 1 for an interactive bind.
  p6 = 0
  ret 0x18  (6 dwords popped)
```

Call (exactly the existing helper):
```cpp
funk_replay_one(reb, rec, n, /*p4=*/ (void*)cre, /*p5=*/ 1);
//   → ((fn_funk_dispatch)(reb+0x00475680))(qm, buf, &cursor=0, -1, (int)cre, 1, 0)
```
Walker flow for our record (verified): reads `tag=*(u16)` (low byte 3) and
`size=*(u16 BE)@+2`, copies `size` bytes into scratch `local_a0c`, advances cursor,
`switch(tag)` **case 3 → `FUN_0048bb40`** (00475680:210-214). `FUN_0048bb40` loops
`FUN_00472bc0()` over the payload — **case 1 self-resolves `res:CAPMILES_GREET`**
into `qm+0xa880` (00472bc0:324-331), **case 9** locates our DlgNPC slot by name —
then calls **`FUN_00461540(unaff_retaddr)`** (the apply/show, 0048bb40:173). Apply
writes the 0xbc dialog-history record and the window renders our global.res string.

**Why NOT call `FUN_0048bb40`/`FUN_00461540` directly:** `FUN_0048bb40` reads ALL
its inputs from the `qm+0xa460/0xa860/0xa880` scratch that ONLY `FUN_00472bc0`
(driven by the walker) populates — calling it without the walker leaves the scratch
stale and it would show garbage or nothing. `FUN_00461540` has a 14-arg signature
(00461540:5-8) with no clean DLL-buildable arg layout. The walker is the only entry
that wires the scratch correctly. So the walker route is both the simplest correct
call AND the most robust.

### THE ONE READ-ONLY BREAKPOINT (confirms name-vs-hash feed)

BP **`FUN_0080e780` @ `0x0080E780` entry**. On replaying OUR record, log `param_1`
(the incoming id) and the string `FUN_005f6290`/`FUN_0080fab0` build (the registered
NAME being hashed). PASS criteria: the logged NAME == `CAPMILES_GREET` (or the id
stringifies to it) → our text will be fetched from global.res. If instead it shows
a numeric/vanilla name, the field-1 resolve took the wrong branch and we fall back
to writing `sacred_hash("CAPMILES_GREET")|0x80000000` directly (§5 fallback).
Optional second BP: `FUN_00472bc0` @ `0x00472BC0` to watch case-1 take the
`FUN_006726f0` branch on our `res:` string. Both are read-only / non-mutating.

---

## 4. Minimal SDK change list  (ordered recipe)

Files: `sdk/player_state.cpp` (dialog_arm), `custom/lua/lib/npcobj.lua` (Npc:say —
no change needed), bake of `global.res` (already done).

1. **(already done)** `custom/scripts/us/global.res` holds `CAPMILES_GREET` /
   `ROCHEFORD_GREET` at the correct `sacred_hash` idents (verify `check_dlgtext.py`).

2. **`dlg_put_rec` — support fid-9 for the DlgNPC name** (player_state.cpp:905).
   Today it emits the 2nd string with opcode `0x01`. Add a variant (or a param) that
   emits the bind name with opcode **`0x09`** and an optional trailing button byte:
   ```
   o[p++]=0x09; copy(dlgName); o[p++]=0; o[p++]=0x39; /* button '9' */ o[p++]=0; /*END*/
   ```
   Keep the existing `0x00` lead and BE size write. (If field-9 binding proves
   unnecessary because p4=creature already selects the NPC in `FUN_00461540`, emit
   the minimal `@8215`-style record: lead `00`, `01`+`res:KEY`+NUL, `39`, END.)

3. **`dialog_arm` — replace the content-handle writes with a record replay**
   (player_state.cpp:1075-1132 and 1140-1143):
   - KEEP: the `FUN_00463240` talk-route block (1057-1074) — it sets
     `entry+0x4c = creature+0xc` correctly, the gate, SelfTriggerQuest, and
     registers the pump refKey. KEEP the `+0x14|0x80000` gate write (1144-1146).
   - DELETE: the `FUN_00465070` content-resolve (1084-1099), the `entry+0x4c =
     content` write at the by-objIdx entry (1100-1109), the by-handle `entry+0x4c =
     content` loop (1110-1132), and the `creature+0xc = content` write (1140-1143).
   - INSERT (after the route block): build + replay the text record:
     ```cpp
     uint8_t rec[0x208];
     size_t n = dlg_put_rec9(rec, 0x03, /*res*/text_key_with_res_prefix,
                             /*dlgName*/dlg_name, /*btn*/0x39);
     funk_replay_one(reb, rec, n, cre, /*p5=*/1);
     ```
     `text_key_with_res_prefix` = `"res:" + text_key` (the record field-1 MUST carry
     the `res:` prefix so `FUN_0084b2e5` selects the resource branch; this differs
     from `dlgnpc_bind` which passes the BARE name — keep them distinct).
   - Keep `dlg_probe` pre/post for diagnostics.

4. **`Npc:say`** (npcobj.lua:327) — no change; it already calls
   `sacred.dialog_arm(self._h, self._name, text_key, voice)`. `self._name` is the
   DlgNPC bind name (fid-9), `text_key` is the global.res key.

5. **Build/deploy/run** per `session-state-2026-06-13.md`; talk to the captain;
   confirm the native window shows OUR line. If wrong text → set the §3 BP on
   `FUN_0080e780` and apply the §5 fallback.

---

## 5. Fallback (only if the §3 BP shows the resolve missing our name)

If `FUN_0080e780` is NOT fed `CAPMILES_GREET` from the record, write the resolved
hash directly so the high-bit branch hits global.res: after replay, set the matched
DlgNPC `entry+0x4c`'s text source — but note that requires the entry+0x4c to be a
text id, which it is NOT (§0). The true fallback is the **direct high-bit feed into
the field-1 value**: emit the record with field-1 = the NUMERIC literal
`res:<sacred_hash("CAPMILES_GREET")>` OR `res:<id>|0x80000000` form so
`FUN_006726f0` takes `FUN_0080eaf0(hash)` directly. This keeps the record path; only
the field-1 string changes from `res:CAPMILES_GREET` to a numeric `res:` that the
resolver maps to our global.res entry. Confidence MED — settle with the same BP.

---

## 6. Confidence table + live-BP flags

| Claim | Conf | Evidence |
|---|---|---|
| tag-0x03 framing `[03][size u16 BE][00 lead][fields][00 END]` | HIGH | 1,208 live records in `bin/NetScript/FunkCode.bin`; examples @821/@8215/@8680 |
| field 1 = `res:` text (ASCIIZ); field 9 = DlgNPC name; trailing 0x39/0x37 = button | HIGH | `FUN_00472bc0` cases 1/9; `FUN_0048bb40` case 9; bin decode |
| field-1 `res:` self-resolves via `FUN_006726f0`→`FUN_0080e780`→sacred_hash→global.res | HIGH | 00472bc0:324-331; 006726f0; 0080e780 disasm |
| walker ABI `__thiscall ECX=qm, 6 args, ret 0x18`, case 3→`FUN_0048bb40` | HIGH | 00475680:5,134-135,156-172,210-214; `funk_replay_one` already implemented |
| `FUN_0048bb40` reads scratch `qm+0xa460/880`, ends in `FUN_00461540` apply | HIGH | 0048bb40:117,173,310-343 |
| `entry+0x4c` = creature handle, not text id | HIGH | 00463240:106-107 (existing knowledge) |
| `creature+0x10` = TYPE id, not text source (record path independent of it) | HIGH | player_state.cpp:209/757/827/1241; record path uses field-1 |
| direct call to `FUN_0048bb40`/`FUN_00461540` is NOT viable (stale scratch / 14-arg) | HIGH | scratch only filled by walker-driven `FUN_00472bc0`; 00461540:5-8 |
| name-vs-hash feed at render time | MED | needs 1 read-only BP `FUN_0080e780` @0x80E780 |
| fallback numeric/high-bit `res:` field-1 | MED | same BP settles |

### Key VAs
- walker `FUN_00475680` `0x475680` (`funk_replay_one` p4=creature, p5=1)
- field parser `FUN_00472bc0` `0x472BC0` (case 1 res-resolve @324-331)
- tag-0x03 handler `FUN_0048bb40` `0x48BB40` → apply `FUN_00461540` `0x461540`
- resolver `FUN_006726f0` `0x6726F0` → `FUN_0080e780` `0x80E780` / `FUN_0080eaf0` `0x80EAF0`
- res-prefix test `FUN_0084b2e5` `0x84B2E5`; talk-route `FUN_00463240` `0x463240`
- `qm` @ `0x00AACF80`; DlgNPC table `qm+0x755c` stride 0x50, name@+0x04, bind@+0x4c
- SDK: `dialog_arm` player_state.cpp:1042; `funk_replay_one`:921; `dlg_put_rec`:905
