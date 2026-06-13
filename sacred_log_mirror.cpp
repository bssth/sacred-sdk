// SacredSDK — mirror Sacred's internal debug-log (FUN_0066ef40) into sdk_log.
//
// Sacred's FUN_0066ef40 is `int __cdecl debug_log(const char* fmt, ...);` —
// the variadic printer that EVERY internal subsystem uses. The compiler's
// parse-error messages ("parseStatement() line%d expected '%c' found '%s'"),
// the "compiling 'X' failed" message, the cScriptResource pool-fill notes,
// and a thousand other internal diagnostics ALL go through it.
//
// We trampoline-detour at the function entry, copy fmt+args off the stack,
// vsnprintf into a buffer, push that into sdk_log, then JMP-through to the
// real function so Sacred's own debug-window receives it unchanged.
//
// The hook is a naked function: it reads (without modifying) the caller's
// fmt + first few args from the stack, calls our logger, and tail-jumps to
// the trampoline that runs the original first 6 bytes + jumps back to
// FUN_0066ef40 + 6.
//
// Prologue (6 bytes) of FUN_0066ef40:
//   64 a1 00 00 00 00   mov eax, fs:[0]
// That's a single-instruction unit; we save those 6 bytes verbatim into our
// trampoline and write `JMP rel32` + 1 NOP over them.

#include "sdk.h"
#include "hooks/detour.h"   // Goal A2: unified trampoline-detour installer
#include <cstdarg>
#include <cstring>

namespace sdk { namespace sacred_log_mirror {

volatile bool g_active = false;
volatile long g_calls  = 0;

constexpr uintptr_t FUN_0066ef40_RVA = 0x0066EF40 - 0x00400000;
constexpr size_t    PROLOGUE_BYTES   = 6;        // mov eax, fs:[0]

static uint8_t* g_trampoline = nullptr;

// C-side formatter — called from the naked thunk with fmt and a SNAPSHOT
// of the first few stack args.
extern "C" void __cdecl mirror_format_and_log(const char* fmt,
                                               uint32_t a0, uint32_t a1,
                                               uint32_t a2, uint32_t a3)
{
    InterlockedIncrement(&g_calls);
    if (!fmt) return;

    // Safety: IsBadReadPtr on fmt — Sacred's `FUN_0066ef40` might be invoked
    // with weird arg patterns we mis-decoded.
    if (IsBadReadPtr(fmt, 1)) return;

    char buf[512];
    __try {
        struct { uint32_t v[4]; } pack = { {a0, a1, a2, a3} };
        _vsnprintf_s(buf, _TRUNCATE, fmt, (va_list)&pack);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    size_t n = strnlen(buf, sizeof(buf));
    while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
    if (n == 0) return;

    sdk_log("[sacred] %s", buf);
}

// Naked tail-call thunk.  Sacred CALLed FUN_0066ef40, landed here via our
// 5-byte JMP detour.  Stack on entry:
//
//   [esp + 0]  = original return address (back into Sacred)
//   [esp + 4]  = fmt
//   [esp + 8]  = a0
//   [esp + 12] = a1
//   [esp + 16] = a2
//   [esp + 20] = a3
//
// What we do:
//   1. Push fmt + 4 args (in reverse cdecl order) for mirror_format_and_log.
//      Each push shifts esp by -4, so the SOURCE offset stays a constant
//      [esp + 20] for the next arg.
//   2. Call mirror_format_and_log; cdecl caller cleans → `add esp, 20`.
//   3. Tail-jump to trampoline.  Trampoline runs original 6 prologue bytes
//      then `jmp FUN_0066ef40 + 6`.  Original code sees its own untouched
//      stack frame (ret_addr + args still at [esp+0]..[esp+20]) and proceeds.
//
// EAX/ECX/EDX get clobbered — that's fine because FUN_0066ef40's first
// instruction is `mov eax, fs:[0]` which overwrites EAX, and Sacred's call
// site already considers them caller-saved (cdecl).  We don't touch
// EBX/ESI/EDI/EBP/ESP (preserved by mirror_format_and_log per cdecl ABI).
__declspec(naked) static void __cdecl hook_FUN_0066ef40() {
    __asm {
        push dword ptr [esp + 20]   ; a3
        push dword ptr [esp + 20]   ; a2 (esp shifted -4, so source moved +4 → still [esp+20])
        push dword ptr [esp + 20]   ; a1
        push dword ptr [esp + 20]   ; a0
        push dword ptr [esp + 20]   ; fmt
        call mirror_format_and_log
        add  esp, 20                ; clean 5 cdecl args
        jmp  dword ptr [g_trampoline]
    }
}

void install() {
    if (g_active) return;
    if (!g_attach.exe_module) {
        sdk_log("[sacred_log_mirror] no exe module — abort");
        return;
    }
    uintptr_t base = (uintptr_t)g_attach.exe_module;
    uintptr_t target = base + FUN_0066ef40_RVA;

    // Unified installer (hooks/detour.cpp): verify the 6-byte `mov eax, fs:[0]`
    // prologue (64 A1 ...), 1-NOP pad. Emitted bytes unchanged. Goal A2.
    static const uint8_t sig[2] = { 0x64, 0xA1 };
    if (!hooks::install_trampoline(target, PROLOGUE_BYTES,
                                   (void*)&hook_FUN_0066ef40, &g_trampoline,
                                   sig, 2, "sacred_log_mirror")) {
        return;
    }
    g_active = true;
}

}} // namespace sdk::sacred_log_mirror
