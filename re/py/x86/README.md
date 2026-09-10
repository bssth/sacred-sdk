# `sdk/re/py/x86/` — capstone + Ghidra tooling for `Sacred_decrypted.exe`

Offline developer tooling only. **Nothing here is loaded by the injected DLL.**

Requires `pip install capstone` (tested with 5.0.7, Python 3.12) and — for the
Ghidra scripts — JDK 21 + Ghidra 12.1 (paths are set inside `run_ghidra.bat`).

## Library layer

| file | what it gives you |
|---|---|
| `pe.py` | minimal PE loader: sections, `va2off`, `read(va, n)`, `.text` blob |
| `scan.py` | the workhorse. `find_disp_refs(disp)` = every instruction whose memory operand uses that struct offset; `gh_func_of(va)` / `gh_name(va)` = the containing Ghidra function (handles nested/overlapping entries); `string_at(va)` |
| `xrefs.py` | `CALLMAP` (direct `call`/`jmp rel32` targets → sites), `DWORDMAP` (vtable/table slots pointing into `.text`), `callers(va)`, `datarefs(va)` |
| `funcinfo.py` | profile one function: referenced strings, call targets, immediates |
| `strmap.py` | global string index. `STRINGS` (VA → text, 23 924 of them), `REFS` (string → code sites), `func_strings()`, and `python strmap.py "<regex>"` to grep the binary's strings **with the referencing function names** |
| `names.py` | one-line "what is this function" summary from its strings |

`scan.py` reads `sdk/re/ghidra/functions.csv` (exported by
`ghidra/ExportFunctions.java`) for authoritative function boundaries and names,
and caches its own scans under `_cache/` (delete that dir after re-exporting).

## Report layer

| file | what it does |
|---|---|
| `bitfield_report.py <disp>` | every bit operation on a struct field: which bit, which function, plus a by-bit index. This is what produced the `+0x1F4` / `+0x200` tables in `npc_ai_flags.md`. e.g. `python bitfield_report.py 0x1F4` |
| `writes.py <disp>` | only the *writes* to a struct field |
| `npc_records.py` | authoritative `CreateNPC` (FunkCode tag 0x01) record decoder. Field widths come from the interpreter switch in `FUN_00472bc0`; decodes **99.99 %** of the 64 246 vanilla records byte-exactly. Also handles the `9F EF BE ED FE <name>NUL` symref token (`FUN_00453970`) |
| `npc_flag_corpus.py` | correlates every CreateNPC flag opcode with creature types/bands over all `bin/*/FunkCode.bin` + `StartCode.bin` (deduped by md5) |
| `balance_keys.py` | extracts the balance key → live global-variable table out of the native parser `FUN_005eb010` (227 named floats/ints at `0x00AD4490..0x00AD55A8`) |

## Ghidra side (`sdk/re/ghidra/`)

`run_ghidra.bat <Script.java> [args]` is a thin wrapper over
`analyzeHeadless -process Sacred_decrypted.exe -noanalysis`.

- `ExportFunctions.java` → `functions.csv` (`entry,min,max,size,name`). Re-run
  it after anything that changes the program database.
- `CreateMissingFunctions.java` → reads `missing_funcs.txt` (one hex VA per
  line, produced on the capstone side) and disassembles + creates a function at
  each address not already inside one. This is how `.text` coverage went from
  78.8 % to **93.2 %**; see `text_sweep_2026-09.md`.
- `DecompileFunc.java` / `DecompileForce.java` → `decompiled/<addr>_<name>.c`.
