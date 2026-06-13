// Forced borderless-windowed for Sacred Gold + DDraw call logging.
//
// All three primitives we need (CreateWindowExA, ChangeDisplaySettingsA,
// DirectDrawCreate/Ex) are imported by name through Sacred.exe's IAT, so we
// patch the IAT slots directly — no MinHook / Detours / vtable surgery.

#include "sdk.h"
#include "iat_hook.h"
#include <unknwn.h>

namespace sdk { namespace hooks {

using CreateWindowExA_t        = HWND (WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int,
                                                 HWND, HMENU, HINSTANCE, LPVOID);
using ChangeDisplaySettingsA_t = LONG (WINAPI*)(LPDEVMODEA, DWORD);
using DirectDrawCreate_t       = HRESULT (WINAPI*)(GUID*, void**, IUnknown*);
using DirectDrawCreateEx_t     = HRESULT (WINAPI*)(GUID*, void**, REFIID, IUnknown*);
using SetWindowPos_t           = BOOL (WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindow_t             = BOOL (WINAPI*)(HWND, int, int, int, int, BOOL);
using SetCursor_t              = HCURSOR (WINAPI*)(HCURSOR);

static CreateWindowExA_t        orig_CreateWindowExA        = nullptr;
static ChangeDisplaySettingsA_t orig_ChangeDisplaySettingsA = nullptr;
static DirectDrawCreate_t       orig_DirectDrawCreate       = nullptr;
static DirectDrawCreateEx_t     orig_DirectDrawCreateEx     = nullptr;
static SetWindowPos_t           orig_SetWindowPos           = nullptr;
static MoveWindow_t             orig_MoveWindow             = nullptr;
static SetCursor_t              orig_SetCursor              = nullptr;

ForceConfig g_force = { false, false, 0, 0, false, false, false };
volatile bool g_main_seen     = false;
volatile bool g_main_modified = false;

// Whether we've already forced the main-window style. Sacred can call
// CreateWindowExA many times (menus, controls, dialogs), but only the *first*
// top-level Sacred-owned window is the main game canvas.
static bool g_main_overridden = false;

// --- sdk.ini reader -------------------------------------------------------
// Tiny INI parser tailored to our minimal needs. Format:
//   [hooks]
//   force_borderless=1
//   force_width=1920
//   force_height=1080
//   swallow_displaymode=1
//
// Lookup is exe_dir\sdk.ini. Anything missing keeps defaults.
static void load_config() {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    char path[MAX_PATH];
    _snprintf_s(path, _TRUNCATE, "%s\\sdk.ini", exe);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        sdk_log("[hooks] no sdk.ini at '%s' — keeping defaults (no force)", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* k = line;
        const char* v = eq + 1;
        // trim
        while (*k == ' ' || *k == '\t') k++;
        while (*v == ' ' || *v == '\t') v++;
        char* eol = (char*)v + strlen(v);
        while (eol > v && (eol[-1] == '\n' || eol[-1] == '\r' || eol[-1] == ' ')) {
            *(--eol) = 0;
        }
        if      (_stricmp(k, "force_borderless")  == 0) g_force.enable_borderless = (atoi(v) != 0);
        else if (_stricmp(k, "swallow_displaymode") == 0) g_force.swallow_displaymode = (atoi(v) != 0);
        else if (_stricmp(k, "force_width")        == 0) g_force.width = atoi(v);
        else if (_stricmp(k, "force_height")       == 0) g_force.height = atoi(v);
        else if (_stricmp(k, "force_fullscreen")   == 0) g_force.fullscreen = (atoi(v) != 0);
        else if (_stricmp(k, "enable_hd")          == 0) g_force.hd = (atoi(v) != 0);
        else if (_stricmp(k, "enable_stretch")     == 0) g_force.stretch = (atoi(v) != 0);
        else if (_stricmp(k, "enable_smooth")      == 0) g_force.smooth = (atoi(v) != 0);
    }
    fclose(f);
    sdk::framecap::g_enabled = g_force.smooth;
    sdk_log("[hooks] sdk.ini: borderless=%d swallow_dm=%d wh=%dx%d stretch=%d smooth=%d",
            g_force.enable_borderless, g_force.swallow_displaymode,
            g_force.width, g_force.height, g_force.stretch, g_force.smooth);
}

// Match Sacred's main window:
//   - hInstance equals the EXE's module (i.e. Sacred itself created it,
//     not USER32 internals or a child DLL)
//   - top-level (no parent / owner)
//   - reasonably sized (>= 320x240)
static bool looks_like_sacred_main(HWND parent, const char* cls, int w, int h) {
    // Match Sacred's main window by CLASS NAME, not hInstance. The
    // hInstance check failed in practice (Sacred's window is created with
    // an hInstance != our exe_module handle), so g_sacred_hwnd never got
    // set → the DDraw 800x600→client stretch (gated on g_sacred_hwnd)
    // never ran → tiny image in a desktop-sized window ("in a window").
    // Sacred passes the literal class string "Sacred" (RE-confirmed).
    if (parent) return false;
    if (w < 320 || h < 240) return false;
    if (!cls || HIWORD(cls) == 0) return false;          // atom, not a string
    return _stricmp(cls, "Sacred") == 0;
}

static HWND WINAPI hook_CreateWindowExA(
    DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
    int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    // Class names can be atoms (HIWORD == 0) — never dereference those as strings.
    const char* cls = (HIWORD(lpClassName) ? lpClassName : "(atom)");
    const char* nm  = (lpWindowName && HIWORD(lpWindowName) ? lpWindowName : "(null)");

    bool would_match = !g_main_overridden &&
                       looks_like_sacred_main(hWndParent,
                           HIWORD(lpClassName) ? lpClassName : nullptr,
                           nWidth, nHeight);

    // Apply borderless override if enabled and this is the main window.
    DWORD effStyle   = dwStyle;
    DWORD effExStyle = dwExStyle;
    int   effW = nWidth, effH = nHeight;
    int   effX = X,      effY = Y;
    if (would_match && g_force.enable_borderless) {
        // Strip ALL frame bits, keep WS_VISIBLE.
        const DWORD frame_mask = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX
                               | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER
                               | WS_DLGFRAME | WS_OVERLAPPED;
        effStyle = (effStyle & ~frame_mask) | WS_POPUP | WS_VISIBLE;
        // Drop window-frame ex bits.
        effExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE
                       | WS_EX_STATICEDGE);
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        if (g_force.fullscreen) {
            // True fullscreen: cover the whole primary monitor at desktop
            // resolution from origin (0,0). DDraw stretch (ddraw_hooks)
            // scales Sacred's 800x600 primary to this client area.
            effW = sw; effH = sh; effX = 0; effY = 0;
        } else {
            if (g_force.width  > 0) effW = g_force.width;
            if (g_force.height > 0) effH = g_force.height;
            // Center on the primary monitor.
            effX = (sw - effW) / 2;
            effY = (sh - effH) / 2;
        }
    }

    sdk_log("[hook] CreateWindowExA%s%s: cls='%s' name='%s' "
            "style=%#lx->%#lx ex=%#lx->%#lx %dx%d->%dx%d at (%d,%d)->(%d,%d)",
            would_match ? " MAIN-MATCH" : "",
            (would_match && g_force.enable_borderless) ? " [FORCING borderless]" : "",
            cls, nm, dwStyle, effStyle, dwExStyle, effExStyle,
            nWidth, nHeight, effW, effH, X, Y, effX, effY);

    HWND h = orig_CreateWindowExA(effExStyle, lpClassName, lpWindowName, effStyle,
                                  effX, effY, effW, effH, hWndParent, hMenu, hInstance, lpParam);

    if (would_match && h) {
        g_sacred_hwnd = h;
        g_main_overridden = true;
        g_main_seen = true;
        g_main_modified = g_force.enable_borderless;
        sdk_log("[hook]   -> Sacred main HWND=%p (modified=%d)", h, (int)g_force.enable_borderless);
    }
    return h;
}

static LONG WINAPI hook_ChangeDisplaySettingsA(LPDEVMODEA lpDevMode, DWORD dwFlags) {
    if (g_force.swallow_displaymode) {
        if (lpDevMode) {
            sdk_log("[hook] ChangeDisplaySettingsA SWALLOWED (req %lux%lu @%lubpp flags=%#lx)",
                    lpDevMode->dmPelsWidth, lpDevMode->dmPelsHeight,
                    lpDevMode->dmBitsPerPel, dwFlags);
        } else {
            sdk_log("[hook] ChangeDisplaySettingsA(NULL, %#lx) SWALLOWED (would restore)", dwFlags);
        }
        return DISP_CHANGE_SUCCESSFUL;
    }
    if (lpDevMode) {
        sdk_log("[hook] ChangeDisplaySettingsA pass-through (req %lux%lu @%lubpp flags=%#lx)",
                lpDevMode->dmPelsWidth, lpDevMode->dmPelsHeight,
                lpDevMode->dmBitsPerPel, dwFlags);
    } else {
        sdk_log("[hook] ChangeDisplaySettingsA(NULL, %#lx) pass-through (restore default)", dwFlags);
    }
    return orig_ChangeDisplaySettingsA(lpDevMode, dwFlags);
}

// ReBorn HD part 1: the engine's render W/H come from two hardcoded
// `push imm32` operands feeding the display-init mode lookup
// (cTextureLoader @0x00815f70): VA 0x00816c75 = WIDTH (0x400=1024),
// VA 0x00816c71 = HEIGHT (0x300=768). Patch them to the configured W×H
// BEFORE that lookup runs. DirectDrawCreate fires during display init,
// before the mode scan, and .text is already decrypted by then — a
// clean one-shot point. (Spec: .claude/knowledge/re/render_dims.md)
static void hd_patch_render_dims_once() {
    static bool done = false;
    if (done) return;
    if (!g_force.hd || g_force.width <= 0 || g_force.height <= 0) return;
    HMODULE exe = g_attach.exe_module;
    if (!exe) return;
    uintptr_t rebase = (uintptr_t)exe - 0x00400000;
    // Bytes: 68 00 03 00 00 (push 0x300=768) @0x816c6f, then 68 00 04 00 00
    // (push 0x400=1024) @0x816c74. So the HEIGHT imm32 starts at 0x816c70 and
    // the WIDTH imm32 at 0x816c75. (Earlier 0x816c71 for height was off-by-one:
    // it read 0x68000003 — the 0x300 tail + the next `push` opcode — so the
    // 1024/768 guard always failed and HD silently did nothing.)
    uint32_t* pW = (uint32_t*)(rebase + 0x00816c75);   // imm32 = 1024 (0x400)
    uint32_t* pH = (uint32_t*)(rebase + 0x00816c70);   // imm32 = 768  (0x300)
    __try {
        if (*pW != 0x400 || *pH != 0x300) {
            sdk_log("[hd] render-dim immediates not 1024/768 (W=%#x H=%#x) "
                    "— .text not decrypted yet or wrong build; skip",
                    *pW, *pH);
            return;
        }
        DWORD oldp;
        VirtualProtect(pW, 4, PAGE_EXECUTE_READWRITE, &oldp);
        *pW = (uint32_t)g_force.width;
        VirtualProtect(pW, 4, oldp, &oldp);
        VirtualProtect(pH, 4, PAGE_EXECUTE_READWRITE, &oldp);
        *pH = (uint32_t)g_force.height;
        VirtualProtect(pH, 4, oldp, &oldp);
        FlushInstructionCache(GetCurrentProcess(), pW, 4);
        FlushInstructionCache(GetCurrentProcess(), pH, 4);
        done = true;
        sdk_log("[hd] render-dim immediates patched -> %dx%d "
                "(VA 0x00816c75/0x00816c71)", g_force.width, g_force.height);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[hd] render-dim patch faulted");
    }
}

static HRESULT WINAPI hook_DirectDrawCreate(GUID* guid, void** out, IUnknown* unk) {
    hd_patch_render_dims_once();
    HRESULT hr = orig_DirectDrawCreate(guid, out, unk);
    sdk_log("[hook] DirectDrawCreate(guid=%p) -> hr=%#lx, obj=%p", guid, hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out) {
        ddraw_hooks::install(*out);
    }
    return hr;
}

static HRESULT WINAPI hook_DirectDrawCreateEx(GUID* guid, void** out, REFIID iid, IUnknown* unk) {
    hd_patch_render_dims_once();
    HRESULT hr = orig_DirectDrawCreateEx(guid, out, iid, unk);
    sdk_log("[hook] DirectDrawCreateEx(guid=%p) -> hr=%#lx, obj=%p", guid, hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out) {
        ddraw_hooks::install(*out);
    }
    return hr;
}

// Keep Sacred's main window pinned to the full screen. In windowed mode the
// engine SetWindowPos/MoveWindow's its own window down to the render size
// (1024x768, centered) AFTER our CreateWindowExA force — and the DirectDraw
// clipper attached to that window then clips our stretched present back into the
// small rect. Forcing the window to cover the whole screen lets the clipper pass
// the full-screen Blt (ddraw_hooks hook_Blt does the actual stretch).
static bool want_pin_fullscreen(HWND hwnd) {
    // Only pin for the stretch path (which needs the clipper full-screen). Plain
    // borderless must NOT pin — it confused the engine's transitions (black).
    return g_force.stretch && hwnd && hwnd == (HWND)g_sacred_hwnd;
}

static BOOL WINAPI hook_SetWindowPos(HWND hwnd, HWND after, int x, int y,
                                     int cx, int cy, UINT flags) {
    if (want_pin_fullscreen(hwnd)) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        return orig_SetWindowPos(hwnd, after, 0, 0, sw, sh, flags);
    }
    return orig_SetWindowPos(hwnd, after, x, y, cx, cy, flags);
}

static BOOL WINAPI hook_MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
    if (want_pin_fullscreen(hwnd)) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        return orig_MoveWindow(hwnd, 0, 0, sw, sh, repaint);
    }
    return orig_MoveWindow(hwnd, x, y, w, h, repaint);
}

// In smooth mode the SDK overlay shows the captured frame (with Sacred's OWN
// software cursor baked in) opaquely on top — so the redundant OS cursor (the
// IDC_ARROW that DefWindowProc sets via SetCursor) becomes a SECOND visible
// pointer. Force it to none; the game's drawn cursor remains. Stateless.
static HCURSOR WINAPI hook_SetCursor(HCURSOR h) {
    if (g_force.smooth) return orig_SetCursor(nullptr);
    return orig_SetCursor(h);
}

void install() {
    HMODULE exe = GetModuleHandleA(nullptr);
    load_config();

    // Suppress Windows' "(Not Responding)" ghost window. With our borderless
    // fullscreen + stretched DDraw present, the game's message pump can lag past
    // Windows' ~5s timeout, so the desktop window manager throws up a ghost
    // "Sacred is not responding" dialog even though the game is fine. This API
    // turns ghosting off for the whole process (no downside for a game).
    if (HMODULE u32 = GetModuleHandleA("user32.dll")) {
        typedef void (WINAPI* DisableGhosting_t)(void);
        if (auto fn = (DisableGhosting_t)GetProcAddress(u32, "DisableProcessWindowsGhosting")) {
            fn();
            sdk_log("[hooks] DisableProcessWindowsGhosting() called (no 'not responding' ghost)");
        }
    }
    sdk_log("[hooks] installing IAT patches against exe=%p (g_attach.exe_module=%p)",
            exe, g_attach.exe_module);

    orig_CreateWindowExA = (CreateWindowExA_t)
        iat::patch(exe, "USER32.dll", "CreateWindowExA", (void*)hook_CreateWindowExA);

    orig_ChangeDisplaySettingsA = (ChangeDisplaySettingsA_t)
        iat::patch(exe, "USER32.dll", "ChangeDisplaySettingsA", (void*)hook_ChangeDisplaySettingsA);

    orig_DirectDrawCreate = (DirectDrawCreate_t)
        iat::patch(exe, "DDRAW.dll", "DirectDrawCreate", (void*)hook_DirectDrawCreate);

    orig_DirectDrawCreateEx = (DirectDrawCreateEx_t)
        iat::patch(exe, "DDRAW.dll", "DirectDrawCreateEx", (void*)hook_DirectDrawCreateEx);

    // Pin the main window to fullscreen so the DDraw clipper passes the stretched
    // present (the engine otherwise shrinks its own window to the render size).
    orig_SetWindowPos = (SetWindowPos_t)
        iat::patch(exe, "USER32.dll", "SetWindowPos", (void*)hook_SetWindowPos);
    orig_MoveWindow = (MoveWindow_t)
        iat::patch(exe, "USER32.dll", "MoveWindow", (void*)hook_MoveWindow);
    orig_SetCursor = (SetCursor_t)
        iat::patch(exe, "USER32.dll", "SetCursor", (void*)hook_SetCursor);

    sdk_log("[hooks] done: cwex=%p cds=%p ddc=%p ddcex=%p",
            orig_CreateWindowExA, orig_ChangeDisplaySettingsA,
            orig_DirectDrawCreate, orig_DirectDrawCreateEx);
}

}} // namespace sdk::hooks
