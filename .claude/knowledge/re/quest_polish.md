# Quest polish — Q1 transform / Q2 click re-arm / Q3 row style — 2026-05-16

## Q1 — hero ↔ KompassPos transform (PRIORITY, resolved, HIGH)

The prior "no transform" claim is WRONG. There **is** a fixed affine
scale, proven from the raw disasm of `FUN_004a5980` literal-coord path
(0x4a5d0c–0x4a5d64, identical at the slot-3 path 0x4a5e6b–0x4a5ecf):

```
0x4a5d1c call FUN_006224b0(0, entry+0x10, entry+0x14, 0)  ; copy KompassPos into rec
0x4a5d23 fild [rec+4]            ; KompassPos X (= entry+0x10, integer)
0x4a5d29 fadd dword [0x890670]   ; + A
0x4a5d2f fmul dword [0x890dc0]   ; * B
0x4a5d35 __ftol -> local_1c (X in HERO space)
   ... same for Y (entry+0x14) -> local_18
0x4a5eeb sub local_1c , [hero+0x1c]   ; distance computed in HERO space
```

Constants read from `.rdata` of `Sacred_decrypted.exe` (static, map-
independent — verified by Python):
- `0x890670` = float **0.5**  (A, the +0.5 cell-center offset)
- `0x890dc0` = float **53.66563034057617** (B, cells→hero-world scale)

So hero `cCreature+0x1C/+0x20` IS raw hero-world space (the runtime
~174698/140499). Quest `entry+0x10/+0x14` is KompassPos (cells). They
are **NOT** the same space; convert one side.

**Recipe — compare player to a quest KompassPos marker:**
```
A=0.5 ; B=53.66563034057617
om   = *(uint*)0x00AD5C40                 // cObjectManager (static ptr OK here)
ctx  = *(uint*)0x0182EBE8 ; idx=*(uint*)(ctx+0x14)   // 1..16
hero = *(uint*)( *(uint*)(om+4) + idx*4 )
hx   = *(int*)(hero+0x1C) ; hy=*(int*)(hero+0x20)     // hero-world
// quest marker (entry+0x10/+0x14 = KompassPos kx,ky):
mx = (kx + A)*B ; my = (ky + A)*B          // KompassPos -> hero-world
dist = hypot(hx-mx, hy-my)                 // plain Euclidean, hero-world units
// "arrived" when dist < ~ 1.0*B .. 3.0*B  (1–3 KompassPos cells ≈ 54..160)
```
Inverse (hero→KompassPos, if you prefer): `kx = hx/B - A`.

`FUN_00622620` (matrix `_DAT_00ad7790/94/a0/a4`) and `FUN_00622660`
(inverse, `*_DAT_00ad7788`) are the **screen-pixel rotation only**
(used by FUN_006ccbd0/FUN_006e3f70 for drawing); they are NOT in the
distance path. Those `_DAT_00ad77xx` are runtime-filled (static bytes
are garbage) — irrelevant to the check.

Validation vs runtime: quest9 KompassPos (3201,2473)→hero-world
(171810,132742); player read (174698,140499) ⇒ inverse KompassPos
(3255,2618), ~54/145 cells from Bellevue HQ — i.e. player a screen or
two from the marker at world-load. Order of magnitude exact, transform
confirmed. **Confidence HIGH** (static disasm + constant read +
numeric check). No runtime BP needed for the formula. Optional sanity
BP `0x4a5d35` (dump ST0 pre-/post-, and `[hero+0x1c]`) if you want
byte-perfect proof on your map.

## Q2 — journal-click re-arms slot-1 (resolved, HIGH)

Handler: **`FUN_006b3940` @ 0x006B3940**, `__thiscall`, `ret 0xc`.
- ECX = journal-page UI object (`ebp`).
- arg1 `[esp+4]`→esi = UI message struct; `(*[esi])+4` returns msg
  type; **type 2 = row click/select** (`0x6b3960 cmp eax,2`).
- Clicked-row → entry index: reads selected cell coords
  `u16 [ebp+0x15e]`, `u16 [ebp+0x162]`, indexes page-row array base
  `[ebp + (col)*4 + 0x97c]`, then row record `+0x10` list, element
  `+0xc` = **registry entry index** (loaded into `esi` at 0x6b3c47).
- Re-arm: `0x6b3ca2 push esi ; mov ecx,0xAACF80 ; call 0x49DAB0`
  (= slot-1 writer, sets `cQuestMgr+0x3a0 + C*8 = entry_index`), then
  `call 0x6B1990` (marker refresh). Gated to side quests:
  `+04==100||101` AND `+00!=3` (the `0x6b3c7b..0x6b3ca0` test block).

**Minimal SDK hook options (pick one):**
1. Detour `FUN_006b3940`: after the original returns, if the current
   journal selection resolves to 9512's entry index, call
   `FUN_0049DAB0` (`__thiscall`, ECX=0x00AACF80, arg = 9512's idx) +
   `FUN_006B1990` (ECX=page obj) to re-point the marker. This is a
   general "on quest-log selection" site.
2. Simpler / no UI-state parsing: detour `FUN_0049DAB0` itself — when
   ANY quest row is clicked the engine calls it; if you want 9512 to
   keep the marker, re-assert 9512's idx whenever the arg is some
   other entry (or just always re-arm 9512 on each call). One-line
   hook, no row-decoding.

Recommended: option 2 for robustness (no dependence on +0x15e/+0x162
UI layout). Confidence HIGH (direct disasm of the only two 0x49DAB0
callers; 0x6b3ca8 is the journal one, 0x806e4a is unrelated).

## Q3 — journal ROW secondary vs primary (resolved, HIGH)

Row style is chosen in `FUN_006b07e0` from `entry+0x04` (type) and
`entry+0x00`, NOT from the marker slot:

| field | MAIN/primary | SIDE/secondary |
|---|---|---|
| `+0x04` type | **1 or 2** → icon **0x49a**; eligible for active-quest header **0x499** | **100 (0x64)** → icon **0x49c**  *(or 101/0x65 → 0x49b)* |
| `+0x00` | 3 ⇒ greyed/struck (icon `&~3`); !=3 normal | same semantics |
| `+0x0C` bit0 | filled "current step" bullet (`|0x80`) | same |
| `+0x16C` | page id (= page+1, or 0 for page 0) — gate only | same |

Decisive lines (decompile 110-125 / disasm 0x6b3bb0): the **0x499
"active quest" heading is explicitly suppressed when type==100 or
type==101** (`if (t!=100 && t!=0x65 && idx==FUN_0049da50() && +00!=3)
local_d4=0x499`). That heading + the 0x49a icon are what make a row
look PRIMARY. A side quest uses type 100 → icon 0x49c → no 0x499
header → SECONDARY look. Live log corroborates: vanilla MAIN quest_id=1
`+00:03 +04:0x64`… wait — id=1 dumps `+04=0x64(100)`; id=9 dumps
`+04=02`. So in THIS data id=1 already uses the type-100 icon while id
=9 uses type-2. The visible "primary" emphasis is the **0x499 header**
(only types 1/2 get it) + non-grey `+00`. We currently set
`+00=3,+04=1`: `+00=3` greys the row AND blocks the 0x499 header, but
`+04=1` still selects the **0x49a (main) icon** — that is why 9512's
row still reads primary.

**Set on 9512's entry for a clean SIDE-quest row:**
```
*(u32*)(e+0x00) = 0;     // NOT 3 (3 = greyed/solved, not "side")
*(u32*)(e+0x04) = 100;   // 0x64 -> side icon 0x49c, suppresses 0x499 header
*(u8 *)(e+0x0C) &= ~1;   // open bullet (set |1 when step done)
*(u16*)(e+0x16C)= page;  // unchanged: 0 = first tab else tab+1
// +0x24 must stay non-0 (render gate) — leave as-is
```
(If you specifically want the alt "type 101" side variant use
`+0x04=0x65` → icon 0x49b; 100 is the standard side-quest icon.)

Confidence HIGH (FUN_006b07e0 switch, disasm-confirmed, cross-checked
vs sdk_loaded.log F8 dumps). No BP required; optional visual confirm
after setting `+04=100`.
