# Sacred Resolution / Fullscreen — RE Spec

Binary: `Sacred_decrypted.exe`, base 0x00400000, no ASLR. All VAs verified via
IAT-thunk disassembly (capstone) + Ghidra corpus. Confidence noted per claim.

## 1. Where the window resolution comes from  (CONFIRMED, high)

There is **no resolution config key and no registry resolution store**. EXE has
NO `RESOLUTION`/`ScreenX`/`ScreenY` token; the only reg paths are DirectX/
DirectPlay internals (`Software\Microsoft\Direct3D`, `…\DirectPlay8\…`,
`SOFTWARE\Microsoft\Windows\CurrentVersion`) — none resolution-related.
`Settings.cfg` (only cfg the EXE opens; string @0xA1AE30; **no STEAM_Settings.cfg
string exists** — that file is a launcher artifact) has no W/H keys.

Single `CreateWindowExA` call site: **VA 0x00813C81** (`call [0x008903A4]`).
Window class **and** name are both the literal `"Sacred"` (ptr `[0x00A1CD64]`
= `[0x00A1CD60]` -> 0x009E3110 = "Sacred"). hWndParent = NULL, hInstance = the
engine HINSTANCE (= `GetModuleHandleA(NULL)` = 0x00400000).

Branch selector @0x00813C29 = `FUN_00813A90(cfg,"win")` (cmdline/cfg flag
`"win"`, str @0x0095EFD4):
- **windowed branch @0x00813C59**: nWidth = `GetSystemMetrics(SM_CXSCREEN=0)`,
  nHeight = `GetSystemMetrics(SM_CYSCREEN=1)` (via `[0x008902CC]`), X=0,Y=0,
  **dwStyle = 0x90000000 (WS_POPUP|WS_VISIBLE)**, dwExStyle=0.
- fullscreen branch @0x00813C3C: same style, X=Y=W=H=0.

=> The 1920x1080 is **the Windows desktop primary-monitor size**
(`GetSystemMetrics`). User's panel is bigger but the desktop mode is 1920x1080,
so the window is 1920x1080. There is no in-game way to exceed desktop size here.

## 2. Display path (CONFIRMED, high)

`cTextureLoader_Instance_00815f70` is the display setup. @0x00816035 calls
`FUN_0081C360` = the **FULLSCREEN** cfg reader (builds "FULLSCREEN" str
@0xA1D9C0, looks it up in parsed Settings.cfg). If true: fills a DEVMODEA and
calls `ChangeDisplaySettingsA(&dm,4)` @0x008160B4 with **hardcoded
dmPelsWidth=0x400 (1024), dmPelsHeight=0x300 (768)**, dmBitsPerPel = 0x10/0x20
(GFX32 via FUN_0081C510), dmFields=0x1C0000. Fullscreen mode is **fixed
1024x768** — not config-driven, not 800x600. With active `Settings.cfg`
`FULLSCREEN:0`, this branch is skipped -> windowed popup at desktop size.

Internal render is the small DDraw primary (640/800/1024 per cfg); our
`ddraw_hooks.cpp hook_Blt` already rewrites the primary Blt destRect to
`GetClientRect(g_sacred_hwnd)`. So **bigger window == bigger image, IF
`g_sacred_hwnd` is set** (gated on the CreateWindowExA match firing). (CONFIRMED)

## 3. Sacred community patch method (CONFIRMED, file cited)

`refs/Inoff_Sacred_Patch_Source.zip` -> `2.30 Patch/Patch Source Sacred.exe.txt`,
"Änderung 10: Border für Sacred-Fenster im Fenstermodus". It does NOT change
resolution at all — it only flips the window **style** byte at the (retail-
layout) CreateWindowExA: `PUSH 10CF0000` -> `PUSH 10CA0000`. I.e. the official
"widescreen" trick is purely: run windowed, let the window inherit the full
desktop via GetSystemMetrics, and clear the frame bits. There is no resolution
key or registry value in any community patch. Resolution = set the **Windows
desktop** resolution; the game follows it.

## 4. Concrete SDK recipe (least-fragile)

Root cause of current failure: `looks_like_sacred_main` rejects on
`hinst != g_attach.exe_module`. Sacred's CreateWindowExA *does* pass parent=NULL
and full size, but the hInstance equality is the only failing gate (the hook
logs the line WITHOUT "MAIN-MATCH"). Replace the fragile hInstance test with a
**class/name match**, which is rock-solid here because both are the constant
"Sacred":

In `hooks.cpp looks_like_sacred_main(...)` add the real signal — match when
parent==NULL AND (lpClassName/lpWindowName, when not an atom, equals "Sacred")
AND w>=320,h>=240. Keep hInstance only as a *fallback* OR drop it entirely.
Pass `lpClassName`/`lpWindowName` into the matcher:

```
bool is_sacred = lpClassName && HIWORD(lpClassName) &&
                 _stricmp(lpClassName,"Sacred")==0;
// match if parent==NULL && is_sacred && w>=320 && h>=240
```

This makes `g_sacred_hwnd`/`g_main_overridden` set, which (a) lets the
borderless/size override apply and (b) enables the ddraw stretch.

To run TRUE fullscreen at desktop native resolution, "full scale":

- **Best (no binary edit, no display-mode change):** keep
  `force_borderless=1`, `force_fullscreen=1`. With the fixed matcher, our hook
  rewrites the window to `style=WS_POPUP|WS_VISIBLE`, X=Y=0, W=`SM_CXSCREEN`,
  H=`SM_CYSCREEN` (already the code path at hooks.cpp:119). Window now covers
  the whole primary monitor at the desktop resolution; ddraw_hooks stretches
  Sacred's 800x600 primary to that client rect -> full-scale image.
- **For native panel res > current desktop:** Sacred can only ever be as big
  as the desktop (GetSystemMetrics). To get true native res, raise the desktop
  mode at DLL attach via `ChangeDisplaySettingsExA(NULL,&dm,...,CDS_FULLSCREEN)`
  to the panel's native WxH BEFORE the window is created (DllMain/early hook,
  before 0x00813C81 runs), then let the GetSystemMetrics path pick it up. Keep
  `swallow_displaymode=1` so Sacred's own (1024x768) ChangeDisplaySettingsA at
  0x008160B4 is intercepted and ignored (FULLSCREEN:0 in active cfg means it
  won't fire anyway). Restore desktop on detach.
- No registry/cfg writes needed — there is no resolution key to write.

## Runtime BP to confirm matcher (flag)

If after the class-name fix MAIN-MATCH still doesn't log, set a BP at
**0x00813C81** (CreateWindowExA call) and inspect `[esp]` (exStyle) ..
`[esp+0x14]`=W, `[esp+0x18]`=H, `[esp+0x1C]`=parent, `[esp+0x24]`=hInstance,
and the `[0x00A1CD64]` string, to verify the args our hook sees match. Confidence
the class-name fix works: high (string is a hard constant in .data).
