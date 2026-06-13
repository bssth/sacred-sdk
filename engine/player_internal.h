// engine/player_internal.h — shared internals for the player_state split.
// (Goal A4) The cQuestMgr/cObjectManager player ops used to live in one 1644-L
// player_state.cpp; they are being split into engine/{hero,creature,dialog}.cpp.
// The only cross-cutting dependency is the SEH-safe accessor trio (thin
// forwarders to engine::mem), so it lives here for all the split TUs to share.
#pragma once
#include "mem.h"

namespace sdk { namespace player {

inline bool safe_read_ptr(uintptr_t addr, uintptr_t* out) {
    return engine::mem::read_ptr(addr, out);
}
template <typename T>
inline bool safe_read(uintptr_t addr, T* out) {
    return engine::mem::read<T>(addr, out);
}
template <typename T>
inline bool safe_write(uintptr_t addr, T val) {
    return engine::mem::write<T>(addr, val);
}

}} // namespace sdk::player
