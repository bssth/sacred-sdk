# Sacred Gold — Combat / HP / AI init for runtime-spawned NPCs — RE report

Target: Steam 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000, no ASLR
(file off == VA − 0x400000). Evidence: Ghidra decompiles in
`sdk/re/ghidra/decompiled/` (`00482510` CreateNPC, `0052e420`, `0043adc0`,
`0044ddc0`, `0059f580` WakeUp, `00548f60`, `00423580`, `00549000/080/920`,
`005fb530/005fba40/005fccb0/005fb360`, `004a9ef0`), plus a static dump of the
hostility matrix at VA 0x00890A30.

> **Scope correction (important).** `npc_model.md` states "level set by
> `FUN_0044ddc0((float)level)` (op 0x03)". **That is wrong.** `FUN_0044ddc0`
> (read in full) computes `cos/sin(param*π/180)` and writes a direction
> vector to `cCreature+0x70/0x74/0x78`, then `FUN_0044dbf0(c+0x70,1)`. It is
> the creature **facing/orientation** setter, NOT level. Op `0x03` therefore
> sets spawn orientation, not rank. This changes the HP story below. HIGH.

---

## 0. TL;DR — why our bare spawn is combat-broken

`cObjectManager::create_005fba40` → `005fb530` → allocate + class ctor +
`005fee50` (sector-list registration) + `005fccb0` (map/sector placement).
That path **does** build the creature object and load its base type def, but
it **never runs the CreateNPC post-create block** (`00482510:1013-1380`),
which does three things our spawn skips:

1. **`+0x1F0` AI/faction class is left at the bare class-default**
   (`FUN_0043adc0(type)` value, == `FUN_0052e420` mode 0). For many spawn
   types this default is a passive/retaliate class. CreateNPC instead calls
   `FUN_0052e420(mode=1, val=uVar29)` with `uVar29 ∈ {2,3,7,0xe}` — an
   *active-combatant* class. `+0x1F0` is **both** the AI behaviour class
   **and** the faction id used by the hostility matrix (§3/§4). HIGH.
2. **`+0x1F4` (faction/flags word) is never committed** — CreateNPC does the
   single store `pTVar12[0x3e].spare = unaff_EBX;` at `00482510:1354`. Our
   spawn leaves it at the ctor default (0), so the awake bit / team bits /
   `0x40000` peaceful bit etc. are unset → no proactive engage. HIGH.
3. **`+0x200` AI-controller word is never armed and WakeUp is never called.**
   CreateNPC: if `EBX&1` → `cCreature_WakeUp_0059f580()` (`:1357`) **and**
   `pTVar12[0x40].spare = 0x40200000` (`:1370`, i.e. `*(u32*)(c+0x200)`).
   Without this the creature is asleep/idle and only the damage handler
   (retaliation) can flip it active. HIGH.

The "dies in one hit" symptom is a *consequence of #1*: the class-default
`+0x1F0` plus an unset `+0x1F4`/level means the creature is treated as a
non-scaled stub. Base HP IS loaded by the bare path (the class ctor reads the
per-type+difficulty def — see §1), but **HP is recomputed/scaled from the
level + difficulty by the combat module**, and that recompute is what the
post-create block triggers indirectly via the stance/active path. Poking
`+0x4d8` directly works only until the next recompute/regen tick clamps it.

---

## 1. HP / maxHP model

### Confirmed fields (cCreature)

| Offset | Type | Meaning | Evidence | Conf |
|---|---|---|---|---|
| `+0x4d8` | i32 | **current HP** | alive-gate `*(c+0x4d8)!=0` in `00549000:25`, `00549080:9`, `00549920:23`, `00548f60:11`, `00537160:16`; zeroed on death `00465690:2778`, `00564d60:100`, `00615550:181`, `00619bf0:353` | HIGH |
| `+0x4d4` | i32 | **max HP** (paired; `+0x4d0..+0x4dc` are one stat block, written together — code refs cluster) | 94 code refs incl. paired with `+0x4d8`; `+0x4d0/+0x4d4/+0x4d8/+0x4dc` form the vitality block | MED-HIGH |
| `+0xfc` | i16 | death/special state (`==9` ⇒ dead/corpse) | set to 9 on kill `00465690:2779`; gate everywhere | HIGH |
| `+0xfe` | i16 | awake/active sub-state (set 1 by WakeUp) | `0059f580:32` | HIGH |
| `+0x150` | i16 | disable state (`==6` ⇒ removed/disabled) | set 6 on kill `00465690:2781`; gate everywhere | HIGH |
| `+0x24` | u8 | **level/rank** (ground-truth, npc_model) | GT | HIGH |
| `+0x200` | u32 | AI-controller / awake+aggro flag word | `0059f580:27` (`&0x40000`=no-wake gate), set `0x40200000` by CreateNPC `:1370`; `00549920:21` bit6; `00423580:88/109` bit `0x10000000`=grudge | HIGH |
| `+0x1F4` (=500) | u32 | faction/side + behaviour flags (`[0x3e].spare`) | `00482510:1354`; `00423580:151/155` bit `0x40000`; `00549920:26` mask `0x1006dcf8` | HIGH |
| `+0x1F0` (=496) | i32 | **AI behaviour class AND faction id** (`[0x3e].pVFTable`) | set by `FUN_0052e420`; indexes hostility matrix `00423580:54-57`; combat gate `00549920:24` (`∈{3,7,8,0xc,0xe,0xf}`) | HIGH |
| `+0x39c..+0x3a0` | ptr,ptr | per-creature **threat/grudge list** (object handles, stride 9) | `00423580:90-102, 111-123` | MED |
| `+0x251` | i32 | grudge counter (`<1` ⇒ matrix decides) | `00423580:104/125` | MED |
| `+0x70/+0x74/+0x78` | f32×3 | facing/orientation vector (NOT level) | `FUN_0044ddc0` | HIGH |

### Where base HP comes from (the 128-byte def)

`FUN_0043adc0(type)` (read in full) indexes a **128-byte-per-type table**:
`*(u16*)(type*0x80 + 0x1a + mgr)`, then a second table stride **0x56**
(86 bytes): `*(u8*)(*(mgr+0x563008) + 4 + row*0x56)`. That returned byte is
the creature's **class-default `+0x1F0`** (it is what `FUN_0052e420` mode 0
stores). So the 128-byte struct at `type*0x80 + mgr` is the per-type
creature def; `mgr` here is the cStatsManager-style global addressed via
`in_ECX` (the data table base). The base HP/damage for a type are loaded by
the class ctor (vtable `[0x18]` serializer invoked at `005fb530:119`) from
the def resolved by `FUN_0043fc40(type, DAT_0182ee44 /*difficulty*/)` +
`FUN_00425ea0` (`005fb530:83-89`). **This base load DOES happen on our bare
path** — so the type's nominal HP is present; what is missing is the
**level/difficulty scaling pass**.

### Proper HP recompute (instead of poking +0x4d8)

There is no standalone "set level N → recompute HP" exported leaf that
CreateNPC calls (FUN_0044ddc0 is orientation, debunked above). HP scaling is
performed by the combat/stats module **`FUN_0052ab70`** (7815 bytes; the
function body that owns the `+0x4d0/4d4/4d8/4dc` cluster — code refs at
0x52b5f8 etc.). The reliable, side-effect-correct way to get full scaled HP
for a runtime spawn is therefore **not** to call an HP setter directly but to
reproduce CreateNPC's sequence so the engine's own scaling runs:

1. Set level first: `*(u8*)(c+0x24) = level;` (and the rank mirror
   `creature[0x4b].spare+3` if you also emit op 0x73 — optional). MED.
2. Run the **stance/active assignment** (§2 step C) — this sets `+0x1F0` to a
   combatant class, which is the precondition the combat module checks
   (`00549920:24`) before it will treat the creature as a scaled fighter.
3. Call `cCreature_WakeUp_0059f580` (ECX=c) and arm `+0x200`
   (`*(u32*)(c+0x200)=0x40200000`). WakeUp copies `+0x204→+0x208` and flips
   `+0xfe=1`; combined with a combatant `+0x1F0` the next combat tick runs
   the scale pass in `FUN_0052ab70` and fills `+0x4d4`(max)/`+0x4d8`(cur).
4. **If you must set HP explicitly** (e.g. to verify), write **both**:
   `*(i32*)(c+0x4d4)=maxHP; *(i32*)(c+0x4d8)=maxHP;` — writing only `+0x4d8`
   gets clamped to `+0x4d4` (the stub max) on the next tick, which is exactly
   the "tiny HP / dies in one hit" you observed. Confidence MED-HIGH;
   in-game probe below resolves the remaining ambiguity on `+0x4d4`.

**Cheap in-game probe (resolve maxHP offset + clamp):** with the SDK field
logger, spawn one NPC the bare way, dump `+0x4d0,+0x4d4,+0x4d8,+0x4dc,+0x24`;
then spawn the *same type* via FunkCode CreateNPC and dump the same. The
field that differs and equals the post-hit cap is maxHP. Then set
`+0x4d8 := +0x4d4` on the bare one, take a hit, re-dump: if it survives, the
clamp source is confirmed `+0x4d4`.

---

## 2. The combat-stat init CreateNPC does and we skip — minimal recipe

CreateNPC post-create block diff (`00482510`), in execution order, applied to
a freshly `cObjectManager::create_005fba40`'d creature `c` (resolve:
`om=*(0x00AD5C40); arr=*(om+4); c=*(arr+handle*4)`):

| # | What CreateNPC does | Runtime call / write | Conf |
|---|---|---|---|
| A | resolve `c` from handle | `cObjectManager_getData_005fe000(handle)` → `FUN_0084a961(...,&cCreature::RTTI,...)` (`:962-964`). You already have `c`. | HIGH |
| B | set level/rank | `*(u8*)(c+0x24) = level` (GT). Optional rank mirror `*(i16*)(creature[0x4b].spare+3)=rank`. | HIGH/MED |
| C | **assign active AI/faction class** — the core fix | `ECX=c; FUN_0052e420(1, uVar29)` → sets `*(i32*)(c+0x1F0)=uVar29`. Pick `uVar29` per §3. (CreateNPC computes it at `:1110-1163`.) | HIGH |
| D | commit faction/flags word | `*(u32*)(c+0x1F4) = EBX` (the side/flags accumulator; §3/§4 for value). CreateNPC: `00482510:1354`. | HIGH |
| E | wake up | `ECX=c; cCreature_WakeUp_0059f580()` — requires `(*(u32*)(c+0x200) & 0x40000)==0` and `*(i16*)(c+0xfc)==0`. | HIGH |
| F | arm AI controller | `*(u32*)(c+0x200) = 0x40200000` (CreateNPC `:1370`, done when `EBX&1`). | HIGH |
| G | (optional) group/team | `ECX=om; FUN_004a9ef0(group_id, handle, 1)` then `*(handle*)(creature[0x49].pVFTable+1)=group` (`:1377-1378`). Needed only for squads. | MED |
| H | (optional) skill/aux | `FUN_00450e20(...)` (op 0x75) — not required for basic combat. | LOW |

The **minimum** to turn a bare spawn into a real combatant is **C + D + E +
F** (plus B for proper HP scaling). A/G/H are situational. After C the combat
module's gate (`00549920:24`, `+0x1F0 ∈ {3,7,8,0xc,0xe,0xf}`) passes and the
HP/damage scale pass in `FUN_0052ab70` runs on the next tick → full HP +
real damage. C also makes the hostility predicate (`FUN_00423580`) return a
meaningful result instead of the inert class default.

---

## 3. Proactive-defender AI (seek & attack on sight, not retaliate-only)

### Why the bare spawn is retaliate-only

Two independent reasons, both fixed by §2:

1. `+0x1F0` is the bare class default from `FUN_0043adc0(type)`. The combat
   AI driver only *proactively scans for targets* for classes in the active
   set; a passive default class only ever enters combat through the
   damage/retaliation path. Setting `+0x1F0` to an **active** value via
   `FUN_0052e420(1,val)` is the switch.
2. `+0x200` is not armed and WakeUp never ran, so the AI tick that does
   line-of-sight target scanning is gated off (`0059f580:27` `&0x40000`;
   `00549920:21` checks `+0x200 >>6 &1` i.e. bit `0x40`, which is part of
   the `0x40200000` arm value).

### The stance selector `uVar29` (CreateNPC `00482510:1110-1163`)

CreateNPC chooses `uVar29` from the side tri-state `local_6c8` and the
faction-bit accumulator `EBX` and the *current* `+0x1F0` (==7 special):

- `local_6c8 == 1` (ally side; opcode `0x08`):
  - if `(EBX & 0x4000)==0` and `+0x1F0 != 7` → **`uVar29 = 2`**
  - else → `uVar29 = 0xe`
- `local_6c8 == 0` or `< 0` with `(EBX & 0xc000)!=0`:
  - if `(EBX & 0x4000)!=0` or `+0x1F0 == 7` → `uVar29 = 7` else `uVar29 = 3`
- then `FUN_0052e420(1, uVar29)` writes `+0x1F0`, and
  `+0x200 &= 0xFFFFFFBF` (clears bit 0x40 transiently before WakeUp re-arms).

All of {2,3,7,0xe} are in the combat-active set `{3,7,8,0xc,0xe,0xf}` (note
2 is *not* in that set literally — but class 2 is the soldier/ally class that
the matrix and driver treat as active; the `00549920` set is the *speech*
gate, the *target-scan* gate is the matrix + `+0x200` arm). The **ally
defending-soldier archetype = `uVar29 = 2`**.

### Proactive-defender ally recipe (exact)

```
om = *(u32*)0x00AD5C40 ; arr = *(u32*)(om+4) ; c = *(u32*)(arr + handle*4)

1. *(u8 *)(c + 0x24)  = level                 // scale tier (B)
2. ECX=c ; FUN_0052e420(1, 2)                  // +0x1F0 = 2  (ally combatant) (C)
3. *(u32*)(c + 0x1F4) = 0x00000001             // faction/flags: bit0=awake/active (D)
                                               //   (NO 0x40000 'peaceful' bit — see §4)
4. ECX=c ; cCreature_WakeUp_0059f580()         // (E)
5. *(u32*)(c + 0x200) = 0x40200000             // arm AI controller (F)
6. (optional) ensure not stationary:
   *(u8*)(creature[0x56].spare+3) &= ~8        // clear STATIONARY bit (npc_model)
```

Step 2's class **2** + step 3's faction make the hostility matrix (§4) return
"hostile to monsters, friendly to hero/townsfolk", and steps 4-5 enable the
on-sight target scan. Confidence: structure HIGH; exact `+0x1F4` value MED
(class 2 alone may already give correct behaviour with `+0x1F4=1`; verify
with the probe in §4 — try `+0x1F4 ∈ {1, 0}` and observe).

---

## 4. Faction model — ally vs hero, hostile to monsters

### Hostility predicate `FUN_00423580(typeA, typeB)` → bool "A hostile to B"

Core (`00423580:54-66`):
```
clsA = *(i32*)(creatureA + 0x1F0)
clsB = *(i32*)(creatureB + 0x1F0)
hostile = MATRIX[ clsA*0x10 + clsB ] != 0      // 16x16 byte matrix
```
The static matrix at VA **0x008EB548** is anti-tamper-scrambled (all 0x01);
`00423580:57-66` detects this and **restores the real matrix from VA
0x00890A30** (copies 0x40 dwords = 256 bytes). The real 16×16 matrix
(`M[A][B]`, 1 = A attacks B), dumped from the binary at file off 0x490A30:

```
     B: 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
A= 0:   1  1  1  1  1  1  1  1  1  1  1  1  1  0  1  1
A= 1:   1  1  0  1  1  0  1  1  0  0  0  0  0  0  0  0
A= 2:   1  0  1  0  1  1  1  0  1  1  1  1  1  0  1  1
A= 3:   1  1  0  1  1  0  1  1  0  0  0  0  0  0  0  0
A= 4:   1  1  0  1  1  0  1  1  0  0  0  0  0  0  0  0
A= 5:   1  0  1  0  1  1  1  0  0  1  1  1  0  0  1  1
A= 6:   1  1  1  1  1  1  1  1  1  1  1  1  1  0  1  1
A= 7:   1  1  0  1  1  0  1  1  0  0  0  0  0  0  0  0
A= 8:   1  0  1  0  1  0  1  0  1  1  1  1  1  0  1  1
A= 9:   1  0  1  0  1  1  1  0  1  1  1  1  1  0  1  0
A=10:   1  0  1  0  1  1  1  0  1  1  1  1  1  0  1  1
A=11:   1  0  1  0  1  1  1  0  1  1  1  1  1  0  1  1
A=12:   1  0  1  0  1  0  1  0  1  1  1  1  1  0  1  1
A=13:   0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0
A=14:   1  0  1  0  1  1  1  0  1  1  1  1  1  0  1  1
A=15:   1  0  1  0  1  0  1  0  0  0  1  1  1  0  1  1
```

Reading it:

- **Class 13 = universal non-combatant / immune.** Row 13 all 0 (attacks
  nobody) and column 13 all 0 (nobody attacks it). This is the quest-immune
  townsperson / essential-NPC class. Use for "must not die, must not fight".
- **"Player/soldier" cluster = classes {1,3,4,7}.** Mutually: M[1][3]=1?
  Actually within {1,3,4,7} the cells are 1 (they would fight each other if
  same-team logic didn't exempt them — same-handle is exempted at
  `00423580:23`, and same-group via the `+0x39c` list). Crucially they are
  **friendly (0) to the monster cluster columns 2,5,8..12,14,15**? No —
  re-read row 1: `1 1 0 1 1 0 1 1 0 0 0 0 0 0 0 0`. Class 1 **attacks**
  {0,1,3,4,6,7} and is **friendly** to {2,5,8,9,10,11,12,13,14,15}.
- **"Monster" cluster = classes {2,8,9,10,11,12,14,15} (and 5).** Row 2:
  `1 0 1 0 1 1 1 0 1 1 1 1 1 0 1 1` → class 2 **attacks** the monster
  columns {0,2,4,5,6,8,9,10,11,12,14,15} and is **friendly** to {1,3,7,13}.

So the clean archetype mapping is:

| Want | `+0x1F0` class | Result |
|---|---|---|
| **Ally soldier** (fights monsters, friendly to hero & townsfolk) | **2** | M[2][monsters]=1, M[2][1]=M[2][3]=M[2][7]=M[2][13]=0 → never attacks hero/town | HIGH |
| Hostile monster (attacks hero & allies) | **8** (or 9/10/11/12/14) | M[8][2]=1 (attacks ally-soldier), M[8][others]=1 | HIGH |
| Town/quest immune (no combat, can't be hit by faction logic) | **13** | row/col all 0 | HIGH |
| Hero-equivalent / town guard variant | 1 / 3 / 7 | friendly to monster cluster *as targets of themselves*; matches vanilla guard | MED |

**The hero itself** resolves through `00423580:45-52`: if B is the hero
(`FUN_004266f0(B.type)`), the predicate redirects to the hero's
`+0x1ec`-referenced creature's `+0x1F0`. Net effect: an ally with `+0x1F0=2`
is **friendly to the hero** (column for hero class is 0 in row 2) — it will
**not** turn on the player. This is the requested "ally vs hero, hostile to
monsters" guarantee. HIGH.

### Faction override bits in `+0x1F4` (=offset 500)

- `+0x1F4 & 0x40000` ⇒ **peaceful/no-aggro override**: `00423580:151-157`
  forces non-hostile (or flips `local_9` false). **Do NOT set this bit on a
  defending soldier** — it is the "this NPC never fights" flag (used by
  pure-quest/town NPCs). Leaving it clear is what makes the soldier proactive.
- `+0x1F4 & 0x80000` = "is in a team list" (CreateNPC OR-s it at `:1349` when
  added to a team). Not required for a lone defender.
- `+0x1F4` bit0 (`EBX&1`) = awake/active → drives WakeUp + `+0x200`
  arm in CreateNPC (`:1355, :1369`). Set it.
- `+0x200 & 0x10000000` = per-target **grudge** mode: if set, hostility is
  decided by the `+0x39c..+0x3a0` handle list + `+0x251` counter rather than
  purely by the matrix (`00423580:88-128`). Leave clear for plain
  matrix-driven behaviour.

### Vanilla allied-soldier values (best reconstruction)

From the CreateNPC ally path (`local_6c8==1`, opcode `0x08`, then `0x12`
awake): `+0x1F0 = 2` (or `0xe` if `EBX&0x4000`/subtype 7), `+0x1F4 = 1`
(bit0 awake, no 0x40000), `+0x200 = 0x40200000`. Confidence: `+0x1F0=2`
HIGH; `+0x1F4=1` MED (could carry extra side bits depending on the exact
side opcode chosen — pin with probe).

---

## 5. Recommended in-game probes (SDK field logger)

Resolve `c` as `om=*(0x00AD5C40); arr=*(om+4); c=*(arr+handle*4)`.

1. **maxHP / clamp.** Dump `+0x24,+0x4d0,+0x4d4,+0x4d8,+0x4dc` for (a) a bare
   spawn, (b) the same type via FunkCode CreateNPC, (c) a real vanilla
   soldier. The field that tracks "post-hit cap" and differs (a) vs (b/c) is
   maxHP. Then set `+0x4d8:=+0x4d4` on (a), take a hit, re-dump → confirms
   clamp source.
2. **AI class & faction.** Dump `+0x1F0,+0x1F4,+0x200` for a vanilla allied
   soldier and a vanilla monster. Expected: soldier `+0x1F0 ∈ {1,2,3,7}`,
   monster `+0x1F0 ∈ {8,9,10,11,12,14,15}`; soldier `+0x1F4` bit0=1,
   bit `0x40000`=0; `+0x200`=0x40200000-ish.
3. **Apply §2 recipe** to a bare spawn (steps C/D/E/F with `+0x1F0=2`,
   `+0x1F4=1`, `+0x200=0x40200000`), place it near a monster, observe: it
   should walk to and attack the monster on sight and survive multiple hits;
   it must not attack the hero. If it still attacks the hero, try
   `+0x1F4=0`; if passive, verify `+0x200` and that `+0xfc==0` before
   WakeUp.
4. **Level/HP scale.** After step 3, vary `+0x24` (1 vs 20) before WakeUp and
   confirm `+0x4d4/+0x4d8` differ after one combat tick → proves the combat
   module scales HP from `+0x24`+difficulty (no explicit HP setter needed).

---

## 6. Confidence summary

| Claim | Conf |
|---|---|
| `+0x4d8` = current HP; `+0xfc==9` dead; `+0x150==6` disabled | HIGH |
| `+0x4d4` = max HP / clamp source | MED-HIGH (probe 1 confirms) |
| `FUN_0044ddc0` is orientation, NOT level (npc_model.md erratum) | HIGH |
| Bare path skips `+0x1F0` active class, `+0x1F4` commit, WakeUp, `+0x200` arm | HIGH |
| `FUN_0052e420(1,val)` sets `+0x1F0`; `=2` ⇒ ally combatant | HIGH |
| Hostility = 16×16 matrix @0x00890A30 indexed by `[A.+0x1F0*16 + B.+0x1F0]` | HIGH |
| `+0x1F0=2` ⇒ hostile to monsters, friendly to hero/townsfolk (no player turn) | HIGH |
| `+0x1F4 & 0x40000` = peaceful/no-aggro (must stay clear for defender) | HIGH |
| Exact vanilla soldier `+0x1F4` value (`1` vs side bits) | MED (probe 2/3) |
| HP auto-scales from `+0x24`+difficulty in `FUN_0052ab70` once active | MED-HIGH (probe 4) |
| Group/squad via `FUN_004a9ef0(group,handle,1)` + `creature[0x49]` | MED |

---

## Ally-team binding — never attack the hero (2026-05-16)

**Root cause found, and it is a MATRIX-POLARITY ERRATUM in §4, not a missing
team binding.** The proactive AI target scan uses *only* the hostility
predicate `FUN_00423580`; there is no separate team/side bypass in the scan.
The reason our class-2 guard attacks the hero is that **§4 inverted the
matrix semantics** and therefore picked the wrong `+0x1F0`. Decompile
evidence below; this supersedes §3/§4's `+0x1F0 = 2` recipe.

### 1. The proactive target-acquisition path (the real scan)

Xrefs to `FUN_00423580` (Ghidra `XrefsTo`): 17 callers; the proactive
**target picker** is **`FUN_00542b20`** (7 calls). It walks the nearby-creature
list (`FUN_005feaf0` → `piVar10`), range-gates on self X/Y (`in_ECX[7]/[8]`),
and for each candidate calls `FUN_00423580(self.handle, cand.handle)`; if it
returns "hostile" (and `rand()%3!=0`) it latches `pvStack_80 = cand` as the
attack target (`00542b20:331,455,567`). **The friend/foe test in the scan IS
`FUN_00423580` and nothing else** — no team-id / `+0x39c` / `+0x1F4`-side
short-circuit gates the scan itself (the `+0x39c` grudge list and the
`FUN_00423480` party check live *inside* `FUN_00423580`). So whatever makes a
vanilla soldier reject the hero must be expressible through `FUN_00423580`'s
inputs. `FUN_00599910` (which also calls `FUN_00548f60`/`004266f0`) is the
**health-bar / FX overlay** renderer (NPCUWR speech is `FUN_00549920`), NOT
the picker — earlier notes that implied otherwise are wrong.

### 2. MATRIX POLARITY — §4 is inverted (HIGH, decompiled + hexdump)

`FUN_00423580` final decision (`00423580:54-67,150-158`), single-player
(`DAT_0182ebec`/`FUN_007db280` multiplayer block at :135-148 is skipped):

```
A = param_1 (the asker / scanner) ; B = param_2 (candidate)
iVar1 = B.+0x1F0 ; iVar6 = A.+0x1F0 * 0x10
local_9 = ( MATRIX[ A.+0x1F0*16 + B.+0x1F0 ] == 0 )      // row=A, col=B
if (local_9 != false) goto end;                          // :67 short-circuit
... grudge / hero-redirect only run when local_9==false ...
end: return local_9;                                     // caller: !=0 ⇒ HOSTILE
```

`local_9` is **`cell == 0`**, and the function **returns `local_9`**, and
every caller treats a non-zero return as *hostile* (`FUN_00548f60:16`
`if (cVar1 != '\0') return 1;`). Therefore:

> **In the real matrix, byte `0x00` = "A ATTACKS B" (hostile); byte `0x01`
> = "A ignores B" (friendly).** §4 stated the opposite ("1 = A attacks B").
> This is the bug that produced the wrong `+0x1F0 = 2` recipe.

Hexdump confirms orientation (file off `0x490A30`, 16 rows × 16 bytes,
row-major, row = A = scanner):

```
A= 0: 01 01 01 01 01 01 01 01 01 01 01 01 01 00 01 01
A= 1: 01 01 00 01 01 00 01 01 00 00 00 00 00 00 00 00
A= 2: 01 00 01 00 01 01 01 00 01 01 01 01 01 00 01 01
A= 3: 01 01 00 01 01 00 01 01 00 00 00 00 00 00 00 00
A= 4: 01 01 00 01 01 00 01 01 00 00 00 00 00 00 00 00
A= 7: 01 01 00 01 01 00 01 01 00 00 00 00 00 00 00 00
   (rows 5,6,8..15 unchanged from the §4 dump; only the *reading* flips)
```

Corrected reading (**0 = attack**):

- **Player/ally cluster = {1,3,4,7}.** Rows 1,3,4,7 are identical:
  attack (`00`) columns **{2,5,8,9,10,11,12,13,14,15}** = the monster
  cluster; ignore (`01`) columns **{0,1,3,4,6,7}** = each other + the hero.
- **Monster cluster = {2,5,8,9,10,11,12,14,15}.** Row 2 attacks (`00`)
  columns **{1,3,7,13}** = the player/ally/quest cluster, ignores monsters.
- The hero runs runtime **`+0x1F0 = 1`** (empirical scan: h=1, class 1) —
  she is in the player/ally cluster.

Consequences:

- `M[2][1] = 0x00` ⇒ a `+0x1F0 = 2` creature **is hostile to the class-1
  hero**, and because `local_9` becomes `true` at `:56` the `:67`
  short-circuit jumps **past** the hero-redirect (:130-133) entirely — it
  never gets a chance to save the hero. **This is exactly why our class-2
  guard attacks the Vampiress.** §3/§4's `+0x1F0=2` recipe is WRONG.
- `M[3][1] = M[4][1] = M[7][1] = 0x01` ⇒ a `+0x1F0 ∈ {3,4,7}` creature
  **ignores the class-1 hero** while `M[3][8] = M[3][2] = 0x00` ⇒ it
  **attacks the monster cluster**. This is the ally-soldier behaviour.
- `M[1][...]` is the *hero's own* row: class 1 ignores {0,1,3,4,6,7}
  (other players/allies) and attacks monsters — consistent with the hero
  and with class-{3,4,7} allies being friendly *to each other and to her*.

### 3. The hero-redirect (when it matters) — `00423580:45-52,130-133`

When B's **def-table class byte == 4** (`FUN_004266f0(B.+0x10)`; the
*static* playable-hero marker — distinct from runtime `+0x1F0`), and B has a
controller handle `*(B+0x1ec) != 0`, then `local_8` = that controller
creature and the verdict is recomputed as
`local_9 = ( MATRIX[ local_8.+0x1F0*16 + A.+0x1F0 ] == 0 )` (row =
hero-controller's `+0x1F0`, col = A). **But this block only executes when
`local_9 == false` at `:67`** — i.e. only when `M[A.1F0][1] == 0x01` already
(A is in the ally cluster). It can *flip an ally hostile* if the hero's
controller record has a monster-ish `+0x1F0`, but it can **never rescue a
class-2 creature**, because class 2 hits the `:67` short-circuit first.
`+0x1ec` is the owner/controller-handle chain (`FUN_005997c0` walks `+0x1ec`
until handle `< 0x11` = a player slot; `FUN_004a15a0:124-127`,
`FUN_00491d40:208`). For the single-player Vampiress, the hero is the player
avatar; the redirect either no-ops (`+0x1ec==0`) or points at another
ally-cluster record — either way, choosing an ally-cluster `+0x1F0` for our
guard is correct and robust. The `FUN_00423480` "same party" check (:146) is
gated behind the multiplayer block and is irrelevant in single-player.

### 4. CONCRETE RECIPE ADDITION to npc_make_combatant

**Change exactly one value** vs the §3 recipe: set the AI/faction class to a
**player/ally-cluster** value instead of 2. **Use `+0x1F0 = 3.**

```
om = *(u32*)0x00AD5C40 ; arr = *(u32*)(om+4) ; c = *(u32*)(arr + handle*4)

1. *(u8 *)(c + 0x24)  = level                  // scale tier (unchanged, §2-B)
2. ECX=c ; FUN_0052e420(1, 3)                   // +0x1F0 = 3  (ally cluster) ***CHANGED from 2***
3. *(u32*)(c + 0x1F4) = 0x00000001              // bit0 awake; NO 0x40000 peaceful; NO 0x4 (=summon/owner-redirect) (§2-D)
4. ECX=c ; cCreature_WakeUp_0059f580()          // (§2-E)
5. *(u32*)(c + 0x200) = 0x40200000              // arm AI controller; leave bit 0x10000000 (grudge) CLEAR (§2-F)
6. (optional) clear STATIONARY: *(u8*)(creature[0x56].spare+3) &= ~8
```

Why this yields the three required properties (matrix rows, 0=attack):

- **Proactively attacks monsters:** `M[3][monsterCols 2,5,8..15] = 0x00`
  ⇒ `FUN_00423580(guard, monster)` returns hostile ⇒ `FUN_00542b20` latches
  the monster as target. Steps 4-5 enable the on-sight scan (unchanged).
- **NEVER attacks the hero:** `M[3][1] = 0x01` ⇒ `local_9=false` at `:56`;
  predicate returns `local_9` ⇒ **not hostile**. (The hero-redirect can only
  run here and, for an SP avatar, resolves to ally-cluster or no-op; it
  cannot make class 3 hostile to a class-1 hero via any column-3 cell —
  `M[1][3]=0x01`.)
- **Friendly to other allies:** `M[3][{1,3,4,7}] = 0x01`; same-handle is
  exempt at `:23`; same-group exempt via `+0x39c`. Allies of class 1/3/4/7
  mutually ignore.

`+0x1F0 = 7` is an equally valid alternative (identical row); `3` is the
plain town/neutral-defender value (it is exactly what CreateNPC's *default*
no-side-opcode path picks: `00482510:1114` `uVar29 = 3`). Do **not** use
`0xe` (CreateNPC's `local_6c8==1 && EBX&0x4000`/subtype-7 ally variant) for
the basic case — its row differs and it is the "berserk ally" stance.
Do **not** set `+0x1F4 & 0x4`: that marks the creature a summon/pet and makes
`FUN_00423480`/`FUN_00423580` substitute its `+0x251` owner handle (party
logic) — not wanted for a standalone town guard.

### 5. Cheap field-probes (1-2, SDK field logger + a run)

Resolve `c`: `om=*(0x00AD5C40); arr=*(om+4); c=*(arr+handle*4)`.

- **Probe P1 (decisive, ~2 min): read a vanilla Valorian Soldier's
  `+0x1F0`.** In a town with allied soldiers, dump `+0x1F0,+0x1F4,+0x200`
  for a real Valorian Soldier (type 257) and for the hero (h=1). Prediction
  (HIGH): soldier `+0x1F0 ∈ {3,4,7}` (NOT 2), hero `+0x1F0 == 1`,
  soldier `+0x1F4 & 0x40000 == 0`. If the soldier reads `+0x1F0 == 2`,
  the matrix-polarity conclusion is wrong — fall back and instead probe by
  flipping our guard's `+0x1F0` over {1,3,4,7} (next probe).
- **Probe P2 (recipe verify): apply §4 recipe with `+0x1F0 = 3`**, spawn
  the guard next to a monster and within sight of the hero. Expected: walks
  to and attacks the monster, **never** swings at the hero, survives
  multiple hits. If it still hits the hero, dump `+0x1F0` post-WakeUp (some
  later init may overwrite it) and re-assert after WakeUp; also try
  `+0x1F0 = 7`. If it ignores monsters too, the issue is the scan-enable
  (`+0x200`/`+0xfc`), not faction.

### 6. Confidence

| Claim | Conf |
|---|---|
| Proactive picker = `FUN_00542b20`; friend/foe = `FUN_00423580` only, no separate team gate in the scan | HIGH |
| Matrix polarity is **0 = hostile / 1 = ignore** (§4 was inverted) | HIGH (decompile :56-67,150-158 + hexdump + caller `:16`) |
| Player/ally cluster = `+0x1F0 ∈ {1,3,4,7}`; monster cluster = {2,5,8,9,10,11,12,14,15} | HIGH |
| `+0x1F0 = 2` (old §3/§4 recipe) is wrong — class 2 attacks the class-1 hero (M[2][1]=0, short-circuits before hero-redirect) | HIGH |
| New recipe `+0x1F0 = 3` (or 7): attacks monsters, ignores hero, friendly to allies | HIGH (matrix); MED pending P1/P2 in-game |
| Hero-redirect (:45-52,130-133) uses `*(B+0x1ec)` controller's `+0x1F0`; only runs when asker is already ally-cluster; cannot rescue class 2 | HIGH |
| `+0x1F4 & 0x4` = summon/owner-substitution (party path) — keep clear | MED-HIGH (`FUN_00423480:20-29`) |
| Single-player ignores the `FUN_00423480` party check (multiplayer-gated at :135-148) | HIGH |

---

## Aggro/detection range — why late +0x1F0 override is half-blind (2026-05-16)

**Bottom line: there is NO per-class detection-range table and the brief's
hypothesis is FALSE. The proactive engagement radius is a single hardcoded
constant (~800 world units). The reluctance is NOT a range desync from the
late `+0x1F0=7` override — it is `+0x1F4` (the faction/side word) lacking a
"real side" bit. `+0x1F0` plays no part in detection range. The fix is a
TEMPLATE/`+0x1F4` change, not an AI re-init and not a different `+0x1F0`.**
Decompile evidence below (all HIGH unless noted).

### 1. AI tick -> picker chain (who reads what)

Per-creature AI tick = **`FUN_00539700`** (creature-update dispatcher).
`unaff_EBP` = `c`. `switch(*(i16*)(c+0xfc))` (the AI **state**): **only
`+0xfc == 0` (idle/search) calls the proactive picker `FUN_00542b20`**
(`00539700:61-64`, case 0). Other states run motion/combat behaviors; a
second `switch(*(i16*)(c+0x150))` (`:118`) runs the locomotion/combat
sub-FSM (case 7 = `FUN_0052ab70` HP/dmg scaler). The picker fires **only
while the creature is idle state 0**. `FUN_00542b20` is invoked via one
indirect site `0x0053983c` (vtable/thunk; sole caller, `XrefsTo` = 1 ref).

In `FUN_00542b20`, `in_ECX = c`; index map: `in_ECX[3]`=`+0x0c`(self
handle), `in_ECX[4]`=`+0x10`(type), `in_ECX[6..9]`=`+0x18/1c/20/24`
(cell/X/Y/sector), `in_ECX[0x54]`=`+0x150`(sub-state),
`in_ECX[0x7c]`=**`+0x1F0`**, `in_ECX[0x7d]`=**`+0x1F4`**, `+0xfe`=awake
(set 1 by WakeUp), `+0x152`=scan cooldown, `+0x200`=AI-ctrl word.

### 2. The detection range IS a constant (no per-class/type table)

The proactive scan (`LAB_0054338b`, `00542b20:394-478`) iterates **only
self's own sector-cell bucket** (`FUN_005feaf0((short)in_ECX[6])` — a pure
`grid[+0x10] + clamp(cell)*0xc` bucket index, no range arg). Per candidate
it computes `dist = isqrt(dx*dx + dy*dy)` via **`FUN_004b2710`** (verified
pure integer sqrt), `dx = cand.+0x1c - self.+0x1c`, `dy = cand.+0x20 -
self.+0x20` (world units), and the **only** acceptance gate is the literal
**`uVar19 < 800`** at `00542b20:469`. Every distance value in the picker
(`0x7d00, 800, 0x5dc, 1000, 0x3e8, 0xc9`) is a hardcoded immediate, not a
lookup; none is indexed by `+0x1F0` or type. (WakeUp `FUN_0059f580:44-46`
separately uses 600/400 X/Y for an awake-on-touch test.) **`+0x1F0` is read
in the picker ONLY for same-class friend-skip (`:412`) and the
class-5/9/0xf special hunts — never for range.** ⇒ the brief's "range keyed
to the old class, desynced by the late `+0x1F0` write" is impossible: range
is class-independent and constant.

### 3. The REAL gate: `+0x1F4` must carry a "real side" bit (`& 0x1006dcf8`)

Two top-level gates + a branch, all on **`+0x1F4`** (`in_ECX[0x7d]`), not
`+0x1F0`:

- **Entry gate** `00542b20:129-130`:
  `(*(u16*)(c+0x152) < 100) && (*(i16*)(c+0xfe)==1) && ((c+0x1F4 & 0x1006dcfbU)!=0)`.
  Fail -> `else` = the §4 lazy-arm.
- **Active vs passive** `00542b20:259-303` (after entry passes):
  - `if ((*(u8*)(c+0x1F4)&1)==0 || c+0x150==3 || c+0x150==5)` -> **PASSIVE**
    `LAB_00544303`: at most sets `+0x3f=2` (alert), **no target scan** =
    retaliate/point-blank only. **This is the reported symptom.**
  - else (bit0 set, `+0x150 not in {3,5}`): `:303`
    `if ((c+0x1F4 & 0x1006dcf8U)!=0)` -> `LAB_00543113` -> full proactive
    scan (`LAB_0054338b`, 800u). **If `& 0x1006dcf8`==0, `:317` does
    `c+0x1F4 &= ~1` (clears the awake bit) and falls to PASSIVE —
    permanently half-blind.**

Masks: `0x1006dcf8` (real side/team set) is satisfied by `+0x1F4` bits
`0x8,0x10,0x20,0x40,0x80,0x400,0x800,0x1000,0x4000,0x8000,0x10000000`.
**NOT** by bit0 `0x1` (awake), `0x2`, `0x2000`, `0x10000`, **`0x400000`**
(the team-list OR `00482510:1349`), `0x2000000`, `0x8000000`. `0x1006dcfb`
= that set plus bits 0,1.

CreateNPC side-opcode -> EBX (committed `[0x3e].spare=EBX` @`00482510:1354`
= `+0x1F4`):

| opcode | sets | in `0x1006dcf8`? |
|---|---|---|
| `0x08` ally | `local_6c8=1` only, **NO EBX bit** | n/a |
| `0x12` awake | `EBX \| 1` | **NO** (bit0) |
| `0x2b` | `EBX = 8` | **YES** |
| `0x42` | `EBX = 0x400` | **YES** |
| `0x2e/0x30/0x32/0x2f/0x31` | `0x10/0x20/0x80/0x40` | **YES** |
| `0x11` group (`FUN_004a9ef0`) | adds to `+0x7510` list; **does NOT touch `+0x1F4`** | NO |
| team-list `00482510:1349` | `EBX \| 0x400000` | **NO** |

⇒ `friendly_town_guard`/`patrol_soldier` as `02 04 02 08 12 00` (opcodes
`0x08`+`0x12` only) ends with **`+0x1F4 = 1`** (bit0 only) -> no
`0x1006dcf8` bit -> `:303` fails -> `:317` clears bit0 -> PASSIVE. **Exactly
the "only wakes at intimate distance" symptom — unrelated to the late
`+0x1F0=7` write or any range parameter.**

### 4. Why a real vanilla soldier aggros from afar — the lazy auto-arm

`00542b20:1065-1104` (the `else`-of-`:129`):
```
if (c+0x1F4 == 0  &&  (u32)c+0x0c > 0x10  &&  *(i32*)(c+0x245) < 1  &&
    (c+0x200 & 0x40077c0) == 0  &&  FUN_004266f0(c.type)==0 /*not playable-hero class*/ &&
    c.type != 0x757  &&  ((c+0x2B9 & 0x80) || (c+0x14 & 0x80000000)))   // player-near / activated trigger
{   ... FUN_00423580(self, hero) ...
    c+0x1F4 |= (monster-default-class ? 0x20 : specialType ? 0x10000000 : 8)  // all in 0x1006dcf8
    or if hero-hostile  c+0x1F4 |= 1;
}
```
**The engine self-arms a creature's proactive side bit the first time the
player gets near — but ONLY if `+0x1F4 == 0` exactly (whole word).** This is
the native "dormant NPC wakes proactively when you approach". Vanilla town
soldiers reach full 800u scanning either via a real side opcode
(`0x2b`/`0x42`/...) or via this lazy-arm (left `+0x1F4==0`).

**Destructive interaction:** `0x12` sets `+0x1F4=1`; `1 != 0` ⇒ the
lazy-arm `:1065` is **permanently disabled**, while bit0 alone never
satisfies `:303`. So `0x08+0x12` is the *worst* combo for a proactive
defender: no real side bit and the engine is blocked from supplying one.
(`npc_templates.md §B.4`'s recorded `+0x1F4==0x00400000` is consistent only
as a partial/early read — `0x400000` is also not in `0x1006dcf8`; a truly
proactive soldier must read a `0x1006dcf8` bit, most likely `0x8`.) Pin
with Probe P1.

### 5. The fix (preferred = pure template/side pivot, NO post-spawn override)

**(c) PREFERRED — pure template pivot.** Spawn via engine CreateNPC
(`npc_templates.md §B`) with a side opcode that commits a `0x1006dcf8`-class
EBX bit, keeping the `0x12` awake opcode so bit0 is also set:
  - **RECOMMENDED stream:** `02<TYPE> 04<POS> 02<SUBID> 2b 12 00`
    -> `+0x1F4 = 9` (`8 | 1`): `:259` bit0=1 OK and `:303`
    `9 & 0x1006dcf8 = 8` OK ⇒ **ACTIVE proactive scan, full 800u radius.**
  - `02<TYPE> 04<POS> 2b 00` -> `+0x1F4 = 8`: passes entry gate but `:259`
    bit0=0 routes PASSIVE — use the recommended stream instead.
  NO `FUN_0052e420` post-spawn override is needed *for range*. For
  friend/foe leave the type's native matrix class (verify soldier type 257
  via P1); the `FUN_0052e420(1,7)` write, if kept for friend/foe, does NOT
  affect range and was never the cause — changing/removing it will NOT fix
  aggro. The required change is to `+0x1F4`.

**(a) If keeping bare-spawn + post-spawn recipe (`npc_make_combatant`):**
change only the `+0x1F4` write (currently `0x1` or `0x00400000` — both
half-blind):
```
om=*(u32*)0x00AD5C40; arr=*(u32*)(om+4); c=*(u32*)(arr+handle*4);
*(u32*)(c+0x1F4) = 0x00000009;   // bit0 awake + bit3 (0x8) real side  <- THE FIX
ECX=c; FUN_0052e420(1, 3);       // +0x1F0 ally cluster (friend/foe only, unchanged)
*(u8*)(c+0x24) = level;
ECX=c; cCreature_WakeUp_0059f580();   // sets +0xfe=1 (entry gate :129 needs it)
*(u8*)(c+0x2B7) &= ~8;           // un-stationary
// keep 0x40000 CLEAR (it suppresses, see above sections); +0x200 per §B.4
```
`0x9` is the minimal value satisfying BOTH `:259` (bit0) and `:303`
(`0x8`). Conf HIGH (mechanism); exact vanilla-identical value MED — P1.

**(b) No AI-reinit leaf needed and none exists.** The picker derives sight
state live from `+0x1F4`/`+0xfe`/`+0x150`/position each tick; there is no
class-keyed AI-sight (re)init function. Option (b) is moot.

### 6. Cheap in-game probes (SDK field logger; c via om/arr)

- **P1 (decisive, ~2 min) — read a real proactive Valorian Soldier.** Find
  a soldier that aggros a monster from afar; dump
  `+0x1F0,+0x1F4,+0xfe,+0x150,+0xfc`. Predict (HIGH):
  `+0x1F4 & 0x1006dcf8 != 0` (expect `0x8`; maybe `0x400`),
  `+0x1F4 & 1 == 1`, `+0xfe == 1`. Dump our guard: expect `+0x1F4 == 1`
  (or `0x400000`), `& 0x1006dcf8 == 0` — the smoking gun. If the vanilla
  soldier also reads `+0x1F4 == 1`, re-open (decompile `:303`/`:317` makes
  that very unlikely).
- **P2 (fix verify) — apply §5(a) with `+0x1F4 = 0x9`** on an otherwise
  identical bare guard near a monster, hero in view. Expect: walks to &
  attacks the monster from ~800u, not point-blank; never swings at the hero
  (matrix class 3/7); tanky. Still point-blank: re-dump `+0x1F4`
  post-WakeUp (re-assert AFTER WakeUp), confirm `+0x150 not in {3,5}` and
  `+0xfc==0` on an idle tick. Hits the hero ⇒ matrix (`+0x1F0`) issue,
  unrelated to this fix.

### 7. Confidence

| Claim | Conf |
|---|---|
| Detection radius = hardcoded ~800 world units (`00542b20:469`, isqrt `FUN_004b2710`); no per-class/type range table | HIGH |
| Brief hypothesis (late `+0x1F0` desyncs range) is FALSE — range never reads `+0x1F0` | HIGH |
| Picker runs only at AI state `+0xfc==0` (`FUN_00539700:61` case 0) | HIGH |
| Passive/active branch hinges on `+0x1F4` bit0 (`:259`) AND `+0x1F4 & 0x1006dcf8` (`:303`); else `:317` clears bit0 -> half-blind forever | HIGH |
| `0x08+0x12` template -> `+0x1F4=1` -> no `0x1006dcf8` bit -> exactly the point-blank-only symptom | HIGH |
| Engine lazy-auto-arms proactive side bit only when `+0x1F4==0` (`:1065`); `0x12` setting `+0x1F4=1` disables it | HIGH |
| Fix = give `+0x1F4` a `0x1006dcf8` bit (recommend `0x9`), via side opcode `0x2b` (template, preferred) or direct write; no AI-reinit needed | HIGH (mechanism); MED (exact vanilla value — P1) |
| No standalone AI-sight-(re)init leaf exists; brief option (b) moot | HIGH |

---

## Proactive guard aggro — re-grounded (2026-05-16)

**This section SUPERSEDES the prior "Aggro/detection range" §3 conclusion
that a `0x1006dcf8` "real side" bit in `+0x1F4` is *required* for the
proactive scan. That was wrong.** Re-grounded from the full
`FUN_00542b20` decompile + capstone disasm of the load-bearing branches.
All offsets are byte offsets from `cCreature` (esi in the picker;
`in_ECX[N]` ⇒ `+N*4`). The diagnostic-log field ambiguity is resolved
first because it changes everything.

### 0. The `f14` vs `+0x1F4` ambiguity — RESOLVED from the scan code

`sdk\player_state.cpp::scan_creatures` (the world scan that produced the
brief's vanilla line) reads, verbatim:
```
safe_read(c+0x1f0,&st);  safe_read(c+0x1f4,&fac);
safe_read(c+0x14, &f14); safe_read(c+0x4d8,&hp);
```
So in `st=00000007 fac=00004000 f14=80400012 hp=607`:
- `st`  = `cCreature+0x1F0` = **7** (AI/matrix class — player/ally cluster)
- `fac` = `cCreature+0x1F4` = **0x00004000**  ← the faction/side word
- `f14` = `cCreature+0x14`  = **0x80400012** (an UNRELATED flags word;
  `+0x14 & 0x80000` is the charm/thrall glow bit per HANDOFF; not the
  picker input). The picker reads `+0x14` only at `:62` (`& 0x100000`
  early-return) — `0x80400012 & 0x100000 = 0`, harmless.

⇒ The vanilla active guard's persistent faction word is
`+0x1F4 = 0x4000`; our spawned guard's is `+0x1F4 = 0x1`. The picker
input is `+0x1F4` (`in_ECX[0x7d]`), NOT `+0x14`. CONF: HIGH.

### 1. Q1 — the exact proactive-scan gate (disasm-verified)

Per-creature AI tick `FUN_00539700` dispatches `switch(*(i16)(c+0xfc))`;
**`case 0` → picker `FUN_00542b20`** (`00539700:61-64`). Picker runs only
while AI-state `+0xfc == 0` (idle/search). Inside the picker, to reach the
broad nearby-creature scan `LAB_0054338b` (the loop that calls
`FUN_00423580` and latches a target), ALL of these must hold:

| # | Gate (VA) | Condition | our `+0x1F4=1` |
|---|---|---|---|
| g1 | `0x542b3a` | `(c+0x14 & 0x100000)==0` | ✓ (0x40400000) |
| g2 | `0x542b46` | `(c+0x200 & 0x40000)==0` | ✓ (0 or 0x40200000) |
| g3 | `0x542cae` | `*(u16)(c+0x152) < 100` | needs scan-cooldown low |
| g4 | `0x542cbc` | `*(i16)(c+0xfe) == 1` (**awake**) | needs WakeUp to have stuck |
| g5 | `0x542cca` | `(*(u32)(c+0x1F4) & 0x1006dcfb) != 0` | ✓ (`1&..fb=1`) |
| g6 | `0x5430??` | `+0x150 ∉ {3,5}` AND `(*(byte)(c+0x1F4)&1)!=0` (**bit0**) | ✓ bit0=1 |
| g7 | `0x5430f6` | branch on `(c+0x1F4 & 0x1006dcf8)`: if ==0 → straight to scan; if !=0 → must pass hero-resolve `FUN_00548f30` else **`+0x1F4 &= 0x1006dcf8`** and go PASSIVE | `1 & ..f8 = 0` ⇒ straight to scan |

Disasm of the decisive branch (g5..g7), capstone, base 0x400000:
```
00542cae cmp word [esi+0x152],0x64        ; g3
00542cb6 jae 0x54443e                      ;   fail → passive return
00542cbc cmp word [esi+0xfe],1             ; g4 (awake)
00542cc4 jne 0x54443e                      ;   fail → passive return
00542cca test dword [esi+0x1f4],0x1006dcfb ; g5 (entry side mask, incl bit0)
00542cd4 je  0x54443e                      ;   fail → passive return
...
005430e6 test ecx,ecx / 005430ed call 0x548f30 / 005430f4 jne 0x543113  ; hero-resolve OK → SCAN
005430f6 mov  ecx,[esi+0x1f4]
005430fc and  ecx,edi                      ; edi = 0x1006dcf8
00543100 mov  [esi+0x1f4],ecx              ; *** PER-TICK REWRITE: +0x1F4 &= 0x1006dcf8 ***
00543106 test al,1                          ; bit0 of masked value
00543108 je   0x544303                      ;   bit0 now clear → PASSIVE
```
Then the scan-loop run/skip gate (`LAB_0054338b`, `:396-397`):
```
005433a5 cmp [esp+0x20],edx / 5433a9 jne 5433bd ; pvStack_70==pvStack_80 ?
005433ab call 0x849b40 / cdq / idiv 3 / cmp edx,2 / je 5433dc ; && rand()%3==2
005433bd cmp dword [esi+0x1f0],5  / je 5433dc        ; OR +0x1F0==5
005433c6 test dword [esi+0x1f4],0x40000 / jne 5433dc ; OR +0x1F4 & 0x40000
005433d2 cmp dword [esi+0x10],0x4b / jne 0x543631    ; OR type==0x4b ; else SKIP scan
```

**Decisive facts (all HIGH, disasm-verified):**

1. `+0x1F4 = 1` (bit0 only) **DOES reach the proactive scan** — g5 passes
   (`1 & 0x1006dcfb`=1), g6 passes (bit0=1, `+0x150∉{3,5}`), g7 takes the
   `& 0x1006dcf8 == 0` arm and falls straight to `LAB_0054338b`. The prior
   "needs a 0x1006dcf8 bit or it's half-blind" claim is FALSE for bit0-only.
2. The numeric value that makes a type-286, `+0x1F0=7` creature
   proactively aggro at range is simply **`+0x1F4` with bit0 set, no
   `0x1006dcf8` bit, AND `+0xfe==1`, `+0xfc==0`, `+0x152<100`,
   `+0x150∉{3,5}`**. `+0x1F4 = 1` already satisfies the `+0x1F4` part.
3. Once at `LAB_0054338b`, the broad scan runs **every idle tick** only if
   `+0x1F0==5` OR `+0x1F4 & 0x40000` OR `type==0x4b`; otherwise it runs on
   a **`rand()%3==2`** (≈1-in-3 idle ticks) — which is the *normal vanilla
   guard cadence* (sub-second reacquire) and is plenty proactive. So
   per-tick scan frequency is NOT the bug.
4. **`+0x1F4 & 0x40000` is NOT a clean "scan harder" bit.** In
   `FUN_00423580:150-157` (`0x40000` on either party) it *inverts* the
   matrix friend/foe verdict — setting it would make the guard attack the
   hero/allies and ignore monsters. Do NOT set `0x40000`.

### 2. Q2 — what a vanilla guard really has; engine-correct persistent state

- A vanilla `friendly_town_guard` CreateNPC stream `02<T> 04<POS> 02<SUB>
  08 12 00`: opcode `0x08` → `local_6c8=1`; `0x12` → `EBX=1`. Stance
  block `00482510:1134-1136`: `local_6c8==1 && EBX&0x4000==0 &&
  +0x1F0!=7` → `uVar29=2` → `FUN_0052e420(1,2)` → `+0x1F0=2`. Commit
  `00482510:1354 +0x1F4 = EBX = 1`; `:1355 EBX&1` → `WakeUp` (sets
  `+0xfe=1`); `:1369` → `+0x200 = 0x40200000`. **So a vanilla
  template-spawned guard ends at `+0x1F4 = 1`, `+0xfe = 1`** — identical
  to what our spawn *should* have. The persistent proactive state is
  therefore **`+0x1F4` bit0 + `+0xfe==1` + a non-monster matrix class in
  `+0x1F0`**, NOT a special side bit.
- The scanned vanilla guard read `+0x1F4 = 0x4000` because `+0x1F4` is
  mutated at runtime by combat/AI (it is a working register, not a stable
  config): the picker writes `+0x1F4 &= 0x1006dcf8` at `0x543100`,
  `FUN_00461540:171-172` rewrites it to `(old & 0x2000)|(param &
  0x6682304)`, the lazy-arm ORs `8`/`0x20`/`0x10000000`/`1`. `0x4000`
  (bit 14) is in `0x1006dcf8`/`0x1006dcfb`; a guard that has been in
  combat settles to whatever the combat path left. It is NOT an authored
  template value and must NOT be copied verbatim. CONF: HIGH.
- Map-placed vanilla guards that were authored with NO side opcode commit
  `+0x1F4 == 0`; the **lazy-arm** `00542b20:1065-1104` then self-arms them
  the first time the player approaches (`+0x1F4 |= 8` / `|1`, all reached
  only when `+0x1F4 == 0` exactly). Both routes (template `08 12` ⇒ bit0;
  or `+0x1F4==0` ⇒ engine lazy-arm) yield a proactive guard. The engine
  does NOT derive proactiveness from a detection-range float, a behavior
  pointer, `+0xfc`, or `+0x1F0` — `+0x1F0` is friend/foe only (matrix).

### 3. Q3 — why per-frame writes lose; the real reluctance cause

Two distinct mechanisms, both disasm-confirmed:

- **Why a poked value decays:** if the picker ever takes the g7
  `(+0x1F4 & 0x1006dcf8)!=0` arm and the hero-resolve `FUN_00548f30`
  fails, `0x543100 mov [esi+0x1f4],ecx` executes `+0x1F4 &= 0x1006dcf8`
  *that tick* — stripping bit0 (and 0x4/0x40000/etc). A poked `+0x1F4=9`
  → `9 & 0x1006dcf8 = 8` (bit0 gone) → PASSIVE; subsequent ticks/lazy-arm
  or `FUN_00461540` (`(old&0x2000)|(param&0x6682304)` — wipes bit0, 0x8,
  0x40000) drive it toward `1`/`8`. This is the "AI tick rewrites it /
  9 decays to 1" the brief observed. So fighting it per-frame is futile.
- **Why our guard is reluctant even at `+0x1F4=1`:** `+0x1F4=1` reaches
  the scan ONLY if g4 `+0xfe==1` (awake) also holds on idle ticks. Our
  spawn ground truth shows `+0x200 = 0` (HANDOFF deliberately sets
  `+0x200=0`, not the CreateNPC `0x40200000`). WakeUp `FUN_0059f580`
  sets `+0xfe=1` **only if** `(+0x200 & 0x40000)==0 && +0xfc==0 &&
  +0xfe==0` at call time and is **not re-applied** by the idle loop. Any
  transient that leaves `+0xfe != 1` on an idle tick fails g4 → picker
  jumps to the `else` at `00542b20:1045`; the lazy-arm there requires
  `+0x1F4 == 0` (ours is 1) so it does nothing and `:1106` sets
  `+0xfe = 0` — the guard is stuck never entering the scan, reacting only
  via the damage/retaliation path = the observed point-blank behavior.
  Net: `+0x1F4=1` is correct but is **dead weight without a reliably
  latched `+0xfe==1`**, and `+0x1F4=1` simultaneously **disables the
  lazy-arm fallback** (which needs `+0x1F4==0`). CONF: HIGH (mechanism).

There is a **stable engine input** that keeps it proactive without
per-frame fighting: let the engine's own CreateNPC post-create block run
(it sets `+0x1F4=1`, calls WakeUp, sets `+0x200=0x40200000` so the
WakeUp precondition `(+0x200&0x40000)==0` holds and `+0xfe` latches), and
do NOT subsequently zero `+0x200` or rewrite `+0x1F4`. The
HANDOFF-recorded "set `+0x200=0`, `+0x1F4=0x00400000`" post-spawn deltas
are the regression: `0x400000 ∉ 0x1006dcfb` so g5 *fails outright*
(point-blank only), and `+0x200=0` removes the AI-arm. CONF: HIGH.

### 4. Q4 — concrete SDK recipe

**(a) PREFERRED — pure template, let the engine init (CONF: HIGH).**
Author the guard via engine CreateNPC with stream
`02<TYPE> 04<POS> [02<SUBID>] 08 12 00` (the vanilla `friendly_town_guard`
opcodes: `0x08` ally side, `0x12` awake). The engine then itself does:
`+0x1F0` = stance 2 (override to 7 below for friend/foe parity with
vanilla), `+0x1F4 = 1`, `WakeUp` ⇒ `+0xfe = 1`, `+0x200 = 0x40200000`.
**Apply NO post-spawn `+0x1F4`/`+0x200` writes.** This reproduces the
exact persistent state of a vanilla template guard. Friend/foe: keep the
type's matrix class or set `+0x1F0 = 7` (vanilla guard parity; class 7
attacks monsters {2,5,8..15}, ignores hero/ally {1,3,4,7} — matrix
verified). Do this BEFORE/at WakeUp via the engine stance, not after.

**(b) If keeping the bare-spawn + post-spawn path (CONF: HIGH mechanism;
the ABI items are already proven elsewhere in this doc):**
```
om=*(u32*)0x00AD5C40; arr=*(u32*)(om+4); c=*(u32*)(arr+handle*4);
*(u8 *)(c+0x24)  = level;                       // HP scale tier
ECX=c; FUN_0052e420(1, 7);                       // +0x1F0 = 7 (friend/foe; matrix class 7)
*(u32*)(c+0x1F4) = 0x00000001;                   // bit0 only — reaches scan via g7 ==0 arm
*(u32*)(c+0x200) = 0x40200000;                   // ARM AI ctrl  *** restore this — do NOT leave 0 ***
ECX=c; cCreature_WakeUp_0059f580();              // sets +0xfe=1 (precond (+0x200&0x40000)==0 holds: 0x40200000&0x40000=0)
*(u8*)(c+0x2B7) &= ~8;                           // un-stationary (optional)
// Do NOT set +0x1F4 = 0x00400000 (∉0x1006dcfb ⇒ g5 fails ⇒ point-blank).
// Do NOT set +0x1F4 & 0x40000 (inverts friend/foe in FUN_00423580).
// Do NOT re-poke +0x1F4 per frame (0x543100 / FUN_00461540 will fight you).
```
Order matters: write `+0x200` and `+0x1F0`/`+0x1F4` BEFORE `WakeUp`
(WakeUp checks `+0x200 & 0x40000`, copies `+0x204→+0x208`, latches
`+0xfe=1`). `FUN_0052e420` = `0x0052E420` __thiscall(self,int mode,
u32 val); `cCreature_WakeUp` = `0x0059F580` __thiscall(self) — both
ABIs already exercised in this project (npc_templates.md §B.4).

**ONE cheap breakpoint that settles the only open item** (does `+0xfe`
stay 1 across idle ticks, i.e. is `+0x200=0x40200000` enough, or does
something re-clear `+0xfe`): HW-exec BP at **`0x00542cbc`** (`cmp word
[esi+0xfe],1`) with the guard's `esi`; log `esi`, `[esi+0xfe]`,
`[esi+0xfc]`, `[esi+0x1f4]`, `[esi+0x150]`, `[esi+0x152]` each hit. If
`+0xfe==1`, `+0xfc==0`, `+0x152<100`, `+0x150∉{3,5}`, `+0x1F4` bit0=1 on
the breakpoint, the guard IS entering the scan and any remaining
reluctance is the rand()%3 cadence (vanilla-normal). If `+0xfe==0` there,
WakeUp isn't latching → re-assert `+0x200=0x40200000` then re-call
`FUN_0059f580` after the engine's own init completes.

### 5. Confidence

| Claim | Conf |
|---|---|
| Log `fac`=+0x1F4, `f14`=+0x14 (separate field) — from scan_creatures source | HIGH |
| Vanilla guard persistent state = `+0x1F4` bit0 + `+0xfe==1` + non-monster `+0x1F0`; `0x4000` snapshot is a runtime combat residue, not authored | HIGH |
| `+0x1F4=1` (bit0 only) DOES reach the proactive scan (g7 `&0x1006dcf8==0` arm) — prior "needs 0x1006dcf8 bit" claim REFUTED | HIGH (disasm 0x5430f6) |
| Per-tick rewrite that eats poked values = `0x00543100 mov [esi+0x1f4],ecx` (`&=0x1006dcf8`) + `FUN_00461540:171` | HIGH |
| Reluctance root cause = `+0xfe` not latched at 1 on idle ticks (HANDOFF set `+0x200=0`, killing the AI-arm) + `+0x1F4=1` disables lazy-arm fallback | HIGH (mechanism); the `+0xfe` persistence detail = the BP above |
| `+0x1F4 & 0x40000` inverts friend/foe (`FUN_00423580:150-157`) — must NOT set | HIGH |
| Fix = engine-template (a) OR post-spawn (b) with `+0x1F4=1`, `+0x200=0x40200000`, WakeUp, no per-frame writes | HIGH (mechanism); MED until the one BP confirms `+0xfe` persistence |
| `+0x1F0=7` gives correct friend/foe (attacks monsters, ignores hero/allies) | HIGH (matrix @0x490A30 verified) |

---

## Proactive guard aggro — DEFINITIVE (2026-05-16)

**This section SUPERSEDES and reconciles every prior pass: "## 3. Proactive-
defender AI", "## Aggro/detection range", "## Proactive guard aggro — re-
grounded", and the contradictory `npc_make_combatant` comment block in
`player_state.cpp`. Conclusions are raw-disasm-grounded (capstone, base
0x400000). The +0x200 contradiction is resolved by a single hard fact both
prior passes missed: the engine's "AI-arm" magic value is written to
`+0x204`, NOT `+0x200`. `in_ECX[N]` = byte offset `+N*4`.**

### 0. THE ROOT FACT THAT RESOLVES THE CONTRADICTION

CreateNPC's own awake/post-create tail, raw disasm (the `pTVar12[0x40].spare`
in the Ghidra decompile = slot 0x40*8 + 4 = **0x204**, decompiler artifact —
the bytes prove it):

```
; FUN_00482510, the "08 12" ally-guard path (esi/EBX = side accumulator)
004839f6 mov  dword [edi+0x1f4], esi          ; +0x1F4 = EBX  (for "08 12" => 1)
004839fc and  esi, 1                           ; awake bit?
004839ff je   0x483a08                         ; no -> skip WakeUp
00483a03 call 0x59f580                          ; WakeUp(ECX=creature)   <-- runs while +0x200==0 AND +0x204==0
00483a08 ... (+0x2b7 stationary bit per op 0x6b)
00483a24 cmp  esi, ebx
00483a26 je   0x483a32
00483a28 mov  dword [edi+0x204], 0x40200000    ; *** +0x204, NOT +0x200 ***  (only if awake)
```

WakeUp internals, raw disasm:

```
0059f59c test dword [esi+0x200], 0x40000        ; PRECONDITION: bit18 of +0x200 must be CLEAR
0059f5a6 jne  0x59f7f8                           ;   set -> WakeUp is a no-op
0059f5ac cmp  word  [esi+0xfc], 0                ; +0xfc==0 (alive/idle)
0059f5b6 cmp  word  [esi+0xfe], 0                ; +0xfe==0 (not already awake)
0059f5c0 mov  eax, dword [esi+0x204]             ; read +0x204
0059f5c6 mov  word  [esi+0xfe], 1                ; *** LATCH AWAKE ***
0059f5cf mov  dword [esi+0x208], eax             ; +0x208 = +0x204
```

The picker's ONLY `+0x200` read that gates the scan, raw disasm:

```
; FUN_00542b20
00542b3e test dword [esi+0x14],  0x100000        ; g1
00542b45 jne  0x5446dc                            ;   -> bail (no scan)
00542b4b test dword [esi+0x200], 0x40000          ; g2  *** the sole +0x200 scan gate ***
00542b55 jne  0x5446dc                            ;   -> bail (no scan, ever)
```

`0x40200000 & 0x40000 == 0`. `0 & 0x40000 == 0`. Bit 0x40 (`+0x200>>6&1`,
think-cadence at 0x5399cc): `0x40200000>>6&1 == 0`, `0>>6&1 == 0`. Lazy-arm
gate `test [esi+0x200],0x40077c0` (0x5444d7): `0x40200000 & 0x40077c0 == 0`,
`0 & 0x40077c0 == 0`. AI-dispatch gate `test byte [ebp+0x200],0xc` (0x539779):
`0x40200000 & 0xc == 0`, `0 & 0xc == 0`.

> **At EVERY field that gates proactive AI, `+0x200 == 0` and
> `+0x200 == 0x40200000` are BIT-IDENTICAL. `+0x200` does not, anywhere in
> the think/target chain, distinguish a proactive guard from a passive one.
> The whole A-vs-B argument was about the wrong field.** CONF: HIGH.

### 1. RECONCILING PASS A AND PASS B (both partly wrong, now explained)

- **Pass B ("+0x200=0 killed the AI-arm; restore 0x40200000") — WRONG
  premise, accidentally harmless.** The engine's AI-arm is `+0x204 =
  0x40200000` (0x483a28), consumed by WakeUp into `+0x208`. Writing
  `+0x200 = 0x40200000` neither helps nor (by itself) hurts the scan — every
  gate masks bits that 0x40200000 doesn't contain. Pass B "worked" in any
  test only because the SAME recipe also called WakeUp; the +0x200 write was
  inert.
- **Pass A ("force 0x40200000 hung them; keep +0x200==0") — WRONG cause,
  right value by accident.** Keeping `+0x200==0` is correct, but NOT because
  0x40200000 is dangerous in +0x200 (it is inert there). The "hang" Pass A
  saw came from a *different* delta in that same experiment: setting
  `+0x1F4 = 0x00400000`. `0x400000 & 0x1006dcfb == 0` so the picker's g5
  entry gate `00542cca test [esi+0x1f4],0x1006dcfb / je 0x54443e` FAILS
  outright -> the creature never scans (point-blank only) -> looked "hung".
  The +0x200 write was a red herring in both passes; the real variable was
  always `+0x1F4` + whether `+0xfe` was latched.

> **Resolved truth about +0x200: it is irrelevant to proactive aggro. A
> vanilla hand-placed guard in steady state has `+0x200 == 0` (ctor default;
> nothing on the `08 12` path writes +0x200). Do not write +0x200 at all.
> The engine's actual AI-arm is `+0x204 = 0x40200000`, set AFTER WakeUp by
> CreateNPC; WakeUp copies +0x204 -> +0x208 and latches `+0xfe = 1`.**
> CONF: HIGH (raw disasm 0x483a28 / 0x59f5c0-0x59f5cf / 0x542b4b).

### 2. Q1 — vanilla hand-placed guard init vs our driver (the real gap)

Map/dev-authored NPCs are read by the TLV interpreter **`FUN_004a2b40`**
(the same opcode reader family our driver feeds). Both the FunkCode
CreateNPC handler `FUN_00482510` AND the generic placement path converge on
the **shared post-create initializer `FUN_00461540`** (it owns the
stance/`FUN_0052e420`, the conditional `WakeUp` at 0x461deb, the team-bind
`FUN_004a9ef0`, the `+0x2b7` stationary bit, the DlgNPC/quest bind, group
membership, level/HP, and the smith/trader/master class bits at +0x200
0x1000/0x2000/0x4000). `FUN_00461540` itself **never writes `+0x204 =
0x40200000`** — that store is unique to `FUN_00482510:0x483a28`. So:

- Our `createnpc_engine` driving `FUN_00482510` with `02<T> 04<POS>
  [02<SUB>] 08 12 00` **DOES reach the full, correct init**, including
  `+0x1F4=1`, `WakeUp` (-> `+0xfe=1`), `+0x204=0x40200000`. There is **no
  missing AI-package / behaviour-pointer / detection-range / guard-post
  field** — proactiveness is not stored as any such thing (Q2). The driver
  is engine-faithful AS LONG AS we do not post-mutate the fields it set.
- The ACTUAL gap is entirely post-spawn: `npc_make_combatant` /
  `dlgnpc_bind` then overwrite `+0x1F4` (to `1` or `0x00400000`) and/or
  `+0x200`, and the `stance(1,7)` is applied as a late `+0x1F0` poke. The
  late `+0x1F0=7` is harmless for friend/foe (matrix), but the `+0x1F4`
  rewrites and any per-frame re-poke are the regression (§4).

### 3. Q2 — the real proactive-detection mechanism (exact, disasm)

Proactiveness is **not** a field value, a behaviour pointer, a detection
float, or `+0x1F0`. It is: *the picker `FUN_00542b20` reaches its broad
nearby-creature scan loop `LAB_0054338b` while the creature is in AI-state
`+0xfc==0`*. The scan radius is the hardcoded `uVar19 < 800` in
`00542b20` (isqrt `FUN_004b2710`); class-independent (confirmed: no +0x1F0/
type-indexed range table). To reach `LAB_0054338b` every idle tick, ALL must
hold (raw disasm verified):

| Gate (VA) | Condition | our spawn `+0x1F4=1`, `+0x200=0`, `+0xfe=1` |
|---|---|---|
| 0x542b3e | `+0x14 & 0x100000 == 0` | PASS (+0x14=0x40400000) |
| 0x542b4b | `+0x200 & 0x40000 == 0` | PASS (+0x200=0) |
| 0x542cae | `*(u16)(c+0x152) < 100` (scan cooldown) | PASS when not mid-cooldown |
| 0x542cbc | `*(i16)(c+0xfe) == 1` (**AWAKE — set only by WakeUp**) | **the linchpin** |
| 0x542cca | `*(u32)(c+0x1f4) & 0x1006dcfb != 0` | PASS (`1 & ..fb = 1`) |
| ~0x5430xx | `+0x150 not in {3,5}` AND `(*(byte)(c+0x1f4)&1)!=0` (bit0) | PASS (bit0=1) |
| 0x5430f6 | branch on `+0x1F4 & 0x1006dcf8`: ==0 -> straight to scan; !=0 -> must pass `FUN_00548f30` else `00543100 mov [esi+0x1f4],ecx` does `+0x1F4 &= 0x1006dcf8` (strips bit0) -> PASSIVE | `1 & ..f8 = 0` -> straight to scan |

AI-state `+0xfc==0` -> case 0 -> `FUN_00542b20` is `FUN_00539700` `switch
(+0xfc)` case 0. Once at `LAB_0054338b`, the broad scan runs every idle tick
if `+0x1F0==5 || +0x1F4&0x40000 || type==0x4b`, ELSE on `rand()%3==2`
(~1-in-3 idle ticks = normal vanilla guard reacquire cadence — plenty
proactive; NOT the bug). Friend/foe inside the scan is `FUN_00423580` only
(16x16 matrix @0x00890A30, **0x00=attack / 0x01=ignore**); `+0x1F0=7` =
attacks monster cluster, ignores hero/ally cluster {1,3,4,7}. CONF: HIGH.

> **The proactive mechanism = `+0xfe==1` (awake, set ONLY by WakeUp and
> NEVER re-set by the idle loop) + `+0x1F4` bit0 set + `+0x1F4 &
> 0x1006dcfb != 0` (bit0 alone satisfies this) + `+0xfc==0` + `+0x152<100`
> + `+0x150 not in {3,5}` + `+0x200 & 0x40000 == 0`. A non-monster `+0x1F0`
> (7) gives correct friend/foe. The scanned vanilla guard's `+0x1F4=0x4000`
> is runtime combat residue (the per-tick `00543100 +0x1F4 &= 0x1006dcf8`,
> `FUN_00461540` rewrite `(old&0x2000)|(param&0x6682304)`, lazy-arm ORs),
> NOT an authored value — do NOT copy 0x4000.** CONF: HIGH.

### 4. Q3 — why per-frame writes lose; the true reluctance cause

1. **Decay of poked `+0x1F4`:** if the picker ever takes the g7
   `(+0x1F4 & 0x1006dcf8)!=0` arm and `FUN_00548f30` fails, `00543100 mov
   [esi+0x1f4],ecx` runs `+0x1F4 &= 0x1006dcf8` THAT tick. A poked
   `+0x1F4=9` -> `9 & 0x1006dcf8 = 8` (bit0 lost) -> PASSIVE; `FUN_00461540`
   reinit `(old&0x2000)|(param&0x6682304)` and the lazy-arm further churn
   it. Fighting `+0x1F4` per-frame is futile. (Bit0-only `+0x1F4=1` takes
   the g7 `==0` arm and is NOT stripped — it is the stable value.)
2. **The real reluctance:** `+0x1F4=1` reaches the scan ONLY if g4
   `+0xfe==1` holds on idle ticks. `+0xfe` is set to 1 **exclusively by
   WakeUp** (0x59f5c6) and is **cleared to 0 by the picker itself on many
   exit paths** (the `*(u16)(c+0xfe)=0` stores, e.g. at the
   `LAB_00544418` tail) — it is consumed each pass and must be
   RE-LATCHED. The idle loop's only re-arm is the lazy-arm at
   `00542b20` line 1065, which requires `+0x1F4 == 0` EXACTLY. Our
   post-spawn delta sets `+0x1F4=1` (or 0x00400000), so the lazy-arm is
   permanently disabled, and nothing re-asserts `+0xfe`. Net: after the
   first scan pass clears `+0xfe`, the guard never re-enters the scan and
   only the damage/retaliation path fires -> exactly the observed
   point-blank behaviour. The HANDOFF `+0x1F4=0x00400000` makes it worse:
   `0x400000 & 0x1006dcfb == 0` -> g5 (0x542cca) fails outright every tick.
   CONF: HIGH.

The engine-faithful resolution is to let the creature stay in the EXACT
state CreateNPC leaves it: `+0x1F4=1`, `+0x204=0x40200000`, `+0xfe=1`,
`+0x200=0` — and crucially **do not post-mutate `+0x1F4`/`+0x200`/`+0xfe`
at all**. The recurring `+0xfe` re-latch in vanilla comes from the normal
combat/AI FSM transitions for a creature whose `+0x1F4`/`+0x1F0` the engine
itself owns end-to-end. The moment we hand-edit those fields we take the
creature off that managed path and `+0xfe` stops being re-armed. CONF: HIGH
(mechanism); the precise vanilla `+0xfe` re-latch cadence is the one
residual unknown (BP below).

### 5. Q4 — THE SINGLE RECIPE

**(a) PREFERRED — pure engine template, ZERO post-spawn field writes.
CONF: HIGH.**

Drive `createnpc_engine` (FUN_00482510) with the vanilla
`friendly_town_guard` stream and apply NOTHING afterward:

```
record payload (after the 01 00 <size> header createnpc_engine adds):
  00                      flags
  02 <TYPE_LO> <TYPE_HI>  opcode 0x02  creature type   (286 = 0x11E -> 1E 01)
  04 <POS...>             opcode 0x04  spawn position
  02 <SUBID_LO> <SUBID_HI>opcode 0x02  sub-id (optional, template-specific)
  08                      opcode 0x08  ALLY side  (sets local_6c8=1)
  12                      opcode 0x12  AWAKE      (sets EBX|=1)
  00                      END
=> stream bytes:  02 1E 01  04 <pos>  [02 <sub>]  08  12  00
```

The engine then itself performs, in order:
`+0x1F0` = stance (its `08`-path value; we override friend/foe below),
`004839f6 +0x1F4 = 1`, `00483a03 WakeUp` (-> `+0xfe=1`, copies the still-zero
`+0x204`->`+0x208`), `00483a28 +0x204 = 0x40200000`. Result is the EXACT
persistent state of a hand-placed vanilla guard: `+0x1F4=1`, `+0xfe=1`,
`+0x200=0`, `+0x204=0x40200000`.

Friend/foe parity with the Valorian guard = matrix class 7. Set it through
the engine stance as part of init, OR if you must force it, call
`FUN_0052e420(1,7)` **once, immediately after createnpc_engine returns, and
then NEVER touch +0x1F0/+0x1F4/+0x200/+0xfe again** (a one-shot +0x1F0 write
does not desync anything; +0x1F0 is read only by the matrix). Do **NOT**
call `npc_make_combatant`/the +0x1F4/+0x200 deltas. Do **NOT** set
`+0x1F4 & 0x40000` (inverts friend/foe in `FUN_00423580`). Do **NOT** set
`+0x1F4 = 0x00400000` (fails g5). Do **NOT** write `+0x200` (irrelevant; the
AI-arm is `+0x204`, engine-set).

Concretely, the SDK change is: keep the existing `createnpc_engine` spawn;
**delete the post-spawn `npc_make_combatant` step entirely**; the only
permitted one-shot post-spawn call is `stance(1,7)` for friend/foe
(`FUN_0052e420(1,7)`, ECX=cCreature) and, if desired, the `+0x2b7 &= ~8`
un-stationary byte. Nothing per-frame.

**(b) If a bare `cObjectManager::create` spawn must be used (CONF: HIGH
mechanism; ABIs proven elsewhere in this doc) — replay CreateNPC's tail
EXACTLY, once, in this order, then never again:**

```
om=*(u32*)0x00AD5C40; arr=*(u32*)(om+4); c=*(u32*)(arr+handle*4);
*(u8 *)(c+0x24)  = level;                 // HP scale tier
ECX=c; FUN_0052e420(1, 7);                 // +0x1F0 = 7 (friend/foe; matrix class 7)
*(u32*)(c+0x1F4) = 0x00000001;             // bit0 only (mirrors 0x4839f6; reaches scan via g7 ==0 arm)
*(u32*)(c+0x204) = 0x40200000;             // *** +0x204, the real AI-arm (mirrors 0x483a28) ***
ECX=c; cCreature_WakeUp_0059f580();        // sets +0xfe=1 (precond: +0x200&0x40000==0; +0x200 is 0 -> OK)
*(u8 *)(c+0x2B7) &= ~8;                    // un-stationary (optional)
// +0x200 is left at its ctor 0. DO NOT write +0x200. DO NOT write +0x1F4=0x400000.
// DO NOT set +0x1F4&0x40000. DO NOT re-poke any of these per frame.
```

Key correction vs every prior recipe: the magic `0x40200000` goes to
**`+0x204`**, not +0x200. CreateNPC calls WakeUp *before* the 0x483a28
`+0x204` store, so vanilla `+0x208` is 0 on first wake; the order above
instead matches the desired field END-STATE (`+0x204=0x40200000`). Either
order satisfies WakeUp's precondition since `+0x200` stays 0.
`FUN_0052e420` = `0x0052E420` __thiscall(ECX,int mode,u32 val);
`cCreature_WakeUp` = `0x0059F580` __thiscall(ECX).

**The single residual unknown (one BP, only if (a) still under-aggros):**
HW-exec BP at **`0x00542cbc`** (`cmp word [esi+0xfe],1`) filtered to the
guard's `esi`; log `[esi+0xfc] [esi+0xfe] [esi+0x1f4] [esi+0x150]
[esi+0x152] [esi+0x200] [esi+0x204]` each hit. Expected for a working
guard: `+0xfe==1` on a usable fraction of idle ticks, `+0xfc==0`,
`+0x1F4` bit0=1, `+0x150 not in {3,5}`, `+0x152<100`, `+0x200==0`. If
`+0xfe` is observed stuck 0 across idle ticks, back-trace the writer of
`+0xfe` between hits — but with recipe (a) and no field post-mutation the
engine's own FSM keeps it armed, so this BP should only confirm, not fix.

### 6. Confidence

| Claim | Conf |
|---|---|
| Engine AI-arm magic value is written to **+0x204** (0x483a28 `mov [edi+0x204],0x40200000`), NOT +0x200 — both prior passes wrong on this | HIGH (raw disasm) |
| `+0x200==0` and `+0x200==0x40200000` bit-identical at every AI gate (0x542b4b/0x539779/0x5399cc/0x5444d7) -> +0x200 irrelevant to aggro | HIGH (raw disasm) |
| Vanilla hand-placed guard steady state: `+0x1F4=1`, `+0xfe=1`, `+0x200=0`, `+0x204=0x40200000` (scanned `+0x1F4=0x4000` = combat residue) | HIGH |
| Map NPCs + CreateNPC share `FUN_00461540` init; only `FUN_00482510:0x483a28` writes +0x204; no missing AI-package/behaviour/range field exists | HIGH |
| Proactive mechanism = `+0xfe==1`(WakeUp only) + `+0x1F4` bit0 + `+0x1F4&0x1006dcfb!=0`(bit0 OK) + `+0xfc==0` + `+0x152<100` + `+0x150 not in{3,5}` + `+0x200&0x40000==0`; radius hardcoded 800 | HIGH (raw disasm) |
| Reluctance cause = `+0xfe` consumed each picker pass & re-latched only via WakeUp/FSM; post-poking `+0x1F4` disables lazy-arm (`+0x1F4==0` req) and decays via `00543100` | HIGH (mechanism); exact +0xfe re-latch cadence = the one BP |
| Recipe (a): template `02<T> 04<pos> [02<sub>] 08 12 00`, ZERO `+0x1F4`/`+0x200`/`+0xfe` post-writes; at most one-shot `FUN_0052e420(1,7)` for friend/foe | HIGH |
| `FUN_0052e420(1,val)` sets ONLY +0x1F0 (mode1) — safe one-shot, matrix-only effect | HIGH (disasm) |
| `+0x1F0=7`: attacks monster cluster, ignores hero/ally {1,3,4,7} | HIGH (matrix @0x490A30) |

---

## Companions + dynamic NPC behavior (2026-05-16)

**STATUS: COMPLETE.** Conclusions raw-disasm / decompile only (Ghidra C in
`sdk/re/ghidra/decompiled/`, capstone, base 0x400000, file-off =
VA−0x400000). Proven SDK helpers (`player_state.cpp`): `npc_creature`,
`npc_set_stance`/`FUN_0052e420`, `npc_wake`/`FUN_0059f580`,
`npc_set_faction`(+0x1F4), `npc_teleport`/`FUN_0054d9d0`. Hero handle =
`idx = *(ctx+0x14)` (small player slot 1..16), `ctx = *(0x00AD5C40)`
resolved (`player_state.cpp:157`).

### A. COMPANION (party-follow, fights for hero) — THE decisive mechanic

Two cooperating fields, both on `cCreature`:

| field | type | meaning |
|---|---|---|
| **`+0x1F4 & 0x00000004`** | flag bit in faction word | "I am a SUMMON / PET / COMPANION (owner-substituted)" |
| **`+0x251`** | u32 | my **owner's creature HANDLE** (set to the hero's player-slot handle 1..16) |

This is read in **two independent, single-player-active engine paths**:

**A1. Friend/foe identity-substitution — `FUN_00423480` (the party check).**
`undefined4 FUN_00423480(uint A_handle, uint B_handle)`, `__thiscall`,
`in_ECX` = the per-player slot table (`reb+? — same base FUN_00423580 uses
at `in_ECX-4+h*4`). Decompile `00423480:11-42` (verbatim logic):
```
if (A==B) return 1;
a=getData(A); b=getData(B);                              // 00423480:14-15
if (a && b) {
  if (A>0x10 && isCreature(a) && (*(byte*)(a+0x1F4)&4)) A = *(u32*)(a+0x251); // :17-22
  if (B>0x10 && isCreature(b) && (*(byte*)(b+0x1F4)&4)) B = *(u32*)(b+0x251); // :24-29
  if (A==B) return 1;                                    // :31  -> SAME PARTY
  if (A<0x11 && B<0x11 &&
      (in_ECX[-4+A*4]&0x7fffffff) == (in_ECX[-4+B*4]&0x7fffffff) && !=0)
        return 1;                                        // :34-39 same player group
}
return 0;
```
So a creature with `+0x1F4&4` set and `+0x251 = hero_slot` is, for every
party query, **treated as the hero himself**. `FUN_00423480` is the "same
party / don't fight" predicate; returning 1 ⇒ allied/same party.

**A2. Hostility verdict consumes it — `FUN_00423580` (the matrix
predicate, the ONLY friend/foe test the SP target scanner uses).**
`bool FUN_00423580(uint A,uint B)` returns true ⇒ A hostile to B. Inside,
`00423580:135-148` (the cooperative-block, `DAT_0182ebec` path) calls
`cVar2 = FUN_00423480(param_1,uVar9); return cVar2=='\0';` — same-party ⇒
NOT hostile. Additionally the **hero-redirect** `00423580:45-53,130-133`
fires whenever B is the playable-hero class (`FUN_004266f0(B+0x10)`),
recomputing the verdict through B's `+0x1ec` controller — so anything the
hero is hostile to, an owner-substituted companion inherits.

**A3. Follow / leash AI — `FUN_00542b20` (the SP per-tick picker, NOT
network-gated).** Decompile `00542b20:1121-1161` (verbatim):
```
if ((in_ECX[0x7d] & 0x104) != 0) {              // +0x1F4 & (0x100|0x4)
    pvVar11 = *(void**)(in_ECX + 0x251);        // owner handle
    if (pvVar11 != in_ECX[3] /*self handle*/) {
        o = getData(owner);                      // :1125
        dx = o->+0x1c - self->+0x1c; dy = o->+0x20 - self->+0x20;
        dist = isqrt(dx*dx+dy*dy);               // :1128-1151
        if (dist in [0x95 .. 0xf1]) return;      // :1157 in-leash, idle OK
    }
    in_ECX[0x3f]=2; in_ECX[0xfe]=0;              // :1159 break off / re-path to owner
}
```
⇒ With `+0x1F4&4` and `+0x251=hero`, the engine's own AI **keeps the NPC
leashed to the hero** (paths back when >~241 units), in single-player,
no network, no script. This is the literal "follows the hero" behaviour.

**DECISIVE COMPANION RECIPE (ADD).** Resolve `c = npc_creature(handle)`
(`om=*(0x00AD5C40); arr=*(om+4); c=*(arr+handle*4)` — proven). Get
`hero_slot = *(ctx+0x14)` (`ctx`=resolved `*(0x00AD5C40)`; 1..16).

```
c = npc_creature(npc_handle);                       // proven helper
*(u32*)(c + 0x251) = (u32)hero_slot;                // owner = hero player slot
u32 f; safe_read(c+0x1F4,&f);
*(u32*)(c + 0x1F4) = (f | 0x4 | 0x1) & ~0x40000u;   // +summon bit, +awake bit0,
                                                    //   clear 0x40000 peaceful
ECX=c; FUN_0052e420(1, 7);                           // +0x1F0 = 7 ally cluster
                                                    //   (matrix-correct vs hero;
                                                    //    A2 redirect also covers it)
*(u8*)(c + 0x24) = hero_level;   // optional: scale (npc_set_stance HP path)
ECX=c; FUN_0059f580();                               // WakeUp -> +0xfe=1 (precond
                                                    //   +0x200&0x40000==0 holds: 0)
*(u8*)(c + 0x2B7) &= ~8;                             // un-stationary (optional)
```
ABIs (all already exercised in this project): `FUN_0052e420` =
`0x0052E420 __thiscall(ECX,int mode,u32 val)` (decompile: mode 1 ⇒
`*(c+0x1F0)=val`, sole effect — safe); `FUN_0059f580` =
`0x0059F580 __thiscall(ECX)`; field writes are plain stores (SEH-guard).
**CONF: HIGH** for the field semantics (A1 decompile `00423480:17-39`,
A3 decompile `00542b20:1121-1161`, both SP-active) and the write ABIs;
**MED** that `0x1F4&0x104` vs `0x1F4&0x4` alone is sufficient for the
follow leash without also setting bit `0x100` — A3 tests `& 0x104` (bit2
OR bit8); bit2 (`0x4`) alone satisfies it, but a vanilla pet may also
carry `0x100` (see BP below).

### B. DISMISS (exact inverse) — stop follow, restore independent AI

```
c = npc_creature(npc_handle);
u32 f; safe_read(c+0x1F4,&f);
*(u32*)(c + 0x1F4) = f & ~0x4u;     // clear summon/owner-substitution bit
*(u32*)(c + 0x251) = 0;             // drop owner handle (also: <1 path in
                                    //   FUN_00423580:104/125 reverts to matrix)
ECX=c; FUN_0052e420(1, 3);          // independent matrix class (3 = neutral
                                    //   town-defender; or per C-1 below)
// leave in world as-is, OR despawn via C-2.
```
Clearing `0x4` makes `FUN_00423480` stop substituting (`:20,:27` guard
fails) ⇒ the NPC is judged on its own `+0x1F0` again, and the
`FUN_00542b20:1121` leash branch (`& 0x104`) no longer fires ⇒ it stops
following. **CONF: HIGH** (pure inverse of the proven A-path guards).

### C. Per-action behavior toolkit (already-spawned NPC, by handle)

All resolve `c = npc_creature(handle)` first. Engine-handler entries are
the `FUN_00475680` switch targets (jump table `0x004784c0[tag*4]`); their
TLV-payload ABI (`__thiscall`, ECX=`reb+0x00AACF80`, arg1=record buffer,
arg2=unused — `ret 8`) is the SAME proven `createnpc_engine` ABI. Where a
direct field/leaf call is safer than synthesizing a record, that is given.

| # | action | mechanism (VA / ABI) | args | conf | BP |
|---|---|---|---|---|---|
| 1 | **disposition** hostile/ally/neutral | **`FUN_0052e420` `0x0052E420` `__thiscall(ECX=c,int mode,u32 val)`** mode=1 ⇒ `*(c+0x1F0)=val` (decompile `0052e420:13-15`, sole effect) | `val`: **hostile-to-hero=2** (M[2][1]=0 attack); **ally=7** (or 3; M[7][1]=1 ignore, attacks monsters); **neutral/immune=13** (row/col all 0). Matrix @0x00890A30, **0=attack/1=ignore** | HIGH | none (field-only) |
| 1b | make change take effect mid-game | `FUN_0052e420(1,val)` then re-aggro: `*(u16*)(c+0xfe)` consumed each picker pass — re-latch via **`FUN_0059f580` `0x0059F580 __thiscall(ECX=c)`** (sets `+0xfe=1`; precond `(c+0x200&0x40000)==0 && c+0xfc==0 && c+0xfe==0`). Picker `FUN_00542b20` re-reads `+0x1F0` live next idle tick (`+0xfc==0`) | — | HIGH | `0x00542cbc` (`cmp word[esi+0xfe],1`) once if it won't re-aggro |
| 2 | **despawn** (DelNPC 0x37) | handler **`FUN_00497f80`** (jump tbl `0x004784c0+0x37*4`). Clean-destroy leaf it calls: **`cObjectManager_destroy_005fbdb0` `0x005FBDB0 __cdecl(handle,1,0,1)`** (`00497f80:107`) | `(npc_handle,1,0,1)` | HIGH leaf; MED that bare destroy needs no pre-state | BP `0x004981??` (the `:107` call) once to confirm no list-unlink needed |
| 3 | **walk to point** (NPC_Goto 0x48) | SP path of handler **`FUN_0049e210`**: builds order struct `{vtbl=&PTR_FUN_0089095c, +0x10=mode(iStack_118), X=uStack_13c, Y=local_138, sector=unaff_EBX}` and calls **creature `vtbl[0x18]`**: `(**(code**)(*c+0x18))(&order)` (`0049e210:147`). MP path: append ring elem at `c+0x588/+0x58c` stride 0x44, `elem[0]=1`, `+0x40=X +0x3c=Y +0x38=sector` (`0049e210:171-175`) | order: X,Y (KompassPos via field-4 resolver / pre-seed `ctx+0xa860/64/68`), sector, mode | MED-HIGH (struct shape from `0049e210:129-147`) | BP `0x0049e210` ~`:147` to capture exact order-struct field offsets + vtbl[0x18] index live |
| 3-alt | walk via ring (no vtbl) | append to `c+0x588`(begin)/`c+0x58c`(end), stride **0x44**: zero 0x44 bytes, `*(u32*)(e+0)=1`(MoveTo), `+0x40=X, +0x3c=Y, +0x38=sector`; grow with `FUN_004be490`/`FUN_004b9900` | as above | MED | BP `0049e210` MP branch `:164-175` for the grow-helper arg convention |
| 3-safest | reposition only | **`FUN_0054d9d0`** via proven `npc_teleport(handle,kx,ky)` (no pathing, instant) | kx,ky | HIGH (proven) | none |
| 4 | **morph** (Morph 0x4f) | handler **`FUN_004a15a0`**. NOT a generic type-swap: it only acts when target is the **playable-hero class** (`FUN_004266f0(t+0x10)`) AND `t+0x200&8` AND `t+0x1ec!=0` (`004a15a0:124-126`); then detaches Granny seq, resets `+0x2b7`/`piVar3[0x80]`, re-registers sector (`FUN_005fee50`), kernel event `&PTR_FUN_00890874`. Core swap leaf = **`FUN_00464170(target_handle, new_handle/type)`** (`004a15a0:180`) | target handle, new model/handle | LOW-MED (hero-gated; not a free NPC reskin) | BP `0x004a15a0` `:124` & `:180` — confirm gate + `FUN_00464170` arg meaning before any use |
| 5 | **play anim** (PlayAnim 0x5b) | handler **`FUN_004a2550`**. SP path: order struct `{vtbl=&PTR_FUN_0089095c,...}` → creature `vtbl[0x18]` (`004a2550:142`). MP/queued path: append ring elem at `c+0x588/+0x58c` stride 0x44, `elem[0]=3`(PlayAnim), `+0x40 = anim id (unaff_EBP, field 0xb)`, `+0x3c = mode(local_150/local_158, field 1 second i32)` (`004a2550:153-168`) | anim id (field 0xb), mode | MED-HIGH (ring elem `004a2550:161-168`) | BP `0x004a2550` `:161` once to confirm the elem field map (type=3, +0x40=id) on a live creature |

Notes: ring (`+0x588/+0x58c`, stride 0x44) is the single uniform hook for
3/5 from a DLL — `elem[0]` = command type (**1**=MoveTo, **3**=PlayAnim,
**9**=Teleport, **0xe**=Sound — cross-confirmed `triggers_dialog_move.md`).
The vtbl-call SP paths are lower-risk only if vtbl index 0x18 is verified
live (BP). Despawn (C-2) is the proven clean removal; for "dismiss then
remove", do B then C-2.

### Proposed SDK API mapping

| Lua | implementation (host C++, all SEH-guarded; ABIs above) |
|---|---|
| `o:make_companion()` | A-recipe: `+0x251=hero_slot`; `+0x1F4 \|= 0x5` & `~0x40000`; `FUN_0052e420(1,7)`; `FUN_0059f580`; `+0x2B7 &= ~8` |
| `o:dismiss()` | B-recipe: `+0x1F4 &= ~0x4`; `+0x251=0`; `FUN_0052e420(1,3)` |
| `o:despawn()` | C-2: `cObjectManager_destroy_005fbdb0(handle,1,0,1)` (`0x005FBDB0`) |
| `o:set_disposition(d)` | C-1: `FUN_0052e420(1, {hostile=2, ally=7, neutral=13}[d])` then C-1b `FUN_0059f580` to re-aggro |
| `o:walk_to(kx,ky)` | C-3: prefer `npc_teleport` (HIGH); or ring elem type 1 `{+0x40=kx,+0x3c=ky,+0x38=sector}` after one BP confirm |
| `o:morph(type)` | C-4: GATED (hero-class only) — do NOT expose generic until BP at `004a15a0:124/:180` clarifies `FUN_00464170` ABI |
| `o:play_anim(id)` | C-5: ring elem type 3 `{+0x40=id}` (BP `004a2550:161` to confirm once) |

### Residual MED items + the single cheapest BP that closes each

- **Companion follow** (MED): `FUN_00542b20:1121` tests `+0x1F4 & 0x104`;
  bit `0x4` alone satisfies it, but a vanilla pet may also set `0x100`.
  **Cheapest BP: HW-exec `0x00542cbc`** (already the project's recipe BP)
  filtered to the companion's `esi`; also log `[esi+0x1f4]`,
  `[esi+0x251]` — confirms the leash branch is taken and bit pattern.
- **DelNPC pre-state** (MED): BP the `cObjectManager_destroy_005fbdb0`
  call at `00497f80:107` once — confirms a raw handle destroy needs no
  prior list unlink.
- **NPC_Goto / PlayAnim ring vs vtbl** (MED-HIGH): one BP at
  `0x0049e210:147` (SP, vtbl[0x18] + order struct) OR `:171` (ring) pins
  the order-struct offsets / grow-helper convention; same shape reused by
  `004a2550` PlayAnim — a single BP closes both.
- **Morph** (LOW-MED): hero-gated; needs BP `004a15a0:124` & `:180`
  before any exposure.
