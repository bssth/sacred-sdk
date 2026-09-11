// engine/build_profile.cpp — see build_profile.h.
//
// The pin table doubles as documentation: every VA here has been verified byte
// for byte against sdk/Sacred_decrypted.exe, which maps 1:1 to the live image
// (VA = file offset + 0x400000 for .text/.rdata/.data — see
// .claude/knowledge/re/our_build_vas.md).
#include "../sdk.h"
#include "build_profile.h"
#include <cstring>

namespace sdk { namespace engine { namespace build {

// ---------------------------------------------------------------------------
//  Steam / GOG "Sacred Gold" — MASTER build compiled Oct 13 2006, SecuROM .bind
// ---------------------------------------------------------------------------
// The four `gate` pins are exactly the set dump_text.cpp used before this file
// existed; they are the sites whose plaintext proves the decryptor has run.
// Keeping precisely those four as the gate means the decrypt timing behaviour is
// unchanged — the additional pins are verified but never block the gate, so a
// page that decrypts late (or on demand) can never cost us patches::install()
// the way a wider gate would.
static const PinSig kSteamPins[] = {
    // --- gate set (identical to the historical dump_text.cpp kSites) ---
    { 0x0080EAF0, { 0x6A,0xFF,0x68 },                          3, true,  "globalres lookup" },
    { 0x0080F5E0, { 0x53,0x56,0x8B },                          3, true,  "globalres string" },
    { 0x00672CF0, { 0x8B,0x44,0x24 },                          3, true,  "res resolve" },
    { 0x00811440, { 0x51,0x53,0x55 },                          3, true,  "force-foreground wait" },

    // --- identity pins: spread across the pages our patches target ---
    { 0x00615F30, { 0x6A,0xFF,0x68,0x2C,0xED,0x86,0x00 },      7, false, "console command handler" },
    { 0x0061981B, { 0x0F,0x85,0xF8,0x01,0x00,0x00 },           6, false, "console error branch" },
    { 0x0081145F, { 0x74,0x09,0x5E,0x5D,0x32,0xC0,0x5B,0x59 }, 8, false, "debugger-freeze branch" },
    { 0x00812D8C, { 0x74,0x3A,0x8B,0x94,0x24,0xB8,0x02,0x00 }, 8, false, "WM_ACTIVATEAPP branch" },
    { 0x00813C78, { 0x68,0x00,0x00,0x00,0x90,0x50,0x51,0x6A }, 8, false, "CreateWindowEx style push" },
    { 0x00817B4E, { 0x74,0x0D,0xFF,0x15,0xEC,0x01,0x89,0x00 }, 8, false, "instance-mutex branch" },
    { 0x0084C0E1, { 0x74,0x28,0x8B,0x54,0x24,0x10,0x66,0x8B }, 8, false, "chat copy loop guard" },
    { 0x00816C6F, { 0x68,0x00,0x03,0x00,0x00,0x68,0x00,0x04 }, 8, false, "display mode push 768/1024" },
    { 0x0040EB89, { 0x68,0x00,0x00,0xC0,0x43,0x68,0x00,0x00 }, 8, false, "clip rect push +-384/+-512" },
};

static const BuildProfile kProfiles[] = {
    {
        "steam_gold_2006-10-13",
        "Sacred Gold (Steam/GOG), MASTER compiled Oct 13 2006",
        0x452F85C7u,   // TimeDateStamp
        0x0196E000u,   // SizeOfImage
        0x00B6B5F1u,   // CheckSum
        0x0048E632u,   // .text VirtualSize
        kSteamPins,
        sizeof(kSteamPins) / sizeof(kSteamPins[0]),
    },
};

// ---------------------------------------------------------------------------

static State               g_state = State::Unknown;
static const BuildProfile* g_profile = nullptr;
static Fingerprint         g_fp = {};
static bool                g_detected = false;
static bool                g_confirmed = false;

State               state()   { return g_state; }
const BuildProfile* profile() { return g_profile; }
const Fingerprint&  fingerprint() { return g_fp; }

const char* state_text() {
    switch (g_state) {
        case State::Unknown:  return "UNKNOWN";
        case State::Matched:  return "MATCHED";
        case State::Trusted:  return "TRUSTED";
        case State::Rejected: return "REJECTED";
    }
    return "?";
}

uintptr_t rebase() {
    return g_fp.valid ? (g_fp.image_base - 0x00400000u) : 0;
}

// Read the PE headers of the live image. SecuROM encrypts .text, never the
// headers, so this is safe and meaningful at DllMain time.
static bool read_fingerprint(HMODULE mod, Fingerprint& out) {
    out.valid = false;
    if (!mod) return false;
    __try {
        const uint8_t* base = reinterpret_cast<const uint8_t*>(mod);
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const IMAGE_NT_HEADERS32* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;

        out.timestamp     = nt->FileHeader.TimeDateStamp;
        out.size_of_image = nt->OptionalHeader.SizeOfImage;
        out.checksum      = nt->OptionalHeader.CheckSum;
        out.image_base    = reinterpret_cast<uintptr_t>(mod);
        out.text_vsize    = 0;

        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (memcmp(sec[i].Name, ".text", 5) == 0) {
                out.text_vsize = sec[i].Misc.VirtualSize;
                break;
            }
        }
        out.valid = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void detect() {
    if (g_detected) return;
    g_detected = true;

    if (!read_fingerprint(g_attach.exe_module, g_fp)) {
        sdk_log("[build] could not read PE headers of %p — state=UNKNOWN",
                (void*)g_attach.exe_module);
        g_state = State::Unknown;
        return;
    }

    for (const BuildProfile& p : kProfiles) {
        if (p.pe_timestamp     == g_fp.timestamp &&
            p.pe_size_of_image == g_fp.size_of_image &&
            p.pe_checksum      == g_fp.checksum &&
            p.text_vsize       == g_fp.text_vsize) {
            g_profile = &p;
            g_state   = State::Matched;
            sdk_log("[build] %s  ts=%08x img=%08x sum=%08x text=%08x base=%p -> MATCHED",
                    p.id, g_fp.timestamp, g_fp.size_of_image, g_fp.checksum,
                    g_fp.text_vsize, (void*)g_fp.image_base);
            if (g_fp.image_base != 0x00400000u)
                sdk_log("[build] NOTE: image base %p != 0x400000 — rebase delta %p",
                        (void*)g_fp.image_base, (void*)rebase());
            return;
        }
    }

    g_state = State::Unknown;
    sdk_log("[build] UNRECOGNISED Sacred.exe: ts=%08x img=%08x sum=%08x text=%08x",
            g_fp.timestamp, g_fp.size_of_image, g_fp.checksum, g_fp.text_vsize);
    sdk_log("[build] engine features that write .text are DISABLED. "
            "Lua, overlay, custom/ overrides and IAT hooks still work.");
}

// Compare one pin against live memory. SEH-guarded: on a not-yet-decrypted or
// unmapped page this returns false instead of taking the process down.
static bool pin_ok(const PinSig& p, uintptr_t reb) {
    __try {
        const uint8_t* q = reinterpret_cast<const uint8_t*>(reb + p.va);
        for (uint8_t i = 0; i < p.len; ++i)
            if (q[i] != p.sig[i]) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool gate_pins_ready(const char** why) {
    if (why) *why = nullptr;
    if (!g_profile) return false;
    const uintptr_t reb = rebase();
    for (size_t i = 0; i < g_profile->n_pins; ++i) {
        const PinSig& p = g_profile->pins[i];
        if (!p.gate) continue;
        if (!pin_ok(p, reb)) {
            if (why) *why = p.what;
            return false;
        }
    }
    return true;
}

bool confirm_decrypted() {
    if (g_confirmed) return g_state == State::Trusted;
    if (!g_profile) {
        sdk_log("[build] confirm_decrypted: no profile — .text patching stays disabled");
        return false;
    }
    g_confirmed = true;

    const uintptr_t reb = rebase();
    int ok = 0, bad = 0;
    for (size_t i = 0; i < g_profile->n_pins; ++i) {
        const PinSig& p = g_profile->pins[i];
        if (pin_ok(p, reb)) { ++ok; continue; }
        ++bad;
        // Log what we actually found; a mismatch here is either a build we don't
        // know, a page that never decrypted, or someone else patching first.
        uint8_t live[12] = {};
        __try {
            memcpy(live, (const void*)(reb + p.va), p.len);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        char got[64], want[64]; got[0] = want[0] = 0;
        for (uint8_t k = 0; k < p.len && k < 8; ++k) {
            char b[8];
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%02x ", live[k]);   strcat_s(got, b);
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%02x ", p.sig[k]);  strcat_s(want, b);
        }
        sdk_log("[build] pin FAILED @ %08x (%s): live=[%s] expected=[%s]",
                p.va, p.what, got, want);
    }

    if (bad == 0) {
        g_state = State::Trusted;
        sdk_log("[build] pins %d/%d verified -> TRUSTED (%s)",
                ok, (int)g_profile->n_pins, g_profile->id);
    } else {
        g_state = State::Rejected;
        sdk_log("[build] pins %d/%d verified, %d FAILED -> REJECTED; "
                ".text patching disabled for safety", ok, (int)g_profile->n_pins, bad);
    }
    return g_state == State::Trusted;
}

}}} // namespace sdk::engine::build
