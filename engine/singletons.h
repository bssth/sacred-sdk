// engine/singletons.h — the three engine singleton roots, ONE canonical form.
// (Goal A3)
//
// Kills the rebased-vs-raw footgun: some call sites used `reb + 0x00AACF80`,
// others a bare `*(uintptr_t*)0x00AACF80` (which silently assumes no-ASLR).
// These accessors take the rebase delta `reb` (= module_base - 0x00400000, 0
// under Sacred's no-ASLR) and encode the address-of-vs-deref semantics so a
// caller can never get them wrong.
#pragma once
#include "addresses.h"
#include "mem.h"

namespace sdk { namespace engine { namespace singletons {

// cQuestMgr — a STATIC object located AT addr::QM. Returns its ADDRESS; qm IS
// the object, NOT a pointer, so callers do `*(qm + field)`, never `*qm`. This
// is the ECX of every quest/dialog record handler.
inline uintptr_t qm(uintptr_t reb) { return reb + addr::QM; }

// cObjectManager — addr::OM holds a POINTER to it. Returns the object pointer
// (0 if unreadable / not yet live). Creature table: arr = *(om + 4).
inline uintptr_t om(uintptr_t reb) {
    uintptr_t p = 0; mem::read_ptr(reb + addr::OM, &p); return p;
}

// Active-context singleton — addr::CTX holds a POINTER to it (0 if not live).
// Hero slot = *(ctx + 0x14).
inline uintptr_t ctx(uintptr_t reb) {
    uintptr_t p = 0; mem::read_ptr(reb + addr::CTX, &p); return p;
}

}}} // namespace sdk::engine::singletons
