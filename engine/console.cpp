// engine/console.cpp — see console.h.
#include "../sdk.h"
#include "console.h"
#include "build_profile.h"
#include "../core/config.h"
#include "../hooks/detour.h"
#include "../patchset/patchset.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>

namespace sdk { namespace engine { namespace console {

// Steam/GOG build. Each of these is a pin in build_profile.cpp, so install()
// only ever runs against bytes that were verified moments earlier.
static constexpr uintptr_t VA_DISPATCH      = 0x00615F30; // cEngine console dispatcher, thiscall, ret 4
static constexpr uintptr_t VA_UIEVENT_CTOR  = 0x00553210; // cEventUI2(code, wnd, sub, 0 x6), thiscall, ret 0x24
static constexpr uintptr_t VA_UIEVENT_DTOR  = 0x004B2410; // ~cEventUI2, thiscall
static constexpr uintptr_t VA_UISTRING_VTBL = 0x008911D8; // cEventUI_string vftable
static constexpr uintptr_t VA_KERNEL_GET    = 0x00808E50; // cKernel singleton (creates it on first use)
static constexpr uintptr_t VA_KERNEL_RECV   = 0x008092F0; // cKernel::receive_event(ev, 1, 0), ret 0xc

static constexpr int      UI_CODE_ADD_LINE = 0x26;        // the window appends +0x74 as a line
static constexpr size_t   EV_SIZE          = 0x174;
static constexpr size_t   EV_TEXT_OFS      = 0x74;
static constexpr size_t   EV_TEXT_LEN      = 0x100;

typedef void* (__thiscall* UiEventCtorFn)(void* self, int code, const char* wnd, const char* sub,
                                         int, int, int, int, int, int);
typedef void  (__thiscall* UiEventDtorFn)(void* self);
// The engine calls this with the event's three receive_event arguments still on
// the stack and reuses them for the next call; it reads none of them.
typedef void* (__cdecl*    KernelGetFn)(void* ev, int a1, int a2);
typedef void  (__thiscall* KernelRecvFn)(void* self, void* ev, int a1, int a2);

// Registered as __fastcall so MSVC gives us ecx = cEngine* and cleans the one
// stack argument, which is the thiscall/ret 4 contract of the original.
typedef char (__fastcall* DispatchFn)(void* engine, void* edx, const char* line);

static uint8_t* g_tramp     = nullptr;
static bool     g_installed = false;

bool installed() { return g_installed; }

// ---------------------------------------------------------------------------
//  Output
// ---------------------------------------------------------------------------
// SEH leaf: nothing in here needs unwinding. A fault inside the engine's event
// code must cost us one console line, not the game.
static bool post_line_seh(const char* text) {
    const uintptr_t reb = build::rebase();
    uint8_t ev[EV_SIZE];
    memset(ev, 0, sizeof(ev));
    __try {
        ((UiEventCtorFn)(reb + VA_UIEVENT_CTOR))(ev, UI_CODE_ADD_LINE, "UI_WND_CONSOLE", "", 0, 0, 0, 0, 0, 0);
        *(uintptr_t*)ev = reb + VA_UISTRING_VTBL;
        size_t n = strlen(text);
        if (n > EV_TEXT_LEN - 1) n = EV_TEXT_LEN - 1;
        memcpy(ev + EV_TEXT_OFS, text, n);
        ev[EV_TEXT_OFS + n] = 0;

        void* kernel = ((KernelGetFn)(reb + VA_KERNEL_GET))(ev, 1, 0);
        if (kernel) ((KernelRecvFn)(reb + VA_KERNEL_RECV))(kernel, ev, 1, 0);
        ((UiEventDtorFn)(reb + VA_UIEVENT_DTOR))(ev);
        return kernel != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool print(const char* text) {
    if (!g_installed || !text) return false;
    return post_line_seh(text);
}

bool print_f(const char* fmt, ...) {
    char buf[EV_TEXT_LEN];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    return print(buf);
}

// ---------------------------------------------------------------------------
//  Command table
// ---------------------------------------------------------------------------
struct Command {
    const char* name;
    const char* help;
    Handler     fn;
};
static constexpr int MAX_COMMANDS = 24;
static Command g_cmds[MAX_COMMANDS];
static int     g_ncmds = 0;

static const Command* find_command(const char* name, size_t len) {
    for (int i = 0; i < g_ncmds; ++i)
        if (strlen(g_cmds[i].name) == len && _strnicmp(g_cmds[i].name, name, len) == 0)
            return &g_cmds[i];
    return nullptr;
}

bool add(const char* name, const char* help, Handler fn) {
    if (!name || !*name || !fn || g_ncmds >= MAX_COMMANDS) return false;
    if (find_command(name, strlen(name))) return false;
    g_cmds[g_ncmds++] = Command{ name, help ? help : "", fn };
    return true;
}

static void run_handler_seh(Handler fn, const char* args, bool* faulted) {
    __try { fn(args); *faulted = false; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *faulted = true; }
}

static const char* skip_spaces(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

// Returns true if the line was an `sdk` command (handled, even if it failed).
static bool try_sdk_line(const char* line) {
    const char* p = skip_spaces(line);
    if (_strnicmp(p, "sdk", 3) != 0) return false;
    if (p[3] != 0 && p[3] != ' ' && p[3] != '\t') return false;   // "sdkfoo" is not ours

    p = skip_spaces(p + 3);
    const char* word = p;
    while (*p && *p != ' ' && *p != '\t') ++p;
    size_t wlen = (size_t)(p - word);
    const char* args = skip_spaces(p);

    sdk_log("[console] > %s", line);
    const Command* c = wlen ? find_command(word, wlen) : find_command("help", 4);
    if (!c) {
        print_f("sdk: unknown command '%.*s' - try: sdk help", (int)(wlen > 64 ? 64 : wlen), word);
        return true;
    }
    bool faulted = false;
    run_handler_seh(c->fn, args, &faulted);
    if (faulted) {
        sdk_log("[console] command '%s' faulted", c->name);
        print_f("sdk %s: faulted (see sdk_loaded.log)", c->name);
    }
    return true;
}

static char __fastcall hook_dispatch(void* engine, void* edx, const char* line) {
    if (line && try_sdk_line(line)) return 1;
    return ((DispatchFn)g_tramp)(engine, edx, line);
}

// ---------------------------------------------------------------------------
//  Built-in commands
// ---------------------------------------------------------------------------
static void cmd_help(const char*) {
    print("SacredSDK console commands:");
    for (int i = 0; i < g_ncmds; ++i)
        print_f("  sdk %-8s %s", g_cmds[i].name, g_cmds[i].help);
}

static void cmd_build(const char*) {
    const build::BuildProfile* bp = build::profile();
    const build::Fingerprint&  fp = build::fingerprint();
    print_f("build: %s %s", build::state_text(), bp ? bp->id : "(unknown)");
    if (fp.valid)
        print_f("pe ts=%08x image=%08x sum=%08x text=%08x",
                fp.timestamp, fp.size_of_image, fp.checksum, fp.text_vsize);
}

static const char* st_name(patchset::St st) {
    switch (st) {
        case patchset::St::Off:     return "off";
        case patchset::St::Applied: return "ON";
        case patchset::St::Skipped: return "skipped";
        case patchset::St::Failed:  return "FAILED";
    }
    return "?";
}

static void cmd_patches(const char*) {
    print_f("patchset: %d applied, %d skipped, %d failed",
            patchset::applied_count(), patchset::skipped_count(), patchset::failed_count());
    int shown = 0;
    const int n = patchset::record_count();
    for (int i = 0; i < n; ++i) {
        patchset::RecordInfo ri;
        if (!patchset::record_at(i, &ri)) continue;
        if (shown == 6) { print_f("  ... %d more in the overlay panel", n - i); break; }
        if (ri.detail && *ri.detail && ri.st != patchset::St::Applied)
            print_f("  %-7s %s (%s)", st_name(ri.st), ri.key, ri.detail);
        else
            print_f("  %-7s %s", st_name(ri.st), ri.key);
        ++shown;
    }
}

static void cmd_verify(const char*) {
    const patchset::VerifyResult v = patchset::verify_live();
    print_f("verify: %d original, %d patched, %d unexpected", v.original, v.patched, v.unexpected);
    if (v.unexpected) print("  unexpected sites are listed in sdk_loaded.log");
}

static void cmd_reload(const char*) {
    int n = config::reload();
    print_f("sdk.ini + Settings.cfg re-read: %d keys", n);
    print("  patch and hook toggles take effect on the next start");
}

// ---------------------------------------------------------------------------
//  Install
// ---------------------------------------------------------------------------
void install() {
    if (g_installed) return;
    if (!config::get_bool("console", "sdk_commands", true)) {
        sdk_log("[console] sdk_commands=0 - dispatcher left untouched");
        return;
    }
    if (!build::can_patch_text()) {
        sdk_log("[console] build profile is %s - not hooking the console", build::state_text());
        return;
    }

    add("help",    "this list",                            cmd_help);
    add("build",   "which Sacred.exe the SDK is running in", cmd_build);
    add("patches", "engine patch records and their state",  cmd_patches);
    add("verify",  "re-read every patch site from memory",  cmd_verify);
    add("reload",  "re-read sdk.ini and Settings.cfg",      cmd_reload);

    // push -1 (2) + push imm32 (5): seven bytes, whole instructions, and both
    // position-independent, so they run unchanged from the trampoline.
    static const uint8_t sig[7] = { 0x6A, 0xFF, 0x68, 0x2C, 0xED, 0x86, 0x00 };
    const uintptr_t target = build::rebase() + VA_DISPATCH;
    if (!hooks::install_trampoline(target, 7, (void*)&hook_dispatch, &g_tramp,
                                   sig, sizeof(sig), "console")) {
        return;
    }
    g_installed = true;
    sdk_log("[console] dispatcher @%08x hooked - type `sdk help` in the game console", (unsigned)target);
}

}}} // namespace sdk::engine::console
