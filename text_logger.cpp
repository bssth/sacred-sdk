// SacredSDK — live resource-lookup logger.
//
// Hooks FUN_0080eaf0 (va:0x0080EAF0) — the dictionary lookup that every
// `resource(id)` / `resource("NAME")` call funnels through after hashing.
// We log every unique hash queried + the result pointer (NULL on miss).
//
// Combined with our recovered hash function (sacred_hash.py) and the partial
// hash_names.csv, this gives us full live coverage of every text id the
// game actually uses, dramatically narrowing the search for un-named hashes.
//
// Implementation: classic trampoline detour.
//   1. Allocate executable memory for the trampoline.
//   2. Copy original first N (>=5) prologue bytes there + append a jmp back.
//   3. Overwrite original first 5 bytes with `jmp our_hook`.
//   4. Our hook calls trampoline (which runs the saved prologue then jumps
//      past the detour into the rest of the original).
//
// FUN_0080eaf0's prologue is the MSVC SEH-frame setup:
//     6A FF                push -1
//     68 10 A7 88 00       push 0x0088A710
//     64 A1 00 00 00 00    mov  eax, fs:[0]
//     50                   push eax
//     ...
// First 7 bytes (`6A FF 68 10 A7 88 00`) form two complete instructions, both
// pure stack pushes, position-independent. Save 7 bytes → trampoline.

#include "sdk.h"
#include "hooks/detour.h"   // Goal A2: unified trampoline-detour installer
#include <cstring>
#include <unordered_set>
#include <cstdio>
#include <intrin.h>         // _ReturnAddress / _AddressOfReturnAddress (call-chain trace)
#include "ports/engine/sacred_hash.h"   // sacred::engine::sacred_hash (dedup dialog names)

namespace sdk { namespace text_logger {

volatile bool g_active = false;
volatile long g_calls  = 0;
volatile long g_unique = 0;

// FUN_0080eaf0 prologue is __thiscall:
//   ECX = "this" pointer to the resource-manager singleton
//   [esp+4] = uint32_t hash key (already masked with 0x7FFFFFFF by caller)
// Returns entry pointer in EAX (NULL on miss). Stack cleanup: `ret 4`.
typedef void* (__fastcall *lookup_fn_t)(void* this_ptr, void* edx_dummy,
                                        uint32_t hash);

constexpr uintptr_t FUN_0080EAF0_RVA = 0x0080EAF0 - 0x00400000;
constexpr size_t    PROLOGUE_BYTES   = 7;  // 2 + 5: two pushes

static lookup_fn_t g_trampoline = nullptr;

// ── DIALOG-BODY CAPTURE on FUN_0080f5e0 ─────────────────────────────────────
// FUN_0080f5e0 is the by-key string fetch that returns the actual displayed
// wchar_t* (FUN_0080eaf0 funnels into it). Hooking it lets us log, during a
// short capture window, every (key -> resolved string) the engine fetches —
// so when the dialog window opens showing "murderer" in the BODY, we see the
// exact key that produced it and can redirect that key to our text hash. The
// window is armed by dialog_arm (covers the talk-open moment); capped + gated
// so it doesn't spam the per-frame UI lookups.
typedef wchar_t* (__fastcall* f5e0_fn_t)(void* this_ptr, void* edx, unsigned int key);
constexpr uintptr_t FUN_0080F5E0_RVA = 0x0080F5E0 - 0x00400000;
constexpr size_t    F5E0_PROLOGUE   = 5;   // 53 56 8b 31 57 (push;push;mov esi,[ecx];push)
static f5e0_fn_t    g_tramp_f5e0    = nullptr;
static volatile DWORD g_cap_until = 0;
static volatile long  g_cap_n     = 0;
constexpr long        CAP_MAX     = 250;

// DIALOG-TEXT REDIRECT. While a bound-NPC conversation is active (armed by
// dialog_arm), the talk window fetches the creature's VANILLA dialog line(s)
// through FUN_0080f5e0 (proven: key=06048666 -> "...track down the murderer..").
// Our replayed res:KEY never reaches the window. So instead we substitute the
// KEY at fetch time: when the engine asks for a vanilla dialog SENTENCE during
// the window, we re-fetch with OUR baked text hash and return that — the window
// then renders our text natively. UI labels (short / no spaces / "+++..") are
// filtered out so only real dialog sentences are swapped.
static volatile long     g_redir_hits   = 0;

// ── PERSISTENT vanilla-key -> our-hash REDIRECT MAP ─────────────────────────
// The talk window fetches the creature's VANILLA dialog line by KEY through
// FUN_0080f5e0 (Miles: key=06048666 -> "..track down the murderer.."). The fetch
// happens ~1ms BEFORE any reliable "this NPC is talking" signal (npc_in_dialog
// bit 0x400 is only a ~240ms pulse at the OK-click, AFTER the fetch), so we can
// NOT gate the swap on talk state at fetch time. Instead we apply a stable map
// {vanilla_key -> our_hash} UNCONDITIONALLY (no timing): whenever the engine
// fetches a mapped key, return our baked text. The map is LEARNED once per NPC
// (on the talk-close edge, from the last dialog sentence fetched) and PERSISTED
// to disk so it's permanent across sessions. Append-only + lock-free reads
// (entries fully written before g_map_n is bumped).
struct KeyMap { uint32_t key; uint32_t hash; };
static KeyMap            g_map[128];
static volatile long     g_map_n = 0;
static char              g_map_path[MAX_PATH] = {0};

static volatile uint32_t g_last_sentence_key = 0;   // last dialog sentence fetched
static volatile DWORD    g_last_sentence_t   = 0;

static uint32_t map_lookup(uint32_t k) {
    long n = g_map_n;
    for (long i = 0; i < n; i++) if (g_map[i].key == k) return g_map[i].hash;
    return 0;
}

static void map_persist_append(uint32_t key, uint32_t hash) {
    if (!g_map_path[0]) return;
    FILE* f = nullptr; fopen_s(&f, g_map_path, "ab");
    if (!f) return;
    fprintf(f, "%08x,%08x\n", key, hash);
    fclose(f);
}

static void map_add(uint32_t key, uint32_t hash, bool persist) {
    if (!key || !hash || map_lookup(key)) return;
    long n = g_map_n;
    if (n >= 128) return;
    g_map[n].key = key; g_map[n].hash = hash;
    g_map_n = n + 1;                      // publish AFTER fields written
    sdk_log("[dlgredir] map %skey=%08x -> hash=%08x (n=%ld)",
            persist ? "LEARNED " : "seed ", key, hash, g_map_n);
    if (persist) map_persist_append(key, hash);
}

// Learn the current NPC's vanilla dialog key from the last sentence the window
// fetched, mapping it to our_hash. Called on the talk-CLOSE edge (o:on_talk_end)
// where we know which NPC just finished — the sentence buffered ~1ms earlier is
// that NPC's line. Idempotent (skips if already mapped).
extern "C" void text_logger_learn(uint32_t our_hash) {
    our_hash &= 0x7FFFFFFFu;
    uint32_t k = g_last_sentence_key;
    if (!our_hash || !k) return;
    if (GetTickCount() - g_last_sentence_t > 5000) return;   // stale buffer
    map_add(k, our_hash, true);
}

// Capture-only arm (logs key->string for `ms`); redirect is map-based now.
extern "C" void text_logger_arm_dialog(uint32_t /*our_hash*/, unsigned ms) {
    g_cap_n     = 0;
    g_cap_until = GetTickCount() + ms;
}

// Heuristic: is this resolved string a real dialog SENTENCE (vs a UI label like
// "Price"/"Round Shield"/"+++EMPTY+++")? Require a space and a decent length.
static bool looks_like_sentence(const wchar_t* ws) {
    if (!ws) return false;
    int len = 0; bool space = false;
    for (const wchar_t* p = ws; *p && len < 4096; ++p, ++len)
        if (*p == L' ') space = true;
    if (len < 20 || !space) return false;
    if (ws[0] == L'+' || ws[0] == L'(') return false;   // "+++EMPTY+++"
    return true;
}

// FUN_00672cf0(ecx=obj, name) — the dialog BY-NAME text resolver (the walker
// calls it with the dialog line's resource NAME; it sacred_hashes the name and
// resolves global.res). Logging the NAME here reveals the vanilla dialog line
// names per NPC (what hashes to 06048666 etc.) — the native binding point. Our
// own probes call FUN_0080f5e0 by HASH, never this, so this is pollution-free.
typedef void* (__fastcall* f672cf0_t)(void* this_ptr, void* edx, const char* name);
constexpr uintptr_t FUN_00672CF0_RVA = 0x00672CF0 - 0x00400000;
constexpr size_t    F672CF0_PROLOGUE = 5;   // 8b 44 24 04 56
static f672cf0_t    g_tramp_672cf0   = nullptr;

// ── NATIVE DIALOG-NAME OVERRIDE ─────────────────────────────────────────────
// PROVEN: the talk window resolves the creature's VANILLA dialog line BY NAME
// through FUN_00672cf0 (the quest walker, ra ~0x4752xx). Captain Miles speaks
// quest DQ_15024's node "DQ_15024_OFFEN" (hash 06048666 = "..the murderer.."),
// Rocheford speaks "DQ_15024_ZIEL". To show OUR text we substitute the NAME the
// walker passes: vanilla node name -> our baked global.res name. The engine then
// sacred_hashes OUR name and resolves OUR text — fully native, per-node (each
// quest state maps to the right line), no timing/gating/pollution. Set by the
// mod via sacred.dialog_override(vanilla_name, our_name).
struct NameOverride { char from[64]; char to[64]; };
static NameOverride  g_ovr[64];
static volatile long g_ovr_n = 0;

extern "C" void text_logger_dialog_override(const char* from, const char* to) {
    if (!from || !to || !*from || !*to) return;
    // (1) Populate the FINAL-resolver HASH swap — the proven path. The talk
    // window resolves the vanilla node via SEVERAL resolvers (FUN_00672cf0 is
    // only one), but ALL funnel through FUN_0080f5e0(hash). So map
    // sacred_hash(from) -> sacred_hash(to): every resolution of the vanilla node,
    // by any path, returns OUR baked text. (This is what worked live in 79bae857;
    // now populated deterministically from the explicit override — no auto-learn.)
    uint32_t fh = sacred::engine::sacred_hash(from);
    uint32_t th = sacred::engine::sacred_hash(to);
    map_add(fh, th, false);
    // (2) Also register the by-NAME substitution at FUN_00672cf0 (belt+braces).
    long n = g_ovr_n;
    for (long i = 0; i < n; i++) if (!strcmp(g_ovr[i].from, from)) {   // update existing
        strncpy_s(g_ovr[i].to, to, _TRUNCATE); return; }
    if (n >= 64) return;
    strncpy_s(g_ovr[n].from, from, _TRUNCATE);
    strncpy_s(g_ovr[n].to,   to,   _TRUNCATE);
    g_ovr_n = n + 1;
    sdk_log("[dlgname] override \"%s\"(%08x) -> \"%s\"(%08x) (n=%ld)", from, fh, to, th, g_ovr_n);
}

static const char* override_lookup(const char* name) {
    long n = g_ovr_n;
    for (long i = 0; i < n; i++) if (!strcmp(g_ovr[i].from, name)) return g_ovr[i].to;
    return nullptr;
}

static void* __fastcall hook_FUN_00672cf0(void* this_ptr, void* edx, const char* name) {
    // FUN_00672cf0 resolves ALL by-name text; the QUEST DIALOG walker calls it
    // from ~0x4752xx-0x4754xx (proven by the dlgcaller trace). Only act there.
    uintptr_t ra = (uintptr_t)_ReturnAddress();
    if (name && ra >= 0x00475100 && ra < 0x00475680) {
        __try {
            // NATIVE OVERRIDE: swap the vanilla node name for our text name.
            const char* sub = override_lookup(name);
            if (sub) {
                sdk_log("[dlgname] override \"%s\" -> \"%s\"", name, sub);
                return g_tramp_672cf0(this_ptr, edx, sub);
            }
            // DISCOVERY: log each DISTINCT dialog line name once (dedup by hash),
            // so a modder can find the vanilla node names to override.
            char nm[96]; int j = 0;
            for (; j < 95 && name[j]; j++) {
                unsigned char c = (unsigned char)name[j];
                nm[j] = (c >= 0x20 && c < 0x7f) ? name[j] : '.';
            }
            nm[j] = 0;
            if (nm[0]) {
                uint32_t hh = sacred::engine::sacred_hash(nm);
                static uint32_t seen[256]; static volatile long seen_n = 0;
                bool dup = false; long n = seen_n;
                for (long i = 0; i < n; i++) if (seen[i] == hh) { dup = true; break; }
                if (!dup && n < 256) {
                    seen[n] = hh; seen_n = n + 1;
                    // Copy-paste hint for the modder: this is the vanilla node an
                    // NPC speaks; pass it as say()'s 2nd arg to show your own text.
                    sdk_log("[dlgname] vanilla node \"%s\" (hash %08x) — to override: "
                            "o:say(\"YOUR_TEXT_KEY\", \"%s\")", nm, hh, nm);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return g_tramp_672cf0(this_ptr, edx, name);
}

static wchar_t* __fastcall hook_FUN_0080f5e0(void* this_ptr, void* edx, unsigned int key) {
    uint32_t k = key & 0x7FFFFFFFu;

    // REDIRECT: a mapped vanilla key -> swap unconditionally for our baked text.
    uint32_t to = map_lookup(k);
    if (to && k != to) {
        wchar_t* ours = g_tramp_f5e0(this_ptr, edx, to);
        if (ours) {
            InterlockedIncrement(&g_redir_hits);
            sdk_log("[dlgredir] swap key=%08x -> hash=%08x", k, to);
            return ours;
        }
    }

    wchar_t* ws = g_tramp_f5e0(this_ptr, edx, key);

    // Buffer the last dialog SENTENCE fetched (UI labels filtered) so the
    // talk-close learn can map it to the NPC that just finished talking.
    if (!to && looks_like_sentence(ws)) {
        g_last_sentence_key = k;
        g_last_sentence_t   = GetTickCount();
    }

    if (g_cap_until && GetTickCount() < g_cap_until && g_cap_n < CAP_MAX) {
        InterlockedIncrement(&g_cap_n);
        char u[160]; u[0] = 0;
        if (ws) {
            __try { WideCharToMultiByte(CP_UTF8, 0, ws, -1, u, (int)sizeof(u), nullptr, nullptr); u[sizeof(u)-1]=0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { u[0]=0; }
        }
        sdk_log("[dlgcap] FUN_0080f5e0 key=%08x -> \"%.120s\"", key, ws ? u : "(null)");
    }
    return ws;
}

// ── DIALOG-TEXT WATCH (timestamped) ─────────────────────────────────────────
// FUN_0080eaf0 is the FINAL global.res-by-hash resolver every displayed string
// funnels through (both the id→name→hash path and the high-bit direct path).
// dialog_arm registers the hash of the NPC's text (sacred_hash("CAPMILES_GREET")
// = 0x1b13fa50) here; the hook then logs, TIMESTAMPED, each time the engine
// resolves that hash + whether it returned non-NULL. This decisively answers
// whether the OPEN dialog window resolves OUR text (vs. resolving some other
// hash → the vanilla default). A non-NULL hit during the open window == our
// text reached the renderer (then it's a display/slot bug, not a resolve gap);
// no hit during the window == the dialog body never asks for our hash.
static volatile uint32_t g_watch[8] = {0};
extern "C" void text_logger_watch(uint32_t hash) {
    hash &= 0x7FFFFFFFu;
    if (!hash) return;
    for (int i = 0; i < 8; i++) if (g_watch[i] == hash) return;   // already watched
    for (int i = 0; i < 8; i++) {
        if (g_watch[i] == 0) { g_watch[i] = hash; sdk_log("[dlgres-watch] now watching FUN_0080eaf0(hash=%08x)", hash); return; }
    }
}
static inline bool is_watched(uint32_t hash) {
    hash &= 0x7FFFFFFFu;
    for (int i = 0; i < 8; i++) if (g_watch[i] == hash) return true;
    return false;
}

// Dedup-set of hashes we've already recorded. Guarded by g_cs.
static CRITICAL_SECTION g_cs;
static bool g_cs_init = false;
static std::unordered_set<uint32_t>* g_seen = nullptr;
// Pending hashes accumulated since the last flush. Owned under g_cs.
static std::unordered_set<uint32_t>* g_pending = nullptr;

// The hook proper.
static void* __fastcall hook_FUN_0080eaf0(void* this_ptr, void* edx, uint32_t hash) {
    void* result = g_trampoline(this_ptr, edx, hash);

    // Timestamped watch + CALL-CHAIN trace: when the engine resolves a watched
    // hash (e.g. the vanilla "murderer" line 06048666), log who asked. The hook
    // IS the detour target, so _ReturnAddress() == the caller of FUN_0080eaf0;
    // scan the stack for in-.text pointers to reconstruct the chain up to the
    // dialog code that holds the text ref (the node we want to rewrite natively).
    if (is_watched(hash)) {
        void* ra = _ReturnAddress();
        uintptr_t* fp = (uintptr_t*)_AddressOfReturnAddress();
        char chain[200]; int off = 0; chain[0] = 0;
        for (int i = 0; i < 48 && off < 180; i++) {
            uintptr_t v = 0;
            __try { v = fp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            if (v >= 0x00401000 && v < 0x00900000)
                off += _snprintf_s(chain + off, sizeof(chain) - off, _TRUNCATE, " %08x", (unsigned)v);
        }
        sdk_log("[dlgcaller] hash=%08x %s ra=%08x chain:%s", hash & 0x7FFFFFFFu,
                result ? "HIT" : "MISS", (unsigned)(uintptr_t)ra, chain);
    }

    // Record. Keep fast — single CS, only one set lookup + insert.
    InterlockedIncrement(&g_calls);
    if (g_cs_init && g_seen) {
        EnterCriticalSection(&g_cs);
        // emplace into the seen-set; if it was new also push into pending.
        auto pr = g_seen->insert(hash);
        if (pr.second) {
            g_pending->insert(hash);
            InterlockedIncrement(&g_unique);
        }
        LeaveCriticalSection(&g_cs);
    }
    return result;
}

// --------- Trampoline allocation + detour install -------------------------

static bool install_trampoline_detour(uintptr_t target, void* hook) {
    // Unified installer (hooks/detour.cpp). 7-byte prologue, no sig check
    // (the .text-decrypt gate already ran before this). Emitted bytes
    // unchanged from the old hand-rolled copy. Goal A2.
    return hooks::install_trampoline(target, PROLOGUE_BYTES, hook,
                                     (uint8_t**)&g_trampoline, nullptr, 0,
                                     "text_logger");
}

// --------- Periodic flush worker ------------------------------------------
//
// Drains g_pending under the CS every N ms and appends to seen_hashes.csv.

static DWORD WINAPI flush_worker(LPVOID) {
    // Resolve path: <gamedir>\sdk\logs\seen_hashes.csv
    char path[MAX_PATH];
    {
        char exe[MAX_PATH] = {0};
        GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
        char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
        char dir[MAX_PATH];
        _snprintf_s(dir, _TRUNCATE, "%s\\sdk\\logs", exe);
        CreateDirectoryA(dir, nullptr);
        _snprintf_s(path, _TRUNCATE, "%s\\seen_hashes.csv", dir);
    }
    // Write header once.
    {
        FILE* f = nullptr;
        fopen_s(&f, path, "ab");
        if (f) {
            fseek(f, 0, SEEK_END);
            if (ftell(f) == 0) fputs("hash\n", f);
            fclose(f);
        }
    }

    while (g_active) {
        Sleep(5000);   // flush every 5 s
        if (!g_cs_init) continue;

        // Drain pending into a local snapshot under the CS.
        std::unordered_set<uint32_t> snap;
        EnterCriticalSection(&g_cs);
        if (g_pending && !g_pending->empty()) {
            snap.swap(*g_pending);
        }
        LeaveCriticalSection(&g_cs);

        if (snap.empty()) continue;

        FILE* f = nullptr;
        fopen_s(&f, path, "ab");
        if (!f) continue;
        for (uint32_t h : snap) {
            fprintf(f, "%u\n", h);
        }
        fclose(f);
    }
    return 0;
}

// --------- Public install --------------------------------------------------

void install() {
    if (g_active) return;
    if (!g_attach.exe_module) {
        sdk_log("[text_logger] no exe module captured — abort");
        return;
    }
    uintptr_t base = (uintptr_t)g_attach.exe_module;
    uintptr_t target = base + FUN_0080EAF0_RVA;

    // Verify prologue: expect `6A FF 68` at entry. If not, .text might not
    // be decrypted, or the function moved in this build.
    uint8_t* code = (uint8_t*)target;
    if (code[0] != 0x6A || code[1] != 0xFF || code[2] != 0x68) {
        sdk_log("[text_logger] unexpected prologue %02x %02x %02x %02x at %p — abort",
                code[0], code[1], code[2], code[3], (void*)target);
        return;
    }

    InitializeCriticalSection(&g_cs);
    g_cs_init = true;
    g_seen    = new std::unordered_set<uint32_t>();
    g_pending = new std::unordered_set<uint32_t>();

    if (!install_trampoline_detour(target, (void*)&hook_FUN_0080eaf0)) {
        sdk_log("[text_logger] detour install failed");
        return;
    }

    g_active = true;
    sdk_log("[text_logger] hook live @ %p (trampoline=%p, hook=%p)",
            (void*)target, g_trampoline, (void*)&hook_FUN_0080eaf0);

    // Dialog-body capture hook on FUN_0080f5e0 (key->wchar* fetch).
    uintptr_t f5e0 = base + FUN_0080F5E0_RVA;
    {
        uint8_t* c = (uint8_t*)f5e0;
        if (c[0] == 0x53 && c[1] == 0x56 && c[2] == 0x8B) {
            if (hooks::install_trampoline(f5e0, F5E0_PROLOGUE, (void*)&hook_FUN_0080f5e0,
                                          (uint8_t**)&g_tramp_f5e0, nullptr, 0, "text_logger:f5e0"))
                sdk_log("[text_logger] body-capture hook live @ %p (tramp=%p)", (void*)f5e0, g_tramp_f5e0);
            else
                sdk_log("[text_logger] body-capture hook install FAILED @ %p", (void*)f5e0);
        } else {
            sdk_log("[text_logger] f5e0 prologue %02x %02x %02x unexpected — body-capture skipped",
                    c[0], c[1], c[2]);
        }
    }

    // Load the persisted dialog-redirect map (vanilla key -> our hash) and seed
    // the known Captain Miles mapping so it works on the very first talk.
    {
        char exe[MAX_PATH] = {0};
        GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
        char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
        char dir[MAX_PATH];
        _snprintf_s(dir, _TRUNCATE, "%s\\sdk\\logs", exe);
        CreateDirectoryA(dir, nullptr);
        _snprintf_s(g_map_path, _TRUNCATE, "%s\\dlg_redirect_map.csv", dir);
        FILE* f = nullptr; fopen_s(&f, g_map_path, "rb");
        if (f) {
            unsigned kk = 0, hh = 0;
            while (fscanf_s(f, " %x , %x", &kk, &hh) == 2) map_add(kk, hh, false);
            fclose(f);
        }
        // (No key-swap seed — superseded by the native dialog-NAME override,
        // sacred.dialog_override, applied in FUN_00672cf0.)
        sdk_log("[text_logger] dialog-redirect map loaded: %ld entries (%s)", g_map_n, g_map_path);
    }
    // Trace WHO resolves the vanilla "murderer" line (06048666) — the dialog code
    // that holds the text ref we want to rewrite for a native fix.
    text_logger_watch(0x06048666u);

    // Hook FUN_00672cf0 to log the dialog line NAMES (the native by-name binding).
    {
        uintptr_t t = base + FUN_00672CF0_RVA;
        uint8_t* c = (uint8_t*)t;
        if (c[0] == 0x8B && c[1] == 0x44 && c[2] == 0x24) {
            if (hooks::install_trampoline(t, F672CF0_PROLOGUE, (void*)&hook_FUN_00672cf0,
                                          (uint8_t**)&g_tramp_672cf0, nullptr, 0, "text_logger:672cf0"))
                sdk_log("[text_logger] dialog-name hook live @ %p", (void*)t);
            else
                sdk_log("[text_logger] dialog-name hook FAILED @ %p", (void*)t);
        } else {
            sdk_log("[text_logger] 672cf0 prologue %02x %02x %02x unexpected", c[0], c[1], c[2]);
        }
    }

    CreateThread(nullptr, 0, flush_worker, nullptr, 0, nullptr);
}

}} // namespace sdk::text_logger
