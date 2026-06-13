# SacredSDK — Python tools

Python utilities for working with Sacred Gold's data formats from the
command line. All scripts are stdlib Python 3.10+ except where noted.

There are two sections below:

- **[Modder-facing](#modder-facing)** — the tools you'll use writing,
  shipping, and debugging mods.
- **[Workshop](#workshop)** — analysis / reverse-engineering helpers.
  Useful if you're extending the SDK or digging into Sacred's
  internals; less polished, output formats may change.

A few one-off recon scripts from earlier RE sessions live in
`scratch/` and are git-ignored — not part of the public surface.

---

## Setup

```cmd
python -m pip install pefile
```

`pefile` is only needed by `recon_exe.py`. Everything else is stdlib.

Most scripts have absolute paths to `E:\SteamLibrary\steamapps\common\Sacred Gold\`
baked in — edit `GAME` at the top of a script if your install lives
elsewhere. (Future: env var override.)

---

## Modder-facing

### FunkCode bytecode round-trip

The two halves of the `.bin ↔ .lua / .fkasm` pipeline. Both have
mnemonic-preferred output with `_HEX` fallback for ops the encoder
doesn't yet model, so every vanilla `.bin` round-trips byte-perfect
(verified on 132/132 files).

| Script | Purpose |
|---|---|
| `funkcode_decompile_lua.py` | `.bin → .lua` data dump consumed by `lib/vanilla.load()`. Per record: `{ tag, flags, {"LABEL", args...}, ... }`. |
| `funkcode_decompile.py` | `.bin → .fkasm` (text disassembly, line-per-op format). |
| `funkcode_compile.py` | `.fkasm → .bin` (round-trip with above). |
| `funkcode_disasm.py` | Pretty-print a single `.bin` to stdout — best for ad-hoc inspection. |
| `funkcode_ops.py` | Library (imported by the above): opcode table + structured (de)serializer. |
| `funkcode_tags.py` | Tag → subsystem dictionary (102 distinct tags, 79 labelled). |
| `funkcode_tagmap.py` | Auto-generated tag inventory from a vanilla scan. |

Examples:
```cmd
:: Decompile vanilla Sera to Lua for use with v.load
python tools\funkcode_decompile_lua.py bin\TYPE_NPC_SERAPHIM\FunkCode.bin ^
    -o custom\lua\_vanilla\bin\TYPE_NPC_SERAPHIM\FunkCode.lua

:: Inspect a single .bin
python tools\funkcode_disasm.py custom\bin\TYPE_NPC_SERAPHIM\QuestCode.bin

:: Round-trip test
python tools\funkcode_decompile.py FunkCode.bin -o /tmp/a.fkasm
python tools\funkcode_compile.py /tmp/a.fkasm -o /tmp/b.bin
fc /b FunkCode.bin /tmp/b.bin   :: should report no differences
```

### Resource lookup & global.res

| Script | Purpose |
|---|---|
| `sacred_hash.py` | Sacred resource-name hash (CLI + library). 6/6 self-test vectors pass. Used to compute the keys for `global.res` lookups. |
| `globalres_modify.py` | Edit `scripts/<lang>/global.res` text entries by name / id / hash. Writes to `custom/scripts/<lang>/global.res` (vanilla untouched). |
| `globalres_peek.py` | Inspect `global.res` structure: header, slot count, first-N entries, byte histogram. |
| `globalres_resolve.py` | Reverse-lookup: given a hash, find the symbolic name (cross-checks `hash_names.csv`). |
| `hash_names.csv` | ~1 847-entry `hash,name,text` dictionary harvested from the community. ~145 KB. |

Examples:
```cmd
:: Change one quest line — vanilla stays untouched, your edit lands
:: in custom/scripts/us/global.res which Patch 1 serves.
python tools\globalres_modify.py --by-name HQ_3_2_1_Log_Title --to "My New Title"

:: Find a hash you saw in a log
python tools\globalres_resolve.py 213393821

:: Compute hash for a symbolic name
python tools\sacred_hash.py "HQ_3_2_1_Log_Title"
```

### Quest text dumpers

| Script | Purpose |
|---|---|
| `quest_dump.py` | Dump readable quest cards (`HQ_/NQ_/RB_/DQ_` prefixes). Output: title, header, log entries, location. |
| `quest_book.py` | Bulk-dump all 435 quests to a single Markdown file (~332 KB). |
| `quest_script.py` | Extract per-quest bytecode in execution order — script trace from FunkCode. |
| `quest_script_book.py` | Bulk variant of `quest_script.py` (~12.5 MB Markdown). |

Examples:
```cmd
:: A single quest
python tools\quest_dump.py HQ_3_2_1

:: Whole game's quest text → one big Markdown
python tools\quest_book.py -o quests-all.md
```

### Balance.bin

| Script | Purpose |
|---|---|
| `balance_diff.py` | Compares Steam `Balance.bin` against community variants (Vanilla / Veteran / ReBorn) and dumps byte diffs + string scan. |

### Smoke testing the proxy DLL

| Script | Purpose |
|---|---|
| `smoke_test_proxy.bat` | Launch Sacred → wait 4s for DllMain → kill → cat the SDK log. Quick sanity check after a DLL rebuild. |

---

## Workshop

These are the tools we used to reverse-engineer Sacred. Most are
single-pass scripts that output to stdout; expect to read their source
to understand what they're showing you.

### Bytecode / interpreter

| Script | What it does |
|---|---|
| `funkcode_decompile_semantic.py` | `.bin → readable Lua` using high-level `lib/quest` helpers. Recognises ~7.5% of vanilla record patterns; the rest fall back to `raw.rec(...)`. |
| `funkcode_grammar.py` | Grammar synthesiser — parses every payload against the known opcode table, reports unrecognised tails. |
| `funkcode_swap.py` | Same-length identifier swap on `.bin` files. Predates the Lua pipeline; useful when you want a tiny mod without setting up a Lua bake. |
| `funkcode_walker.py` | Walker simulator — iterates records and prints what each tag does in execution order. |
| `funkcode_roundtrip_test.py` | Asserts `compile(decompile(B)) == B` on every vanilla `.bin`. Currently 132/132 pass. |
| `walker_dispatch.py` | Decoded outer-walker dispatch table from `FUN_00475680` (132 cases → 116 subsystems). |
| `interpreter_callers.py` | Static analysis: who calls the bytecode interpreter `FUN_00472bc0`? |
| `interpreter_parse.py` | Decode the interpreter's per-opcode width table by static analysis. |
| `interpreter_payload.py` | Decode opcode-payload widths (CSTR1, CSTR2, U32_CSTR1, …). |
| `interpreter_strings.py` | Pull every string referenced by the interpreter, with caller context. |

### Subsystem labelling

| Script | What it does |
|---|---|
| `subsystem_label.py` | First-pass subsystem name resolver (tag → C function in Sacred.exe). |
| `subsystem_label2.py` | Refined pass — adds xref / call-site context. Used to produce the `18-funkcode-tag-table.md` reference. |

### Hash / global.res RE

| Script | What it does |
|---|---|
| `hash_bruteforce.py` | Try short ASCII permutations against a target hash. |
| `hash_expand.py` | Expand a partial hint into candidate names (e.g. `HQ_X_X_X`-shaped). |
| `hash_verify.py` | Validate the recovered hash function against a known (name, hash) pair set. |
| `seen_hashes_resolve.py` | Cross-reference the live `seen_hashes.csv` (written by `text_logger` at runtime) against `hash_names.csv` to identify what Sacred actually queried. |

### Quest analysis (depth)

| Script | What it does |
|---|---|
| `quest_inventory.py` | Quest count breakdown across all 8 classes. |

### Lua catalog generators

Regenerate the `custom/lua/lib/` catalogs that mods consume. Output is
deterministic; re-run when the source changes.

| Script | What it does |
|---|---|
| `gen_item_lua.py` | Generates `custom/lua/lib/items_gen.lua` from the in-binary `TYPE_*` table in `Sacred_decrypted.exe` (base `0x008EC328`, stride `0x44`, 5624 rows). |
| `gen_npc_lua.py` | Generates `custom/lua/lib/npc.lua` from the community `sacred_modding - characters.csv`. |

Both have input/output paths hard-coded near the top: `gen_item_lua.py`
reads `Sacred_decrypted.exe`; `gen_npc_lua.py` defaults its CSV to
`C:\Users\bssth\Downloads\refs\sacred_modding - characters.csv` (override
as `argv[1]`). Edit these for a different install.

### EXE recon

| Script | What it does |
|---|---|
| `recon_exe.py` | Parses `Sacred.exe` (sections, imports, entry point, strings buckets). Reads `pefile`. |
| `extract_compiler_strings.py` | Pull cScriptCompiler-related strings from Sacred.exe (parseStatement / loadScriptedSequenceR / ...). |
| `extract_subsystem_addrs.py` | Extract the addresses of the labelled subsystems for cross-checking. |
| `search_txt_loader.py` | Locate the `.txt` script loader path inside Sacred.exe. |
| `search_va_uses.py` | Find all `[reg+disp32]` accesses to a given offset (hero-struct field heat-map). |
| `splice_decrypted.py` | Splice the runtime-decrypted `.text` dump back into a Ghidra-loadable EXE. |

### Patch / ReBorn HD compatibility

| Script | What it does |
|---|---|
| `verify_229_patches.py` | Cross-check our runtime hooks against Thorium's 2007 unofficial patch source. |
| `verify_names_csv.py` | Spot-check `hash_names.csv` integrity. |
| `verify_patch_targets.py` | Confirm patch addresses against the live Sacred.exe build. |
| `reborn_anchor.py` | Find ReBorn HD anchor offsets for shifted addresses. |
| `reborn_bindiff.py` | Diff a ReBorn `.bin` against vanilla. |
| `reborn_diff.py` | High-level: which subsystems does ReBorn touch? |

### Ghidra

| Path | What it does |
|---|---|
| `ghidra/` | Java scripts for headless Ghidra. Setup notes: `JAVA_HOME=...jdk-21`, `GHIDRA_HOME=...ghidra_12`. Headless launcher is community-maintained — call analyzeHeadless directly if `run_headless.bat` misbehaves. |

---

## Style notes

- **No non-ASCII in `print()`**: the Windows code page on the typical
  Sacred-modder machine is `cp1251` or `cp1252` and unicode arrows /
  comparison glyphs crash the script mid-run. Stick to ASCII.
- **Endianness**: big-endian `u16` for record sizes; little-endian for
  payload ints/floats. Don't confuse.
- **Game path is hard-coded**. Most scripts have `GAME = r"E:\SteamLibrary\..."`
  near the top. We'll switch to env-var / argparse in a future cleanup
  pass — see [docs/TOOLS_PLAN.md](../docs/TOOLS_PLAN.md) for the open
  decisions.
