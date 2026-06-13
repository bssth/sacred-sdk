# Reference mining: Resacred (Sacred Gold C++ remake)

Source: `E:/Downloads/refs/Resacred-master.zip` (38 MB) → extracted to `E:/refs_extract/resacred/Resacred-master`.
Mined 2026-06-13. READ-ONLY reference; nothing built/run.

## What it is

`Resacred` is an in-progress **Sacred Gold remake / asset viewer** in C++ + OpenGL 3 (genie/premake build,
SDL2 + zlib + Dear ImGui + stb). License/author unknown (GitHub `Resacred-master`, last touched 2019).
README progress: "Load texture Data [100%], Display world map [80%]". It is **not** a full game — it is a
**renderer that reads the original Sacred `.pak`/`.keyx`/`.wldx` data files and draws the world map** in an
ImGui-driven debug viewer. That makes it a near-ideal Rosetta stone for OUR SDK: it documents the on-disk
Sacred file formats with working zlib decompression and exact struct layouts that match the game's own loader.

### Provenance / authority of the offsets
The repo ships **two IDA databases of the Sacred executable**: `ida_db/Sacred_2.idb` (81 MB) and
`ida_db/Sacred_3.idb` (88 MB). The C++ structs were reverse-engineered from those IDBs, so the struct layouts
below are derived from the *actual game loader*, not guesses. The IDBs are binary (need IDA Pro to open) —
catalog only, not text-minable here. If we ever get IDA access, those two files are the highest-value artifact
in the whole archive for cross-checking our cCreature/cWorld offsets.

### Architecture (src/, ~25 files)
- `rs_base.h` / `rs_allocator.*` / `rs_array.h` — engine primitives: `i32/u8/...` typedefs, `MemBlock`,
  bucket/ring allocators, fixed + growable `Array<T,N>`, `defer()` macro. Custom, no STL, NoRTTI/NoExceptions.
- `rs_file.{h,cpp}` — **THE prize**: all Sacred file-format structs + parsers + zlib inflate. (full detail below)
- `rs_resources.{h,cpp}` — runtime resource manager: streams `texture.pak`, decompresses sectors on demand,
  GPU texture LRU cache, ties all the pak loaders together. Documents the canonical data-file set + load order.
- `rs_game.cpp` — world rendering: **iso/world coordinate math**, sector grid mapping, tile-mesh + UV atlas
  construction, floor/static/mixed placement. (coordinate constants below)
- `rs_renderer.*`, `rs_gl_utils.*`, `rs_shader.*`, `rs_window.*`, `rs_math.h` (mat4/vec), `rs_thread.*`,
  `rs_string.*`, `rs_logger.*`, `rs_dbg_draw.*` — generic engine plumbing, low RE value.
- `tileset.psd`, `imgui*`, `stb_*`, `gl3w/glcorearb` — assets/vendored libs, ignore.

---

## Sacred data-file set + load order (from `ResourceManager::init`, paths relative to `sacred_data/`)

| File | Format | Loader | Contents |
|---|---|---|---|
| `texture.pak` | PakHeader+descs | streamed (async) | all world textures; entry = `PakTextureHeader`(80B)+pixels |
| `tiles.pak` | PakHeader+descs | `loadTileTextureIds` | `PakTile`(64B) entries; **18 entries per logical tile** |
| `Floor.PAK` | PakHeader+descs | `loadFloorData` | `FloorEntry`(16B) linked-list of floor overlays |
| `sectors.keyx` | PakHeader+`KeyxSector`[] | `loadSectorKeyx` | sector directory: id, world rect, sub-file offsets/sizes |
| `sectors.wldx` | raw, zlib blobs | `loadSectorData` | per-sector compressed `WldxEntry`[4096] map grids |
| `mixed.pak` | PakHeader+descs | `pak_mixedRead` | sprite-atlas placement (`PakMixedData`) for statics |
| `Static.pak` | PakHeader+descs | `pak_staticRead` | `PakStatic`(64B) placed-object instances |
| `items.pak` | PakHeader+descs | `pak_itemRead` | `PakItemType`(128B) object type definitions |

World map = 100×100 grid of sectors, each sector = 64×64 tiles, total **6051 valid sectors** (hard asserted).

---

## File-format knowledge table (format → repo location → maps to OUR SDK)

| Format / algo | Where (file:lines) | Maps to our SDK |
|---|---|---|
| **PAK container** (`PakHeader` 256B + `PakSubFileDesc`[] table) | rs_file.h:91-109 | Generic Sacred archive. Header: `char type[3]; u8 version; i32 entryCount; u8[8]; i32 worldX; i32 worldY; u8[232]`. Then `entryCount` × `{i32 type; i32 offset; i32 size}` directory. Entry #0 is often a skipped/invalid sentinel (Floor & texture loaders do `fileDesc++; entryCount--`). |
| **Texture entry** (`PakTextureHeader` 80B, packed) | rs_file.h:112-131; parse rs_file.cpp:499-529 | `char filename[32]; u16 width,height; u8 typeId; u32 compressedSize; u8; u32 offset; u8[34]`. `typeId==6` → raw RGBA8 (w*h*4 bytes follow header). else (`typeId==4`) → **zlib-compressed ARGB4444** (decompress to w*h*2 bytes). Pixel data starts at `header+80`. |
| **zlib inflate** (stock zlib, 16 KB chunks) | rs_file.cpp:345-411 | `zlib_decompress(in, inSize, out, outMax, *outSize)`. Standard `inflateInit/inflate(Z_NO_FLUSH)`. Sacred uses **plain zlib streams** for compressed textures AND sector map data. Confirms zlib (not a custom codec) — drop-in if we ever decode .pak/.wldx in C++. |
| **Sector directory** (`KeyxSector` 0x300=768B) | rs_file.h:133-156; parse rs_file.cpp:602-681 & rs_resources.cpp:447-514 | `char name[32]; i32; i32 sectorId; i32; u16 neighbourIds[8]; i32 posX1,posY1,posX2,posY2; {i32 int0; i32 fileOffset; i32 size} subs[32]; u8 data[308]`. The 32 "subs" are sub-streams into `sectors.wldx`: **sub[13] = compressed map data (offset+size in wldx), sub[15].size = uncompressed size, sub[11].fileOffset==32 (map data), sub[12].fileOffset==131104 (height data), sub[2].size = numHeights**. |
| **Map cell** (`WldxEntry` 32B) | rs_file.h:158-175 | The core map grid cell, 4096 per sector (64×64). `i32 tileId; i32 staticId; i32 entityId; i32 floorId; u8[8]; i8 smthX,smthY,smthZ,smthW (color/lighting RGBA?); u8 offsetX,offsetY; u8 flags; u8 someTypeId(0-15)`. **Directly comparable to our in-memory world-cell struct — cross-check field order against game RAM.** |
| **Decompressed sector** (`SectorxData`) | rs_file.h:186-191 | `char name[32]; WldxEntry data[4096]; SectorxhHeightEntry heightData[]`. So uncompressed sector blob = 32-byte name + 4096×32B cells (=131104, matches sub[12].fileOffset) + height table. `SectorxhHeightEntry`=36B (rs_file.h:177-184). |
| **Tile mapping** (`PakTile` 64B) | rs_file.h:244-252; rs_resources.cpp:415-445 | `char filename[32] (iso%d.tga); i32 textureId; i32 tileId; i32[6]`. **18 PakTile entries per logical tile** (animation/rotation frames). Lookup: `textureId = tileTexIds[tileId/18]`, local frame = `tileId % 18`. |
| **Floor overlay** (`FloorEntry` 16B, linked list) | rs_file.h:254-262; rs_file.cpp:563-593 | `i32 id; u32 pakTileIds; i32 varC; i32 nextFloorId`. **Bit-packed: diffuseTileId = pakTileIds & 0x1FFFF, alphaMaskTileId = pakTileIds >> 17.** Floors chain via `nextFloorId` (walk until 0). |
| **Static instance** (`PakStatic` 64B, packed) | rs_file.h:322-356; rs_file.cpp:690-713 | Placed object: `i32 id, itemTypeId, field_8; i16 field_C; i32 worldX, worldY; u8; i32 parentId, anotherParentId, nextStaticId; i16 parentOffsetTx, parentOffsetTy; i32 triggerId; ...; u8 layer; i8 smthX,smthY,smthZ; ...`. `worldX/worldY` are world coords; objects chain via parent/next ids and carry a `triggerId`. |
| **Item type** (`PakItemType` 128B, packed) | rs_file.h:358-388; rs_file.cpp:774-798 | Object/item definition: `i32 flags, int_1, itemTextureId, field_C, mixedId, field_14, spawnInfoId, ..., soundProfileId; ...; u8 category; ...; char nameStr[32]; i16 ...; i16 someVectorId; i16 marginX, marginY; ...`. `itemTypeId` from PakStatic indexes here; `mixedId` → sprite atlas. **`nameStr[32]` + `category` are directly useful for an item DB.** |
| **Sprite atlas** (`PakMixedDesc`16B / `PakMixedData`32B / `PakMixedEntry`64B) | rs_file.h:275-320; rs_file.cpp:715-772 | mixed.pak maps an item to N sub-sprites with UVs. `PakMixedData`: `i32 textureId; u16 width,height,x,y; u16 zero[2]; f32 uvX1,uvY1,uvX2,uvY2`. Parser flattens variable-length per-desc arrays into one `mixed[]` pool, rewriting `desc.mixedDataId` to the pool start index; entries with `size==16` are empty. |
| **world.bin** (simple) | rs_file.cpp:531-561 | `u32 count;` then `count` × `{i32 varA,varB,varC}` (`WorldBinEntry` 12B). Purpose not yet decoded. |
| **TGA header** (for iso%d.tga tiles) | rs_file.cpp:329-343 | standard 18-byte TGA, listed for completeness. |

---

## Coordinate / world math (from `rs_game.cpp`) — high value, exact constants

- **World→screen (iso projection), `sacred_worldToScreen` (rs_game.cpp:233):**
  `sx = x*0.89442718 + y*-0.89442718;  sy = x*0.44721359 + y*0.44721359;  sz = z`
  Inverse `sacred_screenToWorld` (rs_game.cpp:240):
  `wx = x*0.44721359 - y*-0.89442718;  wy = y*0.89442718 - x*0.44721359`
  Note `0.89442718 ≈ 2/√5`, `0.44721359 ≈ 1/√5` → a 2:1 isometric (atan(0.5)≈26.57°) basis. This is the
  canonical Sacred world↔screen transform.
- **Sector world-rect → 100×100 grid index (rs_game.cpp:264, validated by assert):**
  `gridX = (posX1 + 25) / 53.66563 / 64;  gridY = (posY1 + 25) / 53.66563 / 64`
  i.e. one tile spans `53.66563` world units along each map axis; a sector is 64 tiles wide.
  `sectorIdMap[gridY*100 + gridX] = sectorId`.
- **Iso 3D rotation basis (rs_game.cpp:254):** `RotateX(60°) * RotateZ(-45°)` (`VIEW_X_ANGLE=1.04719755=60°`).
- **Render tile size:** `TILE_WIDTH = 67.9` units (screen-space quad scale for one tile).
- **Tile-atlas UVs (rs_game.cpp:26-52):** 18 frames laid out in a 256-px atlas; diamond quad UVs at
  rows of `25/256` with `(line&1)*52/256` horizontal stagger — the iso-tile rhombus packing.
- **Cell color/lighting:** `WldxEntry.smthX/Y/Z` packed as `0xff000000 | (smthZ<<16)|(smthY<<8)|smthX`
  (rs_game.cpp:851) → these three bytes are a per-cell RGB tint (likely baked lighting).

---

## Cross-reference to OUR known offsets

Our memory (`talk-signal-0x200.md`) tracks the live game's `cCreature` struct (e.g. +0x200 talk bit 0x400).
Resacred works at the **file/asset layer**, not the live entity layer, so there is **no direct cCreature
offset overlap** — but it complements us:
- Our SDK reads live RAM (cCreature/cWorld) injected via ijl15 proxy. Resacred reads the *cold* data files.
  `WldxEntry`/`PakStatic`/`PakItemType` are the on-disk source for what becomes live entities, so they are the
  schema our live structs are populated FROM. Field names here (`itemTypeId`, `triggerId`, `layer`, `worldX/Y`,
  `category`, `nameStr`) are strong hints for naming/decoding fields we see in live RAM.
- World/base VA: game EXE base `0x00400000`, file offset = VA − 0x400000 (unchanged). Resacred's IDBs
  (`ida_db/Sacred_*.idb`) are RE of that same EXE — if opened in IDA they would give named functions for the
  loaders mirrored in `rs_file.cpp`, directly cross-checkable to our RVAs.

---

## Concrete C++-port candidates (8)

These are self-contained, dependency-light, and immediately useful if we ever decode Sacred assets natively
inside our DLL/tools instead of (or alongside) live-RAM reads.

1. **TODO(port): zlib texture/sector decompressor** — `zlib_decompress()` (rs_file.cpp:345-411). Stock zlib,
   ~65 lines, no engine deps. Foundation for everything else. We already could link zlib in the SDK.
2. **TODO(port): PAK container reader** — `PakHeader`+`PakSubFileDesc` walk (rs_file.h:91-109; pattern in every
   `pak_*Read`). Trivial: read 256B header, then `entryCount` 12B descs, index by `offset`. Handle the
   entry#0-sentinel quirk (skip first for Floor/texture/static/item paks).
3. **TODO(port): texture decoder** — `pak_textureRead()` (rs_file.cpp:499-529). typeId 6 → RGBA8 passthrough;
   typeId 4 → zlib→ARGB4444 (w*h*2). GL upload uses `GL_UNSIGNED_SHORT_4_4_4_4_REV` + `GL_BGRA`
   (rs_resources.cpp:207-212) — note the BGRA/REV ordering when converting 16-bit to 32-bit.
4. **TODO(port): sector map loader** — `keyx_sectorsRead()` (rs_file.cpp:602-681) + `KeyxSector`/`WldxEntry`
   structs. Reads `sectors.keyx` directory, pulls compressed blob from `sectors.wldx` at `subs[13]`, inflates
   to `WldxEntry[4096]`. This is the path to enumerate every map cell offline. Validate `subs[11]==32`,
   `subs[12]==131104` invariants.
5. **TODO(port): static + item-type + mixed loaders** — `pak_staticRead`/`pak_itemRead`/`pak_mixedRead`
   (rs_file.cpp:690-798). Gives placed-object instances, their type defs (incl. `nameStr[32]`, `category`),
   and sprite atlas. Combined → an **offline item/object database** we can diff against live entities.
6. **TODO(port): world↔screen iso transform + sector-grid mapping** — `sacred_worldToScreen` /
   `sacred_screenToWorld` (rs_game.cpp:233-245) and the `(pos+25)/53.66563/64` grid index (rs_game.cpp:264).
   Pure math, ~10 lines, lets us convert live `worldX/worldY` (seen in cCreature/PakStatic) to map sector +
   screen coords — useful for any minimap/overlay/teleport feature in our SDK.
7. **TODO(port): floor-overlay bit unpack** — `diffuse = pakTileIds & 0x1FFFF; alpha = pakTileIds >> 17`,
   linked via `nextFloorId` (rs_file.cpp:583-586, rs_game.cpp:349-360). One-liner, needed to render/understand
   ground decals.
8. **TODO(port): tile→texture lookup convention** — `tileTexIds[tileId/18]`, local frame `tileId % 18`
   (rs_resources.cpp:436-441, rs_game.cpp:341). The "18 frames per tile" rule is non-obvious and required to
   resolve any `tileId` (in `WldxEntry`/`FloorEntry`) to an actual texture.

## Not-yet-decoded / open questions (logged for later)
- `world.bin` `{varA,varB,varC}` records — purpose unknown (rs_file.cpp:531-561).
- `WldxEntry` trailing `u8 rest[8]`, `flags`, `someTypeId(0-15)` — semantics TBD.
- `PakStatic` many `field_*` (parentId chains, triggerId) — partially named; needs IDB cross-check.
- `SectorxhHeightEntry` height table layout (36B, mostly `unk`) — height-map decode incomplete.
- The two `ida_db/Sacred_*.idb` files: open in IDA Pro to recover named loader functions + confirm every
  struct offset above against the real EXE. Highest-leverage follow-up if IDA is available.
