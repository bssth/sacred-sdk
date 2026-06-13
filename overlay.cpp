// SacredSDK — true in-game overlay.
//
// Renders ImGui via D3D11 into a layered, color-keyed popup window that is
// glued on top of Sacred's main HWND every frame. The window's background is
// pure magenta (RGB 0xFF00FF); WS_EX_LAYERED + LWA_COLORKEY tells DWM to
// composite that color as fully transparent. Net effect: Sacred renders
// normally underneath, ImGui floats on top.
//
// Input modes (toggle: F11)
// -------------------------
//   CAPTURE  — overlay has WS_EX_NOACTIVATE off, takes foreground, ImGui gets
//              clicks/keys. Sacred is paused for input. (Default at startup
//              so we land on a workable UI.)
//   PASSTHRU — WS_EX_TRANSPARENT + WS_EX_NOACTIVATE re-added; the overlay is
//              click-through, Sacred receives all input. Overlay still
//              renders.
//
// Why a layered popup, not DDraw surface composition:
//   Sacred renders via DirectDraw 7; ImGui has no DDraw backend, and the cost
//   of hooking IDirectDrawSurface::Blt to do software composition is high
//   (palette-aware blit, alpha math, surface-format translation). A layered
//   DXGI window glued to Sacred's client rect is dramatically simpler and
//   loses only in exclusive-fullscreen — which Settings.cfg disables via
//   "borderless" mode that hooks.cpp already enforces.

#include "sdk.h"
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <tchar.h>
#include <shellapi.h>
#include <vector>

#pragma comment(lib, "shell32.lib")

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace sdk { namespace overlay {

// --- state ---
static HANDLE g_thread = nullptr;
static volatile LONG g_should_quit = 0;
static HWND g_hwnd = nullptr;
static ID3D11Device*           g_dev   = nullptr;
static ID3D11DeviceContext*    g_ctx   = nullptr;
static IDXGISwapChain*         g_swap  = nullptr;
static ID3D11RenderTargetView* g_rtv   = nullptr;

// Magenta colorkey (full red + full blue, zero green) → DWM treats it as
// 100% transparent. Picked so that ImGui's dark theme never blends here.
static constexpr COLORREF CHROMA_KEY = RGB(0xFF, 0x00, 0xFF);

// --- toast queue (used by sacred.notify) -----------------------------------
//
// sacred.notify(text) → C: sacred_show_banner → overlay::push_toast → here.
// We keep a small ring of recent toasts each with an expiry tick, render
// the unexpired ones as a banner column near the top of the Sacred client
// area. The runtime_triggers layer has already applied throttle+dedup so
// our queue is bounded by user pace, not engine pace.
//
// Buffer overflow protection: TOAST_CAP=8. If push runs while queue is
// full we evict the oldest. Each toast lives for TOAST_TTL_MS ms.
struct Toast {
    char     text[256];
    uint64_t expires_at;       // GetTickCount64 future deadline
};
static constexpr int      TOAST_CAP    = 8;
static constexpr uint64_t TOAST_TTL_MS = 4500;
static Toast              g_toasts[TOAST_CAP];
static int                g_toast_count = 0;   // valid entries [0..count)
static CRITICAL_SECTION   g_toast_cs;
static bool               g_toast_cs_init = false;

void push_toast(const char* text) {
    if (!text) return;
    if (!g_toast_cs_init) {
        InitializeCriticalSection(&g_toast_cs);
        g_toast_cs_init = true;
    }
    EnterCriticalSection(&g_toast_cs);
    if (g_toast_count >= TOAST_CAP) {
        // Evict oldest (shift left). Cheap — TOAST_CAP is small.
        memmove(&g_toasts[0], &g_toasts[1], sizeof(Toast) * (TOAST_CAP - 1));
        g_toast_count = TOAST_CAP - 1;
    }
    Toast& t = g_toasts[g_toast_count++];
    strncpy_s(t.text, sizeof(t.text), text, _TRUNCATE);
    t.expires_at = GetTickCount64() + TOAST_TTL_MS;
    LeaveCriticalSection(&g_toast_cs);
}

// Render expired-aware toast column. Called from the per-frame UI pass.
static void draw_toasts() {
    if (!g_toast_cs_init) return;
    uint64_t now = GetTickCount64();
    Toast snap[TOAST_CAP];
    int n = 0;
    EnterCriticalSection(&g_toast_cs);
    // Drop expired toasts (compact in-place).
    int w = 0;
    for (int r = 0; r < g_toast_count; r++) {
        if (g_toasts[r].expires_at > now) {
            if (w != r) g_toasts[w] = g_toasts[r];
            w++;
        }
    }
    g_toast_count = w;
    n = w;
    memcpy(snap, g_toasts, sizeof(Toast) * n);
    LeaveCriticalSection(&g_toast_cs);
    if (n == 0) return;

    // Render at top-center of viewport. Each toast: dark translucent BG +
    // amber-tinted text. Stacks vertically (newest at bottom).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float center_x = vp->Pos.x + vp->Size.x * 0.5f;
    float y        = vp->Pos.y + 80.0f;
    for (int i = 0; i < n; i++) {
        ImGui::SetNextWindowPos(ImVec2(center_x, y), ImGuiCond_Always,
                                ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        char winid[24]; _snprintf_s(winid, _TRUNCATE, "##toast%d", i);
        ImGui::Begin(winid, nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::TextUnformatted(snap[i].text);
        ImGui::PopStyleColor();
        ImVec2 sz = ImGui::GetWindowSize();
        ImGui::End();
        y += sz.y + 6.0f;
    }
}

// Input-mode state. CAPTURE = overlay receives mouse/keys; PASSTHRU = clicks
// fall through to Sacred. F11 toggles. Default = CAPTURE so the SDK UI is
// immediately interactive after attach.
// Default OFF: overlay starts hidden + input passthrough (DEBUG-only tool,
// per user). F12 = show/hide UI, F11 = grab/release input.
static volatile bool g_input_capture = false;
// F12 toggles overlay window visibility — hides ALL ImGui chrome behind
// a fully-transparent layer. Toasts AND the chrome chip are hidden. The
// overlay thread keeps running so handlers etc. stay alive; we just
// stop rendering UI when this is true.
static volatile bool g_overlay_hidden = true;
static bool          g_last_f7  = false;
// Free-text label typed in the overlay; F7 stamps it into the log line
// so saved positions are self-documenting.
static char          g_pos_label[64] = "";
static bool          g_last_f8  = false;
static bool          g_last_f9  = false;
static bool          g_last_f10 = false;
static bool          g_last_f11 = false;
static bool          g_last_f12 = false;

// --- D3D11 setup -----------------------------------------------------------
static bool create_rtv() {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    HRESULT hr = g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return SUCCEEDED(hr);
}

static bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        want, ARRAYSIZE(want), D3D11_SDK_VERSION,
        &scd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) {
        // Fall back to WARP so even crappy GPUs or RDP sessions render.
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            want, ARRAYSIZE(want), D3D11_SDK_VERSION,
            &scd, &g_swap, &g_dev, &got, &g_ctx);
        if (FAILED(hr)) {
            sdk_log("[overlay] D3D11CreateDeviceAndSwapChain failed: 0x%08x", hr);
            return false;
        }
        sdk_log("[overlay] D3D11 fell back to WARP");
    }
    return create_rtv();
}

// --- integrated bilinear upscaler (enable_smooth) --------------------------
// Draws the captured native game frame (frame_cap.cpp) full-screen with a LINEAR
// sampler, smoothing the 1024x768→screen upscale that DirectDraw point-samples.
static ID3D11VertexShader*  g_up_vs   = nullptr;
static ID3D11PixelShader*   g_up_ps   = nullptr;
static ID3D11SamplerState*  g_up_samp = nullptr;
static ID3D11Texture2D*     g_up_tex  = nullptr;   // dynamic BGRA8, frame-sized
static ID3D11ShaderResourceView* g_up_srv = nullptr;
static int                  g_up_w = 0, g_up_h = 0;
static std::vector<uint8_t> g_up_raw;              // staging for take()

static void upscaler_init() {
    // Fullscreen-triangle VS (no vertex buffer) + textured PS with linear sampler.
    static const char* kVS =
        "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
        "V main(uint id:SV_VertexID){V o;float2 t=float2((id<<1)&2,id&2);"
        "o.uv=t;o.p=float4(t*float2(2,-2)+float2(-1,1),0,1);return o;}";
    static const char* kPS =
        "Texture2D tx:register(t0);SamplerState sm:register(s0);"
        "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{return tx.Sample(sm,uv);}";
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(kVS, strlen(kVS), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsb, &err))) {
        sdk_log("[overlay][up] VS compile failed"); if (err) err->Release(); return;
    }
    if (FAILED(D3DCompile(kPS, strlen(kPS), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psb, &err))) {
        sdk_log("[overlay][up] PS compile failed"); if (err) err->Release(); if (vsb) vsb->Release(); return;
    }
    g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_up_vs);
    g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_up_ps);
    vsb->Release(); psb->Release();
    D3D11_SAMPLER_DESC sdsc{};
    sdsc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;        // bilinear
    sdsc.AddressU = sdsc.AddressV = sdsc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdsc.MaxLOD = D3D11_FLOAT32_MAX;
    g_dev->CreateSamplerState(&sdsc, &g_up_samp);
    sdk_log("[overlay][up] bilinear upscaler ready (vs=%p ps=%p samp=%p)",
            g_up_vs, g_up_ps, g_up_samp);
}

static bool upscaler_ensure_tex(int w, int h) {
    if (g_up_tex && g_up_w == w && g_up_h == h) return true;
    if (g_up_srv) { g_up_srv->Release(); g_up_srv = nullptr; }
    if (g_up_tex) { g_up_tex->Release(); g_up_tex = nullptr; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_up_tex))) return false;
    if (FAILED(g_dev->CreateShaderResourceView(g_up_tex, nullptr, &g_up_srv))) return false;
    g_up_w = w; g_up_h = h;
    return true;
}

// Convert one raw frame (16-bit RGB565 or 32-bit BGRX) into the mapped BGRA8.
static void upscaler_convert(const uint8_t* raw, int w, int h, int bpp,
                             void* dst, int dstPitch) {
    if (bpp == 32) {
        const int row = w * 4;
        for (int y = 0; y < h; ++y)
            memcpy((uint8_t*)dst + (size_t)y * dstPitch, raw + (size_t)y * row, row);
    } else { // 16-bit 565
        for (int y = 0; y < h; ++y) {
            const uint16_t* s = (const uint16_t*)(raw + (size_t)y * (w * 2));
            uint8_t* d = (uint8_t*)dst + (size_t)y * dstPitch;
            for (int x = 0; x < w; ++x) {
                uint16_t p = s[x];
                uint8_t r = (uint8_t)((p >> 11) & 0x1f), g = (uint8_t)((p >> 5) & 0x3f), b = (uint8_t)(p & 0x1f);
                d[x*4+0] = (uint8_t)((b << 3) | (b >> 2));   // B
                d[x*4+1] = (uint8_t)((g << 2) | (g >> 4));   // G
                d[x*4+2] = (uint8_t)((r << 3) | (r >> 2));   // R
                d[x*4+3] = 0xFF;                             // A
            }
        }
    }
}

// Draw the captured frame full-screen with bilinear filtering. Returns true if
// it drew (so the caller skips the magenta transparent clear).
static bool upscaler_draw(int rtv_w, int rtv_h) {
    if (!g_up_vs || !g_up_ps || !g_up_samp) return false;
    int w = 0, h = 0, bpp = 0;
    if (!sdk::framecap::peek_dims(&w, &h, &bpp)) return false;
    if (w <= 0 || h <= 0 || (bpp != 16 && bpp != 32)) return false;
    if (!upscaler_ensure_tex(w, h)) return false;

    const int bytespp = bpp / 8;
    if ((int)g_up_raw.size() < w * bytespp * h) g_up_raw.resize((size_t)w * bytespp * h);
    static DWORD s_last_fresh = 0;
    if (sdk::framecap::take(g_up_raw.data(), w * bytespp)) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_up_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            upscaler_convert(g_up_raw.data(), w, h, bpp, m.pData, (int)m.RowPitch);
            g_ctx->Unmap(g_up_tex, 0);
        }
        s_last_fresh = GetTickCount();
    }
    if (!g_up_srv) return false;
    // If fresh frames stopped flowing (a screen transition / load / present-path
    // change), DON'T keep covering the screen with a stale (possibly black) frame
    // — bow out (return false → transparent) so the engine's OWN present shows
    // through. Resumes smoothing the moment new frames arrive.
    if (s_last_fresh == 0 || (GetTickCount() - s_last_fresh) > 200) return false;

    D3D11_VIEWPORT vp{}; vp.Width = (float)rtv_w; vp.Height = (float)rtv_h; vp.MaxDepth = 1.0f;
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->IASetInputLayout(nullptr);
    g_ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_up_vs, nullptr, 0);
    g_ctx->PSSetShader(g_up_ps, nullptr, 0);
    g_ctx->PSSetShaderResources(0, 1, &g_up_srv);
    g_ctx->PSSetSamplers(0, 1, &g_up_samp);
    g_ctx->Draw(3, 0);
    return true;
}

static void cleanup_device() {
    if (g_up_srv)  { g_up_srv->Release();  g_up_srv = nullptr; }
    if (g_up_tex)  { g_up_tex->Release();  g_up_tex = nullptr; }
    if (g_up_samp) { g_up_samp->Release(); g_up_samp = nullptr; }
    if (g_up_ps)   { g_up_ps->Release();   g_up_ps = nullptr; }
    if (g_up_vs)   { g_up_vs->Release();   g_up_vs = nullptr; }
    g_up_w = g_up_h = 0;
    if (g_rtv)  { g_rtv->Release();  g_rtv = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx)  { g_ctx->Release();  g_ctx = nullptr; }
    if (g_dev)  { g_dev->Release();  g_dev = nullptr; }
}

// --- input-mode toggle -----------------------------------------------------
//
// CAPTURE  : overlay can be clicked, ImGui receives input, Sacred is locked
//            out of mouse/keys while it has focus.
// PASSTHRU : WS_EX_TRANSPARENT + WS_EX_NOACTIVATE; clicks fall through to
//            Sacred and the overlay never steals foreground.
//
// We modify GWL_EXSTYLE in-place — Windows picks up WS_EX_TRANSPARENT changes
// without needing SetWindowPos(SWP_FRAMECHANGED) on layered popups.
static void apply_input_mode() {
    if (!g_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (g_input_capture) {
        ex &= ~(LONG_PTR)WS_EX_TRANSPARENT;
        ex &= ~(LONG_PTR)WS_EX_NOACTIVATE;
    } else {
        ex |= WS_EX_TRANSPARENT;
        ex |= WS_EX_NOACTIVATE;
    }
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);

    // Push focus to the appropriate window so keys flow there.
    if (g_input_capture) {
        SetForegroundWindow(g_hwnd);
        SetFocus(g_hwnd);
    } else if (g_sacred_hwnd && IsWindow(g_sacred_hwnd)) {
        SetForegroundWindow(g_sacred_hwnd);
    }
}

// Place / size our overlay to exactly match Sacred's client area. Called
// every frame — Sacred's window doesn't typically move after creation, but
// the user might Alt-Tab or change resolution, and we want to track.
static void reflow_to_sacred() {
    HWND s = g_sacred_hwnd;
    if (!s || !IsWindow(s) || !g_hwnd) return;
    RECT cr;
    if (!GetClientRect(s, &cr)) return;
    POINT tl{ 0, 0 };
    if (!ClientToScreen(s, &tl)) return;
    int w = cr.right - cr.left;
    int h = cr.bottom - cr.top;
    if (w <= 0 || h <= 0) return;
    RECT my;
    if (!GetWindowRect(g_hwnd, &my)) return;
    if (my.left != tl.x || my.top != tl.y
        || (my.right - my.left) != w || (my.bottom - my.top) != h) {
        SetWindowPos(g_hwnd, HWND_TOPMOST, tl.x, tl.y, w, h, SWP_NOACTIVATE);
    }
}

// --- WndProc ---------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;
    switch (msg) {
        case WM_SIZE:
            if (g_dev && wp != SIZE_MINIMIZED) {
                if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
                g_swap->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp),
                                      DXGI_FORMAT_UNKNOWN, 0);
                create_rtv();
            }
            return 0;
        case WM_CLOSE:
            // Hide instead of destroy; user can re-show via hotkey later.
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- UI --------------------------------------------------------------------
// Snapshot a tail of the log every frame and render it as a scrollable region.
static void draw_ui() {
    using namespace sdk;
    auto* a = &g_attach;

    // Floating chrome chip — always visible, shows current input mode + how
    // to toggle. Sits in the top-left, doesn't trap clicks.
    {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGuiWindowFlags chip = ImGuiWindowFlags_NoDecoration
                              | ImGuiWindowFlags_AlwaysAutoResize
                              | ImGuiWindowFlags_NoSavedSettings
                              | ImGuiWindowFlags_NoFocusOnAppearing
                              | ImGuiWindowFlags_NoNav
                              | ImGuiWindowFlags_NoMove;
        ImGui::Begin("##sdk_chip", nullptr, chip);
        if (g_input_capture) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                               "SacredSDK [CAPTURE]  F11=release F12=hide  F9=clear F10=snap");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                               "SacredSDK [PASSTHRU] F11=grab F12=hide  F9=clear F10=snap");
        }
        ImGui::End();
    }

    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10.0f, 50.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("SacredSDK overlay");

    double up_s = (GetTickCount() - a->attach_tick) / 1000.0;
    ImGui::Text("uptime  : %.1f s", up_s);
    ImGui::Text("pid/tid : %lu / %lu", a->pid, a->attach_tid);
    ImGui::Text("exe     : %s", a->exe_path);
    ImGui::Text("cmdline : %s", a->cmdline);
    ImGui::Text("exe base: %p   self base: %p", a->exe_module, a->self_module);

    ImGui::Separator();
    // --- Lua-driven baking. Shows what the auto-bake did at startup +
    //     manual rebake button. Errors and Lua tracebacks land in `status`
    //     so the user sees them without digging through sdk_loaded.log.
    if (ImGui::CollapsingHeader("Lua mods (custom/lua/**.lua)", ImGuiTreeNodeFlags_DefaultOpen)) {
        long files = sdk::lua_bake::baked_files();
        long records = sdk::lua_bake::baked_records();
        ImGui::Text("baked files   : %ld", files);
        ImGui::Text("baked records : %ld", records);
        const char* st = sdk::lua_bake::status();
        bool is_error = st && (strstr(st, "error") || strstr(st, "failed")
                               || strstr(st, "cannot") || strstr(st, "abort"));
        ImGui::TextColored(is_error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                    : ImVec4(0.8f, 1.0f, 0.8f, 1.0f),
                           "status: %s", st ? st : "(idle)");
        if (sdk::lua_bake::busy()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(baking…)");
        } else {
            if (ImGui::Button("Rebake all .lua")) {
                sdk::lua_bake::bake_all();
            }
            ImGui::SameLine();
            if (ImGui::Button("Open custom/ in Explorer")) {
                char exe[MAX_PATH] = {0};
                GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
                char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
                char custom_dir[MAX_PATH];
                _snprintf_s(custom_dir, _TRUNCATE, "%s\\custom", exe);
                ShellExecuteA(nullptr, "open", custom_dir, nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
            if (ImGui::Button("Open MODDING_GUIDE")) {
                char exe[MAX_PATH] = {0};
                GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
                char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
                char guide[MAX_PATH];
                _snprintf_s(guide, _TRUNCATE, "%s\\custom\\MODDING_GUIDE.md", exe);
                ShellExecuteA(nullptr, "open", guide, nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Drop .lua mods at <game>/custom/lua/<rel>.lua; output goes to "
            "<game>/custom/<rel>.bin where fs_override picks it up.");
    }

    // --- Runtime trigger hooks — live counters for sacred.on_trigger.
    if (ImGui::CollapsingHeader("Runtime triggers", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool ready    = sdk::runtime_triggers::g_ready;
        long handlers = sdk::runtime_triggers::g_handlers;
        long fires    = sdk::runtime_triggers::g_fires;
        long errs     = sdk::runtime_triggers::g_handler_errs;
        long thunk_st = sdk::runtime_triggers::g_thunk_self_trigger;
        long thunk_dc = sdk::runtime_triggers::g_thunk_dialog_check;
        long thunk_fn = sdk::runtime_triggers::g_thunk_funnel;
        long thunk_hs = sdk::runtime_triggers::g_thunk_hash;
        long seen_st  = sdk::runtime_triggers::g_seen_self_trigger;
        long seen_dc  = sdk::runtime_triggers::g_seen_dialog_check;
        long seen_fn  = sdk::runtime_triggers::g_seen_funnel;
        long seen_hs  = sdk::runtime_triggers::g_seen_hash;
        ImGui::TextColored(ready ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f)
                                  : ImVec4(1.0f, 0.7f, 0.4f, 1.0f),
                           "state          : %s", ready ? "ready" : "idle");
        ImGui::Text("handlers       : %ld registered name(s)", handlers);
        ImGui::Text("fires          : %ld successful pcall(s)", fires);
        ImGui::TextColored(errs > 0 ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                     : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "errors         : %ld pcall failure(s)", errs);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f),
                           "thunk @0x80E780: %ld  →  seen: %ld  (sacred_hash — every name)",
                           thunk_hs, seen_hs);
        ImGui::Text("thunk @0x463240: %ld  →  seen: %ld  (FIRE FUNNEL — by name)",
                    thunk_fn, seen_fn);
        ImGui::Text("thunk @0x4915A0: %ld  →  seen: %ld  (SelfTriggerQuest)",
                    thunk_st, seen_st);
        ImGui::Text("thunk @0x491170: %ld  →  seen: %ld  (Dialog-Check)",
                    thunk_dc, seen_dc);
        if ((thunk_st > 0 && seen_st == 0) ||
            (thunk_dc > 0 && seen_dc == 0) ||
            (thunk_fn > 0 && seen_fn == 0) ||
            (thunk_hs > 0 && seen_hs == 0)) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                "thunk>seen gap → hook fires, but name validation rejects");
        }
        // Last-seen names — guides the user to register correct triggers.
        char ring[8][128];
        int nshown = sdk::runtime_triggers::last_seen(ring, 8);
        if (nshown > 0) {
            ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f),
                "Recent trigger names (most recent first):");
            for (int i = 0; i < nshown; i++) {
                ImGui::BulletText("%s", ring[i]);
            }
        } else if (seen_st == 0 && seen_dc == 0 && ready) {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1.0f),
                "(hooks live but no trigger dispatched yet — play through "
                "an NPC dialog or quest event to see names here)");
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "status: %s",
                           sdk::runtime_triggers::status());
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Pick a name above and register via "
            "sacred.on_trigger(\"NAME\", function() … end).");
        if (ImGui::Button("Snapshot to log")) {
            sdk::runtime_triggers::snapshot_ring_to_log();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "do the thing in-game, click → ring goes to sdk_loaded.log");
    }

    // --- Script mods (byte-swap on FunkCode.bin) — first thing users want.
    sdk::script_mods::draw_panel();

    if (ImGui::CollapsingHeader("Source compiler (Path B)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("runs / successes : %ld / %ld",
                    sdk::source_compiler::g_runs,
                    sdk::source_compiler::g_successes);
        if (ImGui::Button("Run smoke test")) {
            sdk::source_compiler::smoke_test();
        }
        ImGui::SameLine();
        ImGui::TextWrapped("writes minimal `custom\\scripts\\test_script.txt` "
                          "then calls FUN_00671ad0");
        ImGui::Separator();
        ImGui::TextWrapped("last: %s", sdk::source_compiler::g_last_message);
    }

    if (ImGui::CollapsingHeader("Custom/ overrides", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("CreateFileA opens : %ld", sdk::fs_override::g_total_opens);
        ImGui::TextColored(
            sdk::fs_override::g_redirected > 0
                ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "redirected        : %ld",
            sdk::fs_override::g_redirected);
        const char* last = sdk::fs_override::last_redirect();
        if (last && last[0]) {
            ImGui::TextWrapped("last  : %s", last);
        }
        ImGui::TextWrapped("drop replacement files in `<game_dir>\\custom\\` "
                          "mirroring the install tree (e.g. "
                          "`custom\\scripts\\us\\global.res`)");
    }

    if (ImGui::CollapsingHeader("2.29-rosetta patches", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(sdk::patches::g_patch1_active
                               ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "patch 1 (global.res from disk) : %s",
                           sdk::patches::g_patch1_active ? "ACTIVE" : "off");
        ImGui::TextColored(sdk::patches::g_patch6_active
                               ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "patch 6 (focus-force NOP)      : %s",
                           sdk::patches::g_patch6_active ? "ACTIVE" : "off (site TBD)");
        ImGui::TextWrapped("status: %s", sdk::patches::status());
    }

    if (ImGui::CollapsingHeader("Display force (sdk.ini)", 0)) {
        const auto& fc = sdk::hooks::g_force;
        ImGui::Text("force_borderless    : %s", fc.enable_borderless  ? "ON" : "off");
        ImGui::Text("swallow_displaymode : %s", fc.swallow_displaymode ? "ON" : "off");
        ImGui::Text("size override       : %dx%d (0=keep)", fc.width, fc.height);
        ImGui::Text("main window seen    : %s%s",
                    sdk::hooks::g_main_seen ? "yes" : "no",
                    sdk::hooks::g_main_modified ? " (modified)" : "");
        ImGui::Separator();
        ImGui::TextColored(sdk::ddraw_hooks::g_installed
                               ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "DDraw vtable hooks  : %s",
                           sdk::ddraw_hooks::g_installed ? "INSTALLED" : "not yet");
        ImGui::Text("primary surfaces    : %ld", sdk::ddraw_hooks::g_primary_surfaces_seen);
        ImGui::Text("Blt total / stretched: %ld / %ld",
                    sdk::ddraw_hooks::g_blt_calls,
                    sdk::ddraw_hooks::g_blt_stretched);
    }

    if (ImGui::CollapsingHeader("Resource lookup logger", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(sdk::text_logger::g_active
                               ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "FUN_0080eaf0 hook  : %s",
                           sdk::text_logger::g_active ? "ACTIVE" : "off");
        ImGui::Text("calls   : %ld", sdk::text_logger::g_calls);
        ImGui::Text("unique  : %ld   (flushed every 5s to sdk\\logs\\seen_hashes.csv)",
                    sdk::text_logger::g_unique);
    }

    if (ImGui::CollapsingHeader("Player (read-only)", ImGuiTreeNodeFlags_DefaultOpen)) {
        sdk::player::Snapshot ps = sdk::player::read();
        if (!ps.valid) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "no player loaded (struct@0x%p)", (void*)ps.struct_addr);
        } else {
            ImGui::Text("struct  : 0x%p", (void*)ps.struct_addr);
            const char* cls = sdk::player::class_name(ps.class_id);
            ImGui::Text("class   : %u (%s)", ps.class_id, cls ? cls : "?");
            ImGui::Text("health  : %d", ps.health);
            int32_t wx = 0, wy = 0;
            if (sdk::player::world_pos(&wx, &wy)) {
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f),
                    "map xy  : %d, %d   KompassPos (F7 dumps; paste into KX,KY)", wx, wy);
            } else {
                ImGui::Text("world   : (unavailable)");
            }
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("pos label (F7 stamps this)",
                             g_pos_label, sizeof(g_pos_label));

            if (ImGui::TreeNodeEx("skills", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < 8; i++) {
                    uint8_t id = ps.skills[i];
                    if (id == 0) continue;
                    const char* nm = sdk::player::skill_name(id);
                    ImGui::Text("  [%d] %2u %s", i + 1, id, nm ? nm : "?");
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("equipment")) {
                #define SLOT(name) ImGui::Text("  %-9s : %u", #name, ps.name)
                SLOT(helmet);  SLOT(cuirass);  SLOT(belt);     SLOT(boots);
                SLOT(gauntlets); SLOT(bracers); SLOT(amulet1); SLOT(amulet2);
                SLOT(ring1); SLOT(ring2); SLOT(ring3); SLOT(ring4);
                SLOT(weapon_l); SLOT(weapon_r);
                SLOT(cannon); SLOT(shoulders); SLOT(greaves); SLOT(wings);
                #undef SLOT
                ImGui::TreePop();
            }
        }
    }

    if (ImGui::CollapsingHeader("Log (in-memory ring)", ImGuiTreeNodeFlags_DefaultOpen)) {
        static char snap[LogRing::CAP][LogRing::LINE_MAX];
        int n = g_log.snapshot(snap, LogRing::CAP);

        if (ImGui::BeginChild("##loglines", ImVec2(0, 200), true,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (int i = 0; i < n; i++)
                ImGui::TextUnformatted(snap[i]);
            // Auto-scroll only if user is at the bottom.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        if (ImGui::Button("ping log")) {
            sdk_log("[ui] ping from overlay button");
        }
    }
    ImGui::End();
}

// --- Thread main -----------------------------------------------------------
static DWORD WINAPI thread_main(LPVOID) {
    // Create the overlay a few seconds in — late enough that Sacred's
    // window + DDraw swapchain exist and settle (so we layer cleanly on
    // top, not mis-positioned), but a FIXED delay: do NOT gate on
    // g_sacred_hwnd. The CreateWindowExA "main window" match is
    // unreliable (Sacred makes its own popup from its config), so
    // g_sacred_hwnd may never be set — gating on it killed the overlay.
    Sleep(5000);

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                    GetModuleHandleW(nullptr), nullptr, nullptr, nullptr,
                    nullptr, L"SacredSDK_Overlay", nullptr };
    if (!RegisterClassExW(&wc)) {
        sdk_log("[overlay] RegisterClassExW failed (%lu)", GetLastError());
        return 1;
    }

    // Window style breakdown:
    //   WS_POPUP         — no border, no caption — pure client area for DX11
    //   WS_EX_LAYERED    — required for SetLayeredWindowAttributes; this is
    //                      what lets DWM treat our magenta clear-color as
    //                      transparent
    //   WS_EX_TOPMOST    — float above Sacred even when Sacred has focus
    //   WS_EX_TOOLWINDOW — no taskbar entry / no Alt-Tab clutter
    //   (WS_EX_TRANSPARENT + WS_EX_NOACTIVATE start CLEARED — default mode
    //    is CAPTURE so the SDK UI is immediately usable. F11 toggles.)
    DWORD ex_style = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    g_hwnd = CreateWindowExW(ex_style,
        wc.lpszClassName, L"SacredSDK",
        WS_POPUP,
        50, 50, 600, 420,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) {
        sdk_log("[overlay] CreateWindowExW failed (%lu)", GetLastError());
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 2;
    }

    // Colorkey-transparency: anything we render at exactly RGB(0xFF,0x00,0xFF)
    // becomes 100% transparent in compositing. Other pixels render normally.
    SetLayeredWindowAttributes(g_hwnd, CHROMA_KEY, 0, LWA_COLORKEY);

    if (!create_device(g_hwnd)) {
        DestroyWindow(g_hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 3;
    }
    upscaler_init();   // bilinear game-frame upscaler (enable_smooth)
    ShowWindow(g_hwnd, SW_SHOWNA);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr; // don't litter %CWD% with imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    sdk_log("[overlay] true-overlay up: hwnd=%p, chroma=#%06lX, F11=toggle input",
            g_hwnd, (unsigned long)CHROMA_KEY);

    MSG msg{};
    bool done = false;
    while (!done && !InterlockedCompareExchange(&g_should_quit, 0, 0)) {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // F11 edge-toggle: input mode CAPTURE ↔ PASSTHRU.
        // F12 edge-toggle: overlay visibility (hides ALL UI + toasts).
        // GetAsyncKeyState polls global key state so this works regardless
        // of which window has foreground.
        bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (f11 && !g_last_f11) {
            g_input_capture = !g_input_capture;
            apply_input_mode();
            sdk_log("[overlay] input mode -> %s",
                    g_input_capture ? "CAPTURE" : "PASSTHRU");
        }
        g_last_f11 = f11;

        bool f12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (f12 && !g_last_f12) {
            g_overlay_hidden = !g_overlay_hidden;
            sdk_log("[overlay] visibility -> %s",
                    g_overlay_hidden ? "HIDDEN" : "VISIBLE");
        }
        g_last_f12 = f12;

        // F9 edge: clear the ring so the next F10 snapshot only contains
        // names queried since the clear. Workflow:
        //   - approach NPC
        //   - press F9 (ring empty, capturing starts fresh)
        //   - click accept
        //   - press F10 (snapshot of names captured during accept)
        // F8 edge: dump the quest-display registry to the log. Use this
        // AFTER the world is fully loaded (walk around, open a quest) to
        // see how many entries exist and whether a custom quest_id
        // survived vanilla's savegame load.
        // F7 edge: dump live hero world coords to the log. Stand where you
        // want a quest marker / screenshot, press F7, paste the numbers
        // into questbook_set_kompass(KX,KY,...).
        bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (f7 && !g_last_f7) {
            int32_t wx = 0, wy = 0;
            if (sdk::player::world_pos(&wx, &wy)) {
                sdk_log("[overlay] F7 -> hero map pos (KompassPos): "
                        "KX=%d KY=%d  label='%s'  (paste into KX,KY / "
                        "questbook_set_kompass/marker)",
                        wx, wy, g_pos_label[0] ? g_pos_label : "(none)");
            } else {
                sdk_log("[overlay] F7 -> hero map pos unavailable "
                        "(no player loaded)");
            }
        }
        g_last_f7 = f7;

        bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (f8 && !g_last_f8) {
            sdk_log("[overlay] F8 → questbook dump");
            sdk::runtime_triggers::questbook_dump_to_log();
        }
        g_last_f8 = f8;

        bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (f9 && !g_last_f9) {
            sdk_log("[overlay] F9 → ring cleared");
            sdk::runtime_triggers::clear_ring();
        }
        g_last_f9 = f9;

        // F10 edge: snapshot the runtime_triggers ring to the log without
        // breaking game focus. Use this RIGHT WHEN doing the in-game
        // action you want to identify (accept quest, kill mob, …).
        bool f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        if (f10 && !g_last_f10) {
            sdk_log("[overlay] F10 → snapshot triggered");
            sdk::runtime_triggers::snapshot_ring_to_log();
        }
        g_last_f10 = f10;

        // Glue to Sacred's client rect every frame. Cheap when nothing moved
        // (one GetWindowRect + 1 compare); resizes swapchain via WM_SIZE.
        reflow_to_sacred();

        // Smooth mode startup-focus fix: when our opaque upscaler overlay is up,
        // Sacred can end up NOT the active window (input ignored → menu doesn't
        // click; OS arrow shows as a 2nd cursor). Alt-Tab away+back fixes it; do
        // that automatically ONCE, ~1.5 s in, by handing the foreground to
        // Sacred's window (we never want OUR overlay to be the active window in
        // passthrough). One-shot so we don't fight legitimate focus changes.
        if (sdk::framecap::g_enabled && !g_input_capture) {
            static int  fg_frames = 0;
            static bool fg_done   = false;
            HWND game = (HWND)g_sacred_hwnd;
            if (game && !fg_done && ++fg_frames > 90) {
                SetForegroundWindow(game);
                SetActiveWindow(game);
                // Kill the OS arrow that DefWindowProc sets from the window CLASS
                // cursor (it bypasses our IAT SetCursor hook → showed as a 2nd
                // pointer next to Sacred's own in-frame cursor). NULL class cursor
                // → DefWindowProc sets none.
                SetClassLongPtrW(game, GCLP_HCURSOR, 0);
                fg_done = true;
                sdk_log("[overlay] smooth: foreground->Sacred %p, class cursor cleared", game);
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (!g_overlay_hidden) {
            draw_ui();
            draw_toasts();      // sacred.notify banners over the game window
        }
        ImGui::Render();

        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        // Smooth mode: draw the captured game frame full-screen (bilinear) — this
        // fills the whole RTV opaquely, so the overlay shows the SMOOTH upscale
        // over the engine's point-sampled DDraw present underneath. Falls back to
        // the magenta colorkey (transparent → game shows through) when there's no
        // frame yet or enable_smooth is off.
        bool drew_game = false;
        if (sdk::framecap::g_enabled) {
            RECT rc{}; GetClientRect(g_hwnd, &rc);
            drew_game = upscaler_draw(rc.right, rc.bottom);
        }
        if (!drew_game) {
            const float clear[4] = { 1.0f, 0.0f, 1.0f, 1.0f };  // magenta → transparent
            g_ctx->ClearRenderTargetView(g_rtv, clear);
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    sdk_log("[overlay] thread exiting cleanly");
    return 0;
}

void start() {
    if (g_thread) return;
    g_thread = CreateThread(nullptr, 0, thread_main, nullptr, 0, nullptr);
}
void stop() {
    InterlockedExchange(&g_should_quit, 1);
    if (g_hwnd) PostMessageW(g_hwnd, WM_QUIT, 0, 0);
    // Don't WaitForSingleObject on the thread during DLL_PROCESS_DETACH —
    // the loader lock would deadlock with the overlay thread's CreateWindow
    // having taken USER's lock earlier. We let the thread die when the
    // process terminates.
}

}} // namespace sdk::overlay
