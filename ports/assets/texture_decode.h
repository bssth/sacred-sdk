// texture_decode.h — Sacred PAK texture entry decoder (typeId 6 RGBA8, typeId 4 zlib->ARGB4444).
//
// SOURCE: sdk/.claude/knowledge/refs_resacred.md
//   - "Texture entry (PakTextureHeader 80B, packed)" (line 58):
//       char filename[32]; u16 width,height; u8 typeId; u32 compressedSize; u8; u32 offset; u8[34].
//       typeId==6 -> raw RGBA8 (w*h*4 bytes after header).
//       typeId==4 -> zlib-compressed ARGB4444 (decompress to w*h*2 bytes). Pixels start at +80.
//   - Port candidate #3 ("texture decoder"). GL upload note (rs_resources.cpp:207-212):
//       GL_UNSIGNED_SHORT_4_4_4_4_REV + GL_BGRA when uploading the 16-bit ARGB4444 form.
//   - "zlib inflate (stock zlib, 16 KB chunks)" (line 59): plain zlib streams.
//
// The PakTextureHeader is 80 bytes; in the texture.pak it is the per-entry header that PRECEDES
// the pixel bytes (Resacred's PakHeader/descs locate the entry; this header sits at the entry's
// payload start). We model the header + a decode that yields a host-friendly 32-bit RGBA8 buffer
// regardless of source typeId, plus a helper that documents the exact GL_4_4_4_4_REV/BGRA channel
// order so callers that upload the raw 16-bit form get the swizzle right.
//
// zlib: declared against the (future) herosave zlib_codec interface — see ZlibInflateFn below.
// We do NOT depend on zlib directly here; the caller injects an inflate function.
//
// TODO(port): wire when the SDK decodes Sacred textures natively. FUTURE-USE only — not added to
//   SacredSDK.vcxproj, not #included from any existing TU.
// TODO(port): share the herosave zlib_codec — replace ZlibInflateFn with that module's decompress
//   entry point once it lands (the herosave port already needs stock zlib inflate).
// TODO(verify): no absolute engine VAs are referenced here (all offsets are in-file), so no
//   byte-sig vs Sacred_decrypted.exe is needed.

#ifndef SACRED_PORTS_ASSETS_TEXTURE_DECODE_H
#define SACRED_PORTS_ASSETS_TEXTURE_DECODE_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace sacred {
namespace assets {

// --- PakTextureHeader (80 bytes, packed) ----------------------------------------------------
#pragma pack(push, 1)
struct PakTextureHeader {
    char     filename[32];
    uint16_t width;
    uint16_t height;
    uint8_t  typeId;          // 6 = raw RGBA8, 4 = zlib-compressed ARGB4444
    uint32_t compressedSize;  // size of the compressed payload (typeId 4)
    uint8_t  pad1;
    uint32_t offset;          // entry offset field (engine bookkeeping)
    uint8_t  pad34[34];
};
#pragma pack(pop)
static_assert(sizeof(PakTextureHeader) == 80, "PakTextureHeader must be 80 bytes");

constexpr std::size_t kPakTextureHeaderSize = 80;
constexpr uint8_t kTexTypeRGBA8     = 6; // raw RGBA8, w*h*4 bytes follow header
constexpr uint8_t kTexTypeARGB4444  = 4; // zlib -> ARGB4444 (16-bit), w*h*2 bytes

// --- zlib codec hook (shared with the future herosave zlib_codec) ---------------------------
// Inflate `inSize` bytes at `in` into the caller-provided `out` buffer of `outMax` bytes.
// On success returns true and sets *outSize to the number of bytes written. This mirrors
// Resacred's zlib_decompress(in, inSize, out, outMax, *outSize) (rs_file.cpp:345-411).
// TODO(port): point this at the herosave zlib_codec's decompress fn when it exists.
using ZlibInflateFn = bool(*)(const uint8_t* in, std::size_t inSize,
                              uint8_t* out, std::size_t outMax, std::size_t* outSize);

// --- Decoded image (always normalized to 8-bit RGBA, row-major, top-left origin) ------------
struct DecodedImage {
    uint16_t width  = 0;
    uint16_t height = 0;
    std::vector<uint8_t> rgba; // width*height*4 bytes, R,G,B,A order
    bool valid() const { return width != 0 && height != 0 && rgba.size() == size_t(width)*height*4; }
};

// Parse the 80-byte header that begins at `entry`. `entrySize` is the bytes available from
// `entry` to the end of the buffer. Returns false on malformed input.
bool ReadPakTextureHeader(const uint8_t* entry, std::size_t entrySize, PakTextureHeader* out);

// Decode a texture entry into 8-bit RGBA8.
//   typeId 6: pixel bytes are raw RGBA8 starting at entry+80 (w*h*4). Copied as-is.
//   typeId 4: pixel bytes are a zlib stream starting at entry+80 (compressedSize bytes) that
//             inflates to w*h*2 bytes of ARGB4444; we expand each 16-bit texel to RGBA8.
// `inflate` may be null when only typeId 6 entries are expected; a typeId 4 entry then fails.
// Returns false on any inconsistency (bad header, missing inflate, decompressed size mismatch).
bool DecodePakTexture(const uint8_t* entry, std::size_t entrySize,
                      ZlibInflateFn inflate, DecodedImage* out);

// --- ARGB4444 expansion ---------------------------------------------------------------------
// The 16-bit texels are ARGB4444 as the engine uploads them with GL_UNSIGNED_SHORT_4_4_4_4_REV
// + GL_BGRA. With _4_4_4_4_REV the nibbles in the little-endian u16 map, from the GL_BGRA channel
// order, to: bits[0:4]=A, [4:8]=R, [8:12]=G, [12:16]=B  (i.e. component order A,R,G,B low->high).
// We expand each nibble to 8 bits via n*17 (0x0->0x00 .. 0xF->0xFF). Writes R,G,B,A bytes.
inline void ExpandArgb4444Texel(uint16_t t, uint8_t* rgbaOut) {
    const uint8_t a = static_cast<uint8_t>((t >>  0) & 0xF);
    const uint8_t r = static_cast<uint8_t>((t >>  4) & 0xF);
    const uint8_t g = static_cast<uint8_t>((t >>  8) & 0xF);
    const uint8_t b = static_cast<uint8_t>((t >> 12) & 0xF);
    rgbaOut[0] = static_cast<uint8_t>(r * 17);
    rgbaOut[1] = static_cast<uint8_t>(g * 17);
    rgbaOut[2] = static_cast<uint8_t>(b * 17);
    rgbaOut[3] = static_cast<uint8_t>(a * 17);
}

// GL upload descriptor for callers that want to push the RAW 16-bit ARGB4444 form straight to
// the GPU (no CPU expansion), matching the engine. format/type are the GL enum *values*; we name
// them as constants so this header needs no GL include.
//   GL_BGRA                          = 0x80E1
//   GL_UNSIGNED_SHORT_4_4_4_4_REV    = 0x8365
struct GlArgb4444Upload {
    static constexpr uint32_t kGL_BGRA = 0x80E1;
    static constexpr uint32_t kGL_UNSIGNED_SHORT_4_4_4_4_REV = 0x8365;
    // internalFormat is conventionally GL_RGBA (0x1908) / GL_RGBA4 (0x8056) — caller's choice.
};

} // namespace assets
} // namespace sacred

#endif // SACRED_PORTS_ASSETS_TEXTURE_DECODE_H
