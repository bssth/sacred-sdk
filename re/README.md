# sdk/re/ — reverse-engineering assets (offline, never runtime)

The old `sdk/tools/` folder was dissolved here (2026-06-13) once its **only**
runtime coupling — the `python.exe` FunkCode round-trip in `script_mods.cpp` —
was ported to native C++ (`sdk/funkcode.cpp`). Nothing in this tree is loaded by
the injected DLL at runtime; it's all developer tooling + RE knowledge.

```
re/
  py/            offline dev scripts (CLIs + generators), still runnable:
                   funkcode_*.py (disasm/decompile/grammar/roundtrip CLIs — the
                     runtime path is now C++; these stay as the dev oracle),
                   globalres_*.py, sacred_hash.py, hash_*.py, quest_*.py,
                   gen_item_lua.py / gen_npc_lua.py (emit checked-in custom/lua/lib/*.lua),
                   hash_names.csv, README.md
    _archive/    one-off RE-archaeology scripts (recon/interpreter/reborn/verify_*)
  ghidra/        headless RE tooling + corpus:
                   *.java (DecompileFunc, XrefsTo, FindStrings, …), run_headless.bat,
                   decompiled/  — 298 per-function Ghidra C exports (git-ignored,
                                  regenerable from Sacred.exe + the Java scripts)
```

RE knowledge docs (the 27 canonical specs) live under
`sdk/.claude/knowledge/re/` (with `_archive/` and `fixtures/`), indexed by
`.claude/knowledge/INDEX.md`.

The FunkCode opcode table is shared between the Lua baker (`lua_bake.cpp`) and
the native (de)compiler (`funkcode.cpp`) via `sdk/lua_bake_opcodes.inc` — the
single source of truth. `py/funkcode_roundtrip_test.py` remains the dev-side
regression oracle (the C++ port passes 20/20 retail bins byte-exact).
