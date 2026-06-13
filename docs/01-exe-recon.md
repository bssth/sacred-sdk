# 01 — `Sacred.exe` static recon

## Binary identity

| Path | SHA-256 | Size |
|---|---|---|
| `Sacred.exe` | `4df6659352282a0e57bdf69d2ac33396200fd0b54670d327cb9822ffdc4891cd` | 11 927 552 |
| `Testapp.exe` | identical to `Sacred.exe` | 11 927 552 |
| `Config.exe` | `92ba57e9…` | — |
| `GameServer.exe` | `8af79b02…` | — |

`Testapp.exe` is the same binary as `Sacred.exe`, renamed. Branching likely on `argv[0]` or a flag — probably a debug/test entrypoint.

Build: PE32 i386 GUI, MSVC 6.0 linker, `TimeDateStamp 2006-10-13T12:25:43Z`. Subsystem 2 (GUI), ImageBase `0x400000`. Not packed — imports table is full and named, `.text` is a flat 4.7 MB.

## Sections

| Name | RVA | VSize | RawSize | Flags | Entropy |
|---|---|---|---|---|---|
| `.text` | `0x00001000` | `0x0048e632` | `0x0048f000` | XR | 8.00 |
| `.rdata` | `0x00490000` | `0x00056846` | `0x00057000` | R | 4.45 |
| `.data` | `0x004e7000` | `0x00f4a794` | `0x0013d000` | RW | 3.67 |
| `.rsrc` | `0x01432000` | `0x00539a18` | `0x0053a000` | R | 5.80 |
| `.bind` | `0x0196c000` | `0x00002000` | `0x00002000` | R | 5.11 |

EP RVA = `0x196c3db`, inside `.bind`, not `.text` — SafeDisc/SecuROM stub layout. `.text` entropy 8.00 indicates it is encrypted (see [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md)). SacredUtils' canonical inventory lists `protect.dll` as a required runtime, but the Steam install has no `protect.dll`; Steam most likely patched `.bind` to jump straight into `.text` or replaced the stub with a thin redirector.

**Hooking implication:** any in-process work that runs at `DllMain` time (e.g. the planned `ijl15.dll` proxy) executes after `.bind` is done, so the stub is not in the way.

## Imports (high-signal entries)

| DLL | # | Role |
|---|---|---|
| `granny.dll` | 54 | RAD Granny 3D — skeletal animation (`.grn`/`.gr2`) |
| `mss32.dll` | 62 | Miles Sound System |
| `tincat2.dll` | 4 | Multiplayer transport (`TinCat_Scramble` ⇒ packets are obfuscated) |
| `libxml2.dll` | 12 | XML parsing (loads, saves) |
| `ijl15.dll` | 4 | Intel JPEG. Proxy-DLL candidate for hooks |
| `DDRAW.dll` | 3 | DirectDraw (renderer) |
| `WS2_32.dll` | 15 | Winsock |
| `unicows.dll` | — | Unicode-on-Win9x shim |
| Standard | — | KERNEL32 (123), USER32 (62), GDI32 (19), ADVAPI32 (6), ole32 (4), OLEAUT32 (5), IMM32 (10), VERSION (3), WINMM (3) |

## String-mining

C++ class names leak in error/debug strings; Ghidra string-xref gives symbol recovery:

- `ItemDataMgr::saveHero` — inventory persistence
- `LEVELUP %d to %d` — XP path
- `NumQuestItem %d`, `NumScrolls %d` — quest/inventory counters
- `Sector:%d-%d` — map paging
- `Lv:% 2.2d` — character level UI
- TinCat lobby/CDKey/connection error templates — netcode

File paths referenced from `.rdata`:

- `Bin\Balance.bin`, `Bin\Wea.bin`, `Bin\World.bin`, `Bin\World2.bin`, `Bin\wpmod.bin`
- `Bin\sets.bin`, `Bin\merc.bin`, `Bin\Rust.bin`, `Bin\MultiStart.bin`
- `%s\DefPos.bin`, `%s\FunkCode.bin`, `%s\QuestCode.bin`, `%s\QuestPoolCode.bin`, `%s\StartCode.bin`, `%s\Vectoren.bin` (where `%s` = `TYPE_NPC_*`)
- `.\WORLD\Sectors.bin`, `.\WORLD\FLOOR.PAK`, `.\WORLD\STATIC.PAK`, `.\WORLD\TRIGGERS.PAK`
- `.\Pak\Texture.pak`, `.\PAK\MIXED.PAK`, `.\PAK\MODELS.PAK`, `.\PAK\SOUND.PAK`, `.\PAK\TILES.PAK`, `.\PAK\MODELS%.2d.PAK`
- `./SAVE/GAME%.2d.PAK`, `./SAVE/SAVE02.PAK`, `./SAVE/GAMEF%d.PAK`
- `.\SCRIPTS\%s\global.res` (`%s` = locale: `us`, `de`, …)
- `.\Scripts\Balance.txt` — text source for `Balance.bin`? Worth probing whether the engine still reads `.txt` as a fallback.

Enum-token strings (15 747 of them, e.g. `CL_DEMON`, `CL_DRAGON`, `BONUS_B/M/R/W`, `BOW_CH/GS/LV`, `CHANCE4BLOCK`, `CHEATS`, `UI_REGION_*`) live in `.data`. These are string↔id mappings for the in-game script/balance system.

`CHEATS` is among them — SacredUtils confirms it is a launch-arg flag (`Sacred.exe CHEATS=1`).

## Facts established

| Fact | Basis |
|---|---|
| `Sacred.exe CHEATS=1` cheat launch arg | exists in vanilla, confirmed by SacredUtils source |
| No DRM blocking hooks | `.bind` is a dead/neutered stub on Steam build |
| C++ symbols readable from strings | Ghidra triage is cheap |
| Many config strings live in `.data` | static maps usable as anchors for patch search |
