// globalres.h — global.res index+blob emitter (byte-exact rebuild + append).
//
// SOURCE:
//   - sdk/.claude/knowledge/re/globalres_format.md §1-§4 (byte-exact, verified
//     against the real 23123-slot file: from-scratch rebuild = 0 mismatches).
//   - Resolver math FUN_0080f5e0; loader FUN_0080e680.
//   - re_backlog.md S4 + "C++-port candidates" #2.
//
// FORMAT (all little-endian; UTF-16-LE payloads, NO terminators, NO sentinels):
//   INDEX: N records * 16 bytes: { u32 d0; u32 ident; u32 off; u32 pad; }
//     d0[0]   = N (binary-search hi bound / slot count)
//     d0[k]   = 2 * units(slot k-1)         for k = 1..N-1
//     ident[k]= sacred_hash(name) & 0x7fffffff      (index sorted asc by ident)
//     off[0]  = N*16 (= blob_start);  off[k+1] = off[k] + 2*units(k)
//     pad[k]  = 0 (unused; 4 vanilla slots carry 1/3/5 cosmetically)
//   BLOB: u32 = 2*units(N-1)   (the "extra length" dword the loader reads for
//         the last slot), then text payloads back-to-back in INDEX order.
//   units(k) = number of UTF-16 code units of slot k's text (terminator excl).
//   File ends exactly at off[N-1] + 4 + 2*units(N-1).
//
// CRUCIAL coupling (globalres_format.md §3d): blob PHYSICAL order MUST equal
// index order, because d0[k+1] is the length for whatever slot sits at index k.
// Therefore the emitter sorts the full slot list ascending by ident and emits
// index and blob in that single shared order.
//
// This emitter is self-contained: it depends only on sacred_hash.h (a sibling
// port) and standard headers. It does NOT read/write files itself — it builds
// an in-memory byte vector the caller can persist (or feed to fs_override).
//
// TODO(port): wire when replacing custom/lua/lib/text.lua's stale span-copy
//   model with a real re-derive emitter (handles appends, not just in-place
//   reskins). globalres_format.md §4 is the spec this implements. Currently
//   UNWIRED — not added to SacredSDK.vcxproj, not #included anywhere.

#ifndef SACRED_SDK_PORTS_ENGINE_GLOBALRES_H
#define SACRED_SDK_PORTS_ENGINE_GLOBALRES_H

#include <cstdint>
#include <string>
#include <vector>

namespace sacred {
namespace engine {

// One logical resource slot. `text_units` holds the UTF-16-LE code units of
// the string WITHOUT a trailing terminator (the engine appends NUL itself).
struct GlobalResSlot {
    uint32_t              ident = 0;  // sacred_hash(name) & 0x7fffffff
    std::vector<uint16_t> text_units; // UTF-16-LE code units, no NUL
    uint32_t              pad = 0;    // index pad field (0 is always safe)
};

// Build the slot list from a freshly-parsed vanilla global.res image. Returns
// the slots in PARSE (index) order; ident/text are exactly as on disk. Use this
// when round-tripping or before appending. Returns false on malformed input
// (e.g. truncated, blob_start not a multiple of 16).
//
// `file` is the entire global.res byte image (index ++ blob).
bool ParseGlobalRes(const std::vector<uint8_t>& file,
                    std::vector<GlobalResSlot>& out_slots);

// Make a slot for a (name, utf16 text) pair. The ident is computed via
// sacred_hash(name). `text_units` must NOT include a trailing 0 terminator.
GlobalResSlot MakeSlot(const std::string& name,
                       const std::vector<uint16_t>& text_units,
                       uint32_t pad = 0);

// Convenience: build a slot from a name and a narrow ASCII/Latin-1 string,
// widening each byte to one UTF-16 code unit (sufficient for ASCII keys/text;
// for real localized text the caller should pass explicit UTF-16 units).
GlobalResSlot MakeSlotAscii(const std::string& name,
                            const std::string& ascii_text,
                            uint32_t pad = 0);

// Emit a byte-exact global.res image from a slot list. The function:
//   1) sorts a copy of `slots` ascending by ident (unsigned == signed here,
//      since every ident has bit31 clear), deduping equal idents (last wins),
//   2) emits the 16-byte index records with d0/off chained per §4,
//   3) emits the blob (extra-length dword + payloads) in the same order.
// Returns the complete file image. Deterministic; no I/O.
std::vector<uint8_t> EmitGlobalRes(const std::vector<GlobalResSlot>& slots);

} // namespace engine
} // namespace sacred

#endif // SACRED_SDK_PORTS_ENGINE_GLOBALRES_H
