# `.claude` workspace — Sacred Gold SDK

Durable knowledge + planning workspace for the Sacred Gold reverse-engineering & modding SDK
(a C++ DLL injected via an `ijl15.dll` proxy, plus Lua mods and Python dev tools).
This directory is the project's long-term memory; the live game-root junction lets a session
working in the game dir reach it directly.

## Where this lives
- Canonical path: `E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\.claude\`.
- A junction at the game root surfaces the workspace so a session rooted at
  `E:\SteamLibrary\steamapps\common\Sacred Gold` finds `.claude/` without leaving the game dir.
- The injected DLL deploys into the game dir; the SDK sources live under `sdk/`.

## Subdirectories — what each holds & how to use it

| Dir | Holds | Use it for |
|---|---|---|
| **`plans/`** | `MASTER_PLAN.md` — the ordered cross-goal execution plan (refactor / tools-elimination / RE sweep / refs-ports / workspace), with per-step risk + dependency order. | START HERE for "what do I do next". Every step is gated to keep the deployed DLL buildable. |
| **`knowledge/`** | `INDEX.md` + 8 mined reports (SDK arch, tools audit, RE backlog, and 5 `refs_*` reference mines). Future: `re/` (the relocated CANON scratch specs under topic subfolders) + `re/fixtures/`. | Look up a file format, engine offset, struct layout, or a `TODO(port)` candidate. `INDEX.md` is the fast map. |
| **`memory/`** | `MEMORY.md` index + dated session-state resume points + validated-signal notes (`talk-signal-0x200.md`) + initiative trackers. | RESUME a session. Read `session-state-2026-06-13.md` first — it has the deployed md5, build/deploy/run recipe, what works, and pending polish items. |
| **`agents/`** | Sub-agent configs (sub-agents wrote the 8 knowledge reports). | Re-run or add specialized audit/mining agents. |

## How to use this workspace
1. **Resuming?** Read `memory/session-state-2026-06-13.md`, then `plans/MASTER_PLAN.md`.
2. **Need a fact** (offset/format/VA)? Start at `knowledge/INDEX.md`, jump to the report, then the cited source file:line.
3. **Doing RE?** `knowledge/re_backlog.md` is the single prioritized roadmap — SOLVED areas are locked (don't re-derive); OPEN targets each carry the concrete next experiment.
4. **Porting from refs?** `MASTER_PLAN.md` Goal D groups every `TODO(port)` into tiers (data tables → format codecs → live-RAM bindings → networking). Each carries exact offsets so it's actionable cold.

## Conventions
- Game EXE base `0x00400000`, no ASLR → **file offset = VA − 0x400000**; absolute VAs are stable to hardcode (this Steam Gold build).
- **Community-ref VAs are from a DIFFERENT (German Underworld) build** — byte-signature-verify before trusting any absolute VA from `refs_native_src.md`/`refs_tools.md`. Our own `re_backlog.md` corpus IS this Steam build.
- `.text` ships SafeDisc-encrypted; **no `.text` hook/patch may run in DllMain** — they arm only after the entropy decrypt-gate (`dump_text.cpp`).
- Confidence tags in reports: **[HIGH]** read directly, **[MED]** inferred, **[LOW]** verify before acting. Port stubs are marked `TODO(port):`.
- Every refactor/port step keeps the deployed DLL buildable; verify the deployed md5 changes only when intended (baseline `bc69043b`).
