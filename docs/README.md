# SacredSDK — documentation

If you just want to write a mod, **start here**:

→ **[MODDING_GUIDE.md](MODDING_GUIDE.md)** — the modder-facing manual.

The rest of this directory is a reverse-engineering journal,
preserved in chronological order so anyone who wants to understand
*why* the SDK is shaped the way it is, or wants to extend it into
territory we haven't covered yet, can pick up the thread.

---

## Reading paths

### "I want to write a mod"
1. [MODDING_GUIDE.md](MODDING_GUIDE.md) — installation, examples,
   full Lua API reference.
2. The `custom/lua/examples/` directory in your install — copy-paste
   starters numbered `01_hello.lua` through `07_runtime_triggers.lua`.
3. [roadmap.md](roadmap.md) for what's planned next.

### "I want to understand how SacredSDK works"
Skim these in order — they tell the story of the reverse engineering:

1. [01-exe-recon.md](01-exe-recon.md) — opening pass on Sacred.exe
2. [07-proxy-experiment.md](07-proxy-experiment.md) — first in-process
   foothold via the `ijl15.dll` proxy DLL slot
3. [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md)
   — bypassing Sacred's `.text` encryption
4. [16-interpreter.md](16-interpreter.md) — decoding Sacred's bytecode
   interpreter (`FUN_00472bc0`)
5. [21-custom-overrides.md](21-custom-overrides.md) — the `custom/`
   directory + `CreateFileA` IAT hook that lets mods live without
   touching vanilla
6. [community-refs.md](community-refs.md) — what other community RE
   projects have already mapped (massive cheat sheet)
7. [roadmap.md](roadmap.md) — where we are on the staircase plan

### "I want to dig into a specific Sacred format"
- **FunkCode bytecode**: [03](03-funkcode.md) (framing) →
  [04](04-funkcode-tags.md) (tag table) →
  [05](05-funkcode-grammar.md) (grammar) →
  [06](06-funkcode-types.md) (types) →
  [18](18-funkcode-tag-table.md) (tag reference)
- **Balance.bin**: [02-balance-bin.md](02-balance-bin.md)
- **Quest data** (`global.res` + naming): [08-quests.md](08-quests.md)
  → [10-hash-cracked.md](10-hash-cracked.md)
- **Quest scripts** (decompiled): [17-script-decompilation.md](17-script-decompilation.md)
  → [19-pseudocode.md](19-pseudocode.md)
- **Patch lineage** (Thorium 2.29): [12-patch-229-rosetta.md](12-patch-229-rosetta.md)
  → [13-real-patches.md](13-real-patches.md)
- **Compiler internals** (Sacred's own cScriptCompiler):
  [20-source-language.md](20-source-language.md) →
  [22-source-mod-poc.md](22-source-mod-poc.md)

---

## Full file index

| File | One-line |
|---|---|
| [MODDING_GUIDE.md](MODDING_GUIDE.md) | **Modder-facing manual — read first.** |
| [01-exe-recon.md](01-exe-recon.md) | EXE sections, imports, strings, EP/DRM observation |
| [02-balance-bin.md](02-balance-bin.md) | `bin/Balance.bin` schema draft; Veteran diff |
| [03-funkcode.md](03-funkcode.md) | `bin/TYPE_NPC_*/FunkCode.bin` analysis |
| [04-funkcode-tags.md](04-funkcode-tags.md) | Auto-generated tag table |
| [05-funkcode-grammar.md](05-funkcode-grammar.md) | Payload grammar synthesis |
| [06-funkcode-types.md](06-funkcode-types.md) | Value-type dictionary (53 entries, ~76% coverage) |
| [07-proxy-experiment.md](07-proxy-experiment.md) | First in-process foothold via `ijl15.dll` proxy |
| [08-quests.md](08-quests.md) | Quest decompilation distance + gap analysis |
| [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md) | `.text` encryption bypass; interpreter findings |
| [10-hash-cracked.md](10-hash-cracked.md) | Resource-name hash function recovered |
| [11-first-hook.md](11-first-hook.md) | Player Inspector overlay via pointer chains |
| [12-patch-229-rosetta.md](12-patch-229-rosetta.md) | Recovered Thorium 2007 patch source |
| [13-real-patches.md](13-real-patches.md) | Patch 1 + Patch 6 ported to runtime hooks |
| [14-logger-and-mods.md](14-logger-and-mods.md) | Resource-hash logger + first text-mod CLI |
| [15-quest-dumper.md](15-quest-dumper.md) | Bulk quest text dumper (332 KB markdown) |
| [16-interpreter.md](16-interpreter.md) | Bytecode interpreter structural decode |
| [17-script-decompilation.md](17-script-decompilation.md) | End-to-end script pipeline + 12.5 MB bulk dump |
| [18-funkcode-tag-table.md](18-funkcode-tag-table.md) | Full TAG → subsystem reference (79/112 labelled) |
| [19-pseudocode.md](19-pseudocode.md) | Pseudo-code decompilation (~95% opcode coverage) |
| [20-source-language.md](20-source-language.md) | Recovered FunkCode source-language grammar |
| [21-custom-overrides.md](21-custom-overrides.md) | `custom/` override + CreateFileA IAT hook |
| [22-source-mod-poc.md](22-source-mod-poc.md) | Path B: call Sacred's own compiler from our DLL |
| [community-refs.md](community-refs.md) | Mined intel from community RE projects + wishlist |
| [roadmap.md](roadmap.md) | Where we are on the staircase plan |
| [TOOLS_PLAN.md](TOOLS_PLAN.md) | Curation plan for the Python `tools/` directory (next public push) |

---

## Conventions

- Hex offsets are file-relative unless prefixed `va:` (then they are
  virtual addresses inside `Sacred.exe`, image base `0x00400000`).
- "Vanilla" = stock Steam files. "Veteran" = Veteran mod (`Balance.bin`
  diff, see [02](02-balance-bin.md)).
- The Steam install is never modified. All mods live under `<game>/custom/`.
- Build target verified: Sacred Gold Steam edition, build **2.0.2.28**
  (binary date 2006-10-13).
