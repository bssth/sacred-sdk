// udp_announce.h — Sacred UDP server-announce packet codec (a running host advertising a game).
//
// SOURCE: sdk/.claude/knowledge/refs_magician_net.md §2.4 ("Server-announce UDP packet").
//         Reimplements SacredAncariaConnection (SAC) Server.ServerFromUncompressedData /
//         CompressData (C#) in C++. The SAC default ports are §2.4 too.
//
// FRAME:  [ 4-byte header (kept verbatim) ] [ zlib-compressed payload (RFC-1950 zlib) ]
//   The 4 header bytes are copied unchanged in->out; only the inflated body is parsed/edited.
//
// INFLATED server-info body byte map (§2.4):
//   +2..+3  u16  Port        = body[3]*256 + body[2]          (LE u16)
//   +4..+7  4    IP          = {body[7],body[6],body[5],body[4]}  (stored byte-reversed)
//   +8      1    flags       bit 0x04=Locked, 0x02=Pass req, 0x80(>>7)=Started,
//                            (b>>4)&7 = GameMode
//   +9      1    Difficulty  (bit-flag enum, NOT 0..4)
//   +12     1    CurNumber   current players
//   +13     1    MaxNumber   max players
//   +14..+61 48  Name        UTF-16LE, null-trimmed, 24 chars max
//
// TODO(port): wire when the SDK advertises a modded host or enumerates games without the C#
//             SAC bridge. Nothing wires it yet.
// TODO(port): share herosave zlib_codec — zlib (de)compression is taken via the ZlibCodec
//             interface below so this stays standalone; supply the SDK's existing zlib wrapper
//             (the same one shlib/herosave needs for 0xBAADC0DE sections) at wire time.
// NOTE: no absolute engine VAs dereferenced — no byte-sig verification needed.

#ifndef SACRED_SDK_PORTS_NET_UDP_ANNOUNCE_H
#define SACRED_SDK_PORTS_NET_UDP_ANNOUNCE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sacred {
namespace net {

// ---- SAC default UDP ports (refs §2.4) ----------------------------------------------------
enum : uint16_t {
    kSacServerPortListen    = 2006,  // NETWORK_PORT_LISTEN (SAC receives the game's announce)
    kSacClientPortBroadcast = 2005,  // NETWORK_PORT_BROADCAST (re-emit to client loopback/LAN)
};

// ---- Enums (refs §2.4) --------------------------------------------------------------------
// Difficulty is a bit-flag enum (NOT contiguous 0..4).
enum Difficulty : uint8_t {
    kDiffBronze   = 0,
    kDiffSilver   = 1,
    kDiffGold     = 2,
    kDiffPlatinum = 4,
    kDiffNiobium  = 8,
};
// GameMode = (flags >> 4) & 7.
enum GameMode : uint8_t {
    kModeUnderworldCampaign = 0,
    kModeCampaign           = 1,
    kModeFree               = 2,
    kModePlayerkiller       = 4,
};
// flags (byte +8) bits.
enum : uint8_t {
    kFlagPasswordReq = 0x02,
    kFlagLocked      = 0x04,
    kFlagStarted     = 0x80,
};

// ---- zlib interface (TODO(port): share herosave zlib_codec) --------------------------------
// RFC-1950 zlib (NOT raw deflate, NOT gzip), matching Ionic.Zlib ZlibStream used by SAC.
// Implement with the SDK's existing zlib wrapper. Return false on failure.
struct ZlibCodec {
    virtual ~ZlibCodec() {}
    // Inflate 'in' (zlib stream) into 'out'. Caller does not know inflated size up front.
    virtual bool inflate(const uint8_t* in, size_t in_len, std::vector<uint8_t>* out) = 0;
    // Deflate 'in' into a zlib stream 'out'.
    virtual bool deflate(const uint8_t* in, size_t in_len, std::vector<uint8_t>* out) = 0;
};

// ---- Decoded server info ------------------------------------------------------------------
struct ServerInfo {
    uint16_t    port = 0;
    uint8_t     ip[4] = {0, 0, 0, 0};   // human order: ip[0].ip[1].ip[2].ip[3]
    uint8_t     flags = 0;
    uint8_t     difficulty = kDiffBronze;
    uint8_t     cur_players = 0;
    uint8_t     max_players = 0;
    std::string name;                   // decoded from UTF-16LE, null-trimmed

    bool locked()        const { return (flags & kFlagLocked) != 0; }
    bool password_req()  const { return (flags & kFlagPasswordReq) != 0; }
    bool started()       const { return (flags & kFlagStarted) != 0; }
    uint8_t game_mode()  const { return static_cast<uint8_t>((flags >> 4) & 7); }
};

// Field offsets within the INFLATED body.
enum : size_t {
    kUdpHeaderSize   = 4,
    kUdpOffPort      = 2,
    kUdpOffIp        = 4,
    kUdpOffFlags     = 8,
    kUdpOffDiff      = 9,
    kUdpOffCur       = 12,
    kUdpOffMax       = 13,
    kUdpOffName      = 14,
    kUdpNameBytes    = 48,   // 24 UTF-16LE chars
    kUdpBodyMin      = kUdpOffName + kUdpNameBytes,  // 62
};

// ---- Decode -------------------------------------------------------------------------------

// Parse an already-inflated body (>= 62 bytes) into ServerInfo. Returns false if too short.
bool udp_parse_inflated(const uint8_t* body, size_t body_len, ServerInfo* out);

// Full decode: strip the 4-byte header, inflate the rest via 'zlib', parse. On success also
// returns the verbatim 4-byte header (needed to re-emit) via 'out_header4' if non-null.
bool udp_announce_decode(const uint8_t* packet, size_t packet_len, ZlibCodec& zlib,
                         ServerInfo* out, uint8_t out_header4[4] = nullptr);

// ---- Encode -------------------------------------------------------------------------------

// Build the inflated body (>= 62 bytes) from ServerInfo. Bytes 0,1,10,11 and any tail are
// left as supplied by 'tmpl' (a prior inflated body) or zero-filled if tmpl is null/short.
std::vector<uint8_t> udp_build_inflated(const ServerInfo& info,
                                        const uint8_t* tmpl = nullptr, size_t tmpl_len = 0);

// Full encode: deflate 'inflated_body' via 'zlib' and prepend the 4-byte header verbatim.
bool udp_announce_encode(const uint8_t header4[4], const uint8_t* inflated_body, size_t body_len,
                         ZlibCodec& zlib, std::vector<uint8_t>* out_packet);

// SAC relay helper: overwrite the IP in an INFLATED body at +4..+7 with 'public_ip' (human
// order), byte-reversed as the wire stores it (body[4]=ip[3] .. body[7]=ip[0]). Used because
// the host can't know its own NAT'd public IP. Body must be >= 8 bytes.
void udp_overwrite_ip(uint8_t* inflated_body, size_t body_len, const uint8_t public_ip[4]);

} // namespace net
} // namespace sacred

#endif // SACRED_SDK_PORTS_NET_UDP_ANNOUNCE_H
