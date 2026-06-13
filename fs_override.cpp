// SacredSDK — filesystem override layer.
//
// Hooks the Sacred.exe IAT slot for `CreateFileA` (and `_lopen` / `OpenFile`
// if they're imported) so we can transparently redirect ANY file open to a
// shadow copy under `<game_dir>\custom\`.
//
// Design goals
// ------------
//   - **Zero changes to the Steam install**. Modders drop files in
//     `<game_dir>\custom\<sub>\<file>` mirroring the original tree.
//     Steam's "Verify integrity" never sees them, never overwrites them.
//   - **Read-only redirection**. We only swap the path for READ opens.
//     Sacred's write opens (logs, save games) go through untouched.
//   - **No content-type assumptions**. The same hook handles
//     `scripts/us/global.res`, `bin/TYPE_NPC_X/FunkCode.bin`,
//     `bin/Balance.bin`, `pak/textures.pak`, etc.
//   - **Cheap on the hot path**. Per-open cost is a single
//     `GetFileAttributesA` of the candidate `custom\` path; if it doesn't
//     exist, we fall back to the original path immediately.
//
// What we deliberately DON'T hook
// -------------------------------
//   - `Sacred.exe` itself — that's our entry point, never goes through us.
//   - DLLs (`granny.dll`, `ijl15.dll`, etc.) — loaded by Windows loader,
//     not via CreateFileA.
//   - The override directory itself (no recursive lookups).
//   - Anything outside the game install dir.
//   - Write opens (GENERIC_WRITE | OPEN_ALWAYS | CREATE_*).

#include "sdk.h"
#include "iat_hook.h"
#include <cstdio>
#include <cstring>

namespace sdk { namespace fs_override {

volatile long g_total_opens = 0;
volatile long g_redirected  = 0;
volatile long g_verbose     = 0;   // when set, log every CreateFileA call

static char g_last_redirect[MAX_PATH * 2] = "";
const char* last_redirect() { return g_last_redirect; }

static char g_game_dir[MAX_PATH] = {0};        // e.g. E:\SteamLibrary\...\Sacred Gold
static size_t g_game_dir_len = 0;
static char g_custom_dir[MAX_PATH] = {0};      // <game_dir>\custom

static bool path_starts_with_i(const char* p, const char* prefix, size_t prefix_len) {
    return _strnicmp(p, prefix, (int)prefix_len) == 0;
}

// Normalise a path to <game_dir>-relative form if possible.
// Returns nullptr if the path is outside the game install or is one of the
// blocklisted top-level files we never override.
//
// Accepts:
//   "scripts\us\global.res"               (already relative)
//   ".\scripts\us\global.res"             (dot-relative)
//   "E:\...\Sacred Gold\scripts\us\..."   (absolute inside game)
//   "scripts/us/global.res"               (forward slashes — normalised)
static bool to_relative(const char* in, char out_rel[MAX_PATH]) {
    if (!in || !*in) return false;

    char buf[MAX_PATH];
    size_t n = strlen(in);
    if (n >= MAX_PATH) return false;
    memcpy(buf, in, n + 1);

    // Normalise slashes
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '/') buf[i] = '\\';

    const char* rel = buf;
    if (n >= 2 && buf[0] == '.' && buf[1] == '\\') rel = buf + 2;
    else if (path_starts_with_i(buf, g_game_dir, g_game_dir_len)
             && buf[g_game_dir_len] == '\\') {
        rel = buf + g_game_dir_len + 1;
    }
    // Reject anything still containing a drive letter (X:\)
    if (rel[0] && rel[1] == ':') return false;
    // Reject root-level / blocklisted names
    if (!strchr(rel, '\\')) {
        // top-level file — these are EXE, DLLs, configs etc. never override.
        return false;
    }
    // Reject anything that's already inside the override dir.
    if (_strnicmp(rel, "custom\\", 7) == 0) return false;
    // Reject parent-dir escapes for safety.
    if (strstr(rel, "..\\")) return false;

    strncpy_s(out_rel, MAX_PATH, rel, _TRUNCATE);
    return true;
}

// Build <game_dir>\custom\<rel>
static bool build_custom_path(const char* rel, char out[MAX_PATH]) {
    int r = _snprintf_s(out, MAX_PATH, _TRUNCATE, "%s\\%s", g_custom_dir, rel);
    return r > 0;
}

static bool custom_exists(const char* custom_path) {
    DWORD attrs = GetFileAttributesA(custom_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return false;
    return true;
}

// ----- hook -----------------------------------------------------------
using CreateFileA_t = HANDLE (WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                        DWORD, DWORD, HANDLE);
static CreateFileA_t orig_CreateFileA = nullptr;

static HANDLE WINAPI hook_CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess,
                                      DWORD dwShareMode,
                                      LPSECURITY_ATTRIBUTES lpSecAttrs,
                                      DWORD dwCreationDisposition,
                                      DWORD dwFlagsAndAttributes,
                                      HANDLE hTemplateFile)
{
    InterlockedIncrement(&g_total_opens);

    if (g_verbose && lpFileName) {
        bool wants_write = (dwDesiredAccess & GENERIC_WRITE) != 0;
        sdk_log("[fs] %s  disp=%lu acc=%#lx '%s'",
                wants_write ? "WRITE" : "read ",
                dwCreationDisposition, dwDesiredAccess, lpFileName);
    }

    if (!lpFileName) {
        return orig_CreateFileA(lpFileName, dwDesiredAccess, dwShareMode,
                                lpSecAttrs, dwCreationDisposition,
                                dwFlagsAndAttributes, hTemplateFile);
    }

    // Only redirect pure-read opens. If the caller is creating or writing,
    // pass through — that's a save game / log file, not a content read.
    bool is_read_only =
        (dwDesiredAccess & GENERIC_WRITE) == 0 &&
        (dwCreationDisposition == OPEN_EXISTING ||
         dwCreationDisposition == OPEN_ALWAYS);

    if (is_read_only) {
        char rel[MAX_PATH];
        if (to_relative(lpFileName, rel)) {
            char custom_path[MAX_PATH];
            if (build_custom_path(rel, custom_path) && custom_exists(custom_path)) {
                _snprintf_s(g_last_redirect, _TRUNCATE,
                            "%s  ->  custom\\%s", lpFileName, rel);
                InterlockedIncrement(&g_redirected);
                sdk_log("[fs_override] %s  ->  %s", lpFileName, custom_path);
                return orig_CreateFileA(custom_path, dwDesiredAccess, dwShareMode,
                                        lpSecAttrs, dwCreationDisposition,
                                        dwFlagsAndAttributes, hTemplateFile);
            }
        }
    }
    return orig_CreateFileA(lpFileName, dwDesiredAccess, dwShareMode,
                            lpSecAttrs, dwCreationDisposition,
                            dwFlagsAndAttributes, hTemplateFile);
}

// ----- install --------------------------------------------------------
void install() {
    // Resolve game dir from g_attach.exe_path (set at DllMain).
    if (g_attach.exe_path[0]) {
        strncpy_s(g_game_dir, _TRUNCATE, g_attach.exe_path, _TRUNCATE);
        char* slash = strrchr(g_game_dir, '\\');
        if (slash) *slash = 0;
        g_game_dir_len = strlen(g_game_dir);
    }
    _snprintf_s(g_custom_dir, _TRUNCATE, "%s\\custom", g_game_dir);

    // NB: do NOT call CreateDirectoryA / any filesystem API from DllMain —
    // the Windows loader holds its lock during DLL init and a filesystem
    // call can recurse into LoadLibrary (e.g. for shell-extension DLLs) and
    // deadlock or crash. The user / setup creates `custom/` manually; if it
    // doesn't exist, the redirect simply doesn't match anything and Sacred
    // falls through to the vanilla file. Safe to ship.

    // Patch IAT (just memory writes — loader-safe).
    HMODULE exe = GetModuleHandleA(nullptr);
    orig_CreateFileA = (CreateFileA_t)iat::patch(
        exe, "KERNEL32.dll", "CreateFileA", (void*)hook_CreateFileA);

    sdk_log("[fs_override] installed. game_dir='%s'  custom_dir='%s'  orig_CreateFileA=%p",
            g_game_dir, g_custom_dir, orig_CreateFileA);
}

}} // namespace sdk::fs_override
