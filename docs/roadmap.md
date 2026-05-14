# Roadmap — staircase plan

Iterative path from zero to a hooked-DLL mod. Each step has standalone value; we can stop at any rung.

| # | Step | Status | Output |
|---|---|---|---|
| 0 | Hygiene: work on a copy, hash exe, note version | ✅ done | `Sacred.exe` SHA-256 `4df66593…91cd`, build 2006-10-13 |
| 1 | Passive recon: strings, imports, sections | ✅ done | [01-exe-recon.md](01-exe-recon.md) |
| 2 | Behavioural recon: ProcMon trace at startup / ingame | ⏳ todo | which `.bin` and `.pak` are hot |
| 3a | Data modding — `Balance.bin` | 🟡 schema sketched | [02-balance-bin.md](02-balance-bin.md); Veteran is a 40-byte float diff |
| 3b | Data modding — `FunkCode.bin` (script bytecode) | 🟡 grammar pinned, 53-type dict, 76 % byte coverage | [03-funkcode.md](03-funkcode.md), [04-funkcode-tags.md](04-funkcode-tags.md), [05-funkcode-grammar.md](05-funkcode-grammar.md), [06-funkcode-types.md](06-funkcode-types.md) |
| 4 | DLL-proxy on `ijl15.dll` (4 imports, easy proxy) | ✅ live — DllMain confirmed inside `Sacred.exe` | [07-proxy-experiment.md](07-proxy-experiment.md) |
| 5 | Ghidra triage of `Sacred.exe` | ✅ ENCRYPTION BYPASSED — decrypted `.text` dumped via SacredSDK, spliced, Ghidra now fully usable | [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md) |
| 6 | First MinHook hook from the proxy DLL | ⏳ todo | log struct on item drop |
| 7 | Larger surgery (subsystem wrap) | ⏳ stretch | replace e.g. loot table loader |

## Free wins already collected (from SacredUtils gift)

- `Sacred.exe CHEATS=1` — built-in cheat mode, no patches required.
- `ch.files.txt` — canonical inventory of every `.bin`/`.pak`/runtime DLL.
- Veteran mod bundle = vanilla + 40-byte float patch on `Balance.bin`.

## Quest decompilation track (added this session)

| # | Step | Status |
|---|---|---|
| Q1 | Quest-name inventory across all 8 classes | ✅ 287 HQ + 242 NQ + 2400 DQ vars |
| Q2 | `global.res` format & resolver | ✅ cracked, 23 123 entries decodable |
| Q3 | Symbolic `res:NAME` → id hash function | ❌ TODO (bruteforce ~2h) |
| Q4 | Low-number `res:1024` table | ❌ TODO (probe context) |
| Q5 | Quest text dumper (title/header/body/log) | 🟡 ready once Q3 done |
| Q6 | Quest logic decode (steps, conditions, rewards) | ❌ needs FunkCode tag semantics |

See [08-quests.md](08-quests.md).

## Open questions

1. What exactly does `CHEATS=1` enable? (key bindings, console, god mode, ...) — needs runtime test.
2. Steam install is missing `protect.dll` listed in canonical inventory — is `.bind` section neutered or just a dead stub?
3. `FunkCode.bin` Addon copies are byte-identical across 8 classes — does the game still load from `bin\Addon\<class>\FunkCode.bin` or does the loader skip the class path entirely for Addon?
5. Are `FunkCode.bin` records consumed live at runtime or compiled into runtime structs at startup? Determines whether we can hot-edit them.
6. The full tag alphabet (~70 ASCII + many non-ASCII) — what's each kind?
4. `scripts\us\global.res` (2.7 MB) — likely localization but might also carry scripted content.
