# Real patches landed

Two of the seven 2.29-rosetta patches (docs/12-patch-229-rosetta.md) are
implemented as runtime DLL hooks. Build target: `ijl15.dll`. No on-disk EXE
modification.

## Patch 1 — `global.res` from disk

**Implementation**: `sdk/patches.cpp::hook_FUN_0080e680`

5-byte JMP detour at `va:0x0080E680` (the PE BINARY 107 chained-XOR loader).
Replacement:
1. Reads `LANGUAGE` from `Settings.cfg` (default `us`)
2. Resolves `<game_dir>\scripts\<lang>\global.res`
3. CreateFileA → GetFileSizeEx → HeapAlloc → ReadFile
4. Stores `(buffer, size)` into the resource-out struct (ECX-pointed)
5. Returns 1 on success, 0 on failure (Sacred treats failure as fatal — there's
   a "Can't load Global.res!" MessageBox path that triggers ExitProcess).

**Safety**:
- Original `FUN_0080e680` allocates a fresh buffer too; ours uses
  `HeapAlloc(GetProcessHeap())` instead of MSVC 6 `operator_new`. Sacred's
  resource manager never frees this buffer in the process lifetime, so
  allocator-mismatch is moot.
- Calling convention preserved: original is `__thiscall(void)` (only ECX); our
  hook is `__fastcall(ECX, EDX)` which is binary-identical for the ECX-only
  case, EDX ignored.
- File contents are byte-identical to what the original PE-resource code
  produces after chained-XOR decoding — verified in
  `tools/globalres_resolve.py` when the 23,123 entries were first decoded.

**Side effects unlocked**:
- Drop a different `global.res` in `scripts/<lang>/` → game uses it.
- Future: hot-swap language in the ImGui overlay by re-running the load.
- Future: write to that file from inside our DLL to mod text live.

## Patch 6 — neutralize `FUN_00811440` (force-foreground busy-wait)

**Implementation**: `sdk/patches.cpp::try_install_patch6`

Direct byte-overwrite at the function entry. The decompiled function:

```c
bool FUN_00811440(HWND hwnd, uint timeout) {
    DWORD start = GetTickCount();
    bool ok = true;
    if (!IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_SHOW);
        while (GetForegroundWindow() != hwnd && ok) {
            SetForegroundWindow(hwnd);           // ← busy-wait
            ok = (GetTickCount()-start < timeout) || timeout == sentinel;
        }
        while (GetFocus() != hwnd && ok) {
            SetFocus(hwnd);                      // ← busy-wait
            ok = (GetTickCount()-start < timeout) || timeout == sentinel;
        }
        UpdateWindow(hwnd);
        return ok;
    }
    return false;
}
```

If `timeout == DAT_00a1ccc4` (an "infinite" sentinel), and the OS denies
foreground (which it will under a debugger, or when another process has
focus-lock since Win2000), the loop never exits — the freeze 2007 Thorium
described as "Debuggerfreez".

Replacement at the entry:

```
33 C0          xor eax, eax       ; return false
C2 08 00       ret 8              ; stdcall: clean 8 bytes of args
```

Calling convention auto-detected at install time by scanning the original for
the first `c3` / `c2 nn nn` epilogue. On our build the function is stdcall
(`ret 8`), matching `bool(HWND, uint)`.

Sacred treats the `false` return as "couldn't acquire focus" and continues
without spin. ShowWindow + UpdateWindow are skipped, but Sacred's window is
already shown/updated by the time this helper is invoked (only called during
second-stage display chain setup after ChangeDisplaySettings). No visible side
effects expected.

## Files changed / added

| File | What |
|---|---|
| `sdk/patches.cpp` | new — implementation of both patches |
| `sdk/sdk.h` | added `sdk::patches` namespace + status string |
| `sdk/dllmain.cpp` | call `patches::install()` after `hooks::install()` |
| `sdk/overlay.cpp` | new "2.29-rosetta patches" CollapsingHeader with live status |
| `sdk/SacredSDK.vcxproj` | registered `patches.cpp` |
| `sdk/tools/ghidra/FindPatch6Site.java` | new — Ghidra script that found Patch 6 site |
| `sdk/tools/verify_patch_targets.py` | new — pre-flight byte check for both patches |
| `sdk/tools/ghidra/decompiled/00811440_FUN_00811440.c` | new — decompiled focus-force func |

## Expected overlay output at launch

In the overlay panel "2.29-rosetta patches":
- patch 1 should go green with status like:
  `patch1: '<gamedir>\\scripts\\us\\global.res' -> 1234567 bytes @ 0x...`
- patch 6 should go green with status like:
  `patch6: FUN_00811440 neutralized as stdcall(ret imm16); was 51 53 55 8b...`

Diagnostics:
- patch 1 red/grey but game still has text → hook didn't fire, FUN_0080e680's
  original code ran. Investigate the detour install step in `[patches]` log
  lines.
- patch 1 status "fail" → most likely `scripts/us/global.res` not present. List
  the directory.
- patch 6 didn't install → check the log for `[patch6]` reason.

## Open items

- Other 5 rosetta patches (2/3/4/5/7) — see docs/12 for which we want.
- Combo: once patch 1 is proven, add ImGui "language" dropdown that invalidates
  Sacred's resource manager and triggers reload.
- Patch 6 alternative: instead of killing the whole function, NOP just the two
  CALL sites (0x00811487 SetForegroundWindow, 0x008114be SetFocus). Less
  invasive, same outcome for the freeze. Add as a config option.
