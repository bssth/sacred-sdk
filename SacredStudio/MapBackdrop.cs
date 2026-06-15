using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Windows.Media.Imaging;

namespace SacredStudio;

/// <summary>
/// Real Sacred world terrain backdrop from the Python map viewer's baked images,
/// in the SAME iso space the editor uses (iso = ((wx-wy)*48,(wx+wy)*24)) so terrain
/// aligns 1:1 with markers. Two levels of detail:
///
///  • LOD16 — 113 overview chunks (2048² PNG). Chunk (cx,cy) → iso square
///    [cx*32768..+32768]². Cheap; used when zoomed far out.
///  • LOD8  — 6050 per-sector PNGs (769×385, one 64×64-tile sector each, factor 8).
///    Twice the LOD16 detail; used when zoomed in. Sector grid→id comes from
///    Assets/map/lod8_sectors.json (exported from the game's sectors.keyx).
///
/// Geometry (viewer.py): OVERVIEW_CHUNK_PX=2048, factor 16 → 32768 iso/chunk.
/// LOD8 sector at grid (gx,gy): origin=(gx*64,gy*64); the image's top-left in iso is
/// (((gx-gy)*64-63)*48, (gx+gy)*64*24); its iso size is imagepx*8.
/// </summary>
public sealed class MapBackdrop
{
    public const double ChunkIsoSize = 2048.0 * 16.0;   // 32768 iso px per LOD16 chunk
    public const int Lod8Factor = 8;
    private const int ChunkDecodeWidth = 2048;          // full native LOD16

    // Optional baked LOD overview (far-zoom fallback; the native GL render covers near
    // zoom). Ships only lod8_sectors.json; the LOD8/LOD16 PNGs are an external bake
    // pointed at by SACRED_MAP_CACHE (a sacred_map_viewer cache dir). Absent → no
    // overview backdrop, which is fine — GL takes over on zoom-in.
    private static readonly string? MapCache = Environment.GetEnvironmentVariable("SACRED_MAP_CACHE");
    private static readonly string[] Lod16Dirs =
    {
        Path.Combine(AppContext.BaseDirectory, "Assets", "map"),
        MapCache is null ? "" : Path.Combine(MapCache, "lod16"),
    };
    private static readonly string[] Lod8Dirs =
    {
        Path.Combine(AppContext.BaseDirectory, "Assets", "map", "lod8"),
        MapCache is null ? "" : Path.Combine(MapCache, "lod8"),
    };
    private static readonly string[] SectorJsonPaths =
    {
        Path.Combine(AppContext.BaseDirectory, "Assets", "map", "lod8_sectors.json"),
        Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "Assets", "map", "lod8_sectors.json"),
    };

    private static readonly Regex ChunkRe =
        new(@"^chunk_([+-]\d+)_([+-]\d+)\.png$", RegexOptions.Compiled | RegexOptions.IgnoreCase);

    // ---- LOD16 ----
    private readonly string? _chunkDir;
    private readonly HashSet<(int cx, int cy)> _chunks = new();
    private readonly Dictionary<(int cx, int cy), BitmapSource?> _chunkCache = new();

    // ---- LOD8 ----
    private readonly string? _sectorDir;
    private readonly Dictionary<(int gx, int gy), int> _sectorGrid = new();   // grid -> sector_id
    private readonly Dictionary<(int gx, int gy), BitmapSource?> _sectorCache = new();

    public bool Available => _chunkDir is not null && _chunks.Count > 0;
    public bool HasLod8 => _sectorDir is not null && _sectorGrid.Count > 0;

    public MapBackdrop()
    {
        // LOD16 chunks
        foreach (var d in Lod16Dirs)
        {
            if (!Directory.Exists(d)) continue;
            try
            {
                foreach (var f in Directory.EnumerateFiles(d, "chunk_*.png"))
                {
                    var m = ChunkRe.Match(Path.GetFileName(f));
                    if (m.Success)
                        _chunks.Add((int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture),
                                     int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture)));
                }
            }
            catch { }
            if (_chunks.Count > 0) { _chunkDir = d; break; }
        }

        // LOD8 sector grid map
        foreach (var jp in SectorJsonPaths)
        {
            try
            {
                if (!File.Exists(jp)) continue;
                using var doc = JsonDocument.Parse(File.ReadAllText(jp));
                if (!doc.RootElement.TryGetProperty("sectors", out var secs)) continue;
                foreach (var kv in secs.EnumerateObject())
                {
                    int sid = int.Parse(kv.Name, CultureInfo.InvariantCulture);
                    var arr = kv.Value;
                    int gx = arr[0].GetInt32(), gy = arr[1].GetInt32();
                    _sectorGrid[(gx, gy)] = sid;
                }
            }
            catch { _sectorGrid.Clear(); }
            if (_sectorGrid.Count > 0) break;
        }
        if (_sectorGrid.Count > 0)
            foreach (var d in Lod8Dirs) { if (Directory.Exists(d)) { _sectorDir = d; break; } }
    }

    public bool TryGetIsoBounds(out double x0, out double y0, out double x1, out double y1)
    {
        x0 = y0 = x1 = y1 = 0;
        if (!Available) return false;
        int minX = int.MaxValue, minY = int.MaxValue, maxX = int.MinValue, maxY = int.MinValue;
        foreach (var (cx, cy) in _chunks)
        {
            minX = Math.Min(minX, cx); minY = Math.Min(minY, cy);
            maxX = Math.Max(maxX, cx); maxY = Math.Max(maxY, cy);
        }
        x0 = minX * ChunkIsoSize; y0 = minY * ChunkIsoSize;
        x1 = (maxX + 1) * ChunkIsoSize; y1 = (maxY + 1) * ChunkIsoSize;
        return true;
    }

    // ---- LOD16 chunk access ----
    public BitmapSource? GetChunk(int cx, int cy)
    {
        var key = (cx, cy);
        if (_chunkCache.TryGetValue(key, out var c)) return c;
        BitmapSource? bmp = null;
        if (_chunkDir is not null && _chunks.Contains(key))
            bmp = Load(Path.Combine(_chunkDir, $"chunk_{cx:+00000;-00000}_{cy:+00000;-00000}.png"), ChunkDecodeWidth);
        _chunkCache[key] = bmp;
        return bmp;
    }

    // ---- LOD8 sector access ----
    /// <summary>Iso top-left of a sector image at grid (gx,gy).</summary>
    public static (double x, double y) SectorIsoOrigin(int gx, int gy) =>
        (((gx - gy) * 64.0 - 63.0) * 48.0, (gx + gy) * 64.0 * 24.0);

    public bool HasSector(int gx, int gy) => _sectorGrid.ContainsKey((gx, gy));

    public BitmapSource? GetSector(int gx, int gy)
    {
        var key = (gx, gy);
        if (_sectorCache.TryGetValue(key, out var c)) return c;
        BitmapSource? bmp = null;
        if (_sectorDir is not null && _sectorGrid.TryGetValue(key, out int sid))
            bmp = Load(Path.Combine(_sectorDir, $"sector_{sid:000000}.png"), 0);  // native res
        _sectorCache[key] = bmp;
        return bmp;
    }

    private static BitmapSource? Load(string path, int decodeWidth)
    {
        try
        {
            var bi = new BitmapImage();
            bi.BeginInit();
            bi.CacheOption = BitmapCacheOption.OnLoad;
            bi.CreateOptions = BitmapCreateOptions.IgnoreColorProfile;
            if (decodeWidth > 0) bi.DecodePixelWidth = decodeWidth;
            bi.UriSource = new Uri(path);
            bi.EndInit();
            bi.Freeze();
            return bi;
        }
        catch { return null; }
    }
}
