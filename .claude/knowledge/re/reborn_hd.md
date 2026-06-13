# ReBorn HD / 2.29 — Resolution & Fullscreen Method (implementation spec)

Sources read: `Sacred-Patch Source.txt`, `Patcher Source.pb`, ReBorn changelist
`.docx` (extracted), `SacredReborn config.exe` strings, **`SacredReborn.exe`
disassembly (capstone, base 0x400000)**. Confidence per claim.

## TL;DR — it is TRUE HD, driven by a config file (HIGH)

ReBorn renders the engine at the user's W×H. It is **not** a Blt stretch and
**not** a ChangeDisplaySettings change. Three facts, all confirmed in
`SacredReborn.exe`:

1. The 2.29 patch (`Sacred-Patch Source.txt`, all 7 "Änderungen") contains **zero
   resolution code** — Global.res/HD-load, cheat removal, chat-crash, balance.bin
   only. `Patcher Source.pb` is a generic binary-diff applier (no offsets). The HD
   work is 100% ReBorn's, layered on top. (CONFIRMED)
2. `SacredReborn config.exe` keys (mangled-symbol + string scan): `SR_HD_ENABLE`,
   `SR_HD_WIDTH`, `SR_HD_HEIGHT`, `SR_UPSCALING`, plus reused `FULLSCREEN`. Built
   `D:\Prog projects\ReBorn_HD_config_2`. (CONFIRMED)
3. changelist_total.txt §"ALTERNATIVE CONFIGURATION" / "NOTE ON HD SETTINGS":
   all keys live in **`Settings.cfg`** (game folder, `KEY : VALUE`, English).
   Max 2048×2048. "If your monitor does not support the resolution → *Error
   Initializing DirectX* in fullscreen; solved by windowed mode." That error is
   a DirectDraw/D3D `SetDisplayMode/CreateDevice` failure ⇒ the chosen W×H is
   pushed into the **real DirectDraw backbuffer**, i.e. true HD. (CONFIRMED)

## Config schema (HIGH — write these to drive resolution)

`Settings.cfg`, one per line, exact tokens, value space-padded `KEY : VALUE`:
```
SR_HD_ENABLE : 1      ; 1=custom resolution on, 0=off
SR_HD_WIDTH  : 1366   ; game render width
SR_HD_HEIGHT : 768    ; game render height
SR_UPSCALING : 0      ; texture-trembling fix (test); leave 0
FULLSCREEN   : 1      ; 1=true fullscreen, 0=windowed (reuses engine key)
```
ReBorn config also writes these; they are the single source of truth. The
existing `Settings.cfg` in the game folder currently has `FULLSCREEN : 0` and
**no SR_HD_\* lines** — add them.

## How ReBorn applies it inside the EXE (HIGH, with caveat)

In `SacredReborn.exe` the original Sacred `GetSystemMetrics(SM_C?SCREEN)` window
sizing is **replaced by two globals**:

- **`0x00A1AD20` = window/back-buffer WIDTH**, **`0x00A1AD24` = HEIGHT**.
- Only reads, all at the single `CreateWindowExA` site (`call [0x0088E3A4]` @
  **0x00813331**): class-init `0x008132AC/0x008132A2`; WS_POPUP branch pushes
  `[0xA1AD20]/[0xA1AD24]` @0x008132EC; windowed branch @0x00813318. Style is
  `0x90000000` (WS_POPUP|WS_VISIBLE), X=Y=0. (CONFIRMED via xref: the ONLY 3
  code refs to 0xA1AD20/24 are this site.)
- `ChangeDisplaySettingsA` (`call [0x0088E314]`) sites: **0x00815763** (mode set)
  and 0x00816795 (restore, NULL,0). The DEVMODE at 0x00815763 is **still
  hardcoded `dmPelsWidth=0x400(1024)`, `dmPelsHeight=0x300(768)`, dmFields=
  0x1C0000** (bytes `C7 84 24 F8.. 00 04 00 00 / C7 84 24 FC.. 00 03 00 00`).
  ⇒ ReBorn did **not** change the ChangeDisplaySettings path. (CONFIRMED)

Conclusion: ReBorn drives HD by (a) writing `0xA1AD20/24` from
`SR_HD_WIDTH/HEIGHT` at startup so the window is created at W×H, and (b)
feeding the same W×H into the **DirectDraw `SetDisplayMode`/back-buffer create**
(plus a proportional camera-zoom tweak for >1366×768, per docx §"NOTE ON HD").
The 0xA1AD20/24 globals are uninitialised in .data; ReBorn's appended C++
config-apply code writes them (same parser family as the `FULLSCREEN` reader at
**0x0081B400**, which `repne scasb`+strcmp's the cfg token then stores the
parsed value).

**CAVEAT — VA mismatch:** these VAs are *ReBorn's* layout (un-Steam'd 2.28
base). Steam `Sacred.exe` is **DRM-packed** (`.bind` section; on-disk .text is
encrypted — diffing on disk is impossible, confirmed). The user's prior RE VAs
(CreateWindowExA @0x00813C81, CDS @0x008160B4) are the *Steam-decrypted-runtime*
addresses and **do not equal** ReBorn's (0x00813331 / 0x00815763). ReBorn and
Steam are different builds; do not blindly copy ReBorn VAs into the Steam target.

## Recipe for our ijl15.dll (against Steam Sacred.exe)

We cannot reuse ReBorn's binary (different build). Replicate the *method* on the
Steam image, post-decrypt, before EP / before CreateWindowExA at 0x00813C81:

1. **Read** `SR_HD_WIDTH/HEIGHT/ENABLE` from `sdk.ini` (or Settings.cfg) →
   `W,H,en`.
2. **Window size:** we already IAT-hook `CreateWindowExA`. When `en`, in the
   hook force `nWidth=W, nHeight=H, X=Y=0, style=WS_POPUP|WS_VISIBLE` and set
   `g_sacred_hwnd` (use the class/name=="Sacred" match from resolution.md §4 —
   that gate is the real fix; do not gate on hInstance). No .text patch needed.
3. **True HD render (the load-bearing part):** Sacred's renderer is DirectDraw
   (`ddraw_hooks.cpp` already present). Hook `IDirectDraw*::SetDisplayMode` and
   the primary/back-buffer `CreateSurface` (`DDSURFACEDESC.dwWidth/dwHeight`):
   when `en`, rewrite the requested mode/surface dims to **W×H** instead of the
   engine's 1024×768 / 800×600. This is exactly ReBorn's mechanism (back-buffer
   at W×H ⇒ the "Error Initializing DirectX" symptom when the GPU rejects W×H).
   Keep our existing Blt-destRect→client-rect rewrite as the scaler for the UI
   that still assumes the small primary; with the back-buffer now W×H the image
   is rendered, not stretched.
4. **Fullscreen / desktop:** Steam build's own fullscreen CDS @0x008160B4 is
   hardcoded 1024×768 — keep `swallow_displaymode=1` to drop it. For native
   fullscreen at W×H, call `ChangeDisplaySettingsExA(NULL,&dm,NULL,
   CDS_FULLSCREEN,NULL)` with `dmPelsWidth=W,dmPelsHeight=H,
   dmFields=DM_PELSWIDTH|DM_PELSHEIGHT(|DM_BITSPERPEL from GFX32)` at DLL attach
   **before** 0x00813C81 runs (prereq when W/H > current desktop), so the
   GetSystemMetrics/window path covers the full screen; restore on detach. If
   the mode is unsupported, fall back to windowed (matches docx guidance).
5. No registry. The only persistent store is `Settings.cfg` keys above; writing
   them is harmless and is what ReBorn's own config.exe does.

## Quest banner+sound (bonus)

Not encountered in the resolution-relevant code paths examined; see existing
`scratch/quest_fanfare.md` — no new data here.
