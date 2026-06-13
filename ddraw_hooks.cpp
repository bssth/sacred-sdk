// SacredSDK — DirectDraw vtable hooks for HD stretching.
//
// Sacred is a 2D DirectDraw game with a hardcoded internal framebuffer
// (640×480 / 800×600 / 1024×768 — pick from Settings.cfg). Window mode
// resizing alone gives a 1920×1080 window with the game stuck in the
// top-left 800×600 — Sacred Blts its small backbuffer to the same dest
// rect regardless of the window size.
//
// Fix: hook IDirectDraw::CreateSurface to recognise the PRIMARY surface,
// then patch the primary's vtable to override its Blt method. When Sacred
// calls primary->Blt(destRect=(0,0,800,600), src=backbuffer, ...) we replace
// destRect with the window's full client rect — DDraw natively supports
// asymmetric Blt-stretching when src/dst rects differ.
//
// Reference: DirectDraw 7 vtable layout (ddraw.h).
//   IDirectDraw* vtable:
//     0..2 = QueryInterface, AddRef, Release  (IUnknown)
//     3..N = IDirectDraw methods
//     6    = CreateSurface
//     20   = SetCooperativeLevel
//     21   = SetDisplayMode
//   IDirectDrawSurface vtable:
//     0..2 = IUnknown
//     5    = Blt
//     11   = Flip
//
// We hook v1 vtable. If Sacred QueryInterfaces a v7 and uses that, the
// log will tell us (we'll see calls on the v1 hook stop firing).

#include "sdk.h"
#include <ddraw.h>
#include <cstring>

#pragma comment(lib, "ddraw.lib")

namespace sdk { namespace ddraw_hooks {

volatile bool g_installed             = false;
volatile long g_blt_calls             = 0;
volatile long g_blt_stretched         = 0;
volatile long g_primary_surfaces_seen = 0;

// ---- vtable indices (DirectDraw7-compat, but identical for v1 for the
// methods we touch) -------------------------------------------------------
static constexpr size_t IDD_CreateSurface      = 6;
static constexpr size_t IDD_EnumDisplayModes   = 8;
static constexpr size_t IDD_SetCooperativeLevel = 20;
static constexpr size_t IDD_SetDisplayMode     = 21;
static constexpr size_t IDS_Blt                = 5;
static constexpr size_t IDS_Flip               = 11;

// ---- saved originals ----------------------------------------------------
typedef HRESULT (WINAPI* CreateSurface_t)(IDirectDraw*, LPDDSURFACEDESC,
                                          IDirectDrawSurface**, IUnknown*);
typedef HRESULT (WINAPI* SetCooperativeLevel_t)(IDirectDraw*, HWND, DWORD);
typedef HRESULT (WINAPI* SetDisplayMode_t)(IDirectDraw*, DWORD, DWORD, DWORD);
typedef HRESULT (WINAPI* EnumDisplayModes_t)(IDirectDraw*, DWORD,
                                             LPDDSURFACEDESC, LPVOID,
                                             LPDDENUMMODESCALLBACK);
static EnumDisplayModes_t     orig_EnumDisplayModes    = nullptr;
typedef HRESULT (WINAPI* Blt_t)(IDirectDrawSurface*, LPRECT,
                                 IDirectDrawSurface*, LPRECT,
                                 DWORD, LPDDBLTFX);
typedef HRESULT (WINAPI* Flip_t)(IDirectDrawSurface*, IDirectDrawSurface*, DWORD);

static CreateSurface_t        orig_CreateSurface       = nullptr;
static SetCooperativeLevel_t  orig_SetCooperativeLevel = nullptr;
static SetDisplayMode_t       orig_SetDisplayMode      = nullptr;
static Blt_t                  orig_Blt                 = nullptr;
static Flip_t                 orig_Flip                = nullptr;

static IDirectDraw*        g_dd       = nullptr;
static IDirectDrawSurface* g_primary  = nullptr;
static DWORD               g_primary_width  = 0;
static DWORD               g_primary_height = 0;

// ---- vtable patching helper ---------------------------------------------
// Replace `vtable[index]` with `replacement`, return the original. The
// vtable usually lives in DDRAW.dll's writable data section but VirtualProtect
// it just in case.
static void* patch_vtable_slot(void** vtable, size_t index, void* replacement) {
    DWORD old;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old)) {
        sdk_log("[ddraw] VirtualProtect(vtable[%zu]) failed: %lu",
                index, GetLastError());
        return nullptr;
    }
    void* orig = vtable[index];
    vtable[index] = replacement;
    DWORD dummy;
    VirtualProtect(&vtable[index], sizeof(void*), old, &dummy);
    return orig;
}

// ---- IDirectDrawSurface::Blt --------------------------------------------
// The "stretching" intercept. When Sacred Blts onto the primary surface,
// override the destRect to fill the full window client area.
static HRESULT WINAPI hook_Blt(IDirectDrawSurface* self, LPRECT destRect,
                                IDirectDrawSurface* src, LPRECT srcRect,
                                DWORD flags, LPDDBLTFX fx) {
    InterlockedIncrement(&g_blt_calls);

    // Only rewrite the FRAME PRESENT: an offscreen→primary Blt whose dest is the
    // (native-size) window content area. The vtable is shared by all surfaces, so
    // we must let every other Blt (textures, small UI pieces blitted straight to
    // the primary) pass through untouched — stretching those would scramble the
    // frame. Heuristic: self is the primary, there is a real source surface, and
    // the dest spans most of the window (a large blit = the present).
    if (self == g_primary && src != nullptr) {
        static long diag = 0;
        long d = InterlockedIncrement(&diag);
        if (d <= 16) {
            char db[64]="NULL", sb[64]="NULL";
            if (destRect) _snprintf_s(db,_TRUNCATE,"(%ld,%ld,%ld,%ld)%ldx%ld",destRect->left,destRect->top,destRect->right,destRect->bottom,destRect->right-destRect->left,destRect->bottom-destRect->top);
            if (srcRect)  _snprintf_s(sb,_TRUNCATE,"(%ld,%ld,%ld,%ld)%ldx%ld",srcRect->left,srcRect->top,srcRect->right,srcRect->bottom,srcRect->right-srcRect->left,srcRect->bottom-srcRect->top);
            sdk_log("[ddraw][blt#%ld] PRIMARY src=%p dest=%s srcRect=%s flags=%#x", d, src, db, sb, flags);
        }
    }
    if (self == g_primary && src != nullptr && destRect) {
        long dw = destRect->right - destRect->left;
        long dh = destRect->bottom - destRect->top;
        // The present is the one large offscreen→primary blit. Stretch the WHOLE
        // source surface (the engine's native-size render target) to the full
        // desktop. We pass srcRect=NULL so DDraw uses the real source bounds —
        // the engine sometimes asks for a srcRect bigger than the source (window
        // pinned to 1920x1080 but render is 1024x768), which reads out of bounds
        // → black. NULL = whole source → correct content, stretched to fill.
        if (dw >= 512 && dh >= 384) {
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            RECT full = { 0, 0, sw, sh };
            InterlockedIncrement(&g_blt_stretched);
            // Smooth mode: snapshot the native frame for the overlay's bilinear
            // upscaler (it draws on top of this point-sampled DDraw present).
            if (sdk::framecap::g_enabled) {
                static long hb = 0, capok = 0, capfail = 0;
                DDSURFACEDESC sd; ZeroMemory(&sd, sizeof(sd)); sd.dwSize = sizeof(sd);
                HRESULT lr = src->Lock(nullptr, &sd, DDLOCK_READONLY | DDLOCK_WAIT, nullptr);
                if (SUCCEEDED(lr)) {
                    if (sd.lpSurface) {
                        sdk::framecap::publish(sd.lpSurface, (int)sd.dwWidth, (int)sd.dwHeight,
                                               (int)sd.lPitch, (int)sd.ddpfPixelFormat.dwRGBBitCount);
                        InterlockedIncrement(&capok);
                    }
                    src->Unlock(nullptr);
                } else {
                    InterlockedIncrement(&capfail);
                }
                if ((InterlockedIncrement(&hb) % 120) == 1)
                    sdk_log("[cap-hb] present alive: prim=%p src=%p capOK=%ld capFAIL=%ld lastLockHr=%#lx",
                            (void*)self, (void*)src, capok, capfail, lr);
            }
            return orig_Blt(self, &full, src, nullptr /*whole source*/, flags, fx);
        }
    }
    return orig_Blt(self, destRect, src, srcRect, flags, fx);
}

// ---- IDirectDrawSurface::BltFast (vtable slot 7) ------------------------
// BltFast canNOT stretch (src size == dest size always). If Sacred presents via
// BltFast onto the primary, the small frame lands 1:1 in the corner and a Blt
// destRect rewrite won't help — we'd need to convert it to a stretching Blt.
// For now: log so we know whether the present path is BltFast.
typedef HRESULT (WINAPI* BltFast_t)(IDirectDrawSurface*, DWORD, DWORD,
                                    IDirectDrawSurface*, LPRECT, DWORD);
static BltFast_t orig_BltFast = nullptr;
static HRESULT WINAPI hook_BltFast(IDirectDrawSurface* self, DWORD x, DWORD y,
                                   IDirectDrawSurface* src, LPRECT srcRect, DWORD trans) {
    if (self == g_primary && src != nullptr) {
        static long diag = 0;
        long d = InterlockedIncrement(&diag);
        if (d <= 16) {
            long sw = srcRect ? (srcRect->right - srcRect->left) : -1;
            long sh = srcRect ? (srcRect->bottom - srcRect->top) : -1;
            sdk_log("[ddraw][bltfast#%ld] PRIMARY at (%lu,%lu) src=%p srcRect=%ldx%ld trans=%#x",
                    d, x, y, src, sw, sh, trans);
        }
    }
    return orig_BltFast(self, x, y, src, srcRect, trans);
}

// ---- IDirectDrawSurface::Flip -------------------------------------------
// Some apps flip instead of blit. Log it; for stretching, flip-backed
// surfaces need different handling (would require fake primary or render
// indirection). For now just log and pass through.
static HRESULT WINAPI hook_Flip(IDirectDrawSurface* self,
                                 IDirectDrawSurface* override_target,
                                 DWORD flags) {
    if (self == g_primary) {
        // Don't spam — log every 60th call (≈1Hz at 60 FPS).
        static long count = 0;
        if ((InterlockedIncrement(&count) % 60) == 1) {
            sdk_log("[ddraw] Flip on primary (override=%p flags=%#x) — "
                    "this surface flips instead of blits; stretching won't work",
                    override_target, flags);
        }
    }
    return orig_Flip(self, override_target, flags);
}

// ---- Patch the primary surface's vtable ---------------------------------
// DISABLED in safe-mode. First attempt crashed Sacred ~5s in, likely because
// Sacred's CreateSurface(PRIMARY|COMPLEX, backbuffers=N) needs the exclusive
// cooperative-level we were stripping. Coming back to this with a proper
// log-everything pass first.
static volatile bool g_blt_hooked = false;
static void patch_surface_vtable(IDirectDrawSurface* surf) {
    if (!surf) return;
    if (!sdk::hooks::g_force.stretch) {
        sdk_log("[ddraw] (no stretch) primary surface %p — Blt vtable not patched", surf);
        return;
    }
    if (g_blt_hooked) return;                       // shared vtable — patch once
    void** vt = *(void***)surf;                     // surface vtable (shared by all surfaces)
    orig_Blt = (Blt_t)patch_vtable_slot(vt, IDS_Blt, (void*)&hook_Blt);
    orig_BltFast = (BltFast_t)patch_vtable_slot(vt, 7 /*BltFast*/, (void*)&hook_BltFast);
    orig_Flip = (Flip_t)patch_vtable_slot(vt, IDS_Flip, (void*)&hook_Flip);
    g_blt_hooked = (orig_Blt != nullptr);
    sdk_log("[ddraw][stretch] primary %p — hooked Blt(orig=%p) BltFast(orig=%p) Flip(orig=%p)",
            surf, orig_Blt, orig_BltFast, orig_Flip);
}

// ---- IDirectDraw::CreateSurface -----------------------------------------
static HRESULT WINAPI hook_CreateSurface(IDirectDraw* self,
                                          LPDDSURFACEDESC desc,
                                          IDirectDrawSurface** out,
                                          IUnknown* outer)
{
    // ReBorn HD: before the surface is made, rewrite the PRIMARY/
    // BACKBUFFER dimensions to the configured W×H so Sacred renders at
    // that resolution. Only when explicit dims are present (backbuffer);
    // the primary is usually display-sized and left alone.
    if (sdk::hooks::g_force.hd && desc &&
        sdk::hooks::g_force.width > 0 && sdk::hooks::g_force.height > 0 &&
        (desc->dwFlags & DDSD_WIDTH) && (desc->dwFlags & DDSD_HEIGHT)) {
        DWORD caps = desc->ddsCaps.dwCaps;
        DWORD w = desc->dwWidth, h = desc->dwHeight;
        // Rewrite EVERY render-sized surface (the engine's logical
        // 1024x768 / 800x600), regardless of memory-pool caps, so the
        // whole COMPLEX flip chain + 3D-device + vidmem/sysmem copies
        // stay CONSISTENT. The earlier caps-mask missed a LOCALVIDMEM
        // copy (caps 0x24000) → mixed 1920x1080/1024x768 dims → DDraw
        // "Error initializing DirectX". Skip textures (atlas/UI assets).
        bool render_sized = (w == 1024 && h == 768) || (w == 800 && h == 600);
        if (render_sized && !(caps & DDSCAPS_TEXTURE)) {
            sdk_log("[ddraw][HD] CreateSurface %lux%lu -> %dx%d (caps=%#x)",
                    w, h, sdk::hooks::g_force.width,
                    sdk::hooks::g_force.height, caps);
            desc->dwWidth  = (DWORD)sdk::hooks::g_force.width;
            desc->dwHeight = (DWORD)sdk::hooks::g_force.height;
        }
    }

    HRESULT hr = orig_CreateSurface(self, desc, out, outer);

    if (SUCCEEDED(hr) && desc && out && *out) {
        DWORD caps  = desc->ddsCaps.dwCaps;
        DWORD w     = desc->dwWidth;
        DWORD h     = desc->dwHeight;
        const char* kind = (caps & DDSCAPS_PRIMARYSURFACE) ? "PRIMARY"
                        : (caps & DDSCAPS_BACKBUFFER)     ? "BACKBUFFER"
                        : (caps & DDSCAPS_OFFSCREENPLAIN) ? "OFFSCREEN"
                        : (caps & DDSCAPS_TEXTURE)        ? "TEXTURE"
                        :                                   "(other)";
        sdk_log("[ddraw] CreateSurface caps=%#x kind=%s size=%lux%lu -> surf=%p",
                caps, kind, w, h, *out);

        if (caps & DDSCAPS_PRIMARYSURFACE) {
            g_primary = *out;
            g_primary_width  = w;
            g_primary_height = h;
            InterlockedIncrement(&g_primary_surfaces_seen);
            patch_surface_vtable(*out);
        }
    } else if (FAILED(hr)) {
        sdk_log("[ddraw] CreateSurface FAILED hr=%#lx caps=%#x %lux%lu",
                hr, desc ? desc->ddsCaps.dwCaps : 0,
                desc ? desc->dwWidth : 0, desc ? desc->dwHeight : 0);
    }
    return hr;
}

// ---- IDirectDraw::SetCooperativeLevel -----------------------------------
// SAFE-MODE: log only. Stripping EXCLUSIVE here made Sacred's subsequent
// CreateSurface(PRIMARY|COMPLEX) fail with DDERR_NOEXCLUSIVEMODE and crash.
static HRESULT WINAPI hook_SetCooperativeLevel(IDirectDraw* self, HWND hwnd, DWORD flags) {
    sdk_log("[ddraw] SetCooperativeLevel(hwnd=%p, flags=%#x)  exclusive=%d fullscreen=%d normal=%d",
            hwnd, flags,
            (flags & DDSCL_EXCLUSIVE) != 0,
            (flags & DDSCL_FULLSCREEN) != 0,
            (flags & DDSCL_NORMAL) != 0);
    return orig_SetCooperativeLevel(self, hwnd, flags);
}

// ---- IDirectDraw::SetDisplayMode ----------------------------------------
// SAFE-MODE: log only. We need to first understand what cooperative-level
// state Sacred is in when this is called before deciding how to handle it.
static HRESULT WINAPI hook_SetDisplayMode(IDirectDraw* self, DWORD w, DWORD h, DWORD bpp) {
    if (sdk::hooks::g_force.hd) {
        // SWALLOW. We run borderless-windowed at W×H; the W×H surfaces
        // already carry the resolution. Actually changing the exclusive
        // display mode to W×H is what fails ("Error initializing
        // DirectX") when the GPU/DDraw legacy path rejects it — exactly
        // ReBorn's "use windowed" guidance. Pretend success, change
        // nothing.
        sdk_log("[ddraw][HD] SetDisplayMode SWALLOWED (req %lux%lu@%lu) "
                "— windowed HD, surfaces already W×H", w, h, bpp);
        return DD_OK;
    }
    sdk_log("[ddraw] SetDisplayMode pass-through (%lux%lu @%lubpp)", w, h, bpp);
    return orig_SetDisplayMode(self, w, h, bpp);
}

// ReBorn HD part 2 (load-bearing): the engine's display-init mode lookup
// (FUN_00644130) only matches a resolution that DirectDraw ENUMERATED.
// Our patched-in W×H is not a real desktop mode, so we append synthetic
// W×H entries (16 & 32 bpp) to whatever the driver reports. Without this
// the lookup returns 0 → dxDisplay init fails. (Spec: render_dims.md §2)
static HRESULT WINAPI hook_EnumDisplayModes(IDirectDraw* self, DWORD flags,
                                            LPDDSURFACEDESC reqDesc,
                                            LPVOID ctx,
                                            LPDDENUMMODESCALLBACK cb)
{
    HRESULT hr = orig_EnumDisplayModes(self, flags, reqDesc, ctx, cb);
    if (sdk::hooks::g_force.hd && cb &&
        sdk::hooks::g_force.width > 0 && sdk::hooks::g_force.height > 0) {
        DWORD W = (DWORD)sdk::hooks::g_force.width;
        DWORD H = (DWORD)sdk::hooks::g_force.height;
        const DWORD bpps[2] = { 16, 32 };
        for (int i = 0; i < 2; i++) {
            DDSURFACEDESC d; ZeroMemory(&d, sizeof(d));
            d.dwSize  = sizeof(DDSURFACEDESC);
            d.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT
                      | DDSD_PITCH | DDSD_REFRESHRATE;
            d.dwWidth = W; d.dwHeight = H;
            d.lPitch  = (LONG)(W * (bpps[i] / 8));
            d.dwRefreshRate = 60;
            d.ddpfPixelFormat.dwSize  = sizeof(DDPIXELFORMAT);
            d.ddpfPixelFormat.dwFlags = DDPF_RGB;
            d.ddpfPixelFormat.dwRGBBitCount = bpps[i];
            sdk_log("[ddraw][HD] EnumDisplayModes inject %lux%lu@%lu",
                    W, H, bpps[i]);
            cb(&d, ctx);   // ignore DDENUMRET — best-effort append
        }
    }
    return hr;
}

// ---- HD diagnostic: dump the engine's canonical render dims --------------
// dxDisplay = *(void**)0x00CDCA1C (set @0x00816c9d). Render W/H live at
// dxDriver+0x20 / +0x1c (dxDriver is the dxDisplay subobject). We dump a wide
// window of the object as dwords + flag any field == 1920/1080/1024/768 so we
// can SEE whether the engine canonical dims went to HD (1920x1080) and locate
// the 2D-layer dim field that stayed 1024x768. (Only runs under enable_hd.)
// Scan the engine's live .data for adjacent dword pairs holding a W,H (or H,W)
// resolution, so we can SEE which globals are HD (1920x1080) vs still-2D
// (1024x768). The 1024x768 hits are the un-patched 2D-layer dim source(s).
static void hd_scan_data_for_dims(uintptr_t base) {
    auto* dos = (const IMAGE_DOS_HEADER*)base;
    auto* nt  = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t dstart = 0, dend = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (memcmp(sec[i].Name, ".data", 5) == 0) {
            dstart = base + sec[i].VirtualAddress;
            dend   = dstart + sec[i].Misc.VirtualSize;
            break;
        }
    }
    if (!dstart) { sdk_log("[hd-diag] no .data section"); return; }
    sdk_log("[hd-diag] scanning live .data [%p,%p) for W/H dim globals", (void*)dstart, (void*)dend);
    auto isW = [](uint32_t a, uint32_t b){
        return (a==1024&&b==768)||(a==768&&b==1024)||(a==800&&b==600)||(a==600&&b==800); };
    int n2d=0, nhd=0, n2w=0, nhw=0;
    for (uintptr_t p = dstart; p + 8 <= dend; p += 1) {        // byte-granular (catch misaligned)
        // dword pairs
        if ((p & 3) == 0 && p + 8 <= dend) {
            uint32_t a=*(uint32_t*)p, b=*(uint32_t*)(p+4);
            if (isW(a,b) && n2d<40){ sdk_log("[hd-diag]  2D-DWORD @%p=(%u,%u)",(void*)p,a,b); ++n2d; }
            if (((a==1920&&b==1080)||(a==1080&&b==1920)) && nhd<40){ sdk_log("[hd-diag]  HD-DWORD @%p=(%u,%u)",(void*)p,a,b); ++nhd; }
        }
        // word pairs (16-bit W,H)
        uint16_t wa=*(uint16_t*)p, wb=*(uint16_t*)(p+2);
        if (((wa==1024&&wb==768)||(wa==768&&wb==1024)||(wa==800&&wb==600)||(wa==600&&wb==800)) && n2w<40)
            { sdk_log("[hd-diag]  2D-WORD  @%p=(%u,%u)",(void*)p,wa,wb); ++n2w; }
        if (((wa==1920&&wb==1080)||(wa==1080&&wb==1920)) && nhw<40)
            { sdk_log("[hd-diag]  HD-WORD  @%p=(%u,%u)",(void*)p,wa,wb); ++nhw; }
    }
    sdk_log("[hd-diag] done: 2D dword=%d word=%d | HD dword=%d word=%d", n2d, n2w, nhd, nhw);
}

static DWORD WINAPI hd_dims_diag(LPVOID) {
    Sleep(12000);                       // let display init + first frames settle
    __try {
        void* dxDisplay = *(void**)0x00CDCA1C;
        sdk_log("[hd-diag] dxDisplay(*0xCDCA1C)=%p  (+0x1c=H +0x20=W)", dxDisplay);
        if (dxDisplay) {
            uint32_t* o = (uint32_t*)dxDisplay;
            sdk_log("[hd-diag] dxDisplay dims: H=%u W=%u pitch=%u", o[7], o[8], o[9]);
            // The dims object is HEAP, not .data — scan it widely for any field
            // still holding 1024/768 (the world viewport rect / 2D clip etc.).
            for (int i = 0; i < 0x400/4; ++i) {
                uint32_t v = o[i];
                const char* t = (v==1024)?" <==1024":(v==768)?" <==768":
                                (v==1920)?" <==1920":(v==1080)?" <==1080":
                                (v==800)?" <==800":(v==600)?" <==600":"";
                if (*t) sdk_log("[hd-diag]  dxDisplay+0x%03x = %u%s", i*4, v, t);
            }
        }
        // ReBorn reportedly has dim globals at 0xA1AD20/24 — check our build.
        __try {
            uint32_t a = *(uint32_t*)0x00A1AD20, b = *(uint32_t*)0x00A1AD24;
            sdk_log("[hd-diag] [0xA1AD20]=%u [0xA1AD24]=%u (ReBorn dim-global slot)", a, b);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        uintptr_t base = (uintptr_t)g_attach.exe_module;
        if (base) hd_scan_data_for_dims(base);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[hd-diag] faulted");
    }
    return 0;
}

// ---- Public install -----------------------------------------------------
//
// SAFE-MODE: vtable patching DISABLED. First attempt crashed Sacred ~5-10s
// in (after our patches+logger went live). Sacred uses
// CreateSurface(PRIMARY|COMPLEX, backbuffers=N) which requires DDSCL_EXCLUSIVE
// — and even pure-passthrough vtable hooks on IDirectDraw turned out to be
// risky here. Until we get a clean log of Sacred's DDraw flow, we don't
// touch the vtable.
//
// Gated by sdk.ini key `enable_ddraw_vtable=1`. Off by default.
void install(void* idirectdraw) {
    if (g_installed) return;
    if (!idirectdraw) {
        sdk_log("[ddraw] install: null IDirectDraw");
        return;
    }
    g_dd = (IDirectDraw*)idirectdraw;
    g_installed = true;

    // ReBorn HD path: patch ONLY the IDirectDraw-level CreateSurface +
    // SetDisplayMode vtable slots to rewrite dims to W×H. We deliberately
    // do NOT touch SetCooperativeLevel or the surface Blt vtable — that
    // combination crashed Sacred before (it needs EXCLUSIVE + COMPLEX
    // primary). CreateSurface/SetDisplayMode passthrough+dim-rewrite is
    // exactly ReBorn's mechanism and the minimal, safer subset.
    const bool hd      = sdk::hooks::g_force.hd &&
                         sdk::hooks::g_force.width > 0 && sdk::hooks::g_force.height > 0;
    const bool stretch = sdk::hooks::g_force.stretch;

    // We need the IDirectDraw::CreateSurface hook for BOTH modes:
    //  - HD: to rewrite surface dims to W×H (true HD render).
    //  - stretch: to capture the PRIMARY surface and hook its Blt (present
    //    stretch). In stretch mode hook_CreateSurface does NOT rewrite dims
    //    (gated on g_force.hd), it only records g_primary + patches its Blt.
    if (hd || stretch) {
        void** vt = *(void***)idirectdraw;
        orig_CreateSurface = (CreateSurface_t)
            patch_vtable_slot(vt, IDD_CreateSurface, (void*)&hook_CreateSurface);
    }
    if (hd) {
        void** vt = *(void***)idirectdraw;
        orig_SetDisplayMode = (SetDisplayMode_t)
            patch_vtable_slot(vt, IDD_SetDisplayMode, (void*)&hook_SetDisplayMode);
        orig_EnumDisplayModes = (EnumDisplayModes_t)
            patch_vtable_slot(vt, IDD_EnumDisplayModes,(void*)&hook_EnumDisplayModes);
        sdk_log("[ddraw] install: IDirectDraw=%p — HD ENABLED %dx%d",
                idirectdraw, sdk::hooks::g_force.width, sdk::hooks::g_force.height);
        HANDLE hdg = CreateThread(nullptr, 0, hd_dims_diag, nullptr, 0, nullptr);
        if (hdg) CloseHandle(hdg);
    }
    if (stretch && !hd) {
        sdk_log("[ddraw] install: IDirectDraw=%p — STRETCH mode (native render, "
                "Blt-stretch present to window client rect)", idirectdraw);
    }
    if (!hd && !stretch) {
        sdk_log("[ddraw] install: IDirectDraw=%p — SAFE-MODE (no hd/stretch)",
                idirectdraw);
    }
}

}} // namespace sdk::ddraw_hooks
