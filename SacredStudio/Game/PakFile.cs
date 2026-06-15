using System.IO;
using System.Text;

namespace SacredStudio.Game;

/// <summary>
/// A Sacred .pak archive read fully into memory. Layout (verified against the
/// community SacredEngineRemake + our own RE): header 0x100, entry count at +4
/// (u32, u16 fallback), then `count` descriptors of {u32 type, u32 offset, u32 size}.
/// Each entry's payload lives at `offset` (often behind a per-entry header).
///
/// Native replacement for the offline Python pak readers — the editor now decodes
/// the game's own files directly, no Python in the pipeline.
/// </summary>
public sealed class PakFile
{
    public const int HeaderSize = 0x100;
    public const int DescriptorSize = 0x0C;

    public readonly record struct Descriptor(uint Type, uint Offset, uint Size);

    public byte[] Data { get; }
    public Descriptor[] Descriptors { get; }
    public int Count => Descriptors.Length;

    private PakFile(byte[] data)
    {
        Data = data;
        Descriptors = ReadDescriptors(data);
    }

    public static PakFile Load(string path) => new(File.ReadAllBytes(path));

    private static Descriptor[] ReadDescriptors(byte[] d)
    {
        if (d.Length < HeaderSize) return Array.Empty<Descriptor>();
        uint c32 = BitConverter.ToUInt32(d, 4);
        ushort c16 = BitConverter.ToUInt16(d, 4);
        long max = (d.Length - HeaderSize) / DescriptorSize;
        int count = c32 <= max ? (int)c32 : (c16 <= max ? c16 : 0);
        var descs = new Descriptor[count];
        for (int i = 0; i < count; i++)
        {
            int o = HeaderSize + i * DescriptorSize;
            descs[i] = new Descriptor(
                BitConverter.ToUInt32(d, o),
                BitConverter.ToUInt32(d, o + 4),
                BitConverter.ToUInt32(d, o + 8));
        }
        return descs;
    }

    /// <summary>Read a NUL-terminated Latin-1 string of at most <paramref name="max"/> bytes.</summary>
    public string ReadCString(int offset, int max = 0x20)
    {
        int end = offset, limit = Math.Min(Data.Length, offset + max);
        while (end < limit && Data[end] != 0) end++;
        return Encoding.Latin1.GetString(Data, offset, end - offset);
    }

    public uint U32(int offset) => BitConverter.ToUInt32(Data, offset);
    public ushort U16(int offset) => BitConverter.ToUInt16(Data, offset);
}
