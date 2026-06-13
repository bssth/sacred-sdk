# Roadmap — staircase plan

Iterative path from zero to a hooked-DLL mod. Each step has standalone value; work can stop at any rung.

| # | Step | Status | Output |
|---|---|---|---|
| 0 | Hygiene: work on a copy, hash exe, note version | Done | `Sacred.exe` SHA-256 `4df66593…91cd`, build 2006-10-13 |
| 1 | Passive recon: strings, imports, sections | Done | [01-exe-recon.md](01-exe-recon.md) |
| 2 | Behavioural recon: ProcMon trace at startup / ingame | Planned | which `.bin` and `.pak` are hot |
| 3a | Data modding — `Balance.bin` | Partial — schema sketched | [02-balance-bin.md](02-balance-bin.md); Veteran is a 40-byte float diff |
| 3b | Data modding — `FunkCode.bin` (script bytecode) | Partial — grammar pinned, 53-type dict, 76 % byte coverage | [03-funkcode.md](03-funkcode.md), [04-funkcode-tags.md](04-funkcode-tags.md), [05-funkcode-grammar.md](05-funkcode-grammar.md), [06-funkcode-types.md](06-funkcode-types.md) |
| 4 | DLL-proxy on `ijl15.dll` (4 imports, easy proxy) | Done — DllMain confirmed inside `Sacred.exe` | [07-proxy-experiment.md](07-proxy-experiment.md) |
| 5 | Ghidra triage of `Sacred.exe` | Done — encryption bypassed; decrypted `.text` dumped via SacredSDK, spliced, Ghidra usable | [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md) |
| 6 | First MinHook hook from the proxy DLL | Planned | log struct on item drop |
| 7 | Larger surgery (subsystem wrap) | Planned (stretch) | replace e.g. loot table loader |

## Wins collected from SacredUtils

- `Sacred.exe CHEATS=1` — built-in cheat mode, no patches required.
- `ch.files.txt` — inventory of every `.bin`/`.pak`/runtime DLL.
- Veteran mod bundle = vanilla + 40-byte float patch on `Balance.bin`.

## Quest decompilation track

| # | Step | Status |
|---|---|---|
| Q1 | Quest-name inventory across all 8 classes | Done — 287 HQ + 242 NQ + 2400 DQ vars |
| Q2 | `global.res` format & resolver | Done — cracked, 23 123 entries decodable |
| Q3 | Symbolic `res:NAME` → id hash function | Done — see [10-hash-cracked.md](10-hash-cracked.md) |
| Q4 | Low-number `res:1024` table | Planned (probe context) |
| Q5 | Quest text dumper (title/header/body/log) | Done — see [15-quest-dumper.md](15-quest-dumper.md) |
| Q6 | Quest logic decode (steps, conditions, rewards) | Done — see [17-script-decompilation.md](17-script-decompilation.md) |

See [08-quests.md](08-quests.md).

## Open questions

1. What does `CHEATS=1` enable? (key bindings, console, god mode) — needs runtime test.
2. Steam install is missing `protect.dll` from the canonical inventory — is `.bind` neutered or a dead stub?
3. `FunkCode.bin` Addon copies are byte-identical across 8 classes — does the game load from `bin\Addon\<class>\FunkCode.bin` or skip the class path for Addon?
4. Are `FunkCode.bin` records consumed live at runtime or compiled into runtime structs at startup? Determines whether they can be hot-edited.
5. The full tag alphabet (~70 ASCII + many non-ASCII) — what is each kind?
6. `scripts\us\global.res` (2.7 MB) — likely localization, may also carry scripted content.
