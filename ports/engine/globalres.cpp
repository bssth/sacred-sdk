// globalres.cpp — implementation of the byte-exact global.res emitter.
// See globalres.h for the source/spec references (globalres_format.md §1-§4).
//
// TODO(port): wire when replacing text.lua's span-copy model. UNWIRED.

#include "globalres.h"
#include "sacred_hash.h"

#include <algorithm>
#include <cstring>

namespace sacred {
namespace engine {

namespace {

inline uint32_t rd_u32(const std::vector<uint8_t>& b, size_t off) {
    // little-endian; caller guarantees off+4 <= size().
    return  static_cast<uint32_t>(b[off])
         | (static_cast<uint32_t>(b[off + 1]) << 8)
         | (static_cast<uint32_t>(b[off + 2]) << 16)
         | (static_cast<uint32_t>(b[off + 3]) << 24);
}

inline void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

} // namespace

bool ParseGlobalRes(const std::vector<uint8_t>& file,
                    std::vector<GlobalResSlot>& out_slots) {
    out_slots.clear();
    if (file.size() < 16) return false;

    // blob_start = off[0] = u32 @ file offset 8 (globalres_format.md §1).
    const uint32_t blob_start = rd_u32(file, 8);
    if (blob_start == 0 || (blob_start % 16) != 0) return false;
    if (blob_start > file.size()) return false;

    const uint32_t n = blob_start / 16;
    if (n == 0) return false;

    // d0[0] must equal N (the binary-search hi bound). Sanity check only.
    const uint32_t d0_0 = rd_u32(file, 0);
    if (d0_0 != n) return false;

    // extra-length dword at blob_start = 2*units(N-1).
    if (blob_start + 4 > file.size()) return false;
    const uint32_t last_len_dw = rd_u32(file, blob_start);

    out_slots.reserve(n);
    for (uint32_t k = 0; k < n; ++k) {
        const size_t rec = static_cast<size_t>(k) * 16;
        const uint32_t ident = rd_u32(file, rec + 4);
        const uint32_t off   = rd_u32(file, rec + 8);
        const uint32_t pad   = rd_u32(file, rec + 12);

        // units(k): for k<N-1 it lives in the NEXT record's d0 (>>1); for the
        // last slot it is the extra-length dword (>>1).
        uint32_t two_units;
        if (k < n - 1) two_units = rd_u32(file, rec + 16);  // d0[k+1]
        else           two_units = last_len_dw;
        const uint32_t units = two_units >> 1;

        const size_t payload = static_cast<size_t>(off) + 4;
        if (payload + static_cast<size_t>(units) * 2 > file.size()) return false;

        GlobalResSlot s;
        s.ident = ident;
        s.pad   = pad;
        s.text_units.resize(units);
        for (uint32_t u = 0; u < units; ++u) {
            const size_t bo = payload + static_cast<size_t>(u) * 2;
            s.text_units[u] = static_cast<uint16_t>(
                file[bo] | (static_cast<uint16_t>(file[bo + 1]) << 8));
        }
        out_slots.push_back(std::move(s));
    }
    return true;
}

GlobalResSlot MakeSlot(const std::string& name,
                       const std::vector<uint16_t>& text_units,
                       uint32_t pad) {
    GlobalResSlot s;
    s.ident = sacred_hash(name.c_str(), name.size()); // already &0x7fffffff
    s.text_units = text_units;
    s.pad = pad;
    return s;
}

GlobalResSlot MakeSlotAscii(const std::string& name,
                            const std::string& ascii_text,
                            uint32_t pad) {
    std::vector<uint16_t> units;
    units.reserve(ascii_text.size());
    for (unsigned char c : ascii_text) units.push_back(static_cast<uint16_t>(c));
    return MakeSlot(name, units, pad);
}

std::vector<uint8_t> EmitGlobalRes(const std::vector<GlobalResSlot>& slots) {
    // 1) Sort a copy ascending by ident; dedupe equal idents, last write wins.
    //    (Unsigned == signed here: every ident has bit31 clear after &0x7fff'ffff.)
    std::vector<GlobalResSlot> list = slots;
    std::stable_sort(list.begin(), list.end(),
                     [](const GlobalResSlot& a, const GlobalResSlot& b) {
                         return a.ident < b.ident;
                     });
    if (!list.empty()) {
        std::vector<GlobalResSlot> dedup;
        dedup.reserve(list.size());
        for (auto& s : list) {
            if (!dedup.empty() && dedup.back().ident == s.ident) {
                dedup.back() = s; // last write wins
            } else {
                dedup.push_back(s);
            }
        }
        list.swap(dedup);
    }

    const uint32_t m = static_cast<uint32_t>(list.size());

    // Precompute units[k] and the running absolute offsets.
    std::vector<uint32_t> units(m);
    for (uint32_t k = 0; k < m; ++k)
        units[k] = static_cast<uint32_t>(list[k].text_units.size());

    const uint32_t blob_start = m * 16;

    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(blob_start) + 4 + 64);

    // 2) Emit index records (16 bytes each): d0, ident, off, pad.
    uint32_t off = blob_start; // off[0] = blob_start
    for (uint32_t k = 0; k < m; ++k) {
        uint32_t d0;
        if (k == 0) d0 = m;                  // d0[0] = M
        else        d0 = 2 * units[k - 1];   // d0[k] = 2*units(k-1)

        put_u32(out, d0);
        put_u32(out, list[k].ident);
        put_u32(out, off);
        put_u32(out, list[k].pad);

        off += 2 * units[k]; // off[k+1] = off[k] + 2*units(k)
    }

    // 3) Emit blob: extra-length dword (= 2*units(M-1)), then payloads in order.
    const uint32_t last_two_units = (m > 0) ? 2 * units[m - 1] : 0;
    put_u32(out, last_two_units);

    for (uint32_t k = 0; k < m; ++k) {
        for (uint16_t cu : list[k].text_units) {
            out.push_back(static_cast<uint8_t>(cu & 0xff));
            out.push_back(static_cast<uint8_t>((cu >> 8) & 0xff));
        }
    }

    // No tail, no padding, no terminators (globalres_format.md §3e).
    return out;
}

} // namespace engine
} // namespace sacred
