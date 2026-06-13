// SacredSDK — ijl15.dll proxy for Sacred Gold (i386).
//
// Build target: Win32 (x86), output filename: ijl15.dll.
// Exports are declared via #pragma comment(linker, "/EXPORT:…") below.
// Each forwards to the renamed real DLL (ijl15_real.dll), with the original
// ordinal preserved so Sacred.exe's ordinal-2..5 imports resolve correctly.
//
// On DLL_PROCESS_ATTACH we:
//   1. capture process metadata (exe path, cmdline, pid, base)
//   2. spawn the overlay thread (own D3D11 window + ImGui)
//   3. write a startup record to sdk\logs\sdk_loaded.log AND the in-memory ring

#include "sdk.h"
#include <stdlib.h>   // atoi (sdk.ini flag parse)

// --- Exported forwarders ----------------------------------------------------
// /EXPORT:name=otherdll.name,@ordinal[,NONAME]
// Ordinals match the original ijl15.dll layout (verified by pefile).
#pragma comment(linker, "/EXPORT:ijlGetLibVersion=ijl15_real.ijlGetLibVersion,@1")
#pragma comment(linker, "/EXPORT:ijlInit=ijl15_real.ijlInit,@2")
#pragma comment(linker, "/EXPORT:ijlFree=ijl15_real.ijlFree,@3")
#pragma comment(linker, "/EXPORT:ijlRead=ijl15_real.ijlRead,@4")
#pragma comment(linker, "/EXPORT:ijlWrite=ijl15_real.ijlWrite,@5")
#pragma comment(linker, "/EXPORT:ijlErrorStr=ijl15_real.ijlErrorStr,@6")

namespace sdk {

LogRing    g_log{};
AttachInfo g_attach{};
volatile HWND g_sacred_hwnd = nullptr;

// File log path resolved lazily so we don't do FS work in DllMain unless needed.
static char g_log_path[MAX_PATH] = {0};
static CRITICAL_SECTION g_file_cs;
static bool g_file_cs_init = false;

static void resolve_log_path() {
    if (g_log_path[0]) return;
    char exe_path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    char* p = strrchr(exe_path, '\\');
    if (p) *p = 0;
    char dir[MAX_PATH];
    _snprintf_s(dir, _TRUNCATE, "%s\\sdk\\logs", exe_path);
    CreateDirectoryA(dir, NULL);
    _snprintf_s(g_log_path, _TRUNCATE, "%s\\sdk_loaded.log", dir);
}

// Read a boolean flag from exe_dir\sdk.ini (flat key=value; sections ignored).
// Returns true only if `key` is present with a non-zero value. Used to keep
// dev-only self-tests OFF by default for shipped builds (not everyone has PAX
// saves, and per-launch probes are noise). Opt in with e.g. `selftest=1`.
static bool sdk_ini_flag(const char* key) {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    char path[MAX_PATH];
    _snprintf_s(path, _TRUNCATE, "%s\\sdk.ini", exe);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    bool on = false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        const char* k = line; const char* v = eq + 1;
        while (*k == ' ' || *k == '\t') ++k;
        while (*v == ' ' || *v == '\t') ++v;
        if (_stricmp(k, key) == 0) { on = (atoi(v) != 0); break; }
    }
    fclose(f);
    return on;
}

void sdk_log(const char* fmt, ...) {
    // build the line once
    char line[LogRing::LINE_MAX];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = _snprintf_s(line, _TRUNCATE,
                        "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                        st.wYear, st.wMonth, st.wDay,
                        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (n < 0) return;
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(line + n, LogRing::LINE_MAX - n, _TRUNCATE, fmt, args);
    va_end(args);

    // Push to ring (locked, fast)
    g_log.push(line);

    // Also append to file. Locked separately so overlay reads don't block on disk.
    if (g_file_cs_init) {
        EnterCriticalSection(&g_file_cs);
        resolve_log_path();
        FILE* f = nullptr;
        fopen_s(&f, g_log_path, "ab");
        if (f) { fputs(line, f); fputc('\n', f); fclose(f); }
        LeaveCriticalSection(&g_file_cs);
    }
}

static void capture_attach(HMODULE self) {
    g_attach.pid = GetCurrentProcessId();
    g_attach.attach_tid = GetCurrentThreadId();
    g_attach.self_module = self;
    g_attach.exe_module = GetModuleHandleA(NULL);
    g_attach.attach_tick = GetTickCount();
    GetModuleFileNameA(NULL, g_attach.exe_path, MAX_PATH);
    LPSTR cmd = GetCommandLineA();
    if (cmd) strncpy_s(g_attach.cmdline, _TRUNCATE, cmd, _TRUNCATE);
}

} // namespace sdk

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    using namespace sdk;
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            InitializeCriticalSection(&g_file_cs); g_file_cs_init = true;
            g_log.init();

            capture_attach(hModule);

            sdk_log("=== ijl15 proxy DllMain DLL_PROCESS_ATTACH ===");
            sdk_log("  exe       = %s", g_attach.exe_path);
            sdk_log("  cmdline   = %s", g_attach.cmdline);
            sdk_log("  pid/tid   = %lu / %lu", g_attach.pid, g_attach.attach_tid);
            sdk_log("  self base = %p", g_attach.self_module);
            sdk_log("  exe  base = %p", g_attach.exe_module);

            fs_override::install(); // CreateFileA IAT redirect (custom/ overrides any read)
            hooks::install();       // IAT patches go in before EP, so Sacred sees us
            //                      DO NOT install .text patches here — at DllMain
            //                      time the .bind SafeDisc stub hasn't decrypted
            //                      yet, so any byte we write into .text gets
            //                      XOR'd into garbage. patches::install() is now
            //                      chained off dumptext's entropy-trigger worker.
            dumptext::start();   // worker: wait for .bind decryption, then dump
            //                                                  AND call patches::install()
            lua_bake::start_auto_bake();  // worker: walk custom/lua/, produce custom/*.bin
            overlay::start();
            // Dev self-tests are OFF by default (not everyone has PAX saves, and
            // per-launch probes are log noise). Opt in via sdk.ini `selftest=1`.
            // All functionality stays available on demand — the engine VAs and
            // game-zlib resolve LAZILY on first sacred.read_save/globalres/etc.
            if (sdk_ini_flag("selftest")) {
                sdk_log("[startup] selftest=1 — running hero-save + engine-resolve self-tests");
                start_hero_save_probe();      // worker: self-test the wired PAX hero-save port
                engine_resolve::start_engine_resolve();  // worker: verify engine VAs + globalres probe
            }
            break;
        }
        case DLL_PROCESS_DETACH: {
            sdk_log("=== DLL_PROCESS_DETACH (lpReserved=%p) ===", lpReserved);
            overlay::stop();
            g_log.shutdown();
            if (g_file_cs_init) {
                DeleteCriticalSection(&g_file_cs);
                g_file_cs_init = false;
            }
            break;
        }
    }
    return TRUE;
}
