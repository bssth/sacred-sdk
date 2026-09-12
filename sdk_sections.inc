// sdk_sections.inc — included by runtime_triggers.cpp, inside namespace
// sdk::runtime_triggers. SDK-owned script SECTIONS: our own Dialog: nodes and
// button handlers, served to the engine without touching any .bin file.
//
// How the engine runs a section (.claude/knowledge/quests/RE_dialog_buttons.md
// §3, §8.1, §8.2):
//   talk     FUN_0052AB70 -> FUN_0046b480 finds "Dialog:<DlgNPC name>" by name
//            -> WorkFunktion FUN_0046BA90(section, npc, 0)
//   button   FUN_006e74e0 -> FUN_00499da0 -> WorkFunktion(section, 0, 0), the
//            section resolved from the SetButton action name by FUN_00460590
//   WorkFunktion: fseek(FunkCode FILE, entry+0x40) + fread(stackbuf, 1,
//            entry+0x44) -> the record walker FUN_00475680 over the buffer.
//
// So an SDK section is one appended 0x54-byte entry in the engine's section
// table (DAT_00aab708: name[64], +0x40 start, +0x44 len, +0x48 owner quest,
// +0x4c 0, +0x50 run-once latch) whose start is a VIRTUAL offset (>= VBASE) into
// our own byte store, plus WorkFunktion's two CRT calls (fseek at 0x46bce1,
// fread at 0x46bd01, both direct E8 rel32) redirected to wrappers that serve
// our bytes for virtual offsets and pass every real offset straight through.
// Every engine lookup then finds our names unchanged. Running one of our
// sections also queues the Lua trigger "SECTION:<name>" for the next tick.
//
// The table is rebuilt on every world load (FUN_0046f9b0 resizes it to the
// Vectoren.bin count), so sections_tick() re-appends ours whenever they are
// missing. The grown table is a block from the engine's own operator new, so
// its vector code can later reallocate or free it; the block it replaces is
// deliberately left allocated, so no stale pointer can ever read freed memory.

#include <string.h>

void fire(const char* trigger_name);                  // defined below

constexpr uint32_t SDK_SEC_VBASE  = 0x40000000u;
constexpr int      SDK_SEC_MAX    = 256;
constexpr uint32_t SDK_SEC_STORE  = 256u * 1024u;
constexpr uint32_t SDK_SEC_MAXLEN = 0x3F00u;         // WorkFunktion's stack buffer is ~16 KB

struct SdkSection { char name[64]; uint32_t off; uint32_t len; int32_t owner; };
static SdkSection    g_sec[SDK_SEC_MAX];
static int           g_sec_n        = 0;
static uint8_t       g_sec_store[SDK_SEC_STORE];
static uint32_t      g_sec_used     = 0;

static uintptr_t     g_sec_block    = 0;              // our table block (0 = not injected yet)
static unsigned      g_sec_vanilla  = 0;              // engine entries below ours
static int           g_sec_injected = 0;              // how many of ours are in the block
static unsigned      g_sec_gen      = 0;              // bumped on every (re)inject: our indices moved
static bool          g_sec_patched  = false;
static bool          g_sec_warned   = false;

static volatile LONG g_sec_q[64];                     // run sections -> Lua callbacks
static volatile LONG g_sec_qh = 0, g_sec_qt = 0;
static volatile int  g_sec_armed    = -1;             // fseek -> fread hand-off

typedef int    (__cdecl* crt_fseek_t)(void* f, long off, int whence);
typedef size_t (__cdecl* crt_fread_t)(void* buf, size_t sz, size_t n, void* f);
typedef void*  (__cdecl* op_new_t)(size_t n);
static const crt_fseek_t kCrtFseek = (crt_fseek_t)0x008498CD;
static const crt_fread_t kCrtFread = (crt_fread_t)0x00849986;
static const op_new_t    kOpNew    = (op_new_t)0x00849842;

// A virtual offset we do not own can only come from a stale entry. Never let
// the walker loose on an uninitialised stack buffer: fill it with padding
// records (tag 0 = no-op, size 3) so it steps through harmlessly.
static void sec_fill_padding(uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)((i % 3) == 2 ? 3 : 0);
}

static int __cdecl sdk_wf_fseek(void* f, long off, int whence) {
    if (whence == 0 && (uint32_t)off >= SDK_SEC_VBASE) {
        uint32_t o = (uint32_t)off - SDK_SEC_VBASE;
        int hit = -2;                                  // -2 = virtual, unknown
        for (int i = 0; i < g_sec_n; i++) if (g_sec[i].off == o) { hit = i; break; }
        g_sec_armed = hit;
        if (hit >= 0) {
            LONG t = g_sec_qt;
            if (t - g_sec_qh < 64) { g_sec_q[t & 63] = hit; g_sec_qt = t + 1; }
        }
        return 0;
    }
    g_sec_armed = -1;
    return kCrtFseek(f, off, whence);
}

static size_t __cdecl sdk_wf_fread(void* buf, size_t sz, size_t n, void* f) {
    int i = g_sec_armed;
    if (i != -1) {
        g_sec_armed = -1;
        size_t want = sz * n;
        if (i < 0) { sec_fill_padding((uint8_t*)buf, want); return n; }
        size_t have = g_sec[i].len, k = want < have ? want : have;
        if (k) memcpy(buf, g_sec_store + g_sec[i].off, k);
        if (k < want) sec_fill_padding((uint8_t*)buf + k, want - k);
        return sz ? k / sz : 0;
    }
    return kCrtFread(buf, sz, n, f);
}

// Rewrite `call rel32` at `site` from `expect` to `to`. Refuses unless the
// bytes are exactly the expected call, so a changed build is left alone.
static bool sec_call_is(uintptr_t site, uintptr_t expect) {
    const uint8_t* p = (const uint8_t*)site;
    return p[0] == 0xE8 && site + 5 + *(const int32_t*)(p + 1) == expect;
}
static void sec_call_write(uintptr_t site, void* to) {
    uint8_t* p = (uint8_t*)site;
    DWORD old = 0;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)to - (site + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

static void sec_write_entry(uint8_t* e, const SdkSection& s) {
    memset(e, 0, 0x54);
    strncpy_s((char*)e, 64, s.name, _TRUNCATE);
    *(uint32_t*)(e + 0x40) = SDK_SEC_VBASE + s.off;
    *(uint32_t*)(e + 0x44) = s.len;
    *(int32_t*) (e + 0x48) = s.owner;                  // -1 = global: no quest gate
}

static void sections_inject() {
    if (g_sec_n == 0) return;
    __try {
        uintptr_t b = *(uintptr_t*)0x00AAB708, e = *(uintptr_t*)0x00AAB70C;
        if (!b || e < b) return;
        unsigned n = (unsigned)((e - b) / 0x54);
        bool ours = (b == g_sec_block) && (n == g_sec_vanilla + (unsigned)g_sec_injected);
        if (ours && g_sec_injected == g_sec_n) return;           // up to date
        unsigned vanilla = ours ? g_sec_vanilla : n;             // else the engine rebuilt it
        unsigned total   = vanilla + (unsigned)g_sec_n;
        uint8_t* blk = (uint8_t*)kOpNew((size_t)total * 0x54);
        if (!blk) { sdk_log("[sections] operator new(%u) failed", total * 0x54); return; }
        memcpy(blk, (void*)b, (size_t)vanilla * 0x54);
        for (int i = 0; i < g_sec_n; i++)
            sec_write_entry(blk + (size_t)(vanilla + (unsigned)i) * 0x54, g_sec[i]);
        *(uintptr_t*)0x00AAB708 = (uintptr_t)blk;
        *(uintptr_t*)0x00AAB70C = (uintptr_t)(blk + (size_t)total * 0x54);
        *(uintptr_t*)0x00AAB710 = (uintptr_t)(blk + (size_t)total * 0x54);
        g_sec_block = (uintptr_t)blk; g_sec_vanilla = vanilla; g_sec_injected = g_sec_n;
        g_sec_gen++;                                   // our indices moved: sdk_quests.inc re-resolves
        sdk_log("[sections] table %u -> %u entries (SDK sections #%u..#%u)",
                vanilla, total, vanilla, total - 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[sections] inject faulted");
    }
}

// Called from heartbeat(), outside the Lua guard, on the game thread.
static void sections_tick() {
    if (!g_sec_patched) {
        HMODULE exe = g_attach.exe_module;
        if (exe && (uintptr_t)exe == 0x00400000) {
            bool a = sec_call_is(0x0046BCE1, 0x008498CD);
            bool r = sec_call_is(0x0046BD01, 0x00849986);
            if (a && r) {                                            // both or neither
                sec_call_write(0x0046BCE1, (void*)&sdk_wf_fseek);
                sec_call_write(0x0046BD01, (void*)&sdk_wf_fread);
                g_sec_patched = true;
                sdk_log("[sections] WorkFunktion fseek/fread redirected (0x46bce1, 0x46bd01)");
            } else if (!g_sec_warned) {
                g_sec_warned = true;
                sdk_log("[sections] WorkFunktion call sites not as expected (fseek %d, fread %d): "
                        "SDK sections disabled", a ? 1 : 0, r ? 1 : 0);
            }
        }
    }
    if (g_sec_patched) sections_inject();
    while (g_sec_qh != g_sec_qt) {
        LONG h = g_sec_qh;
        int i = (int)g_sec_q[h & 63];
        g_sec_qh = h + 1;
        if (i >= 0 && i < g_sec_n) {
            char tok[80];
            _snprintf_s(tok, _TRUNCATE, "SECTION:%s", g_sec[i].name);
            fire(tok);
        }
    }
}

static int sec_find(const char* name) {
    for (int i = 0; i < g_sec_n; i++) if (_stricmp(g_sec[i].name, name) == 0) return i;
    return -1;
}

// sacred.section_define(name, bytes [, owner_quest]) -> SDK section number
// Registers (or replaces) a script section built from raw record bytes (see
// custom/lua/lib/sections.lua). It enters the engine's section table on the
// next tick and is found by name like any vanilla section. `owner_quest` is the
// section entry's +0x48: -1 (the default) is a global section, a quest id makes
// WorkFunktion refuse to run it until that quest is set up, exactly as it gates
// the 6,196 quest-owned vanilla sections (MECHANICS §1.4).
static int l_sacred_section_define(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* bytes = luaL_optlstring(L, 2, "", &len);
    int32_t owner = (int32_t)luaL_optinteger(L, 3, -1);
    size_t nl = strlen(name);
    if (nl < 1 || nl > 63) return luaL_error(L, "section_define: name must be 1..63 chars");
    if (len > SDK_SEC_MAXLEN) return luaL_error(L, "section_define: %d bytes > %d", (int)len, (int)SDK_SEC_MAXLEN);
    uint32_t need = (uint32_t)len + 1;                 // +1 keeps every offset unique
    if (g_sec_used + need > SDK_SEC_STORE) return luaL_error(L, "section_define: store full");
    int i = sec_find(name);
    bool fresh = (i < 0);
    if (fresh) {
        if (g_sec_n >= SDK_SEC_MAX) return luaL_error(L, "section_define: too many sections");
        i = g_sec_n;
        strncpy_s(g_sec[i].name, name, _TRUNCATE);
    }
    memcpy(g_sec_store + g_sec_used, bytes, len);
    g_sec_store[g_sec_used + len] = 0;
    g_sec[i].off   = g_sec_used;
    g_sec[i].len   = (uint32_t)len;
    g_sec[i].owner = owner;
    g_sec_used += need;
    if (fresh) g_sec_n++;
    if (g_sec_block && i < g_sec_injected) {           // redefinition: keep the live entry in step
        __try {
            uint8_t* e = (uint8_t*)(g_sec_block + (size_t)(g_sec_vanilla + (unsigned)i) * 0x54);
            *(uint32_t*)(e + 0x40) = SDK_SEC_VBASE + g_sec[i].off;
            *(uint32_t*)(e + 0x44) = g_sec[i].len;
            *(int32_t*) (e + 0x48) = g_sec[i].owner;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    sdk_log("[sections] define '%s' %u bytes owner=%d (sdk #%d%s)",
            name, (unsigned)len, owner, i, fresh ? "" : ", replaced");
    lua_pushinteger(L, i);
    return 1;
}
