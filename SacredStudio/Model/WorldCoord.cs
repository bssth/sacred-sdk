using System.Text.Json.Serialization;

namespace SacredStudio.Model;

/// <summary>
/// A position on the Sacred world tile grid. (wx, wy) are absolute world tile
/// coordinates — the same space the runtime Lua API consumes via o:teleport(kx, ky)
/// and sacred.spawn_item(type, kx, ky). Sector origins sit on a 64-tile lattice:
/// origin = grid * SECTOR_SIZE.
/// </summary>
public readonly record struct WorldTile(int Wx, int Wy)
{
    public const int SectorSize = 64;

    /// <summary>Owning sector grid cell (origin / 64), floored.</summary>
    [JsonIgnore]
    public (int Gx, int Gy) SectorGrid =>
        ((int)Math.Floor(Wx / (double)SectorSize), (int)Math.Floor(Wy / (double)SectorSize));

    /// <summary>Local tile coordinate inside the owning sector (0..63).</summary>
    [JsonIgnore]
    public (int Lx, int Ly) Local =>
        (((Wx % SectorSize) + SectorSize) % SectorSize, ((Wy % SectorSize) + SectorSize) % SectorSize);

    public override string ToString() => $"({Wx}, {Wy})";
}

/// <summary>
/// Isometric projected pixel position, matching the Python viewer exactly:
///   iso_x = (wx - wy) * (ISO_STEP_W / 2)
///   iso_y = (wx + wy) * (ISO_STEP_H / 2)
/// with ISO_STEP_W = 96, ISO_STEP_H = 48. Used for placing markers on a baked
/// LOD overview / iso canvas.
/// </summary>
public readonly record struct IsoPoint(double X, double Y);

public static class IsoProjection
{
    public const int IsoStepW = 96;
    public const int IsoStepH = 48;

    public static IsoPoint WorldToIso(WorldTile t) =>
        new((t.Wx - t.Wy) * (IsoStepW / 2.0), (t.Wx + t.Wy) * (IsoStepH / 2.0));

    /// <summary>
    /// Inverse projection: iso pixel -> nearest world tile. Derived by solving
    ///   ix = (wx - wy) * 48 ; iy = (wx + wy) * 24
    /// => wx = ix/96 + iy/48 ; wy = iy/48 - ix/96.
    /// </summary>
    public static WorldTile IsoToWorld(double ix, double iy)
    {
        double wx = ix / IsoStepW + iy / IsoStepH;
        double wy = iy / IsoStepH - ix / IsoStepW;
        return new WorldTile((int)Math.Round(wx), (int)Math.Round(wy));
    }
}
