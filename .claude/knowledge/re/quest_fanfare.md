# Quest "solved" fanfare (banner + chime) — 2026-05-16

## TL;DR (decisive)

Vanilla "quest solved" is **FUN_0048e600 @ 0x0048E600** (tag-0x4d
handler, `__thiscall`, ECX=cQuestMgr=0x00AACF80). It does, atomically:
(1) marks the **0x124-stride questbook list** entry done & emits a
**0x1ba** refresh event; (2) pops the **0x174 registry** entry
(`*(ctx+0x428) -= 0x174`); (3) emits a **0x1bd** questbook event on
the kernel/network bus. The on-screen golden banner + completion chime
are produced by the engine's **registered questbook-UI listener** that
consumes the 0x1bd (and sibling) event — exactly the same architecture
as the gold "+N" event whose cosmetic consumer the SDK already relies
on. There is **no separate playSound(id)**; the banner *and* the chime
are bundled into the listener's reaction to the bus event. Journal
greying/removal is **coupled** (same function, before the emit).
**Confidence: HIGH** for the mechanism; MEDIUM for hand-marshalling
the blob (prefer calling FUN_0048e600).

## The event family (vtable PTR_FUN_00890874 @ 0x00890874)

Blob built either inline or by ctor **FUN_00423ac0** (`__thiscall`,
ECX=event obj, params→obj[1..0xe]; obj is 0x6c bytes with embedded
std::string members at obj[0xf]/[0x12]/[0x15], trailer
obj[0x18]=0x10, obj[0x19]=1, obj[0x1a]=0). Layout:
- `+0x00` vtable = **0x00890874** (NOTE: different from the gold
  event's 0x00890B38 — this is the questbook event class)
- `+0x04` event-type id (the discriminator)
- `+0x08..` payload (varies by id)
- tail `+0x60`=0x10, `+0x64`=1, `+0x68`=0

Verified ids (all questbook): **0x1ba** entry remove/refresh
(FUN_0048e600 1st block, FUN_0046cbe0); **0x1bd** entry add/update —
carries a full 0x174 entry marshalled field-by-field (+0x00,+0x04,
+0x08, +0x10[0x14], +0x24, +0x28[0x28], +0x50[0x118], +0x168[0xc];
FUN_0046cbe0 L472-484, FUN_0048e600 2nd block); **0x1be**
(FUN_00471000); **0x1bf** the 7 quest tag handlers' "quest add/state"
(FUN_004790c0/78780/78d80/793d0/7a0c0/7b480); 0x1c0/0x1c6/0x1c9
related. **The "solved" fanfare is the 0x1bd emitted after the vector
pop in FUN_0048e600** (L305-343).

Dispatch (same for every id, identical to gold-event pattern):
`obj = FUN_007d84a0(&blob)` (singleton getter @0x007D84A0, returns
DAT_0182ebe8) then `NetworkManager_receive_event_007d8950()`
(`__thiscall`-ish, enqueues onto the bus). The UI listener fires the
banner+sound off the bus. [HIGH]

## Gating prerequisites (must hold or NOTHING shows)

In FUN_0048e600 the 0x1bd emit is guarded by:
`if (DAT_0182ebec != 0 && *(char*)(ctx+0x14) == 0)`.
- `DAT_0182ebec` = "real game session, not editor" (= already 1 in
  normal play; HIGH — same gate in all 7 handlers).
- `*(char*)(cQuestMgr+0x14) == 0` = "not network-replica side".
If `DAT_0182ebec==0` the fanfare never fires. Post-world-load both
hold in single-player. [HIGH]

## Sound

No standalone sound call in the producer path. The chime is emitted by
the questbook-UI bus listener as part of rendering the 0x1bd
notification (mirror of the gold event: one bus event → coin sound +
float text + HUD invalidate, all in the registered consumer). The
banner text is a **resource-dict string id** (like the journal's
active-quest header id 0x499), NOT ASCII in the EXE — confirmed: no
"QuestSolved"/"solved"/"Auftrag"/"geloest" literals exist; FindStrings
returned only `*.bin` filenames. So we cannot/needn't supply a sound
id; firing the event is sufficient. [HIGH no separate call; MEDIUM on
exact resource id — not needed for the recipe.]

## Journal coupling

**Coupled.** FUN_0048e600, in one call: greys/refreshes the 0x124
list entry (0x1ba), pops the 0x174 registry entry
(`memcpy last-over-target; *(ctx+0x428)-=0x174`), THEN emits 0x1bd.
So calling FUN_0048e600 also does the §3(b) "vanish from journal"
removal — do **not** also run the manual pop from quest_lifecycle.md
§3(b) (double-pop / corrupt vector). If you want the "greyed but still
listed" look instead, that is the *independent* `entry+0x00=3` write
(quest_lifecycle §3a) and does NOT itself fire any fanfare. [HIGH]

## Minimal SDK recipe for quest_id 9512 (banner + chime)

Preferred — let the engine do it (avoids the fragile 0x6c blob):

1. Ensure 9512 is a real registry entry that passes the journal gate
   (+0x24 != 0, +0x16C page) — already the existing work.
2. On the main thread, post-world-load, with cQuestMgr known:
   build the tag-0x4d FunkCode stream FUN_0048e600 expects (its
   `FUN_00472bc0(&blob,param_1)` reader; param at `ctx+0xa860`
   carries the quest_id to match) and call:
   `FUN_0048e600(stream_ptr)` with `ECX = 0x00AACF80`,
   `__thiscall`, takes 1 stack arg (the FunkCode record ptr; the
   quest_id is read from the parsed stream into `cQuestMgr+0xa860`,
   matched against `entry+0x08`). This pops 9512 and fires the
   real 0x1bd fanfare + 0x1ba journal refresh — identical to vanilla.
3. Pay reward from existing `runtime_triggers ctx:give_gold`/give-item
   on the same trigger (no engine reward path — see §4 quest_lifecycle).

Fallback if synthesising the tag-0x4d stream is impractical: replicate
FUN_0048e600 L305-343 directly — do the vector pop manually
(quest_lifecycle §3b memcpy+pop), then construct the 0x1bd blob via
`FUN_00423ac0(0x1bd, slotIdx, savedStr, sub, 0,0,0,0,0,0,0,0,0,0)`
(ECX = a 0x6c-byte zeroed buffer), then
`o=FUN_007d84a0(&buf); NetworkManager_receive_event_007d8950()` with
ECX=o, exactly like emit_gold_change_event. The param shape: in
FUN_0048e600 it is `FUN_00423ac0(0x1bd, uVar10 /*=entry slot index*/,
uStack_1b0 /*string handle from FUN_0065b170*/, FUN_0065b170()/*0*/,
0,0,0,0,0,0,0,0,0,0)` (L327). [recipe MEDIUM-HIGH; exact
FUN_00423ac0 string-member arg form NEEDS A RUNTIME BP.]

## Needs runtime breakpoint (flagged, exact)

- **BP 0x0048E600** (entry) on a vanilla quest you solve normally:
  dump ECX (confirm = 0x00AACF80), `[ECX+0x14]`, `DAT_0182ebec`,
  and `[esp+4]` (the stream). Then BP **0x0048E81B** (the
  `FUN_00423ac0(0x1bd,...)` callsite, ~L327) and dump the 14 args
  pushed + the 0x6c-byte ECX buffer after the ctor returns + the
  resulting blob `+0x00..+0x6c`. That single capture nails the exact
  0x1bd payload for the hand-marshal fallback.
- If calling FUN_0048e600 directly: BP its entry once with our
  synthetic stream to confirm `FUN_00472bc0` parses it (returns the
  tag code into the local that gates the `==0xb`/solved path) and
  that `iVar3` hits the L252+ removal branch (`*(short*)(param+2)==4`
  — i.e. stream record type 4). Verify the matched `uVar11`/`uVar10`
  resolve to slot of 9512 before the pop.

## Files
- decompiled/0048e600_FUN_0048e600.c — the solved path (pop + 0x1ba + 0x1bd)
- decompiled/0046cbe0_FUN_0046cbe0.c — 0x1bd entry-marshal reference (L472-513)
- decompiled/00423ac0_FUN_00423ac0.c — event-object ctor (0x6c bytes, vtable 0x00890874)
- decompiled/007d84a0 + 007d8950_NetworkManager_receive_event — bus dispatch
- decompiled/004790c0 / 00471000 — sibling 0x1bf / 0x1be emit templates
- runtime_triggers.cpp emit_gold_change_event — working bus-emit pattern to copy
