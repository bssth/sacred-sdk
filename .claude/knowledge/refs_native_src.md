# Native / Source Archive Mining — Sacred Gold SDK

Source archives mined from `E:/Downloads/refs/`, extracted to `E:/refs_extract/<name>/`.
Game EXE base = `0x00400000`, no ASLR, so **file offset = VA − 0x400000**.
Patch sources below target the **German Sacred Underworld 2.x EXE** ("Sacred_2"); verify the exact build before reusing absolute VAs (the deployed Steam EXE may differ — diff the byte signatures, not just the address).

Tooling note: `7z` is NOT on PATH but `C:\Program Files\7-Zip\7z.exe` exists and extracted the `.rar`. No standalone `unrar` needed. All 7 archives extracted successfully.

---

## TOP PRIORITY #1 — Hero save file format (PAX) — the `.pax` container

Two independent implementations agree: **HeroDump (C++/MFC)** and **sacred_charmodif (Delphi/Pascal)**. This is the canonical, fully-decoded format.

### Container layout (`HeroN.pax`, e.g. `hero0.pax`)
```
Offset 0x000  Header (size 0x100 / 0x1C0 — see note):
  +0x00  BYTE[3]  magic            = {0x41,0x4D,0x48} = "AMH"
  +0x03  WORD     SubHdr1 / version (a.k.a. PAX version):
                    0x1006 = "1.6"      (unsupported by tools)
                    0x1007 = "1.7/1.8"  Sacred Plus  (OpenMethod 1)
                    0x101B = "2.21 UW"  Sacred Underworld (OpenMethod 2)  <-- Gold
                  BHVS uses byte at +0x03 >= 0x1A to flag "is Underworld".
  +0x05  BYTE[3]  empty (null)
  +0x08  WORD     SubHdr2          = 0x022C
  +0x0A  BYTE[30] unknown1
  +0x1E  WORD     VersionID        = 0x2A3B for v1.8 (build-specific subversion)

Offset 0x100  Allocation Table (10 entries × 12 bytes = 120 bytes):
  struct TATRecord { DWORD DataType; DWORD Offset; DWORD UnpackedSize; }  // 12 bytes
  Table[0..9].  (HeroDump calls these "section entries"; charmodif "allocation table".)
  Header region copied wholesale up to 0x1C0 in charmodif (Streams[0] = first 0x1C0 bytes).
```

### Section / stream payloads
Each table entry's `Offset` points to a payload. A compressed payload begins with:
```
struct TZSectionHeader {       // 32 bytes
  DWORD BadCode = 0xBAADC0DE;  // compression signature/sentinel
  DWORD Size;                  // compressed (zlib) size of THIS section
  BYTE  NullArray[24];
}                              // followed by zlib-deflate stream of length Size
```
If the first DWORD at `Offset` != `0xBAADC0DE`, the payload is **stored uncompressed** (length = table entry's `UnpackedSize`). Decompression = standard **zlib `inflate`** (HeroDump links real zlib1.dll; charmodif uses ZLibEx). Recompression = `deflate` at max level.

**Charmodif stream index → table mapping** (the 9 logical streams `Streams[0..8]`):
```
Stream 0 = Header (raw, first 0x1C0 bytes)
Stream 1 = "C7" Hero Info        <- COMPRESSED (table[0])   *** the main stat block ***
Stream 2 = "CA" unknown          <- COMPRESSED (table[1])
Stream 3 = "CB" unknown          <- COMPRESSED (table[2])
Stream 4 = "C8" unknown          <- COMPRESSED (table[3])
Stream 5 = "C3" Special/Import info <- UNCOMPRESSED (table[4])
Stream 6 = "C4" additional info  <- UNCOMPRESSED (table[5])  (HeroDump hardcodes type 0xC4 len=64)
Stream 7 = "CD" unknown          <- COMPRESSED (table[8])
Stream 8 = "CE" unknown          <- UNCOMPRESSED (table[9])
```
Note table indices 6,7 are unused/skipped on write; write order is [0..3] compressed, [4]=stream5, [5]=stream6 raw, [8]=stream7 compressed, [9]=stream8 raw. Reproduce exactly when rewriting.

HeroDump also recognizes a separate sidecar comment file: `<heropath>.txt` (plain text, free-form note).

### C7 "Hero Info" stream — exact field offsets (charmodif `Main.pas`, OpenMethod 2 = UW)
All offsets are into the **decompressed Stream 1** buffer:
```
0x020D  WORD   "cheats used" flag (nonzero => cheats)            (load 694 / save 787)
0x03DD  BYTE   CharacterType (class id; see CharsTable below)    (load 603)
0x03E1  DWORD  Experience                                        (load 625 / save 772)
0x03F8  BYTE   "C-Points" (class/skill points?)                  (load 618 / save 753)
0x03F9  BYTE[8] Skill IDs   (the 8 chosen skills, one byte each) (load 656 / save 798)
0x0401  BYTE[8] Skill Levels (level of each of the 8 skills)     (load 681 / save 809)
0x0417  WORD   "A-Points" (attribute points?)                    (load 621 / save 757)
0x041B  DWORD  Gold                                              (load 615 / save 749)
0x042B  WORD   Level                                             (load 628 / save 770)
0x0483  BYTE   Combat-Art count  (OpenMethod 1 / Plus)           CombatArts.pas:72
0x04CB  BYTE   Combat-Art count  (OpenMethod 2 / UW)             CombatArts.pas:74
        followed by <count> × TCARecord (see below), then the rest of the stream.
```
`SkillsArray` is read/written as `array[0..7] of Byte`.

### Combat-Art record (`TCARecord`, packed, 22 bytes), C7 stream after the CA count
```
struct TCARecord {
  BYTE Unknown1[5];   // usually {0x00,0x02,0x00,0x00,0x00}
  WORD ID;            // combat-art ID (see tableCombatArts)
  BYTE Level;
  BYTE Unknown2[14];
}
```
Read loop: seek to 0x483 (Plus) or 0x4CB (UW), read 1-byte count, then `count` × TCARecord. On write, the tool copies the remainder of the stream after the CA block back verbatim (preserves trailing unknown data).

### C3 "Special Info" stream (`TSpecialInfo`, packed) — Stream 5, UNCOMPRESSED
Used by the in-game "Import" menu and is the block BHVS reads for the hero list.
```
struct TSpecialInfo {
  WORD     HeroLevel;            // 0x00  level shown in Import menu
  BYTE     Unknown1[2];          // 0x02
  BYTE     CharacterType;        // 0x04  start-location class id
  BYTE     Unknown2[3];          // 0x05
  WCHAR    HeroName[64];         // 0x08  UTF-16LE, 128 bytes
  BYTE     Unknown3[106];        // 0x88
  BYTE     LevelsUnLock;         // 0xF2  =1 => all difficulties unlocked
  BYTE     Unknown3_1[6];        // 0xF3
  BYTE     ResurrectionsCounter; // 0xF9
  BYTE     CheatFlag;            // 0xFA  odd / 0x81 => cheats were used
  BYTE     Unknown4[305];        // 0xFB
}
```

### Alternative: BHVS direct-file read (no decompression needed for the header summary)
BHVS reads the hero summary straight from the raw file without inflating, via an indirection pointer (useful for a fast "list heroes" path):
```
file +0x003  BYTE  : >=0x1A  => Sacred Underworld
file +0x134  DWORD : dwOffset -> seek there, then sequentially:
    +0x00 DWORD Level
    +0x04 DWORD CharType (CharType enum below)
    +0x08 WCHAR[64] Name (UTF-16LE)
    then skip 7*16+4 = 0x74 bytes, then:
    DWORD Experience
    DWORD Gold
```
Slot number parsed from filename `hero%lu.pax` (valid 0..5 for Classic, 0..7 for UW).

**Source files:**
`E:/refs_extract/HeroDump_Quellcode/SacredHeroFile.cpp` (container parse + zlib inflate, with `bDump` mode that writes each section to `<file>.<typehex>` raw and `.<typehex>_` decompressed)
`E:/refs_extract/HeroDump_Quellcode/PaxSection.h`, `SacredHeroFile.h`
`E:/refs_extract/sacred_charmodif_src/.../classHero.pas` (Compile/Decompile = full read+rewrite)
`E:/refs_extract/sacred_charmodif_src/.../Main.pas` (field offsets, lines 595-832)
`E:/refs_extract/sacred_charmodif_src/.../CombatArts.pas` (CA record + count offsets)
`E:/refs_extract/BHVS_quellcode/HeroInfo.cpp` (lines 110-210; direct-read layout + name write-back)

---

## TOP PRIORITY #2 — Inoffizieller Patch source (engine VAs / patches) — VERY valuable

By **Thorium (SacredVault)**. Two builds, each with full before/after disassembly at absolute VAs.
Files: `E:/refs_extract/Inoff_Sacred_Patch_Source/2.29 Patch/2.29.12 Source.txt`,
`.../2.30 Patch/Patch Source Sacred.exe.txt`, plus PureBasic patcher/runtime-DLL sources.

### Confirmed engine addresses (German Sacred UW EXE, base 0x400000)
| VA | What it is |
|----|-----------|
| `0x0080DBF0`–`0x0080DCCB` | Global.res loader (originally loads binary resource #0x6B, XOR-decodes with key `0x45AD` per WORD). Patch replaces it with CreateFile/ReadFile of `Global.res` from disk. **The XOR `0x45AD` per-WORD scheme is the Global.res on-disk obfuscation key.** |
| `0x008485E2` | engine `malloc`-equivalent: `push size; call 008485E2` returns ptr in EAX. **Reusable allocator.** |
| `0x0061561B` | start of in-EXE cheat handler (NOP'd by patch); region `0x006156B8`–`0x00615FA7` is free space the patch repurposes for injected code |
| `0x00615FD0` | console command handler (German build) = `#ConsoleHook_German` |
| `0x0065CE80`, `0x005FE060`, `0x00603E90`, `0x00849701`, `0x00849590`, `0x00830F60`, `0x0066F1C0` | engine helper calls used near the console/chat code. `0x0066F1C0` = **debug/log print** (`push msgptr; call 0066F1C0; add esp,4`) |
| `0x00553080` | UI window create-by-name (used with `"UI_WND_CONSOLE"` at `0x00953FD0`) |
| `0x0084AE73`–`0x0084AEAB` | wide-string copy routine; site of the **chat-crash fix** (adds `IsBadReadPtr` guard before reading the message buffer) |
| `0x00816BD0` | single-instance guard: `CreateMutexA("SACRED_INSTANCE")`; mutex handle stored at `[0x0182CD38]`. Patch NOPs the "already running" path => **multi-instancing** |
| `0x0084C704` | **EXE entry point**. Patch hooks it (`jmp 0x006156E4`) to `LoadLibraryA("PatchClient.dll")` then restores `push ebp / mov ebp,esp / push -1` and returns to `0x0084C709` |
| `0x00758ED0` / string at `0x0095D014` | main-menu version string (decoupled from internal/lobby version) |
| `0x00810AF3`–`0x00810B17` | `IsIconic` minimized-check; patch NOPs the focus-force => **debugger-freeze fix** |
| `0x00812421` (case 0x1C `WM_ACTIVATEAPP`) | window-switch handler; patch prevents screen clear on alt-tab. Window-active flag at `[0x0182CDB8]` (AND `0xFFFDFFFF`) |
| `0x00813309`/`0x00813331` | main `CreateWindowExA`; style changed `0x10CF0000` -> `0x10CA0000` (windowed-mode border). Window class/title ptrs at `[0x00A1AD24]`,`[0x00A1AD20]` |
| `0x00856A8A` -> `0x00615717` | **Balance.bin loader hook**: compares requested path against string at `0x0094C4D4`, forces hardcoded path `.\Bin\Balance.bin` (string written at `0x009E1AFC`). Confirms balance data is `.\Bin\Balance.bin` |
| `0x006198B3` | console "unknown command" handler; patch suppresses the "Error! Try HELP..." message (string at `0x0095D228`) |

### Imported-function thunk table (IAT, German UW build) — `call [VA]`
```
0x0088E0EC ExitProcess     0x0088E018 GetVersion      0x0088E180 GetModuleHandleA
0x0088E150 FindResourceA   0x0088E154 LoadResource    0x0088E290 SizeofResource
0x0088E15C LockResource    0x0088E160 FreeResource    0x0088E244 CreateFileA
0x0088E1D8 GetFileSize     0x0088E1D4 ReadFile        0x0088E24C CloseHandle
0x0088E240 IsBadReadPtr    0x0088E1BC LoadLibraryA    0x0088E1AC CreateMutexA
0x0088E1EC GetLastError    0x0088E238 GetTickCount    0x0088E34C MessageBoxA
0x0088E334 IsIconic        0x0088E338 ShowWindow      0x0088E33C GetForegroundWindow
0x0088E374 SetFocus        0x0088E3A4 CreateWindowExA 0x0088E2CC GetSystemMetrics
```
(Derived from the `FF15 xxxxxxxx` operands annotated in the disassembly — invaluable for resolving any new hook site.)

### PatchClient.dll runtime source (PureBasic 4.20) — runtime engine state
File: `.../2.30 Patch/PatchClientDLL.pb` (1092 lines).
```
#GameModeAddr_German = 0x0182CB6C   // BYTE: 0=Singleplayer, 1=Multiplayer (live game-mode flag)
#ConsoleHook_German  = 0x00615FD0   // console handler (matches table above)
window class/title "Sacred"/"Sacred" with dwStyle 0x90000000 => detected as the game window
```
Implements a `GetCursorPos` hook that rescales cursor coords from the real window to the engine's logical 1024×768 (via `GetClientRect`+`ClientToScreen`) — i.e. **the math to map physical mouse to engine 1024×768 space** (relevant for any overlay/input work).
The 2.29 PureBasic patcher (`Patcher Source.pb`, 260 lines) is just the file-patching driver.

---

## Per-archive catalog (remaining)

### BHVS_quellcode.ZIP / BHVS_sourcecode.ZIP — "Sacred Hero Vault" tool (C++/MFC, VC6 .dsw)
By Uwe Posselt / SacredVault Team. GUI vault manager for `.pax` heroes (tree view, slots, rename, comments, drag-drop). `quellcode` is the fuller superset (adds config dialogs, transparency, hotkeys, richedit). The matching `BHVS.7z` (13 MB, not in scope here) is likely the built binaries + resources.
Reusable knowledge:
- `CharType` enum (`HeroInfo.h`): `1=Seraphim, 2=Gladiator, 3=Magier(BattleMage), 4=Dunkelelf(DarkElf), 5=Waldelfe(WoodElf), 6=Vampirin(Vampiress), 8=Zwerg(Dwarf), 9=Daemonin(Demon)` (7 unused). **Matches charmodif CharsTable exactly.**
- Hero name uses embedded color/icon codes; `RemoveColorAndIcons()` / `CheckColorCode()` strip a 10-char color escape (see `HeroInfo.cpp` ~690-760) — relevant if rendering hero names.
- `HookHelper.cpp` loads `keyboardhook.dll` exporting `SetupKeyboardHook` / `UninstallKeyboardHook` / `SetupApplicationHook` / `UninstallApplicationHook` — a global keyboard/app hook pattern (a small companion DLL, not Sacred-specific).
- `SacredInfo.cpp`: detects a Sacred install by `sacred.exe` presence + file version; distinguishes "Sacred Classic" vs "Sacred Underworld"; user dirs found by mask `save*.` (savegame folders under the install).

### HeroDump_Quellcode.zip — `.pax` section dumper (C++/MFC) — see TOP PRIORITY #1
Ships real **zlib** (`ZLib/include/zlib.h`, `zconf.h`, `zdll.lib`, `zlib1.dll`) — confirms hero compression is vanilla zlib/deflate. `WideString.cpp/.h` = UTF-16 ↔ ANSI helper for the WCHAR name field. `HeroDump.zip` (binary, 22 KB) = the built tool.

### sacred_charmodif_src_v0.15.0.16_public.rar — Sacred Character Modifier (Delphi/Pascal) — see TOP PRIORITY #1
By LinkZ (GPL). Borland/Delphi project (`.dpr`, `.dfm`, `.pas`). The most complete hero editor source. Key data tables (all reusable, exported here):
- `CharsTable.pas`: class-id → name (array `[0..9]`, see CharType above).
- `SkillsTable.pas`: `c_st_Skills[0..33][DE,EN,RU]` — **34 skill names** with the skill ID = array index (0 = none). e.g. 1=Celestial Magic, 2=Weapon Lore, 3=Polearms, 4=Sword Lore, 5=Axe Lore, 6=Dual Wield, 7=Ranged Combat, 8=Agility, 9=Parrying, 10=Constitution, 11=Armor, 12=Meditation, 13=Blade Combat, 14=Magic Lore, 15=Fire Magic, 16=Water Magic ... (full list in file).
- `tableCombatArts.pas`: `table_CombatArts[0..122]` — **123 combat arts** keyed by `(HeroClass, Num, CAType, ID1, ID2, Name)`. `ID1`/`ID2` are the `TCARecord.ID` values written to the save. CAType: 1=spell-type, 2=combat-type. (e.g. Seraphim "Combat Jump" ID=0x03F9, "Rotating Blades of Light" ID=0x0011; Gladiator "Hard Hit" ID=0x0422; BattleMage "Fire Ball" ID=0x0005.)
- `ExpTable.pas`: `cExpTable[1..206]` — **level→cumulative-XP table**, levels 1..206 (L1=0, L2=300, L3=1200 ... L206=2,117,050,200). Use to convert XP↔level.
- `tableSurvivalBonus.pas`: maps survival-bonus tier (`Min..Max` byte range) → percentage `P`. Survival bonus value stored in Stream 6 (`csAdditionalInfo`) at offset `0x09` (WORD); resurrection counter at `0x00` (DWORD) of that stream.

### Language_quellcode.zip — Sacred Vault tools' language DLL (C++/MFC) — NOT the game's localization
By SacredVault. A small `Language.dll` used by the *vault tools* (BHVS etc.), not Sacred itself. Reads `*.lng` files that are plain **INI files**:
```
[Common] ID=<LANGID>  Desc=<name>  ToolTips=<tooltipOffset>
[Strings] <uint id>=<text>   (one per line)
```
Exports: `GetLanguage / SetLanguage / SetDefaultLanguage / SelectLanguage / GetString / GetStringLength / GetTooltipOffset / GetLanguageName(+Length)` (see `Language.def`). Picks `.lng` by matching `GetUserDefaultLangID()` then primary-language fallback. Mildly reusable as a lightweight INI-based string-table pattern.

### suac_recoded_127.zip — "Advanced Configuration Recoded" (.NET binaries, NO source)
By community ("recoded"). Contains compiled **.NET assemblies only** (`AdvancedConfiguration.exe`, `AccountManager.exe`, `SacredVault.AccountData.dll`, `SUWDirVer.dll`, `SUWSettings.dll`, `Validation.dll`, German satellite resources). No `.cs`/source. It's a settings/account-manager GUI editing `settings.cfg` (multiple accounts, removed the count cap). Catalog only — would need decompilation (ILSpy/dnSpy) to extract logic; assembly names suggest `SUWSettings.dll` wraps `settings.cfg` and `SUWDirVer.dll` does install-dir version detection.

---

## C++-port / refactor candidates (for the SDK)

1. **TODO(port): PAX hero-save reader/writer.** Port `classHero.pas` CompileHero/DecompileHero + the C7/C3 field map into a `SacredHeroFile` C++ class (we already have zlib via the engine and/or system). Gives read/write of level, XP, gold, class, 8 skills+levels, combat arts, survival bonus, cheat flags, resurrections, name. *This is the single highest-value port.* Use HeroDump's `SacredHeroFile.cpp` as the container skeleton and `Main.pas` offsets for fields.

2. **TODO(port): Hero data tables.** Translate `ExpTable.pas` (level↔XP), `SkillsTable.pas` (34 skill ids→names), `tableCombatArts.pas` (123 CA id↔name↔class), `CharsTable.pas` (class ids), `tableSurvivalBonus.pas` (tier→%) into a header of `constexpr` arrays. Pure data, trivially portable, immediately useful for any stat UI/diagnostics.

3. **TODO(port): Global.res on-disk decoder.** The loader patch reveals Global.res (binary resource 0x6B) is XOR-obfuscated per-WORD with key `0x45AD`. Port a `decodeGlobalRes(buf)` that XORs each WORD by 0x45AD (the chained `xor [ecx-2],[ecx]` in the original is the *resource* unscramble — re-read `0x0080DC8B` loop carefully before porting). Lets us read Global.res strings directly.

4. **TODO(port): engine helper bindings.** Wrap the confirmed VAs as typed function pointers in our SDK address map: `engine_alloc = 0x008485E2`, `debug_print = 0x0066F1C0`, `ui_create_window_by_name = 0x00553080`, console handler `0x00615FD0`. Gate behind a build-signature check first. The IAT thunk table above lets us resolve any further hook site without re-reversing imports.

5. **TODO(port): live game-mode read.** `*(BYTE*)0x0182CB6C` = singleplayer(0)/multiplayer(1); window-active flag `*(DWORD*)0x0182CDB8`; instance mutex handle `*(void**)0x0182CD38`. Useful diagnostics for the injected DLL. (Verify these globals against the Steam build.)

6. **TODO(port): cursor→engine-1024×768 mapping.** Port the `GetCursorPosHook` math (`PatchClientDLL.pb`) for any overlay/input feature that must map physical mouse coords to the engine's logical resolution.

7. **TODO(port): optional engine fixes** (only if we ever ship EXE patches): chat-crash `IsBadReadPtr` guard at `0x0084AE81`, multi-instancing at `0x00816BD0`, debugger-freeze NOP at `0x00810B0F`, windowed-border style `0x10CA0000` at `0x00813328`. All have copy-paste byte patches in the 2.30 source. Lower priority — these duplicate what the official Inoff Patch already does.

8. **(catalog only) suac AdvancedConfiguration** — .NET, no source; decompile later if account/`settings.cfg` editing is ever needed.

### Cross-checks / cautions
- Class id `6 = Vampiress` in both tools, but `CharsTable[6]` shows `'-'` for the *string* slot while `cID_Vampiress=6` — the printable-name array has a quirk at index 6/7; use `cID_*` constants, not raw `cStr_CharacterClass` indexing, for class 6.
- All patch VAs are for the **German** Sacred Underworld EXE. The Steam "Sacred Gold" EXE is very likely a different build → treat every absolute VA as a *lead*, confirm by byte-signature search before use.
- charmodif only "supports" PAX `0x101B` (2.21 UW). Steam Gold heroes should match; if a different version word appears, the field offsets may shift.
