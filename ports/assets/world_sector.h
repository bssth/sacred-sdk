// world_sector.h — Sacred sectors.keyx / sectors.wldx loader (sector directory + map grid).
//
// SOURCE: sdk/.claude/knowledge/refs_resacred.md
//   - "Sector directory (KeyxSector 0x300=768B)" (line 60) + parse refs (rs_file.cpp:602-681,
//     rs_resources.cpp:447-514):
//       char name[32]; i32; i32 sectorId; i32; u16 neighbourIds[8];
//       i32 posX1,posY1,posX2,posY2; {i32 int0; i32 fileOffset; i32 size} subs[32]; u8 data[308].
//       sub[13] = compressed map data (offset+size in wldx); sub[15].size = uncompressed size;
//       INVARIANTS: sub[11].fileOffset == 32 (map data), sub[12].fileOffset == 131104 (height).
//   - "Map cell (WldxEntry 32B)" (line 61): i32 tileId; i32 staticId; i32 entityId; i32 floorId;
//       u8[8]; i8 smthX,smthY,smthZ,smthW; u8 offsetX,offsetY; u8 flags; u8 someTypeId.
//   - "Decompressed sector (SectorxData)" (line 62): char name[32]; WldxEntry data[4096];
//       SectorxhHeightEntry heightData[]. 32 + 4096*32 = 131104 (matches sub[12].fileOffset).
//   - "Tile mapping (PakTile 64B)" (line 63): 18 PakTile entries per logical tile;
//       textureId = tileTexIds[tileId/18], frame = tileId % 18.
//   - "Floor overlay (FloorEntry 16B)" (line 64): diffuse = pakTileIds & 0x1FFFF,
//       alpha = pakTileIds >> 17; chain via nextFloorId until 0.
//   - "Cell color/lighting" (line 89): argb = 0xff000000 | (smthZ<<16)|(smthY<<8)|smthX.
//   - Port candidates #4 (sector map loader) and #7/#8 (floor unpack, tile->texture lookup).
//
// This is a HEADER-LIGHT port: structs + free functions. zlib is injected (no direct dependency),
// declared against the future herosave zlib_codec interface — see ZlibInflateFn.
//
// TODO(port): wire when the SDK enumerates map cells offline (minimap, sector overlay, teleport
//   targeting). FUTURE-USE only — not added to SacredSDK.vcxproj, not #included anywhere.
// TODO(port): share the herosave zlib_codec for InflateFn.
// TODO(verify): no absolute engine VAs are referenced (all offsets/invariants are in-file), so no
//   byte-sig vs Sacred_decrypted.exe is required.

#ifndef SACRED_PORTS_ASSETS_WORLD_SECTOR_H
#define SACRED_PORTS_ASSETS_WORLD_SECTOR_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace sacred {
namespace assets {

// --- Constants / invariants -----------------------------------------------------------------
constexpr std::size_t kKeyxSectorSize     = 0x300; // 768 bytes
constexpr std::size_t kWldxEntrySize      = 32;
constexpr std::size_t kCellsPerSector     = 4096;  // 64x64
constexpr std::size_t kSectorEdgeTiles    = 64;
constexpr std::size_t kKeyxNumSubs        = 32;

// Sub-stream indices into sectors.wldx (from rs_file.cpp / rs_resources.cpp).
constexpr std::size_t kSubMapData         = 13;  // compressed map data (offset+size in wldx)
constexpr std::size_t kSubUncompressedLen = 15;  // sub[15].size = uncompressed size
constexpr std::size_t kSubNumHeights      = 2;   // sub[2].size  = numHeights

// Invariant fileOffset values used to validate a parsed KeyxSector.
constexpr int32_t kInvariantSub11FileOffset = 32;     // sub[11].fileOffset == 32  (map data)
constexpr int32_t kInvariantSub12FileOffset = 131104; // sub[12].fileOffset == 131104 (height)

// Decompressed sector blob layout: 32-byte name + 4096*32 cells = 131104 (== height fileOffset).
constexpr std::size_t kSectorNameBytes    = 32;
constexpr std::size_t kSectorCellsBytes    = kCellsPerSector * kWldxEntrySize; // 131072
constexpr std::size_t kSectorCellsEnd      = kSectorNameBytes + kSectorCellsBytes; // 131104

// Tile mapping: 18 PakTile entries per logical tile.
constexpr int32_t kPakTilesPerLogicalTile = 18;

// Floor bit-packing masks.
constexpr uint32_t kFloorDiffuseMask = 0x1FFFF; // diffuse = pakTileIds & 0x1FFFF
constexpr uint32_t kFloorAlphaShift  = 17;      // alpha   = pakTileIds >> 17

// --- zlib codec hook (shared with future herosave zlib_codec) -------------------------------
// TODO(port): point at the herosave zlib_codec decompress fn when it exists.
using ZlibInflateFn = bool(*)(const uint8_t* in, std::size_t inSize,
                              uint8_t* out, std::size_t outMax, std::size_t* outSize);

// --- KeyxSector (768B sector directory entry) -----------------------------------------------
#pragma pack(push, 1)
struct KeyxSub {            // one of the 32 sub-stream descriptors
    int32_t int0;
    int32_t fileOffset;    // offset into sectors.wldx (or an invariant sentinel value)
    int32_t size;
};
struct KeyxSector {
    char     name[32];
    int32_t  unk0;
    int32_t  sectorId;
    int32_t  unk1;
    uint16_t neighbourIds[8];
    int32_t  posX1, posY1, posX2, posY2; // world-rect; feed posX1/posY1 to iso_transform grid map
    KeyxSub  subs[kKeyxNumSubs];          // 32 * 12 = 384 bytes
    uint8_t  data[308];
};
#pragma pack(pop)
static_assert(sizeof(KeyxSector) == kKeyxSectorSize, "KeyxSector must be 768 bytes");

// --- WldxEntry (32B map cell) ---------------------------------------------------------------
#pragma pack(push, 1)
struct WldxEntry {
    int32_t tileId;    // resolve via tileTexIds[tileId/18], frame = tileId%18
    int32_t staticId;  // -> Static.pak instance
    int32_t entityId;
    int32_t floorId;   // -> Floor.PAK overlay chain (FloorEntry)
    uint8_t rest[8];   // TODO: semantics TBD (refs_resacred.md open question)
    int8_t  smthX, smthY, smthZ, smthW; // per-cell color/lighting (RGB tint + ?)
    uint8_t offsetX, offsetY;
    uint8_t flags;     // TODO: semantics TBD
    uint8_t someTypeId; // 0..15, TODO: semantics TBD
};
#pragma pack(pop)
static_assert(sizeof(WldxEntry) == kWldxEntrySize, "WldxEntry must be 32 bytes");

// --- Decoded sector -------------------------------------------------------------------------
struct DecodedSector {
    char      name[32] = {0};
    int32_t   sectorId = 0;
    int32_t   posX1 = 0, posY1 = 0, posX2 = 0, posY2 = 0;
    std::vector<WldxEntry> cells; // 4096 entries on success (64x64)
    // Height data is left raw (SectorxhHeightEntry layout is only partially decoded upstream).
    std::vector<uint8_t> heightRaw;
    bool valid() const { return cells.size() == kCellsPerSector; }

    // Indexing helper: cell (tx, ty) within the sector, 0 <= tx,ty < 64.
    const WldxEntry* cell(std::size_t tx, std::size_t ty) const {
        if (tx >= kSectorEdgeTiles || ty >= kSectorEdgeTiles) return nullptr;
        if (cells.size() != kCellsPerSector) return nullptr;
        return &cells[ty * kSectorEdgeTiles + tx];
    }
};

// --- KeyxSector helpers (header-only; inline so the .h stands alone) -------------------------
// Parse one 768-byte KeyxSector from `data` (must have >= 768 bytes). false if too small.
inline bool ReadKeyxSector(const uint8_t* data, std::size_t size, KeyxSector* out) {
    if (!data || !out || size < kKeyxSectorSize) return false;
    // memcpy a packed struct == on-disk layout. <cstring> is pulled in by the .cpp-free helpers
    // below via this header's own includes; we declare the prototype to avoid a hard dependency.
    for (std::size_t i = 0; i < kKeyxSectorSize; ++i)
        reinterpret_cast<uint8_t*>(out)[i] = data[i];
    return true;
}

// Validate the two known invariants (sub[11].fileOffset==32, sub[12].fileOffset==131104).
// Returns true if both hold. A failing sector signals a parse/version mismatch, not corruption
// per se — callers may log and continue.
inline bool CheckKeyxInvariants(const KeyxSector& s) {
    return s.subs[11].fileOffset == kInvariantSub11FileOffset
        && s.subs[12].fileOffset == kInvariantSub12FileOffset;
}

// Read the whole sectors.keyx directory: a PakHeader-style header is NOT assumed here — Resacred
// reads a contiguous array of KeyxSector after a 256-byte header (the keyx file is itself a
// ResacredPak whose payload region holds KeyxSector[]). Callers that already located the array
// start pass it directly. `count` is how many 768-byte records `data` holds.
inline std::vector<KeyxSector> ReadKeyxDirectory(const uint8_t* data, std::size_t size,
                                                 std::size_t count) {
    std::vector<KeyxSector> v;
    if (!data) return v;
    const std::size_t maxFit = size / kKeyxSectorSize;
    if (count > maxFit) count = maxFit;
    v.resize(count);
    for (std::size_t i = 0; i < count; ++i)
        ReadKeyxSector(data + i * kKeyxSectorSize, kKeyxSectorSize, &v[i]);
    return v;
}

// --- Sector decompression -------------------------------------------------------------------
// Inflate a sector's map blob. `wldx` is the whole sectors.wldx buffer; the sector's compressed
// map lives at subs[13].fileOffset for subs[13].size bytes, inflating to subs[15].size bytes
// (which must be >= the name(32) + cells(131072) region). On success fills `out` with the 4096
// WldxEntry cells (+ raw height tail). `inflate` is the injected zlib codec.
inline bool DecodeSector(const KeyxSector& s,
                         const uint8_t* wldx, std::size_t wldxSize,
                         ZlibInflateFn inflate,
                         DecodedSector* out) {
    if (!out || !wldx || !inflate) return false;
    out->cells.clear();
    out->heightRaw.clear();

    const KeyxSub& map = s.subs[kSubMapData];        // sub[13]
    const KeyxSub& ulen = s.subs[kSubUncompressedLen]; // sub[15]
    if (map.fileOffset < 0 || map.size <= 0 || ulen.size <= 0) return false;

    const std::size_t off = static_cast<std::size_t>(map.fileOffset);
    const std::size_t inSz = static_cast<std::size_t>(map.size);
    if (off > wldxSize || inSz > wldxSize - off) return false;

    const std::size_t outMax = static_cast<std::size_t>(ulen.size);
    if (outMax < kSectorCellsEnd) return false; // must at least cover name + 4096 cells

    std::vector<uint8_t> raw(outMax);
    std::size_t produced = 0;
    if (!inflate(wldx + off, inSz, raw.data(), outMax, &produced)) return false;
    if (produced < kSectorCellsEnd) return false;

    // name[32]
    for (std::size_t i = 0; i < kSectorNameBytes; ++i) out->name[i] = static_cast<char>(raw[i]);
    out->sectorId = s.sectorId;
    out->posX1 = s.posX1; out->posY1 = s.posY1; out->posX2 = s.posX2; out->posY2 = s.posY2;

    // cells[4096]
    out->cells.resize(kCellsPerSector);
    for (std::size_t i = 0; i < kCellsPerSector; ++i) {
        const uint8_t* src = raw.data() + kSectorNameBytes + i * kWldxEntrySize;
        uint8_t* dst = reinterpret_cast<uint8_t*>(&out->cells[i]);
        for (std::size_t b = 0; b < kWldxEntrySize; ++b) dst[b] = src[b];
    }

    // remaining bytes after the cell block are the raw height table (layout partially decoded).
    if (produced > kSectorCellsEnd)
        out->heightRaw.assign(raw.begin() + kSectorCellsEnd, raw.begin() + produced);

    return out->valid();
}

// --- Floor overlay bit-unpack (FloorEntry 16B) ----------------------------------------------
#pragma pack(push, 1)
struct FloorEntry {
    int32_t  id;
    uint32_t pakTileIds; // bit-packed: diffuse = &0x1FFFF, alpha = >>17
    int32_t  varC;
    int32_t  nextFloorId; // chain until 0
};
#pragma pack(pop)
static_assert(sizeof(FloorEntry) == 16, "FloorEntry must be 16 bytes");

struct FloorTiles { int32_t diffuseTileId; int32_t alphaMaskTileId; };

inline FloorTiles UnpackFloor(uint32_t pakTileIds) {
    FloorTiles f;
    f.diffuseTileId   = static_cast<int32_t>(pakTileIds & kFloorDiffuseMask);
    f.alphaMaskTileId = static_cast<int32_t>(pakTileIds >> kFloorAlphaShift);
    return f;
}

// --- Tile -> texture lookup convention ------------------------------------------------------
// 18 PakTile frames per logical tile: the texture is keyed by tileId/18, the animation/rotation
// frame within that texture is tileId%18. `tileTexIds` is the array built by loadTileTextureIds
// (one textureId per logical tile). Returns false if the index is out of range.
struct TileLookup { int32_t textureId; int32_t frame; };

inline bool ResolveTile(int32_t tileId, const int32_t* tileTexIds, std::size_t tileTexCount,
                        TileLookup* out) {
    if (tileId < 0 || !tileTexIds || !out) return false;
    const std::size_t logical = static_cast<std::size_t>(tileId) / kPakTilesPerLogicalTile;
    if (logical >= tileTexCount) return false;
    out->textureId = tileTexIds[logical];
    out->frame     = tileId % kPakTilesPerLogicalTile;
    return true;
}

// --- Per-cell color/lighting ----------------------------------------------------------------
// argb = 0xff000000 | (smthZ<<16) | (smthY<<8) | smthX  (rs_game.cpp:851). The smth* fields are
// stored as signed bytes on disk but used as 0..255 color components, so mask to a byte.
inline uint32_t CellTintARGB(const WldxEntry& e) {
    const uint32_t x = static_cast<uint8_t>(e.smthX);
    const uint32_t y = static_cast<uint8_t>(e.smthY);
    const uint32_t z = static_cast<uint8_t>(e.smthZ);
    return 0xff000000u | (z << 16) | (y << 8) | x;
}

} // namespace assets
} // namespace sacred

#endif // SACRED_PORTS_ASSETS_WORLD_SECTOR_H
