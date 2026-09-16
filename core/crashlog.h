// core/crashlog.h — write what the engine was doing when it faults.
//
// Windows' own report gives one address and nothing else, and a fault inside a
// shared helper (a string hash, an allocator) says nothing about who called it.
// This logs the registers and a scan of the stack for return addresses into
// Sacred.exe or into our patch cave, each tagged with the patch record that owns
// the address when there is one — which is exactly the question while porting
// patches: "is this ours?"
//
// It is a vectored handler that only OBSERVES: it never handles an exception, so
// the engine's own __try blocks and Windows' crash path behave as before. The
// first few distinct faulting addresses are logged; repeats are not.
#pragma once

namespace sdk { namespace crashlog {

// `[sdk] crash_log` (default 1). Safe to call from DllMain. Idempotent.
void install();

}} // namespace sdk::crashlog
