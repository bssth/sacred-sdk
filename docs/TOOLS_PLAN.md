# Tools — public-push status

The `tools/` directory ships publicly alongside the docs. This document records the curation decisions.

---

## Layout (current)

```
sdk/tools/
├── README.md                   ← Walkthrough: "I want to X → run Y"
├── 53 .py scripts              ← All flat (modder-facing + workshop)
├── hash_names.csv              ← 23 123-entry hash dictionary (~600 KB)
├── smoke_test_proxy.bat        ← Sanity check after DLL rebuild
├── ghidra/
│   ├── README.md
│   ├── 8 .java scripts         ← Headless Ghidra scripts
│   ├── run_headless.bat
│   └── decompiled/             ← gitignored (derived from Sacred.exe)
└── scratch/                    ← gitignored (one-off recon scripts)
    └── README.md
```

### Flat layout instead of `internal/` subfolder

Tier 1 (modder-facing) and Tier 2 (workshop) scripts are not split into separate directories: Tier 2 scripts import Tier 1 helpers (e.g. `funkcode_decompile_semantic.py` imports `funkcode_ops`, `funkcode_tags`). A subfolder split would require `sys.path` shims at the top of every moved script. Instead, `tools/README.md` sections "Modder-facing" vs "Workshop" so the docs do the separation without breaking imports.

---

## What ships publicly

18 modder-facing scripts (the `tools/README.md` "Modder-facing" section): FunkCode round-trip pipeline, `globalres_modify`, quest dumpers, `sacred_hash`, `balance_diff`, smoke test.

~30 workshop scripts (the "Workshop" section): interpreter analysis, subsystem labelling, hash brute-search, EXE recon, ReBorn HD diff, type-system history (deprecated, kept for the RE story), Ghidra Java scripts.

Data: `hash_names.csv` (23 123 known hashes harvested from the community).

---

## What stays local

`tools/scratch/` (12 scripts) — `recon_gold[1-9].py`, `recon_inv[1-3].py`. One-off recon iterations whose findings are already baked into `sdk/runtime_triggers.cpp`. Kept locally as RE archaeology.

`tools/ghidra/decompiled/` (1.8 MB / 127 C files) — decompiled output from Ghidra runs against `Sacred.exe`. Derived from copyrighted material; regenerable from a Sacred.exe + the Java scripts in `tools/ghidra/`. Keep local.

`__pycache__/` — Python bytecode cache, never published.

---

## Open polish items (non-blocking)

1. Hard-coded game path. Most scripts have `GAME = r"E:\SteamLibrary\..."` near the top. Switch to an env var (`SACRED_GAME_DIR`) or `argparse` flag. Effort: ~1 hour.
2. `--help` consistency. Most CLI scripts have help text but the polish varies. Standardising the format. ~1 hour.
3. Python version statement. Most scripts target 3.10+ (some use `match` statements). Pin this in `tools/README.md`. ~5 min.
4. `run_headless.bat` in `tools/ghidra/` has a quirk where `import` invokes `import-decrypted`. Workaround in the ghidra README. ~30 min to fix properly.

None of these block the public push.
