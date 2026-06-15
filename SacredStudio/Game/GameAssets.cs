using System.IO;
using System.IO.Compression;

namespace SacredStudio.Game;

/// <summary>
/// Native (C#) decode of the Sacred game files the map renderer needs — no Python,
/// no pre-baked assets. Loads tiles.pak / Floor.pak / sectors.keyx fully, streams
/// texture.pak (820 MB) from disk, and keeps sectors.wldx in memory (61 MB). All
/// formats verified against our RE + the community SacredEngineRemake.
///
///  • tile_id → (texture sheet entry, sub-tile slot 0..17)   [tiles.pak]
///  • sheet entry → 256² BGRA pixels (decoded on demand)     [texture.pak]
///  • sector (gx,gy) → ground tile ids + corner tints + FLOOR overlay chains
///    [sectors.keyx + sectors.wldx + Floor.pak]
/// </summary>
public sealed class GameAssets
{
    // 18 staggered sub-tile origins inside a sheet (a tile is 100×50 at one of these).
    public static readonly (int x, int y)[] SubPositions =
    {
        (0,0),(104,0),(52,25),(156,25),(0,50),(104,50),(52,75),(156,75),
        (0,100),(104,100),(52,125),(156,125),(0,150),(104,150),(52,175),(156,175),(0,200),(104,200),
    };

    public bool Available { get; }
    public string? GameDir { get; }

    public readonly record struct SectorInfo(int Gx, int Gy, int Ox, int Oy, uint Sid);
    public readonly record struct TileRef(int Sheet, int Sub);
    public readonly record struct FloorOverlay(int LocalIdx, long Primary, long Secondary, int Depth);
    // Liquid surface: a tiling water/lava texture (sheet entry) with per-corner alpha.
    public readonly record struct LiquidSurface(int LocalIdx, int Sheet, byte AL, byte AT, byte AR, byte AB);

    public sealed class SectorDecode
    {
        public uint[] Ground = new uint[4096];
        public byte[] Tints = new byte[4096 * 4];      // L,T,R,B per tile
        public FloorOverlay[] Floors = Array.Empty<FloorOverlay>();
        public LiquidSurface[] Liquids = Array.Empty<LiquidSurface>();
    }

    // tiles.pak: tile_id → (sheet name, tile number) → (texture entry index, sub)
    private TileRef[] _tileRefs = Array.Empty<TileRef>();   // by tile_id; Sheet<0 = none

    // texture.pak (streamed)
    private FileStream? _texStream;
    private readonly object _texLock = new();
    private record struct TexRec(long Offset, int Size, int W, int H, byte Type);
    private TexRec[] _texRecs = Array.Empty<TexRec>();
    private Dictionary<string, int> _texByName = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Static sprites (trees/buildings) — native Static/ITEMS/mixed decode.</summary>
    public SpriteAssets? Sprites { get; private set; }

    // world
    private byte[]? _wldx;
    private readonly Dictionary<uint, KeyxEnt> _entById = new();
    private readonly Dictionary<(int, int), SectorInfo> _byGrid = new();
    private readonly List<SectorInfo> _sectors = new();

    // Floor.pak chain walking
    private PakFile? _floorPak;

    private const int TileRecSize = 0x20;
    private const int FloorHeadOff = 0x0C;
    private const int FloorRecRefOff = 0x04, FloorRecNextOff = 0x0C;
    private const uint PrimaryMask = 0x1FFFF;
    private const int SecondaryShift = 17;
    private const uint SecondaryMask = 0x7FFF;
    private const int FloorChainMax = 128;

    private readonly record struct KeyxEnt(uint Id, int TilesRel, int TilesSize, int CompOff, int CompSize, byte Style90, byte StyleA0);

    private const int SurfaceTypeOff = 0x1F;
    private const int LiquidAlphaOff = 0x10;     // L,T,R,B as sbyte at +0x10..+0x13

    public IReadOnlyList<SectorInfo> Sectors => _sectors;

    public GameAssets()
    {
        foreach (var dir in CandidateDirs())
        {
            try
            {
                string pak = Path.Combine(dir, "Pak");
                string world = Path.Combine(dir, "World");
                string tiles = Path.Combine(pak, "tiles.pak");
                string tex = Path.Combine(pak, "texture.pak");
                string keyx = Path.Combine(world, "sectors.keyx");
                string wldx = Path.Combine(world, "sectors.wldx");
                string floor = Path.Combine(world, "Floor.pak");
                if (!File.Exists(tiles) || !File.Exists(tex) || !File.Exists(keyx) || !File.Exists(wldx))
                    continue;

                _texStream = new FileStream(tex, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 16, FileOptions.RandomAccess);
                LoadTiles(tiles);
                _wldx = File.ReadAllBytes(wldx);
                LoadKeyx(keyx);
                if (File.Exists(floor)) _floorPak = PakFile.Load(floor);

                try { Sprites = new SpriteAssets(this, pak, world); }
                catch { Sprites = null; }

                GameDir = dir;
                Available = _tileRefs.Length > 0 && _sectors.Count > 0;
                break;
            }
            catch { /* try next candidate dir */ }
        }
    }

    private static IEnumerable<string> CandidateDirs()
    {
        // 1) explicit override
        var env = Environment.GetEnvironmentVariable("SACRED_DIR");
        if (!string.IsNullOrWhiteSpace(env)) yield return env;
        // 2) climb from the exe: the SDK lives at <game>\sdk\, so a build under
        //    <game>\sdk\SacredStudio\bin\Debug\... finds the game root by walking up.
        string d = AppContext.BaseDirectory;
        for (int i = 0; i < 10 && d.Length > 3; i++)
        {
            if (Directory.Exists(Path.Combine(d, "Pak")) && Directory.Exists(Path.Combine(d, "World")))
                yield return d;
            d = Path.GetDirectoryName(d.TrimEnd(Path.DirectorySeparatorChar)) ?? d;
        }
        // 3) last resort
        yield return Directory.GetCurrentDirectory();
    }

    // ---- tiles.pak + texture.pak index ----
    private void LoadTiles(string tilesPath)
    {
        var byName = IndexTexture();      // texture.pak is 820 MB — index via the stream
        _texByName = byName;

        var tp = PakFile.Load(tilesPath);
        _tileRefs = new TileRef[tp.Count];
        for (int i = 0; i < tp.Count; i++)
        {
            _tileRefs[i] = new TileRef(-1, 0);
            var de = tp.Descriptors[i];
            if (de.Size < 0x28 || de.Offset == 0) continue;
            int off = (int)de.Offset;
            string fn = tp.ReadCString(off, 0x20).ToLowerInvariant();
            uint tnum = tp.U32(off + 0x24);
            int sheet = -1;
            if (!byName.TryGetValue(fn, out sheet))
            {
                int dot = fn.LastIndexOf('.');
                if (dot <= 0 || !byName.TryGetValue(fn[..dot], out sheet)) sheet = -1;
            }
            if (sheet >= 0) _tileRefs[i] = new TileRef(sheet, (int)(tnum % 18));
        }
    }

    /// <summary>Index texture.pak (descriptors + per-entry 0x50 headers) without
    /// loading the whole 820 MB file; returns a name→entry-index map.</summary>
    private Dictionary<string, int> IndexTexture()
    {
        var byName = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var fs = _texStream!;
        var hdr = new byte[PakFile.HeaderSize];
        fs.Position = 0; fs.ReadExactly(hdr);
        uint c32 = BitConverter.ToUInt32(hdr, 4);
        ushort c16 = BitConverter.ToUInt16(hdr, 4);
        long max = (fs.Length - PakFile.HeaderSize) / PakFile.DescriptorSize;
        int count = c32 <= max ? (int)c32 : (c16 <= max ? c16 : 0);

        var desc = new byte[(long)count * PakFile.DescriptorSize];
        fs.Position = PakFile.HeaderSize; fs.ReadExactly(desc);
        _texRecs = new TexRec[count];
        var eh = new byte[0x50];
        for (int i = 0; i < count; i++)
        {
            uint off = BitConverter.ToUInt32(desc, i * PakFile.DescriptorSize + 4);
            uint size = BitConverter.ToUInt32(desc, i * PakFile.DescriptorSize + 8);
            if (off == 0 || size == 0 || off + 0x50 > (ulong)fs.Length) continue;
            fs.Position = off; fs.ReadExactly(eh);
            int end = 0; while (end < 0x20 && eh[end] != 0) end++;
            string name = System.Text.Encoding.Latin1.GetString(eh, 0, end).ToLowerInvariant();
            _texRecs[i] = new TexRec(off, (int)size, BitConverter.ToUInt16(eh, 0x20), BitConverter.ToUInt16(eh, 0x22), eh[0x24]);
            if (name.Length == 0) continue;
            byName[name] = i;
            int dot = name.LastIndexOf('.');
            if (dot > 0) byName[name[..dot]] = i;
        }
        return byName;
    }

    public bool TryTile(long tileId, out TileRef r)
    {
        if ((ulong)tileId < (ulong)_tileRefs.Length)
        {
            r = _tileRefs[tileId];
            return r.Sheet >= 0;
        }
        r = default; return false;
    }

    public (int w, int h) SheetDim(int sheet) =>
        (uint)sheet < (uint)_texRecs.Length ? (_texRecs[sheet].W, _texRecs[sheet].H) : (256, 256);

    public int SheetCount => _texRecs.Length;

    /// <summary>Decode a texture.pak sheet by name (for sprite composition).</summary>
    internal (byte[]? px, int w, int h) DecodeSheetByName(string name)
    {
        name = name.ToLowerInvariant();
        if (!_texByName.TryGetValue(name, out int idx))
        {
            int dot = name.LastIndexOf('.');
            if (dot <= 0 || !_texByName.TryGetValue(name[..dot], out idx)) return (null, 0, 0);
        }
        return DecodeSheet(idx);
    }

    /// <summary>Decompressed raw 0x20-byte tile records for a sector (for static walk).</summary>
    internal byte[]? SectorTilesRaw(uint sid) =>
        _entById.TryGetValue(sid, out var e) ? DecompressSectorTiles(e) : null;

    /// <summary>Decode a texture.pak sheet to BGRA (called on the streamer's bg thread).</summary>
    public (byte[]? px, int w, int h) DecodeSheet(int sheet)
    {
        if ((uint)sheet >= (uint)_texRecs.Length || _texStream is null) return (null, 0, 0);
        var rec = _texRecs[sheet];
        if (rec.W <= 0 || rec.H <= 0) return (null, 0, 0);
        byte[] payload;
        long payOff = rec.Offset + 0x50;
        lock (_texLock)
        {
            long avail = _texStream.Length - payOff;
            if (avail <= 0) return (null, 0, 0);
            int len = (int)Math.Min(rec.Size, avail);
            payload = new byte[len];
            _texStream.Position = payOff;
            _texStream.ReadExactly(payload, 0, len);
        }
        var px = TextureDecode.ToBgra(rec.Type, payload, rec.W, rec.H);
        return (px, rec.W, rec.H);
    }

    // ---- keyx / wldx ----
    private void LoadKeyx(string path)
    {
        byte[] d = File.ReadAllBytes(path);
        const int hdr = 0x100, ent = 0x300;
        uint c32 = BitConverter.ToUInt32(d, 4);
        ushort c16 = BitConverter.ToUInt16(d, 4);
        long max = Math.Max(0, (d.Length - hdr) / ent);
        int count = c32 <= max ? (int)c32 : (c16 <= max ? c16 : 0);

        var raws = new List<(uint id, int rx, int ry, KeyxEnt e)>(count);
        for (int i = 0; i < count; i++)
        {
            int o = hdr + i * ent;
            if (o + ent > d.Length) break;
            uint id = BitConverter.ToUInt32(d, o + 0x024);
            int rx = BitConverter.ToInt32(d, o + 0x3C), ry = BitConverter.ToInt32(d, o + 0x40);
            var e = new KeyxEnt(id,
                (int)BitConverter.ToUInt32(d, o + 0x0D4), (int)BitConverter.ToUInt32(d, o + 0x0D8),
                (int)BitConverter.ToUInt32(d, o + 0x0EC), (int)BitConverter.ToUInt32(d, o + 0x0F0),
                d[o + 0x2E0], d[o + 0x2E1]);
            raws.Add((id, rx, ry, e));
            _entById[id] = e;
        }

        float scale = InferScale(raws);
        foreach (var (id, rx, ry, _) in raws)
        {
            if (id == 0) continue;
            int ox = (int)Math.Round((rx + 0x19) * scale / 64) * 64;
            int oy = (int)Math.Round((ry + 0x19) * scale / 64) * 64;
            var si = new SectorInfo(ox / 64, oy / 64, ox, oy, id);
            _byGrid[(si.Gx, si.Gy)] = si;
            _sectors.Add(si);
        }
    }

    private static float InferScale(List<(uint id, int rx, int ry, KeyxEnt e)> raws)
    {
        int min = int.MaxValue;
        foreach (var sel in new Func<(uint, int, int, KeyxEnt), int>[] { t => t.Item2, t => t.Item3 })
        {
            var vals = new SortedSet<int>();
            foreach (var r in raws) vals.Add(sel(r));
            int? prev = null;
            foreach (var v in vals) { if (prev is int p && v > p) min = Math.Min(min, v - p); prev = v; }
        }
        return min == int.MaxValue || min == 0 ? 1f : 64f / min;
    }

    public bool TryGetSectorByGrid(int gx, int gy, out SectorInfo s) => _byGrid.TryGetValue((gx, gy), out s);

    private byte[]? DecompressSectorTiles(KeyxEnt e)
    {
        if (_wldx is null || e.CompSize == 0) return null;
        try
        {
            byte[] blob;
            using (var ms = new MemoryStream(_wldx, e.CompOff, e.CompSize))
            using (var zs = new ZLibStream(ms, CompressionMode.Decompress))
            using (var outp = new MemoryStream())
            {
                zs.CopyTo(outp);
                blob = outp.GetBuffer();
                if (e.TilesRel + e.TilesSize > outp.Length) return null;
            }
            var tiles = new byte[e.TilesSize];
            Array.Copy(blob, e.TilesRel, tiles, 0, e.TilesSize);
            return tiles;
        }
        catch { return null; }
    }

    /// <summary>Decode a sector's ground + tints + floor overlays (heavy; cache the result).</summary>
    public SectorDecode? DecodeSector(SectorInfo s)
    {
        if (!_entById.TryGetValue(s.Sid, out var e)) return null;
        byte[]? tb = DecompressSectorTiles(e);
        if (tb is null || tb.Length < 4096 * TileRecSize) return null;

        var sd = new SectorDecode();
        var floors = new List<FloorOverlay>();
        var liquids = new List<LiquidSurface>();
        var seen = new HashSet<uint>();
        for (int idx = 0; idx < 4096; idx++)
        {
            int o = idx * TileRecSize;
            sd.Ground[idx] = BitConverter.ToUInt32(tb, o);
            sd.Tints[idx * 4 + 0] = tb[o + 0x14];
            sd.Tints[idx * 4 + 1] = tb[o + 0x15];
            sd.Tints[idx * 4 + 2] = tb[o + 0x16];
            sd.Tints[idx * 4 + 3] = tb[o + 0x17];

            uint head = BitConverter.ToUInt32(tb, o + FloorHeadOff);
            if (head != 0 && _floorPak is not null)
                WalkFloorChain(head, idx, floors, seen);

            int surf = tb[o + SurfaceTypeOff] & 0xF0;
            if (surf == 0x90 || surf == 0xA0)
            {
                int styleId = surf == 0x90 ? e.Style90 : e.StyleA0;
                int sheet = LiquidSheet(styleId, out int mult);
                if (sheet >= 0)
                    liquids.Add(new LiquidSurface(idx, sheet,
                        LAlpha(tb[o + LiquidAlphaOff + 0], mult), LAlpha(tb[o + LiquidAlphaOff + 1], mult),
                        LAlpha(tb[o + LiquidAlphaOff + 2], mult), LAlpha(tb[o + LiquidAlphaOff + 3], mult)));
            }
        }
        sd.Floors = floors.ToArray();
        sd.Liquids = liquids.ToArray();
        return sd;
    }

    private static byte LAlpha(byte raw, int mult) => (byte)Math.Clamp((sbyte)raw * mult, 0, 255);

    // styleId → liquid texture name + main-alpha multiplier (from SacredEngineRemake).
    private readonly Dictionary<int, (int sheet, int mult)> _liquidCache = new();
    private int LiquidSheet(int styleId, out int mult)
    {
        if (_liquidCache.TryGetValue(styleId, out var c)) { mult = c.mult; return c.sheet; }
        (string fam, string kind, int m) = styleId switch
        {
            0 or 1 => ("B", "WATER", -12),
            2 => ("C", "WATER", -12),
            3 => ("D", "WATER", -12),
            4 => ("A", "LAVA", -255),
            5 => ("B", "LAVA", -255),
            6 => ("C", "LAVA", -255),
            7 => ("A", "SCHWEFEL", -255),
            8 => ("D", "LAVA", -255),
            9 => ("E", "WATER", -255),
            10 => ("F", "WATER", -24),
            11 => ("G", "WATER", -12),
            12 => ("E", "LAVA", -255),
            13 => ("B", "WATER", -12),
            _ => ("C", "WATER", -12),
        };
        string name = $"{fam}_{kind}02.TGA".ToLowerInvariant();
        int sheet = -1;
        if (!_texByName.TryGetValue(name, out sheet))
        {
            int dot = name.LastIndexOf('.');
            if (dot <= 0 || !_texByName.TryGetValue(name[..dot], out sheet)) sheet = -1;
        }
        _liquidCache[styleId] = (sheet, m);
        mult = m;
        return sheet;
    }

    private void WalkFloorChain(uint head, int localIdx, List<FloorOverlay> outList, HashSet<uint> seen)
    {
        seen.Clear();
        uint fid = head;
        int depth = 0;
        var pak = _floorPak!;
        while (fid != 0 && depth < FloorChainMax)
        {
            if (!seen.Add(fid) || fid >= (uint)pak.Count) break;
            var de = pak.Descriptors[(int)fid];
            if (de.Offset == 0 || de.Offset + 0x10 > (uint)pak.Data.Length) break;
            int off = (int)de.Offset;
            uint refv = pak.U32(off + FloorRecRefOff);
            uint next = pak.U32(off + FloorRecNextOff);
            long primary = refv & PrimaryMask;
            long secondary = (refv >> SecondaryShift) & SecondaryMask;
            if (primary != 0) outList.Add(new FloorOverlay(localIdx, primary, secondary, depth));
            fid = next;
            depth++;
        }
    }
}
