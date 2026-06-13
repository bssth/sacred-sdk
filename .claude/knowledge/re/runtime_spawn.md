# Runtime "spawn creature at world tile (X,Y)" — recipe

Sacred Gold, Steam build 2.0.2.28, `Sacred_decrypted.exe`, imagebase 0x00400000, no ASLR.

## TL;DR

The sector-map / cWorld singleton is **`*(void**)0x00AD3560`**. It is a *cache* of
`(*(void**)0x00AD5C40)[0]` (i.e. `cObjectManager + 0x00`). To spawn at a tile:

```c
struct Pos { uint16_t sector; uint32_t X; uint32_t Y; uint8_t level; }; // 13 bytes, use 16
Pos buf;
// 1) build position (sector=0 -> needs resolve)
FUN_006224b0(/*ECX=*/&buf, /*sector*/0, /*X*/X, /*Y*/Y, /*level*/0);   // __thiscall, ret 0x10
// 2) get cWorld (with the engine's lazy-init fallback)
void* cWorld = *(void**)0x00AD3560;
if (!cWorld) { cWorld = (*(void***)0x00AD5C40)[0]; *(void**)0x00AD3560 = cWorld; }
// 3) resolve sector  (returns 0/false if (X,Y) outside the map -> abort)
bool ok = FUN_00635c40(/*ECX=*/cWorld, /*ushort* */ &buf);   // __thiscall, ret 4
if (!ok || buf.sector == 0) { /* off-map: do not spawn */ }
// 4) create the creature
void* objmgr = *(void**)0x00AD5C40;
int handle = cObjectManager_create_005fba40(/*ECX=*/objmgr, type, &buf, 0, 1, 0);
```

After step 3 `buf.sector` is the real sector id and `cObjectManager_create_005fba40`
parks the creature at the correct tile instead of the KompassPos(2850,3860) default.

## cWorld singleton — exact source & how derived

`mov ecx,[0xAD3560]` is the ECX used by **every** verified resolver call:

* `FUN_0054d9d0` (hero teleport): `0x0054DC53 mov ecx,[0xAD3560]` then `call 0x635d10`;
  `0x0054DC69 mov ecx,[0xAD3560]` then `call 0x6354d0`.
* generic teleport/spawn at `0x004FAE27 mov ecx,[0xAD3560]` then `call 0x6354d0`.
* `FUN_004a2b40` (creature spawn/teleport) at `0x004A3E11`:
  `mov eax,[0xAD5C40]; mov ecx,[eax]; call 0x635C40` — i.e. ECX = `cObjectManager[0]`.

The lazy-init fallback proving `0xAD3560 == cObjectManager[0]` appears verbatim at
`0x004F21AC` and `0x004FADF6`:

```
mov eax,[0xAD3560]      ; cached cWorld
test eax,eax            ; (or: cmp eax,ebx)
jne  use
mov edx,[0xAD5C40]      ; cObjectManager singleton
mov eax,[edx]           ; cObjectManager->field_0  == the cWorld
mov [0xAD3560],eax      ; populate cache
use:
mov ecx,[0xAD3560]
```

Confidence: **high** (identical pattern at 3 independent sites + every resolver
caller uses one of these two equivalent forms).

Recommendation: use `0x00AD3560` with the null fallback shown above (mirrors the
engine; safe even if the cache is not yet populated).

## cWorld layout (verified against decompiled bodies)

* **+0x240** : `int** sectorPtrTable` — base of an array of pointers, indexed by
  sector id. Guard used by resolver: `sector < (cWorld[0x244]-cWorld[0x240])>>2`
  AND `((int*)cWorld[0x240])[sector] != 0`. (= "is this sector id currently
  loaded/valid".)
* **+0x244** : `int** sectorPtrTableEnd` — one-past-end of that array
  (`(end-begin)>>2` = sector count).
* **+0x284** : `uint16_t sectorGrid[128*128]` — the tile→sector lookup.
  Index = `(sx*0x80 + sy)` where `sx = X>>6`, `sy = Y>>6` (note: X drives the
  *outer* /0x80 stride, Y the inner). Bounds: `sx<0x80 && sy<0x80 && X>=0 && Y>=0`,
  else result 0. Source: `FUN_00635c40` line 25, `FUN_00635d10` lines 20/36
  (identical expression).
* **+0x44 / +0x48** : current-sector origin in tile units (`FUN_006229e0`:
  `[+0x44] <= X < [+0x44]+0x40`, `[+0x48] <= Y < [+0x48]+0x40`) — the 64-tile
  (0x40) "are we still in the cached sector" fast path. Not needed for spawn.

Confidence: **high** for +0x240/+0x244/+0x284 (two independent decompiled
functions agree). +0x44/+0x48 medium (inferred from FUN_006229e0 only).

## FUN_006224b0 — exact prototype (re-verified)

`0x006224B0`, `__thiscall`, `ret 0x10` (callee pops 4 dword args):

```
ECX = &buf
[esp+4]  uint16  sector   (pass 0)
[esp+8]  uint32  X        (KompassPos tile, 0..~6300)
[esp+0xc] uint32 Y
[esp+0x10] uint8  level
writes: buf[0]=u16 sector, buf[4]=u32 X, buf[8]=u32 Y, buf[0xC]=u8 level
```

Engine usage (`FUN_0054d9d0` @0x54DC45): `push Y; push 0(level); push X;
push 0(sector); lea ecx,[&buf]; call 0x6224b0`.

## FUN_00635c40 vs FUN_00635d10

Both resolve identically (read X=buf+4, Y=buf+8, `sx=X>>6, sy=Y>>6`,
`buf.sector = cWorld[0x284 + (sx*0x80+sy)*2]`) and both return
`buf.sector != 0`. Differences:

* **FUN_00635c40** (used by the creature-spawn path FUN_004a2b40): if
  `buf.sector` already non-zero AND valid in the +0x240 table AND
  `FUN_006229e0` says still inside that sector's 64-tile box → keep it; else
  recompute from the grid. **Use this one** — it is exactly what the
  creature path uses.
* **FUN_00635d10** (used by hero teleport): same, but the "already valid"
  fast-path uses `FUN_00622a40`. Functionally equivalent for a fresh buffer
  with `sector==0` (both fall straight through to the grid lookup).

For a freshly-built buffer (`sector==0`) both take the grid-lookup branch and
give the same answer. Recipe uses **FUN_00635c40** to mirror the creature path.

Guard: if it returns 0 / `buf.sector==0`, the (X,Y) is outside the loaded
128x128 grid or that sector isn't streamed in — **do not call create**, the
creature would again be parked at the default.

## Minimal sequence is sufficient — no extra init needed

The creature path `FUN_004a2b40` does literally only:
`FUN_006224b0(&buf,...)` (via FUN_006224e0 float variant) → `mov ecx,[cWorld]`
→ `FUN_00635c40(ecx,&buf)` → object create. No `FUN_00635d10` pre-init, no
manual +0x240/+0x244 writes (those are read-only validity tables the engine
maintains while streaming). So the 4-step recipe above is complete.

`cWorld_getParentObject_006354d0(cWorld, &buf, levelArg)` is the *3D parent /
level-object* lookup used additionally by the hero teleport (FUN_0054d9d0)
when `param_3 != 0`. It is **not** required just to place a creature on the
ground at a tile — `cObjectManager_create_005fba40` only needs a buffer whose
`sector` is resolved. Call it only if you later need vertical/level placement.

## Open items needing a runtime breakpoint

1. **Confirm `0x00AD3560` is non-null during normal gameplay** (in-world, not
   menu). BP `0x0054DC53`; inspect `[0xAD3560]` and `[[0xAD5C40]+0]` — expect
   equal & non-null. (The fallback handles null anyway.)
2. **Confirm grid index order** (`sx*0x80+sy` vs `sy*0x80+sx`). Decomp clearly
   shows `((X>>6)*0x80 + (Y>>6))*2`; verify by BP at `0x00635C72`-ish (the
   `movzx` from `[ecx+0x284+...]`) with a known hero (X,Y) and reading back
   `buf.sector`, comparing to the hero's actual sector. Medium-risk: the X/Y
   naming in the task (KompassPos) vs engine's internal swap.
3. **type id space for `cObjectManager_create_005fba40`** — unchanged from
   prior work; not part of this task but worth a sanity BP on first spawn.
4. Verify FUN_006224b0 X/Y argument order at runtime (task says X@+4,Y@+8;
   engine push order is `push Y; push level; push X; push sector` confirming
   X first arg, Y last — matches).
