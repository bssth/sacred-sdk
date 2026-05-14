# 09 — Ghidra hit a wall: `.text` is encrypted

Today's reverse-engineering attempt via Ghidra headless ran into a fundamental obstacle. Documenting it carefully so we don't burn time on the same shape twice.

## What we tried

Ghidra 12.1 headless analysis of `Sacred.exe`:

```
analyzeHeadless sdk\ghidra sacred ^
  -import Sacred.exe -postScript FindStrings.java -scriptPath sdk\tools\ghidra
```

Auto-analysis completed in 79 s (well under the 5–15 min estimate — modern hardware on a 12 MB binary). All 30+ analyzers reported success. Project saved.

## What didn't work

Cross-reference lookup for every string of interest returned **zero references**, both via Ghidra's `ReferenceManager` and via raw byte-search for the string's 4-byte LE address in any section:

```
=== 'FunkCode.bin' ===
  string @ 0094e850 : %s\FunkCode.bin
    refs: analyzed=0  byte-scan=0
=== 'Bin\Balance.bin' ===
  string @ 009e3ae4 : Bin\Balance.bin
    refs: analyzed=0  byte-scan=0
...
```

Even `ItemDataMgr::saveHero(%d)` and the other obvious printf-format strings — used dozens of times in the codebase — show zero references.

## Why

Section entropies told the truth:

| Section | Range | Entropy (whole) | Verdict |
|---|---|---|---|
| `.text`   | `0x401000..0x88FFFF` | **8.000** | encrypted (maximum entropy = uniform random) |
| `.rdata`  | `0x890000..0x8E6FFF` | 4.45 | normal — string tables, RTTI etc. |
| `.data`   | `0x8E7000..0x183179B` | 3.67 | normal — initialized globals |
| `.rsrc`   | `0x1832000..0x1D6BFFF` | (mid) | normal — resources |
| `.bind`   | `0x1D6C000..0x1D6DFFF` | 5.11 | normal — the SafeDisc stub itself |

The first 64 bytes of `.text` on disk:

```
7e 13 da 6f bf f3 df 6a bf 81 51 6a 7c 11 c1 fa
f7 55 e5 fe d2 aa 1a fe d2 6b fa fb d7 2b 82 75
d7 e8 12 e5 47 78 82 75 d7 e8 12 e5 47 78 82 75
2d 87 ea a7 81 02 ea c3 20 02 ea c3 20 52 8e 4a
```

This is not x86 — no recognisable prologue, repeating opcodes, or call patterns. Combined with EP = `0x196c3db` (inside `.bind`, not `.text`) and the canonical `protect.dll` being absent in the Steam install, this is a classic **SafeDisc / SecuROM-style runtime decrypter**:

1. PE loader maps the EXE.
2. PE loader runs `DllMain` of every imported DLL (including our `ijl15.dll` proxy).
3. PE loader transfers control to EP = `0x196c3db` inside `.bind`.
4. `.bind` stub iterates through `.text` and decrypts it in-memory.
5. Stub jumps to the real Sacred entry point (somewhere in the now-decrypted `.text`).

Until step 4 finishes, `.text` is the encrypted blob Ghidra is staring at. Static analysis of that blob is information-theoretically meaningless: every byte is uniformly random.

## Implication for the project

Ghidra-as-static-decompiler is **blocked** until we can feed it the decrypted code. All our advance-planning of scripts (decompile parser, find resource hash, find tag-switch table) is fine — they just need a Sacred image with the real `.text`.

## Plan: dump decrypted `.text` via SacredSDK

We already inject into Sacred (`ijl15.dll` proxy, see [07-proxy-experiment.md](07-proxy-experiment.md)). The new step is small:

1. From `DllMain`, spawn a worker thread.
2. The thread sleeps ~1 second so the `.bind` stub has finished decrypting.
3. Thread reads `[0x401000, 0x88FFFF]` from process memory (it's our own process — `memcpy` from those virtual addresses).
4. Writes the bytes to `sdk\logs\text_dump.bin`.
5. Game continues running normally.

Then on the analysis side:

1. Read the original `Sacred.exe` from disk into a buffer.
2. Splice the dumped decrypted `.text` bytes into the buffer at `.text`'s raw-file offset (0x1000, size 0x48F000).
3. Save as `sdk\Sacred_decrypted.exe`.
4. Re-import that into Ghidra. Auto-analysis now sees real code → references work → decompiler is usable.

Optional refinements:
- Hash-check that the dumped bytes are actually unencrypted (e.g. entropy < 6.5, or check for `55 8B EC` prologues).
- If the stub also decrypts parts of `.rdata` (some protectors do), dump those too. The 4.45 entropy of `.rdata` suggests it's already clear, but doesn't hurt to verify.
- Time the decryption: instead of `Sleep(1000)`, poll the first 64 bytes of `.text` until they look like x86 (e.g., first byte is `0x55` for `push ebp` or first 3 bytes are `8B FF`).

## Side observation: sanity check

```
function Catch@00404fc1 @ 00404fc1  refs=1
    <- 008a60b4
```

Ghidra's reference machinery does work — Catch (a tiny SEH thunk in encrypted-but-pattern-matching territory) shows a reference. The reference at `008a60b4` is inside `.rdata`, in the structured-exception unwind tables. Those references resolve fine because they're encoded in metadata Ghidra can read independently of `.text` content. They tell us nothing about real code flow.

## Honest re-estimate of the staircase

| Step | Status | Update |
|---|---|---|
| 5  Ghidra triage | 🔴 blocked until `.text` decrypted | unblock via the dump-via-DLL plan above |
| Q3 Symbol hash (`res:HQ_*` resolver) | 🔴 was hoped to come from Ghidra | same: needs decrypted `.text` |
| Q6 Quest logic decode | 🔴 same dependency | same |
| 3a Balance.bin tweaks | ✅ doable without Ghidra | unchanged |
| Q5 Quest text dump via global.res | 🟡 still pending hash | could also bruteforce hash externally |

Two viable parallel tracks now:

1. **Decrypted-text track:** SacredSDK dumps `.text`, we splice, re-import, Ghidra works. Estimated effort: 1 evening for the dump code + splice tool. After that, the original plan (find FunkCode parser, find `res:` resolver) is straight-line.
2. **External-bruteforce track:** ignore Ghidra, write a script that hashes our 982 known symbol names with ~10 common hash functions, intersects with global.res's id set. Cheaper if the hash is standard.

Both tracks are independently viable; doing the decrypt one ALSO solves Ghidra-related future work (FunkCode tag semantics, all other reverse needs). Doing bruteforce first lets us start dumping quest texts tonight if the hash is FNV or DJB.

## Status of both tracks (this session)

### Track 2 — hash bruteforce: ❌ negative result

`sdk\tools\hash_bruteforce.py` ran 10 standard hash functions (djb2, djb2_xor, sdbm, fnv1a32, fnv1_32, crc32, adler32, jenkins lookup3, murmur3_32, loose_sum) × 7 input variants (as-is, lower, upper, +null, upper+null, with `res:` prefix, with `res:` prefix + null) over 13 631 candidate names extracted from FunkCode.bin. Best result: **2 hits** out of ~95 000 attempts. At ~5×10⁻⁶ random collision rate against the 23 123-entry id set, the expected noise-level hits per variant is ~0.5. The top results are statistical noise.

Conclusion: Sacred uses a **non-standard / custom hash function** for symbolic `res:` lookups. We won't get the hash without reading the binary. Track 2 closed; Track 1 is the only viable path.

### Track 1 — SacredSDK `.text` dumper: ✅ implemented

New module: `sdk\dump_text.cpp` (added to vcxproj, namespace `sdk::dumptext`). It:

1. Spawns a worker thread from `DllMain` (alongside `hooks::install()` and `overlay::start()`).
2. Polls a 16 KB sample at `.text + 0x100000` every 200 ms, computing Shannon entropy. While encrypted, entropy is ≈ 8.000; after the `.bind` stub decrypts, it drops to ~5.5–6.5.
3. As soon as entropy < 7.0 (or after 6 s of polling, fallback), dumps exactly `0x48F000` bytes from `0x401000` to `sdk\logs\text_dump.bin`.
4. Logs each poll's entropy and the final write to `sdk\logs\sdk_loaded.log`.

Companion splice tool: `sdk\tools\splice_decrypted.py`. Reads the original `Sacred.exe`, replaces the encrypted `.text` (raw offset `0x1000`, raw size `0x48F000`) with the decrypted bytes from the dump, and writes `sdk\Sacred_decrypted.exe`. Includes sanity checks:
- original `.text` entropy should be ≥ 7.5 (encrypted)
- dumped bytes should be in 5.5–6.5 range (real code)
- spliced output should contain ≥ 100 instances of `55 8B EC` (the canonical MSVC function prologue)

Ghidra launcher updated: `run_headless.bat` now supports `import-decrypted` and `process-decrypted` modes, working against a sibling project `sacred_decrypted` so the encrypted import remains available for comparison.

### Activation pipeline

```cmd
:: 1. Launch Sacred briefly (Steam or smoke-test). Stay >= 4 seconds to give
::    dump_text's worker time to fire.

:: 2. Check the dump exists.
dir sdk\logs\text_dump.bin

:: 3. Splice into Sacred_decrypted.exe (verifies entropy).
python sdk\tools\splice_decrypted.py

:: 4. Re-import to Ghidra (a second project, sacred_decrypted).
sdk\tools\ghidra\run_headless.bat import-decrypted

:: 5. Run analysis scripts against the decrypted project.
sdk\tools\ghidra\run_headless.bat process-decrypted FindStrings.java
```

After step 5, the FindStrings report should show meaningful xref counts and function names. From there: decompile the FunkCode loader (`%s\FunkCode.bin` xref), the resource resolver (`:res:%d` xref), and the value-type switch table — all the targets that the encrypted-project couldn't reach.

## Live results (this session)

Dump pipeline executed end-to-end:

```
[dump] poll #1 entropy=7.985      ← .text still encrypted on first sample
[dump] poll #2 entropy=6.229      ← .bind stub finished, real x86 code visible
[dump] wrote 4 780 032 / 4 780 032 bytes to sdk\logs\text_dump.bin  (final entropy 6.229)
```

Splice: 714 canonical `55 8B EC` (push ebp / mov ebp, esp) prologues found in spliced .text. `sdk\Sacred_decrypted.exe` written, 11 927 552 bytes (= original).

Ghidra import of decrypted EXE: 665 seconds (vs 79 s for encrypted — real auto-analysis finally had work to do). Project name: `sacred_decrypted` (sibling of `sacred`).

FindStrings against `sacred_decrypted` now returns **real cross-references**:

| String | Function (entry) | Notes |
|---|---|---|
| `%s\FunkCode.bin` | `FUN_0046f9b0` | per-class file loader, also handles StartCode/Vectoren/Quest{,Pool}Code/DefPos |
| `Bin\Balance.bin` | `FUN_00803920`, `FUN_00711ee0` | two callers (init + reload?) |
| `.\SCRIPTS\%s\global.res` | `FUN_00817b30` (= WinMain) | builds the path, hands off |
| `Scripts\Balance.txt` | `FUN_00815f70` | Game init function (called from WinMain) |
| `:res:%d` | `FUN_00472bc0` | likely the `printf` format for an id resolver |
| `ItemDataMgr::saveHero(...)` × 16 | `FUN_00604690` | the saveHero method |
| `CHEATS ENABLED/DISABLED` | various | cheat-flag handling |

## What we learned by decompiling three functions

**`FUN_00817b30` (WinMain, 226 decompiled lines)** — single-instance mutex check, Win9x-version error path, Settings.cfg load, locale selection by iterating `DAT_00899394 .. 0x899444` in 0x10 strides (~12 locale entries), `sprintf(buf, ".\SCRIPTS\%s\global.res", locale)`, then call to `FUN_0080e680(buf)` and the main message loop.

**`FUN_0080e680` (65 lines)** — NOT a disk loader. Calls `FindResourceA(NULL, /*id*/ 0x6b, /*type*/ "BINARY")`, decrypts the resource with a chained-XOR pass (last word XOR 0x45ad, then `w[i] ^= w[i+1]` backwards). Loads an **embedded PE resource**, not `global.res`. Implications:
  - The Sacred PE has encrypted BINARY resources in `.rsrc` with small numeric IDs.
  - The "missing" `res:1024`, `res:17631`, etc. that we couldn't find in `global.res` are likely PE resource IDs of this kind, not file-index hashes. Different namespace.

**`FUN_0046f9b0` (1227 lines, the per-class script loader)** — builds the paths and calls a generic file-into-buffer loader for each of:
  - `StartCode.bin` → stored at `this->buffer[0]`
  - `FunkCode.bin`  → stored at `this->buffer[4]`
  - `QuestCode.bin` → stored at `this->buffer[8]`
  - `QuestPoolCode.bin` → stored at `this->buffer[12]`
  - `DefPos.bin` → handled specially (reads 4 bytes, expects version magic `0x4D2 = 1234`)
  - `Vectoren.bin` → similar pattern

  Reusable primitives discovered:
  - `FUN_00849ace(filename) → byte_buffer*` — equivalent of "load file into heap" / file_into_memory.
  - `FUN_00849986(buf, &out, size, count)` — `fread`-equivalent for the in-memory cursor model.
  - `FUN_0084a5e0(buf, &out, size, count)` — second fread variant; signatures slightly differ.

  **This function does NOT parse FunkCode records.** It only loads the bytes into memory and stores the buffer pointer in a script-context struct. The TLV walker we want is somewhere else — called when scripts execute, probably from a `cInterpretSQW::*` method or similar.

## Status of the staircase

| Step | Status now |
|---|---|
| 5  Ghidra triage | ✅ working on decrypted image; xrefs + decompile produce sensible output |
| Q3 Symbol hash (`res:HQ_*` resolver) | 🟡 next: find the function that hashes cstr → u32 (probably called from `FUN_00472bc0` neighborhood, or from `FUN_0080e680`'s sibling that loads `global.res` from disk) |
| Q4 Low-id `res:NNN` | 🟡 hypothesis: PE resource IDs (FindResourceA + chained XOR). Verifiable by extracting `.rsrc` resource id 0x6b and checking entropy/contents. |
| Q5 Quest text dumper | 🟡 unblocked: once we have either the hash or the PE-resource mapping, we can render quest text. |
| Q6 Quest logic decode | 🟡 need to find FunkCode interpreter (search for "tag-byte switch with 60+ cases" near cInterpretSQW methods) |

## 🎯 FunkCode interpreter located

`FUN_00472bc0` — 10 610 bytes, 2521 decompiled lines — turns out to be the **FunkCode interpreter / type dispatcher**. Inside there's one giant `switch` on the leading byte with cases for **every tag and value-type we identified empirically**:

```c
case 1:  case 0x29:  case 99:  case 0x68:  case 0x69:  case 0x6a:  case 0x9d:  ...
case 2:  case 3:  case 10:  case 0x6b:  case 0x6c:  case 0x9b:  case 0x9c:
case 4:  case 0xc:  case 0xd:
case 5:  case 9:  case 0x41:  case 0x60:  case 0x83:
case 6 .. 8, 0xe, 0xf, 0x10, 0x12, 0x13, 0x14, 0x1a, 0x1b, ..., 0x91, ...
```

Cases that read 4-byte u32 immediately after the tag and advance `cursor += 5` — i.e. tagged u32 fields:

```c
case 0x1c:  case 0x53:  case 0x54:  case 0x55:  case 0x56:  case 0x57:  case 0x86:
    uVar26 = *(undefined4 *)(uVar16 + 1 + param_2);
    *(undefined4 *)(in_ECX + 0xa860) = uVar26;
    FUN_0045ee20(9, 0, &DAT_0094e958, uVar26);
    *param_1 = *param_1 + 5;
    break;
```

This **confirms** our empirical type-dictionary entries `0x1c`, `0x53`, `0x86` as u32. And it adds the previously-unresolved `0x54`, `0x55`, `0x56`, `0x57` — all u32 too.

**Special case `0x1d` is the resource resolver:**

```c
case 0x1d:
    FUN_00854059(*(undefined4 *)(uVar16 + 1 + param_2), pCVar11, 10);   // itoa-like, base 10
    uVar26 = *(undefined4 *)(*param_1 + 1 + param_2);
    FUN_006725e0(uVar26, 0, &cScriptResource::RTTI_Type_Descriptor,
                 &cScriptResourceDlg::RTTI_Type_Descriptor, 0);
    uVar26 = FUN_006726f0(uVar26);
    local_134 = (char *)FUN_0084a961(uVar26);
    if (local_134 != (char *)0x0) {
        FUN_0045ee20(9, 0, s__res__d_0094e8e4, *(undefined4 *)(*param_1 + 1 + param_2));
        ...
```

I.e. **type `0x1d` carries a u32 that is a resource id**, looked up via `cScriptResource` RTTI and formatted with the `":res:%d"` literal. Previously our inference said `0x1d = u16`; the right answer is `0x1d = u32-resource-id`.

This also locks down where future improvements should live: every script-execution path in the game flows through this dispatcher. Renaming the cases (e.g. `case 0x1d` → "resolveResource", `case 0x86` → "loadGlobalSymbol", etc.) in Ghidra would turn the whole 2521-line C dump into a readable interpreter.

## Mass function renaming (199 symbols recovered)

`RenameByLogString.java` walks every defined string, finds substrings shaped like `ClassName::Method` (or `NS::ClassName::Method`), follows xrefs to the enclosing function, and renames Ghidra's `FUN_xxxxxxxx` to `ClassName_Method_xxxxxxxx` if no prior name exists.

One pass: **354 candidate strings → 199 functions renamed.** Notable recoveries:

```
cInterpretSQW_initRegion, _enterRegion, _exitRegion,
cInterpretSQW_initSector, _enterSector, _exitSector,
cInterpretSQW_Teleporter
cGranny_initialize, _bindTextures, _draw3DPoint,
cGranny_render (× many overloads), _renderShadow, _renderShadowFake
cGrannyModelManager_cGrannyModelManager (ctor), _flushMotion, _getModel,
cGrannyModelManager_grnUsePak, _pakReadHeader, _generateLookup, _loadLookup,
cGrannyModelManager_saveMotionParams, _loadMotionParams
cCreature_inventory_putItem, _equipment_reset, _equipment_equip,
cCreature_pickupItem, _WakeUp
cCreatureHero_gethitvalu, _CalcResults, _debugInfo
cObject_advanceTime, _createPacket
cObject3D_grnSequenceLoad
cObjectFactory_create
cTrigger_setState, _resetState
cCalendarTime_save
cStatsManager_Instance
TypeManager_loadSoundProfiles, _loadItemTypes, _loadWeaponInfo,
TypeManager_getDoorDirection
cObjectFX_updateWorldPositionFromOwner, _dopSystemAction
ItemDataMgr_saveHero
... 139 more
```

After this pass, decompiling any function shows readable callees — e.g. `cObjectManager.getData(...)` instead of `FUN_005fe000(...)`. Every future decompile gets this for free.

## Re-decompile of `FUN_00472bc0` (FunkCode interpreter) with new names

Cases that we can now confidently document:

| Byte | Encoding | Confirmed semantics |
|---|---|---|
| `0x01, 0x29, 0x63, 0x68, 0x69, 0x6a, 0x9d` | cstr | NPC / role name; dispatch on literal strings `"DLG"`, `"DLGNPC"`, `"QUESTFELLOW"`, `"QUESTNPC"`; resolves to a creature via `cObjectManager.getData()` |
| `0x1c, 0x53, 0x54, 0x55, 0x56, 0x57, 0x86` | u32 LE | integer literal (stored into `this->slot_at_0xa860`) |
| `0x1d` | u32 LE | **resource id**, formatted as `":res:%d"`, looked up via `cScriptResource::RTTI_Type_Descriptor` cast |
| `0x6b, 0x6c, 0x9b, 0x9c` | (case 2,3,10 grouped) | TBD — same case body |
| `0x4c, 0x4e, 0x4f, 0x50, 0x51, ...` | (case-group with 60+ tags) | TBD — fall-through tagged operations |

Cases handle ~100 distinct byte values in total. Each case is short (8–40 lines of C); each can be read in a minute. Refining the full type table from here is mostly typing rather than reverse engineering.

Also visible: the interpreter heavily uses `cObjectManager.getData()` to fetch game objects (creatures, items) by id, and RTTI casts (`cScriptResource` / `cCreature` / `cObject`) to type-check at runtime. Sacred's scripts ARE object-oriented at runtime.

## What's the next decompile target

Three high-leverage candidates, in order:

1. **`FUN_00472bc0`** — referenced by `:res:%d`. Probably small, almost certainly the `res:` token formatter/resolver. If it ingests a cstr and returns a u32, that's our hash function. (Very small effort, very high payoff.)
2. **`FUN_00711ee0` / `FUN_00803920`** — the two callers of `Bin\Balance.bin`. They tell us how Balance.bin is consumed at runtime — confirming our offset-to-meaning hypothesis for the floats.
3. **`cInterpretSQW::*` family** — find methods whose names start with this prefix (the strings exist as printf prefixes); they implement the FunkCode interpreter. The actual record-walking + tag-switch lives here.
