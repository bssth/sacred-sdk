# `custom/` override layer

A clean way to mod Sacred files without touching the Steam-verified install.
The SDK DLL hooks `CreateFileA` at the IAT level: any read that would
resolve to `<game_dir>\<sub>\<file>` is transparently re-pointed at
`<game_dir>\custom\<sub>\<file>` if that override exists.

```
Sacred.exe                                Disk
   ↓ CreateFileA("scripts\us\global.res")
   └─ ijl15.dll fs_override hook
        ├── exists `custom\scripts\us\global.res`?  ──── YES ── open THAT
        └── else  pass through                        ──── NO  ── open vanilla
```

## Why this layout

- **Steam-friendly**: vanilla files never change, so "Verify integrity" is
  always green. Anything in `custom/` lives outside Steam's view.
- **Format-agnostic**: works for `global.res`, `Balance.bin`,
  `bin/TYPE_NPC_*/FunkCode.bin`, `pak/*.pak`, anything Sacred opens via
  CreateFileA.
- **Stackable**: future per-mod subdirectories or selectable mod profiles
  are a small extension (`custom\<modname>\...` + priority list).
- **Reversible**: delete the override, vanilla resumes on next launch.

## How it works (one CreateFileA call)

```c
HANDLE hook_CreateFileA(LPCSTR lpFileName, DWORD access, ...) {
    if (read_only(access)) {
        if (relative = strip_game_dir_prefix(lpFileName)) {
            candidate = "<game_dir>\\custom\\" + relative;
            if (GetFileAttributesA(candidate) != INVALID) {
                return orig_CreateFileA(candidate, ...);   // swap and call
            }
        }
    }
    return orig_CreateFileA(lpFileName, ...);              // pass through
}
```

Guard rails:

- ✅ Only redirects read opens (`GENERIC_READ`, `OPEN_EXISTING`/`OPEN_ALWAYS`).
- ❌ Write opens (save games, logs) pass through untouched.
- ❌ Top-level files (the EXE itself, DLLs) never redirected.
- ❌ Paths outside the game dir.
- ❌ `..\` escapes filtered (sandbox).

## Two redirection paths active

There are now **two** layers in front of file reads, deliberately:

1. **High-level Patch 1** (`patches.cpp`): the FUN_0080e680 detour that
   loads `global.res` from disk in the first place. We already taught it
   to check `custom/scripts/<lang>/global.res` first, then fall back to
   the vanilla file.
2. **Low-level CreateFileA hook** (`fs_override.cpp`): catches every OTHER
   file Sacred opens. So `Balance.bin`, FunkCode `.bin` files, etc.,
   all get the override path "for free".

The two are independent — Patch 1's chained-XOR resource-load can't be
expressed as a simple file-open, so it needs its own check. Everything
else takes the CreateFileA path.

## Examples

```cmd
:: Text mod
copy scripts\us\global.res custom\scripts\us\global.res
notepad ... custom\scripts\us\global.res     # via globalres_modify.py
```

```cmd
:: Balance mod
copy bin\Balance.bin custom\bin\Balance.bin
:: edit Balance.bin (we have the schema in docs/02)
```

```cmd
:: Quest-script mod (future, requires FunkCode encoder)
copy bin\TYPE_NPC_GLADIATOR\FunkCode.bin custom\bin\TYPE_NPC_GLADIATOR\FunkCode.bin
:: edit and the game picks it up next launch
```

## Tooling integration

- `sdk/tools/globalres_modify.py` writes its output to `custom/scripts/us/global.res`
  by default. On first invocation it clones the vanilla file there so you
  always start from a complete baseline. The vanilla file is never touched.
- Future `funkcode_modify.py` / `balance_modify.py` will follow the same
  convention: read vanilla, write `custom/...`.

## Verification in the overlay

`Custom/ overrides` panel shows:

- `CreateFileA opens` — total opens observed by the hook
- `redirected` — how many actually hit a `custom\` file
- `last` — the most recently redirected path

Quick smoke test:

```cmd
copy scripts\us\global.res custom\scripts\us\global.res
:: launch Sacred
```

Open the overlay. After Sacred reaches the main menu, `redirected` should
be ≥ 1 with `last` showing `scripts\us\global.res -> custom\scripts\us\global.res`.

## Files added

| File | What |
|---|---|
| `sdk/fs_override.cpp` | the CreateFileA hook |
| `sdk/sdk.h` | namespace `fs_override` declarations |
| `sdk/dllmain.cpp` | `fs_override::install()` called from DllMain |
| `sdk/overlay.cpp` | `Custom/ overrides` panel |
| `sdk/patches.cpp` | Patch 1 now checks `custom/` first |
| `sdk/tools/globalres_modify.py` | writes to `custom/scripts/us/` by default |
| `custom/README.md` | user-facing instructions |
| `custom/scripts/`, `custom/bin/` | empty skeleton dirs |
