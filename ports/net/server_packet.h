// server_packet.h — Sacred lobby SERVERPACKET (134-byte "server in list" body) + the
//                    lobby packet-type dispatch (add/remove/MOTD).
//
// SOURCE: sdk/.claude/knowledge/refs_magician_net.md §2.1 (lobby packet-type IDs / body map),
//         §2.2 (SERVERPACKET struct), §2.3 (static endpoints). Ports the struct + the
//         12/13/14/21/56 switch from refs SacredFilter/LobbyManager.cpp + SacredFilter.h.
//
// A lobby control packet's BODY (see lobby_frame.h) carries a u16 packet TYPE at body+2.
// For server add/update types the body IS a SERVERPACKET, which the original proxy parses
// and rewrites (IP/port -> localhost) to route games through itself.
//
// TODO(port): wire when standing up a private/community master server or a live lobby
//             observer. Header-only, pure byte logic; nothing wires it yet.
// NOTE: no absolute engine VAs dereferenced — no byte-sig verification needed. The lobby
//       host string is a network endpoint constant, not an EXE offset.

#ifndef SACRED_SDK_PORTS_NET_SERVER_PACKET_H
#define SACRED_SDK_PORTS_NET_SERVER_PACKET_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sacred {
namespace net {

// ---- Static endpoints (refs §2.3) ---------------------------------------------------------
// Real Ascaron lobby host/port. Proxy fakes are kept for reference.
static const char     kSacredLobbyHost[]          = "sacredenu.ascaron-net.com";
enum : uint16_t {
    kSacredLobbyPort        = 7066,   // SACRED_NETWORK_LOBBYPORT
    kVirtualLobbyPort       = 13763,  // VIRTUAL_NETWORK_LOBBYPORT (proxy's fake lobby)
    kVirtualGamePortBase    = 15763,  // VIRTUAL_NETWORK_GAMEPORT_BASE (per-game, increments)
};

// ---- Lobby packet types (refs §2.1) -------------------------------------------------------
// usPacketType = *(u16*)&body[2].
enum LobbyPacketType : uint16_t {
    kLobbyTypeServerAdd1    = 12,  // server appeared/updated
    kLobbyTypeServerAdd2    = 13,  // server appeared/updated
    kLobbyTypeServerRemove  = 14,  // server removed from list
    kLobbyTypeServerAdd3    = 21,  // server appeared/updated
    kLobbyTypeMotd          = 56,  // welcome / MOTD (text @ body+150, u32 len @ body+20)
};

// Body field offsets shared across lobby control packets.
enum : size_t {
    kLobbyBodyOffType   = 2,    // u16 packet type
    kLobbyMotdTextOff   = 150,  // MOTD text start (type 56)
    kLobbyMotdLenOff    = 20,   // u32 MOTD text length (type 56)
};

// Read the lobby packet type from a body buffer (>= 4 bytes). Little-endian u16 at +2.
inline uint16_t lobby_packet_type(const uint8_t* body, size_t body_len) {
    if (!body || body_len < kLobbyBodyOffType + 2)
        return 0;
    return static_cast<uint16_t>(body[kLobbyBodyOffType] |
                                 (body[kLobbyBodyOffType + 1] << 8));
}

// Classify a body. true => the body is a SERVERPACKET (add/update). 'is_remove' set for 14.
inline bool lobby_is_server_packet(uint16_t type, bool* is_remove = nullptr) {
    if (is_remove) *is_remove = (type == kLobbyTypeServerRemove);
    switch (type) {
        case kLobbyTypeServerAdd1:
        case kLobbyTypeServerAdd2:
        case kLobbyTypeServerAdd3:
        case kLobbyTypeServerRemove:
            return true;
        default:
            return false;
    }
}

// ---- SERVERPACKET (refs §2.2) -------------------------------------------------------------
// #pragma pack(1); total 18 + 80 + 4 + 4 + 2 + 10 + 4 + 12 = 134 bytes.
#pragma pack(push, 1)
struct ServerPacket {
    uint8_t  header[18];   // packet header within the body
    char     name[80];     // server/game name (ASCII, null-terminated)
    uint32_t ip1;          // "internal" IP (often LAN), raw 4-byte network order
    uint32_t ip2;          // "external"/public IP (the routable one), raw network order
    uint16_t port;         // game server TCP port
    uint8_t  unknown[10];
    uint32_t id;           // lobby-assigned server ID (key for tracking)
    uint8_t  unknown2[12];
};
#pragma pack(pop)

static_assert(sizeof(ServerPacket) == 134, "SERVERPACKET must be 134 bytes (pack(1))");

// Parse a body into a SERVERPACKET (copy; safe for unaligned/short-lived buffers).
// Returns false if the body is too short.
inline bool server_packet_parse(const uint8_t* body, size_t body_len, ServerPacket* out) {
    if (!body || !out || body_len < sizeof(ServerPacket))
        return false;
    std::memcpy(out, body, sizeof(ServerPacket));
    return true;
}

// Serialize a SERVERPACKET back into a 134-byte buffer.
inline void server_packet_write(const ServerPacket& sp, uint8_t out[sizeof(ServerPacket)]) {
    std::memcpy(out, &sp, sizeof(ServerPacket));
}

// Rewrite routing to localhost (the proxy hijack: ip1=ip2=127.0.0.1, port=local).
// 'ip_127_0_0_1_net' is 127.0.0.1 in network byte order == inet_addr("127.0.0.1").
// We compute it without winsock to keep this header standalone: bytes {127,0,0,1} LE-packed.
inline uint32_t loopback_ip_net() {
    // network order 127.0.0.1 == 0x0100007F on a little-endian host.
    return 0x0100007Fu;
}
inline void server_packet_route_to_localhost(ServerPacket* sp, uint16_t local_port) {
    if (!sp) return;
    sp->ip1 = sp->ip2 = loopback_ip_net();
    sp->port = local_port;
}

} // namespace net
} // namespace sacred

#endif // SACRED_SDK_PORTS_NET_SERVER_PACKET_H
