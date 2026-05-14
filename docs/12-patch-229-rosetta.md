# The 2007 unofficial-patch 2.29 — Rosetta stone

Source: `Sacred-Patch Source.txt`, by Thorium (SacredVault), 2007-01-29.
Companion: `Patcher Source.pb` (PureBasic source of the installer).

**The 2.29 patch is the foundation of ReBorn HD** — everything ReBorn does is
"these 7 fixes from 2007 plus a decade of additional changes." Having the
source means we don't have to bindiff our way to enlightenment; we know
exactly what was changed and why.

## The seven patches

### Change 1 — Load `global.res` from disk instead of PE BINARY resource 107
**Original**: caller at `0x0080DC09` invokes `FUN_0080e680` to decode embedded
PE resource 107 (`FindResourceA(NULL, 0x6B, "BINARY")` + chained-XOR via
`last_word ^ 0x45AD`, then propagating).

**Patch**: replaces ~106 bytes at `0x0080DC09..0x0080DCCB` with inline
`CreateFileA("...\\scripts\\<lang>\\global.res") + GetFileSize + ReadFile`,
keeping the SEH frame. PE BINARY resource 107 can then be **deleted** — which
is exactly why ReBorn HD's `.rsrc` is **2.7 MB smaller** than vanilla.

**For us (DLL hook)**: hook `FUN_0080e680` at `va:0x0080E680`. When called,
return the disk file's bytes instead of the resource. We already decoded the
exact return contract (`puVar4 = operator_new(size); *in_ECX = puVar4;
in_ECX[1] = size;`). Even simpler: hook `FindResourceA` with `lpName=0x6B,
lpType="BINARY"` and return a fake HRSRC backed by our disk read.

This single hook unlocks:
- Runtime language switching (read `scripts/<lang>/global.res` from any
  path; pick `lang` in the ImGui overlay).
- Text mods without repacking `.rsrc`.
- Live reload for translation iteration.

### Change 2 — NOP console-cheat path (frees space)
**Original**: `0x0061561B..0x00615FA7` contains the in-game console cheat
dispatcher. Patch NOPs the jump and treats the body as 2284 bytes of free
working memory for new code.

**For us**: irrelevant. We're a DLL — we have all the memory we want. Skip.

### Change 3 — Easter Egg
Empty section in the source. Nothing to extract.

### Change 4 — Chat-crash fix
**Original**: at `0x0084AE81` Sacred reads chat-packet data with no validity
check; corrupted packets in MP crash the process.

**Patch**: replaces 2 bytes at `0x0084AE81` with `jmp 0x006156DE`. The
trampoline at `0x006156DE` (carved out of the Change-2 free zone) calls
`IsBadReadPtr(dataAddr, dataSize)` first, falls through to the original code
if readable, or logs `"Sacred.exe Crash fixed!"` and returns to the
post-instruction address `0x0084AEAB` if not.

**For us**: low priority (single-player doesn't trigger it). When we do MP:
MinHook on the chat-handler function with the same IsBadReadPtr wrap.

### Change 5 — Version display "2.29.12.0"
Cosmetic only — main menu shows new version, but internal `VL_VERSION` stays
2.28 so multiplayer lobby still accepts the client.

**For us**: trivial when we feel like it. Hook the UI render function or
patch the version string at `0x0095D014` directly. Lowest priority.

### Change 6 — Debugger-freeze fix — **directly relevant to our black-screen**
**Original**: at `0x00810B0F` Sacred has a 2-byte instruction that forces
window focus right after the display-mode change. Under a debugger (or any
condition where focus assignment fails), this hangs the process.

**Patch**: NOP those 2 bytes.

**Important context**: this is *the same bug* that bit us when we tried to
force-borderless. We hooked `ChangeDisplaySettingsA` to swallow the call;
Sacred's force-focus afterwards landed in undefined state and we got the
black screen + frozen taskbar. 2007 already knew about this!

**For us — highest priority technical fix**:
1. Find the corresponding instruction in our build (Steam 2.28 ASE 2006-10-13).
   The address `0x00810B0F` may not match byte-for-byte but the function is
   close: search a window around it for the call to `SetForegroundWindow`,
   `SetFocus`, or `BringWindowToTop` immediately after `ChangeDisplaySettings`.
2. Write a 2-byte `90 90` over those bytes from our DLL during init.

### Change 7 — Hardcoded `.\Bin\Balance.bin` path
**Original**: at `0x00856A8A` Sacred builds Balance.bin's path from the
working directory, which conflicts when running two Sacred processes from
one folder (Change 2 enables multi-window via mutex removal).

**Patch**: jmp at `0x00856A8A` to a trampoline at `0x00615717` that
overrides the path argument to a hardcoded `.\Bin\Balance.bin` whenever the
caller asked for that specific file.

**For us**: irrelevant unless we care about running multiple Sacred copies
from one install. Skip.

## What about our build vs theirs?

Verified `tools/verify_229_patches.py`: the byte patterns at the exact 2007
patch addresses **do not match** because Steam Gold 2.28 ASE (2006-10-13)
has a slightly different binary layout from the German 2.28 the patch was
written for. **However**:

- `FUN_0080e680` matches **byte-for-byte** at `va:0x0080E680` — the PE BINARY
  loader IS the same.
- Function `0x00856A95` (Balance.bin path build) is **11 bytes shifted** vs
  patch's `0x00856A8A` — same function, slightly different prologue.
- The string compare anchor at `0x0094C4D4` has different content (us:
  `"VL_TIME_TAG"`, them: probably `".\\Bin\\Balance.bin"`).

So **all 7 patches are conceptually transferable** — we just need to
**re-discover the sites** in our build:

| Patch | How to relocate on our build |
|-------|------------------------------|
| 1 | `FUN_0080e680` already found at `0x0080E680` — find its caller(s) for Change 1's hijack point, or just replace the loader's body |
| 4 | Search for `recv`/chat packet handler near `0x0084AE81`, identify by string xrefs |
| 5 | Search for the version string `"2.28"` or similar in `.data`, find xref |
| 6 | **PRIORITY**: search for `SetForegroundWindow` / `SetFocus` calls near `ChangeDisplaySettingsA` site |
| 7 | Search for `.\\Bin\\Balance.bin` or `Balance.bin` literal in `.rdata`/`.data`, find xref |

## Plan derived from this

**Priority A — actionable now:**
1. **Hook `FUN_0080e680` (Change 1)** — runtime `global.res` from disk. Single
   most valuable feature; unlocks language switching + text mods.
2. **Relocate + NOP focus-force (Change 6)** — kills the black-screen risk
   forever, makes future DDraw / display hooks safe to attempt.

**Priority B — nice to have:**
3. Display-version cosmetic (Change 5)
4. Chat-crash wrap (Change 4) — only when we touch MP.

**Skip:**
- Change 2 (we don't need their carved-out memory).
- Change 3 (no content).
- Change 7 (single-instance is fine for us).

## Files we now have

| File | What it is |
|------|------------|
| `Sacred-Patch Source.txt` | The seven patches in asm form, with addresses |
| `Patcher Source.pb` | PureBasic source of the patcher installer (TLV diff format: Replace/Cut/Paste opcodes, MD5 verification against German 2.28) |
| `tools/verify_229_patches.py` | Verifies which patch sites match our build (most don't, on byte level) |

Originals archived in `sdk/docs/refs/` if we ever need to consult them
verbatim.
