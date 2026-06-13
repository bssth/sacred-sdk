# Path B proof-of-concept — calling Sacred's compiler from the DLL

## Background

- `cScriptCompiler` is intact in retail Sacred (docs/20).
- Top-level entry `FUN_00671ad0` has zero static xrefs — no game code path
  invokes it. The code is intact and self-contained.
- The function takes `this` (ECX) and a file-path (stack arg), opens the
  file, tokenises, parses, emits FunkCode bytecode into `this+0/this+4`.
- It begins with `*(int*)this = 0; *(int*)(this+4) = 0;` — so it needs no
  real C++ instance with a vtable. A zeroed buffer suffices.

## Implementation

`sdk/source_compiler.cpp` calls `FUN_00671ad0` directly with:
- a 1 KB zero-init buffer as `this` (no vtable, no constructor),
- a file-path argument (via `__fastcall` ABI, binary-compatible with the
  original `__thiscall`),
- an SEH wrapper so a crash inside the compiler doesn't take Sacred down.

The overlay panel "Source compiler (Path B)" has a Run smoke test button
that:
1. Writes a minimal source to `custom\scripts\test_script.txt`:
   ```
   pragma resources 0
   exit
   ```
2. Calls `FUN_00671ad0` with that path.
3. Captures the result + the output buffer pointer/size from `this`.

## Expected outcomes

| Outcome | Meaning | Next step |
|---|---|---|
| log: `'custom\…' -> result=1 bytecode=0x… (N bytes)` with N > 0 | compile worked, bytecode in buffer | grow the source: VarDecl, VarAssign, if/else |
| log: `result=0 bytecode=NULL (0 bytes)` | FUN_00671ad0 exited cleanly but file path didn't resolve OR the source had a parse error | check Sacred's log strings for the parse-error message — identifies which token tripped, narrowing the grammar |
| log: `SEH: exception 0x… inside compiler` | the compiler tried to read game state not provided (e.g. a global manager pointer) | decompile the crash address from the SEH handler to find the dependency |
| log: `FUN_00671ad0 prologue 0xXX unexpected` | `.text` decryption hadn't completed yet | wait — `source_compiler` should be invoked only after `dumptext` reports decryption (same gating as text_logger / patches) |

The smoke test is manually invoked from the overlay, not run at startup, so
the user controls timing.

## After the smoke test passes

Escalation:

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

That gives end-to-end `.txt → .bin` compilation through Sacred's own
compiler. Then:
- Modder edits `custom/scripts/<quest>.txt`
- DLL recompiles on game start (or on overlay button)
- Output goes to `custom/bin/<class>/FunkCode.bin` (or wherever)
- The CreateFileA hook serves the recompiled file to Sacred

A source-level mod pipeline without leaving the DLL.

## Properties of this path

- Round-trip safety: uses Sacred's own compiler, so any source that compiles
  produces game-valid bytecode (no risk of malformed records).
- No re-implementation: delegates to ~12 KB of compiler code Sacred already
  ships, rather than writing a lexer/parser/codegen.
- Extensible: feature support tracks Sacred's grammar. Unsupported features
  surface via the compiler's built-in parse-error messages (with line
  numbers).

## Known unknowns at first smoke test

- File-loading path inside the compiler: FUN_00671ad0 → FUN_0066fa60 →
  FUN_0065aea0 → some open primitive (probably CreateFileA via the CRT). The
  fs_override hook should pick this up. If not, the log shows
  "compiling 'X' failed" and the path needs further tracing.
- Resource-registration dependency: `pragma resources 0` should be the
  cheapest valid script. If the compiler still wants something not provided
  (e.g. a current cScriptResourceMgr pointer), it shows in the SEH outcome.
- Output buffer ownership: who owns the memory at `this[0]` after compile.
  Sacred's normal path probably copies it elsewhere or keeps it in a
  long-lived resource pool. Here the buffer is in process heap (allocated by
  Sacred's `operator new` inside the compiler). Lifetime spans the process —
  fine for a snapshot.

## Build status

```
sdk/source_compiler.cpp   ← new
sdk/sdk.h                 ← `sdk::source_compiler` namespace declarations
sdk/overlay.cpp           ← "Source compiler (Path B)" header + button
sdk/SacredSDK.vcxproj     ← compiles the new file
```

Built clean. `ijl15.dll` deployed to game dir. Next step: launch Sacred,
wait for `.text` decryption + patches install, click the smoke-test button,
read the log line.
