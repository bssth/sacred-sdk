# sdk/ports/ — unwired TODO(port) class catalogue

Future-use C++14 (MSVC) ports for the Sacred Gold SDK. **NONE of this is in the
build yet, by design** — no file here is added to `sdk/SacredSDK.vcxproj`, and no
existing TU `#include`s any of it. Each header/cpp is standalone (standard headers
+ its own sibling port headers only; no `engine/sdk.h`, no `engine/mem.h`) and
carries SOURCE doc-comments plus `TODO(port)` / `TODO(verify)` tags.

These are written-but-not-wired classes ("написать код, но пока не использовать
нигде"). The catalogue below is the map of what exists and what each piece needs
before it can be turned on.

Total: **34 files** across 6 clusters (24 `.h`, 6 `.cpp`, + cluster sub-READMEs/this).

---

## Cluster: `data/` — compile-time constexpr lookup tables

Pure data; no engine VAs; no byte-sig needed. All transcribed verbatim from the
`custom/lua/lib/data/*.lua` tables (themselves from the `sacred_modding - *.csv`
refs) and documented in `re/data_tables.md`. All row-count `static_assert`s pass.

| File | Purpose | Source |
|---|---|---|
| `data/creature_types.h` | 474-row creature/character type-id table (real id ↔ byteswapped hex; hero/monster/npc/animal/townsfolk bands) | `creatures.lua`; `re/data_tables.md §1` |
| `data/combat_arts.h` | 156-row combat-art/spell id→name (packed `(modifier<<16)|opcode`) | `combat_arts.lua`; `re/data_tables.md §2` |
| `data/companions.h` | 4-row per-class summonable companion model/name `global.res` handles | `companions.lua`; `re/data_tables.md §3` |
| `data/hero_tables.h` | ExpTable(206)/Chars(10)/Skills(34)/ClassSkills(17×9)/CombatArts(123)/SurvivalBonus(79) progression tables | `hero_tables.lua`; `re/data_tables.md §4`; charmodif v0.15 (GPLv2) |
| `data/string_ids.h` | 823-row quest/NPC/item string-id→EN name (17000.. main, 19500.. UW); UTF-8 | `names.lua`; `re/data_tables.md §5` |
| `data/weapon_bonus_names.h` | 300-row weapon/item bonus id→name (German verbatim) | Weapon.pak Editor v0.2.3.0 `MainModule.bas`; `refs_formats_data.md B3/P7` |

---

## Cluster: `herosave/` — `.pax` hero-save container (read/write)

| File | Purpose | Source |
|---|---|---|
| `herosave/zlib_codec.h` / `.cpp` | inflate/deflate wrapper for `0xBAADC0DE` PAX sections; real body only compiles under `SACRED_HAVE_ZLIB` | `refs_native_src.md #1`; `refs_resacred.md`; HeroDump `SacredHeroFile.cpp` |
| `herosave/hero_save.h` / `.cpp` | `Hero0?.pax` AMH container reader/writer (0x1C0 header, 10-entry alloc table, compressed/stored sections); write-order matches charmodif `CompileHero` | `refs_native_src.md #1`; charmodif `classHero.pas`; BHVS |
| `herosave/hero_stats.h` | Typed getters/setters over the decompressed 0xC7 HeroInfo + 0xC3 SpecialInfo; version-gated (UW vs Classic/Plus −0x48 delta) | charmodif `Main.pas` 595-832, `CombatArts.pas` |

---

## Cluster: `assets/` — on-disk asset formats (PAK / texture / sector / iso)

In-file offsets only; no engine VAs; no byte-sig needed. zlib is **injected**
(caller supplies an inflate fn) so these stay standalone.

| File | Purpose | Source |
|---|---|---|
| `assets/iso_transform.h` | world↔screen iso projection + sector-grid mapping (header-only, pure math) | `refs_resacred.md` rs_game.cpp:233/240/264 |
| `assets/pak_archive.h` / `.cpp` | THREE distinct `.pak` readers: ResourcePak (SND/TEX/MDL), ResacredPak (256B hdr+desc), CreatureCif (CIF, 86B records) | `refs_formats_data.md B1/B2`; `refs_resacred.md` |
| `assets/texture_decode.h` / `.cpp` | PAK texture entry decoder (typeId 6 RGBA8, typeId 4 zlib→ARGB4444) → host RGBA8 | `refs_resacred.md` PakTextureHeader 80B |
| `assets/world_sector.h` | `sectors.keyx`/`.wldx` loader (KeyxSector 768B dir, WldxEntry 32B cells, floor/tile lookup) | `refs_resacred.md` rs_file.cpp:602-681 |

---

## Cluster: `balance/` — creature/item balance data + live type catalog

| File | Purpose | Source |
|---|---|---|
| `balance/balance_bin.h` / `.cpp` | typed accessor over on-disk `balance.bin` (int32/float32 @ absolute offsets; in-place patch, no checksum) | `refs_magician_net.md §3`; `refs_formats_data.md §B4` |
| `balance/sacred_creature.h` | `Creature.pak` "CIF" 86-byte SacredCreature record reader + Class/Skill/Flag enums | `refs_formats_data.md §B2` |
| `balance/item_type_catalog.h` | walker over the **live** engine TYPE_* name/id table; mirrors resolver `FUN_0043cd90` | `re/items.md §1`; `re_backlog.md S7` |
| `balance/hostility_matrix.h` | pure 16×16 faction-hostility query + `FUN_00423580` predicate (peaceful-override, hero-redirect) | `re/combat_init.md §4`; `re_backlog.md S3` |

---

## Cluster: `engine/` — name-hash, global.res, live cCreature/quest writers, VA table

| File | Purpose | Source |
|---|---|---|
| `engine/sacred_hash.h` | pure `sacred_hash(name) & 0x7fffffff` resource-name hash (MUL 0x71, MOD 999999991, signed-mod) | `re/globalres_format.md §1`; `FUN_0080e780`; verified vs `text.lua` |
| `engine/globalres.h` / `.cpp` | byte-exact `global.res` index+blob rebuild/append emitter (depends on `sacred_hash.h`) | `re/globalres_format.md §1-§4` |
| `engine/command_ring.h` | cCreature scripted-command ring append (MoveTo/Teleport/Voice) at +0x588/+0x58c, stride 0x44 | `re/triggers_dialog_move.md §B/§C`; `re_backlog.md S8` |
| `engine/quest_entry.h` | typed cQuestMgr quest-entry (0x174 stride) writer + marker registration | `re/quest_lifecycle.md §1-§3`; `re_backlog.md S6` |
| `engine/engine_bindings.h` | named engine VAs + thin in-process call wrappers (alloc/spawn/etc.) | `refs_native_src.md` Inoffizieller-Patch addr table |

---

## WIRING CHECKLIST — what each class needs before use

**data/** (all six)
- Add the chosen header(s) to a TU and `#include`; expose via a `sacred.*` Lua
  binding (e.g. `sacred.creature_name(id)`) if UI/script needs lookup.
- `weapon_bonus_names.h`: `TODO(verify)` translate German→English via
  `speditor.ini` if an EN label table is required.
- No byte-sig, no external libs.

**herosave/**
- **Link zlib** (vendor `zlib`, define `SACRED_HAVE_ZLIB`) — prerequisite for
  `zlib_codec.cpp`, which `hero_save.cpp` depends on.
- Add `zlib_codec` + `hero_save` (+ `hero_stats` if editing fields) to vcxproj.
- `hero_save.h` `TODO(verify)`: confirm the per-WORD version marker `0x101B` (UW)
  on a real Steam-Gold `Hero0?.pax`.
- `hero_stats.h` `TODO(verify)`: the −0x48 Classic/Plus shift is corroborated only
  by the CA-count pair — dump a Classic save before trusting writes on that path.
- Expose `sacred.read_save` / `sacred.write_save` Lua binding at wire time.

**assets/**
- Provide a `ZlibInflateFn` — **reuse `herosave/zlib_codec`** (so zlib must be
  linked) for `texture_decode`, `world_sector`. `iso_transform.h` needs nothing.
- `pak_archive` needs no zlib (raw payloads).
- Add chosen files to vcxproj; wire to whatever consumes assets (minimap overlay,
  asset extractor). No byte-sig (all in-file offsets).

**balance/**
- `balance_bin`, `sacred_creature`: caller owns the file buffer; add to vcxproj,
  expose via `sacred.*` if live editing is wanted. No byte-sig (on-disk files).
- `item_type_catalog.h`: **byte-sig-verify VA `0x008EC328`** (file offset
  `0x4EC328`) against `Sacred_decrypted.exe` — confirm stride 0x44, count 5624,
  id@+0, name@+4, record[0]=="TYPE_INVALID". Then supply a live base ptr or a
  `MemReader`. Only usable inside the injected DLL (or an out-of-proc reader).
- `hostility_matrix.h`: **byte-sig-verify VAs `0x008EB548`** (scrambled live
  matrix) and **`0x00890A30`** (real 16×16 restore source), and the `+0x1F0`
  class / `+0x1F4` override field offsets, against `Sacred_decrypted.exe`. Grudge
  mode is intentionally not modeled.

**engine/**
- `sacred_hash.h`: no deps — `#include` and use directly.
- `globalres.h/.cpp`: depends on `sacred_hash.h`; add both to vcxproj. Wire to
  replace `text.lua`'s span-copy model when append support is needed. No byte-sig.
- `command_ring.h`: **byte-sig-verify** the ring offsets `+0x588/+0x58c`, stride
  `0x44`, and the grow helpers `FUN_004be490` / `FUN_004b9900`. **BLOCKING
  unknown:** the grow-helper ABI is unverified — needs a live BP on
  `FUN_004a9730` to capture the arg convention before the append path is safe.
  In-process only; caller supplies read/write callbacks.
- `quest_entry.h`: **byte-sig-verify** registry ptrs `0x00AAD3A4`/`0x00AAD3A8`,
  singleton `0x00AACF80`, `qb_resolve` `0x00672740`, `FUN_007d84a0`, stride
  `0x174`. In-process; caller supplies `MemAccess`.
- `engine_bindings.h`: **EVERY VA is `TODO(verify)` byte-sig vs
  Sacred_decrypted.exe.** `engine_alloc (0x008485E2)` landed mid-function in a
  probe — treat as UNCONFIRMED, re-find by signature. Requires the SDK DLL
  injected into Sacred's address space; do NOT call any wrapper until confirmed.

**net/** (cluster has its own sub-README; summary)
- `tincat_crc32.h`: no deps, table regenerated from polynomial — use directly.
- `lobby_frame.h/.cpp`: depends on `tincat_crc32.h`; back socket I/O with winsock
  at wire time. No byte-sig.
- `server_packet.h`: depends on lobby framing; for a private master server / lobby
  observer. No byte-sig.
- `udp_announce.h/.cpp`: needs an injected `ZlibCodec` — **reuse
  `herosave/zlib_codec`** (link zlib). No byte-sig.

---

## DEPENDENCY ORDER (build/wire bottom-up)

```
zlib (external)
  └─> herosave/zlib_codec
        ├─> herosave/hero_save -> herosave/hero_stats
        ├─> assets/texture_decode, assets/world_sector   (inject inflate fn)
        └─> net/udp_announce                             (inject ZlibCodec)

engine/sacred_hash
  └─> engine/globalres

net/tincat_crc32
  └─> net/lobby_frame
        └─> net/server_packet

data/*  -> (no port deps) but is the shared id/name vocabulary every other
           cluster keys against (creature ids, combat-art opcodes, string ids).
           Treat data/ as the foundation: wire it first so the rest can resolve
           human-readable labels.
```

Standalone (no port-to-port deps): `data/*`, `assets/iso_transform.h`,
`assets/pak_archive.*`, `balance/*`, `engine/command_ring.h`,
`engine/quest_entry.h`, `engine/engine_bindings.h`, `herosave/` only on zlib.

---

## Reminder

Nothing here is compiled by the project. To activate any piece: (1) satisfy its
checklist (link libs / byte-sig the VAs / inject callbacks), (2) add the file(s)
to `SacredSDK.vcxproj`, (3) `#include` from a real TU, (4) expose via the relevant
`sacred.*` Lua binding. Until then these remain future-use reference ports.
