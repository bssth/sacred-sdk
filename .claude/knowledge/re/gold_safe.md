# gold_safe — stop save corruption (gib on death after raw +0x3EE write)

## Root cause (CONFIRMED, high confidence)

Sacred keeps **two XOR-obfuscated shadow copies** of the protected hero
block. The shadow data lives behind two globals (pointers, page-guarded
via `VirtualProtect`):

- `DAT_0182EE5C` → shadow buffer **A**, key = `k = b|b<<8|b<<16|b<<24` where `b = *(u8*)(hero+0x38)`
- `DAT_0182EE60` → shadow buffer **B**, key = `k<<1`

Layout (both buffers): `shadow[0]=hero+0x3B4 ^ key`, **`shadow[4]=hero+0x3EE ^ key` (GOLD)**, `shadow[8]=(u16)hero+0x3FE ^ key`, then a 0x3B-dword copy of `hero+0x3B0..` (skills/stats/cheat-basis).

A raw `*(u32*)(hero+0x3EE) += amount` desyncs live gold from BOTH shadows.
The next **validator** pass declares "Cheater detected" and runs the
punishment. There is NO single check on death; rather the validator runs
at save AND on a runtime sweep, and the punishment forces the hero into a
destroy/death state (looks like a gib).

### Validator + punishment (the "gib")
- `FUN_00564d60` (runtime integrity sweep; also CalcResults driver). Compare loop @ `0x00564DCB`+; **punishment label `LAB_0056514D` @ 0x0056514D**. Confidence: high.
- `cEngine_save_00619BF0` (save-time validator). Compare @ `0x00619CB0`+; punishment @ **`0x0061A24B`** (`"Hase!"` `0x0094E4CC`, `"...Cheater detected..."` `0x0094E4B0`). Confidence: high.

Punishment writes (identical in both): `cStatsManager(*(hero+0xC))`,
`FUN_004246D0()`, `FUN_005568B0(1)`, `*(u32*)(hero+0x4D8)=0`,
`*(u16*)(hero+0xFC)=9`, `*(u16*)(hero+0xFE)=0`, `*(u16*)(hero+0x150)=6`,
`*(u16*)(hero+0x152)=0`, `DAT_0182EE50=1`, `FUN_0054D760(0x169)`,
`FUN_004E8DA0(1,2)`. That state change = the body-parts explosion. On
mismatch the validator ALSO clobbers the offending live field to `1`
(why a tampered save sometimes shows gold==1).

## The legitimate "commit gold" mechanism (no single addGold primitive)

There is **no exported add/set-gold function**. Loot/vendor/level-up
each mutate `hero+0x3EE` inline then **re-arm both shadows inline** with
this idiom (≈30 sites): `VirtualProtect(EE5C,RW)` → recompute key from
`hero+0x38` → write `shadow=live^key` for A and `^(key<<1)` for B →
`VirtualProtect(EE5C,RO)`. The FunkCode give-gold opcode (`FUN_0048DA40`
case `0xB`, dispatched by `FUN_00472BC0`) is **display-only** — it emits
the cosmetic `0x3EC` event and never touches `0x3EE`. (Confirms the
current SDK event code is fine; only the money write is wrong.)

**Cleanest reusable re-arm routine: `FUN_00564D60`** — `void __thiscall
FUN_00564D60(void *this /*ECX*/, u16 a, char b, char c)`. Called as
`FUN_00564D60((u16)*(hero+0x3FE), 1, 0)` from `FUN_0048DF30` @0x0048E0xx
right after a stat write; it validates THEN re-arms both shadows from
live. ECX = the cCreatureHero (same object as `[0x006D5C40]→+4→+4→+0x3AC`;
verify `*(this+? )` resolves to hero — pass the hero base used for
0x3EE). Confidence: medium-high (needs one runtime BP to confirm ECX).

The pure, side-effect-free re-arm (no validation, no CalcResults) is the
inlined block inside `FUN_0048DF30` at **0x0048E14B–0x0048E220**.

## Minimal safe SDK recipe (decisive)

Do the WHOLE thing on the main thread, post-world-load, hero resolvable.
Replace `sacred_give_hero_gold_silent` with **write-then-re-arm-both-shadows**:

```c
uint8_t  b   = *(uint8_t*)(hero + 0x38);
uint32_t key = b | (b<<8) | (b<<16) | (b<<24);
uint32_t* A  = *(uint32_t**)0x0182EE5C;   // deref the global -> buffer
uint32_t* B  = *(uint32_t**)0x0182EE60;

// 1) mutate live (clamp 0..INT32_MAX as today)
*(uint32_t*)(hero + 0x3EE) += amount;

// 2) re-arm BOTH shadows for the 3 protected scalars (A and B).
DWORD old;
VirtualProtect(A, 0x1000, PAGE_READWRITE, &old);
VirtualProtect(B, 0x1000, PAGE_READWRITE, &old);
uint32_t g  = *(uint32_t*)(hero + 0x3EE);
uint32_t s4 = *(uint32_t*)(hero + 0x3B4);
uint16_t s8 = *(uint16_t*)(hero + 0x3FE);
A[0]=s4^key;  A[1]=g^key;  A[2]=(uint32_t)s8^key;
uint32_t k2 = key<<1;
B[0]=s4^k2;   B[1]=g^k2;   B[2]=(uint32_t)s8^k2;
VirtualProtect(A, 0x1000, 0x201 /*PAGE_READONLY|GUARD as engine uses*/, &old);
VirtualProtect(B, 0x1000, 0x201, &old);
```

This keeps BOTH shadows consistent for gold AND the two co-validated
fields (0x3B4, 0x3FE) we read live, so the validator passes and death no
longer gibs. The skill/stat tail (shadow+0xC.. = hero+0x3B0+) is
unchanged by us, so leaving it as-is is correct (do NOT touch it).

Prefer this inline re-arm over calling `FUN_00564D60` — the engine
function also runs `cCreatureHero_CalcResults_005796A0` and a full
validate, with unverified ECX semantics; the inline write has zero side
effects and exactly mirrors what every vanilla mutator does.

## Verify with one runtime BP (before shipping)
- BP `0x0048E1C9` (shadow A write in FUN_0048DF30). Dump: `[0182EE5C]`,
  `ESI` (hero base it uses), `[ESI+0x3EE]`, `[ESI+0x38]`. Confirm the
  hero base == our `sdk::player::hero_base()` and that A==deref of global.
  If hero base differs, our 0x3EE writes are on the wrong object — switch
  to that base.
- BP `0x0061A24B` (save-time "Cheater detected"). Should NEVER hit after
  the fix; if it does, dump `[ESI+0x3EE]`, `[[0182EE5C]+4]`, `[ESI+0x38]`
  and recompute key — page-guard (0x201) may need re-querying old prot.

## Confidence summary
- Shadow layout / two buffers / XOR keys: **high** (3 independent
  decompiled sites agree: save validator, FUN_00564D60, FUN_0048DF30).
- Gib = validator punishment state-set: **high** (string-anchored).
- Recipe correctness: **high** for gold+0x3B4+0x3FE; **flag**: confirm
  the page-protection constant (engine passes `0x201`; use the value the
  engine restores — capture via the BP above) and that `[0182EE5C]` is a
  pointer-to-buffer (it is: written only in `cTextureLoader_Instance_00815F70`).
