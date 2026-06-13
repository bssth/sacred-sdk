// hooks/detour.h — the ONE trampoline-detour installer. (Goal A2)
//
// Unifies the 3 byte-identical hand-rolled copies that lived in
// runtime_triggers.cpp (install_hook_sig/build_trampoline/patch_jump),
// text_logger.cpp (install_trampoline_detour), and sacred_log_mirror.cpp.
// They differed ONLY in prologue length (7/7/6), NOP-pad count (2/2/1), and
// sig-check length (3/0/2) — all parameters here. Naked thunks + g_tramp_*
// cells stay in their own TUs; this just installs the redirect.
#pragma once
#include <cstdint>
#include <cstddef>

namespace sdk { namespace hooks {

// Install a classic trampoline detour at `target_va`:
//   * save `prologue_len` (>=5, must cover WHOLE instructions) original bytes
//     into a fresh RWX trampoline that runs them then `jmp target+prologue_len`,
//   * overwrite the target prologue with `E9 rel32 -> thunk`, NOP-padded to
//     `prologue_len`.
// If `sig_len` (0..N) > 0, the first `sig_len` bytes are validated against
// `sig` BEFORE patching (guards still-encrypted / relocated / wrong code).
// Returns the trampoline pointer via `*tramp_out`. `tag` prefixes log lines.
// Emits byte-for-byte the same patch + trampoline the old copies did.
bool install_trampoline(uintptr_t target_va, size_t prologue_len, void* thunk,
                        uint8_t** tramp_out, const uint8_t* sig, size_t sig_len,
                        const char* tag);

}} // namespace sdk::hooks
