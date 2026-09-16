// hd/geometry.cpp — see geometry.h.
//
// Every assignment below mirrors one instruction of ReBorn's two init routines
// (their .rsrc tail, 0x1AC8500 for the width half and 0x1AC9540 for the height
// half). The comment on each line is the slot's ReBorn address, which is how
// the generated patch records name it.
#include "../sdk.h"
#include "geometry.h"
#include "../core/config.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace sdk { namespace hd {

static uint8_t  g_block[kSlotHi - kSlotLo];
static bool     g_ready = false;
static int      g_w = 1024, g_h = 768;
static char     g_status[128] = "hd: not initialised";

static inline void put_u32(uint32_t va, uint32_t v) {
    if (va < kSlotLo || va + 4 > kSlotHi) return;
    memcpy(g_block + (va - kSlotLo), &v, 4);
}
static inline void put_i(uint32_t va, int32_t v)  { put_u32(va, (uint32_t)v); }
static inline void put_f(uint32_t va, float v)    { uint32_t b; memcpy(&b, &v, 4); put_u32(va, b); }
static inline void put_d(uint32_t va, double v) {
    if (va < kSlotLo || va + 8 > kSlotHi) return;
    memcpy(g_block + (va - kSlotLo), &v, 8);
}

// ReBorn rounds the camera zoom to 1/100 through the x87 stack: multiply by
// 100, store as int, load back, divide by 100.
static float round_100(double v) {
    return (float)(((double)(int32_t)llround(v * 100.0)) / 100.0);
}

void init(int w, int h) {
    if (w <= 0) w = 1024;
    if (h <= 0) h = 768;
    g_w = w; g_h = h;
    memset(g_block, 0, sizeof(g_block));

    const int32_t W = w, H = h;
    const int32_t halfW = (int32_t)((uint32_t)W >> 1), halfH = (int32_t)((uint32_t)H >> 1);
    const int32_t offX = (W - 1024) >> 1, offY = (H - 768) >> 1;   // arithmetic, as theirs
    const float   fW = (float)W, fH = (float)H;
    const float   fOffX = (float)offX, fOffY = (float)offY;

    put_i(0xA1EFD0, W);           put_i(0xA1EFD4, H);
    put_i(0xA1EFD8, halfW);       put_i(0xA1EFDC, halfH);
    put_i(0xA1F020, -halfW);      put_i(0xA1F024, -halfH);
    put_i(0xA1F078, -halfW * 2);  put_i(0xA1F0C0, -halfH * 2);

    put_f(0xA1EFE0, fW);          put_f(0xA1EFE4, fH);
    put_f(0xA1EFE8, (float)halfW); put_f(0xA1EFEC, (float)halfH);
    put_f(0xA1EFF0, (float)-halfW); put_f(0xA1EFF4, (float)-halfH);

    // 267 and 200 are the engine's own half-extents of the play field.
    const float sx = 267.0f * fW / 1024.0f;
    const float sy = 200.0f * fH / 768.0f;
    put_f(0xA1EFF8, sx);  put_d(0xA1F010,  (double)sx);
    put_f(0xA1EFFC, -sx); put_d(0xA1F018, -(double)sx);
    put_f(0xA1F000, sy);  put_d(0xA1F0A0,  (double)sy);
    put_f(0xA1F004, -sy); put_d(0xA1F0A8, -(double)sy);

    put_f(0xA1F008, 1.0f / fW);        put_f(0xA1F0B0, 1.0f / fH);
    put_f(0xA1EFC4, 1024.0f / fW);     put_f(0xA1EFC8, 768.0f / fH);

    put_i(0xA1F00C, W + 200);          put_i(0xA1F0CC, H + 200);

    put_i(0xA1F054, offX);             put_f(0xA1F050, fOffX);
    put_i(0xA1F0BC, offY);             put_f(0xA1F0B8, fOffY);

    put_f(0xA1F05C, 501.0f + fOffX);   put_f(0xA1F060, 522.0f + fOffX);
    put_i(0xA1F058, 162 + offX);       put_i(0xA1F064, 170 + offX);
    put_f(0xA1F080, 1024.0f);
    put_f(0xA1F07C, fW - 70.0f);
    put_i(0xA1F070, W - 1);
    put_i(0xA1F074, H - 1);            // set to W-1 first by their code, then overwritten
    put_i(0xA1F068, W - 92);           put_i(0xA1F06C, W - 126);
    put_f(0xA1F098, fH + 50.0f);
    put_i(0xA1EFA8, offX + 105);       put_i(0xA1EFAC, 2 * offY + 700);
    put_f(0xA1F0C4, 379.0f + fOffY);   put_f(0xA1F0C8, 389.0f + fOffY);
    put_f(0xA1EF68, (float)halfW + 1.0f);
    put_f(0xA1EF60, fH - 58.0f);       put_f(0xA1EF64, fH - 58.0f + 1.0f);
    put_i(0xA1EF50, W - 32);           put_i(0xA1EF54, H - 32);
    put_i(0xA1EF58, 400 + offY);       put_i(0xA1EF5C, 680 + offX);
    put_f(0xA1F0D4, 710.0f + 2.0f * fOffY);

    // Camera zoom: only widened past 1366x768, and then every zoom step is
    // divided by how much wider than 4:3-at-1024 the frame got.
    if (W >= 1367) {
        const float z = round_100(((double)fW + (double)fH) / 2134.0);
        put_f(0xA1EF44, round_100(0.5 / z));
        put_f(0xA1EF48, round_100(1.0 / z));
        put_f(0xA1EF4C, round_100(2.0 / z));
    } else {
        put_f(0xA1EF44, 0.5f); put_f(0xA1EF48, 1.0f); put_f(0xA1EF4C, 2.0f);
    }

    g_ready = true;
    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                "hd geometry: %dx%d  offset %d,%d  zoom %.2f",
                W, H, offX, offY, *(float*)(g_block + (0xA1EF48 - kSlotLo)));
    sdk_log("[hd] %s", g_status);
}

void init_from_config() {
    int w = config::get_int("hd", "width", 0);
    int h = config::get_int("hd", "height", 0);
    if (w <= 0) w = atoi(config::game_setting("SR_HD_WIDTH", "0"));
    if (h <= 0) h = atoi(config::game_setting("SR_HD_HEIGHT", "0"));
    if (w <= 0 || h <= 0) { w = 1024; h = 768; }
    init(w, h);
}

bool ready()  { return g_ready; }
int  width()  { return g_w; }
int  height() { return g_h; }
const char* status() { return g_status; }

void* slot_addr(uint32_t va) {
    if (!g_ready || va < kSlotLo || va + 4 > kSlotHi) return nullptr;
    return g_block + (va - kSlotLo);
}

bool slot_u32(uint32_t va, uint32_t* out) {
    void* p = slot_addr(va);
    if (!p || !out) return false;
    memcpy(out, p, 4);
    return true;
}

}} // namespace sdk::hd
