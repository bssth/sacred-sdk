// hooks/detour.cpp — the unified trampoline-detour installer. (Goal A2)
#include "../sdk.h"
#include "detour.h"
#include <cstring>

namespace sdk { namespace hooks {

bool install_trampoline(uintptr_t target_va, size_t prologue_len, void* thunk,
                        uint8_t** tramp_out, const uint8_t* sig, size_t sig_len,
                        const char* tag)
{
    uint8_t* code = (uint8_t*)target_va;

    // Optional prologue signature check (guards un-decrypted / wrong code).
    for (size_t i = 0; i < sig_len; i++) {
        if (code[i] != sig[i]) {
            sdk_log("[%s] unexpected prologue @ %p byte%zu=%02x (expected %02x) "
                    "— aborting hook", tag, (void*)target_va, i, code[i], sig[i]);
            return false;
        }
    }

    // Trampoline: [prologue_len original bytes] + [E9 rel32 -> target+prologue_len].
    uint8_t* tcode = (uint8_t*)VirtualAlloc(
        nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tcode) {
        sdk_log("[%s] trampoline VirtualAlloc failed: %lu", tag, GetLastError());
        return false;
    }
    memcpy(tcode, (void*)target_va, prologue_len);
    tcode[prologue_len] = 0xE9;
    int32_t trel = (int32_t)((uintptr_t)(target_va + prologue_len)
                             - ((uintptr_t)tcode + prologue_len + 5));
    memcpy(tcode + prologue_len + 1, &trel, 4);
    FlushInstructionCache(GetCurrentProcess(), tcode, 32);
    *tramp_out = tcode;

    // Patch original: E9 rel32 -> thunk, NOP-pad to prologue_len.
    DWORD old;
    if (!VirtualProtect((void*)target_va, prologue_len + 1, PAGE_EXECUTE_READWRITE, &old)) {
        sdk_log("[%s] VirtualProtect failed @ %p: %lu", tag, (void*)target_va, GetLastError());
        return false;
    }
    code[0] = 0xE9;
    int32_t hrel = (int32_t)((uintptr_t)thunk - (target_va + 5));
    memcpy(code + 1, &hrel, 4);
    for (size_t i = 5; i < prologue_len; i++) code[i] = 0x90;
    DWORD dummy;
    VirtualProtect((void*)target_va, prologue_len + 1, old, &dummy);
    FlushInstructionCache(GetCurrentProcess(), (void*)target_va, prologue_len + 1);

    sdk_log("[%s] hook live @ %p (trampoline=%p, thunk=%p, len=%zu)",
            tag, (void*)target_va, tcode, thunk, prologue_len);
    return true;
}

}} // namespace sdk::hooks
