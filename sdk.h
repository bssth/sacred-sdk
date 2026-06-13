// SacredSDK — shared internals between dllmain.cpp and overlay.cpp.
//
// Thread-safe ring-buffered log so DllMain / hooks can post text and the
// overlay thread can read it without locking on every frame.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>

// Forward-declare Lua's state struct so subsystem headers can pass `lua_State*`
// without dragging in the full Lua header. Real definition lives in lua.h.
struct lua_State;

namespace sdk {

// ---- log ring buffer ------------------------------------------------------
// Fixed-capacity circular buffer of log lines. Writes are O(1) under a CS,
// reads under the same CS. Overlay copies into its own ImGui-visible state
// once per frame so the lock window is tiny.
struct LogRing {
    static constexpr int CAP = 256;       // 256 lines
    static constexpr int LINE_MAX = 256;  // each up to 255 chars + null
    char lines[CAP][LINE_MAX];
    int head = 0;                          // next write index
    int count = 0;                         // total valid entries (<=CAP)
    CRITICAL_SECTION cs;
    bool cs_init = false;

    void init() {
        if (!cs_init) {
            InitializeCriticalSection(&cs);
            cs_init = true;
        }
    }
    void shutdown() {
        if (cs_init) {
            DeleteCriticalSection(&cs);
            cs_init = false;
        }
    }
    void push(const char* msg) {
        if (!cs_init) return;
        EnterCriticalSection(&cs);
        strncpy_s(lines[head], LINE_MAX, msg, _TRUNCATE);
        head = (head + 1) % CAP;
        if (count < CAP) count++;
        LeaveCriticalSection(&cs);
    }
    // Snapshot up to N lines, oldest-first, into a caller buffer. Returns count.
    int snapshot(char (*out)[LINE_MAX], int max_lines) {
        if (!cs_init) return 0;
        EnterCriticalSection(&cs);
        int n = (count < max_lines) ? count : max_lines;
        int start = (head - n + CAP) % CAP;
        for (int i = 0; i < n; i++) {
            strncpy_s(out[i], LINE_MAX, lines[(start + i) % CAP], _TRUNCATE);
        }
        LeaveCriticalSection(&cs);
        return n;
    }
};

extern LogRing g_log;

// File-and-ring logger. Same timestamp format as the original sdk_log.
void sdk_log(const char* fmt, ...);

// Module info captured at attach time (so overlay can show it without
// re-querying every frame).
struct AttachInfo {
    DWORD pid;
    DWORD attach_tid;
    HMODULE self_module;
    HMODULE exe_module;
    DWORD attach_tick;       // GetTickCount at attach
    char exe_path[MAX_PATH];
    char cmdline[1024];
};
extern AttachInfo g_attach;

// Set by the CreateWindowExA hook when Sacred's main window is born.
// Used by the overlay thread to re-own itself so it floats above Sacred
// without stealing focus or showing up separately in the taskbar.
extern volatile HWND g_sacred_hwnd;

// One-shot self-test of the wired PAX hero-save port (hero_save_probe.cpp).
void start_hero_save_probe();

// Read a hero .pax save's headline stats (the wired PAX port). `path` may be
// absolute or relative to the game dir. Returns false if not found / unreadable
// / zlib failed. Backs the `sacred.read_save(path)` Lua binding.
struct HeroSaveInfo {
    bool     ok = false;
    int      underworld = 0;        // 1 = AMH\x1B (UW layout)
    unsigned class_id = 0, level = 0, gold = 0, xp = 0, c7_size = 0;
    char     class_name[32] = { 0 };
};
bool read_hero_save(const char* path, HeroSaveInfo& out);

// Runtime VA resolver for the SecuROM-packed build (engine_resolve.cpp). Finds
// engine helper VAs (game zlib `inflate`, debug logger, …) by scanning LIVE
// `.text` off the validated .data string anchors — they can't be pinned
// statically (code pages are encrypted on disk). Read-only; never patches.
namespace engine_resolve {
    struct ResolvedEngine {
        bool      resolved = false;
        uintptr_t inflate = 0;        // game's own zlib inflate(z_streamp,int)
        uintptr_t inflate_init = 0;   // inflateInit2_ (references "1.2.1")
        uintptr_t debug_log = 0;      // printf-like logger (writes DEBUG.LOG)
        uintptr_t uncompress = 0;     // game's zlib uncompress(dst,&dlen,src,slen)
        uintptr_t globalres = 0;      // void* __stdcall(handle) -> resource ptr
        uintptr_t objmgr_alloc = 0;   // cObjectManager::allocate __thiscall(this,size)
        uintptr_t globalres_str = 0;  // wchar_t* __thiscall(mgr,key) -> UTF-16 string
    };
    extern ResolvedEngine g_engine;   // cached after resolve()
    bool resolve();                   // run the scan now (SEH-guarded); caches
    void start_engine_resolve();      // delayed worker (waits for decryption)

    // Call the game's own zlib uncompress (drops the external zlib1.dll dep).
    // Lazily resolves+sig-verifies the VA on first use. Returns the zlib rc
    // (0 = Z_OK); returns a nonzero sentinel if the function is unavailable
    // (not decrypted yet / sig mismatch). Backs the PAX hero-save codec.
    int  call_uncompress(unsigned char* dst, unsigned long* dstLen,
                         const unsigned char* src, unsigned long srcLen);
    bool uncompress_available();      // true once the game uncompress is verified

    // Resolve a global.res string handle (ident) to UTF-8 via the engine's own
    // binary-search resolver FUN_0080f5e0 (returns the resource's UTF-16 string).
    // `handle` may carry the 0x80000000 high bit (masked off). Writes a NUL-
    // terminated UTF-8 string into out[cap]. Returns true on a hit, false on
    // miss / not-loaded / unresolved. SEH-guarded; safe before global.res loads
    // (the engine fn null-checks its own table). Backs sacred.globalres(handle).
    bool globalres_string(unsigned int handle, char* out, int cap);
}

// Frame capture for the integrated bilinear upscaler (frame_cap.cpp). The DDraw
// present hook (producer, game thread) publishes the engine's native-size frame;
// the overlay (consumer, its own thread) uploads it to a D3D11 texture and draws
// it full-screen with a bilinear sampler — a smooth upscale over the pixelated
// DDraw present. Gated by sdk.ini `enable_smooth=1`.
namespace framecap {
    extern volatile bool g_enabled;          // mirrors enable_smooth
    // Producer: copy a present source frame (raw 16/32-bpp bits). Cheap memcpy.
    void publish(const void* bits, int width, int height, int pitch, int bpp);
    // Consumer: current frame dims/format without copying (for texture sizing).
    bool peek_dims(int* width, int* height, int* bpp);
    // Consumer: if a NEW frame arrived since last call, copy its raw rows out
    // into `dst` (dstPitch bytes/row) and return true; else false.
    bool take(void* dst, int dstPitch);
}

// Overlay thread lifecycle.
namespace overlay {
    void start();
    void stop();

    // Push a toast banner over the game window. Used by sacred.notify()
    // from Lua. Caller is expected to have applied throttle/dedup —
    // the toast queue here just caps depth at TOAST_CAP and evicts
    // oldest when full. Text is copied (256-byte cap).
    void push_toast(const char* text);
}

// Filesystem override layer: hooks CreateFileA so that any read which
// would land on game_dir/sub/file is transparently redirected to
// game_dir/custom/sub/file if that override exists.
//
// Lets mods drop replacement files under custom/ (mirroring the game tree)
// without touching Steam's verified install.
namespace fs_override {
    void install();
    extern volatile long g_total_opens;   // every CreateFileA call we saw
    extern volatile long g_redirected;    // how many were swapped to the override
    extern volatile long g_verbose;       // 1 = log every CreateFileA path
    const char* last_redirect();          // for the overlay
}

// Sacred-specific hooks (CreateWindowExA / ChangeDisplaySettingsA / DDraw).
namespace hooks {
    void install();

    // Optional aggressive display overrides — borderless windowed at a
    // custom resolution. Read from sdk.ini at install time; toggleable via
    // overlay. Safe to enable now that patches::patch6 kills the
    // force-foreground busy-wait that previously bricked us.
    struct ForceConfig {
        bool   enable_borderless;   // strip WS_OVERLAPPEDWINDOW, use WS_POPUP
        bool   swallow_displaymode; // make ChangeDisplaySettings a no-op
        int    width;               // 0 = keep Sacred's requested size
        int    height;
        bool   fullscreen;          // borderless window = whole primary
                                    // monitor at desktop res, origin (0,0);
                                    // DDraw stretch fills it → true
                                    // fullscreen, full scale. Overrides
                                    // width/height when set.
        bool   hd;                  // ReBorn-style TRUE HD: rewrite the
                                    // DirectDraw SetDisplayMode + back-
                                    // buffer CreateSurface dims to
                                    // width×height so Sacred renders at
                                    // that resolution (not 800x600/1024x768).
        bool   stretch;             // Render at the engine's native size, then
                                    // hook the primary-surface Blt to stretch the
                                    // present to the full window client rect
                                    // (uniform 2D+3D upscale, fills the screen).
                                    // Mutually exclusive with hd in practice.
        bool   smooth;              // On top of stretch: the overlay re-draws the
                                    // captured frame with a D3D11 bilinear sampler
                                    // (smooth upscale instead of DDraw's point
                                    // sampling). Requires stretch.
    };
    extern ForceConfig g_force;

    // Live state (set by hooks; read by overlay).
    extern volatile bool g_main_seen;
    extern volatile bool g_main_modified;
}

// Dump runtime-decrypted .text to sdk\logs\text_dump.bin.
namespace dumptext {
    void start();
}

// Direct-byte patches and inline detours ported from the 2007 SacredVault
// "unofficial patch 2.29" (see docs/12-patch-229-rosetta.md). Implemented as
// runtime hooks rather than on-disk EXE patches.
namespace patches {

    // Patch 1: redirect FUN_0080e680 (PE BINARY 107 loader) to read
    // `scripts/<lang>/global.res` from disk instead. Lets us hot-swap
    // language / mod text without repacking the .rsrc section.
    //
    // Patch 6: locate and NOP the 2-byte "force window focus" instruction
    // that follows the display-mode change — fixes the black-screen risk we
    // ourselves hit when forcing borderless. Currently a STUB: the address
    // for our 2006-10-13 Steam build hasn't been re-discovered yet.
    void install();

    // True once at least one patch is live. Read by overlay.
    extern volatile bool g_patch1_active;
    extern volatile bool g_patch6_active;

    // Last status line from the patch installer (shown in overlay).
    const char* status();
}

// DirectDraw vtable hooks for HD stretching. Hooks the IDirectDraw object
// returned by DirectDrawCreate at the COM vtable level, tracks primary
// surface creation, then patches the primary's Blt method so its destRect
// is forced to the window's client area — Sacred's 800×600 internal frame
// gets stretched to whatever size we forced the window to in hooks.cpp.
namespace ddraw_hooks {
    // Called by hooks.cpp once a real IDirectDraw* is in our hands.
    void install(void* idirectdraw);
    // Stats for overlay.
    extern volatile bool g_installed;
    extern volatile long g_blt_calls;
    extern volatile long g_blt_stretched;
    extern volatile long g_primary_surfaces_seen;
}

// Mirror Sacred's internal debug-log function (FUN_0066ef40) into our
// sdk_log.  The compiler's parse-error and "compiling 'X' failed" messages
// all go through that printer; capturing it lets us SEE what the compiler
// is complaining about.
namespace sacred_log_mirror {
    void install();
    extern volatile bool g_active;
    extern volatile long g_calls;
}

// Calls into Sacred's built-in cScriptCompiler — Path B for source-level
// mods.  The compiler's code is fully intact in retail Sacred but no game
// code path invokes its top entry (FUN_00671ad0).  We construct a minimal
// cScriptCompiler instance and call it ourselves.
namespace source_compiler {
    // Compile `file_path` (a path the CreateFileA hook will look up;
    // dropping the file in `custom/` works fine). Returns 1 on success, 0 on
    // failure. Logs to sdk_log either way. The tagged variant writes the
    // post-compile instance buffer to sdk/logs/compile_inst_<tag>.bin so we
    // can offline-discover where the compiler emits bytecode.
    int compile_file(const char* file_path);
    int compile_file_tagged(const char* file_path, const char* tag);

    // Overlay-driven smoke test. Each click rotates through PROBES[] — the
    // tag rotates with it, so each probe gets its own compile_inst dump.
    void smoke_test();

    extern volatile long g_runs;        // total compile_file invocations
    extern volatile long g_successes;   // returned 1
    extern char g_last_message[256];
}

// In-game GUI front-end for sdk/re/py/funkcode_swap.py. Scans a target .bin,
// lets the user queue same-length identifier swaps from the overlay, applies
// them, and writes the patched file to <game>/custom/<same-path> where
// fs_override picks it up on the next launch.
namespace script_mods {
    void draw_panel();
}

// Lua-as-source mod system. Embeds Lua 5.4 inside the DLL. On attach
// start_auto_bake() spawns a worker that walks `<game>/custom/lua/**.lua`,
// executes each through Lua, expects a list-of-records table, assembles
// FunkCode bytes, and writes the result to `<game>/custom/<mirrored>.bin`
// where fs_override picks it up on the next file open. `bake_all()` reruns
// it on demand (overlay button).
//
namespace lua_bake {
    void start_auto_bake();      // spawn worker once, intended for DllMain
    void bake_all();             // synchronous, rerun-from-overlay path
    const char* status();
    long baked_files();
    long baked_records();
    bool busy();
}

// Runtime trigger hooks — task 20.
//
// During bake, mods can call `sacred.on_trigger("name", function(ctx) … end)`
// to register a callback that fires when Sacred's interpreter walks a
// matching tag-0x1a QuestTrigger record at game-time.
//
// State model
// -----------
// The bake worker hands its lua_State off to this module at the end of the
// bake (instead of closing it). The state lives for the rest of the Sacred
// process. Sacred's interpreter runs on the main thread; when our hook
// fires we call into the Lua state on that same thread, so there is no
// cross-thread contention with Lua APIs (the bake worker has joined by
// then). Re-bakes during a session reuse the same state — handlers
// accumulate unless a mod calls `sacred.clear_triggers()`.
namespace runtime_triggers {

    // Called by lua_bake at the end of bake_all(). On the first call we
    // adopt the state and install the trigger-fire trampoline; on later
    // re-bakes we just confirm the state is still the same one.
    void take_state(::lua_State* L);

    // (Re)install the engine trampoline hooks. Idempotent once they are live.
    // Called BOTH from take_state (bake worker) AND from the .text-decrypt gate
    // (dump_text) — so if the bake worker wins the race against decryption and
    // sees still-encrypted bytes (all hooks abort, none committed), the
    // post-decrypt call retries and succeeds. Without this second caller the
    // SDK's quest/dialog hooks could silently stay uninstalled for the session.
    void ensure_hooks_installed();

    // Called by lua_bake while preparing the state — extends an existing
    // `sacred` Lua table with on_trigger / clear_triggers. Mods invoke
    // these during their bake-time .lua execution.
    void install_lua_api(::lua_State* L);

    // Called by the interpreter trampoline when a trigger fires. Looks up
    // registered handlers by name and runs each via pcall. Errors logged.
    // Safe to call before take_state — becomes a no-op.
    void fire(const char* trigger_name);

    // Stats for overlay.
    extern volatile bool g_ready;         // state adopted + hook installed
    extern volatile long g_handlers;      // unique trigger names registered
    extern volatile long g_fires;         // total times fire() ran a handler
    extern volatile long g_handler_errs;  // pcall failures
    extern volatile long g_notify_calls;  // sacred.notify() total calls
    extern volatile long g_notify_passed; // notify() actually reached engine
    extern volatile long g_notify_dropped;// notify() rejected (throttle/dup/len)

    // DIAGNOSTIC counters.
    //
    //   g_thunk_*       — every time the naked-asm thunk runs, BEFORE any
    //                     validation. Compare against g_seen_*: gap means
    //                     hook fires but the cstring at [ECX+0xa460] isn't
    //                     valid (different func uses a different offset).
    //   g_seen_*        — name passed printable-ASCII validation and was
    //                     dispatched through fire(). If 0 while thunk>0,
    //                     we have the wrong offset; if both 0 the hook
    //                     never executes (wrong target function).
    extern volatile long g_thunk_self_trigger;
    extern volatile long g_thunk_dialog_check;
    extern volatile long g_thunk_funnel;          // FUN_00463240 chokepoint
    extern volatile long g_thunk_hash;            // FUN_0080e780 sacred_hash entry
    extern volatile long g_seen_self_trigger;
    extern volatile long g_seen_dialog_check;
    extern volatile long g_seen_funnel;
    extern volatile long g_seen_hash;
    // Last seen names — index 0 is most recent. Returns number filled.
    int last_seen(char (*out)[128], int max_lines);

    // Dump the ring buffer to sdk_loaded.log so the modder can correlate
    // what they did in-game with which resource ids Sacred queried.
    // Called from the overlay button.
    void snapshot_ring_to_log();

    // Clear the ring. Use BEFORE an in-game action — then any
    // subsequent F10 snapshot only contains names queried since clear.
    void clear_ring();

    // Dump the quest-display registry (every entry's quest_id) to
    // sdk_loaded.log. Bound to the overlay F8 key. Safe to call any
    // time — logs a diagnostic line if the registry isn't readable.
    void questbook_dump_to_log();

    const char* status();
}

// Trampoline-based hook on FUN_0080eaf0 (the resource-dictionary lookup).
// Every (hash, result) pair the live game queries is recorded; the set
// of unique hashes is periodically flushed to sdk/logs/seen_hashes.csv.
// Combined with hash_names.csv, this gives full live coverage of every
// string Sacred actually uses during play.
namespace text_logger {
    void install();                // call after .text decryption
    extern volatile bool g_active;
    extern volatile long g_calls;     // total calls observed
    extern volatile long g_unique;    // distinct hashes seen
}

// Read-only inspector for the live player struct.
// Walks pointer chains taken from the public Cheat Engine table; image-base
// stable due to no-ASLR. All addresses are RVAs from Sacred.exe ImageBase.
namespace player {

    struct Snapshot {
        bool      valid;                  // false if the chain didn't resolve this frame
        uintptr_t struct_addr;            // raw VA of the player struct, for debugging
        uint16_t  class_id;               // 0 if absent; see CLASS_NAMES
        int32_t   health;                 // current HP
        uint8_t   skills[8];              // skill type ids (see SKILL_NAMES)
        uint32_t  helmet, cuirass, belt, boots;
        uint32_t  gauntlets, bracers;
        uint32_t  amulet1, amulet2;
        uint32_t  ring1, ring2, ring3, ring4;
        uint32_t  weapon_l, weapon_r;
        uint32_t  cannon, shoulders, greaves, wings;
    };

    // Resolve and snapshot the live player. Cheap (handful of reads); call once per UI frame.
    Snapshot read();

    // Live hero struct base pointer, or 0 if the CT chain isn't built yet
    // (loading screen, main menu, dead). Caller MUST NULL-check + recheck
    // every fire — chain can torn-down at any time. Used by
    // runtime_triggers to write hero state directly (gold, slots) without
    // the bytecode interpreter.
    uintptr_t hero_base();

    // Live hero world position (base+0x1C = x, base+0x20 = y — same coords
    // Sacred uses for the floating "+gold" text). Returns false if the
    // hero chain isn't resolved (menu / loading / dead). Same coordinate
    // space as quest KompassPos values (world range ~ x 0..6279,
    // y 0..6307), so the dumped numbers can be pasted straight into a
    // questbook_set_kompass call.
    bool world_pos(int32_t* x, int32_t* y);

    // Teleport active hero to KompassPos (kx, ky) — inverse of world_pos.
    // false if hero chain not resolved yet (retry next tick).
    bool set_world_pos(int32_t kx, int32_t ky);

    // Spawn creature `type` (npc.lua id) at KompassPos (kx,ky) via the
    // engine create path. Returns the new creature handle, 0 on failure.
    int spawn_npc(int type, int32_t kx, int32_t ky);
    int spawn_npc_here(int type);   // at the hero (verbatim hero pos)
    int spawn_npc_at(int type, int32_t kx, int32_t ky);  // KompassPos, hero sector

    // Creature-handle accessors (handle from spawn_npc / engine).
    uintptr_t npc_creature(int handle);                 // cCreature* or 0
    bool npc_info(int handle, int* type, int32_t* kx,
                  int32_t* ky, uint32_t* faction);       // any out may be null
    bool npc_set_faction(int handle, uint32_t faction);
    bool npc_wake(int handle);                 // activate AI (WakeUp)
    bool npc_set_stance(int handle, int mode, uint32_t value); // 0=class default
    bool npc_set_invulnerable(int handle, bool on);            // immortal flag
    bool npc_set_stationary(int handle, bool on);              // hold post / roam
    bool npc_set_level(int handle, int level);  // level/rank

    // Storyline runtime layer (.claude/knowledge/re/quest_storyline.md, items.md).
    bool set_npc_name(int handle, const char* name); // DlgNPC/NameArrA entry
    bool npc_quest_icon(int handle, bool on);        // overhead "?!" marker
    int  spawn_item(int type, int32_t kx, int32_t ky); // ground item (== spawn_npc_at)
    bool npc_teleport(int handle, int32_t kx, int32_t ky); // engine TP, any NPC
    bool npc_equip(int handle, int item_type, int slot);   // EXPERIMENTAL
    int  createnpc_engine(const uint8_t* payload, size_t plen, int want_type); // engine CreateNPC -> handle|0
    int  dlgnpc_bind(int handle, const char* name, int marker_on); // -> dlg idx | -1
    bool dialog_arm(int handle, const char* dlg_name,
                    const char* text_key, const char* voice); // R-B replay
    bool dialog_clear(int handle);         // close dialog + clear "?!"
    bool npc_roster_add(int handle, int quest_id); // show in companion panel
    bool npc_roster_remove(int handle);            // remove from panel
    bool npc_make_companion(int handle);   // party-follow + fights for hero
    bool npc_dismiss(int handle);          // inverse of make_companion
    bool npc_despawn(int handle);          // clean engine removal (DelNPC)
    bool npc_set_disposition(int handle, uint32_t matrix_class); // +0x1F0
    void npc_field_dump(int handle, const char* tag);      // diag: log render fields
    bool npc_set_hp(int handle, int hp);                   // cur+max HP (+0x4d8)
    void scan_creatures(int filter_type, int cap);         // diag: dump all live creatures
    void hero_weapon_dump();                                // diag: hero equip slots + type ids
    void dump_vanilla_of(int filter_type, int ai_class, int skip_lo, int skip_hi, const char* tag);
    bool npc_make_combatant(int handle, int level, int ai_class); // real fighter init

    extern const char* CLASS_NAMES[];     // index by Snapshot::class_id, NULL if out of range
    extern const char* SKILL_NAMES[];     // index by skill byte, NULL if out of range
    const char* class_name(uint16_t id);
    const char* skill_name(uint8_t  id);
}

} // namespace sdk
