using System.IO;
using System.IO.Compression;

namespace SacredStudio.Game;

/// <summary>
/// Decodes a texture.pak payload into 32-bit BGRA pixels (the order GL.TexImage2D
/// gets fed as PixelFormat.Bgra). Texture types, ported 1:1 from the community
/// SacredEngineRemake's TexturePakDecoder:
///   0 = raw ARGB4444         3 = RLE-compressed ARGB4444
///   4 = zlib-compressed ARGB4444   6 = raw BGRA8888
/// </summary>
public static class TextureDecode
{
    /// <summary>Decode to BGRA8888 (B,G,R,A per pixel). Returns null for unknown types.</summary>
    public static byte[]? ToBgra(byte type, ReadOnlySpan<byte> payload, int w, int h) => type switch
    {
        0 => Argb4444ToBgra(payload, w, h),
        3 => Argb4444ToBgra(DecompressRle4444(payload, w, h), w, h),
        4 => Argb4444ToBgra(Inflate(payload), w, h),
        6 => CopyBgra(payload, w, h),
        _ => null,
    };

    private static byte[] CopyBgra(ReadOnlySpan<byte> src, int w, int h)
    {
        var px = new byte[w * h * 4];
        int n = Math.Min(src.Length, px.Length);
        src[..n].CopyTo(px);
        return px;            // already B,G,R,A
    }

    private static byte[] Argb4444ToBgra(ReadOnlySpan<byte> src, int w, int h)
    {
        var px = new byte[w * h * 4];
        int n = Math.Min(src.Length / 2, w * h);
        for (int i = 0; i < n; i++)
        {
            int v = src[i * 2] | (src[i * 2 + 1] << 8);
            int di = i * 4;
            px[di + 0] = (byte)((v & 0xF) * 17);          // B
            px[di + 1] = (byte)(((v >> 4) & 0xF) * 17);   // G
            px[di + 2] = (byte)(((v >> 8) & 0xF) * 17);   // R
            px[di + 3] = (byte)(((v >> 12) & 0xF) * 17);  // A
        }
        return px;
    }

    private static byte[] DecompressRle4444(ReadOnlySpan<byte> src, int w, int h)
    {
        var outp = new byte[w * h * 2];
        int s = 0, d = 0, written = 0, max = outp.Length;
        while (d + 1 < outp.Length && s < src.Length)
        {
            byte control = src[s++];
            int length = control & 0x7F;
            if (length == 0x7F)
            {
                if (s + 1 >= src.Length) break;
                length = src[s] | (src[s + 1] << 8);
                s += 2;
            }
            written += length * 2;
            if (length == 0 || written > max) break;
            if ((control & 0x80) != 0)
            {
                if (s + 1 >= src.Length) break;
                byte lo = src[s++], hi = src[s++];
                for (int i = 0; i < length && d + 1 < outp.Length; i++) { outp[d++] = lo; outp[d++] = hi; }
            }
            else
            {
                int len = Math.Min(length * 2, Math.Min(src.Length - s, outp.Length - d));
                src.Slice(s, len).CopyTo(outp.AsSpan(d, len));
                s += len; d += len;
            }
        }
        return outp;
    }

    private static byte[] Inflate(ReadOnlySpan<byte> compressed)
    {
        using var ms = new MemoryStream(compressed.ToArray(), writable: false);
        using var zs = new ZLibStream(ms, CompressionMode.Decompress);
        using var outp = new MemoryStream();
        zs.CopyTo(outp);
        return outp.ToArray();
    }
}
