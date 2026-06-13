// iso_transform.h — Sacred world<->screen isometric projection + sector-grid mapping.
//
// SOURCE: sdk/.claude/knowledge/refs_resacred.md
//   - "Coordinate / world math (from rs_game.cpp)" section (lines 73-90):
//       sacred_worldToScreen (rs_game.cpp:233), sacred_screenToWorld (rs_game.cpp:240),
//       sector world-rect -> 100x100 grid index (rs_game.cpp:264).
//   - Port candidate #6 ("world<->screen iso transform + sector-grid mapping").
//   Cross-checked against OUR viewer convention recorded in sdk/CLAUDE.md:
//       iso_x = (wx-wy)*48 ; iso_y = (wx+wy)*24  (a 2:1 isometric basis, same as Resacred's
//       0.894/0.447 = 2/sqrt5, 1/sqrt5 up to a uniform scale).
//
// This is a HEADER-ONLY, dependency-light port (standard <cstdint>/<cmath> only).
// All math is pure; no engine VAs are referenced, so no byte-sig verification is needed.
//
// TODO(port): wire when an SDK feature needs offline world<->screen mapping (minimap overlay,
//   teleport-by-screen-click, sector lookup for a live worldX/worldY). Until then this is a
//   FUTURE-USE header: written, not #included anywhere, not added to SacredSDK.vcxproj.

#ifndef SACRED_PORTS_ASSETS_ISO_TRANSFORM_H
#define SACRED_PORTS_ASSETS_ISO_TRANSFORM_H

#include <cstdint>
#include <cmath>

namespace sacred {
namespace assets {

// --- Resacred's exact engine constants (rs_game.cpp) ---------------------------------------
// 0.89442718 ~= 2/sqrt(5), 0.44721359 ~= 1/sqrt(5). A 2:1 isometric (atan(0.5) ~= 26.57 deg).
constexpr double kIsoBasisMajor = 0.89442718;  // x weight along screen-x, |y| weight too
constexpr double kIsoBasisMinor = 0.44721359;  // x & y weight along screen-y

// Sector-grid mapping constants (rs_game.cpp:264, validated by the engine's own assert).
// One tile spans this many WORLD units along each map axis; a sector is 64 tiles wide.
constexpr double kWorldUnitsPerTile = 53.66563;
constexpr int    kTilesPerSector    = 64;
constexpr int    kSectorGridDim     = 100;   // world map = 100x100 sector grid
constexpr double kGridBiasWorld     = 25.0;  // (pos + 25) before dividing, per rs_game.cpp

// --- Vector POD -----------------------------------------------------------------------------
struct Vec2d { double x, y; };
struct Vec3d { double x, y, z; };

// World -> screen (Resacred basis). z passes through unchanged.
// rs_game.cpp:233:  sx = x*0.894 + y*-0.894 ; sy = x*0.447 + y*0.447 ; sz = z
inline Vec3d WorldToScreen(double wx, double wy, double wz = 0.0) {
    Vec3d s;
    s.x = wx * kIsoBasisMajor + wy * (-kIsoBasisMajor);
    s.y = wx * kIsoBasisMinor + wy * (kIsoBasisMinor);
    s.z = wz;
    return s;
}

// Inverse, screen -> world (rs_game.cpp:240):
//   wx = sx*0.447 - sy*-0.894 ; wy = sy*0.894 - sx*0.447
inline Vec2d ScreenToWorld(double sx, double sy) {
    Vec2d w;
    w.x = sx * kIsoBasisMinor - sy * (-kIsoBasisMajor);
    w.y = sy * kIsoBasisMajor - sx * kIsoBasisMinor;
    return w;
}

// --- OUR viewer/editor convention (sdk/CLAUDE.md & SacredStudio WorldCoord) ------------------
// Integer-friendly variant used by the Python viewer and SacredStudio: iso pixels from tile
// coords. Same 2:1 isometric shape as the Resacred basis, just a fixed pixel scale.
//   iso_x = (wx - wy) * 48 ; iso_y = (wx + wy) * 24
constexpr double kViewerIsoHalfW = 48.0;  // half tile width  in iso pixels
constexpr double kViewerIsoHalfH = 24.0;  // half tile height in iso pixels

inline Vec2d TileToIsoPixels(double wx, double wy) {
    Vec2d p;
    p.x = (wx - wy) * kViewerIsoHalfW;
    p.y = (wx + wy) * kViewerIsoHalfH;
    return p;
}

// Inverse of TileToIsoPixels (tile coords from iso pixels). Solving the 2x2 system.
inline Vec2d IsoPixelsToTile(double ix, double iy) {
    // ix = (wx-wy)*48, iy = (wx+wy)*24  =>  wx = ix/96 + iy/48 ; wy = iy/48 - ix/96
    const double a = ix / (2.0 * kViewerIsoHalfW);  // = (wx-wy)/2
    const double b = iy / (2.0 * kViewerIsoHalfH);  // = (wx+wy)/2
    Vec2d w;
    w.x = b + a;
    w.y = b - a;
    return w;
}

// --- Sector grid mapping --------------------------------------------------------------------
struct GridIndex { int gx, gy; };

// Sector world-rect top-left (posX1/posY1 from KeyxSector) -> 100x100 grid index.
// rs_game.cpp:264:  gridX = (posX1 + 25)/53.66563/64 ; gridY = (posY1 + 25)/53.66563/64
inline GridIndex SectorWorldToGrid(int posX1, int posY1) {
    GridIndex g;
    g.gx = static_cast<int>((posX1 + kGridBiasWorld) / kWorldUnitsPerTile / kTilesPerSector);
    g.gy = static_cast<int>((posY1 + kGridBiasWorld) / kWorldUnitsPerTile / kTilesPerSector);
    return g;
}

// Flatten a grid index into the engine's sectorIdMap layout: sectorIdMap[gy*100 + gx].
inline int GridToLinear(const GridIndex& g) { return g.gy * kSectorGridDim + g.gx; }

// Convenience: which sector grid cell does a live world position fall into?
// Uses the same bias/scale as the sector top-left mapping above.
inline GridIndex WorldPosToGrid(double worldX, double worldY) {
    GridIndex g;
    g.gx = static_cast<int>((worldX + kGridBiasWorld) / kWorldUnitsPerTile / kTilesPerSector);
    g.gy = static_cast<int>((worldY + kGridBiasWorld) / kWorldUnitsPerTile / kTilesPerSector);
    return g;
}

} // namespace assets
} // namespace sacred

#endif // SACRED_PORTS_ASSETS_ISO_TRANSFORM_H
