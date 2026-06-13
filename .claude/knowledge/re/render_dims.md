# Sacred (Steam 2.0.2.28) — Engine Render-Dimension Spec

Binary: `Sacred_decrypted.exe`, base 0x00400000, no ASLR. VAs verified via Ghidra
decompile + capstone byte-scan. Confidence per claim.

## TL;DR — there is NO static A1AD20-style global in the Steam build (HIGH)

Unlike ReBorn (which has writable .data globals 0xA1AD20/24), the Steam build
derives the engine render extent at init from **two hardcoded immediates at the
display-init call site**, looked up against the DirectDraw-enumerated mode list,
then cached in the **dxDriver7 object** at runtime offsets `+0x1c`(height) /
`+0x20`(width). Those object fields ARE the "engine render width/height" — they
drive SetDisplayMode, the primary/back/Z surfaces, the viewport rects and the
window placement. Patch the immediates (static) and/or the object fields
(runtime). (HIGH — full call chain decompiled.)

## The write site (value source) — HARDCODED 1024x768 (HIGH)

`cTextureLoader_Instance_00815f70` builds the display. At **VA 0x00816c6f**:
```
00816c6f  68 00 03 00 00   push 0x300         ; HEIGHT = 768  -> FUN_00644130 param_2
00816c74  68 00 04 00 00   push 0x400         ; WIDTH  = 1024 -> FUN_00644130 param_1
00816c79  e8 ..            call FUN_00644130  ; mode-table lookup, ebx=bpp(0x10/0x20)
00816c88  8b cf            mov ecx, edi       ; edi = new dxDisplay (operator_new 0x8b2c)
00816c8a  e8 ..            call dxDisplay_dxDisplay_00644830  ; param_2 = matched mode ptr
```
File offsets (no ASLR, raw == VA-0x400000): `push 0x300` @file 0x416c6f, the
`00 03 00 00` literal at file **0x416c70**; `push 0x400` @file 0x416c74, literal
`00 04 00 00` at file **0x416c75**. These are the ONLY producers (FUN_00644130
has exactly 1 xref; dxDisplay ctor has exactly 1 xref @0x00816c8a). (HIGH)

`FUN_00644130(w,h,bpp,flag)` scans the device's enumerated DDraw mode list
(stride 0x84, count at `dev+0x8894`; entry `+0x520`=height u16, `+0x524`=width
u16, `+0x598`=bpp) and returns the matching entry, else **0**. Entry ptr → 
`dxDisplay::dxDisplay` → `cDxDriver7_setDirectDrawMode_id_00645260`, which copies
0x21 dwords from the entry into `dxDriver+0x14..`, so **`dxDriver+0x20`=render
WIDTH (u16), `dxDriver+0x1c`=render HEIGHT (u16)**. (HIGH)

## The read sites (what consumes the dims) — all in setDirectDrawMode 0x00645260 (HIGH)

`[dxDriver+0x20]`/`[+0x1c]` (W/H) are read for, all @0x00645260:
- `SetDisplayMode(w,h,bpp)` (fullscreen branch) ~0x006452f? (vtbl+0x54).
- Primary surface: `dx3DDriver_createSurface_00644ab0(3, w, h,...)` and the 3D
  back/offscreen `(2, param2.w, param2.h)`. `createSurface` puts width in
  DDSURFACEDESC.dwWidth, height in dwHeight (offsets local_e0[2]/[3]).
- Z-buffer: `dxDriver7_createZBuffer_00644ed0(..., w, h)`.
- Window placement: `SetWindowPos(hwnd,..,w,h,..)`; client/viewport rects at
  `dxDriver+0x1dc..0x1f8` (front/back rect = 0,0,w,h) — this is the world
  viewport extent. There is **no separate projection-dim global**; the
  projection/aspect is derived from these surface/viewport dims by the D3D
  device, not from a distinct cached constant. (HIGH)

No independent 800x600 logical-UI dimension global was found in the render path;
UI/HUD is drawn into the same primary and is currently rescaled by the existing
Blt-destRect hook. (MED — UI plane not exhaustively traced; flag below.)

## Coupled constants / aspect (MED)

The engine does not cache an aspect ratio or projection matrix keyed off a
separate width/height global; D3D derives the frustum from the viewport set
from `+0x1c/+0x20`. So changing W/H propagates to projection automatically.
ReBorn's "proportional camera-zoom tweak for >1366x768" is a *gameplay* zoom
preference (isometric camera distance), NOT a correctness requirement — the
image is geometrically correct without it; only the visible map area grows.
HUD/menu art authored for 4:3 will letterbox/stretch unless separately scaled
(your existing UI Blt rescale already handles this). (MED — confirm visually.)

## DLL recipe (Steam VAs, values, timing)

Two equivalent approaches; do BOTH the static patch and keep the ddraw hook.

1. Static .text patch of the hardcoded dims (simplest, deterministic):
   - At VA **0x00816c75** write `WIDTH` as u32 LE (replaces 0x00000400).
   - At VA **0x00816c70** write `HEIGHT` as u32 LE (replaces 0x00000300).
     ⚠️ FIXED 2026-06-13: this previously said 0x816c71 — OFF BY ONE. `push 0x300`
     is `68 00 03 00 00` @0x816c6f, so the height imm32 starts at **0x816c70**.
     Reading 0x816c71 = 0x68000003 (the 0x300 tail + next `push` opcode) → the
     1024/768 guard in hooks.cpp failed and HD silently did nothing. Fixed in
     hooks.cpp hd_patch_render_dims_once(), build 100e3fd8.
   - Type: u32 each (the `push imm32` operand). Apply **after .text decrypt,
     before `cTextureLoader_Instance_00815f70` runs** (i.e. at/just after your
     post-decrypt patch pass, well before display init). 0x401000-region is
     RW after your existing decrypt; `VirtualProtect PAGE_EXECUTE_READWRITE`
     around the 8 bytes, write, restore.
   - Also leave bpp push (ebx=0x10/0x20) unchanged.

2. Ensure the W×H mode is offered to FUN_00644130 (REQUIRED — load-bearing):
   `FUN_00644130` returns 0 if no enumerated DDraw mode equals (W,H,bpp);
   dxDisplay then gets param_2=0 and init fails ("Error initializing
   DirectX" — exactly ReBorn's documented failure). In your DDraw7 wrapper,
   intercept `IDirectDraw7::EnumDisplayModes` and inject/keep a mode entry
   with dwWidth=W, dwHeight=H, matching bpp (16 and 32) so the lookup
   succeeds. (Your existing CreateSurface/SetDisplayMode rewrite then
   produces real W×H surfaces.) This is the true-HD enabler; without it the
   static patch yields a failed lookup.

3. Runtime belt-and-braces (optional): after dxDisplay ctor, the dims live at
   `*(u16*)(dxDriver+0x20)` / `+0x1c`. `dxDriver` = `*(void**)0x00CDCA1C`
   (`DAT_00cdca1c` = dxDisplay) — the dxDriver7 is the dxDisplay subobject;
   resolve via the dxDisplay this and offsets above only if you add a runtime
   BP to confirm the exact dxDriver base (see flag).

Timing summary: patch (1) at post-decrypt; hook (2) lives for whole process;
(1)+(2) together make the engine create and render the world at W×H. No
registry/cfg key exists (consistent with resolution.md).

## Needs a runtime breakpoint (flag)

- BP at **0x00816c79** (the `call FUN_00644130`): inspect EAX on return — if 0,
  the W×H mode was not enumerated → confirms hook (2) is mandatory and that
  the mode-injection is working once non-zero.
- BP at **0x00816c8a** (`call dxDisplay ctor`), then after `setDirectDrawMode`
  returns, dump `[dxDisplay_this+0x14 .. +0x24]` to capture the exact dxDriver7
  sub-object base and confirm `+0x20`=W,`+0x1c`=H at runtime (validates the
  optional runtime poke #3). dxDisplay_this = EDI at 0x00816c88 (= global
  `0x00CDCA1C` after `mov [0xcdca1c],eax` @0x00816c9d).
- Confidence the static patch + EnumDisplayModes injection yields true HD:
  HIGH (mirrors ReBorn's confirmed mechanism; same engine lineage). UI/HUD
  scaling correctness: MED — verify on screen, adjust existing UI Blt rescale.
