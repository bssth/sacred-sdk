// zlib_codec.h — thin inflate/deflate wrapper for the Sacred Gold hero-save (PAX) container.
//
// SOURCE:
//   - sdk/.claude/knowledge/refs_native_src.md  (#1 PAX format: "Decompression = standard
//     zlib inflate (HeroDump links real zlib1.dll); Recompression = deflate at max level").
//   - sdk/.claude/knowledge/refs_resacred.md     (Resacred rs_file.cpp:345-411 "stock zlib,
//     16 KB chunks, inflateInit/inflate(Z_NO_FLUSH)" — confirms PLAIN zlib streams, not raw/gzip).
//   - Reference impl: E:/refs_extract/HeroDump_Quellcode/SacredHeroFile.cpp (inflateInit/inflate/inflateEnd).
//   - Byte-sig confirmed in our build: literal "inflate" present in sdk/Sacred_decrypted.exe
//     @ file 0x495549 (VA 0x895549) and the 0xBAADC0DE compression sentinel @ VA 0x5B71C4 (5 sites).
//
// These are zlib (RFC-1950) streams with the 2-byte 0x78 header — NOT raw deflate and NOT gzip.
// Use the default windowBits (15) / inflateInit / deflateInit, exactly as HeroDump/Resacred do.
//
// TODO(port): wire when the hero-save reader/writer (sacred.read_save / write_save) is enabled
//             AND zlib is linked into the build. This header DECLARES the codec against zlib; the
//             .cpp only compiles its real body when SACRED_HAVE_ZLIB is defined (see zlib_codec.cpp).
//
// This header is intentionally standalone: it does not include <zlib.h> (so it can be parsed even
// before zlib is vendored) and has no dependency on engine/sdk.h or engine/mem.h.

#ifndef SACRED_PORTS_HEROSAVE_ZLIB_CODEC_H
#define SACRED_PORTS_HEROSAVE_ZLIB_CODEC_H

#include <cstdint>
#include <vector>

namespace sacred {
namespace herosave {

// Result of a codec call. Mirrors the subset of zlib return codes the loaders actually branch on
// (HeroDump treats only Z_OK / Z_STREAM_END as success).
enum class CodecStatus {
    Ok = 0,          // completed (Z_OK or Z_STREAM_END)
    NotLinked,       // built without zlib (SACRED_HAVE_ZLIB undefined)
    InitFailed,      // inflateInit / deflateInit failed
    StreamError,     // malformed input / Z_DATA_ERROR / Z_STREAM_ERROR
    BufferTooSmall,  // caller-supplied expected size was too small for the inflated payload
    OutOfMemory      // allocation / Z_MEM_ERROR
};

// Inflate a zlib stream.
//
// 'src'         : pointer to the zlib stream bytes (begins with the 0x78 zlib header).
// 'srcLen'      : number of compressed bytes available.
// 'expectedSize': the UNCOMPRESSED size, which the PAX allocation table stores per-section
//                 (SECTIONENTRY.UnpackedSize). The container ALWAYS knows this up front, so we
//                 size 'out' to it and inflate in a single pass (matching HeroDump exactly).
// 'out'         : receives the decompressed bytes (resized to the actual inflated length).
//
// Returns Ok on success. On any error 'out' is cleared.
CodecStatus Inflate(const uint8_t* src, size_t srcLen, size_t expectedSize,
                    std::vector<uint8_t>& out);

// Convenience overload taking a vector.
inline CodecStatus Inflate(const std::vector<uint8_t>& src, size_t expectedSize,
                           std::vector<uint8_t>& out) {
    return Inflate(src.data(), src.size(), expectedSize, out);
}

// Deflate a buffer to a zlib stream.
//
// 'level': deflate compression level (default 9 = Z_BEST_COMPRESSION; the tools recompress at max
//          so the rewritten save stays as small as the engine expects). Range 0..9.
// 'out'  : receives the zlib stream (the bytes that go AFTER the 0xBAADC0DE/size/24-null framing
//          header inside a PAX section — this function does NOT emit that framing).
//
// Returns Ok on success. On any error 'out' is cleared.
CodecStatus Deflate(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out,
                    int level = 9);

inline CodecStatus Deflate(const std::vector<uint8_t>& src, std::vector<uint8_t>& out,
                           int level = 9) {
    return Deflate(src.data(), src.size(), out, level);
}

// True if this translation unit was compiled with real zlib (SACRED_HAVE_ZLIB). When false, the
// codec functions return CodecStatus::NotLinked and the caller should refuse to read/write saves.
bool IsZlibLinked();

}  // namespace herosave
}  // namespace sacred

#endif  // SACRED_PORTS_HEROSAVE_ZLIB_CODEC_H
