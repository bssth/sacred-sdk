// tincat_crc32.h — Sacred TINCAT2 CRC32 (the checksum on every TCP lobby/game packet).
//
// SOURCE: sdk/.claude/knowledge/refs_magician_net.md §5 ("CRC32 (TINCAT2.DLL) — exact
//         reproduction") + §2.1. Ripped by SonicMouse/Andrew Heinlein from TINCAT2.DLL
//         v2.0.24.0 (256-entry table @ file offset 0x0004986C, routine @ 0x0000DD30).
//         Original C++ lives in refs: SacredGameTools-main/SacredFilter/SacredCRC32.h.
//
// ALGORITHM: standard CRC-32 / IEEE / zlib reflected polynomial 0xEDB88320, table-driven,
//         init = 0 (NOT 0xFFFFFFFF), NO final XOR, no extra reflection beyond the standard
//         reflected table. Confirmed bit-identical to the canonical zlib CRC table, so the
//         table is generated here from the polynomial rather than hard-coding 256 constants.
//
// This is the PREREQUISITE for forging/validating any Sacred TCP packet: the sender writes
//         CRC32(body) into header[24] (see lobby_frame.h). Pure, dependency-free.
//
// TODO(port): wire when the SDK gains a live lobby observer / man-in-the-middle codec
//             (lobby_frame.cpp uses this; nothing wires it yet).
// NOTE: no absolute engine VAs are dereferenced here (the table is regenerated), so there is
//       no byte-signature to verify against Sacred_decrypted.exe for this file.

#ifndef SACRED_SDK_PORTS_NET_TINCAT_CRC32_H
#define SACRED_SDK_PORTS_NET_TINCAT_CRC32_H

#include <cstddef>
#include <cstdint>

namespace sacred {
namespace net {

// Build the 256-entry reflected CRC32 table (zlib polynomial 0xEDB88320) at first use.
// Identical to the table TINCAT2.DLL stores at file offset 0x0004986C.
inline const uint32_t* tincat_crc32_table() {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        built = true;
    }
    return table;
}

// Sacred's TCP body checksum. Equivalent to the asm in SacredCRC32.h / the §5 reference C:
//   crc = 0; for each byte: crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF]; return crc; // no XOR
inline uint32_t tincat_crc32(const uint8_t* buf, size_t len) {
    const uint32_t* table = tincat_crc32_table();
    uint32_t crc = 0;                       // init 0 (NOT 0xFFFFFFFF)
    for (size_t i = 0; i < len; ++i)
        crc = (crc >> 8) ^ table[(crc ^ buf[i]) & 0xFFu];
    return crc;                             // no final XOR
}

} // namespace net
} // namespace sacred

#endif // SACRED_SDK_PORTS_NET_TINCAT_CRC32_H
