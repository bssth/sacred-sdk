# `custom/` override layer

Mods Sacred files without touching the Steam-verified install. The SDK DLL
hooks `CreateFileA` at the IAT level: any read that would resolve to
`<game_dir>\<sub>\<file>` is re-pointed at `<game_dir>\custom\<sub>\<file>`
if that override exists.

```
Sacred.exe                                Disk
   ↓ CreateFileA("scripts\us\global.res")
   └─ ijl15.dll fs_override hook
        ├── exists `custom\scripts\us\global.res`?  ──── YES ── open THAT
        └── else  pass through                        ──── NO  ── open vanilla
```

## Rationale

- Steam-friendly: vanilla files never change, so "Verify integrity" stays
  green. `custom/` lives outside Steam's view.
- Format-agnostic: works for `global.res`, `Balance.bin`,
  `bin/TYPE_NPC_*/FunkCode.bin`, `pak/*.pak`, anything Sacred opens via
  CreateFileA.
- Stackable: per-mod subdirectories or selectable mod profiles are a small
  extension (`custom\<modname>\...` + priority list).
- Reversible: delete the override, vanilla resumes on next launch.

## Hook logic (one CreateFileA call)

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

- Redirects only read opens (`GENERIC_READ`, `OPEN_EXISTING`/`OPEN_ALWAYS`).
- Write opens (save games, logs) pass through untouched.
- Top-level files (the EXE itself, DLLs) never redirected.
- Paths outside the game dir not redirected.
- `..\` escapes filtered (sandbox).

## Two redirection paths

Two layers sit in front of file reads, deliberately:

1. High-level Patch 1 (`patches.cpp`): the FUN_0080e680 detour that loads
   `global.res` from disk. Taught to check `custom/scripts/<lang>/global.res`
   first, then fall back to the vanilla file.
2. Low-level CreateFileA hook (`fs_override.cpp`): catches every other file
   Sacred opens, so `Balance.bin`, FunkCode `.bin` files, etc. get the
   override path for free.

The two are independent — Patch 1's chained-XOR resource-load can't be
expressed as a simple file-open, so it needs its own check. Everything else
takes the CreateFileA path.

## Examples

```cmd
:: Text mod
copy scripts\us\global.res custom\scripts\us\global.res
notepad ... custom\scripts\us\global.res     # via globalres_modify.py
```

```cmd
:: Balance mod
copy bin\Balance.bin custom\bin\Balance.bin
:: edit Balance.bin (schema in docs/02)
```

```cmd
:: Quest-script mod (future, requires FunkCode encoder)
copy bin\TYPE_NPC_GLADIATOR\FunkCode.bin custom\bin\TYPE_NPC_GLADIATOR\FunkCode.bin
:: edit and the game picks it up next launch
```

## Tooling integration

- `sdk/re/py/globalres_modify.py` writes output to
  `custom/scripts/us/global.res` by default. On first invocation it clones
  the vanilla file there as a baseline. The vanilla file is never touched.
- Future `funkcode_modify.py` / `balance_modify.py` follow the same
  convention: read vanilla, write `custom/...`.

## Overlay verification

The `Custom/ overrides` panel shows:

- `CreateFileA opens` — total opens observed by the hook
- `redirected` — how many hit a `custom\` file
- `last` — the most recently redirected path

Smoke test:

```cmd
copy scripts\us\global.res custom\scripts\us\global.res
:: launch Sacred
```

After Sacred reaches the main menu, `redirected` should be ≥ 1 with `last`
showing `scripts\us\global.res -> custom\scripts\us\global.res`.

## Files added

| File | What |
|---|---|
| `sdk/fs_override.cpp` | the CreateFileA hook |
| `sdk/sdk.h` | namespace `fs_override` declarations |
| `sdk/dllmain.cpp` | `fs_override::install()` called from DllMain |
| `sdk/overlay.cpp` | `Custom/ overrides` panel |
| `sdk/patches.cpp` | Patch 1 now checks `custom/` first |
| `sdk/re/py/globalres_modify.py` | writes to `custom/scripts/us/` by default |
| `custom/README.md` | user-facing instructions |
| `custom/scripts/`, `custom/bin/` | empty skeleton dirs |
