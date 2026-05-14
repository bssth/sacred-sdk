# 07 — Experiment 1: `ijl15.dll` proxy

First in-process foothold inside `Sacred.exe`. No EXE patching. Goal: prove that our own code runs inside the game's address space at startup.

## Choice of host DLL

| Candidate | Why considered | Verdict |
|---|---|---|
| `d3d9.dll` (from `Universal-D3D9-HOOK` template) | overlay-friendly, lots of public tooling | ❌ Sacred renders through **DirectDraw**, not D3D9. A `d3d9.dll` next to `Sacred.exe` is never loaded unless a DDraw→D3D9 wrapper (dgVoodoo2, DDrawCompat) is installed first. |
| `ddraw.dll` | actually imported by Sacred (3 functions) | ⚠️ works, but `ddraw` is a system DLL — any mistake risks visual glitches or hard crashes. |
| `ijl15.dll` (Intel JPEG) | 6 exports, **4 imported by ordinal 2..5** in `Sacred.exe`, third-party (sits next to the EXE), C-API, no graphics involvement | ✅ chosen |

`pefile` confirms Sacred's IAT entry for ijl15:

```
Sacred.exe imports from ijl15.dll:
  by_ord: ordinal=2     (= ijlInit)
  by_ord: ordinal=3     (= ijlFree)
  by_ord: ordinal=4     (= ijlRead)
  by_ord: ordinal=5     (= ijlWrite)
```

Original `ijl15.dll` exports (verified with `pefile`):

| Ord | Name |
|---|---|
| 1 | `ijlGetLibVersion` |
| 2 | `ijlInit` |
| 3 | `ijlFree` |
| 4 | `ijlRead` |
| 5 | `ijlWrite` |
| 6 | `ijlErrorStr` |

## How the proxy works

```
                                          +------------------+
            Sacred.exe                    | ijl15.dll        |  <- our proxy
            ┌─────────────────┐           |                  |
            │ IAT(ijl15.dll)  │           |  EXPORTS:        |
            │  ord 2 ──┐      │   load    |   @1 -> forward  |
            │  ord 3 ──┤      │ ────────► |   @2 -> forward  |
            │  ord 4 ──┤      │           |   ...            |
            │  ord 5 ──┤      │           |                  |
            └─────────┼──────┘            |  DllMain (logs)  |
                      │                   +------|-----------+
                      │   PE loader walks forwarder chain     
                      │                          │
                      ▼                          ▼
                                          +------------------+
                                          | ijl15_real.dll   |  <- renamed original
                                          |   (Intel JPEG)   |
                                          +------------------+
```

The Windows PE loader handles forwarder strings transparently — when Sacred's IAT entry for ord-2 is resolved, the loader sees our proxy's `@2` is a forwarder pointing at `ijl15_real.ijlInit` and patches Sacred's IAT to the real function pointer directly. After resolution, calls into `ijlInit` don't even pass through our DLL.

The only code that runs in **our** DLL is `DllMain` on `DLL_PROCESS_ATTACH`. That callback fires once, before Sacred's entry point. After it, we keep memory but no JPEG call ever touches our code.

That's exactly what we want for a bootstrap point — no behavioural change, just a foothold.

## Implementation

Three files inside `sdk\SacredSDK\SacredSDK\`:

| File | Role |
|---|---|
| `dllmain.cpp` | `DllMain` + 6 `#pragma comment(linker, "/EXPORT:foo=ijl15_real.foo,@N")` lines |
| `SacredSDK.vcxproj` | configured so `Release|Win32` builds `ijl15.dll` (TargetName override) |
| `proxy.def` | unused — kept as documentation of the export layout |

Why `#pragma comment(linker, …)` instead of a `.def` file?

The `.def` `EXPORTS name=otherdll.name @N` syntax compiles fine but the modern MSVC linker (v143) treats it as requiring a *local* symbol named `name` and emits `LNK2001` even though the right behaviour is a cross-DLL forwarder. `#pragma comment(linker, "/EXPORT:name=otherdll.name,@N")` produces the same forwarder entry in the export table without that local-symbol requirement.

## Bootstrap behaviour

On `DLL_PROCESS_ATTACH`, `dllmain.cpp::greet` writes to `sdk\logs\sdk_loaded.log` (alongside `sdk\docs\`):

```
[YYYY-MM-DD HH:MM:SS.mmm] === ijl15 proxy DllMain DLL_PROCESS_ATTACH ===
[…] exe       = E:\SteamLibrary\…\Sacred.exe
[…] cmdline   = "Sacred.exe"          ← will catch CHEATS=1 here
[…] pid/tid   = NNNN / NNNN
[…] self base = 0xXXXXXXXX            ← module base for future hook offsets
```

`DLL_THREAD_*` callbacks are disabled via `DisableThreadLibraryCalls` — we don't need them yet, and skipping them avoids unnecessary lock churn on every CreateThread Sacred makes.

## Build (CLI)

```cmd
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
   "E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\SacredSDK.vcxproj" ^
   -p:Configuration=Release -p:Platform=Win32
```

Output: `sdk\Release\ijl15.dll` (currently ~127 KB).

## Deploy (one time)

From the game install root:

```cmd
:: backup original (once)
copy ijl15.dll ijl15_real.dll
:: drop proxy in
copy sdk\Release\ijl15.dll ijl15.dll
```

State afterwards:

| File | What it is |
|---|---|
| `ijl15.dll` | our proxy (~127 KB) |
| `ijl15_real.dll` | original Intel JPEG library (~352 KB) |

To roll back: delete `ijl15.dll`, rename `ijl15_real.dll` back to `ijl15.dll`. (Or just `copy /y ijl15_real.dll ijl15.dll` if you no longer need the backup.)

## Verify

```cmd
sdk\tools\smoke_test_proxy.bat
```

This launches `Sacred.exe`, waits 4 seconds, kills it, and prints the resulting log. If the log appears with the expected lines, the proxy is live.

## Failure modes & first-aid

| Symptom | Likely cause | Fix |
|---|---|---|
| Game crashes immediately on launch | `ijl15_real.dll` missing or wrong arch | Restore original, check backup |
| No log file, but game starts and JPEGs render | DllMain ran but `sdk\logs\` path resolution failed | Check `GetModuleFileNameA` result; verify write permissions to install dir |
| Log appears but cmdline shows the wrong exe | We were loaded into a different process (e.g. Steam launcher) | Check `exe = …` line; confirm it points at `Sacred.exe`/`Testapp.exe` |
| Game starts but no JPEG loaders / textures broken | One of the 4 forwarders is wrong | Re-check ordinals in `dllmain.cpp` vs. the original DLL |

## Why this is the right shape for future hooks

Once `DllMain` has run, we own the process. We can:

- spawn a worker thread (so we don't block PE-load) that:
  - waits for Sacred's main module to finish initializing (poll for `.text` page accessibility, or wait on a known import)
  - resolves Sacred's own functions by xref/pattern matching (Ghidra-derived offsets)
  - installs MinHook / Detours patches there
- install `SetWindowsHookEx`-style overlays
- write a console + ReadFile poller for live config tweaking

All of that lives inside our codebase. Sacred.exe stays byte-identical on disk.

## Status

| Step | State |
|---|---|
| Pick host DLL | ✅ `ijl15.dll` (ordinals 2..5) |
| Forwarder DLL builds | ✅ 127 KB Win32 PE, all 6 exports forwarded |
| Deploy + backup | ✅ swap done |
| Smoke test | ✅ confirmed — game launches, log written |
| Logs land in `sdk\logs\sdk_loaded.log` | ✅ both ATTACH and DETACH callbacks fire |

## Confirmed observations from first live run

```
[2026-05-14 04:18:17.215] === ijl15 proxy DllMain DLL_PROCESS_ATTACH ===
  exe       = E:\SteamLibrary\steamapps\common\Sacred Gold\Sacred.exe
  cmdline   = "Sacred.exe"
  pid/tid   = 4552 / 14212
  self base = 73A20000
[2026-05-14 04:18:38.289] === ijl15 proxy DllMain DLL_PROCESS_ATTACH ===
  cmdline   = "E:\SteamLibrary\steamapps\common\Sacred Gold\Sacred.exe"
  self base = 73910000
[2026-05-14 04:19:38.534] === DLL_PROCESS_DETACH (lpReserved=00000001) ===
```

Three concrete facts established:

1. **Our DllMain runs inside `Sacred.exe`'s address space at PE load time.** ✅
2. **JPEG forwarders work end-to-end** — the game would have crashed mid-startup on the first `ijlInit` if the export table or arch were wrong. ✅
3. **Our DLL's own base address is ASLR-randomized** (`0x73A20000` vs `0x73910000`). Implication for code: any internal offsets must be computed from `GetModuleHandle(NULL)` / `&function` / our own DLL handle. **Sacred.exe itself** is a 2006 MSVC-6 binary with no `/DYNAMICBASE` flag, so its image base stays at `0x400000` across runs and Ghidra-derived offsets will remain stable.
4. **`lpReserved=0x00000001` on DETACH** = the game truly exited (process termination, not DLL unload). Clean teardown path is available for future state save / file flushes.

## Next experiments (in order of leverage)

1. **Confirm we see `CHEATS=1` in the cmdline log.** Cheap free win.
2. **Print the loaded module list at attach time.** First step toward enumerating where Sacred's `.text` lives at runtime — confirms ImageBase from recon (`0x400000`).
3. **Spawn a background thread that periodically writes `tick %lu`.** Proves we can run real work alongside Sacred.
4. **First MinHook patch**: hook `CreateFileA` to log every file Sacred opens — gives us a behavioural trace without ProcMon.
5. **Find and hook one Sacred function** by xref of a known string (e.g. the `Bin\Balance.bin` literal from recon).
