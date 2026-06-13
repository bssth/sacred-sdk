// lobby_frame.cpp — implementation of the Sacred TCP lobby frame codec.
//
// SOURCE: sdk/.claude/knowledge/refs_magician_net.md §2.1. See lobby_frame.h for the frame map.
// TODO(port): wire when the SDK gains a live lobby observer / man-in-the-middle codec.

#include "lobby_frame.h"
#include "tincat_crc32.h"

#include <cstring>

namespace sacred {
namespace net {

uint32_t lobby_read_u32(const uint8_t* p, size_t off) {
    // Little-endian assemble; matches *(unsigned long*)&p[off] on x86.
    return  static_cast<uint32_t>(p[off + 0])
         | (static_cast<uint32_t>(p[off + 1]) << 8)
         | (static_cast<uint32_t>(p[off + 2]) << 16)
         | (static_cast<uint32_t>(p[off + 3]) << 24);
}

void lobby_write_u32(uint8_t* p, size_t off, uint32_t v) {
    p[off + 0] = static_cast<uint8_t>(v & 0xFF);
    p[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

uint32_t LobbyFrame::format() const   { return lobby_read_u32(header, kLobbyOffFormat); }
uint32_t LobbyFrame::body_len() const { return lobby_read_u32(header, kLobbyOffBodyLen); }
uint32_t LobbyFrame::crc() const      { return lobby_read_u32(header, kLobbyOffCrc); }

bool LobbyFrame::crc_valid() const {
    const uint8_t* p = body.empty() ? nullptr : body.data();
    return tincat_crc32(p, body.size()) == crc();
}

bool lobby_decode(const uint8_t* buf, size_t len, LobbyFrame* out, size_t* consumed) {
    if (!buf || !out || len < kLobbyHeaderSize)
        return false;
    const uint32_t body_len = lobby_read_u32(buf, kLobbyOffBodyLen);
    const size_t total = kLobbyHeaderSize + static_cast<size_t>(body_len);
    if (len < total)
        return false;  // not enough bytes buffered yet — caller should read more

    std::memcpy(out->header, buf, kLobbyHeaderSize);
    out->body.assign(buf + kLobbyHeaderSize, buf + total);
    if (consumed)
        *consumed = total;
    return true;
}

std::vector<uint8_t> lobby_encode(const LobbyFrame& frame) {
    LobbyFrame f = frame;  // local copy so we can fix up header fields
    const uint32_t body_len = static_cast<uint32_t>(f.body.size());
    const uint8_t* bp = f.body.empty() ? nullptr : f.body.data();

    lobby_write_u32(f.header, kLobbyOffBodyLen, body_len);
    lobby_write_u32(f.header, kLobbyOffCrc, tincat_crc32(bp, f.body.size()));

    std::vector<uint8_t> wire;
    wire.reserve(kLobbyHeaderSize + f.body.size());
    wire.insert(wire.end(), f.header, f.header + kLobbyHeaderSize);
    wire.insert(wire.end(), f.body.begin(), f.body.end());
    return wire;
}

LobbyFrame lobby_make(const uint8_t* header_template, uint32_t format,
                      const uint8_t* body, size_t body_len) {
    LobbyFrame f;
    if (header_template)
        std::memcpy(f.header, header_template, kLobbyHeaderSize);
    else
        std::memset(f.header, 0, kLobbyHeaderSize);

    if (body && body_len)
        f.body.assign(body, body + body_len);

    lobby_write_u32(f.header, kLobbyOffFormat, format);
    lobby_write_u32(f.header, kLobbyOffBodyLen, static_cast<uint32_t>(f.body.size()));
    lobby_write_u32(f.header, kLobbyOffCrc,
                    tincat_crc32(f.body.empty() ? nullptr : f.body.data(), f.body.size()));
    return f;
}

} // namespace net
} // namespace sacred
