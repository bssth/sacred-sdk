// SacredSDK — runtime ports of the 2007 SacredVault unofficial-patch 2.29.
//
// Background: docs/12-patch-229-rosetta.md
// Source ref: docs/refs/Sacred-Patch Source.txt by Thorium, 2007-01-29.
//
// We're a DLL, so instead of binary-patching Sacred.exe we install inline
// detours and direct byte-overwrites at runtime. Image base is fixed at
// 0x00400000 (no ASLR), so absolute addresses are stable.
//
// Currently implemented:
//   - Patch 1: hook FUN_0080e680 (PE BINARY 107 loader) to read
//              scripts/<lang>/global.res from disk.
//   - Patch 6: STUB — site address for our build (Steam 2.28 ASE 2006-10-13)
//              must be re-discovered; Ghidra script in re/ghidra/.

#include "sdk.h"
#include <cstdio>
#include <cstring>

namespace sdk { namespace patches {

volatile bool g_patch1_active = false;
volatile bool g_patch6_active = false;
volatile bool g_glow_off_active = false;

// DISABLED. The captain "red aura" was PROVEN by measurement to be the
// engine ward FX for the invuln bit (cCreature+0x14 & 0x200000, set by
// set_invulnerable) — NOT the FUN_00408b70 bound/charmed swirl the RE
// agent chased. The real fix is Lua-side: the captain is now
// soft-immortal via per-tick HP restore and never sets +0x200000, so no
// ward aura. This swirl stub is therefore dead weight, and it is the
// only DLL change since the guards last had working vanilla dialogues —
// removing it both de-risks and isolates that regression. Keep the gate
// (flip true only if a real charmed-creature mod ever needs the swirl
// suppressed) but default OFF: do not patch the engine for no reason.
bool g_glow_off_enabled = false;

static char g_status[256] = "patches: not yet installed";
const char* status() { return g_status; }
static void set_status(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
    sdk_log("[patches] %s", g_status);
}

// ---------------------------------------------------------------------------
//  Patch 1: FUN_0080e680 → global.res from disk
// ---------------------------------------------------------------------------
//
// Original signature (decompiled): undefined4 FUN_0080e680(void) — __thiscall.
//   ECX = pointer to a 2-field struct { void* data; DWORD size_in_bytes; }.
//   On success: *ECX = freshly-allocated buffer with chained-XOR-decoded
//               global.res bytes; *(ECX+4) = size; returns 1 in EAX.
//   On failure: returns 0.
//
// Original logic walks PE BINARY resource 107 with a chained-XOR
// (`dst[i] = src[i] XOR src[i+1]`, last word XOR 0x45AD). Result is plain
// UTF-16 LE text-resource blob — identical in shape to what ships on disk as
// scripts/<lang>/global.res. So our replacement just opens the disk file and
// copies it verbatim.
//
// Allocator: original uses Sacred's MSVC 6 `operator_new`. We use
// HeapAlloc(GetProcessHeap()) for the replacement buffer. Sacred's resource
// manager never frees this buffer for the lifetime of the process, so
// allocator mismatch is moot — but if a future code path did free it via
// operator delete, that goes through the same GetProcessHeap() backing on
// MSVC 6 / Windows CRT and HeapFree-after-HeapAlloc is the canonical
// compatible pair.

constexpr DWORD FUN_0080E680_RVA = 0x0080E680 - 0x00400000;

struct ResourceOut {
    void* data;
    DWORD size;
};

// Locate "scripts/<lang>/global.res" relative to Sacred.exe's directory.
// `<lang>` comes from Settings.cfg `LANGUAGE :` line (us/ru/de/...), falling
// back to "us". Returns true on success.
static bool resolve_globalres_path(char out[MAX_PATH]) {
    char exe_dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(g_attach.exe_module, exe_dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    // strip exe filename
    for (DWORD i = n; i--; ) {
        if (exe_dir[i] == '\\' || exe_dir[i] == '/') { exe_dir[i] = 0; break; }
    }
    // read LANGUAGE from Settings.cfg
    char lang[16] = "us";
    char settings_path[MAX_PATH];
    _snprintf_s(settings_path, _TRUNCATE, "%s\\Settings.cfg", exe_dir);
    FILE* sf = nullptr;
    fopen_s(&sf, settings_path, "rb");
    if (sf) {
        char line[256];
        while (fgets(line, sizeof(line), sf)) {
            const char* k = "LANGUAGE";
            if (_strnicmp(line, k, 8) == 0) {
                const char* p = line + 8;
                while (*p == ' ' || *p == '\t' || *p == ':') p++;
                int j = 0;
                while (*p && *p != '\r' && *p != '\n' && *p != ' '
                       && *p != '\t' && j < 15) {
                    lang[j++] = *p++;
                }
                lang[j] = 0;
                break;
            }
        }
        fclose(sf);
    }
    // Prefer custom/ override if a modder dropped a replacement there.
    char custom_path[MAX_PATH];
    _snprintf_s(custom_path, _TRUNCATE,
                "%s\\custom\\scripts\\%s\\global.res", exe_dir, lang);
    DWORD attr = GetFileAttributesA(custom_path);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        strncpy_s(out, MAX_PATH, custom_path, _TRUNCATE);
        return true;
    }
    _snprintf_s(out, MAX_PATH, _TRUNCATE,
                "%s\\scripts\\%s\\global.res", exe_dir, lang);
    return true;
}

// __fastcall = ECX, EDX, rest on stack. The original __thiscall has only ECX
// and no stack args, so we ignore EDX. Return in EAX as both ABIs prescribe.
static int __fastcall hook_FUN_0080e680(ResourceOut* out, void* /*edx*/) {
    if (!out) return 0;

    char path[MAX_PATH];
    if (!resolve_globalres_path(path)) {
        set_status("patch1: resolve_globalres_path failed");
        out->data = nullptr; out->size = 0;
        return 0;
    }

    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        set_status("patch1: CreateFile('%s') err=%lu — fail", path, GetLastError());
        out->data = nullptr; out->size = 0;
        return 0;
    }
    LARGE_INTEGER sz64; sz64.QuadPart = 0;
    if (!GetFileSizeEx(f, &sz64) || sz64.QuadPart < 4 || sz64.QuadPart > 0x40000000) {
        set_status("patch1: GetFileSizeEx out of bounds (%lld)", sz64.QuadPart);
        CloseHandle(f);
        out->data = nullptr; out->size = 0;
        return 0;
    }
    DWORD sz = (DWORD)sz64.QuadPart;
    void* buf = HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) {
        set_status("patch1: HeapAlloc(%lu) failed", sz);
        CloseHandle(f);
        out->data = nullptr; out->size = 0;
        return 0;
    }
    DWORD read = 0;
    if (!ReadFile(f, buf, sz, &read, nullptr) || read != sz) {
        set_status("patch1: ReadFile(%lu) read=%lu err=%lu",
                   sz, read, GetLastError());
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(f);
        out->data = nullptr; out->size = 0;
        return 0;
    }
    CloseHandle(f);

    out->data = buf;
    out->size = sz;
    set_status("patch1: '%s' -> %lu bytes @ %p", path, sz, buf);
    return 1;
}

// ---------------------------------------------------------------------------
//  Detour helper: write `jmp rel32` at target → replacement.
// ---------------------------------------------------------------------------
static bool install_jmp_detour(uintptr_t target, const void* replacement) {
    constexpr size_t LEN = 5;
    DWORD old;
    if (!VirtualProtect((void*)target, LEN, PAGE_EXECUTE_READWRITE, &old)) {
        sdk_log("[patches] VirtualProtect(%p,RWX) failed: %lu",
                (void*)target, GetLastError());
        return false;
    }
    uint8_t* p = (uint8_t*)target;
    p[0] = 0xE9;
    int32_t rel = (int32_t)((intptr_t)replacement - (intptr_t)(target + LEN));
    memcpy(p + 1, &rel, 4);
    DWORD dummy;
    VirtualProtect((void*)target, LEN, old, &dummy);
    FlushInstructionCache(GetCurrentProcess(), (void*)target, LEN);
    return true;
}

// ---------------------------------------------------------------------------
//  Patch 6: neutralize FUN_00811440 (force-foreground busy-wait)
// ---------------------------------------------------------------------------
//
// Located via re/ghidra/FindPatch6Site.java on our 2006-10-13 Steam build.
// FUN_00811440 is a busy-wait loop that hammers SetForegroundWindow and
// SetFocus on its HWND argument until the window actually receives focus, or
// until a caller-supplied timeout elapses. If the timeout argument equals
// `DAT_00a1ccc4` (a sentinel meaning "infinite"), the function loops forever
// — and this is exactly the freeze 2007 Thorium documented as
// "Debuggerfreez". Under a debugger SetForegroundWindow legitimately fails,
// so the inner loops never converge.
//
// 2.29 patches 2 bytes at va:0x00810B0F. Our build's layout puts the same
// behavior in FUN_00811440 (the two cmp/jmp inside the while-loops may be
// the exact 2 bytes), but the safest neutralization is whole-function: make
// it return `false` immediately. Sacred treats that as "couldn't acquire
// focus, that's fine".
//
// Calling convention auto-detect: scan forward from the entry for the first
// `RET` (0xC3) or `RET imm16` (0xC2 nn nn) and emit the same epilogue. MSVC 6
// defaults to __cdecl for free functions, so we expect 0xC3.

constexpr DWORD FUN_00811440_RVA = 0x00811440 - 0x00400000;

static bool try_install_patch6() {
    uintptr_t base = (uintptr_t)g_attach.exe_module;
    uintptr_t target = base + FUN_00811440_RVA;

    // 0) Sanity: function must start with `51 53 55 8b` (push ecx; push ebx;
    //    push ebp; mov ebp,...). If we see anything else, the function may be
    //    still encrypted (we ran before .bind finished) or layout has shifted;
    //    abort rather than corrupt the binary.
    uint8_t* code = (uint8_t*)target;
    if (code[0] != 0x51 || code[1] != 0x53 || code[2] != 0x55 || code[3] != 0x8B) {
        set_status("patch6: unexpected prologue %02x %02x %02x %02x — abort "
                   "(decryption not complete?)",
                   code[0], code[1], code[2], code[3]);
        return false;
    }

    // 1) Auto-detect calling convention.
    int    ret_kind  = -1;   // 0 = ret (C3), 1 = ret nn (C2 nn nn)
    uint16_t ret_imm = 0;
    for (int i = 0; i < 600; i++) {
        uint8_t b = code[i];
        if (b == 0xC3) { ret_kind = 0; break; }
        if (b == 0xC2) {
            ret_kind = 1;
            ret_imm = (uint16_t)(code[i+1] | (code[i+2] << 8));
            break;
        }
    }
    if (ret_kind < 0) {
        sdk_log("[patch6] no RET found within 600 bytes of %p — abort", (void*)target);
        return false;
    }

    // 2) Build the replacement byte stream.
    //    *** BISECT 17:30 *** Empirical finding: original FUN_00811440's
    //    happy-path returns AL = bl = 1 (success/got-focus). When we returned
    //    eax=0, callers took the "couldn't acquire focus" degraded path which
    //    leads to an access violation at 0x0064537c (`rep movsd` on bad esi).
    //    So make the neutralized function return TRUE instead.
    //
    //    6-byte stub:  xor eax,eax ; inc eax ; ret <imm16>  (or  ret  for cdecl)
    uint8_t stub[6];
    size_t  stub_len;
    stub[0] = 0x31;            // xor eax, eax  (clears full eax)
    stub[1] = 0xC0;
    stub[2] = 0x40;            // inc eax       (eax = 1)
    if (ret_kind == 0) {
        stub[3] = 0xC3;        // ret  (cdecl)
        stub_len = 4;
    } else {
        stub[3] = 0xC2;        // ret  imm16  (stdcall)
        stub[4] = (uint8_t)(ret_imm & 0xff);
        stub[5] = (uint8_t)(ret_imm >> 8);
        stub_len = 6;
    }

    // 3) Apply.
    DWORD old;
    if (!VirtualProtect((void*)target, 8, PAGE_EXECUTE_READWRITE, &old)) {
        sdk_log("[patch6] VirtualProtect failed: %lu", GetLastError());
        return false;
    }
    // Save original first bytes for debugging.
    uint8_t orig[8];
    memcpy(orig, code, 8);
    memcpy(code, stub, stub_len);
    DWORD dummy;
    VirtualProtect((void*)target, 8, old, &dummy);
    FlushInstructionCache(GetCurrentProcess(), (void*)target, 8);

    set_status("patch6: FUN_00811440 neutralized as %s (imm16=0x%x, returns TRUE); was %02x %02x %02x %02x...",
               ret_kind == 0 ? "cdecl(ret)" : "stdcall(ret imm16)",
               (unsigned)ret_imm,
               orig[0], orig[1], orig[2], orig[3]);
    return true;
}

// ---------------------------------------------------------------------------
//  Glow-off: neutralize FUN_00408b70 (bound/charmed-creature model swirl)
// ---------------------------------------------------------------------------
//
// RE source of truth: .claude/knowledge/re/quest_storyline.md
//   "## Captain glow (definitive) ... (2026-05-16)".
//
// The red "vampire-raise" aura on dialog-bound story NPCs (Captain Miles)
// is the additive billboard swirl FUN_00408b70 @0x00408B70, the model
// renderer's bound/charmed emitter. It is gated solely by
// cCreature+0x14 & 0x80000 — a bit the engine's own DlgNPC bind handlers
// (FUN_0048f9e0 / FUN_00463240) set on every dialog bind. Marker + name +
// HP bar + invuln + the legit ally/thrall FX are ALL separate functions;
// FUN_00408b70 has exactly one caller (0x0044B942) and is *only ever* the
// swirl. So neutralising it at its entry removes the glow and nothing
// else. (Zeroing +0xbc/+0xc0 does NOT work — it still draws an untextured
// additive column; that earlier conclusion was overturned.)
//
// Convention: disasm-verified from sdk/Sacred_decrypted.exe (capstone).
// FUN_00408b70 has exactly ONE epilogue — `0x004090BE: ret 0x14`
// (bytes C2 14 00) = __thiscall, 5 stack dwords callee-cleans (the
// agent's predicted `ret 0x10` was off by one arg; a naive
// forward-scan-for-first-C2 (patch6's trick) hits a spurious 0xC2
// operand byte here → `ret 0x4589`, stack-shredding). So we DON'T scan
// and we DON'T trust the prediction: we write the exact verified
// epilogue `C2 14 00` at the prologue, guarded by a prologue check.

constexpr DWORD FUN_00408B70_RVA = 0x00408B70 - 0x00400000;

static bool try_install_glow_off() {
    if (!g_glow_off_enabled) {
        set_status("glow_off: disabled by config — skip");
        return false;
    }
    uintptr_t base   = (uintptr_t)g_attach.exe_module;
    uintptr_t target = base + FUN_00408B70_RVA;
    uint8_t*  code   = (uint8_t*)target;

    // Sanity: prologue must be `55 8B EC 81 EC 94 01 00 00`
    // (push ebp; mov ebp,esp; sub esp,0x194). Anything else → still
    // encrypted (ran before .bind) or layout shifted; abort, don't corrupt.
    static const uint8_t expect[] = {0x55,0x8B,0xEC,0x81,0xEC,0x94,0x01,0x00,0x00};
    for (size_t i = 0; i < sizeof(expect); i++) {
        if (code[i] != expect[i]) {
            set_status("glow_off: unexpected prologue %02x %02x %02x %02x"
                       " (want 55 8B EC 81) — abort", code[0],code[1],code[2],code[3]);
            return false;
        }
    }

    // Already stubbed?
    if (code[0] == 0xC3 || code[0] == 0xC2) {
        set_status("glow_off: already stubbed (%02x) — skip", code[0]);
        return false;
    }

    // Disasm-verified epilogue: 0x004090BE `ret 0x14` (C2 14 00).
    const uint8_t stub[3] = {0xC2, 0x14, 0x00};
    const size_t  stub_len = 3;

    DWORD old;
    if (!VirtualProtect((void*)target, 4, PAGE_EXECUTE_READWRITE, &old)) {
        sdk_log("[glow_off] VirtualProtect failed: %lu", GetLastError());
        return false;
    }
    uint8_t orig[4]; memcpy(orig, code, 4);
    memcpy(code, stub, stub_len);
    DWORD dummy;
    VirtualProtect((void*)target, 4, old, &dummy);
    FlushInstructionCache(GetCurrentProcess(), (void*)target, 4);

    set_status("glow_off: FUN_00408b70 stubbed (ret 0x14); was "
               "%02x %02x %02x %02x", orig[0],orig[1],orig[2],orig[3]);
    return true;
}

// ---------------------------------------------------------------------------
//  Public install entry point.
// ---------------------------------------------------------------------------
void install() {
    if (!g_attach.exe_module) {
        set_status("install: no exe module captured yet — abort");
        return;
    }
    uintptr_t base = (uintptr_t)g_attach.exe_module;

    // Patch 1
    {
        uintptr_t target = base + FUN_0080E680_RVA;
        uint8_t first = *(uint8_t*)target;
        if (first == 0xE9) {
            set_status("patch1: target already detoured (0xE9 at entry) — skip");
        } else if (first != 0x6A) {
            set_status("patch1: unexpected first byte 0x%02x at %p — abort patch1",
                       first, (void*)target);
        } else if (install_jmp_detour(target, (void*)&hook_FUN_0080e680)) {
            g_patch1_active = true;
            set_status("patch1: detour installed @ %p -> %p",
                       (void*)target, (void*)&hook_FUN_0080e680);
        } else {
            set_status("patch1: detour install failed");
        }
    }

    // Patch 6
    if (try_install_patch6()) {
        g_patch6_active = true;
    }

    // Glow-off (bound/charmed swirl on dialog-bound story NPCs)
    if (try_install_glow_off()) {
        g_glow_off_active = true;
    }
}

}} // namespace sdk::patches
