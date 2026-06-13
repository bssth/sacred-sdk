// frame_cap.cpp — single-frame handoff from the DDraw present hook (producer,
// game thread) to the overlay's bilinear upscaler (consumer, overlay thread).
//
// The producer Lock()s the engine's native-size present source surface and
// memcpy's its raw bytes here (fast, no conversion). The consumer pulls the
// latest frame, converts to BGRA8 and uploads it to a D3D11 texture, then draws
// it full-screen with a linear sampler — smoothing the 1024x768→screen upscale
// that DirectDraw does with point sampling.
//
// One slot, mutex-guarded, seq-numbered so the consumer only re-uploads on a new
// frame. Mutex is held only for the memcpy (no D3D / no conversion under lock).

#include "sdk.h"
#include <vector>

namespace sdk { namespace framecap {

volatile bool g_enabled = false;

static CRITICAL_SECTION g_cs;
static bool             g_cs_init = false;
static std::vector<uint8_t> g_buf;     // raw rows, tightly packed (w*bytespp)
static int   g_w = 0, g_h = 0, g_bpp = 0, g_rowbytes = 0;
static long  g_seq = 0;                // bumped each publish
static long  g_taken = -1;             // last seq the consumer copied

static void ensure_cs() {
    if (!g_cs_init) { InitializeCriticalSection(&g_cs); g_cs_init = true; }
}

void publish(const void* bits, int width, int height, int pitch, int bpp) {
    if (!bits || width <= 0 || height <= 0 || (bpp != 16 && bpp != 32)) return;
    ensure_cs();
    EnterCriticalSection(&g_cs);
    const int bytespp  = bpp / 8;
    const int rowbytes = width * bytespp;
    g_buf.resize((size_t)rowbytes * height);
    const uint8_t* s = (const uint8_t*)bits;
    for (int y = 0; y < height; ++y)
        memcpy(&g_buf[(size_t)y * rowbytes], s + (size_t)y * pitch, rowbytes);
    g_w = width; g_h = height; g_bpp = bpp; g_rowbytes = rowbytes;
    ++g_seq;
    LeaveCriticalSection(&g_cs);
}

bool peek_dims(int* width, int* height, int* bpp) {
    if (!g_cs_init) return false;
    EnterCriticalSection(&g_cs);
    bool ok = (g_seq > 0);
    if (ok) { if (width) *width = g_w; if (height) *height = g_h; if (bpp) *bpp = g_bpp; }
    LeaveCriticalSection(&g_cs);
    return ok;
}

bool take(void* dst, int dstPitch) {
    if (!g_cs_init || !dst) return false;
    EnterCriticalSection(&g_cs);
    bool fresh = (g_seq != g_taken) && !g_buf.empty();
    if (fresh) {
        g_taken = g_seq;
        uint8_t* d = (uint8_t*)dst;
        for (int y = 0; y < g_h; ++y)
            memcpy(d + (size_t)y * dstPitch, &g_buf[(size_t)y * g_rowbytes], g_rowbytes);
    }
    LeaveCriticalSection(&g_cs);
    return fresh;
}

}} // namespace sdk::framecap
