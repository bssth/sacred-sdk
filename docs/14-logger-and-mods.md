# Live text logger + text-mod workflow + HD force

Three components landed in one pass:

1. Resource-lookup logger (`sdk/text_logger.cpp`) — trampoline hook on
   `FUN_0080eaf0` records every (hash, result) pair the game queries.
2. Text-mod tooling (`sdk/tools/globalres_modify.py`) — edit `global.res`
   in-place; Patch 1 already reads it from disk on every load.
3. HD-resolution force (`sdk/hooks.cpp` + `sdk.ini`) — borderless windowed at
   custom resolution, gated by config, safe to use now that `patch6` killed the
   busy-wait.

## 1. Live text logger

### Architecture

Trampoline detour at `va:0x0080EAF0`:

```
ORIGINAL prologue (7 bytes):
    6A FF             push -1
    68 10 A7 88 00    push offset 0x0088A710
PATCHED:
    E9 rel32          jmp our_hook        (5 bytes)
    90 90             nop nop             (2 bytes, padding)

TRAMPOLINE (in VirtualAlloc'd RWX memory):
    [original 7 bytes verbatim]
    E9 rel_back       jmp (original + 7)
```

Hook:
```cpp
void* __fastcall hook_FUN_0080eaf0(void* this, void* edx, uint32_t hash) {
    void* result = g_trampoline(this, edx, hash);   // run original
    record(hash, result);
    return result;
}
```

`__fastcall` ABI matches `__thiscall` for ECX + 1 stack arg (both use ECX for
first, both `ret 4` on cleanup).

### Output

`sdk/logs/seen_hashes.csv` grows continuously (flush every 5 s). Each line is
one hash. Use `tools/seen_hashes_resolve.py` to join against
`tools/hash_names.csv` and `global.res` text:

```
python sdk/tools/seen_hashes_resolve.py
```

Writes `sdk/logs/seen_named.csv` with columns `hash,name,text,in_globalres`.
Run after a play session to see what was queried.

### Value

- Live coverage of the whole id space the game uses, not just manually
  templated names.
- "Unnamed hashes" in the output are reverse-engineering targets — known-real,
  with real text, unknown symbolic name.
- Combined with a future unicorn-emulator-driven brute-forcer, the unnamed set
  shrinks.

## 2. Text-mod workflow

Patch 1 makes Sacred read `global.res` from disk on every load, so a text mod is
"edit the file, restart Sacred."

`tools/globalres_modify.py` does the editing safely:

```
# Find a string by content
python globalres_modify.py --grep "Mick the Swift"

# Modify by recovered name (uses sacred_hash internally)
python globalres_modify.py --by-name ID_NPC_MERC --to "Bjorn the Slow"

# Or by numeric id (Sacred decimal-stringifies before hashing)
python globalres_modify.py --by-id 17050 --to "Custom NPC name"

# Or by raw hash (from seen_hashes.csv)
python globalres_modify.py --by-hash 1269826129 --to "Bjorn the Slow"
```

- First modify creates `global.res.bak`.
- Length-changing edits are supported — script re-computes all subsequent
  offsets.
- Restore by `copy /Y scripts\us\global.res.bak scripts\us\global.res`.

## 3. HD force (sdk.ini)

### Why it's safe now

The 2007 crash mode was:
1. Override CreateWindowExA → set WS_POPUP / fullscreen size.
2. Sacred calls ChangeDisplaySettingsA → we swallow.
3. Sacred calls `FUN_00811440(hwnd, INFINITE_SENTINEL)` to force-focus.
4. SetForegroundWindow fails under the modified state → busy-wait forever.
5. Black screen + frozen taskbar.

`patch6` makes step 4's function return `false` instantly. Steps 1-3 are safe to
attempt because step 4 can't hang.

### Use

Copy `sdk.ini.example` to `sdk.ini` next to `Sacred.exe`, edit:

```ini
[hooks]
force_borderless=1
swallow_displaymode=1
force_width=1920
force_height=1080
```

Overlay shows the resolved values under "Display force (sdk.ini)".

### Knobs

| key | what |
|---|---|
| `force_borderless` | strip frame, replace with WS_POPUP, center on primary monitor |
| `swallow_displaymode` | make ChangeDisplaySettings a no-op (returns SUCCESSFUL) |
| `force_width` / `force_height` | override window size (0 = keep Sacred's value) |

### Caveats

- Sacred's UI scales by pixel count — at 4K the HUD will be tiny. ReBorn HD also
  adjusts camera distance for >1366×768; we don't yet (DDraw vtable hooks not
  implemented).
- If the game renders only in the top-left corner at original 640×480, Sacred is
  hard-coded to that backbuffer size. Stretching would need hooks on
  `IDirectDraw::SetCooperativeLevel` + `IDirectDrawSurface::Blt`. Future work.

## Files changed / added

| File | What |
|---|---|
| `sdk/text_logger.cpp` | new — trampoline hook + flush worker |
| `sdk/sdk.h` | + `sdk::text_logger` + `sdk::hooks::ForceConfig` |
| `sdk/hooks.cpp` | sdk.ini reader; CreateWindowExA / ChangeDisplaySettingsA can now actually force |
| `sdk/overlay.cpp` | + two new CollapsingHeaders (logger stats, display force) |
| `sdk/dump_text.cpp` | also calls `text_logger::install()` after decryption |
| `sdk/SacredSDK.vcxproj` | registered `text_logger.cpp` |
| `sdk/tools/globalres_modify.py` | new — text-mod helper script |
| `sdk/tools/seen_hashes_resolve.py` | new — joins seen_hashes.csv with names+text |
| `sdk.ini.example` | new — config template, all toggles OFF by default |

## Test order

1. Logger — launch Sacred, play a bit (main menu + load save + few seconds
   in-game). Overlay "Resource lookup logger" should show non-zero `calls` and
   `unique` counts growing. After exit:
   ```
   python sdk/tools/seen_hashes_resolve.py
   ```
   Verify `sdk/logs/seen_named.csv` exists with rows. The rate of "with name" vs
   "(unknown)" is the live hash-namespace coverage.

2. Text mod — grep for a safe string, modify it, restart, see it in-game.
   ```
   python sdk/tools/globalres_modify.py --grep "Mick the Swift"
   python sdk/tools/globalres_modify.py --by-hash 1269826129 --to "TEST MOD WORKS"
   ```
   Find Mick in Bellevue (Sacred starter town); his name now reads "TEST MOD
   WORKS". Restore from `global.res.bak` after.

3. HD force — copy `sdk.ini.example` -> `sdk.ini`, enable `force_borderless`
   only (leave `swallow_displaymode=0`). Launch. Expect Sacred's window
   borderless at requested size. If that works, try `swallow_displaymode=1` too
   — Sacred should then think it's fullscreen.
