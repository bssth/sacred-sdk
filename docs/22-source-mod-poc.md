# Path B proof-of-concept — calling Sacred's compiler from our DLL

## The situation, recapped

- `cScriptCompiler` is fully alive in retail Sacred (docs/20).
- Top-level entry `FUN_00671ad0` has **zero static xrefs** — no game code
  path invokes it. The code itself is intact and self-contained.
- The function takes `this` (ECX) and a file-path (stack arg), opens the
  file, tokenises, parses, emits FunkCode bytecode into `this+0/this+4`.
- The first thing it does is `*(int*)this = 0; *(int*)(this+4) = 0;` —
  so it doesn't need a real C++ instance with a vtable. A zeroed buffer
  suffices.

## What just landed

`sdk/source_compiler.cpp` calls `FUN_00671ad0` directly with:
- a 1 KB zero-init buffer as `this` (no vtable, no constructor),
- a file-path argument (via `__fastcall` ABI which is binary-compatible
  with the original `__thiscall`),
- SEH wrapper so a crash inside the compiler doesn't take Sacred down.

The overlay panel "Source compiler (Path B)" has a **Run smoke test**
button that:
1. Writes a minimal source to `custom\scripts\test_script.txt`:
   ```
   pragma resources 0
   exit
   ```
2. Calls `FUN_00671ad0` with that path.
3. Captures the result + the output buffer pointer/size from `this`.

## What we expect to see (and what to do at each outcome)

| Outcome | Meaning | Next step |
|---|---|---|
| log: `'custom\…' -> result=1 bytecode=0x… (N bytes)` with N > 0 | 🎉 compile worked, we have bytecode in our buffer | start growing the source: VarDecl, VarAssign, if/else |
| log: `result=0 bytecode=NULL (0 bytes)` | FUN_00671ad0 exited cleanly but file path didn't resolve OR the source had a parse error | check Sacred's log strings for the parse-error message — that tells us which token tripped up, which narrows grammar |
| log: `SEH: exception 0x… inside compiler` | the compiler tried to read game state we didn't provide (e.g. a global manager pointer) | look at the crash address in the SEH handler and decompile that region to find the dependency |
| log: `FUN_00671ad0 prologue 0xXX unexpected` | `.text` decryption hadn't completed yet | wait — `source_compiler` should be invoked only after `dumptext` reports decryption (same gating as text_logger / patches) |

The smoke test is **manually invoked** from the overlay, not run at
startup, so the user controls timing.

## After the smoke test passes

The natural escalation:

```c
// Inside source_compiler.cpp
int compile_to_funkcode(const char* txt_path, const char* out_bin_path) {
    int ok = compile_file(txt_path);
    if (!ok) return 0;
    void* buf  = *(void**) (g_compiler_inst + 0);
    size_t len = *(size_t*)(g_compiler_inst + 4);
    FILE* f = fopen(out_bin_path, "wb");
    fwrite(buf, 1, len, f);
    fclose(f);
    return 1;
}
```

That gives us **end-to-end `.txt → .bin` compilation through Sacred's own
compiler**. After that:
- Modder edits `custom/scripts/<quest>.txt`
- DLL recompiles on game start (or on overlay button)
- Output goes to `custom/bin/<class>/FunkCode.bin` (or wherever)
- The CreateFileA hook serves the recompiled file to Sacred

A complete **source-level mod pipeline** without ever leaving the DLL.

## Why this is the elegant path

- Round-trip safety: we use Sacred's OWN compiler, so any source that
  compiles WILL produce game-valid bytecode (no risk of malformed
  records).
- No re-implementation: we don't write our own lexer/parser/codegen, we
  delegate to ~12 KB of compiler code Sacred already ships.
- Trivially extensible: when Sacred's grammar supports a feature, we
  support it. When it doesn't, the compiler tells us with its built-in
  parse-error messages (line numbers and all).

## Known unknowns at first smoke test

- **File-loading path inside the compiler**: FUN_00671ad0 → FUN_0066fa60
  → FUN_0065aea0 → some open primitive (probably CreateFileA via the
  CRT). Our fs_override hook should pick this up. If it doesn't, we'll
  see the log message "compiling 'X' failed" and have to trace further.
- **Resource-registration dependency**: `pragma resources 0` should be
  the cheapest valid script. If the compiler still wants something we
  don't provide (e.g. a current cScriptResourceMgr pointer), we'll see
  it in the SEH outcome and patch it up.
- **Output buffer ownership**: who owns the memory at `this[0]` after
  compile? Sacred's normal path probably copies it elsewhere or keeps it
  in a long-lived resource pool. For us, the buffer is in process heap
  (allocated by Sacred's `operator new` inside the compiler). Lifetime
  spans our process — fine for a snapshot.

## Build status

```
sdk/source_compiler.cpp   ← new
sdk/sdk.h                 ← `sdk::source_compiler` namespace declarations
sdk/overlay.cpp           ← "Source compiler (Path B)" header + button
sdk/SacredSDK.vcxproj     ← compiles the new file
```

Built clean. `ijl15.dll` deployed to game dir. Next step: launch Sacred,
wait for `.text` decryption + patches install, click the smoke-test
button, read the log line.
