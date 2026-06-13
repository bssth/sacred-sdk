// hero_save_probe.cpp — one-shot self-test of the WIRED PAX hero-save port.
// (Goal D — first port wired into the build.) Reads a real save, inflates the
// C7 stat section through the engine's OWN zlib uncompress (no zlib1.dll), and logs the hero's class/level/
// gold/XP — proving the whole sacred.read_save chain works end-to-end against
// real data. DEV-ONLY: gated behind sdk.ini `selftest=1` (OFF by default — not
// everyone has .pax saves). When off, read_hero_save / sacred.read_save still
// work on demand. Runs once on a worker ~6 s after attach when enabled.
#include "sdk.h"
#include "ports/herosave/hero_save.h"
#include "ports/herosave/hero_stats.h"
#include "ports/herosave/zlib_codec.h"   // IsZlibLinked()
#include <vector>

namespace sdk {

static void hero_save_run_test() {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* s = strrchr(exe, '\\'); if (s) *s = 0;

    const char* names[] = { "save\\hero06.pax", "save\\Hero00.pax", "save\\hero07.pax" };
    int tried = 0;
    for (const char* nm : names) {
        char path[MAX_PATH];
        _snprintf_s(path, _TRUNCATE, "%s\\%s", exe, nm);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        tried++;
        wchar_t wpath[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

        using namespace sacred::herosave;
        PaxFile f;
        LoadStatus st = f.Load(wpath);
        if (st != LoadStatus::Ok) {
            sdk_log("[herosave] %s -> Load FAILED (status=%d, zlibLinked=%d)",
                    nm, (int)st, IsZlibLinked() ? 1 : 0);
            continue;
        }
        const std::vector<uint8_t>* c7 = f.SectionPtr(kTypeHeroInfo);
        if (!c7 || c7->size() <= c7::kLevel + 2) {
            sdk_log("[herosave] %s -> Load OK but C7 missing/short (%zu B)",
                    nm, c7 ? c7->size() : (size_t)0);
            continue;
        }
        const uint8_t* p = c7->data();
        uint8_t  cls   = p[c7::kClass];
        uint32_t xp    = *(const uint32_t*)(p + c7::kExperience);
        uint32_t gold  = *(const uint32_t*)(p + c7::kGold);
        uint16_t level = *(const uint16_t*)(p + c7::kLevel);
        const char* cname = player::class_name((uint16_t)cls);
        sdk_log("[herosave] %s OK -> UW=%d class=%u(%s) level=%u gold=%u xp=%u "
                "(C7=%zu B) -- PAX read + zlib inflate WORKS",
                nm, (int)f.IsUnderworld(), cls, cname ? cname : "?",
                level, gold, xp, c7->size());
    }
    if (tried == 0) sdk_log("[herosave] self-test: no save\\*.pax found to test");
}

// Inner parse (has C++ objects → no __try here; SEH is in the wrapper below).
static bool parse_pax(const wchar_t* wpath, HeroSaveInfo& out) {
    using namespace sacred::herosave;
    PaxFile f;
    if (f.Load(wpath) != LoadStatus::Ok) return false;
    const std::vector<uint8_t>* c7 = f.SectionPtr(kTypeHeroInfo);
    if (!c7 || c7->size() <= c7::kLevel + 2) return false;
    const uint8_t* p = c7->data();
    out.ok         = true;
    out.underworld = f.IsUnderworld() ? 1 : 0;
    out.class_id   = p[c7::kClass];
    out.xp         = *(const uint32_t*)(p + c7::kExperience);
    out.gold       = *(const uint32_t*)(p + c7::kGold);
    out.level      = *(const uint16_t*)(p + c7::kLevel);
    out.c7_size    = (unsigned)c7->size();
    const char* cn = player::class_name((uint16_t)out.class_id);
    if (cn) strncpy_s(out.class_name, _TRUNCATE, cn, _TRUNCATE);
    return true;
}

// Public: read a hero .pax's headline stats (backs sacred.read_save). `path`
// absolute, or relative to the game dir. SEH-guarded; false on any failure.
bool read_hero_save(const char* path, HeroSaveInfo& out) {
    out = HeroSaveInfo{};
    if (!path || !*path) return false;
    char abs[MAX_PATH];
    if (path[1] == ':' || path[0] == '\\') {            // already absolute
        strncpy_s(abs, _TRUNCATE, path, _TRUNCATE);
    } else {                                            // relative to game dir
        char exe[MAX_PATH] = {0};
        GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
        char* s = strrchr(exe, '\\'); if (s) *s = 0;
        _snprintf_s(abs, _TRUNCATE, "%s\\%s", exe, path);
    }
    if (GetFileAttributesA(abs) == INVALID_FILE_ATTRIBUTES) return false;
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, abs, -1, wpath, MAX_PATH);
    __try { return parse_pax(wpath, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out = HeroSaveInfo{}; return false; }
}

static DWORD WINAPI hero_save_probe_worker(LPVOID) {
    Sleep(6000);                 // let the game settle (engine zlib resolves lazily)
    __try { hero_save_run_test(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[herosave] self-test faulted");
    }
    return 0;
}

void start_hero_save_probe() {
    HANDLE h = CreateThread(nullptr, 0, hero_save_probe_worker, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

} // namespace sdk
