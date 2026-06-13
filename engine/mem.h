// engine/mem.h — SEH-safe memory accessors + rebase helper. (Goal A1)
//
// Header-only inline. The canonical home for the safe_read/safe_write pattern
// that was duplicated as file-local statics (player_state.cpp) and ad-hoc
// __try blocks all over runtime_triggers.cpp. Every pointer-chain walk into
// engine memory must go through these so a torn-down chain (menu/loading/dead
// creature) returns false instead of crashing the game.
//
// Migration note: existing files keep thin `static` forwarders to these so call
// sites are unchanged while the refactor proceeds (see plan A1/A4).
#pragma once
#include <cstdint>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sdk { namespace engine { namespace mem {

// Module rebase delta: add to a full VA from engine::addr to get the live
// address. base = the hooked module's load base (g_attach.exe_module). With no
// ASLR this is 0x00400000 and reb is 0, but compute it so we never assume.
inline uintptr_t rebase(HMODULE base) {
    return reinterpret_cast<uintptr_t>(base) - 0x00400000u;
}

// Resolve a full engine VA to its live address for the given module base.
inline uintptr_t va(HMODULE base, uintptr_t full_va) {
    return rebase(base) + full_va;
}

// Impl matches player_state.cpp's original statics verbatim (null + IsBadRdPtr)
// so forwarding to these is byte-identical behavior (Goal A1 nil-risk). The
// IsBadReadPtr→__try unification is a deliberate LATER step (the runtime_triggers
// __try blocks use a different idiom); do NOT change it as part of A1.
inline bool read_ptr(uintptr_t addr, uintptr_t* out) {
    if (!addr || IsBadReadPtr((void*)addr, sizeof(uintptr_t))) return false;
    *out = *reinterpret_cast<uintptr_t*>(addr);
    return true;
}

template <class T>
inline bool read(uintptr_t addr, T* out) {
    if (!addr || IsBadReadPtr((void*)addr, sizeof(T))) return false;
    *out = *reinterpret_cast<T*>(addr);
    return true;
}

template <class T>
inline bool write(uintptr_t addr, T val) {
    if (!addr || IsBadWritePtr((void*)addr, sizeof(T))) return false;
    *reinterpret_cast<T*>(addr) = val;
    return true;
}

}}} // namespace sdk::engine::mem
