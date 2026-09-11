// engine/build_profile.h — which Sacred.exe are we actually injected into?
//
// Until now the SDK assumed one build and proved it only per-patch-site, with
// 3-to-12-byte prologue signatures scattered across patches.cpp, text_logger.cpp,
// runtime_triggers.cpp and dump_text.cpp. Nothing ever looked at the PE headers,
// so running under a different Sacred build (ReBorn, a future re-release, a
// community patch) was indistinguishable from running under ours — every
// hardcoded VA would simply point at the wrong instruction.
//
// This is the single place that answers "is this the build our address table was
// written for?", in two stages:
//
//   detect()             at DLL attach. Reads the live PE headers, which SecuROM
//                        does NOT encrypt, and matches them against kProfiles.
//                        Cheap, safe under loader lock, no .text access.
//   confirm_decrypted()  after the .bind stub decrypts .text. Verifies the
//                        profile's byte pins against live code. Only then is the
//                        profile TRUSTED and .text patching allowed.
//
// Everything that writes to .text must gate on can_patch_text(). Read-only
// features (Lua, overlay, custom/ overrides, IAT hooks) keep working on an
// unknown build — they degrade rather than misfire.
#pragma once
#include <cstdint>
#include <cstddef>

namespace sdk { namespace engine { namespace build {

// One byte-signature probe into live .text.
struct PinSig {
    uint32_t    va;         // full VA (pre-rebase)
    uint8_t     sig[12];
    uint8_t     len;        // 1..12
    bool        gate;       // true => the decrypt poll loop waits on this site
    const char* what;       // human label for logs
};

struct BuildProfile {
    const char*   id;               // "steam_gold_2006-10-13"
    const char*   display;          // human description for logs/overlay
    uint32_t      pe_timestamp;     // IMAGE_FILE_HEADER.TimeDateStamp
    uint32_t      pe_size_of_image; // IMAGE_OPTIONAL_HEADER.SizeOfImage
    uint32_t      pe_checksum;      // IMAGE_OPTIONAL_HEADER.CheckSum
    uint32_t      text_vsize;       // .text VirtualSize
    const PinSig* pins;
    size_t        n_pins;
};

enum class State {
    Unknown = 0,   // headers did not match any known profile
    Matched,       // headers matched; .text not verified yet
    Trusted,       // pins verified against live .text
    Rejected,      // headers matched but pins failed — do NOT touch .text
};

// Stage 1. Call once at attach, after g_attach is filled. Idempotent.
void detect();

// Stage 2. Call from the post-decrypt worker. Verifies every pin. Idempotent.
// Returns true if the profile reached Trusted.
bool confirm_decrypted();

State                state();
const BuildProfile*  profile();          // null unless Matched/Trusted
const char*          state_text();       // for the overlay banner

// The one question every .text writer should ask.
inline bool can_patch_text() { return state() == State::Trusted; }

// Rebase delta for the live module (0 under Sacred's no-ASLR). Valid after detect().
uintptr_t rebase();

// Live-image PE facts, filled by detect() even when nothing matched — so the
// log/overlay can tell the user exactly what they are running.
struct Fingerprint {
    uint32_t timestamp, size_of_image, checksum, text_vsize;
    uintptr_t image_base;
    bool valid;
};
const Fingerprint& fingerprint();

// Verify the GATE-flagged pins only. Used by the decrypt poll loop to decide
// whether .text is readable plaintext yet. Returns the first failing pin's
// label in *why (or null).
bool gate_pins_ready(const char** why);

}}} // namespace sdk::engine::build
