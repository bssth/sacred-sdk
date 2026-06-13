// udp_announce.cpp — implementation of the Sacred UDP server-announce codec.
//
// SOURCE: sdk/.claude/knowledge/refs_magician_net.md §2.4. See udp_announce.h for the byte map.
// TODO(port): wire when the SDK advertises/enumerates games; supply a real ZlibCodec
//             (TODO(port): share herosave zlib_codec).

#include "udp_announce.h"

#include <cstring>

namespace sacred {
namespace net {

bool udp_parse_inflated(const uint8_t* body, size_t body_len, ServerInfo* out) {
    if (!body || !out || body_len < kUdpBodyMin)
        return false;

    // Port: LE u16 at +2 == body[3]*256 + body[2].
    out->port = static_cast<uint16_t>(body[kUdpOffPort] | (body[kUdpOffPort + 1] << 8));

    // IP stored byte-reversed: human ip = {body[7],body[6],body[5],body[4]}.
    out->ip[0] = body[kUdpOffIp + 3];
    out->ip[1] = body[kUdpOffIp + 2];
    out->ip[2] = body[kUdpOffIp + 1];
    out->ip[3] = body[kUdpOffIp + 0];

    out->flags       = body[kUdpOffFlags];
    out->difficulty  = body[kUdpOffDiff];
    out->cur_players = body[kUdpOffCur];
    out->max_players = body[kUdpOffMax];

    // Name: UTF-16LE, up to 24 chars (48 bytes), null-trimmed. Decode the BMP subset to a
    // narrow string (Sacred game names are practically ASCII/Latin-1). Code points > 0xFF are
    // emitted as '?' to avoid a UTF-8 dependency here — refine if full Unicode display is wanted.
    out->name.clear();
    for (size_t i = 0; i < kUdpNameBytes; i += 2) {
        const uint16_t ch = static_cast<uint16_t>(body[kUdpOffName + i] |
                                                  (body[kUdpOffName + i + 1] << 8));
        if (ch == 0) break;                 // null terminator
        out->name.push_back(ch <= 0xFF ? static_cast<char>(ch) : '?');
    }
    return true;
}

bool udp_announce_decode(const uint8_t* packet, size_t packet_len, ZlibCodec& zlib,
                         ServerInfo* out, uint8_t out_header4[4]) {
    if (!packet || !out || packet_len < kUdpHeaderSize)
        return false;
    if (out_header4)
        std::memcpy(out_header4, packet, kUdpHeaderSize);

    std::vector<uint8_t> inflated;
    if (!zlib.inflate(packet + kUdpHeaderSize, packet_len - kUdpHeaderSize, &inflated))
        return false;
    return udp_parse_inflated(inflated.data(), inflated.size(), out);
}

std::vector<uint8_t> udp_build_inflated(const ServerInfo& info,
                                        const uint8_t* tmpl, size_t tmpl_len) {
    std::vector<uint8_t> body(kUdpBodyMin, 0);
    if (tmpl && tmpl_len) {
        const size_t n = tmpl_len < body.size() ? tmpl_len : body.size();
        std::memcpy(body.data(), tmpl, n);   // preserve unknown bytes (0,1,10,11, tail)
    }

    // Port LE.
    body[kUdpOffPort + 0] = static_cast<uint8_t>(info.port & 0xFF);
    body[kUdpOffPort + 1] = static_cast<uint8_t>((info.port >> 8) & 0xFF);

    // IP byte-reversed.
    body[kUdpOffIp + 0] = info.ip[3];
    body[kUdpOffIp + 1] = info.ip[2];
    body[kUdpOffIp + 2] = info.ip[1];
    body[kUdpOffIp + 3] = info.ip[0];

    body[kUdpOffFlags] = info.flags;
    body[kUdpOffDiff]  = info.difficulty;
    body[kUdpOffCur]   = info.cur_players;
    body[kUdpOffMax]   = info.max_players;

    // Name -> UTF-16LE, max 24 chars, null-padded (field already zeroed).
    const size_t max_chars = kUdpNameBytes / 2;
    const size_t n = info.name.size() < max_chars ? info.name.size() : max_chars;
    for (size_t i = 0; i < n; ++i) {
        body[kUdpOffName + i * 2 + 0] = static_cast<uint8_t>(info.name[i]);
        body[kUdpOffName + i * 2 + 1] = 0;  // narrow -> BMP low byte
    }
    return body;
}

bool udp_announce_encode(const uint8_t header4[4], const uint8_t* inflated_body, size_t body_len,
                         ZlibCodec& zlib, std::vector<uint8_t>* out_packet) {
    if (!header4 || !inflated_body || !out_packet)
        return false;
    std::vector<uint8_t> compressed;
    if (!zlib.deflate(inflated_body, body_len, &compressed))
        return false;

    out_packet->clear();
    out_packet->reserve(kUdpHeaderSize + compressed.size());
    out_packet->insert(out_packet->end(), header4, header4 + kUdpHeaderSize);
    out_packet->insert(out_packet->end(), compressed.begin(), compressed.end());
    return true;
}

void udp_overwrite_ip(uint8_t* inflated_body, size_t body_len, const uint8_t public_ip[4]) {
    if (!inflated_body || !public_ip || body_len < kUdpOffIp + 4)
        return;
    inflated_body[kUdpOffIp + 0] = public_ip[3];
    inflated_body[kUdpOffIp + 1] = public_ip[2];
    inflated_body[kUdpOffIp + 2] = public_ip[1];
    inflated_body[kUdpOffIp + 3] = public_ip[0];
}

} // namespace net
} // namespace sacred
