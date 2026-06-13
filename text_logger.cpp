// SacredSDK — live resource-lookup logger.
//
// Hooks FUN_0080eaf0 (va:0x0080EAF0) — the dictionary lookup that every
// `resource(id)` / `resource("NAME")` call funnels through after hashing.
// We log every unique hash queried + the result pointer (NULL on miss).
//
// Combined with our recovered hash function (sacred_hash.py) and the partial
// hash_names.csv, this gives us full live coverage of every text id the
// game actually uses, dramatically narrowing the search for un-named hashes.
//
// Implementation: classic trampoline detour.
//   1. Allocate executable memory for the trampoline.
//   2. Copy original first N (>=5) prologue bytes there + append a jmp back.
//   3. Overwrite original first 5 bytes with `jmp our_hook`.
//   4. Our hook calls trampoline (which runs the saved prologue then jumps
//      past the detour into the rest of the original).
//
// FUN_0080eaf0's prologue is the MSVC SEH-frame setup:
//     6A FF                push -1
//     68 10 A7 88 00       push 0x0088A710
//     64 A1 00 00 00 00    mov  eax, fs:[0]
//     50                   push eax
//     ...
// First 7 bytes (`6A FF 68 10 A7 88 00`) form two complete instructions, both
// pure stack pushes, position-independent. Save 7 bytes → trampoline.

#include "sdk.h"
#include "hooks/detour.h"   // Goal A2: unified trampoline-detour installer
#include <cstring>
#include <unordered_set>
#include <cstdio>

namespace sdk { namespace text_logger {

volatile bool g_active = false;
volatile long g_calls  = 0;
volatile long g_unique = 0;

// FUN_0080eaf0 prologue is __thiscall:
//   ECX = "this" pointer to the resource-manager singleton
//   [esp+4] = uint32_t hash key (already masked with 0x7FFFFFFF by caller)
// Returns entry pointer in EAX (NULL on miss). Stack cleanup: `ret 4`.
typedef void* (__fastcall *lookup_fn_t)(void* this_ptr, void* edx_dummy,
                                        uint32_t hash);

constexpr uintptr_t FUN_0080EAF0_RVA = 0x0080EAF0 - 0x00400000;
constexpr size_t    PROLOGUE_BYTES   = 7;  // 2 + 5: two pushes

static lookup_fn_t g_trampoline = nullptr;

// Dedup-set of hashes we've already recorded. Guarded by g_cs.
static CRITICAL_SECTION g_cs;
static bool g_cs_init = false;
static std::unordered_set<uint32_t>* g_seen = nullptr;
// Pending hashes accumulated since the last flush. Owned under g_cs.
static std::unordered_set<uint32_t>* g_pending = nullptr;

// The hook proper.
static void* __fastcall hook_FUN_0080eaf0(void* this_ptr, void* edx, uint32_t hash) {
    void* result = g_trampoline(this_ptr, edx, hash);

    // Record. Keep fast — single CS, only one set lookup + insert.
    InterlockedIncrement(&g_calls);
    if (g_cs_init && g_seen) {
        EnterCriticalSection(&g_cs);
        // emplace into the seen-set; if it was new also push into pending.
        auto pr = g_seen->insert(hash);
        if (pr.second) {
            g_pending->insert(hash);
            InterlockedIncrement(&g_unique);
        }
        LeaveCriticalSection(&g_cs);
    }
    return result;
}

// --------- Trampoline allocation + detour install -------------------------

static bool install_trampoline_detour(uintptr_t target, void* hook) {
    // Unified installer (hooks/detour.cpp). 7-byte prologue, no sig check
    // (the .text-decrypt gate already ran before this). Emitted bytes
    // unchanged from the old hand-rolled copy. Goal A2.
    return hooks::install_trampoline(target, PROLOGUE_BYTES, hook,
                                     (uint8_t**)&g_trampoline, nullptr, 0,
                                     "text_logger");
}

// --------- Periodic flush worker ------------------------------------------
//
// Drains g_pending under the CS every N ms and appends to seen_hashes.csv.

static DWORD WINAPI flush_worker(LPVOID) {
    // Resolve path: <gamedir>\sdk\logs\seen_hashes.csv
    char path[MAX_PATH];
    {
        char exe[MAX_PATH] = {0};
        GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
        char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
        char dir[MAX_PATH];
        _snprintf_s(dir, _TRUNCATE, "%s\\sdk\\logs", exe);
        CreateDirectoryA(dir, nullptr);
        _snprintf_s(path, _TRUNCATE, "%s\\seen_hashes.csv", dir);
    }
    // Write header once.
    {
        FILE* f = nullptr;
        fopen_s(&f, path, "ab");
        if (f) {
            fseek(f, 0, SEEK_END);
            if (ftell(f) == 0) fputs("hash\n", f);
            fclose(f);
        }
    }

    while (g_active) {
        Sleep(5000);   // flush every 5 s
        if (!g_cs_init) continue;

        // Drain pending into a local snapshot under the CS.
        std::unordered_set<uint32_t> snap;
        EnterCriticalSection(&g_cs);
        if (g_pending && !g_pending->empty()) {
            snap.swap(*g_pending);
        }
        LeaveCriticalSection(&g_cs);

        if (snap.empty()) continue;

        FILE* f = nullptr;
        fopen_s(&f, path, "ab");
        if (!f) continue;
        for (uint32_t h : snap) {
            fprintf(f, "%u\n", h);
        }
        fclose(f);
    }
    return 0;
}

// --------- Public install --------------------------------------------------

void install() {
    if (g_active) return;
    if (!g_attach.exe_module) {
        sdk_log("[text_logger] no exe module captured — abort");
        return;
    }
    uintptr_t base = (uintptr_t)g_attach.exe_module;
    uintptr_t target = base + FUN_0080EAF0_RVA;

    // Verify prologue: expect `6A FF 68` at entry. If not, .text might not
    // be decrypted, or the function moved in this build.
    uint8_t* code = (uint8_t*)target;
    if (code[0] != 0x6A || code[1] != 0xFF || code[2] != 0x68) {
        sdk_log("[text_logger] unexpected prologue %02x %02x %02x %02x at %p — abort",
                code[0], code[1], code[2], code[3], (void*)target);
        return;
    }

    InitializeCriticalSection(&g_cs);
    g_cs_init = true;
    g_seen    = new std::unordered_set<uint32_t>();
    g_pending = new std::unordered_set<uint32_t>();

    if (!install_trampoline_detour(target, (void*)&hook_FUN_0080eaf0)) {
        sdk_log("[text_logger] detour install failed");
        return;
    }

    g_active = true;
    sdk_log("[text_logger] hook live @ %p (trampoline=%p, hook=%p)",
            (void*)target, g_trampoline, (void*)&hook_FUN_0080eaf0);

    CreateThread(nullptr, 0, flush_worker, nullptr, 0, nullptr);
}

}} // namespace sdk::text_logger
