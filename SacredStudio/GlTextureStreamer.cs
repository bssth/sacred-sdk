using System.Collections.Concurrent;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using OpenTK.Graphics.OpenGL4;
using PixelFormat = OpenTK.Graphics.OpenGL4.PixelFormat;

namespace SacredStudio;

/// <summary>
/// Streams textures on demand with a bounded VRAM budget. The pixels come from a
/// caller-supplied <c>decode</c> delegate (native game-file decode, or a PNG) which
/// runs on a BACKGROUND thread, so the render thread never blocks on disk/decode.
/// Decoded BGRA pixels are queued and uploaded a few per frame on the render thread
/// (GL must stay on its context thread). An LRU cap evicts textures not touched in
/// the current frame once the cap is exceeded.
///
/// Per frame:  BeginFrame();  tex = Get(key);  …draw tex!=0…;  EndFrame();
/// </summary>
public sealed class GlTextureStreamer
{
    private readonly int _cap;
    private readonly TextureMinFilter _min;
    private readonly TextureMagFilter _mag;
    private readonly int _uploadsPerFrame;
    private readonly Func<int, (byte[]? px, int w, int h)> _decode;

    private readonly Dictionary<int, int> _tex = new();
    private readonly Dictionary<int, long> _used = new();
    private readonly ConcurrentDictionary<int, byte> _pending = new();
    private readonly BlockingCollection<int> _requests = new();
    private readonly ConcurrentQueue<Decoded> _decoded = new();
    private readonly Thread _worker;
    private long _frame;
    private volatile bool _disposed;

    private readonly record struct Decoded(int Key, byte[]? Px, int W, int H);

    public GlTextureStreamer(int cap, TextureMinFilter min, Func<int, (byte[]?, int, int)> decode, int uploadsPerFrame = 24)
    {
        _cap = cap; _min = min; _decode = decode;
        _mag = min == TextureMinFilter.Nearest ? TextureMagFilter.Nearest : TextureMagFilter.Linear;
        _uploadsPerFrame = uploadsPerFrame;
        _worker = new Thread(WorkerLoop) { IsBackground = true, Name = "tex-decode" };
        _worker.Start();
    }

    public void BeginFrame() => _frame++;

    /// <summary>Texture for key, or 0 if not resident yet (request is queued).</summary>
    public int Get(int key)
    {
        if (_tex.TryGetValue(key, out int t)) { _used[key] = _frame; return t; }
        if (_pending.TryAdd(key, 1))
            _requests.Add(key);
        return 0;
    }

    /// <summary>Upload a few decoded textures and evict beyond the cap. Render thread.</summary>
    public void EndFrame()
    {
        int up = 0;
        while (up < _uploadsPerFrame && _decoded.TryDequeue(out var d))
        {
            _pending.TryRemove(d.Key, out _);
            if (d.Px is null) continue;
            int tex = GL.GenTexture();
            GL.BindTexture(TextureTarget.Texture2D, tex);
            GL.TexImage2D(TextureTarget.Texture2D, 0, PixelInternalFormat.Rgba, d.W, d.H, 0,
                          PixelFormat.Bgra, PixelType.UnsignedByte, d.Px);
            GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMinFilter, (int)_min);
            GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMagFilter, (int)_mag);
            GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureWrapS, (int)TextureWrapMode.ClampToEdge);
            GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureWrapT, (int)TextureWrapMode.ClampToEdge);
            _tex[d.Key] = tex;
            _used[d.Key] = _frame;
            up++;
        }
        Evict();
    }

    private void Evict()
    {
        if (_tex.Count <= _cap) return;
        var victims = new List<KeyValuePair<int, long>>();
        foreach (var kv in _used)
            if (kv.Value != _frame) victims.Add(kv);
        victims.Sort((a, b) => a.Value.CompareTo(b.Value));
        int toRemove = _tex.Count - _cap;
        foreach (var v in victims)
        {
            if (toRemove <= 0) break;
            if (_tex.TryGetValue(v.Key, out int tx))
            {
                GL.DeleteTexture(tx);
                _tex.Remove(v.Key);
                _used.Remove(v.Key);
                toRemove--;
            }
        }
    }

    public bool HasPending => !_pending.IsEmpty;
    public int Resident => _tex.Count;

    private void WorkerLoop()
    {
        try
        {
            foreach (var key in _requests.GetConsumingEnumerable())
            {
                if (_disposed) return;
                byte[]? px = null; int w = 0, h = 0;
                try { (px, w, h) = _decode(key); }
                catch { px = null; }
                _decoded.Enqueue(new Decoded(key, px, w, h));
            }
        }
        catch (InvalidOperationException) { /* collection completed */ }
    }

    public void Dispose()
    {
        _disposed = true;
        _requests.CompleteAdding();
    }

    /// <summary>Decode a PNG file to BGRA pixels (used for the legacy per-group sprite images).</summary>
    public static (byte[]? px, int w, int h) DecodePng(string path)
    {
        try
        {
            var bi = new BitmapImage();
            bi.BeginInit();
            bi.UriSource = new Uri(path);
            bi.CacheOption = BitmapCacheOption.OnLoad;
            bi.CreateOptions = BitmapCreateOptions.IgnoreColorProfile;
            bi.EndInit();
            bi.Freeze();
            var conv = new FormatConvertedBitmap(bi, PixelFormats.Bgra32, null, 0);
            conv.Freeze();
            int w = conv.PixelWidth, h = conv.PixelHeight;
            var px = new byte[w * h * 4];
            conv.CopyPixels(px, w * 4, 0);
            return (px, w, h);
        }
        catch { return (null, 0, 0); }
    }
}
