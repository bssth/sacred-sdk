// texture_decode.cpp — implementation of the Sacred PAK texture decoder.
//
// SOURCE: see texture_decode.h doc block (refs_resacred.md texture-entry row + port candidate #3).
//
// TODO(port): wire when the SDK decodes Sacred textures natively. FUTURE-USE only.

#include "texture_decode.h"

#include <cstring>

namespace sacred {
namespace assets {

namespace {

inline uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

} // namespace

bool ReadPakTextureHeader(const uint8_t* entry, std::size_t entrySize, PakTextureHeader* out) {
    if (!entry || !out || entrySize < kPakTextureHeaderSize) return false;
    std::memcpy(out, entry, kPakTextureHeaderSize); // packed struct == on-disk layout
    return true;
}

bool DecodePakTexture(const uint8_t* entry, std::size_t entrySize,
                      ZlibInflateFn inflate, DecodedImage* out) {
    if (!out) return false;
    out->width = 0; out->height = 0; out->rgba.clear();

    PakTextureHeader h;
    if (!ReadPakTextureHeader(entry, entrySize, &h)) return false;
    if (h.width == 0 || h.height == 0) return false;

    const std::size_t pixelCount = static_cast<std::size_t>(h.width) * h.height;
    const uint8_t* payload = entry + kPakTextureHeaderSize;
    const std::size_t payloadAvail = entrySize - kPakTextureHeaderSize;

    if (h.typeId == kTexTypeRGBA8) {
        // Raw RGBA8: w*h*4 bytes follow the header, copied verbatim.
        const std::size_t need = pixelCount * 4;
        if (payloadAvail < need) return false;
        out->rgba.assign(payload, payload + need);
        out->width = h.width;
        out->height = h.height;
        return out->valid();
    }

    if (h.typeId == kTexTypeARGB4444) {
        // zlib stream of compressedSize bytes -> w*h*2 bytes of ARGB4444, then expand to RGBA8.
        if (!inflate) return false;
        const std::size_t inSize = h.compressedSize;
        if (inSize == 0 || inSize > payloadAvail) return false;

        const std::size_t rawSize = pixelCount * 2; // ARGB4444 = 2 bytes/texel
        std::vector<uint8_t> raw(rawSize);
        std::size_t produced = 0;
        if (!inflate(payload, inSize, raw.data(), rawSize, &produced)) return false;
        if (produced != rawSize) return false; // strict: must match the declared dimensions

        out->rgba.resize(pixelCount * 4);
        for (std::size_t i = 0; i < pixelCount; ++i) {
            const uint16_t texel = ReadU16LE(raw.data() + i * 2);
            ExpandArgb4444Texel(texel, out->rgba.data() + i * 4);
        }
        out->width = h.width;
        out->height = h.height;
        return out->valid();
    }

    // Unknown typeId — refuse rather than guess.
    return false;
}

} // namespace assets
} // namespace sacred
