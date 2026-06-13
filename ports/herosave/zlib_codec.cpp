// zlib_codec.cpp — PAX hero-save inflate, backed by the GAME'S OWN zlib.
//
// 2026-06-13: switched off the external zlib1.dll (zdll.lib) onto the engine's
// statically-linked zlib `uncompress`, resolved + byte-sig-verified at runtime
// in sdk/engine_resolve.cpp. This DROPS the zlib1.dll runtime dependency: the
// SDK no longer needs zlib1.dll next to Sacred.exe.
//
// WHY THIS IS NOW POSSIBLE (see .claude/knowledge/re/our_build_vas.md):
//   Sacred_decrypted.exe is FULLY decrypted (the old "encrypted on disk" claim
//   was wrong) and maps 1:1 to the live image (VA = file_off + 0x400000, base
//   0x400000, no ASLR). We statically located the engine's
//     int __cdecl uncompress(Bytef* dst, uLongf* dstLen, const Bytef* src, uLong srcLen)
//   at VA 0x0066e160 (it does inflateInit_("1.2.1")/inflate(Z_FINISH)/inflateEnd
//   on a stack z_stream — stock zlib 1.2.1). engine_resolve verifies its prologue
//   bytes before the first call, so a layout shift fails safe (no crash).
//
// The PAX allocation table always carries the uncompressed size, and the section
// bytes handed to Inflate() are a plain zlib stream (0x78 header) — the
// 0xBAADC0DE framing is already stripped upstream — so a single-shot uncompress
// sized to expectedSize is exactly right (same as HeroDump/Resacred).
//
// Deflate (save WRITING) is not wired to the engine yet; it returns NotLinked.
// Hero-save is currently read-only (sacred.read_save). Wire the engine's deflate
// the same way if/when write_save is implemented.

#include "zlib_codec.h"

// Engine zlib bridge (implemented in sdk/engine_resolve.cpp). Forward-declared
// so this port stays free of <zlib.h> and the heavier sdk.h include.
namespace sdk { namespace engine_resolve {
    int  call_uncompress(unsigned char* dst, unsigned long* dstLen,
                         const unsigned char* src, unsigned long srcLen);
    bool uncompress_available();
}}

namespace sacred {
namespace herosave {

// zlib return codes we branch on (avoids pulling in <zlib.h>).
enum { kZ_OK = 0, kZ_BUF_ERROR = -5, kZ_MEM_ERROR = -4, kZ_UNAVAILABLE = -1000 };

bool IsZlibLinked() {
    // "Linked" now means: the engine's own uncompress is resolved & verified.
    return sdk::engine_resolve::uncompress_available();
}

CodecStatus Inflate(const uint8_t* src, size_t srcLen, size_t expectedSize,
                    std::vector<uint8_t>& out) {
    out.clear();
    if (src == nullptr || srcLen == 0) return CodecStatus::StreamError;
    // The PAX table always supplies the unpacked size; without it we can't size
    // the single-shot output buffer the engine's uncompress needs.
    if (expectedSize == 0) return CodecStatus::BufferTooSmall;

    std::vector<uint8_t> buf;
    buf.resize(expectedSize);

    unsigned long dstLen = static_cast<unsigned long>(expectedSize);
    int rc = sdk::engine_resolve::call_uncompress(
        buf.data(), &dstLen,
        const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(src)),
        static_cast<unsigned long>(srcLen));

    if (rc == kZ_UNAVAILABLE) return CodecStatus::NotLinked;     // engine zlib not ready
    if (rc == kZ_MEM_ERROR)   return CodecStatus::OutOfMemory;
    if (rc == kZ_BUF_ERROR)   return CodecStatus::BufferTooSmall; // expectedSize understated
    if (rc != kZ_OK)          return CodecStatus::StreamError;    // Z_DATA_ERROR / etc.

    if (dstLen < expectedSize) {
        // Fewer bytes than the table promised — the section would read garbage.
        return CodecStatus::BufferTooSmall;
    }
    buf.resize(dstLen);
    out.swap(buf);
    return CodecStatus::Ok;
}

CodecStatus Deflate(const uint8_t*, size_t, std::vector<uint8_t>& out, int) {
    // Save writing is not wired to the engine's deflate yet (read-only path).
    out.clear();
    return CodecStatus::NotLinked;
}

}  // namespace herosave
}  // namespace sacred
