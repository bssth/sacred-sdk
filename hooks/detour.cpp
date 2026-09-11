// hooks/detour.cpp — the unified trampoline-detour installer. (Goal A2)
#include "../sdk.h"
#include "detour.h"
#include <cstring>

namespace sdk { namespace hooks {

bool install_trampoline(uintptr_t target_va, size_t prologue_len, void* thunk,
                        uint8_t** tramp_out, const uint8_t* sig, size_t sig_len,
                        const char* tag, uint8_t* orig_out)
{
    uint8_t* code = (uint8_t*)target_va;

    // An E9 rel32 needs 5 bytes; anything shorter cannot be detoured this way.
    // The upper bound is kMaxPrologue (detour.h), which callers size their
    // orig_out buffers against.
    if (prologue_len < 5 || prologue_len > kMaxPrologue) {
        sdk_log("[%s] bad prologue_len %zu @ %p (must be 5..%zu) — aborting hook",
                tag, prologue_len, (void*)target_va, (size_t)kMaxPrologue);
        return false;
    }

    // Optional prologue signature check (guards un-decrypted / wrong code).
    for (size_t i = 0; i < sig_len; i++) {
        if (code[i] != sig[i]) {
            sdk_log("[%s] unexpected prologue @ %p byte%zu=%02x (expected %02x) "
                    "— aborting hook", tag, (void*)target_va, i, code[i], sig[i]);
            return false;
        }
    }

    // Trampoline: [prologue_len original bytes] + [E9 rel32 -> target+prologue_len].
    // Size it from prologue_len instead of assuming the old hardcoded 32; the
    // allocation is page-granular anyway, but the arithmetic must be honest so a
    // longer prologue can never write past the buffer we reasoned about.
    const size_t tramp_size = prologue_len + 5;
    uint8_t* tcode = (uint8_t*)VirtualAlloc(
        nullptr, tramp_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tcode) {
        sdk_log("[%s] trampoline VirtualAlloc failed: %lu", tag, GetLastError());
        return false;
    }
    memcpy(tcode, (void*)target_va, prologue_len);
    tcode[prologue_len] = 0xE9;
    int32_t trel = (int32_t)((uintptr_t)(target_va + prologue_len)
                             - ((uintptr_t)tcode + prologue_len + 5));
    memcpy(tcode + prologue_len + 1, &trel, 4);
    FlushInstructionCache(GetCurrentProcess(), tcode, tramp_size);

    // Patch original: E9 rel32 -> thunk, NOP-pad to prologue_len. We write bytes
    // [0, prologue_len), so that is exactly the range to unprotect and flush —
    // the old code used prologue_len + 1 and touched one byte it never wrote.
    DWORD old;
    if (!VirtualProtect((void*)target_va, prologue_len, PAGE_EXECUTE_READWRITE, &old)) {
        sdk_log("[%s] VirtualProtect failed @ %p: %lu — aborting hook",
                tag, (void*)target_va, GetLastError());
        // Release the trampoline and leave *tramp_out untouched. The old code
        // published the pointer before this check, so a caller could jump into a
        // trampoline whose target was never detoured, and the block leaked.
        VirtualFree(tcode, 0, MEM_RELEASE);
        return false;
    }

    // Hand the caller the bytes we are about to destroy, so a revert is possible.
    if (orig_out) memcpy(orig_out, code, prologue_len);

    code[0] = 0xE9;
    int32_t hrel = (int32_t)((uintptr_t)thunk - (target_va + 5));
    memcpy(code + 1, &hrel, 4);
    for (size_t i = 5; i < prologue_len; i++) code[i] = 0x90;
    DWORD dummy;
    VirtualProtect((void*)target_va, prologue_len, old, &dummy);
    FlushInstructionCache(GetCurrentProcess(), (void*)target_va, prologue_len);

    *tramp_out = tcode;
    sdk_log("[%s] hook live @ %p (trampoline=%p, thunk=%p, len=%zu)",
            tag, (void*)target_va, tcode, thunk, prologue_len);
    return true;
}

}} // namespace sdk::hooks
