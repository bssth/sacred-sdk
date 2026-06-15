using System.Collections.Concurrent;
using System.IO;

namespace SacredStudio.Game;

/// <summary>
/// Native (C#) static-sprite pipeline — trees, buildings, rocks. Resolves
/// Static.pak → ITEMS.pak → mixed.pak and composes each sprite from its mixed
/// pieces, exactly like the offline Python exporter used to (now removed from the
/// product). Placements are discovered by walking every sector's tile +0x04 static
/// chain once (on a background thread so startup never blocks).
///
///  Static record 0x40: type@+0x04, flags@+0x08, sectorId@+0x0C(u16),
///                      px@+0x0E(s32), py@+0x12(s32), next@+0x1F(u32)
///  ITEMS record 0x80 : groupRef@+0x10(u32)
///  mixed group       : count@+0x00(u32), pieces@+0x10 (0x40 each):
///                      name@+0x00(cstr0x20), cid@+0x20(u32), right@+0x24(u16),
///                      bottom@+0x26(u16), left@+0x28(s16), top@+0x2A(s16),
///                      uv@+0x30 (4 floats: x0,y0,x1,y1)
/// </summary>
public sealed class SpriteAssets
{
    public readonly record struct Placement(int Group, int Px, int Py);
    public readonly record struct Meta(int W, int H, int Ax, int Ay);
    public const float ShiftX = 47.8f, ShiftY = -0.3f;

    private const int StaticRec = 0x40, ItemsRec = 0x80, MixedPiece = 0x40;
    private const uint StaticExcludeFlags = 0x290;
    private const int ChainMax = 4096;

    private record struct StaticRecord(uint Type, uint Flags, ushort SectorId, int Px, int Py, uint Next);
    private record struct Piece(string Atlas, int Right, int Bottom, int Left, int Top, float X0, float Y0, float X1, float Y1);

    private readonly GameAssets _ga;
    private readonly StaticRecord[] _statics;        // by static_id (Type==0 → empty)
    private readonly uint[] _itemGroup;              // type_id → mixed group ref
    private readonly Dictionary<int, Piece[]> _mixed = new();
    private readonly Dictionary<uint, int> _cidToGroup = new();

    private readonly Dictionary<uint, List<Placement>> _bySector = new();
    private readonly ConcurrentDictionary<int, Meta> _meta = new();

    // decoded texture.pak sheets used by composition (single compose thread → plain dict)
    private readonly Dictionary<string, (byte[]? px, int w, int h)> _sheetCache = new();

    public bool Ready { get; private set; }

    public SpriteAssets(GameAssets ga, string pakDir, string worldDir)
    {
        _ga = ga;
        _statics = LoadStatic(Path.Combine(worldDir, "Static.pak"));
        _itemGroup = LoadItems(FindCaseInsensitive(pakDir, "ITEMS.pak"));
        LoadMixed(FindCaseInsensitive(pakDir, "mixed.pak"));
        // Walk all sectors' static chains in the background; sprites pop in when done.
        new Thread(BuildPlacements) { IsBackground = true, Name = "sprite-walk" }.Start();
    }

    private static string FindCaseInsensitive(string dir, string file)
    {
        string direct = Path.Combine(dir, file);
        if (File.Exists(direct)) return direct;
        foreach (var f in Directory.EnumerateFiles(dir))
            if (string.Equals(Path.GetFileName(f), file, StringComparison.OrdinalIgnoreCase))
                return f;
        return direct;
    }

    private static StaticRecord[] LoadStatic(string path)
    {
        var pak = PakFile.Load(path);
        var recs = new StaticRecord[pak.Count];
        for (int i = 1; i < pak.Count; i++)
        {
            var de = pak.Descriptors[i];
            if (de.Offset == 0 || de.Offset + StaticRec > (uint)pak.Data.Length) continue;
            int o = (int)de.Offset;
            recs[i] = new StaticRecord(
                pak.U32(o + 0x04), pak.U32(o + 0x08), pak.U16(o + 0x0C),
                BitConverter.ToInt32(pak.Data, o + 0x0E), BitConverter.ToInt32(pak.Data, o + 0x12),
                pak.U32(o + 0x1F));
        }
        return recs;
    }

    private static uint[] LoadItems(string path)
    {
        var pak = PakFile.Load(path);
        var refs = new uint[pak.Count];
        for (int i = 1; i < pak.Count; i++)
        {
            var de = pak.Descriptors[i];
            if (de.Offset == 0 || de.Offset + ItemsRec > (uint)pak.Data.Length) continue;
            refs[i] = pak.U32((int)de.Offset + 0x10);
        }
        return refs;
    }

    private void LoadMixed(string path)
    {
        var pak = PakFile.Load(path);
        for (int mid = 0; mid < pak.Count; mid++)
        {
            var de = pak.Descriptors[mid];
            if (de.Offset == 0 || de.Size <= 0x10 || de.Offset + de.Size > (uint)pak.Data.Length) continue;
            int off = (int)de.Offset;
            int pc = (int)Math.Min(pak.U32(off), Math.Max(0, ((long)de.Size - 0x10) / MixedPiece));
            if (pc <= 0) continue;
            var pieces = new List<Piece>(pc);
            int p = off + 0x10;
            for (int k = 0; k < pc && p + MixedPiece <= pak.Data.Length; k++, p += MixedPiece)
            {
                string name = pak.ReadCString(p, 0x20);
                uint cid = pak.U32(p + 0x20);
                pieces.Add(new Piece(name,
                    pak.U16(p + 0x24), pak.U16(p + 0x26),
                    BitConverter.ToInt16(pak.Data, p + 0x28), BitConverter.ToInt16(pak.Data, p + 0x2A),
                    BitConverter.ToSingle(pak.Data, p + 0x30), BitConverter.ToSingle(pak.Data, p + 0x34),
                    BitConverter.ToSingle(pak.Data, p + 0x38), BitConverter.ToSingle(pak.Data, p + 0x3C)));
                _cidToGroup.TryAdd(cid, mid);
            }
            if (pieces.Count > 0) _mixed[mid] = pieces.ToArray();
        }
    }

    private int ResolveGroup(uint typeId)
    {
        if (typeId >= (uint)_itemGroup.Length) return -1;
        uint baseRef = _itemGroup[typeId];
        if (baseRef == 0) return -1;
        if (_mixed.ContainsKey((int)baseRef)) return (int)baseRef;
        return _cidToGroup.TryGetValue(baseRef, out int g) ? g : -1;
    }

    private void BuildPlacements()
    {
        try
        {
            var reached = new HashSet<uint>();
            foreach (var s in _ga.Sectors)
            {
                byte[]? tb = _ga.SectorTilesRaw(s.Sid);
                if (tb is null || tb.Length < 4096 * 0x20) continue;
                for (int idx = 0; idx < 4096; idx++)
                {
                    uint st = BitConverter.ToUInt32(tb, idx * 0x20 + 0x04);
                    int depth = 0;
                    var seen = new HashSet<uint>();
                    while (st != 0 && depth < ChainMax && seen.Add(st))
                    {
                        depth++;
                        if (st >= (uint)_statics.Length) break;
                        var rec = _statics[st];
                        uint next = rec.Next;
                        if (rec.Type != 0 && reached.Add(st) && (rec.Flags & StaticExcludeFlags) == 0)
                        {
                            int g = ResolveGroup(rec.Type);
                            if (g >= 0)
                            {
                                uint owner = rec.SectorId != 0 ? rec.SectorId : s.Sid;
                                if (!_bySector.TryGetValue(owner, out var list)) { list = new List<Placement>(); _bySector[owner] = list; }
                                list.Add(new Placement(g, rec.Px, rec.Py));
                            }
                        }
                        st = next;
                    }
                }
            }
        }
        catch { /* leave whatever we gathered */ }
        Ready = true;
    }

    public List<Placement>? SectorPlacements(uint sid) => _bySector.TryGetValue(sid, out var l) ? l : null;

    /// <summary>Sprite size + anchor from its mixed pieces (no texture decode needed).</summary>
    public bool TryMeta(int group, out Meta m)
    {
        if (_meta.TryGetValue(group, out m)) return m.W > 0;
        m = ComputeMeta(group);
        _meta[group] = m;
        return m.W > 0;
    }

    private Meta ComputeMeta(int group)
    {
        if (!_mixed.TryGetValue(group, out var pieces)) return default;
        int minX = int.MaxValue, minY = int.MaxValue, maxX = int.MinValue, maxY = int.MinValue;
        foreach (var p in pieces)
        {
            int dl = Math.Min(p.Left, p.Right), dt = Math.Min(p.Top, p.Bottom);
            int dr = Math.Max(p.Left, p.Right), db = Math.Max(p.Top, p.Bottom);
            if (dr <= dl || db <= dt) continue;
            minX = Math.Min(minX, dl); minY = Math.Min(minY, dt);
            maxX = Math.Max(maxX, dr); maxY = Math.Max(maxY, db);
        }
        if (maxX <= minX || maxY <= minY) return default;
        return new Meta(maxX - minX, maxY - minY, -minX, -minY);
    }

    /// <summary>Compose a sprite's BGRA pixels (sprite-decode thread). Returns null if empty.</summary>
    public (byte[]? px, int w, int h) Compose(int group)
    {
        var m = ComputeMeta(group);
        if (m.W <= 0 || !_mixed.TryGetValue(group, out var pieces)) return (null, 0, 0);
        int w = m.W, h = m.H, ox = m.Ax, oy = m.Ay;     // ox=-minX, oy=-minY
        var dst = new byte[w * h * 4];
        foreach (var p in pieces)
        {
            var (sheet, sw, sh) = GetSheet(p.Atlas);
            if (sheet is null) continue;
            int sl = Clamp((int)MathF.Round(MathF.Min(p.X0, p.X1) * sw), 0, sw);
            int st = Clamp((int)MathF.Round(MathF.Min(p.Y0, p.Y1) * sh), 0, sh);
            int sr = Clamp((int)MathF.Round(MathF.Max(p.X0, p.X1) * sw), 0, sw);
            int sb = Clamp((int)MathF.Round(MathF.Max(p.Y0, p.Y1) * sh), 0, sh);
            if (sr <= sl || sb <= st) continue;
            int dl = Math.Min(p.Left, p.Right), dt = Math.Min(p.Top, p.Bottom);
            int dr = Math.Max(p.Left, p.Right), db = Math.Max(p.Top, p.Bottom);
            if (dr <= dl || db <= dt) continue;
            int dw = dr - dl, dh = db - dt, cw = sr - sl, ch = sb - st;
            // nearest-sample crop→dest, alpha-over into dst at (dl+ox, dt+oy)
            for (int y = 0; y < dh; y++)
            {
                int dy = dt + oy + y;
                if ((uint)dy >= (uint)h) continue;
                int syy = st + (cw == dw && ch == dh ? y : y * ch / dh);
                for (int x = 0; x < dw; x++)
                {
                    int dx = dl + ox + x;
                    if ((uint)dx >= (uint)w) continue;
                    int sxx = sl + (cw == dw && ch == dh ? x : x * cw / dw);
                    int si = (syy * sw + sxx) * 4;
                    int a = sheet[si + 3];
                    if (a == 0) continue;
                    int di = (dy * w + dx) * 4;
                    if (a == 255 || dst[di + 3] == 0)
                    {
                        dst[di] = sheet[si]; dst[di + 1] = sheet[si + 1];
                        dst[di + 2] = sheet[si + 2]; dst[di + 3] = (byte)a;
                    }
                    else
                    {
                        int inv = 255 - a, da = dst[di + 3];
                        int outA = a + da * inv / 255;
                        if (outA == 0) continue;
                        int df = da * inv / 255;
                        dst[di] = (byte)((sheet[si] * a + dst[di] * df) / outA);
                        dst[di + 1] = (byte)((sheet[si + 1] * a + dst[di + 1] * df) / outA);
                        dst[di + 2] = (byte)((sheet[si + 2] * a + dst[di + 2] * df) / outA);
                        dst[di + 3] = (byte)outA;
                    }
                }
            }
        }
        return (dst, w, h);
    }

    private (byte[]? px, int w, int h) GetSheet(string name)
    {
        string key = name.ToLowerInvariant();
        if (_sheetCache.TryGetValue(key, out var c)) return c;
        var dec = _ga.DecodeSheetByName(name);
        if (_sheetCache.Count > 192) _sheetCache.Clear();    // simple bound (single thread)
        _sheetCache[key] = dec;
        return dec;
    }

    private static int Clamp(int v, int lo, int hi) => v < lo ? lo : (v > hi ? hi : v);
}
