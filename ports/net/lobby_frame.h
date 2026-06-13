// lobby_frame.h — Sacred TCP lobby/game packet framing (28-byte header + variable body).
//
// SOURCE: sdk/.claude/knowledge/refs_magician_net.md §2.1 ("TCP lobby/game packet frame")
//         + §2.3. Validated against TINCAT2.DLL; reimplements the framing in
//         refs SacredFilter/SacredSocket.cpp (CSacredSocket::OnReceive / SendPacket),
//         MFC-CSocket-free and transport-agnostic (operates on plain byte buffers).
//
// FRAME:  [ HEADER : 28 bytes ] [ BODY : variable ]
//   +0  12  (unknown / routing — passed through verbatim)
//   +12  4  packet FORMAT      (u32 LE). format == 5 == raw/passthrough "oddball" packet.
//   +16  4  (unknown)
//   +20  4  BODY LENGTH        (u32 LE)  -- #define BODYSIZE(p) (*(unsigned long*)&p[20])
//   +24  4  CRC32(body)        (u32 LE)  -- last 4 bytes; tincat_crc32(body, bodyLen)
//
// RECEIVE loop is classic length-prefixed framing: read 28-byte header, then read BODYSIZE
//   body bytes. SEND recomputes the body CRC into header[24] before transmit.
//
// TODO(port): wire when the SDK gains a live lobby observer / man-in-the-middle codec.
//             Back the actual socket I/O with raw winsock or the SDK's existing IO layer;
//             this header only does in-memory encode/decode + framing math.
// NOTE: pure byte-buffer logic — no absolute engine VAs, so no byte-sig verification needed.

#ifndef SACRED_SDK_PORTS_NET_LOBBY_FRAME_H
#define SACRED_SDK_PORTS_NET_LOBBY_FRAME_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sacred {
namespace net {

// Header field layout (byte offsets into the 28-byte header).
enum : size_t {
    kLobbyHeaderSize   = 28,
    kLobbyOffFormat    = 12,  // u32 packet format
    kLobbyOffBodyLen   = 20,  // u32 body length
    kLobbyOffCrc       = 24,  // u32 CRC32(body)
};

// Packet format sentinel: 5 == raw/passthrough body (no lobby parsing).
enum : uint32_t { kLobbyFormatRawPassthrough = 5 };

// A decoded lobby frame: the 28 routing/format header bytes split out, plus the body.
struct LobbyFrame {
    uint8_t              header[kLobbyHeaderSize];  // full 28-byte header as on the wire
    std::vector<uint8_t> body;                      // body bytes (length == hdr[+20])

    uint32_t format() const;       // header +12
    uint32_t body_len() const;     // header +20 (== body.size() for a valid frame)
    uint32_t crc() const;          // header +24 (stored CRC of the body)
    bool     is_raw_passthrough() const { return format() == kLobbyFormatRawPassthrough; }

    // True if the stored CRC (+24) matches tincat_crc32 over the body. Use to validate
    // a received frame before trusting/parsing its body.
    bool crc_valid() const;
};

// Little-endian header accessors over a raw 28+ byte buffer (no bounds growth; caller ensures
// at least kLobbyHeaderSize bytes are present).
uint32_t lobby_read_u32(const uint8_t* p, size_t off);
void     lobby_write_u32(uint8_t* p, size_t off, uint32_t v);

// ---- DECODE -------------------------------------------------------------------------------

// How many body bytes a header announces. Caller reads exactly this many after the header.
inline uint32_t lobby_body_size(const uint8_t* header28) {
    return lobby_read_u32(header28, kLobbyOffBodyLen);
}

// Decode a full frame from a contiguous buffer that holds header+body. On success returns true,
// writes the frame, and (if 'consumed' != null) sets it to total bytes used (28 + bodyLen).
// Fails (returns false) if 'len' is shorter than 28 + announced body length.
bool lobby_decode(const uint8_t* buf, size_t len, LobbyFrame* out, size_t* consumed = nullptr);

// ---- ENCODE -------------------------------------------------------------------------------

// Serialize a frame to the wire: copies the 28-byte header, fixes up body-len (+20) and
// CRC (+24) from the actual body, then appends the body. Returns the contiguous buffer.
// This mirrors CSacredSocket::SendPacket (recompute CRC into header[24] before send).
std::vector<uint8_t> lobby_encode(const LobbyFrame& frame);

// Convenience: build a frame from a (mostly-zero) 28-byte template header + body, setting
// format, body-len and CRC. 'header_template' may be null (=> all-zero header).
LobbyFrame lobby_make(const uint8_t* header_template, uint32_t format,
                      const uint8_t* body, size_t body_len);

} // namespace net
} // namespace sacred

#endif // SACRED_SDK_PORTS_NET_LOBBY_FRAME_H
