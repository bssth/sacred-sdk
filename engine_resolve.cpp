// engine_resolve.cpp — engine VA resolver, anchored on Sacred_decrypted.exe.
//
// KEY FACT (corrected 2026-06-13, see our_build_vas.md):
//   `sdk/Sacred_decrypted.exe` is FULLY decrypted in `.text` (the earlier
//   "still encrypted on disk" claim was WRONG — it used a bad daa-density
//   threshold). The on-disk Sacred.exe IS SecuROM-encrypted (its .text is
//   garbage), but the decrypted dump has real x86 at every known VA, verified
//   against control functions (FUN_00811440 = `51 53 55 8b`, cEngine_save SEH
//   prologue, cObjectManager_allocate, …). The file maps 1:1: for EVERY
//   section, VA = file_offset + 0x400000. Image base 0x00400000, no ASLR, so
//   the LIVE process image == the decrypted file byte-for-byte. Therefore engine
//   VAs are resolvable STATICALLY and equal the live VAs.
//
// So this resolver no longer relies on a fragile live string-immediate scan.
// It PINS the VAs found offline from Sacred_decrypted.exe and VERIFIES each at
// runtime with a byte-signature check (read the live prologue, compare to the
// bytes the decrypted file has there). A match confirms the VA for this build;
// a mismatch leaves it unresolved (layout shifted / patched / wrong build).
//
// Read-only: never writes to .text. Safe to run any time after .bind decryption.
//
// Resolved offline from Sacred_decrypted.exe (string anchors → enclosing fn):
//   inflate        0x00669b10  ("incorrect header check"@0x00963ca0 +
//                               "invalid block type"@0x00963c2c both inside it)
//                               prologue: 8b 54 24 04 83 ec 44 85 d2 53 55 56
//                               = mov edx,[esp+4]; sub esp,44h; test edx,edx; …
//                               i.e. inflate(z_streamp strm, int flush), FPO.
//   inflateInit2_  0x006643a0  ("1.2.1" version string@0x008943e8)
//                               prologue: 55 8b ec 83 ec 10 …
//   debug logger   0x0066e6a0  ("DEBUG.LOG"@0x00964010)
//                               prologue: 55 8b ec 83 ec 08 56 0f 31 …
//   uncompress     0x0066e160  (one of the two E8 callers of inflate; the small
//                               cdecl wrapper that does sub esp,38h [z_stream] →
//                               inflateInit_(&s,"1.2.1"@0x963b20,0x38)@0x669af0 →
//                               inflate(&s,Z_FINISH=4)@0x669b10 → inflateEnd@
//                               0x66b180 → plain ret. So it IS the stock
//                               int __cdecl uncompress(dst,&dlen,src,slen).
//                               prologue: 83 ec 38 8b 4c 24 48 8b 44 24 44 8b
//
// call_uncompress() below lets the PAX hero-save codec call this in-process,
// which lets us DROP the external zlib1.dll dependency entirely.

#include "sdk.h"
#include <cstring>   // strcmp

namespace sdk {
namespace engine_resolve {

ResolvedEngine g_engine{};   // cached results (declared in sdk.h)

// One pinned + signature-guarded engine symbol.
struct Pinned {
    const char*   name;
    uint32_t      va;        // absolute VA (== live, file maps 1:1)
    const uint8_t sig[12];   // expected first 12 bytes (from Sacred_decrypted.exe)
    size_t        siglen;
};

static const Pinned kPinned[] = {
    { "inflate",       0x00669b10,
      { 0x8b,0x54,0x24,0x04, 0x83,0xec,0x44, 0x85,0xd2, 0x53,0x55,0x56 }, 12 },
    { "inflateInit2_", 0x006643a0,
      { 0x55,0x8b,0xec, 0x83,0xec,0x10, 0x8d,0x45,0xf0, 0x89,0x45,0xfc }, 12 },
    { "debug_log",     0x0066e6a0,
      { 0x55,0x8b,0xec, 0x83,0xec,0x08, 0x56, 0x0f,0x31, 0x89,0x45,0xf8 }, 12 },
    { "uncompress",    0x0066e160,
      { 0x83,0xec,0x38, 0x8b,0x4c,0x24,0x48, 0x8b,0x44,0x24,0x44, 0x8b }, 12 },
    { "globalres",     0x006726f0,   // void* __stdcall(handle) -> resource ptr
      { 0x8b,0x44,0x24,0x04, 0xa9,0x00,0x00,0x00,0x80, 0x74,0x13,0x25 }, 12 },
    { "objmgr_alloc",  0x005fafe0,   // cObjectManager::allocate __thiscall(this,size)
      { 0x83,0xec,0x24, 0x8b,0x44,0x24,0x28, 0x53,0x55,0x56, 0x8b,0xf1 }, 12 },
    { "globalres_str", 0x0080f5e0,   // wchar_t* __thiscall(mgr,key); mgr=(void*)0x0182ED50
      { 0x53,0x56, 0x8b,0x31, 0x57, 0x85,0xf6, 0x0f,0x84,0x8d,0x00,0x00 }, 12 },
};

// Does live memory at `va` start with the expected signature bytes?
static bool sig_ok(uint32_t va, const uint8_t* sig, size_t n) {
    const uint8_t* p = (const uint8_t*)(uintptr_t)va;
    for (size_t i = 0; i < n; ++i) if (p[i] != sig[i]) return false;
    return true;
}

static bool resolve_impl() {
    uintptr_t base = (uintptr_t)g_attach.exe_module;
    if (!base) { sdk_log("[engine_resolve] no exe_module"); return false; }
    if (base != 0x00400000)
        sdk_log("[engine_resolve] NOTE: image base %p != 0x00400000 — pinned VAs "
                "assume no-ASLR; sig-check will catch any mismatch", (void*)base);

    int ok = 0;
    for (const Pinned& s : kPinned) {
        const uint8_t* p = (const uint8_t*)(uintptr_t)s.va;
        bool good = sig_ok(s.va, s.sig, s.siglen);
        sdk_log("[engine_resolve] %-13s @ %08x : live=[%02x %02x %02x %02x %02x %02x] %s",
                s.name, s.va, p[0], p[1], p[2], p[3], p[4], p[5],
                good ? "VERIFIED" : "SIG MISMATCH (unresolved)");
        uint32_t resolved = good ? s.va : 0;
        if (!strcmp(s.name, "inflate"))            g_engine.inflate = resolved;
        else if (!strcmp(s.name, "inflateInit2_")) g_engine.inflate_init = resolved;
        else if (!strcmp(s.name, "debug_log"))     g_engine.debug_log = resolved;
        else if (!strcmp(s.name, "uncompress"))    g_engine.uncompress = resolved;
        else if (!strcmp(s.name, "globalres"))     g_engine.globalres = resolved;
        else if (!strcmp(s.name, "objmgr_alloc"))  g_engine.objmgr_alloc = resolved;
        else if (!strcmp(s.name, "globalres_str")) g_engine.globalres_str = resolved;
        if (good) ++ok;
    }
    g_engine.resolved = (ok == (int)(sizeof(kPinned)/sizeof(kPinned[0])));
    sdk_log("[engine_resolve] DONE: %d/%d verified -> inflate=%p uncompress=%p "
            "globalres=%p objmgr_alloc=%p debug_log=%p", ok,
            (int)(sizeof(kPinned)/sizeof(kPinned[0])),
            (void*)g_engine.inflate, (void*)g_engine.uncompress,
            (void*)g_engine.globalres, (void*)g_engine.objmgr_alloc,
            (void*)g_engine.debug_log);
    return g_engine.inflate != 0;
}

bool resolve() {
    __try { return resolve_impl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[engine_resolve] faulted reading live .text (not yet decrypted?)");
        return false;
    }
}

static DWORD WINAPI resolve_worker(LPVOID) {
    Sleep(9000);                 // let .bind/SecuROM decrypt all code pages
    resolve();

    // DEV self-test (only reached when started via sdk.ini selftest=1): probe a
    // few known global.res handles (Vampiress/Battle Mage companion name_res from
    // companions.h, + the high-bit form). Miss/empty = global.res not loaded yet
    // (e.g. main menu); retry via sacred.globalres() in-game. Proves the resolver
    // chain end-to-end when it hits. NOT part of the lazy resolve() path, so the
    // normal on-demand resolves stay silent.
    if (g_engine.globalres_str) {
        const unsigned int kTest[] = { 0x0019F6E3u, 0x8019F6E3u, 0x001A0153u };
        char tb[512];
        for (unsigned int h : kTest) {
            bool hit = globalres_string(h, tb, (int)sizeof(tb));
            sdk_log("[engine_resolve] globalres(%08x) -> %s", h,
                    hit ? tb : "(miss / not loaded)");
        }
    }
    return 0;
}

void start_engine_resolve() {
    HANDLE h = CreateThread(nullptr, 0, resolve_worker, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

// ---- game zlib uncompress (drops the zlib1.dll dependency) -----------------

typedef int (__cdecl* zlib_uncompress_t)(unsigned char* dst, unsigned long* dstLen,
                                         const unsigned char* src, unsigned long srcLen);

// Ensure uncompress is resolved+verified, returning the callable ptr or null.
// Lazily runs the full resolve pass once if it hasn't happened yet.
static zlib_uncompress_t ensure_uncompress() {
    if (!g_engine.uncompress) resolve();      // idempotent; re-verifies all pins
    return reinterpret_cast<zlib_uncompress_t>(g_engine.uncompress);
}

bool uncompress_available() { return ensure_uncompress() != nullptr; }

// rc convention: 0 = Z_OK (zlib), other = zlib error; -1000 = unavailable.
int call_uncompress(unsigned char* dst, unsigned long* dstLen,
                    const unsigned char* src, unsigned long srcLen) {
    zlib_uncompress_t fn = ensure_uncompress();
    if (!fn) return -1000;                    // not decrypted yet / sig mismatch
    __try {
        return fn(dst, dstLen, src, srcLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[engine_resolve] call_uncompress faulted");
        return -1001;
    }
}

// ---- global.res string resolve (handle -> UTF-8) ---------------------------
//
// FUN_0080f5e0 is __thiscall(ecx = global.res manager, key) and returns a
// freshly malloc'd, NUL-terminated UTF-16 string (or NULL on miss). The manager
// is the STATIC object AT 0x0182ED50 (0x6726f0 loads it as an immediate into
// ECX), and the fn null-checks its own table — so the call is safe even before
// global.res finishes loading. NOTE: it allocates via the game's malloc; we copy
// the result out and intentionally do NOT free it (the matching engine free VA
// isn't pinned) — a few bytes leak per lookup, acceptable for introspection.

typedef wchar_t* (__thiscall* globalres_str_t)(void* mgr, unsigned int key);
static const uintptr_t kGlobalResMgr = 0x0182ED50;

static wchar_t* call_globalres_str(unsigned int key) {
    if (!g_engine.globalres_str) resolve();
    if (!g_engine.globalres_str) return nullptr;
    globalres_str_t fn = reinterpret_cast<globalres_str_t>(g_engine.globalres_str);
    __try {
        return fn(reinterpret_cast<void*>(kGlobalResMgr), key);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[engine_resolve] call_globalres_str faulted");
        return nullptr;
    }
}

bool globalres_string(unsigned int handle, char* out, int cap) {
    if (!out || cap <= 0) return false;
    out[0] = 0;
    unsigned int key = handle & 0x7FFFFFFFu;
    wchar_t* ws = call_globalres_str(key);
    if (!ws) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, cap, nullptr, nullptr);
    if (n <= 0) { out[0] = 0; return false; }
    out[cap - 1] = 0;
    return true;
}

} // namespace engine_resolve
} // namespace sdk
