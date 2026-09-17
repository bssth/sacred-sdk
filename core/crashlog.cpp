// core/crashlog.cpp — see crashlog.h.
#include "../sdk.h"
#include "crashlog.h"
#include "config.h"
#include "../patchset/patchset.h"
#include <cstring>
#include <cstdio>

namespace sdk { namespace crashlog {

static constexpr int kMaxLogged = 8;
static uintptr_t    g_seen[kMaxLogged];
static volatile LONG g_nseen = 0;
static bool         g_installed = false;
static uintptr_t    g_text_lo = 0, g_text_hi = 0;

static bool in_text(uintptr_t va) { return va >= g_text_lo && va < g_text_hi; }

// Readable without faulting inside the handler itself.
static bool readable(uintptr_t va, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)va, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return va + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

// Name an address: patch record if ours, else image-relative.
static void describe(uintptr_t va, char* buf, size_t n) {
    char rec[96];
    if (patchset::describe_address(va, rec, sizeof(rec)))
        _snprintf_s(buf, n, _TRUNCATE, "%08x [%s]", (unsigned)va, rec);
    else if (in_text(va))
        _snprintf_s(buf, n, _TRUNCATE, "%08x", (unsigned)va);
    else
        _snprintf_s(buf, n, _TRUNCATE, "%08x (outside Sacred.exe)", (unsigned)va);
}

// A value on the stack is probably a return address if the bytes right before
// it are a call: E8 rel32 (5), FF /2 with modrm (2, 3 or 6 bytes, 7 with SIB+disp32).
static bool follows_call(uintptr_t ret) {
    if (!readable(ret - 7, 7)) return false;
    const uint8_t* p = (const uint8_t*)ret;
    if (p[-5] == 0xE8) return true;
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;
    if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10) return true;
    return false;
}

static bool is_ours_or_engine(uintptr_t va) {
    char tmp[8];
    return in_text(va) || patchset::describe_address(va, tmp, sizeof(tmp));
}

static LONG WINAPI veh(EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    const DWORD code = er->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;

    const CONTEXT* c = ep->ContextRecord;
    const uintptr_t eip = c->Eip;
    // Execution that left the code altogether (a jump to 0, a return into the
    // stack under DEP) is exactly the corruption a bad patch causes, and its
    // stack still says where it came from. Anything else outside our code is
    // someone else's business.
    const bool wild_exec = code == EXCEPTION_ACCESS_VIOLATION &&
                           er->ExceptionInformation[0] == 8 && !is_ours_or_engine(eip);
    if (!wild_exec && !is_ours_or_engine(eip)) return EXCEPTION_CONTINUE_SEARCH;

    for (LONG i = 0; i < g_nseen && i < kMaxLogged; ++i)
        if (g_seen[i] == eip) return EXCEPTION_CONTINUE_SEARCH;
    LONG slot = InterlockedIncrement(&g_nseen) - 1;
    if (slot >= kMaxLogged) return EXCEPTION_CONTINUE_SEARCH;
    g_seen[slot] = eip;

    char where[128];
    describe(eip, where, sizeof(where));
    sdk_log("[crash] first-chance %08lx at %s  (%s %08x)", code, where,
            code == EXCEPTION_ACCESS_VIOLATION
                ? (er->ExceptionInformation[0] == 1 ? "write" : er->ExceptionInformation[0] == 8 ? "exec" : "read")
                : "-",
            code == EXCEPTION_ACCESS_VIOLATION ? (unsigned)er->ExceptionInformation[1] : 0u);
    sdk_log("[crash]   eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
            c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);

    int found = 0;
    for (uintptr_t sp = c->Esp; sp < c->Esp + 0x800 && found < 16; sp += 4) {
        if (!readable(sp, 4)) break;
        const uintptr_t v = *(const uint32_t*)sp;
        if (!is_ours_or_engine(v) || !follows_call(v)) continue;
        char d[128];
        describe(v, d, sizeof(d));
        sdk_log("[crash]   [esp+%03x] ret %s", (unsigned)(sp - c->Esp), d);
        ++found;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void install() {
    if (g_installed) return;
    if (!config::get_bool("sdk", "crash_log", true)) return;
    HMODULE exe = g_attach.exe_module;
    if (!exe) return;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)exe;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)exe + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            g_text_lo = (uintptr_t)exe + sec->VirtualAddress;
            g_text_hi = g_text_lo + sec->Misc.VirtualSize;
        }
    }
    if (!g_text_lo) return;
    // Last in the chain: anything that handles the exception first never reaches us.
    if (AddVectoredExceptionHandler(0, veh)) {
        g_installed = true;
        sdk_log("[crash] vectored logger installed (.text %08x-%08x)",
                (unsigned)g_text_lo, (unsigned)g_text_hi);
    }
}

}} // namespace sdk::crashlog
