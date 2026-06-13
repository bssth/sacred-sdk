// SacredSDK — runtime trigger hooks (HANDOFF task 20).
//
// Lets modders write `sacred.on_trigger("HQ_3_2_1_sera_DLG_OFFEN",
// function(ctx) … end)` and have the function fire when Sacred dispatches
// that trigger at game-time. First-cut implementation hooks:
//
//   FUN_004915a0   "SelfTriggerQuest"     (NPC dialog → quest trigger)
//   FUN_00491170   "Dialog-Check"         (Dialog %s nicht möglich)
//
// Both share the same MSVC SEH prologue and the same calling convention
// (__fastcall: ECX = ctx, EDX = record, plus stack args we don't touch).
// On entry, `[ECX + 0xa460]` is the interpreter's current name-buffer — it
// holds the trigger name as an inline ASCII cstring. We snapshot ECX in a
// naked thunk, call our C++ helper which reads the name and dispatches to
// the registered Lua handlers, then jump through to the saved prologue +
// jmp back to FUN_xxx + 7 so Sacred sees its own stack frame untouched.
//
// Why two hooks and not one
// -------------------------
// The agent's recon (see HANDOFF task 20) showed tag-0x1a is NOT the fire
// site (it's just metadata-setup). Triggers fire from two distinct
// subsystems: SelfTriggerQuest for `<NPC>_DLG_*` style names, Dialog-Check
// for `Dialog %s` style names. Hooking both gives full coverage. A handler
// that doesn't match the fired name is a cheap dictionary miss.
//
// Lifecycle
// ---------
//   bake worker thread → take_state(L) → if handlers exist, install_hooks()
//   main thread        → Sacred plays → trampoline → fire(name) → pcall
//
// Threading
// ---------
// Sacred's interpreter runs on the main thread. Bake worker exits before
// the first hook fire, so there is no concurrent Lua access. We do the
// .text install on the bake worker thread; that's a race against Sacred's
// main thread potentially executing the prologue we're overwriting — see
// HANDOFF gotcha #12 for the SuspendThread mitigation if it bites. For now
// we follow text_logger's pattern (which works) and skip the suspend.

#include "sdk.h"
#include "hooks/detour.h"   // Goal A2: unified trampoline-detour installer
#include <cstring>
#include <cstdio>

extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
}

namespace sdk { namespace runtime_triggers {

volatile bool g_ready             = false;
volatile long g_handlers          = 0;
volatile long g_fires             = 0;
volatile long g_handler_errs      = 0;
volatile long g_thunk_self_trigger = 0;
volatile long g_thunk_dialog_check = 0;
volatile long g_thunk_funnel       = 0;
volatile long g_thunk_hash         = 0;
volatile long g_seen_self_trigger  = 0;
volatile long g_seen_dialog_check  = 0;
volatile long g_seen_funnel        = 0;
volatile long g_seen_hash          = 0;

static lua_State* g_L         = nullptr;
static char       g_status[256] = "(idle)";

// Diagnostic: small ring of recently-seen trigger names, populated by the
// trampolines whether or not a handler matched. Lets the user discover the
// REAL names Sacred dispatches and register handlers for them.
//
// Anti-spam: time-window dedup. If the same name was pushed within
// RING_DEDUP_MS ms, skip — that way per-frame UI lookups (queried 30×/s)
// don't fill the ring and crowd out rare event-only queries.
//
// Guarded by g_ring_cs because both thunks plus the overlay reader touch
// it.
static const int       RING_CAP       = 64;
static const uint64_t  RING_DEDUP_MS  = 1000;
struct RingEntry {
    char     text[128];
    uint64_t pushed_at;
};
static RingEntry         g_ring[RING_CAP];
static int               g_ring_head  = 0;       // next slot to overwrite
static int               g_ring_count = 0;
static CRITICAL_SECTION  g_ring_cs;
static bool              g_ring_cs_init = false;

static void set_status(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
    sdk_log("[runtime_triggers] %s", g_status);
}

const char* status() { return g_status; }

// =========================================================================
// Game-state bindings — direct hero-struct writes.
// =========================================================================
//
// Task-21 recon concluded that Sacred has NO standalone C primitives for
// these operations — gold-set / has-item / qbit-flip are all bytecode
// interpreter cases. The cleanest runtime path is direct struct writes
// via the CT pointer chain that player_state.cpp already resolves.
//
// Offsets (verified by recon; medium confidence):
//   hero + 0x3EE    u32     gold
//   hero + 0x1A4..0x1E8  (step 4)  18 equip-slot item-ids (u32 each)
//
// Qbit-set lives at interpreter_ctx + 0xA860 as a packed bitfield. That
// requires a different chain (the script-VM singleton) which we haven't
// nailed down — deferred. Mods can still toggle qbits at BAKE time via
// q.set_hero_qbit() in QuestCode.lua, which is the only place qbits
// normally change anyway.

constexpr uintptr_t HERO_OFF_GOLD            = 0x3EE;
constexpr uintptr_t HERO_OFF_EQUIP_BASE      = 0x1A4;
constexpr int       HERO_EQUIP_SLOTS         = 18;
// Verified via CT table + char.cpp source (community refs):
constexpr uintptr_t HERO_OFF_SKILL_ID_BASE   = 0x3CC;  // 8 × u8
constexpr uintptr_t HERO_OFF_SKILL_LVL_BASE  = 0x3D4;  // 8 × u8
constexpr uintptr_t HERO_OFF_MAX_HEALTH      = 0x4D4;  // u32
constexpr uintptr_t HERO_OFF_CUR_HEALTH      = 0x4D8;  // u32
constexpr uintptr_t HERO_OFF_CLASS_ID        = 0x010;  // u16

static int32_t sacred_get_hero_gold() {
    uintptr_t base = sdk::player::hero_base();
    if (!base) return -1;
    void* addr = (void*)(base + HERO_OFF_GOLD);
    if (IsBadReadPtr(addr, sizeof(uint32_t))) return -1;
    return (int32_t)*(uint32_t*)addr;
}

// SILENT path — direct write to [hero+0x3EE], no UI/sound side effects.
// Use as a fallback or when modder explicitly wants quiet adjustment.
static bool sacred_give_hero_gold_silent(int32_t amount) {
    uintptr_t base = sdk::player::hero_base();
    if (!base) return false;
    void* addr = (void*)(base + HERO_OFF_GOLD);
    if (IsBadWritePtr(addr, sizeof(uint32_t))) return false;
    int64_t cur = (int64_t)*(uint32_t*)addr;
    int64_t neu = cur + amount;
    if (neu < 0)        neu = 0;
    if (neu > 0x7FFFFFFFLL) neu = 0x7FFFFFFFLL;
    *(uint32_t*)addr = (uint32_t)neu;
    sdk_log("[runtime_triggers] give_gold(silent) %+d (%lld -> %lld)",
            amount, cur, neu);
    return true;
}

// COSMETIC EVENT — emits a GOLD_CHANGE event to Sacred's kernel event
// bus. Live test (2026-05-14) showed the event handler does NOT itself
// touch [hero+0x3EE] — it just routes side-effects to registered UI
// listeners (coin sound, floating "+N" text, HUD invalidate). So we
// pair the event with the direct write below to get both the money
// AND the polish. Three vanilla callsites use this same blob pattern.
//
// Event blob layout (36 bytes):
//   +0x00 vtable      = 0x00890B38
//   +0x04 kind        = 6
//   +0x08 color       = 0xFFFFFFFF (yellow, +) or 0xFFFF0040 (red, -)
//   +0x0C x           = [hero+0x1C]  (world position)
//   +0x10 y           = [hero+0x20]
//   +0x14 subkind     = 0x3EC        (GOLD_CHANGE discriminator)
//   +0x18 amount      = signed delta
//   +0x1C reserved    = 0
//   +0x20 cleanup_ptr = NULL         (MUST be null — kernel frees if non-null)
//
// Calling convention:
//   kernel = get_kernel(&blob, 0, 0)      // cdecl, 3 ignored args, EAX = singleton
//   recv_event(kernel, &blob, 0, 0)       // __thiscall, ECX=kernel, ret 0x0c
struct GoldChangeEvent {
    void*    vtable;
    uint32_t kind;
    uint32_t color;
    uint32_t x;
    uint32_t y;
    uint32_t subkind;
    int32_t  amount;
    uint32_t reserved;
    void*    cleanup_ptr;
};

typedef void* (__cdecl    *kernel_get_fn_t)(void* a0, int a1, int a2);
typedef void  (__thiscall *kernel_recv_fn_t)(void* this_, void* ev, int a1, int a2);

constexpr uintptr_t SACRED_KERNEL_GET_VA    = 0x00808E50;
constexpr uintptr_t SACRED_KERNEL_RECV_VA   = 0x008092F0;
constexpr uintptr_t SACRED_EVENT_VTABLE_VA  = 0x00890B38;
constexpr uint32_t  GOLD_CHANGE_SUBKIND     = 0x000003EC;

// Emit the cosmetic event (sound/text/HUD-invalidate). Returns true on
// success. Caller must have already updated [hero+0x3EE] via silent path.
static bool emit_gold_change_event(uintptr_t hero_base, int32_t amount) {
    HMODULE exe = g_attach.exe_module;
    if (!exe || (uintptr_t)exe != 0x00400000) return false;

    GoldChangeEvent ev;
    __try {
        ev.vtable      = (void*)SACRED_EVENT_VTABLE_VA;
        ev.kind        = 6;
        ev.color       = (amount >= 0) ? 0xFFFFFFFFu : 0xFFFF0040u;
        ev.x           = *(uint32_t*)(hero_base + 0x1C);
        ev.y           = *(uint32_t*)(hero_base + 0x20);
        ev.subkind     = GOLD_CHANGE_SUBKIND;
        ev.amount      = amount;
        ev.reserved    = 0;
        ev.cleanup_ptr = nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    auto get_kernel = (kernel_get_fn_t) SACRED_KERNEL_GET_VA;
    auto recv_evt   = (kernel_recv_fn_t)SACRED_KERNEL_RECV_VA;

    void* kernel = nullptr;
    __try {
        kernel = get_kernel(&ev, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!kernel) return false;
    __try {
        recv_evt(kernel, &ev, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// PUBLIC give_gold — does both the money math AND the cosmetic event.
// Live testing showed the event is purely a notifier — it doesn't write
// to [hero+0x3EE], so we direct-write FIRST then emit the event to
// trigger the sound/text/HUD invalidate. The event sees the freshly
// updated gold value when the HUD listener queries it.
static bool sacred_give_hero_gold(int32_t amount) {
    uintptr_t base = sdk::player::hero_base();
    if (!base) return false;

    // 1) Actual money change (silent direct write, clamped 0..INT32_MAX).
    if (!sacred_give_hero_gold_silent(amount)) return false;

    // 2) Cosmetic event — coin sound + "+N" floating text + HUD refresh.
    bool ev_ok = emit_gold_change_event(base, amount);
    if (!ev_ok) {
        sdk_log("[runtime_triggers] give_gold %+d: event emit failed "
                "(money still credited)", amount);
    }
    return true;
}

static int sacred_hero_has_item(int item_res) {
    uintptr_t base = sdk::player::hero_base();
    if (!base) return -1;
    void* slots = (void*)(base + HERO_OFF_EQUIP_BASE);
    if (IsBadReadPtr(slots, HERO_EQUIP_SLOTS * sizeof(uint32_t))) return -1;
    const uint32_t* p = (const uint32_t*)slots;
    uint32_t needle = (uint32_t)item_res;
    for (int i = 0; i < HERO_EQUIP_SLOTS; i++) {
        if (p[i] == needle) return 1;
    }
    return 0;  // not in equipment. Backpack scan is a future enhancement.
}

// Qbit-set/get: deferred. Recon located the bitarray at
// interpreter_ctx + 0xA860 but resolving the interpreter ctx pointer
// reliably needs more work. Modders use the bake-time path for now.
static bool sacred_set_hero_qbit(int /*bit*/, bool /*value*/) {
    return false;
}
static int sacred_get_hero_qbit(int /*bit*/) {
    return -1;
}

// Notification: forward to the overlay's toast list (rendered in
// overlay.cpp). Sacred's own in-engine banner system would need a
// reverse-engineering pass we haven't done — but for the modder's UX,
// an ImGui toast over the game window achieves the same effect and
// works during the menu/loading too.
//
// Caller has already applied length cap + dedup + throttle (see
// l_sacred_notify), so we just forward.
static bool sacred_show_banner(const char* text) {
    if (!text) return false;
    ::sdk::overlay::push_toast(text);
    return true;
}

// =========================================================================
// Lua-side: sacred.on_trigger(name, fn) + sacred.clear_triggers()
// =========================================================================
//
// Handlers live in LUA_REGISTRYINDEX[SDK_HANDLERS_KEY], which is itself a
// table mapping trigger name → list of fns. Multiple handlers per name
// stack; fire() runs each in registration order, each in pcall.

static const char SDK_HANDLERS_KEY[]   = "sacred_sdk__trigger_handlers";
static const char SDK_CTX_KEY[]        = "sacred_sdk__ctx";
static const char SDK_TICK_KEY[]       = "sacred_sdk__tick_handlers";
// on_world_load handler list — fired once when the walker captures
// cQuestManager (definition of the fire path is near the walker hook).
static const char SDK_WORLD_LOAD_KEY[] = "sacred_sdk__world_load_handlers";

// =========================================================================
// Notification throttle — anti-overflow for Sacred's UI message queue
// =========================================================================
//
// Sacred's in-game banner ("Quest completed", "Group eliminated", …) goes
// through a finite-size message queue inside the engine. If our mod
// floods sacred.notify() from a hot handler (e.g. fired every frame), we
// risk overflowing the queue or stacking dozens of banners.
//
// Defense in depth:
//   1. Hard cap on message length (256 chars). Anything longer is truncated.
//   2. Minimum gap between successive notifications (default 750 ms).
//      Excess calls return false (caller can detect throttling).
//   3. Back-to-back duplicates are silently dropped regardless of gap.
//
// These caps apply REGARDLESS of how many handlers stack on a trigger;
// every notify() call across all of them goes through one funnel.

constexpr DWORD  NOTIFY_MIN_GAP_MS  = 750;
constexpr size_t NOTIFY_MAX_LEN     = 256;
static  DWORD    g_last_notify_tick = 0;
static  char     g_last_notify_text[NOTIFY_MAX_LEN] = "";
static  CRITICAL_SECTION g_notify_cs;
static  bool     g_notify_cs_init = false;
volatile long    g_notify_calls  = 0;
volatile long    g_notify_passed = 0;
volatile long    g_notify_dropped = 0;

static void ensure_handlers_table(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, SDK_HANDLERS_KEY);
    if (lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_pop(L, 1);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, SDK_HANDLERS_KEY);
}

static int l_on_trigger(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    ensure_handlers_table(L);
    lua_getfield(L, LUA_REGISTRYINDEX, SDK_HANDLERS_KEY);   // [+1] handlers
    lua_getfield(L, -1, name);                              // [+1] list-or-nil
    bool is_new_key = !lua_istable(L, -1);
    if (is_new_key) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name);                          // handlers[name]=list
    }
    int len = (int)lua_rawlen(L, -1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, len + 1);
    lua_pop(L, 2);

    if (is_new_key) InterlockedIncrement(&g_handlers);
    return 0;
}

static int l_clear_triggers(lua_State* L) {
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, SDK_HANDLERS_KEY);
    g_handlers = 0;
    return 0;
}

// sacred.on_tick(fn) — fn() is pcalled ~every 250 ms while in-world
// (driven off the sacred_hash heartbeat). Ideal for polling hero
// position, e.g. "has the player reached the quest marker?".
static int l_on_tick(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, SDK_TICK_KEY);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, SDK_TICK_KEY);
    }
    int len = (int)lua_rawlen(L, -1);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, len + 1);
    lua_pop(L, 1);
    return 0;
}

// sacred.on_world_load(fn) — registers a handler that fires ONCE per
// Sacred process, the moment the FunkCode walker first captures the
// cQuestManager pointer (= a save is loaded, world is alive, walker's
// per-record loop hasn't yet started). The right place to call
// `sacred.questbook_register(...)` so new quest_ids are present before
// tag-0x35 (log_entry) records dispatch.
//
// LIMITATION: switching to a different save within the same Sacred
// session won't re-fire — cQuestManager is a singleton and we keep the
// pointer cached. Workaround: restart Sacred between saves while
// testing. (A multi-fire variant would need a hook on cObjectManager
// load instead of the walker entry; tracked as a future SDK issue.)
static int l_on_world_load(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, SDK_WORLD_LOAD_KEY);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, SDK_WORLD_LOAD_KEY);
    }
    int len = (int)lua_rawlen(L, -1);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, len + 1);
    lua_pop(L, 1);
    return 0;
}

// -------------------------------------------------------------------------
// sacred.notify(text) -> bool
//   shows a top-of-screen banner ("Quest completed" style). Returns true
//   if the message was forwarded to Sacred's queue, false if throttled /
//   deduped / truncated-to-empty.
// -------------------------------------------------------------------------
static int l_sacred_notify(lua_State* L) {
    size_t len = 0;
    const char* msg = luaL_checklstring(L, 1, &len);
    InterlockedIncrement(&g_notify_calls);

    if (!g_notify_cs_init) {
        InitializeCriticalSection(&g_notify_cs);
        g_notify_cs_init = true;
    }
    EnterCriticalSection(&g_notify_cs);

    char clipped[NOTIFY_MAX_LEN];
    size_t clip_len = (len < NOTIFY_MAX_LEN - 1) ? len : NOTIFY_MAX_LEN - 1;
    memcpy(clipped, msg, clip_len);
    clipped[clip_len] = 0;
    if (clip_len == 0) {
        LeaveCriticalSection(&g_notify_cs);
        InterlockedIncrement(&g_notify_dropped);
        lua_pushboolean(L, 0);
        return 1;
    }

    DWORD now = GetTickCount();
    bool throttled = (now - g_last_notify_tick) < NOTIFY_MIN_GAP_MS;
    bool duplicate = (strcmp(clipped, g_last_notify_text) == 0);
    if (throttled || duplicate) {
        LeaveCriticalSection(&g_notify_cs);
        InterlockedIncrement(&g_notify_dropped);
        lua_pushboolean(L, 0);
        return 1;
    }

    g_last_notify_tick = now;
    strncpy_s(g_last_notify_text, sizeof(g_last_notify_text),
              clipped, _TRUNCATE);
    LeaveCriticalSection(&g_notify_cs);

    bool ok = sacred_show_banner(clipped);
    if (ok) InterlockedIncrement(&g_notify_passed);
    else    InterlockedIncrement(&g_notify_dropped);
    sdk_log("[notify] %s", clipped);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// -------------------------------------------------------------------------
// ctx:gold() / ctx:give_gold(N) / ctx:has_item(N) / ctx:set_qbit(N,V) etc.
// Each method receives `self` (ctx table) as arg 1.
// -------------------------------------------------------------------------
static int l_ctx_gold(lua_State* L) {
    int g = sacred_get_hero_gold();
    if (g < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, g);
    return 1;
}
static int l_ctx_give_gold(lua_State* L) {
    int amount = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, sacred_give_hero_gold(amount));
    return 1;
}
static int l_ctx_give_gold_silent(lua_State* L) {
    int amount = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, sacred_give_hero_gold_silent(amount));
    return 1;
}
static int l_ctx_charge_gold(lua_State* L) {
    int amount = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, sacred_give_hero_gold(-amount));
    return 1;
}
static int l_ctx_has_item(lua_State* L) {
    int item_res = (int)luaL_checkinteger(L, 2);
    int r = sacred_hero_has_item(item_res);
    if (r < 0) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, r);
    return 1;
}
static int l_ctx_set_qbit(lua_State* L) {
    int bit = (int)luaL_checkinteger(L, 2);
    bool val = lua_isnone(L, 3) ? true : (lua_toboolean(L, 3) != 0);
    lua_pushboolean(L, sacred_set_hero_qbit(bit, val));
    return 1;
}
static int l_ctx_get_qbit(lua_State* L) {
    int bit = (int)luaL_checkinteger(L, 2);
    int r = sacred_get_hero_qbit(bit);
    if (r < 0) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, r);
    return 1;
}
static int l_ctx_notify(lua_State* L) {
    // ctx:notify("…") forwards to sacred.notify("…"). We just shuffle
    // the args (drop `self`, keep the message) and call l_sacred_notify.
    lua_remove(L, 1);  // drop self
    return l_sacred_notify(L);
}

// -------------------------------------------------------------------------
// Named-state bindings — forwards into [cQuestManager+0x334..+0x338].
//
// Lua surface (both global and ctx-method form):
//   sacred.state_dump()                       -> count (also logs to file)
//   sacred.state_get(name)                    -> {v1, v2, v3, v4} or nil
//   sacred.state_set(name, v0[, v1, v2, v3])  -> bool (in-place only)
//   ctx:get_var(name)                         -> first value (int) or nil
//   ctx:set_var(name, v0[, v1, v2, v3])       -> bool
//
// `state_get` returns the FULL 4-int array because some quest variables
// pack multiple values into one slot. `ctx:get_var` returns just the first
// value as an integer because that's what 90 % of script logic wants.
// -------------------------------------------------------------------------

// Caller owns the buffer; we cap at 256 rows to keep stack pressure in check.
constexpr int STATE_DUMP_MAX = 256;

// Forward decl needed by both the questbook block (which gates on
// "world loaded?") and the named-state Lua bindings further down.
// Definition lives near the bottom alongside the walker hook.
extern volatile uintptr_t g_quest_mgr;

// =========================================================================
// Quest-display registry — read + register from Lua.
// =========================================================================
//
// Recon: see sdk/.claude/knowledge/re/questbook_inserter.md. The display-registry
// vector lives at ABSOLUTE GLOBALS (not behind a cQuestManager pointer):
//
//   DAT_00aad3a4   uint8_t* begin     (= [cQuestManager + 0x424] alias)
//   DAT_00aad3a8   uint8_t* end
//   stride         0x174 (372 bytes)
//   key            (uint32_t) at entry+0x08
//
// Engine path for inserting an entry is `FUN_00806d20` — but it only ever
// fires from `case 0x9a` of the master record dispatcher (savegame /
// network 0x9a records). FunkCode tags 0x35/0x57/0x40/0x3f/0x75/0x4d
// only MUTATE existing entries. That's why brand-new quest_ids silently
// no-op end-to-end without the SDK's help.
//
// Our SDK uses Option A from the recon: call the underlying vector::resize
// directly to grow the array, then stamp `entry+8 = quest_id`. Active-hero
// bookkeeping (quest_id == 100/0x65) is skipped — caller takes that risk
// (the only known users of that path are vanilla, not custom mods).
//
// Lifecycle: the registry is REBUILT from the savegame each load, so the
// mod must re-register its quest_ids on every world-load. Cheap pattern:
// register from a `sacred.on_trigger(...)` handler that fires once early
// in the session.

constexpr uintptr_t QB_REGISTRY_BEGIN_VA  = 0x00aad3a4;   // uint8_t** begin
constexpr uintptr_t QB_REGISTRY_END_VA    = 0x00aad3a8;   // uint8_t** end
constexpr uintptr_t QB_REGISTRY_RESIZE_VA = 0x004b5370;   // vector::resize
constexpr uintptr_t QB_ENTRY_STRIDE       = 0x174;
constexpr uintptr_t QB_ENTRY_OFF_QID      = 0x08;

// --- SDK-owned quest-id allow-list ---------------------------------------
// Every quest_id the SDK itself registers is recorded here. The "hide other
// quests" suppressor (Variant A — keep vanilla scripts 100% intact, just
// stop the *vanilla* quests from showing in the journal/map) zeroes the
// render gate on any registry entry whose quest_id is NOT in this list.
// Fixed array, no STL dependency.
static uint32_t g_sdk_qids[128];
static int      g_sdk_qids_n = 0;
static void sdk_qid_add(uint32_t id) {
    for (int i = 0; i < g_sdk_qids_n; i++) if (g_sdk_qids[i] == id) return;
    if (g_sdk_qids_n < (int)(sizeof(g_sdk_qids) / sizeof(g_sdk_qids[0])))
        g_sdk_qids[g_sdk_qids_n++] = id;
}
static bool sdk_qid_has(uint32_t id) {
    for (int i = 0; i < g_sdk_qids_n; i++) if (g_sdk_qids[i] == id) return true;
    return false;
}

// __thiscall: `this` = pointer to vector struct (begin / end / cap triple
// at 0x00aad3a4..0x00aad3ac), arg1 = new element count. Callee-pops the
// stack arg per MSVC convention. Verified against FUN_004b5370 decompile
// at decompiled/004b5370_FUN_004b5370.c.
typedef void (__thiscall *qb_resize_t)(void* this_, uint32_t new_count);

static int questbook_count_safe() {
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        uintptr_t end   = *(uintptr_t*)QB_REGISTRY_END_VA;
        if (!begin || end < begin) return 0;
        return (int)((end - begin) / QB_ENTRY_STRIDE);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Read quest_id at the given slot. Returns -1 on out-of-range / fault.
static int64_t questbook_quest_id_at(int idx) {
    int n = questbook_count_safe();
    if (idx < 0 || idx >= n) return -1;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        uintptr_t entry = begin + (uintptr_t)idx * QB_ENTRY_STRIDE;
        return (int64_t)(uint32_t)*(uint32_t*)(entry + QB_ENTRY_OFF_QID);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Append a new entry, stamp its quest_id field. Returns the new slot's
// index on success, -1 on failure. Gating: refuse if the cQuestManager
// walker hasn't fired yet (= save not loaded), because vector::resize on
// an uninitialized vector struct would crash.
static int questbook_register_impl(uint32_t quest_id) {
    if (!g_quest_mgr) {
        sdk_log("[questbook] register refused: world not loaded yet "
                "(cQuestManager not captured)");
        return -1;
    }
    int before = questbook_count_safe();
    if (before < 0) {
        sdk_log("[questbook] register refused: count read faulted");
        return -1;
    }
    uint32_t new_count = (uint32_t)(before + 1);

    // Sanity: refuse if this quest_id already exists, to avoid duplicate
    // entries that would confuse the mutator scan.
    for (int i = 0; i < before; i++) {
        if (questbook_quest_id_at(i) == (int64_t)quest_id) {
            sdk_qid_add(quest_id);
            sdk_log("[questbook] register skipped: quest_id=%u already at idx=%d",
                    quest_id, i);
            return i;
        }
    }

    qb_resize_t resize = (qb_resize_t)QB_REGISTRY_RESIZE_VA;
    __try {
        resize((void*)QB_REGISTRY_BEGIN_VA, new_count);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[questbook] register: resize(%u) raised — abort",
                new_count);
        return -1;
    }

    int after = questbook_count_safe();
    if (after != (int)new_count) {
        sdk_log("[questbook] register: post-resize count=%d, expected %u",
                after, new_count);
        return -1;
    }

    // Stamp quest_id into entry+8. The rest of the entry is whatever the
    // resize default-constructed via the copy ctor — typically zeros + the
    // installed vtable pointer. Mutator handlers read quest_id at +8 to
    // match, so this single write makes the entry "discoverable".
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        uintptr_t entry = begin + (uintptr_t)before * QB_ENTRY_STRIDE;
        *(uint32_t*)(entry + QB_ENTRY_OFF_QID) = quest_id;
        sdk_qid_add(quest_id);
        sdk_log("[questbook] registered quest_id=%u at idx=%d entry=%p",
                quest_id, before, (void*)entry);
        return before;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[questbook] register: quest_id stamp faulted");
        return -1;
    }
}

// Variant A suppressor: keep all vanilla FunkCode 100% intact (so the
// new-game intro / Shaddar cinematic / fade-in — which are quest-trigger
// chains wrapped in IF/BlockReader blocks — are NOT corrupted by record
// deletion), and instead stop *vanilla* quests from appearing. For every
// registry entry whose quest_id is NOT SDK-owned, zero the journal render
// gate (+0x24) and the map-marker coords (+0x10/+0x14). The vector entry
// itself is left in place (no resize → no engine desync); the quest logic
// still runs, it just never shows in the journal or on the map. Idempotent
// — safe to call every tick (re-hides quests the script re-activates).
// Returns the number of entries hidden, or -1 on fault.
static int questbook_hide_others_impl() {
    int n = questbook_count_safe();
    if (n <= 0) return n;
    int hidden = 0;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        for (int i = 0; i < n; i++) {
            uintptr_t e   = begin + (uintptr_t)i * QB_ENTRY_STRIDE;
            uint32_t  qid = *(uint32_t*)(e + QB_ENTRY_OFF_QID);
            if (qid == 0 || sdk_qid_has(qid)) continue;     // keep SDK quests
            bool any = false;
            if (*(uint32_t*)(e + 0x24) != 0) {              // journal gate
                *(uint32_t*)(e + 0x24) = 0; any = true;
            }
            if (*(uint32_t*)(e + 0x10) != 0 ||
                *(uint32_t*)(e + 0x14) != 0) {              // kompass marker
                *(uint32_t*)(e + 0x10) = 0;
                *(uint32_t*)(e + 0x14) = 0; any = true;
            }
            if (any) hidden++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return hidden;
}

// sacred.hide_other_quests() -> n   (hidden count, nil on fault)
// Call from on_tick in a class mod to keep the journal free of that
// class's built-in quests while leaving the scripts (and intro) vanilla.
static int l_sacred_hide_other_quests(lua_State* L) {
    int h = questbook_hide_others_impl();
    if (h < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, h);
    return 1;
}

// sacred.hide_vanilla_quests(on) -> on
// Toggle the race-free journal-builder suppressor (hook on FUN_006b07e0).
// While on, every NON-SDK quest's render gate (+0x24) and map marker are
// zeroed right before the journal is built, so vanilla quests never show
// — script logic and the new-game intro stay 100% vanilla. Call ONCE
// (e.g. at mod top); persists for the process.
extern volatile bool g_hide_vanilla;
static int l_sacred_hide_vanilla_quests(lua_State* L) {
    bool on = true;
    if (lua_gettop(L) >= 1) on = lua_toboolean(L, 1) != 0;
    g_hide_vanilla = on;
    sdk_log("[questbook] hide_vanilla_quests = %s", on ? "ON" : "OFF");
    lua_pushboolean(L, on);
    return 1;
}

// -------------------------------------------------------------------------
// Lua bindings: sacred.questbook_count / dump / register / get_id.
// -------------------------------------------------------------------------

static int l_sacred_questbook_count(lua_State* L) {
    int n = questbook_count_safe();
    if (n < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, n);
    return 1;
}

// Shared dump impl — used by both the Lua binding and the overlay F8 key.
// Returns the entry count (or -1 if the registry isn't readable).
static int questbook_dump_impl() {
    int n = questbook_count_safe();
    if (n < 0) {
        sdk_log("[questbook] dump: registry not readable "
                "(world not loaded / globals not mapped)");
        return -1;
    }
    sdk_log("[questbook] === dump: %d entries (cQuestManager=%p) ===",
            n, (void*)g_quest_mgr);
    uintptr_t begin = 0;
    __try { begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA; }
    __except (EXCEPTION_EXECUTE_HANDLER) { begin = 0; }
    for (int i = 0; i < n; i++) {
        int64_t qid = questbook_quest_id_at(i);
        sdk_log("[questbook]   %3d. quest_id=%lld (0x%llx)", i, qid, qid);
        if (!begin) continue;
        uintptr_t e = begin + (uintptr_t)i * QB_ENTRY_STRIDE;
        __try {
            // Header region (+0x00..+0x23): type/flags/quest_id/state.
            const uint8_t* b = (const uint8_t*)e;
            sdk_log("[questbook]        hdr  +00: %02x %02x %02x %02x  %02x %02x %02x %02x  "
                    "%02x %02x %02x %02x  %02x %02x %02x %02x",
                    b[0],b[1],b[2],b[3], b[4],b[5],b[6],b[7],
                    b[8],b[9],b[10],b[11], b[12],b[13],b[14],b[15]);
            sdk_log("[questbook]        hdr  +10: %02x %02x %02x %02x  %02x %02x %02x %02x  "
                    "%02x %02x %02x %02x  %02x %02x %02x %02x",
                    b[16],b[17],b[18],b[19], b[20],b[21],b[22],b[23],
                    b[24],b[25],b[26],b[27], b[28],b[29],b[30],b[31]);
            // Log slots: +0x24 (slot 0) then +0x28.. (slots 1..10), as u32.
            const uint32_t* L = (const uint32_t*)(e + 0x24);
            sdk_log("[questbook]        log  s0=%08x  s1=%08x s2=%08x s3=%08x "
                    "s4=%08x s5=%08x", L[0], L[1], L[2], L[3], L[4], L[5]);
            sdk_log("[questbook]        log  s6=%08x s7=%08x s8=%08x s9=%08x "
                    "s10=%08x", L[6], L[7], L[8], L[9], L[10]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sdk_log("[questbook]        (entry body read faulted)");
        }
    }
    sdk_log("[questbook] === end dump ===");
    return n;
}

// Public entry point (declared in sdk.h) — bound to the overlay F8 key.
void questbook_dump_to_log() {
    questbook_dump_impl();
}

static int l_sacred_questbook_dump(lua_State* L) {
    lua_pushinteger(L, questbook_dump_impl());
    return 1;
}

static int l_sacred_questbook_register(lua_State* L) {
    lua_Integer qid = luaL_checkinteger(L, 1);
    if (qid < 0 || qid > 0xFFFFFFFFLL) {
        return luaL_error(L, "sacred.questbook_register: quest_id %lld "
                             "out of u32 range", (long long)qid);
    }
    int idx = questbook_register_impl((uint32_t)qid);
    if (idx < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, idx);
    return 1;
}

static int l_sacred_questbook_get_id(lua_State* L) {
    int idx = (int)luaL_checkinteger(L, 1);
    int64_t qid = questbook_quest_id_at(idx);
    if (qid < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, qid);
    return 1;
}

// =========================================================================
// Direct entry population — the working path for NEW quests.
// =========================================================================
//
// tag-0x35 (QuestLogSet) records in custom FunkCode/QuestCode do NOT
// execute at world-load for a brand-new quest (vanilla FunkCode is
// structured into quest blocks gated by progression; appended records
// sit in an unreached path; vanilla entries get their log slots from the
// savegame 0x9a stream instead). So instead of relying on the engine to
// run our tag-0x35, we populate the registry entry DIRECTLY after
// registering it.
//
// Recon (sdk/.claude/knowledge/re/questbook_resolver.md + questbook_render.md):
//   * journal render gate (FUN_006b07e0): show entry iff
//       *(u32*)(entry+0x24) != 0  AND
//       *(u16*)(entry+0x16C) == page+1   (or ==0 on page 0)
//   * the +0x24 value is what FUN_00672740(name) returns:
//       sacred_hash31(name) | 0x80000000   (0 if name not in the loaded
//       resource dictionary — so the string MUST exist; we don't fake it)
//   * log line 0 -> +0x24, lines 1.. -> +0x28, +0x2C, … (u32 each)
//   * +0x00 = 3 and +0x04 = 1 mirror a normal vanilla quest (icon /
//     text-helper path); not hard gates but match vanilla shape.
//
// FUN_00672740 is __cdecl(const char* name) — bare name, NO "res:"
// prefix (the interpreter strips it). Needs the resource dictionary
// loaded, which it is post world-load (call from sacred.on_world_load).

constexpr uintptr_t QB_RESOLVE_VA       = 0x00672740;
constexpr uintptr_t QB_ENTRY_OFF_TYPE   = 0x00;   // u32, vanilla=3
constexpr uintptr_t QB_ENTRY_OFF_KIND   = 0x04;   // u32, vanilla 1/2/100
constexpr uintptr_t QB_ENTRY_OFF_LOG0   = 0x24;   // u32 log slot 0 (gate 1)
constexpr uintptr_t QB_ENTRY_OFF_LOGN   = 0x28;   // u32 log slots 1..10
constexpr uintptr_t QB_ENTRY_OFF_PAGE   = 0x16C;  // u16 page id  (gate 2)

// Sacred's resource-name hash (the engine's FUN_0080eaa0 / sacred_hash31).
// Pure C++ port of the verified Lua port in custom/lua/lib/text.lua
// (which is checked against all 823 ids in hash_names.csv). We compute
// the handle OURSELVES instead of calling the engine resolver
// FUN_00672740 — that resolver is __thiscall-into a lazily-created
// resource-cache singleton (DAT_017e7f68) and calling it at on_world_load
// (first walker entry, dict not yet built) crashes hard in engine code,
// uncatchable by SEH.
//
// We don't need the engine call: the journal text formatter (FUN_006b4940,
// reached from the builder FUN_006b07e0) itself does
//   (id & 0x80000000) ? FUN_0080eaf0(id & 0x7fffffff) : ...
// i.e. it RE-RESOLVES the hash against the dictionary at RENDER time —
// when the journal is open and the dict is guaranteed loaded. So all we
// must deposit at entry+0x24 is `sacred_hash31(name) | 0x80000000`.
// Pure arithmetic, no engine state, safe to call any time.
static uint32_t sacred_hash31(const char* s) {
    const uint32_t MOD = 999999991u;     // 0x3B9AC9F7
    const uint32_t MUL = 113u;           // 0x71
    uint32_t h = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        uint32_t oc = *p;
        if (oc >= 0x61 && oc <= 0x7a) oc -= 0x20;     // ascii lower->upper
        uint32_t sm = oc + h * MUL;                   // both mod 2^32
        int64_t  si = (int32_t)sm;                    // signed 32-bit view
        int64_t  r;
        if (si >= 0) r =  (si        % (int64_t)MOD);
        else         r = -((-si)     % (int64_t)MOD);
        if (r < 0)   r += 0x100000000LL;
        h = (uint32_t)(r & 0xFFFFFFFFu);
    }
    return h & 0x7FFFFFFFu;
}

// Returns the +0x24 handle for a resource name (hash | 0x80000000), or 0
// for an empty/null name. Strips a leading "res:" if present. NO engine
// call — see sacred_hash31 note above for why.
static unsigned questbook_resolve_name(const char* name) {
    if (!name || !*name) return 0;
    if (strncmp(name, "res:", 4) == 0) name += 4;
    if (!*name) return 0;
    return sacred_hash31(name) | 0x80000000u;
}

// Populate an existing registry entry's log slots + render-gate fields so
// the journal builder shows it. `names[0]` -> +0x24 (title line), then
// +0x28, +0x2C, … for the rest. `page` 0 => first journal tab.
// Returns: 0 ok, -1 entry not found, -2 a name didn't resolve (entry left
// untouched in that case so we never write a half-built row).
static int questbook_set_log_impl(uint32_t quest_id, int page,
                                  const char* const* names, int n_names) {
    if (n_names <= 0 || n_names > 11) return -2;

    // Resolve ALL names FIRST. FUN_00672740 walks the resource-dictionary
    // singleton and can trigger growth of the quest-registry vector as a
    // side effect — so we must NOT hold an entry pointer across it. Resolve
    // up front, then scan + write back-to-back with nothing in between that
    // could realloc/move the vector.
    unsigned handles[11] = {0};
    for (int i = 0; i < n_names; i++) {
        handles[i] = questbook_resolve_name(names[i]);
        if (handles[i] == 0) {
            sdk_log("[questbook] set_log: name #%d '%s' did not resolve "
                    "(not in loaded resource dictionary) — aborting, "
                    "entry left clean", i, names[i] ? names[i] : "(null)");
            return -2;
        }
    }

    // NOW locate the entry (fresh begin) and write immediately.
    uintptr_t e = 0;
    int cnt = questbook_count_safe();
    if (cnt <= 0) return -1;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        for (int i = 0; i < cnt; i++) {
            uintptr_t cand = begin + (uintptr_t)i * QB_ENTRY_STRIDE;
            if (*(uint32_t*)(cand + QB_ENTRY_OFF_QID) == quest_id) { e = cand; break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (!e) return -1;

    __try {
        // Ground-truth from live F8 dumps of vanilla side quest 15001
        // (active) vs main quest 1: +0x00 is the quest CATEGORY
        // (3 = main, 4 = side) — NOT a completed flag. +0x04 = 2 for an
        // active quest (vanilla bumps it to 100 only at a late progress
        // stage). +0x0C = 1 on every active vanilla quest. Our previous
        // +00=0 made the row look invalid/"completed"; +00=3 had looked
        // "primary" because 3 == main category. Match an active SIDE
        // quest exactly:
        // +0x00 = quest CATEGORY: 3 = MAIN/story, 4 = side. The journal
        // builder FUN_006b07e0:109 tests `==3` to pick the main-quest
        // icon set + the active-quest header — so 3 is what makes 9550
        // render as a STORY quest (was 4 = secondary). (If SDK side
        // quests are added later, parameterize this per quest_id.)
        *(uint32_t*)(e + QB_ENTRY_OFF_TYPE) = 3;   // 3 = MAIN/story
        *(uint32_t*)(e + QB_ENTRY_OFF_KIND) = 2;   // 2 = active state
        *(uint32_t*)(e + 0x0C)              = 1;   // vanilla active = 1
        *(uint32_t*)(e + QB_ENTRY_OFF_LOG0) = handles[0];
        for (int i = 1; i < n_names; i++) {
            *(uint32_t*)(e + QB_ENTRY_OFF_LOGN + (uintptr_t)(i - 1) * 4) = handles[i];
        }
        *(uint16_t*)(e + QB_ENTRY_OFF_PAGE) =
            (uint16_t)(page <= 0 ? 0 : (page + 1));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[questbook] set_log: write to entry %p faulted", (void*)e);
        return -1;
    }
    sdk_log("[questbook] set_log quest_id=%u page=%d lines=%d "
            "(+0x24=%08x) @ %p", quest_id, page, n_names,
            handles[0], (void*)e);
    return 0;
}

// sacred.questbook_set_log(quest_id, page, name0 [, name1, name2, ...])
//   names are resource names (with or without "res:" prefix). Each MUST
//   already exist in global.res (reskinned vanilla names work; brand-new
//   T"" strings work only if text.flush successfully added them to the
//   loaded dictionary). Returns true on success, false otherwise.
// Populate the kompass/position block (+0x10..+0x20) so the journal's
// location preview image renders instead of a black square. Vanilla
// entries carry coordinate-ish values here (e.g. HQ_3_2_1 / quest_id 9:
// +0x10=0x0c81 +0x14=0x09a9 +0x18=0x0c81 +0x1C=0x09a9). FUN_004a5980
// (kompass resolver) + the journal builder consume these. Writes only
// the slots the caller provides (1..5 values -> +0x10,+0x14,+0x18,
// +0x1C,+0x20). Returns 0 ok / -1 entry not found.
static int questbook_set_kompass_impl(uint32_t quest_id,
                                      const uint32_t* vals, int n) {
    if (n <= 0 || n > 5) return -1;
    uintptr_t e = 0;
    int cnt = questbook_count_safe();
    if (cnt <= 0) return -1;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        for (int i = 0; i < cnt; i++) {
            uintptr_t cand = begin + (uintptr_t)i * QB_ENTRY_STRIDE;
            if (*(uint32_t*)(cand + QB_ENTRY_OFF_QID) == quest_id) { e = cand; break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (!e) return -1;
    __try {
        for (int i = 0; i < n; i++)
            *(uint32_t*)(e + 0x10 + (uintptr_t)i * 4) = vals[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[questbook] set_kompass: write faulted @ %p", (void*)e);
        return -1;
    }
    sdk_log("[questbook] set_kompass quest_id=%u n=%d (+0x10=%08x) @ %p",
            quest_id, n, vals[0], (void*)e);
    return 0;
}

// sacred.questbook_set_kompass(quest_id, v0 [, v1, v2, v3, v4])
//   v0->+0x10, v1->+0x14, v2->+0x18, v3->+0x1C, v4->+0x20.
static int l_sacred_questbook_set_kompass(lua_State* L) {
    lua_Integer qid = luaL_checkinteger(L, 1);
    int top = lua_gettop(L);
    int n = top - 1;
    if (n <= 0) return luaL_error(L, "questbook_set_kompass: need >=1 value");
    if (n > 5) n = 5;
    uint32_t vals[5] = {0};
    for (int i = 0; i < n; i++)
        vals[i] = (uint32_t)(int64_t)luaL_checkinteger(L, 2 + i);
    int r = questbook_set_kompass_impl((uint32_t)qid, vals, n);
    lua_pushboolean(L, r == 0 ? 1 : 0);
    lua_pushinteger(L, r);
    return 2;
}

// =========================================================================
// Quest functionality: map marker, objective bullet, completion.
// (Recon: sdk/.claude/knowledge/re/quest_lifecycle.md)
// =========================================================================
//
// Marker: quest_id 9512 > 100 so the per-class slot-0 path is gated out;
// we use the slot-3 single-target marker the engine drives for both
// minimap and world map. It lives at fixed cQuestMgr offsets:
//   +0x7704 = world X (literal coord, same space as KompassPos 0..6279)
//   +0x7708 = world Y
//   +0x7718 = 1   (enable / valid flag)
//   +0x771C = 0
// FUN_004a5980 slot 3 reads these. (HIGH conf on fields; exact record
// layout MEDIUM — verify the dot lands right in-game.)
//
// Completion: the journal has no qbit/hero flag — a quest is "solved"
// either by greying (+0x00) or by removing the entry from the vector
// (vanilla "solved → gone": copy last entry over it, decrement end ptr;
// mirror of tag-0x4d FUN_0048e600). We expose the remove path.
//
// Objective bullet: entry+0x0C bit0 → filled ("done") vs hollow ("open").

// Slot-3 (single tracked target, WHITE/primary look) — we now clear it
// and use slot-1 instead so a quest_id>100 gets the SECONDARY side-quest
// marker style (see .claude/knowledge/re/quest_marker_pos.md Q2).
constexpr uintptr_t QB_S3_X       = 0x7704;
constexpr uintptr_t QB_S3_Y       = 0x7708;
constexpr uintptr_t QB_S3_ON      = 0x7718;
constexpr uintptr_t QB_S3_AUX     = 0x771C;
// Slot-1 per-class column: cQuestMgr+0x3a0 + C*8 holds the entry INDEX.
constexpr uintptr_t QB_SLOT1_BASE = 0x3a0;
constexpr uintptr_t QB_ENTRY_OFF_PRIO   = 0x20;   // priority/range (0)
constexpr uintptr_t QB_ENTRY_OFF_BULLET = 0x0C;   // bit0 = step done
constexpr uintptr_t QB_CTX_GLOBAL = 0x0182EBE8;   // active-context singleton

// Active class/hero index = *(*(0x0182EBE8) + 0x14). Used to index the
// per-class slot-1 marker column. 0 on failure.
static uint32_t questbook_active_class() {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return 0;
    uintptr_t rebase = (uintptr_t)exe - 0x00400000;
    __try {
        uintptr_t ctx = *(uintptr_t*)(rebase + QB_CTX_GLOBAL);
        if (!ctx) return 0;
        uint32_t c = *(uint32_t*)(ctx + 0x14);
        return (c >= 1 && c <= 16) ? c : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Secondary-style map/minimap marker for `quest_id`. Writes the literal
// world coord onto the entry (+0x10/+0x14, +0x20=0), registers the entry
// index in the slot-1 per-class column, and clears slot-3 so the old
// white primary marker stops drawing. quest_id must satisfy the slot-1
// gate: id>=100, entry+4<99, and NOT 8999<id<9500 (9512 passes).
static int questbook_set_marker_impl(uint32_t quest_id, int32_t wx, int32_t wy) {
    uintptr_t mgr = g_quest_mgr;
    if (!mgr) return -1;
    uint32_t C = questbook_active_class();
    if (C == 0) { sdk_log("[questbook] set_marker: no active class"); return -1; }

    int cnt = questbook_count_safe();
    if (cnt <= 0) return -1;
    int found = -1;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        for (int i = 0; i < cnt; i++) {
            if (*(uint32_t*)(begin + (uintptr_t)i * QB_ENTRY_STRIDE
                             + QB_ENTRY_OFF_QID) == quest_id) { found = i; break; }
        }
        if (found < 0) return -1;
        uintptr_t e = begin + (uintptr_t)found * QB_ENTRY_STRIDE;
        // literal-coord marker source (must NOT be -1 / 0xEEEEEEEE)
        *(uint32_t*)(e + 0x10)              = (uint32_t)wx;
        *(uint32_t*)(e + 0x14)              = (uint32_t)wy;
        *(uint32_t*)(e + QB_ENTRY_OFF_PRIO) = 0;
        // MAIN-style marker (RE: quest_lifecycle.md "Map-marker
        // main-vs-secondary"). Drive SLOT-3 — the white PRIMARY
        // compass arrow + the category-aware world-map icon, which
        // re-reads quest-entry +0x00 (==3 ⇒ MAIN sprite 0x8A / colour
        // 0xFFFFFF80, else SIDE). Do NOT register the slot-1 per-class
        // column (the generic GREEN side-quest arrow, no category) —
        // that was exactly why the map showed secondary while the
        // journal (also +0x00==3) showed primary. entry +0x00=3 /
        // +0x04=2 are already set by questbook_set_log_impl.
        *(uint32_t*)(mgr + QB_S3_X)   = (uint32_t)wx;
        *(uint32_t*)(mgr + QB_S3_Y)   = (uint32_t)wy;
        *(uint32_t*)(mgr + QB_S3_ON)  = 1;
        *(uint32_t*)(mgr + QB_S3_AUX) = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[questbook] set_marker write faulted");
        return -1;
    }
    (void)C;
    sdk_log("[questbook] set_marker quest_id=%u idx=%d x=%d y=%d "
            "(slot-3 PRIMARY, category-aware)", quest_id, found, wx, wy);
    return 0;
}

// Locate entry by quest_id (fresh begin). Returns addr or 0.
static uintptr_t questbook_find_entry(uint32_t quest_id) {
    int cnt = questbook_count_safe();
    if (cnt <= 0) return 0;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        for (int i = 0; i < cnt; i++) {
            uintptr_t c = begin + (uintptr_t)i * QB_ENTRY_STRIDE;
            if (*(uint32_t*)(c + QB_ENTRY_OFF_QID) == quest_id) return c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 0;
}

// Mark a quest SOLVED, exactly like vanilla. Ground truth from live F8
// of side quest 15001 transitioning to completed: +0x04 goes 2 -> 100
// (0x64) and the kompass coord (+0x10/+0x14) is zeroed (no marker once
// done). +0x00 (category) and +0x0C stay. This is THE visual "completed"
// state — without +0x04=100 the row keeps the active look.
static int questbook_mark_solved_impl(uint32_t quest_id) {
    uintptr_t e = questbook_find_entry(quest_id);
    if (!e) return -1;
    __try {
        *(uint32_t*)(e + QB_ENTRY_OFF_KIND) = 100;   // 2->100 = solved
        *(uint32_t*)(e + 0x10) = 0;                  // clear kompass src
        *(uint32_t*)(e + 0x14) = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    sdk_log("[questbook] mark_solved quest_id=%u (+0x04=100, kompass cleared)",
            quest_id);
    return 0;
}

// Append ONE journal line: write the resolved handle into the first
// empty log slot (+0x24 then +0x28 + i*4, i=0..9). This is how vanilla
// quests grow their journal as they progress (we observed 15001 gain
// s2 then s3 across stages). Returns slot index (0..10) or -1.
static int questbook_add_log_impl(uint32_t quest_id, const char* name) {
    unsigned h = questbook_resolve_name(name);
    if (h == 0) {
        sdk_log("[questbook] add_log: name '%s' did not resolve",
                name ? name : "(null)");
        return -1;
    }
    uintptr_t e = questbook_find_entry(quest_id);
    if (!e) return -1;
    __try {
        if (*(uint32_t*)(e + QB_ENTRY_OFF_LOG0) == 0) {
            *(uint32_t*)(e + QB_ENTRY_OFF_LOG0) = h;
            sdk_log("[questbook] add_log quest_id=%u slot0 (+0x24)=%08x",
                    quest_id, h);
            return 0;
        }
        for (int i = 0; i < 10; i++) {
            uintptr_t slot = e + QB_ENTRY_OFF_LOGN + (uintptr_t)i * 4;
            if (*(uint32_t*)slot == 0) {
                *(uint32_t*)slot = h;
                sdk_log("[questbook] add_log quest_id=%u slot%d "
                        "(+0x%x)=%08x", quest_id, i + 1,
                        (unsigned)(QB_ENTRY_OFF_LOGN + i * 4), h);
                return i + 1;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    sdk_log("[questbook] add_log quest_id=%u: all 11 log slots full",
            quest_id);
    return -1;
}

// Objective bullet: done=true → filled (+0x0C bit0 set), false → hollow.
static int questbook_set_step_done_impl(uint32_t quest_id, bool done) {
    uintptr_t e = questbook_find_entry(quest_id);
    if (!e) return -1;
    __try {
        uint8_t b = *(uint8_t*)(e + QB_ENTRY_OFF_BULLET);
        *(uint8_t*)(e + QB_ENTRY_OFF_BULLET) = done ? (b | 1) : (b & ~1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    sdk_log("[questbook] set_step_done quest_id=%u done=%d", quest_id, done);
    return 0;
}

// Completion via removal (vanilla tag-0x4d behaviour): last-over-target,
// then pop the vector end pointer. The entry vanishes from the journal.
static int questbook_complete_remove_impl(uint32_t quest_id) {
    int cnt = questbook_count_safe();
    if (cnt <= 0) return -1;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        int found = -1;
        for (int i = 0; i < cnt; i++) {
            if (*(uint32_t*)(begin + (uintptr_t)i * QB_ENTRY_STRIDE
                             + QB_ENTRY_OFF_QID) == quest_id) { found = i; break; }
        }
        if (found < 0) return -1;
        uintptr_t tgt  = begin + (uintptr_t)found * QB_ENTRY_STRIDE;
        uintptr_t last = begin + (uintptr_t)(cnt - 1) * QB_ENTRY_STRIDE;
        if (tgt != last) memcpy((void*)tgt, (void*)last, QB_ENTRY_STRIDE);
        *(uintptr_t*)QB_REGISTRY_END_VA -= QB_ENTRY_STRIDE;   // pop
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[questbook] complete/remove faulted");
        return -1;
    }
    sdk_log("[questbook] complete(remove) quest_id=%u", quest_id);
    return 0;
}

// sacred.give_gold(n) -> bool   (global form of ctx:give_gold — usable
// from on_tick / on_world_load where there's no ctx). n>0 grants, n<0
// charges; does the silent hero-struct write + the coin/text event.
static int l_sacred_give_gold(lua_State* L) {
    int amount = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, sacred_give_hero_gold(amount));
    return 1;
}

// sacred.hero_pos() -> x, y   (world coords, same space as KompassPos /
// the F7 dump). nil if the hero chain isn't resolved (menu / loading).
static int l_sacred_hero_pos(lua_State* L) {
    int32_t x = 0, y = 0;
    if (!sdk::player::world_pos(&x, &y)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    return 2;
}

// sacred.set_spawn(kx, ky) -> bool
// Teleport the active hero to KompassPos (kx, ky) — same space as the F7
// dump / questbook_set_kompass. Returns false if the hero chain isn't
// resolved yet (call from on_tick and stop once it returns true). Used to
// override where a class starts: F7-capture a spot, feed (kx,ky) here.
// Forward refs — defined with the FUN_0054d9d0 hook further down.
// lvl_set: if true the hook also rewrites the teleport LEVEL (arg2) to
// `lvl` — needed because NetScript's start is lvl=2 (an MP region) and
// the overworld spot lives at lvl=0.
struct TpOverride {
    volatile bool armed; volatile int kx, ky;
    volatile bool lvl_set; volatile int lvl;
    volatile unsigned long until_ms;   // rewrite EVERY hero tp until this
};
extern TpOverride    g_tp_ov;
extern volatile long g_tp_log;

static int l_sacred_set_spawn(lua_State* L) {
    int32_t kx = (int32_t)luaL_checkinteger(L, 1);
    int32_t ky = (int32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, sdk::player::set_world_pos(kx, ky));
    return 1;
}

// sacred.arm_spawn_teleport(kx, ky) -> true
// Arm the FUN_0054d9d0 hook: the NEXT teleport whose target creature is
// the active hero (= the engine's own campaign start placement) has its
// destination rewritten to (kx, ky), then disarms. The engine performs
// the move through its normal path (correct sector, no fade). Call this
// from on_world_load so it's armed before the start teleport fires. Pass
// the SAME coordinate space the engine uses for that call — watch the
// "[tp] ... <HERO> xy=(X,Y)" log line to confirm units.
// sacred.spawn_npc(type, kx, ky) -> handle | nil
// Runtime creature spawn via the engine's own create path. `type` =
// npc.lua creature id; (kx,ky) = KompassPos (same space as F7 / set_spawn).
// Call from on_tick/on_world_load once the world is loaded. Returns the
// new creature handle (truthy) or nil on failure.
static int l_sacred_spawn_npc(lua_State* L) {
    int  type = (int)luaL_checkinteger(L, 1);
    int32_t kx = (int32_t)luaL_checkinteger(L, 2);
    int32_t ky = (int32_t)luaL_checkinteger(L, 3);
    int h = sdk::player::spawn_npc(type, kx, ky);
    if (h == 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, h);
    return 1;
}

// sacred.spawn_here(type) -> handle | nil   spawn at the hero (no sector
// math; copies the hero's own valid position).
static int l_sacred_spawn_here(lua_State* L) {
    int type = (int)luaL_checkinteger(L, 1);
    int h = sdk::player::spawn_npc_here(type);
    if (h == 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, h);
    return 1;
}

// sacred.spawn_at(type, kx, ky) -> handle | nil   spawn at KompassPos
// using the hero's (valid) sector — reliable for spots near the player.
static int l_sacred_spawn_at(lua_State* L) {
    int  type = (int)luaL_checkinteger(L, 1);
    int32_t kx = (int32_t)luaL_checkinteger(L, 2);
    int32_t ky = (int32_t)luaL_checkinteger(L, 3);
    int h = sdk::player::spawn_npc_at(type, kx, ky);
    if (h == 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, h);
    return 1;
}

// sacred.npc_info(handle) -> { type=, kx=, ky=, faction= } | nil
static int l_sacred_npc_info(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    int type = 0; int32_t kx = 0, ky = 0; uint32_t fac = 0;
    if (!sdk::player::npc_info(h, &type, &kx, &ky, &fac)) {
        lua_pushnil(L); return 1;
    }
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, type);            lua_setfield(L, -2, "type");
    lua_pushinteger(L, kx);              lua_setfield(L, -2, "kx");
    lua_pushinteger(L, ky);              lua_setfield(L, -2, "ky");
    lua_pushinteger(L, (lua_Integer)fac);lua_setfield(L, -2, "faction");
    return 1;
}

// sacred.npc_set_faction(handle, value) -> bool   (writes cCreature+0x1F4)
static int l_sacred_npc_set_faction(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    uint32_t v = (uint32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, sdk::player::npc_set_faction(h, v));
    return 1;
}

// sacred.npc_wake(handle) -> bool   activate the creature's AI
static int l_sacred_npc_wake(lua_State* L) {
    lua_pushboolean(L, sdk::player::npc_wake((int)luaL_checkinteger(L, 1)));
    return 1;
}

// sacred.npc_set_stance(handle [,mode [,value]]) -> bool
// mode 0 (default) = class-default behavior (skeleton hostile, etc.).
static int l_sacred_npc_set_stance(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    int mode = (lua_gettop(L) >= 2) ? (int)luaL_checkinteger(L, 2) : 0;
    uint32_t v = (lua_gettop(L) >= 3) ? (uint32_t)luaL_checkinteger(L, 3) : 0;
    lua_pushboolean(L, sdk::player::npc_set_stance(h, mode, v));
    return 1;
}

// sacred.npc_set_invulnerable(handle [,on=true]) -> bool
static int l_sacred_npc_set_invulnerable(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    bool on = (lua_gettop(L) < 2) ? true : (lua_toboolean(L, 2) != 0);
    lua_pushboolean(L, sdk::player::npc_set_invulnerable(h, on));
    return 1;
}

// sacred.npc_set_stationary(handle [,on=true]) -> bool
static int l_sacred_npc_set_stationary(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    bool on = (lua_gettop(L) < 2) ? true : (lua_toboolean(L, 2) != 0);
    lua_pushboolean(L, sdk::player::npc_set_stationary(h, on));
    return 1;
}

// sacred.npc_set_level(handle, level) -> bool
static int l_sacred_npc_set_level(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    int lv = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, sdk::player::npc_set_level(h, lv));
    return 1;
}

// sacred.npc_set_name(handle, "Captain Miles") -> bool
// Writes the DlgNPC/NameArrA name entry keyed by this handle. Returns false
// (no-op) if the creature has no such entry yet (pure runtime spawns often
// don't — see docs). Harmless either way.
static int l_sacred_npc_set_name(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    const char* n = luaL_checkstring(L, 2);
    lua_pushboolean(L, sdk::player::set_npc_name(h, n));
    return 1;
}

// sacred.npc_quest_icon(handle [,on=true]) -> bool
// Floating "?!" quest-giver glyph above the NPC. Visual only.
static int l_sacred_npc_quest_icon(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    bool on = (lua_gettop(L) < 2) ? true : (lua_toboolean(L, 2) != 0);
    lua_pushboolean(L, sdk::player::npc_quest_icon(h, on));
    return 1;
}

// sacred.spawn_item(type, kx, ky) -> handle | nil
// Pickup-able ground item. Same create path/id-space as creatures.
static int l_sacred_spawn_item(lua_State* L) {
    int type = (int)luaL_checkinteger(L, 1);
    int32_t kx = (int32_t)luaL_checkinteger(L, 2);
    int32_t ky = (int32_t)luaL_checkinteger(L, 3);
    int h = sdk::player::spawn_item(type, kx, ky);
    if (h == 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, h);
    return 1;
}

// sacred.npc_teleport(handle, kx, ky) -> bool   engine teleport (any NPC)
static int l_sacred_npc_teleport(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    int32_t kx = (int32_t)luaL_checkinteger(L, 2);
    int32_t ky = (int32_t)luaL_checkinteger(L, 3);
    lua_pushboolean(L, sdk::player::npc_teleport(h, kx, ky));
    return 1;
}

// sacred.npc_equip(handle, item_type [,slot=0xC]) -> bool  (EXPERIMENTAL)
static int l_sacred_npc_equip(lua_State* L) {
    int h  = (int)luaL_checkinteger(L, 1);
    int it = (int)luaL_checkinteger(L, 2);
    // slot default 0x0D = MAIN weapon hand (agent-B/items_equip.md: main
    // 0x0D, off-hand 0x0C, mount 0x12, base cCreature+0x1A4 stride 4).
    int sl = (lua_gettop(L) >= 3) ? (int)luaL_checkinteger(L, 3) : 0x0D;
    lua_pushboolean(L, sdk::player::npc_equip(h, it, sl));
    return 1;
}

// sacred.createnpc_engine(payload_bytes, want_type) -> handle | nil
// payload = npc_templates.lua M.build(arch,opts) string. Drives the
// engine's OWN CreateNPC so init is type-correct (no struct poking).
static int l_sacred_createnpc_engine(lua_State* L) {
    size_t plen = 0;
    const char* p = luaL_checklstring(L, 1, &plen);
    int wt = (int)luaL_checkinteger(L, 2);
    int h = sdk::player::createnpc_engine((const uint8_t*)p, plen, wt);
    if (h <= 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, h);
    return 1;
}

// sacred.dialog_arm("DlgName", "TEXT_KEY" [,"voice"]) -> bool
// Arms a bound DlgNPC to speak (R-B: replays a 0x1f Dialog [+0x68 voice]
// record through the engine dispatcher FUN_00475680). "DlgName" MUST
// equal the dlgnpc_bind name; "TEXT_KEY" a registered global.res name
// (text.lua), passed BARE (no "res:"). Main tick, after bind + dict.
// sacred.dialog_arm(handle, "DlgName", "TEXT_KEY" [,"voice"]) -> bool
static int l_sacred_dialog_arm(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    const char* nm = luaL_checkstring(L, 2);
    const char* tk = luaL_checkstring(L, 3);
    const char* vo = lua_isstring(L, 4) ? lua_tostring(L, 4) : nullptr;
    lua_pushboolean(L, sdk::player::dialog_arm(h, nm, tk, vo));
    return 1;
}

// sacred.dialog_clear(handle) -> bool  (close dialog + clear "?!")
static int l_sacred_dialog_clear(lua_State* L) {
    lua_pushboolean(L, sdk::player::dialog_clear(
        (int)luaL_checkinteger(L, 1)));
    return 1;
}

// sacred.npc_roster_add(handle, quest_id) -> bool  (companion panel)
static int l_sacred_npc_roster_add(lua_State* L) {
    lua_pushboolean(L, sdk::player::npc_roster_add(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2)));
    return 1;
}
// sacred.npc_roster_remove(handle) -> bool
static int l_sacred_npc_roster_remove(lua_State* L) {
    lua_pushboolean(L, sdk::player::npc_roster_remove(
        (int)luaL_checkinteger(L, 1)));
    return 1;
}

// sacred.npc_make_companion(handle) -> bool   (party-follow + fights)
static int l_sacred_npc_make_companion(lua_State* L) {
    lua_pushboolean(L, sdk::player::npc_make_companion(
        (int)luaL_checkinteger(L, 1)));
    return 1;
}
// sacred.npc_dismiss(handle) -> bool
static int l_sacred_npc_dismiss(lua_State* L) {
    lua_pushboolean(L, sdk::player::npc_dismiss(
        (int)luaL_checkinteger(L, 1)));
    return 1;
}
// sacred.npc_despawn(handle) -> bool
static int l_sacred_npc_despawn(lua_State* L) {
    lua_pushboolean(L, sdk::player::npc_despawn(
        (int)luaL_checkinteger(L, 1)));
    return 1;
}
// sacred.npc_set_disposition(handle, matrix_class) -> bool
static int l_sacred_npc_set_disposition(lua_State* L) {
    lua_pushboolean(L, sdk::player::npc_set_disposition(
        (int)luaL_checkinteger(L, 1),
        (uint32_t)luaL_checkinteger(L, 2)));
    return 1;
}

// sacred.npc_bind_quest(handle, "Name" [,marker_on=true]) -> idx | nil
// THE storyline unlock: gives a runtime-spawned NPC a real DlgNPC entry
// so its custom name shows AND the overhead quest marker draws. Run once,
// right after spawn, from on_tick (main thread).
static int l_sacred_npc_bind_quest(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    const char* n = luaL_optstring(L, 2, "");
    int on = (lua_gettop(L) < 3) ? 1 : (lua_toboolean(L, 3) ? 1 : 0);
    int idx = sdk::player::dlgnpc_bind(h, n, on);
    if (idx < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, idx);
    return 1;
}

// sacred.npc_set_hp(handle, hp) -> bool   raise current+max HP
static int l_sacred_npc_set_hp(lua_State* L) {
    int h  = (int)luaL_checkinteger(L, 1);
    int hp = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, sdk::player::npc_set_hp(h, hp));
    return 1;
}

// sacred.npc_make_combatant(handle [,level=20 [,ai_class=3]]) -> bool
// Replays CreateNPC's combat init. ai_class 3 (or 7) = ally defender
// (fights monsters, NEVER the hero), 2 = a monster class, 13 = immune
// non-combatant. (Matrix polarity corrected: 2 wrongly attacked the hero.)
static int l_sacred_npc_make_combatant(lua_State* L) {
    int h  = (int)luaL_checkinteger(L, 1);
    int lv = (lua_gettop(L) >= 2) ? (int)luaL_checkinteger(L, 2) : 20;
    int ac = (lua_gettop(L) >= 3) ? (int)luaL_checkinteger(L, 3) : 7;
    lua_pushboolean(L, sdk::player::npc_make_combatant(h, lv, ac));
    return 1;
}

// sacred.dump_vanilla_of(type, ai_class [,skip_lo [,skip_hi]]) -> nil
static int l_sacred_dump_vanilla_of(lua_State* L) {
    int t  = (int)luaL_checkinteger(L, 1);
    int ac = (int)luaL_checkinteger(L, 2);
    int lo = (lua_gettop(L) >= 3) ? (int)luaL_checkinteger(L, 3) : -1;
    int hi = (lua_gettop(L) >= 4) ? (int)luaL_checkinteger(L, 4) : -1;
    sdk::player::dump_vanilla_of(t, ac, lo, hi, "VANILLA");
    return 0;
}

// sacred.hero_weapon_dump() -> nil   logs hero equip slots + item types
static int l_sacred_hero_weapon_dump(lua_State* L) {
    (void)L; sdk::player::hero_weapon_dump(); return 0;
}

// sacred.scan_creatures([filter_type=-1 [,cap=800]]) -> nil
static int l_sacred_scan_creatures(lua_State* L) {
    int ft  = (lua_gettop(L) >= 1) ? (int)luaL_checkinteger(L, 1) : -1;
    int cap = (lua_gettop(L) >= 2) ? (int)luaL_checkinteger(L, 2) : 800;
    sdk::player::scan_creatures(ft, cap);
    return 0;
}

// sacred.npc_dump(handle [,tag]) -> nil   logs render-suspect fields
static int l_sacred_npc_dump(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    const char* tag = luaL_optstring(L, 2, "");
    sdk::player::npc_field_dump(h, tag);
    return 0;
}

// sacred.npc_peek(handle) -> v14, v245, vC, v204   (all integers, 0 on
// fail). PURE read-only SEH-guarded creature-field reads — no patch, no
// hook, no engine call: physically cannot crash the game. Diagnostic to
// observe what state changes on the conversation window open/close for a
// FIX-A2 NPC (FUN_00461540 ruled out by live probe; +0x14&0x80000 /
// +0x245 / +0xc / +0x204 are the candidate per-conversation fields).
// ======================================================================
// HW data breakpoint (DR0/DR7 + VEH) — the DETERMINISTIC instrument to
// pin the real conversation renderer/answer code. Candidate-fn probing
// is exhausted (every disasm guess refuted live). Here we watch the
// EXACT address of the captain's content handle (cCreature+0xc, what
// FIX-A2 writes and the GUI must READ to render the box). When the
// engine reads it, a single-step exception fires and the faulting EIP
// IS the renderer/answer instruction — zero guessing. READ-ONLY: the
// VEH only logs distinct EIPs (throttled), clears DR6, sets the resume
// flag, and continues. No writes, no engine calls.
//   DR7 = L0(bit0) | RW0=11b read/write (bits16-17) | LEN0=00b 1-byte
//         (bits18-19, byte len => no alignment constraint) = 0x00030001
// VEH runs before any SEH frame, so the game's own __except cannot
// swallow it. Single instance; armed on the CALLER's thread (must be
// the game main thread => call from sacred.on_tick).
static volatile LONG g_hwbp_veh_installed = 0;
static uintptr_t     g_hwbp_addr          = 0;
static uint32_t      g_hwbp_eips[24]      = {0};
static int           g_hwbp_neip          = 0;
static int           g_hwbp_logged        = 0;

static LONG CALLBACK hwbp_veh(PEXCEPTION_POINTERS ep) {
    __try {
        if (ep->ExceptionRecord->ExceptionCode != (DWORD)0x80000004) // SS
            return EXCEPTION_CONTINUE_SEARCH;
        CONTEXT* c = ep->ContextRecord;
        if (!(c->Dr6 & 0x1)) return EXCEPTION_CONTINUE_SEARCH; // not DR0
        uint32_t eip = (uint32_t)c->Eip;
        // Time-windowed distinct set: the scene-load burst exhausts the
        // 24-EIP buffer before the player can walk over and talk. Clear
        // it every ~12 s so the TALK-time readers re-log fresh (with a
        // timestamp clearly in the talk window). Load noise logs once
        // early; the renderer/answer EIP appears after a [hwbp] WINDOW.
        static DWORD s_last_reset = 0;
        DWORD now = GetTickCount();
        if (s_last_reset == 0) s_last_reset = now;
        if (now - s_last_reset > 12000) {
            s_last_reset = now;
            g_hwbp_neip = 0; g_hwbp_logged = 0;
            sdk_log("[hwbp] ---- WINDOW reset (talk now; new EIPs "
                    "below are fresh) ----");
        }
        bool seen = false;
        for (int i = 0; i < g_hwbp_neip; i++)
            if (g_hwbp_eips[i] == eip) { seen = true; break; }
        if (!seen && g_hwbp_neip < 24) {
            g_hwbp_eips[g_hwbp_neip++] = eip;
            sdk_log("[hwbp] NEW reader EIP=0x%08X (addr=0x%08X) "
                    "eax=%08X ebx=%08X esi=%08X edi=%08X", eip,
                    (uint32_t)g_hwbp_addr, (uint32_t)c->Eax,
                    (uint32_t)c->Ebx, (uint32_t)c->Esi,
                    (uint32_t)c->Edi);
        } else if (g_hwbp_logged < 80) {
            g_hwbp_logged++;
            sdk_log("[hwbp] hit EIP=0x%08X", eip);
        }
        c->Dr6 = 0;
        c->EFlags |= 0x10000;            // RF: don't re-trap same insn
        return EXCEPTION_CONTINUE_EXECUTION;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

// sacred.arm_hwbp(handle [,offset=0xc]) -> bool. MUST be called from a
// main-thread context (sacred.on_tick) — debug regs are per-thread.
static int l_sacred_arm_hwbp(lua_State* L) {
    int h   = (int)luaL_checkinteger(L, 1);
    // arg2: numeric => watch cCreature+off (noisy, per-tick). Omitted
    // or nil => DEFAULT = the DlgNPC entry+0x4c content slot (the GUI
    // conversation-content cell — read ONLY while the box is drawn /
    // answered; quiet & dialog-specific, the right SNR target). cre+0xc
    // was a per-tick-read flood (proven 2026-05-18).
    bool dlg_mode = !(lua_gettop(L) >= 2 && !lua_isnil(L, 2));
    int off = dlg_mode ? 0 : (int)luaL_checkinteger(L, 2);
    uintptr_t cre = sdk::player::npc_creature(h);
    if (!cre) { lua_pushboolean(L, 0); return 1; }
    uintptr_t addr;
    if (dlg_mode) {
        // entry = *(qm+0x755c) + objIdx*0x50 ; slot = entry + 0x4c
        __try {
            uintptr_t qm  = 0x00AACF80;
            uintptr_t db  = *(uintptr_t*)(qm + 0x755c);
            uint8_t   oix = *(uint8_t*)(cre + 0x245);
            if (!db) { lua_pushboolean(L, 0); return 1; }
            addr = db + (uintptr_t)oix * 0x50 + 0x4c;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            lua_pushboolean(L, 0); return 1;
        }
    } else {
        addr = cre + (uintptr_t)off;
    }
    bool ok = false;
    __try {
        if (InterlockedCompareExchange(&g_hwbp_veh_installed, 1, 0) == 0) {
            AddVectoredExceptionHandler(1, hwbp_veh);
        }
        g_hwbp_addr = addr;
        CONTEXT ctx; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        HANDLE th = GetCurrentThread();
        ctx.Dr0 = (DWORD)addr;
        ctx.Dr7 = 0x00030001u;            // L0 + RW0=11 + LEN0=00
        ctx.Dr1 = ctx.Dr2 = ctx.Dr3 = 0;
        ctx.Dr6 = 0;
        ok = SetThreadContext(th, &ctx) != 0;
        sdk_log("[hwbp] armed addr=0x%08X (h=%d cre=0x%08X +0x%X) "
                "set=%d", (uint32_t)addr, h, (uint32_t)cre, off, ok?1:0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[hwbp] arm faulted"); ok = false;
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int l_sacred_npc_peek(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    uint32_t v14 = 0, v245 = 0, vC = 0, v204 = 0;
    // Dialog state-machine fields (talk_trigger.md): the per-tick handler
    // FUN_0052AB70 dispatches on [cre+0x152] (sub-state 0..6); case 6 =
    // active conversation. +0x150 = major (==6 when a conv is open),
    // +0x15c = dialog id used to build "Talk_<name>_Dlg_<id>". The old
    // dlgpoll watched +0x14/+0x245/+0xc/+0x204 — the WRONG fields — which
    // is why talk looked like "no change". These three are the real ones.
    uint32_t v150 = 0, v152 = 0, v15c = 0, v200 = 0;
    // +0x200: the conversation-participant flag word. BOTH live dialog
    // functions gate the active partner on (v200 & 0x04007000): FUN_0056B130
    // (the per-tick dialog tick) and FUN_00461540 (the conv pump, which then
    // clears those bits). Candidate GENERAL "this NPC is in a conversation"
    // signal for any bound NPC — the SDK-grade talk detector (talk_trigger.md
    // Path B). Returned as the 8th value.
    uintptr_t c = sdk::player::npc_creature(h);
    if (c) {
        __try {
            v14  = *(uint32_t*)(c + 0x14);
            v245 = *(uint8_t*) (c + 0x245);
            vC   = *(uint32_t*)(c + 0x0c);
            v204 = *(uint32_t*)(c + 0x204);
            v150 = *(uint16_t*)(c + 0x150);
            v152 = *(uint16_t*)(c + 0x152);
            v15c = *(uint32_t*)(c + 0x15c);
            v200 = *(uint32_t*)(c + 0x200);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            v14 = v245 = vC = v204 = v150 = v152 = v15c = v200 = 0;
        }
    }
    lua_pushinteger(L, (lua_Integer)v14);
    lua_pushinteger(L, (lua_Integer)v245);
    lua_pushinteger(L, (lua_Integer)vC);
    lua_pushinteger(L, (lua_Integer)v204);
    lua_pushinteger(L, (lua_Integer)v150);
    lua_pushinteger(L, (lua_Integer)v152);
    lua_pushinteger(L, (lua_Integer)v15c);
    lua_pushinteger(L, (lua_Integer)v200);
    return 8;
}

// sacred.npc_in_dialog(handle) -> bool. THE general talk signal: cCreature
// +0x200 bit 0x400 pulses while the player interacts/talks with this NPC
// (validated live 4/4, zero false positives idle/walk/fight; talk_trigger.md).
// Works for ANY bound NPC — no HW-BP, no hooks, no script tables. Pure
// read-only SEH-guarded; the npcobj o:on_talk(fn) wrapper does rising-edge
// detection on top of this.
static int l_sacred_npc_in_dialog(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    int in_dlg = 0;
    uintptr_t c = sdk::player::npc_creature(h);
    if (c) {
        __try {
            in_dlg = ((*(uint32_t*)(c + 0x200)) & 0x400u) ? 1 : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            in_dlg = 0;
        }
    }
    lua_pushboolean(L, in_dlg);
    return 1;
}

// sacred.trigger_table_dump([want_name]) -> count
// PURE read-only diagnostic for Path A (talk_trigger.md). Walks the engine's
// trigger-NAME table at 0xAAB708 (begin) .. 0xAAB70C (end), stride 0x54, and
// logs the entry count + the first 16 entries (name string + raw 0x54-byte
// hex). DECIDES whether Path A is viable: if count>0 the table is live and we
// can replicate the registration; if 0 the table is dormant in this build
// (vanilla campaign uses FunkCode records, not text scripts) and we fall back
// to Path B. If want_name is given, also reports whether that exact name is
// already present. No writes, SEH-guarded — cannot crash.
static int l_sacred_trigger_table_dump(lua_State* L) {
    const char* want = (lua_gettop(L) >= 1 && lua_isstring(L, 1))
                       ? lua_tostring(L, 1) : NULL;
    HMODULE exe = g_attach.exe_module;
    if (!exe || (uintptr_t)exe != 0x00400000) {
        sdk_log("[trigtab] exe base not 0x400000 — skip");
        lua_pushinteger(L, -1); return 1;
    }
    int count = -1;
    int found = 0;
    __try {
        uint32_t begin = *(uint32_t*)0x00AAB708u;
        uint32_t end   = *(uint32_t*)0x00AAB70Cu;
        uint32_t cap   = *(uint32_t*)0x00AAB710u;
        if (begin == 0 || end < begin) {
            sdk_log("[trigtab] table EMPTY (begin=%08X end=%08X) — "
                    "Path A dormant in this build", begin, end);
            count = 0;
        } else {
            count = (int)((end - begin) / 0x54u);
            sdk_log("[trigtab] begin=%08X end=%08X cap=%08X count=%d "
                    "(stride 0x54; entry = name[0x40] + 5 dwords)",
                    begin, end, cap, count);
            // Sample the first 3 entries' full hex (layout reference).
            for (int i = 1; i <= 3 && i < count; i++) {
                unsigned char* b = (unsigned char*)((uintptr_t)begin + (uintptr_t)i * 0x54u);
                char nm[72]; int j; for (j = 0; j < 70 && b[j]; j++) nm[j] = (char)b[j]; nm[j] = 0;
                uint32_t* w = (uint32_t*)(b + 0x40);
                sdk_log("[trigtab] #%d '%s'  +40=%08x +44=%08x +48=%08x +4c=%08x +50=%08x",
                        i, nm, w[0], w[1], w[2], w[3], w[4]);
            }
            // Scan ALL entries for the `want` substring (e.g. "Talk_",
            // "SelfTriggerQuest", or our exact key). Log up to 20 matches
            // with their 5 payload dwords — the template we must replicate.
            if (want) {
                int matches = 0, exact = 0;
                for (int i = 0; i < count; i++) {
                    unsigned char* b = (unsigned char*)((uintptr_t)begin + (uintptr_t)i * 0x54u);
                    // bounded substring search in the inline name (max 0x40)
                    const char* nm = (const char*)b;
                    int hit = 0, wl = (int)strlen(want);
                    for (int s = 0; s <= 0x40 - wl; s++) {
                        if (nm[s] == 0) break;
                        int k = 0; for (; k < wl && nm[s + k] == want[k]; k++) {}
                        if (k == wl) { hit = 1; break; }
                    }
                    if (!hit) continue;
                    if (strcmp(nm, want) == 0) exact = 1;
                    if (matches < 20) {
                        uint32_t* w = (uint32_t*)(b + 0x40);
                        sdk_log("[trigtab] MATCH #%d '%s'  +40=%08x +44=%08x "
                                "+48=%08x +4c=%08x +50=%08x", i, nm,
                                w[0], w[1], w[2], w[3], w[4]);
                    }
                    matches++;
                }
                found = exact;
                sdk_log("[trigtab] want-substr '%s': %d matches, exact=%d",
                        want, matches, exact);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[trigtab] faulted reading table");
        count = -1;
    }
    lua_pushinteger(L, count);
    return 1;
}

static int l_sacred_arm_spawn_teleport(lua_State* L) {
    g_tp_ov.kx    = (int)luaL_checkinteger(L, 1);
    g_tp_ov.ky    = (int)luaL_checkinteger(L, 2);
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        g_tp_ov.lvl     = (int)luaL_checkinteger(L, 3);
        g_tp_ov.lvl_set = true;
    } else {
        g_tp_ov.lvl_set = false;
    }
    // 4th arg = rewrite-window ms. Omitted/0 → ONE-SHOT (proven campaign
    // behaviour). >0 → rewrite every hero tp for that many ms (NetScript
    // multi-step spawn). until_ms==0 is the one-shot sentinel.
    if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) {
        unsigned long win = (unsigned long)luaL_checkinteger(L, 4);
        g_tp_ov.until_ms = (win == 0) ? 0 : (GetTickCount() + win);
    } else {
        g_tp_ov.until_ms = 0;            // one-shot
    }
    g_tp_ov.armed = true;
    g_tp_log      = 0;   // re-enable the arg log for this load
    lua_pushboolean(L, 1);
    return 1;
}

// sacred.questbook_set_marker(quest_id, worldX, worldY) -> bool
// Secondary (side-quest) style minimap + world-map marker.
static int l_sacred_questbook_set_marker(lua_State* L) {
    lua_Integer qid = luaL_checkinteger(L, 1);
    int32_t wx = (int32_t)luaL_checkinteger(L, 2);
    int32_t wy = (int32_t)luaL_checkinteger(L, 3);
    int r = questbook_set_marker_impl((uint32_t)qid, wx, wy);
    lua_pushboolean(L, r == 0 ? 1 : 0);
    return 1;
}

// sacred.questbook_set_step_done(quest_id, done_bool) -> bool
static int l_sacred_questbook_set_step_done(lua_State* L) {
    lua_Integer qid = luaL_checkinteger(L, 1);
    bool done = lua_isnone(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    int r = questbook_set_step_done_impl((uint32_t)qid, done);
    lua_pushboolean(L, r == 0 ? 1 : 0);
    return 1;
}

// sacred.questbook_mark_solved(quest_id) -> bool. Vanilla "solved"
// state (+0x04=100, kompass cleared). Call together with add_log of
// the resolution line for the full vanilla completed look.
static int l_sacred_questbook_mark_solved(lua_State* L) {
    lua_Integer qid = luaL_checkinteger(L, 1);
    lua_pushboolean(L, questbook_mark_solved_impl((uint32_t)qid) == 0 ? 1 : 0);
    return 1;
}

// sacred.questbook_add_log(quest_id, name) -> slot idx (0..10) or nil.
// Appends ONE journal line (next free log slot). For multi-step quest
// progression — call again as the quest advances.
static int l_sacred_questbook_add_log(lua_State* L) {
    lua_Integer qid = luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    int r = questbook_add_log_impl((uint32_t)qid, name);
    if (r < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, r);
    return 1;
}

// sacred.questbook_complete(quest_id) -> bool   (removes from journal)
static int l_sacred_questbook_complete(lua_State* L) {
    lua_Integer qid = luaL_checkinteger(L, 1);
    int r = questbook_complete_remove_impl((uint32_t)qid);
    lua_pushboolean(L, r == 0 ? 1 : 0);
    return 1;
}

static int l_sacred_questbook_set_log(lua_State* L) {
    lua_Integer qid  = luaL_checkinteger(L, 1);
    int         page = (int)luaL_checkinteger(L, 2);
    int top = lua_gettop(L);
    int n   = top - 2;
    if (n <= 0)  return luaL_error(L, "questbook_set_log: need >=1 name");
    if (n > 11)  n = 11;
    const char* names[11] = {0};
    for (int i = 0; i < n; i++) names[i] = luaL_checkstring(L, 3 + i);
    int r = questbook_set_log_impl((uint32_t)qid, page, names, n);
    lua_pushboolean(L, r == 0 ? 1 : 0);
    lua_pushinteger(L, r);     // 0 ok / -1 no entry / -2 unresolved name
    return 2;
}

// Forward declarations + struct + ALL named-state offset constants.
// Implementations live further down in the file alongside the cQuestManager
// walker hook (where it's natural to read them next to the recon comments),
// but the ABI-shape lives here so the Lua-binding code below is reviewable
// without scrolling. The implementation block does NOT redeclare any of
// these — they're visible through the rest of the translation unit.
struct StateRow {
    char      name[64];
    int32_t   v[4];
    uintptr_t addr;
};
constexpr uintptr_t MGR_OFF_STATE_BEGIN = 0x334;
constexpr uintptr_t MGR_OFF_STATE_END   = 0x338;
constexpr uintptr_t STATE_ENTRY_STRIDE  = 0x64;
constexpr uintptr_t STATE_OFF_STATUS    = 0x00;
constexpr uintptr_t STATE_OFF_NAME      = 0x04;
constexpr uintptr_t STATE_OFF_VALUES    = 0x44;
constexpr int       STATE_VALUE_COUNT   = 4;
static int       state_walk(StateRow* out, int max);
static uintptr_t state_find(const char* name);
static bool      state_write_in_place(const char* name,
                                      int n_values,
                                      const int32_t* values);
// (g_quest_mgr forward-declared higher up so the questbook block can use it.)

static int l_sacred_state_dump(lua_State* L) {
    if (!g_quest_mgr) {
        sdk_log("[runtime_triggers] state_dump: cQuestManager not yet captured "
                "(walker hook hasn't fired — load a save first)");
        lua_pushinteger(L, -1);
        return 1;
    }
    StateRow rows[STATE_DUMP_MAX];
    int n = state_walk(rows, STATE_DUMP_MAX);
    if (n < 0) {
        sdk_log("[runtime_triggers] state_dump: walk failed");
        lua_pushinteger(L, -1);
        return 1;
    }
    sdk_log("[runtime_triggers] === state_dump: %d active entries ===", n);
    for (int i = 0; i < n; i++) {
        sdk_log("[runtime_triggers]   %3d. %-40s = [%d, %d, %d, %d] @ %p",
                i + 1, rows[i].name,
                rows[i].v[0], rows[i].v[1], rows[i].v[2], rows[i].v[3],
                (void*)rows[i].addr);
    }
    sdk_log("[runtime_triggers] === end state_dump ===");
    lua_pushinteger(L, n);
    return 1;
}

// Build a Lua array {v0, v1, v2, v3} from a state entry. Returns 1 (table)
// pushed if entry exists, 0 otherwise.
static int push_state_values(lua_State* L, uintptr_t entry_addr) {
    if (!entry_addr) { lua_pushnil(L); return 1; }
    int32_t v[STATE_VALUE_COUNT];
    __try {
        for (int i = 0; i < STATE_VALUE_COUNT; i++) {
            v[i] = *(int32_t*)(entry_addr + STATE_OFF_VALUES + i * 4);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_createtable(L, STATE_VALUE_COUNT, 0);
    for (int i = 0; i < STATE_VALUE_COUNT; i++) {
        lua_pushinteger(L, v[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_sacred_state_get(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    uintptr_t e = state_find(name);
    return push_state_values(L, e);
}

// Read 1..4 int values off the Lua stack starting at `base_idx` (1-based).
// Returns count gathered. Caller has already validated arg count.
static int collect_values(lua_State* L, int base_idx, int32_t* out, int max) {
    int top = lua_gettop(L);
    int got = 0;
    for (int i = base_idx; i <= top && got < max; i++) {
        out[got++] = (int32_t)luaL_checkinteger(L, i);
    }
    return got;
}

static int l_sacred_state_set(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    int32_t vals[STATE_VALUE_COUNT];
    int n = collect_values(L, 2, vals, STATE_VALUE_COUNT);
    if (n == 0) return luaL_error(L, "sacred.state_set('%s'): need at least one value", name);
    bool ok = state_write_in_place(name, n, vals);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ctx:get_var(name) — convenience: returns just the FIRST value as integer.
// Use sacred.state_get(name) if you need the full 4-int array.
static int l_ctx_get_var(lua_State* L) {
    const char* name = luaL_checkstring(L, 2);   // ctx is arg 1
    uintptr_t e = state_find(name);
    if (!e) { lua_pushnil(L); return 1; }
    int32_t v;
    __try { v = *(int32_t*)(e + STATE_OFF_VALUES); }
    __except (EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, v);
    return 1;
}

static int l_ctx_set_var(lua_State* L) {
    const char* name = luaL_checkstring(L, 2);   // ctx is arg 1
    int32_t vals[STATE_VALUE_COUNT];
    int n = collect_values(L, 3, vals, STATE_VALUE_COUNT);
    if (n == 0) return luaL_error(L, "ctx:set_var('%s'): need at least one value", name);
    bool ok = state_write_in_place(name, n, vals);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// Build the ctx table once at state setup; fire() reuses it across calls.
// Each fire() mutates `ctx.trigger_name` to the current name BEFORE the
// pcall. The methods are plain table fields (no __index needed) — Lua's
// `ctx:method()` syntax resolves them directly.
static void build_ctx_table(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_ctx_gold);             lua_setfield(L, -2, "gold");
    lua_pushcfunction(L, l_ctx_give_gold);        lua_setfield(L, -2, "give_gold");
    lua_pushcfunction(L, l_ctx_give_gold_silent); lua_setfield(L, -2, "give_gold_silent");
    lua_pushcfunction(L, l_ctx_charge_gold);      lua_setfield(L, -2, "charge_gold");
    lua_pushcfunction(L, l_ctx_has_item);    lua_setfield(L, -2, "has_item");
    lua_pushcfunction(L, l_ctx_set_qbit);    lua_setfield(L, -2, "set_qbit");
    lua_pushcfunction(L, l_ctx_get_qbit);    lua_setfield(L, -2, "get_qbit");
    lua_pushcfunction(L, l_ctx_notify);      lua_setfield(L, -2, "notify");
    lua_pushcfunction(L, l_ctx_get_var);     lua_setfield(L, -2, "get_var");
    lua_pushcfunction(L, l_ctx_set_var);     lua_setfield(L, -2, "set_var");
    // trigger_name placeholder — overwritten before each pcall.
    lua_pushstring(L, "");                   lua_setfield(L, -2, "trigger_name");
    lua_setfield(L, LUA_REGISTRYINDEX, SDK_CTX_KEY);
}

// The self-contained sacred.* data / engine-introspection bindings (read_save,
// creature_name, combat_art, companions, hash, xp_for_level, skill_name,
// class_skills, survival_bonus, bonus_name, resolve_engine, globalres) live in
// lua_api_data.cpp (refactor A5). install_data_api registers them.
void install_data_api(lua_State* L);

void install_lua_api(lua_State* L);
void install_lua_api(lua_State* L) {
    lua_getglobal(L, "sacred");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        sdk_log("[runtime_triggers] install_lua_api: no `sacred` table");
        return;
    }
    lua_pushcfunction(L, l_on_trigger);       lua_setfield(L, -2, "on_trigger");
    lua_pushcfunction(L, l_on_tick);          lua_setfield(L, -2, "on_tick");
    lua_pushcfunction(L, l_clear_triggers);   lua_setfield(L, -2, "clear_triggers");
    lua_pushcfunction(L, l_sacred_notify);    lua_setfield(L, -2, "notify");
    lua_pushcfunction(L, l_on_world_load);    lua_setfield(L, -2, "on_world_load");
    lua_pushcfunction(L, l_sacred_state_dump); lua_setfield(L, -2, "state_dump");
    lua_pushcfunction(L, l_sacred_state_get);  lua_setfield(L, -2, "state_get");
    lua_pushcfunction(L, l_sacred_state_set);  lua_setfield(L, -2, "state_set");
    lua_pushcfunction(L, l_sacred_questbook_count);    lua_setfield(L, -2, "questbook_count");
    lua_pushcfunction(L, l_sacred_questbook_dump);     lua_setfield(L, -2, "questbook_dump");
    lua_pushcfunction(L, l_sacred_questbook_register); lua_setfield(L, -2, "questbook_register");
    lua_pushcfunction(L, l_sacred_hide_other_quests);  lua_setfield(L, -2, "hide_other_quests");
    lua_pushcfunction(L, l_sacred_hide_vanilla_quests);lua_setfield(L, -2, "hide_vanilla_quests");
    lua_pushcfunction(L, l_sacred_questbook_get_id);   lua_setfield(L, -2, "questbook_get_id");
    lua_pushcfunction(L, l_sacred_questbook_set_log);  lua_setfield(L, -2, "questbook_set_log");
    lua_pushcfunction(L, l_sacred_questbook_set_kompass); lua_setfield(L, -2, "questbook_set_kompass");
    lua_pushcfunction(L, l_sacred_questbook_set_marker);    lua_setfield(L, -2, "questbook_set_marker");
    lua_pushcfunction(L, l_sacred_questbook_set_step_done); lua_setfield(L, -2, "questbook_set_step_done");
    lua_pushcfunction(L, l_sacred_questbook_add_log);       lua_setfield(L, -2, "questbook_add_log");
    lua_pushcfunction(L, l_sacred_questbook_mark_solved);   lua_setfield(L, -2, "questbook_mark_solved");
    lua_pushcfunction(L, l_sacred_questbook_complete);      lua_setfield(L, -2, "questbook_complete");
    lua_pushcfunction(L, l_sacred_hero_pos);                lua_setfield(L, -2, "hero_pos");
    lua_pushcfunction(L, l_sacred_set_spawn);               lua_setfield(L, -2, "set_spawn");
    lua_pushcfunction(L, l_sacred_arm_spawn_teleport);      lua_setfield(L, -2, "arm_spawn_teleport");
    lua_pushcfunction(L, l_sacred_spawn_npc);               lua_setfield(L, -2, "spawn_npc");
    lua_pushcfunction(L, l_sacred_spawn_here);              lua_setfield(L, -2, "spawn_here");
    lua_pushcfunction(L, l_sacred_spawn_at);                lua_setfield(L, -2, "spawn_at");
    lua_pushcfunction(L, l_sacred_npc_info);                lua_setfield(L, -2, "npc_info");
    lua_pushcfunction(L, l_sacred_npc_set_faction);         lua_setfield(L, -2, "npc_set_faction");
    lua_pushcfunction(L, l_sacred_npc_wake);                lua_setfield(L, -2, "npc_wake");
    lua_pushcfunction(L, l_sacred_npc_set_stance);          lua_setfield(L, -2, "npc_set_stance");
    lua_pushcfunction(L, l_sacred_npc_set_invulnerable);    lua_setfield(L, -2, "npc_set_invulnerable");
    lua_pushcfunction(L, l_sacred_npc_set_stationary);      lua_setfield(L, -2, "npc_set_stationary");
    lua_pushcfunction(L, l_sacred_npc_set_level);           lua_setfield(L, -2, "npc_set_level");
    lua_pushcfunction(L, l_sacred_npc_set_name);            lua_setfield(L, -2, "npc_set_name");
    lua_pushcfunction(L, l_sacred_npc_quest_icon);          lua_setfield(L, -2, "npc_quest_icon");
    lua_pushcfunction(L, l_sacred_spawn_item);              lua_setfield(L, -2, "spawn_item");
    lua_pushcfunction(L, l_sacred_npc_teleport);            lua_setfield(L, -2, "npc_teleport");
    lua_pushcfunction(L, l_sacred_npc_equip);               lua_setfield(L, -2, "npc_equip");
    lua_pushcfunction(L, l_sacred_createnpc_engine);        lua_setfield(L, -2, "createnpc_engine");
    lua_pushcfunction(L, l_sacred_npc_bind_quest);          lua_setfield(L, -2, "npc_bind_quest");
    lua_pushcfunction(L, l_sacred_dialog_arm);              lua_setfield(L, -2, "dialog_arm");
    lua_pushcfunction(L, l_sacred_dialog_clear);            lua_setfield(L, -2, "dialog_clear");
    lua_pushcfunction(L, l_sacred_npc_roster_add);          lua_setfield(L, -2, "npc_roster_add");
    lua_pushcfunction(L, l_sacred_npc_roster_remove);       lua_setfield(L, -2, "npc_roster_remove");
    lua_pushcfunction(L, l_sacred_npc_make_companion);      lua_setfield(L, -2, "npc_make_companion");
    lua_pushcfunction(L, l_sacred_npc_dismiss);             lua_setfield(L, -2, "npc_dismiss");
    lua_pushcfunction(L, l_sacred_npc_despawn);             lua_setfield(L, -2, "npc_despawn");
    lua_pushcfunction(L, l_sacred_npc_set_disposition);     lua_setfield(L, -2, "npc_set_disposition");
    lua_pushcfunction(L, l_sacred_npc_dump);                lua_setfield(L, -2, "npc_dump");
    lua_pushcfunction(L, l_sacred_npc_peek);                lua_setfield(L, -2, "npc_peek");
    lua_pushcfunction(L, l_sacred_trigger_table_dump);      lua_setfield(L, -2, "trigger_table_dump");
    lua_pushcfunction(L, l_sacred_npc_in_dialog);           lua_setfield(L, -2, "npc_in_dialog");
    install_data_api(L);   // data/engine bindings live in lua_api_data.cpp (A5)
    lua_pushcfunction(L, l_sacred_arm_hwbp);                lua_setfield(L, -2, "arm_hwbp");
    lua_pushcfunction(L, l_sacred_npc_set_hp);              lua_setfield(L, -2, "npc_set_hp");
    lua_pushcfunction(L, l_sacred_scan_creatures);          lua_setfield(L, -2, "scan_creatures");
    lua_pushcfunction(L, l_sacred_hero_weapon_dump);        lua_setfield(L, -2, "hero_weapon_dump");
    lua_pushcfunction(L, l_sacred_dump_vanilla_of);         lua_setfield(L, -2, "dump_vanilla_of");
    lua_pushcfunction(L, l_sacred_npc_make_combatant);      lua_setfield(L, -2, "npc_make_combatant");
    lua_pushcfunction(L, l_sacred_give_gold);               lua_setfield(L, -2, "give_gold");
    lua_pop(L, 1);

    // One-time ctx table setup. Lives in the registry; fire() mutates its
    // `trigger_name` field and passes the table to handlers as arg 1.
    build_ctx_table(L);
}

// =========================================================================
// Fire path
// =========================================================================

// Throttled "tick": fire() is invoked for every sacred_hash query (many
// per frame in-world), so it's a reliable heartbeat. We pcall registered
// on_tick handlers at most every TICK_MIN_MS — enough for position polling
// (e.g. "did the hero reach the quest marker?") without per-query Lua cost.
constexpr DWORD TICK_MIN_MS = 250;
static DWORD g_last_tick_ms = 0;

static void fire_tick() {
    if (!g_ready || !g_L) return;
    DWORD now = GetTickCount();
    if ((now - g_last_tick_ms) < TICK_MIN_MS) return;
    g_last_tick_ms = now;
    lua_State* L = g_L;
    int top0 = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, SDK_TICK_KEY);
    if (!lua_istable(L, -1)) { lua_settop(L, top0); return; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); continue; }
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* m = lua_tostring(L, -1);
            sdk_log("[runtime_triggers] on_tick handler #%d error: %s",
                    i, m ? m : "?");
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top0);
}

void fire(const char* trigger_name) {
    if (!g_ready || !g_L || !trigger_name) return;
    fire_tick();
    lua_State* L = g_L;
    int top0 = lua_gettop(L);

    lua_getfield(L, LUA_REGISTRYINDEX, SDK_HANDLERS_KEY);
    if (!lua_istable(L, -1)) { lua_settop(L, top0); return; }

    lua_getfield(L, -1, trigger_name);
    if (!lua_istable(L, -1)) { lua_settop(L, top0); return; }

    // Pull the shared ctx table and update its trigger_name field. We
    // pass this same table to every handler, so a multi-handler chain
    // sees the same ctx (and stays cheap — no per-call allocation).
    lua_getfield(L, LUA_REGISTRYINDEX, SDK_CTX_KEY);
    if (lua_istable(L, -1)) {
        lua_pushstring(L, trigger_name);
        lua_setfield(L, -2, "trigger_name");
    }
    // Stack: [-3]=handlers_root, [-2]=handler_list, [-1]=ctx
    int ctx_idx = lua_gettop(L);

    int n = (int)lua_rawlen(L, -2);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -2, i);            // push handler[i]
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); continue; }
        lua_pushvalue(L, ctx_idx);         // push ctx as arg
        int r = lua_pcall(L, 1, 0, 0);
        if (r != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            sdk_log("[runtime_triggers] handler '%s' #%d error: %s",
                    trigger_name, i, msg ? msg : "?");
            InterlockedIncrement(&g_handler_errs);
            lua_pop(L, 1);
        } else {
            InterlockedIncrement(&g_fires);
        }
    }
    lua_settop(L, top0);
}

// =========================================================================
// Trampoline install (FUN_004915a0 + FUN_00491170)
// =========================================================================
//
// Both functions share a 7-byte MSVC SEH prologue:
//     6A FF                push -1
//     68 XX XX XX XX       push <seh_record>
//     ... (mov eax, fs:[0]; push eax; ...)
// First two pushes are 7 bytes and position-independent, so we save them
// verbatim into the trampoline and overwrite with `JMP rel32` + 2 NOPs.

constexpr uintptr_t SELF_TRIGGER_QUEST_RVA = 0x004915a0 - 0x00400000;
constexpr uintptr_t DIALOG_CHECK_RVA       = 0x00491170 - 0x00400000;
constexpr uintptr_t FIRE_FUNNEL_RVA        = 0x00463240 - 0x00400000;
constexpr uintptr_t SACRED_HASH_RVA        = 0x0080e780 - 0x00400000;

// FunkCode walker — __thiscall, ECX = cQuestManager pointer. We hook this
// not for trigger-name dispatch (that's the four hooks above) but to grab
// the cQuestManager pointer so other subsystems can read/write its arrays.
// First fire wins; subsequent fires are silently ignored.
constexpr uintptr_t QUEST_WALKER_RVA       = 0x00475680 - 0x00400000;
// DIAGNOSTIC: questbook-event ctor FUN_00423ac0 (__thiscall, ECX = the
// 0x6c event object being built; stack arg1 = event type id 0x1ba/0x1bd/
// 0x1bf..., the rest = payload fields). EVERY questbook event (incl. the
// "quest solved" 0x1bd banner+chime) is built here, so hooking the ctor
// captures the real payload regardless of which handler triggered it.
constexpr uintptr_t QB_EVENT_CTOR_RVA      = 0x00423ac0 - 0x00400000;

// Dialog-ANSWER handler FUN_0048FF10 (tag-0x76 SelfTriggerQuest record
// handler). Red-team-CONFIRMED (dialog_runtime.md "Talk-signal recipe —
// red-team verdict"): this is THE "the player just answered NPC X"
// point — entered on every answer of an SDK-armed dialog (the named
// self_trigger/dialog_check hooks do NOT fire for FIX-A2 dialogs).
// __thiscall ECX=qm; at the call site (jump tbl [0x4784C0+0x76*4] ->
// 0x478470: push ebp; push &rec; ecx=qm; call) the answered NPC's
// cCreature* = arg2 = ebp = dispatcher __RTDynamicCast(param_4).
// Prologue `6A FF 68` -> standard 7-byte install_hook. We recover arg2
// offset-robustly by capturing the ORIGINAL entry esp via lea in the
// thunk (arg2 is unambiguously *(entry_esp+8)), then fire a
// collision-free "DLGANS:<bound DlgNPC name>" so only THIS hook can
// trigger the quest step (never the bind-time funnel).
constexpr uintptr_t DIALOG_ANSWER_RVA      = 0x0048ff10 - 0x00400000;

// Conversation-window pump FUN_00461540 (RE 2026-05-18 + red-team).
// FUN_0048FF10/by-name hooks are STRUCTURALLY dead for a FIX-A2 SDK
// dialog (no dialog-tree → no tag-0x76/tag-9 record ever replayed; two
// live builds proved zero hits). FUN_00461540 is the GUI conversation
// pump that renders entry+0x4c gated by +0x14&0x80000 (the path that
// DOES run — the window opens). __thiscall ECX=qm; the talked-to
// creature is resolved into EBP at 0x004615f3 via FUN_00464bd0(refKey)
// → om → __RTDynamicCast. Prologue `6A FF 68 9B E8 85 00` = 2 whole
// PI instrs → standard 7-byte install_hook. Red-team PASSed ABI/bytes
// but flagged the load-bearing premise UNPROVEN by static disasm:
// does the player talking to a tree-less FIX-A2 NPC actually drive
// this fn (vs the name-gated default at 0x46159b)? Settle it with a
// pure READ-ONLY probe FIRST (no writes, no teardown, no FUN_005498f0
// — that mutates bind-state, 24 callers, banned): on entry resolve the
// pushed refKey via the engine's OWN FUN_00464bd0 exactly as 0x4615c4
// does, log whether it maps to a bound SDK handle. Binary observation,
// zero crash/state risk. If it fires for our NPC → build the real
// close-edge signal on 0x00461e5c; if zero → abandon, no build wasted.
constexpr uintptr_t DIALOG_PUMP_RVA         = 0x00461540 - 0x00400000;

// Per-creature dialog driver FUN_0056B130 (RE 2026-05-18, the ONE
// non-refuted lead). Disasm-pinned: SEH fn, this=ECX, creature=
// *(this+4); reads EXACTLY the FIX-A2-written fields — +0x200 class
// bits, +0x251 owner, +0x2b7 stationary, +0x245 objIdx, bounds it
// against the DlgNPC vector (÷0x50), then edx=*(creature+0xc) (our
// content handle) → call FUN_004A71F0(ECX=qm, +0xc, &buf). Prologue
// `6A FF 68 5B 84 86 00` = 7-byte SEH, install_hook-compatible.
// READ-ONLY probe: log creature/objIdx/+0xc/+0x14 + om->handle on
// entry (throttled). Proves whether THIS fn runs when the player
// talks to our FIX-A2 captain (every documented fn was refuted live;
// methodology = probe -> live fact -> red-team -> only then code).
constexpr uintptr_t DIALOG_DRV_RVA          = 0x0056b130 - 0x00400000;

// FUN_00465220 = the dialog-content SETTER (read-only probe). The Dialog
// handler FUN_0048f9e0 calls it as __thiscall ECX=ctx, push idx, push
// content-handle ([edi+0xc]) to install dialog text. Our say()/dialog_arm
// bypasses it (writes entry+0x4c raw) → random text. Probe captures the REAL
// resolved content-handle format on a vanilla talk so we can replicate it.
// Prologue `56 8B 74 24 08` (push esi; mov esi,[esp+8]).
constexpr uintptr_t DIALOG_CONTENT_RVA      = 0x00465220 - 0x00400000;

// Engine creature pos+sector setter FUN_0054d9d0 (__thiscall, ECX =
// cCreature*; stack args x, y, level, flag). BOTH FunkCode teleport
// handlers (tag 0x2e FUN_00491d40, tag 0x5c FUN_004a2b40) funnel the
// actual placement through this — including the campaign new-game hero
// start. Prologue is `81EC9C000000 53` (sub esp,0x9c; push ebx) = 7
// position-independent bytes, so the standard 7-byte trampoline applies.
// We hook it to (a) log every call's args (reveals the real start-tp
// coordinate space) and (b) when armed by sacred.arm_spawn_teleport,
// rewrite the destination of the FIRST hero teleport after world load —
// the engine then places the hero at our point via its OWN correct path.
constexpr uintptr_t ENGINE_TP_RVA          = 0x0054d9d0 - 0x00400000;

// Journal list builder FUN_006b07e0 (questbook_render.md): walks the
// whole quest registry and renders a row ONLY if entry+0x24 != 0. It is
// the SINGLE reader of that gate. Hooking its entry lets us zero +0x24
// for vanilla (non-SDK) quests RIGHT BEFORE the build reads it — race-
// free (the tick approach lost to FUN_00496080 re-stamping +0x24 just
// before render). Quest logic / intro stay 100% vanilla; only the
// journal ROW is gated off. Prologue `81 EC DC 00 00 00 53` (7 PI bytes).
constexpr uintptr_t JOURNAL_BUILD_RVA       = 0x006b07e0 - 0x00400000;

// Kompass/marker resolver FUN_004a5980 (questbook_render.md): the on-map
// quest dot. Driven by per-class marker slots cQuestMgr+0x3a0/+0x3a4 +
// C*8 (entry index) and entry+0x10/+0x14 coords; INDEPENDENT of the
// journal builder (compass refreshes continuously). Decomp shows
// `if ((int)slot < 0) return 0;` → slot = -1 means "no marker". Hooking
// its entry lets us, race-free, point vanilla markers at -1 + zero their
// coords. Prologue `83 EC 38 8B 44 24 48` (7 PI bytes).
constexpr uintptr_t MARKER_RESOLVE_RVA      = 0x004a5980 - 0x00400000;

constexpr size_t    PROLOGUE_BYTES         = 7;
constexpr uintptr_t NAME_BUFFER_OFFSET     = 0xa460;

// Per-hook trampoline pointers — `jmp dword ptr [g_tramp_*]` in the naked
// thunks reaches them through this static cell.
static uint8_t* g_tramp_self_trigger = nullptr;
static uint8_t* g_tramp_dialog_check = nullptr;
static uint8_t* g_tramp_funnel       = nullptr;
static uint8_t* g_tramp_hash         = nullptr;
static uint8_t* g_tramp_walker       = nullptr;
static uint8_t* g_tramp_qsolved      = nullptr;
static uint8_t* g_tramp_engine_tp    = nullptr;
static uint8_t* g_tramp_journal      = nullptr;
static uint8_t* g_tramp_marker       = nullptr;
static uint8_t* g_tramp_dlg_answer   = nullptr;
static uint8_t* g_tramp_dlg_pump     = nullptr;
static uint8_t* g_tramp_dlg_drv      = nullptr;
static uint8_t* g_tramp_dlg_content  = nullptr;
volatile bool g_hide_vanilla         = false;   // gate journal suppression
                                                // (extern-declared earlier)

// =========================================================================
// cQuestManager pointer + named-state ([ECX+0x334..+0x338]) accessors.
// =========================================================================
//
// Recon (see docs/community-refs.md + sdk/.claude/knowledge/re/questbook_recon.md):
// Sacred's cQuestManager singleton owns TWO parallel arrays:
//
//   * +0x424..+0x428   stride 0x174   key=quest_id u32 at entry+8
//                      → quest-display registry (the "journal")
//   * +0x334..+0x338   stride 0x64    key=cstring name at entry+4
//                      → named-state store ("hq_uw", "dq_belohnung", …)
//                        used by FunkCode tag 0x69 (VarAssign_int) and
//                        consumed by guard-eval paths.
//
// FUN_00478780 is the upsert routine for the named-state store: lookup by
// strcmp on entry+4, write 100 bytes from a stack-built record into the
// matched slot, OR push_back if no match. The write block looks like:
//
//   begin = [mgr+0x334]
//   end   = [mgr+0x338]
//   cap   = [mgr+0x33c]
//   for (slot = begin; slot < end; slot += 0x64)
//       if (slot[0] == 0 && strcmp(slot+4, name) == 0) overwrite
//   else if (end == cap) FUN_004b9250(grow); else memcpy(end, &record, 100)
//
// We expose READ + WRITE-IN-PLACE only. Append (the grow-and-push-back
// path) requires understanding FUN_004b9250's allocator; that's a future
// extension. For now if the variable doesn't exist, set fails — modders
// must declare it via bake-time q.var()/q.assign() first.
//
// Entry layout (100 B = 0x64):
//   +0x00  u32  status      0 = active, nonzero = tombstoned (skip)
//   +0x04  cstring name     null-terminated, max ~60 chars
//   +0x44  i32  value 0     primary numeric value (most quest vars)
//   +0x48  i32  value 1
//   +0x4C  i32  value 2
//   +0x50  i32  value 3
//   +0x54  i32  reserved (0xffffffff sentinel)
//   +0x58  i32  reserved (0xffffffff sentinel)
//   +0x5C..+0x63  padding/unknown

// Constants + StateRow struct are declared up in the forward block (right
// before the Lua bindings). g_quest_mgr is declared `extern volatile` there
// and DEFINED here.
volatile uintptr_t g_quest_mgr      = 0;
static volatile long g_quest_mgr_seen = 0;   // walker fire-count

// SDK_WORLD_LOAD_KEY is declared up with SDK_HANDLERS_KEY / SDK_CTX_KEY.
// Recap of why on_world_load matters: the FunkCode walker loop dispatches
// tag-0x35 (log_entry) records. By the time the LOOP body runs, this
// entry hook has already captured cQuestManager AND fired on_world_load
// handlers — so a handler's `sacred.questbook_register(9999)` lands
// BEFORE the walker hits the modder's tag-0x35 records for that id.

// Forward decl — fire() is below; we call it from capture_quest_mgr.
static void fire_world_load_handlers();

// Walker hook — fired by every FunkCode record dispatch on the main thread.
// We just snapshot ECX once and bail out fast. Subsequent fires bump the
// counter for diagnostics.
// True if the quest-display registry currently holds at least one of the
// quest_ids the SDK registered. Used to tell a FRESH world (our quests
// absent → re-fire handlers) from steady state / vector growth (present).
static bool sdk_quests_present() {
    int n = questbook_count_safe();
    if (n <= 0) return false;
    for (int i = 0; i < n; i++) {
        int64_t q = questbook_quest_id_at(i);
        if (q > 0 && sdk_qid_has((uint32_t)q)) return true;
    }
    return false;
}

static uintptr_t g_last_reg_begin = 0;

// Walker hook — fires on every FunkCode record dispatch. Must re-fire
// on_world_load on EACH world load (return-to-menu → new game in the same
// process), not once per process. Cheap steady-state exit: the quest
// registry vector's `begin` pointer only changes on (re)allocation, so we
// gate the heavier work on that. When it changes we re-evaluate: if our
// SDK quests are absent the registry is fresh ⇒ a new world loaded ⇒
// re-fire handlers (re-registers quests, re-arms spawn hijack, etc.).
extern "C" void __cdecl capture_quest_mgr(uintptr_t ecx) {
    InterlockedIncrement(&g_quest_mgr_seen);
    if (!ecx || ecx < 0x00400000 || ecx > 0xF0000000) return;

    uintptr_t begin = 0;
    __try { begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    // Steady state: registry vector unmoved and already captured → cheap out.
    if (begin == g_last_reg_begin && g_quest_mgr) return;

    g_quest_mgr      = ecx;
    g_last_reg_begin = begin;

    // Vector (re)allocated. If our quests are still there it's just growth
    // or the post-register realloc of the SAME load — do nothing.
    if (sdk_quests_present()) return;

    // Fresh/empty registry ⇒ a new world load. Re-fire on_world_load here,
    // BEFORE the walker dispatches this load's tag-0x35 records, so
    // sacred.questbook_register()/arm_spawn_teleport() land in time.
    sdk_log("[runtime_triggers] world load — cQuestManager=%p begin=%p; "
            "firing on_world_load", (void*)ecx, (void*)begin);
    fire_world_load_handlers();
}

// Implementation lives near the trigger fire path because both paths
// share the same lua_State and pcall plumbing.
static void fire_world_load_handlers() {
    if (!g_ready || !g_L) return;
    lua_State* L = g_L;
    int top0 = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, SDK_WORLD_LOAD_KEY);
    if (!lua_istable(L, -1)) { lua_settop(L, top0); return; }
    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); continue; }
        int r = lua_pcall(L, 0, 0, 0);
        if (r != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            sdk_log("[runtime_triggers] on_world_load handler #%d error: %s",
                    i, msg ? msg : "?");
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top0);
    sdk_log("[runtime_triggers] on_world_load: fired %d handler(s)", n);
}

__declspec(naked) static void __cdecl hook_quest_walker() {
    __asm {
        pushfd
        push eax
        push edx
        push ecx
        push ecx                       ; arg = cQuestManager pointer
        call capture_quest_mgr
        add  esp, 4
        pop  ecx
        pop  edx
        pop  eax
        popfd
        jmp  dword ptr [g_tramp_walker]
    }
}

// Walk the named-state array. Returns count, fills `out` (caller-allocated).
// Inactive entries (status != 0) are skipped. Names are copied (truncated to
// 64 bytes) into out[i].name; values into out[i].v[0..3]. (StateRow itself
// is defined up near the Lua bindings since the bindings need its sizeof.)

static int state_walk(StateRow* out, int max) {
    if (!g_quest_mgr || !out || max <= 0) return -1;
    uintptr_t mgr = g_quest_mgr;
    uintptr_t begin = 0, end = 0;
    __try {
        begin = *(uintptr_t*)(mgr + MGR_OFF_STATE_BEGIN);
        end   = *(uintptr_t*)(mgr + MGR_OFF_STATE_END);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (!begin || end < begin) return 0;
    int total = (int)((end - begin) / STATE_ENTRY_STRIDE);
    int kept  = 0;
    for (int i = 0; i < total && kept < max; i++) {
        uintptr_t e = begin + (uintptr_t)i * STATE_ENTRY_STRIDE;
        __try {
            uint32_t status = *(uint32_t*)(e + STATE_OFF_STATUS);
            if (status != 0) continue;          // tombstoned / unallocated slot
            const char* name = (const char*)(e + STATE_OFF_NAME);
            // Name validation — 1..60 printable chars.
            int n = 0;
            while (n < 60 && name[n]) {
                unsigned char c = (unsigned char)name[n];
                if (c < 0x20 || c >= 0x80) { n = 0; break; }
                n++;
            }
            if (n == 0) continue;
            strncpy_s(out[kept].name, sizeof(out[kept].name), name, _TRUNCATE);
            for (int v = 0; v < STATE_VALUE_COUNT; v++) {
                out[kept].v[v] = *(int32_t*)(e + STATE_OFF_VALUES + v * 4);
            }
            out[kept].addr = e;
            kept++;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }
    return kept;
}

// Lookup one entry by name. Returns address of the entry or 0 if not found.
// Caller can then read/write the value slots directly at +0x44..+0x50.
static uintptr_t state_find(const char* name) {
    if (!g_quest_mgr || !name || !*name) return 0;
    uintptr_t mgr = g_quest_mgr;
    uintptr_t begin = 0, end = 0;
    __try {
        begin = *(uintptr_t*)(mgr + MGR_OFF_STATE_BEGIN);
        end   = *(uintptr_t*)(mgr + MGR_OFF_STATE_END);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!begin || end < begin) return 0;
    int total = (int)((end - begin) / STATE_ENTRY_STRIDE);
    for (int i = 0; i < total; i++) {
        uintptr_t e = begin + (uintptr_t)i * STATE_ENTRY_STRIDE;
        __try {
            if (*(uint32_t*)(e + STATE_OFF_STATUS) != 0) continue;
            const char* slot_name = (const char*)(e + STATE_OFF_NAME);
            if (strncmp(slot_name, name, 60) == 0 && slot_name[strlen(name)] == 0) {
                return e;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }
    return 0;
}

// Write up to 4 values into an EXISTING entry. Append (creating new entries)
// is not yet supported — caller gets false back if the name doesn't exist.
static bool state_write_in_place(const char* name,
                                 int n_values,
                                 const int32_t* values) {
    if (n_values <= 0 || n_values > STATE_VALUE_COUNT) return false;
    uintptr_t e = state_find(name);
    if (!e) return false;
    void* dst = (void*)(e + STATE_OFF_VALUES);
    if (IsBadWritePtr(dst, n_values * sizeof(int32_t))) return false;
    __try {
        for (int i = 0; i < n_values; i++) {
            *(int32_t*)(e + STATE_OFF_VALUES + i * 4) = values[i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    sdk_log("[runtime_triggers] state_set '%s' (%d values, first=%d) @ %p",
            name, n_values, values[0], (void*)e);
    return true;
}

// Ring-buffer push. Caller must already hold g_ring_cs (or it's not init).
// Time-window dedup: skip if the same name was pushed within RING_DEDUP_MS.
// This filters per-frame UI spam without losing rare events.
static void ring_push_locked(const char* name) {
    uint64_t now = GetTickCount64();
    // Linear scan — RING_CAP is small enough for this to be fast.
    for (int i = 0; i < g_ring_count; i++) {
        if (strcmp(g_ring[i].text, name) == 0) {
            if ((now - g_ring[i].pushed_at) < RING_DEDUP_MS) {
                return;   // recently seen — skip
            }
            // Stale duplicate; remove it (shift left) and fall through to push.
            for (int j = i; j < g_ring_count - 1; j++) {
                g_ring[j] = g_ring[j + 1];
            }
            g_ring_count--;
            // Adjust head if it moved past us.
            if (g_ring_head > i) g_ring_head--;
            break;
        }
    }
    RingEntry& e = g_ring[g_ring_head];
    strncpy_s(e.text, 128, name, _TRUNCATE);
    e.pushed_at = now;
    g_ring_head = (g_ring_head + 1) % RING_CAP;
    if (g_ring_count < RING_CAP) g_ring_count++;
}

// Snapshot up to max_lines names, MOST RECENT FIRST, into a caller buffer.
int last_seen(char (*out)[128], int max_lines) {
    if (!g_ring_cs_init) return 0;
    EnterCriticalSection(&g_ring_cs);
    int n = (g_ring_count < max_lines) ? g_ring_count : max_lines;
    for (int i = 0; i < n; i++) {
        // walk backwards from head-1, wrapping
        int idx = (g_ring_head - 1 - i + RING_CAP) % RING_CAP;
        strncpy_s(out[i], 128, g_ring[idx].text, _TRUNCATE);
    }
    LeaveCriticalSection(&g_ring_cs);
    return n;
}

// Empty the ring. Use right before an in-game action; subsequent
// snapshots will then only contain names that flowed through since.
void clear_ring() {
    if (!g_ring_cs_init) return;
    EnterCriticalSection(&g_ring_cs);
    g_ring_count = 0;
    g_ring_head  = 0;
    LeaveCriticalSection(&g_ring_cs);
    sdk_log("[runtime_triggers] ring cleared");
}

// Dump the whole ring (oldest first) to sdk_loaded.log so the modder
// can correlate gameplay actions with the resource ids Sacred queried.
// Triggered from the overlay button right after the modder does the
// thing they want to react to.
void snapshot_ring_to_log() {
    if (!g_ring_cs_init) {
        sdk_log("[runtime_triggers] snapshot: ring not initialised");
        return;
    }
    char     snap_text[RING_CAP][128];
    uint64_t snap_age[RING_CAP];
    int      n;
    uint64_t now;
    EnterCriticalSection(&g_ring_cs);
    n = g_ring_count;
    now = GetTickCount64();
    for (int i = 0; i < n; i++) {
        int idx = (g_ring_head - n + i + RING_CAP) % RING_CAP;
        strncpy_s(snap_text[i], 128, g_ring[idx].text, _TRUNCATE);
        snap_age[i] = now - g_ring[idx].pushed_at;
    }
    LeaveCriticalSection(&g_ring_cs);

    sdk_log("[runtime_triggers] === snapshot of last %d unique names "
            "(oldest first, age in ms) ===", n);
    for (int i = 0; i < n; i++) {
        sdk_log("[runtime_triggers]   %2d. [%5llu ms ago] '%s'",
                i + 1, snap_age[i], snap_text[i]);
    }
    sdk_log("[runtime_triggers] === end snapshot ===");
}

// C-side helper: validate ECX-as-cstring-buffer, record, dispatch.
// `which` is 0 for self_trigger, 1 for dialog_check — used for the
// per-source counter and the log tag.
static void read_name_and_fire_impl(uintptr_t ctx, int which) {
    // FIRST thing: bump the unconditional thunk-entry counter. This is
    // higher than g_seen_* if our hook fires but cstring validation
    // rejects the bytes at [ctx+0xa460].
    if (which == 0) InterlockedIncrement(&g_thunk_self_trigger);
    else            InterlockedIncrement(&g_thunk_dialog_check);
    if (!ctx) return;
    const char* name = (const char*)(ctx + NAME_BUFFER_OFFSET);
    char copy[128];
    __try {
        // Sanity: cstring 1..127 chars, printable ASCII only. Garbage means
        // ctx isn't what we think (different subsystem, weird call path);
        // bail silently to avoid log spam.
        int n = 0;
        while (n < 128 && name[n]) {
            unsigned char c = (unsigned char)name[n];
            if (c < 0x20 || c >= 0x80) return;
            copy[n] = name[n];
            n++;
        }
        if (n == 0 || n == 128) return;
        copy[n] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    // Bump the right diagnostic counter.
    if (which == 0) InterlockedIncrement(&g_seen_self_trigger);
    else            InterlockedIncrement(&g_seen_dialog_check);

    // Push into the ring so the overlay can show recent names.
    if (g_ring_cs_init) {
        EnterCriticalSection(&g_ring_cs);
        ring_push_locked(copy);
        LeaveCriticalSection(&g_ring_cs);
    }
    // No logging on this path — overlay is the diagnostic surface.

    // TEMP DIAG (remove after the quest is wired to the real name):
    // log each DISTINCT dialog/self-trigger name once, tagged by which
    // hook fired it, so we can see EXACTLY what string flows when the
    // player talks to Captain Miles / Rocheford and gate the quest on
    // that — no guessing.
    {
        static char s_last[128] = {0};
        if (strcmp(s_last, copy) != 0) {
            size_t L = 0; while (copy[L] && L < 126) { s_last[L]=copy[L]; L++; }
            s_last[L] = 0;
            sdk_log("[trig:%s] '%s'", which == 0 ? "self" : "dlg", copy);
        }
    }

    // Actually fire registered handlers (no-op if Lua state isn't ready).
    fire(copy);
}

extern "C" void __cdecl read_name_self_trigger(uintptr_t ctx) {
    read_name_and_fire_impl(ctx, 0);
}
extern "C" void __cdecl read_name_dialog_check(uintptr_t ctx) {
    read_name_and_fire_impl(ctx, 1);
}

// Predicate for log filtering: is this a "trigger-looking" name? Used to
// avoid spamming sdk_log with every resource/item/string Sacred hashes
// (sacred_hash is called THOUSANDS of times per second).
static bool name_looks_triggery(const char* s) {
    if (!s) return false;
    // Pure-decimal numeric IDs are also interesting — Sacred uses
    // `res:1042` form heavily for dialog/quest text refs.
    bool all_digits = (s[0] != 0);
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9') { all_digits = false; break; }
    }
    if (all_digits) return true;
    return (strstr(s, "_DLG_")     ||
            strstr(s, "Trigger")   ||
            strstr(s, "trigger")   ||
            strstr(s, "_Auftrag_") ||
            strstr(s, "Quest")     ||
            strstr(s, "quest")     ||
            strstr(s, "HQ_")       ||
            strstr(s, "DQ_")       ||
            strstr(s, "NQ_")       ||
            strstr(s, "_DLG")      ||
            strstr(s, "_Log_")     ||
            strstr(s, "SDK_")      ||
            false);
}

// Sacred-hash hook: FUN_0080e780 is the symbolic-name → hash entry point.
// __cdecl: arg at [esp+4] on entry. The arg is EITHER a `const char*`
// pointer OR a small numeric resource id (Sacred packs `res:1042` style
// references as the bare integer 1042). Per sacred_hash.py docstring:
//   "Numeric ids pass through 0080e780's str(id)+hash path before reaching
//    0080eaf0 unless their bit 31 is set."
// So if arg looks small, it's a numeric id; format as decimal cstring.
extern "C" void __cdecl read_name_hash(const char* arg_ptr) {
    InterlockedIncrement(&g_thunk_hash);

    char   copy[128];
    uintptr_t v = (uintptr_t)arg_ptr;

    if (v == 0) return;

    if (v < 0x100000) {
        _snprintf_s(copy, _TRUNCATE, "%lu", (unsigned long)v);
    } else if (v & 0x80000000) {
        _snprintf_s(copy, _TRUNCATE, "0x%08x", (unsigned)v);
    } else {
        __try {
            int n = 0;
            while (n < 128 && arg_ptr[n]) {
                unsigned char c = (unsigned char)arg_ptr[n];
                if (c < 0x20 || c >= 0x80) return;
                copy[n] = arg_ptr[n];
                n++;
            }
            if (n == 0 || n == 128) return;
            copy[n] = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    }

    InterlockedIncrement(&g_seen_hash);

    // Push to ring for the overlay. The ring's own dedup avoids
    // back-to-back repeats so the overlay stays useful even when Sacred
    // hammers the same id per-frame.
    if (g_ring_cs_init) {
        EnterCriticalSection(&g_ring_cs);
        ring_push_locked(copy);
        LeaveCriticalSection(&g_ring_cs);
    }

    // fire() does a fast dict lookup; misses are O(1). Lua-side
    // `on_trigger_once` dedups invocations so the modder's callback runs
    // exactly once per name per session.
    fire(copy);
}

// Funnel hook: FUN_00463240 takes the trigger name as a `const char*`
// argument at [esp+0x08]. Single chokepoint for BOTH local-fire (called
// from FUN_00491170 @ 0x00491406) and network/event-replay
// (called from 0x0080777e). This is the canonical sink.
extern "C" void __cdecl read_name_funnel(const char* name_ptr) {
    InterlockedIncrement(&g_thunk_funnel);
    if (!name_ptr) return;

    char copy[128];
    __try {
        int n = 0;
        while (n < 128 && name_ptr[n]) {
            unsigned char c = (unsigned char)name_ptr[n];
            if (c < 0x20 || c >= 0x80) return;
            copy[n] = name_ptr[n];
            n++;
        }
        if (n == 0 || n == 128) return;
        copy[n] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    InterlockedIncrement(&g_seen_funnel);

    if (g_ring_cs_init) {
        EnterCriticalSection(&g_ring_cs);
        ring_push_locked(copy);
        LeaveCriticalSection(&g_ring_cs);
    }
    // No logging on this path — overlay shows the counter.

    fire(copy);
}

// Naked thunk for FUN_004915a0. On entry ECX = ctx, EDX = record. We
// preserve those (plus flags), pass ECX to our helper, restore, then jump
// to the trampoline (saved prologue + jmp back to func+7).
__declspec(naked) static void __cdecl hook_self_trigger_quest() {
    __asm {
        pushfd
        push edx
        push ecx
        push ecx                       ; arg = ctx
        call read_name_self_trigger
        add  esp, 4
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_self_trigger]
    }
}

__declspec(naked) static void __cdecl hook_dialog_check() {
    __asm {
        pushfd
        push edx
        push ecx
        push ecx
        call read_name_dialog_check
        add  esp, 4
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_dialog_check]
    }
}

// Dialog-answer C helper. `entry_esp` = the ORIGINAL esp at FUN_0048FF10
// entry (captured via lea in the thunk, so it is push-count-independent
// — sidesteps the unresolved [esp+0x10 vs 0x18] displacement). Layout
// at entry: [+0]=retaddr [+4]=arg1(&rec) [+8]=arg2 = answered NPC
// cCreature*. We reverse-map cre->SDK handle (om array), resolve its
// bound DlgNPC name (entry+0x04 at idx = *(u8)(cre+0x245)), and fire a
// collision-free "DLGANS:<name>" so ONLY a real player answer triggers
// the quest. Fully SEH-guarded + validated: a wrong ptr just no-ops.
extern "C" void __cdecl read_dialog_answer(uintptr_t entry_esp) {
    if (!entry_esp) return;
    __try {
        uintptr_t cre = *(uintptr_t*)(entry_esp + 8);     // arg2
        // UNCONDITIONAL entry log (throttled) — earlier code returned
        // SILENTLY when cre<0x10000, so "zero [dlganswer]" did NOT prove
        // the hook never fired (FUN_0048FF10's arg2/ebp is 0 on many
        // dispatch paths). This proves whether FUN_0048FF10 fires AT ALL
        // (vanilla answer = the target mechanism) regardless of cre.
        static int s_dlgans_seen = 0;
        if (s_dlgans_seen < 120) {
            s_dlgans_seen++;
            sdk_log("[dlgans-raw] #%d cre=%p", s_dlgans_seen,
                    (void*)cre);
        }
        if (cre < 0x10000) return;
        uintptr_t om = *(uintptr_t*)0x00AD5C40;
        if (!om) return;
        uintptr_t arr = *(uintptr_t*)(om + 4);
        if (!arr) return;
        int handle = -1;
        for (int i = 1; i < 8192; i++) {
            if (*(uintptr_t*)(arr + (uintptr_t)i * 4) == cre) {
                handle = i; break;
            }
        }
        if (handle < 0) {
            sdk_log("[dlganswer] cre=%p -> no handle", (void*)cre);
            return;
        }
        uint8_t idx = *(uint8_t*)(cre + 0x245);           // DlgNPC idx
        uintptr_t db = *(uintptr_t*)(0x00AACF80 + 0x755c);
        uintptr_t de = *(uintptr_t*)(0x00AACF80 + 0x7560);
        if (!db || de < db) {
            sdk_log("[dlganswer] h=%d no dlg vec", handle); return;
        }
        if (idx >= (uint32_t)((de - db) / 0x50)) {
            sdk_log("[dlganswer] h=%d idx=%u oob", handle, idx); return;
        }
        const char* nm = (const char*)(db + (uintptr_t)idx * 0x50 + 4);
        char tok[160]; int n = 0;
        tok[0]='D';tok[1]='L';tok[2]='G';tok[3]='A';tok[4]='N';
        tok[5]='S';tok[6]=':';
        while (n < 140 && nm[n]) {
            unsigned char ch = (unsigned char)nm[n];
            if (ch < 0x20 || ch >= 0x80) { n = 0; break; }
            tok[7 + n] = nm[n]; n++;
        }
        if (n == 0) {
            sdk_log("[dlganswer] h=%d idx=%u bad name", handle, idx);
            return;
        }
        tok[7 + n] = 0;
        sdk_log("[dlganswer] h=%d idx=%u name='%s' -> fire '%s'",
                handle, idx, tok + 7, tok);
        fire(tok);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[dlganswer] faulted (esp=%p)", (void*)entry_esp);
    }
}

// Naked thunk for FUN_0048FF10. We do NOT need ECX/EDX; we capture the
// ORIGINAL entry esp with `lea eax,[esp+0x0C]` (after pushfd+edx+ecx =
// 0xC bytes pushed, so esp+0xC == entry esp) and pass that pointer —
// arg2 is then unambiguously *(eax+8) regardless of any push counting.
__declspec(naked) static void __cdecl hook_dlg_answer() {
    __asm {
        pushfd
        push edx
        push ecx
        lea  eax, [esp+0x0C]
        push eax
        call read_dialog_answer
        add  esp, 4
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_dlg_answer]
    }
}

// --- READ-ONLY probe for the conversation pump FUN_00461540 -----------
// `entry_esp` = ORIGINAL esp at fn entry (lea-captured, push-count
// independent). __thiscall: ECX=qm, ONE pushed stack arg (caller
// 0x48c3a5 `push esi; call`) → refKey at *(entry_esp+4).
//
// PROBE RESULT (2026-05-18, user live test): premise #1 CONFIRMED —
// FUN_00461540 fires when the player talks to our tree-less FIX-A2
// NPC. Load/init runs it with refKey==0 (136 hits, noise); a real
// player conversation produced exactly the talked-to NPC's non-zero
// refKey (3159AF78, 2 hits ~17s after the captain spawned). So refKey
// (==FUN_0044A1C0(cCreature), the same value dialog_arm computes and
// the engine compares at FUN_00464bd0:59) uniquely identifies the
// talked-to NPC. We DROP the engine FUN_00464bd0 call entirely (its
// probe return was a non-bool artifact and it is an unnecessary engine
// call) and instead match refKey against an SDK-side table that
// dialog_arm fills at arm time {refKey,handle,name}. On a non-zero
// match we fire the SAME collision-free "DLGANS:<name>" the dead
// FUN_0048FF10 hook used, so the existing quest on_trigger gates work
// unchanged. Pure reads only; fully SEH-guarded.

struct DlgPumpBind { uint32_t refKey; int handle; char name[64]; };
static DlgPumpBind g_dlg_pump_tab[32];
static int         g_dlg_pump_n   = 0;
static uint32_t    g_dlg_pump_last = 0;   // de-dup: last fired refKey

// Called by player_state.cpp dialog_arm at arm time. Idempotent per
// refKey (refresh handle/name; reset the de-dup latch so a NEW
// conversation on the same NPC fires again).
extern "C" void sdk_dlg_pump_register(uint32_t refKey, int handle,
                                      const char* name) {
    if (!refKey || !name) return;
    for (int i = 0; i < g_dlg_pump_n; i++) {
        if (g_dlg_pump_tab[i].refKey == refKey) {
            g_dlg_pump_tab[i].handle = handle;
            int k = 0; while (k < 63 && name[k]) { g_dlg_pump_tab[i].name[k]=name[k]; k++; }
            g_dlg_pump_tab[i].name[k] = 0;
            if (g_dlg_pump_last == refKey) g_dlg_pump_last = 0;
            return;
        }
    }
    if (g_dlg_pump_n >= 32) return;
    DlgPumpBind& e = g_dlg_pump_tab[g_dlg_pump_n++];
    e.refKey = refKey; e.handle = handle;
    int k = 0; while (k < 63 && name[k]) { e.name[k]=name[k]; k++; }
    e.name[k] = 0;
    sdk_log("[dlgreg] refKey=%08X h=%d name='%s' (n=%d)",
            refKey, handle, e.name, g_dlg_pump_n);
}

extern "C" void __cdecl read_dialog_pump(uintptr_t entry_esp) {
    if (!entry_esp) return;
    __try {
        unsigned refKey = *(unsigned*)(entry_esp + 4);   // pushed arg1
        int mi = -1;
        for (int i = 0; i < g_dlg_pump_n; i++)
            if (refKey && g_dlg_pump_tab[i].refKey == refKey) {
                mi = i; break;
            }
        // FULL DIAGNOSTIC (no blind spot). The load/init refKey==0 noise
        // all occurs BEFORE the captain is registered (dlgreg → n>0). A
        // talk hit occurs AFTER. So: always log non-zero; log refKey==0
        // ONLY once the captain is armed (n>0) — that excludes pre-arm
        // load noise but KEEPS a talk hit even if it is itself refKey==0
        // (the case we must not miss) — bounded by a post-arm cap.
        static int s_zero_logged = 0;
        bool do_log = (refKey != 0) ||
                      (g_dlg_pump_n > 0 && s_zero_logged < 60);
        if (do_log) {
            if (refKey == 0) s_zero_logged++;
            sdk_log("[dlgpump] refKey=%08X match=%d (n=%d)", refKey,
                    mi, g_dlg_pump_n);
        }
        if (refKey && refKey == g_dlg_pump_last) return;  // already fired
        if (mi >= 0) {
            const char* nm = g_dlg_pump_tab[mi].name;
            char tok[96];
            tok[0]='D';tok[1]='L';tok[2]='G';tok[3]='A';tok[4]='N';
            tok[5]='S';tok[6]=':';
            int n = 0;
            while (n < 80 && nm[n]) { tok[7+n]=nm[n]; n++; }
            tok[7+n] = 0;
            g_dlg_pump_last = refKey;
            sdk_log("[dlgpump] refKey=%08X h=%d -> fire '%s'",
                    refKey, g_dlg_pump_tab[mi].handle, tok);
            fire(tok);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[dlgpump] faulted (esp=%p)", (void*)entry_esp);
    }
}

// Naked thunk — identical save form to hook_dlg_answer. lea eax,
// [esp+0x0C] after pushfd+edx+ecx (0xC) == entry esp; refKey is then
// *(eax+4). ECX (=qm) is preserved across our call for the original.
__declspec(naked) static void __cdecl hook_dlg_pump() {
    __asm {
        pushfd
        push edx
        push ecx
        lea  eax, [esp+0x0C]
        push eax
        call read_dialog_pump
        add  esp, 4
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_dlg_pump]
    }
}

// --- READ-ONLY probe for FUN_0056B130 (per-creature dialog driver) ---
// __thiscall: ECX=this; creature = *(this+4). We om-reverse-map the
// creature to an SDK handle (proven read_dialog_answer pattern) and
// log objIdx(+0x245)/content(+0xc)/flags(+0x14). Throttled (this fn
// may run per-tick per creature). PURE reads, SEH-guarded, NO writes,
// NO engine call -> cannot crash. Answers: does FUN_0056B130 run when
// the player talks to our FIX-A2 captain, and with what state.
extern "C" void __cdecl read_dialog_drv(uintptr_t thisptr) {
    if (thisptr < 0x10000) return;
    __try {
        uintptr_t cre = *(uintptr_t*)(thisptr + 4);
        if (cre < 0x10000) return;
        uint32_t v245 = *(uint8_t*) (cre + 0x245);
        uint32_t vC   = *(uint32_t*)(cre + 0x0c);
        uint32_t v14  = *(uint32_t*)(cre + 0x14);
        // om reverse-map creature -> SDK handle (read_dialog_answer ptn)
        int handle = -1;
        uintptr_t om = *(uintptr_t*)0x00AD5C40;
        if (om) {
            uintptr_t arr = *(uintptr_t*)(om + 4);
            if (arr) for (int i = 1; i < 8192; i++)
                if (*(uintptr_t*)(arr + (uintptr_t)i * 4) == cre) {
                    handle = i; break; }
        }
        // For a vanilla dialog NPC (objIdx != 0) read its DlgNPC entry's
        // content slot entry[objIdx]+0x4c — the REAL content format the
        // renderer uses (ours is a corrupted hash). Path A linchpin: this
        // is the value say() must produce. db = *(qm+0x755c), stride 0x50.
        uint32_t e4c = 0xFFFFFFFF, e48 = 0xFFFFFFFF;
        if (v245 != 0) {
            uintptr_t db = *(uintptr_t*)(0x00AACF80 + 0x755c);
            if (db) {
                uintptr_t en = db + (uintptr_t)v245 * 0x50;
                e48 = *(uint32_t*)(en + 0x48);
                e4c = *(uint32_t*)(en + 0x4c);
            }
        }
        static int s_drv = 0;
        // Always log a handle-resolved hit (our NPCs) OR any objIdx!=0 NPC
        // (vanilla dialog node — the format sample we need); throttle the
        // no-handle/objIdx=0 flood to the first 40.
        if (handle >= 0 || v245 != 0 || s_drv < 40) {
            s_drv++;
            sdk_log("[dlg56b] #%d cre=%p h=%d objIdx=%u +0xc=%08X "
                    "+0x14=%08X | dlgEntry +48=%08X +4c=%08X",
                    s_drv, (void*)cre, handle, v245, vC, v14, e48, e4c);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[dlg56b] faulted (this=%p)", (void*)thisptr);
    }
}

// --- READ-ONLY probe for FUN_00465220 (dialog-content setter) ---
// Captures the REAL resolved content-handle the engine installs for a
// dialog, so we can fix say()/dialog_arm (which writes a raw hash and gets
// random text). __thiscall ECX=ctx; [esp+4]=idx; [esp+8]=content-handle.
// Logs idx, content, and the dialog-array entry it targets
// (*(ctx+0x755c)+idx*0x50): entry+0(name/id), entry+0x4c(current content).
// Pure reads, SEH-guarded, NO writes/engine call.
extern "C" void __cdecl read_dialog_content_set(uintptr_t ctx, int idx,
                                                uint32_t content) {
    static int s_n = 0;
    __try {
        uintptr_t db = *(uintptr_t*)(ctx + 0x755c);
        uintptr_t entry = (db && idx >= 0) ? db + (uintptr_t)idx * 0x50 : 0;
        uint32_t e0 = 0, e4c = 0, e48 = 0;
        char name[40]; name[0] = 0;
        if (entry) {
            e0  = *(uint32_t*)(entry + 0x00);
            e48 = *(uint32_t*)(entry + 0x48);
            e4c = *(uint32_t*)(entry + 0x4c);
            const char* nm = (const char*)(entry + 0x04);   // ASCIIZ name
            int j = 0; for (; j < 38 && nm[j] >= 0x20 && nm[j] < 0x7f; j++)
                name[j] = nm[j];
            name[j] = 0;
        }
        if (s_n < 120) {
            s_n++;
            sdk_log("[dlgcontent] #%d idx=%d content=%08X | entry=%p "
                    "+0(id)=%08X +48=%08X +4c=%08X name='%s'",
                    s_n, idx, content, (void*)entry, e0, e48, e4c, name);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[dlgcontent] faulted (ctx=%p idx=%d)", (void*)ctx, idx);
    }
}

// Naked thunk — __thiscall ECX=ctx, stack [esp+4]=idx, [esp+8]=content.
// After pushfd+push edx+push ecx (12 bytes), originals shift by 12:
// idx at [esp+16], content at [esp+20].
__declspec(naked) static void __cdecl hook_dlg_content() {
    __asm {
        pushfd
        push edx
        push ecx
        mov  eax, [esp + 20]           ; content
        mov  edx, [esp + 16]           ; idx
        push eax                       ; arg3 = content
        push edx                       ; arg2 = idx
        push ecx                       ; arg1 = ctx (ECX)
        call read_dialog_content_set
        add  esp, 12
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_dlg_content]
    }
}

// Naked thunk — __thiscall, ECX=this. Pass ECX to the helper; restore
// ECX/EDX/flags for the original; jmp trampoline.
__declspec(naked) static void __cdecl hook_dlg_drv() {
    __asm {
        pushfd
        push edx
        push ecx
        push ecx                       ; arg = this (ECX)
        call read_dialog_drv
        add  esp, 4
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_dlg_drv]
    }
}

// FUN_00463240 hook. __thiscall: ECX = quest hash, EDX free; stack at
// entry is:
//   [esp+0]   = return address (pushed by CALL)
//   [esp+4]   = arg0 = script_ctx*
//   [esp+8]   = arg1 = const char* trigger_name   ← we want this
//   [esp+12]  = arg2 = dialog table index
//   [esp+16]  = arg3 = retaddr-like marker
//
// We pushfd + push edx + push ecx (3 dwords = 12 bytes) before the arg
// push, so the original [esp+8] is now at [esp+8+12] = [esp+20]. We
// `push dword ptr [esp+20]` to pass it to our C helper as the first
// cdecl arg.
__declspec(naked) static void __cdecl hook_funnel() {
    __asm {
        pushfd
        push edx
        push ecx
        push dword ptr [esp+20]       ; arg = original [esp+8] = name pointer
        call read_name_funnel
        add  esp, 4
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_funnel]
    }
}

// DIAGNOSTIC capture for the questbook-event ctor FUN_00423ac0.
// __thiscall: ECX = the 0x6c event object; stack args = (typeId, f1,
// f2, ...) which the ctor copies into the blob. We log the type id +
// the first 10 dword args ONLY for the questbook family (0x1b0..0x1d0)
// so we can replay e.g. the 0x1bd "quest solved" banner+chime for 9512.
static volatile long g_qsolved_logged = 0;
constexpr long QSOLVED_LOG_MAX = 24;     // a few quests' worth of events

extern "C" void __cdecl capture_qb_event(uintptr_t evt_obj,
                                         const uint32_t* args) {
    if (g_qsolved_logged >= QSOLVED_LOG_MAX) return;
    __try {
        uint32_t type_id = args ? args[0] : 0;
        // Only the questbook event family is interesting here.
        if (type_id < 0x1b0 || type_id > 0x1d0) return;
        InterlockedIncrement(&g_qsolved_logged);
        sdk_log("[qbevt] FUN_00423ac0 type=0x%x evt_obj=%p  args: "
                "%08x %08x %08x %08x %08x %08x %08x %08x %08x",
                type_id, (void*)evt_obj,
                args[1], args[2], args[3], args[4], args[5],
                args[6], args[7], args[8], args[9]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[qbevt] capture faulted");
    }
}

// __thiscall hook: ECX = event obj, original stack args start at
// [esp+4]. After pushfd/edx/ecx (12 bytes) they start at [esp+16].
// Pass (evt_obj=ecx, args=&[esp+16]) to the cdecl capture.
__declspec(naked) static void __cdecl hook_quest_solved() {
    __asm {
        pushfd
        push edx
        push ecx
        lea  eax, [esp+16]            ; &args[0] (orig [esp+4])
        push eax
        push ecx                      ; evt_obj (ECX)
        call capture_qb_event
        add  esp, 8
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_qsolved]
    }
}

// --- Engine teleport hijack (FUN_0054d9d0) -------------------------------
// One-shot destination override for the campaign new-game hero start.
// (struct TpOverride + extern decls are up near l_sacred_set_spawn.)
TpOverride    g_tp_ov  = { false, 0, 0, false, 0, 0 };
volatile long g_tp_log = 0;
constexpr long TP_LOG_MAX = 24;

// cdecl helper. `args` points at the on-stack [x, y, level, flag] of the
// FUN_0054d9d0 call (writable — edits land before the original runs).
extern "C" void __cdecl engine_tp_filter(uintptr_t this_, int* args) {
    __try {
        int x = args[0], y = args[1], lvl = args[2], flag = args[3];
        uintptr_t exe = (uintptr_t)g_attach.exe_module;
        if (!exe) return;
        uintptr_t reb = exe - 0x00400000;
        uintptr_t om = *(uintptr_t*)(reb + 0x00AD5C40);
        uintptr_t cx = *(uintptr_t*)(reb + 0x0182EBE8);
        uintptr_t hero = 0;
        if (om && cx) {
            uint32_t idx = *(uint32_t*)(cx + 0x14);
            uintptr_t arr = *(uintptr_t*)(om + 4);
            if (idx && idx < 0x10000 && arr)
                hero = *(uintptr_t*)(arr + (uintptr_t)idx * 4);
        }
        bool is_hero = (hero != 0 && this_ == hero);
        if (g_tp_log < TP_LOG_MAX) {
            InterlockedIncrement(&g_tp_log);
            sdk_log("[tp] FUN_0054d9d0 this=%p hero=%p%s xy=(%d,%d) "
                    "lvl=%d flag=%d", (void*)this_, (void*)hero,
                    is_hero ? " <HERO>" : "", x, y, lvl, flag);
        }
        if (g_tp_ov.armed && is_hero) {
            // until_ms == 0  → ONE-SHOT: rewrite the first hero teleport
            //                  then disarm (the proven campaign behaviour;
            //                  keeps the engine's own level unless lvl_set).
            // until_ms  > 0  → rewrite EVERY hero teleport until that tick
            //                  (NetScript's spawn is a multi-step sequence).
            bool oneshot = (g_tp_ov.until_ms == 0);
            bool expired = !oneshot && GetTickCount() >= g_tp_ov.until_ms;
            if (expired) {
                g_tp_ov.armed = false;
            } else {
                int nl = g_tp_ov.lvl_set ? g_tp_ov.lvl : lvl;
                sdk_log("[tp] HERO tp HIJACK: (%d,%d,lvl%d) -> (%d,%d,lvl%d)%s",
                        x, y, lvl, g_tp_ov.kx, g_tp_ov.ky, nl,
                        oneshot ? " [one-shot]" : "");
                args[0] = g_tp_ov.kx;
                args[1] = g_tp_ov.ky;
                if (g_tp_ov.lvl_set) args[2] = g_tp_ov.lvl;
                if (oneshot) g_tp_ov.armed = false;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// __thiscall hook: ECX = cCreature*, original stack [esp+4..]=x,y,lvl,flag.
// After pushfd/edx/ecx (12 B) the original [esp+4] sits at [esp+16]; pass
// (this=ecx, args=&[esp+16]) so the helper can read AND rewrite x/y.
__declspec(naked) static void __cdecl hook_engine_tp() {
    __asm {
        pushfd
        push edx
        push ecx
        lea  eax, [esp+16]            ; &x  (orig [esp+4])
        push eax                      ; arg2 = int* args
        push ecx                      ; arg1 = this (cCreature*)
        call engine_tp_filter
        add  esp, 8
        pop  ecx
        pop  edx
        popfd
        jmp  dword ptr [g_tramp_engine_tp]
    }
}

// Runs at the TOP of the journal builder FUN_006b07e0, before its loop
// reads entry+0x24. Zero the render gate (+0x24) and map marker
// (+0x10/+0x14) for every registry entry whose quest_id is NOT SDK-owned
// → vanilla quests never get a journal row, while their script logic and
// the new-game intro stay fully intact. Idempotent, race-free.
extern "C" void __cdecl journal_suppress() {
    if (!g_hide_vanilla) return;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        uintptr_t end   = *(uintptr_t*)QB_REGISTRY_END_VA;
        if (!begin || end <= begin) return;
        int n = (int)((end - begin) / QB_ENTRY_STRIDE);
        for (int i = 0; i < n; i++) {
            uintptr_t e   = begin + (uintptr_t)i * QB_ENTRY_STRIDE;
            uint32_t  qid = *(uint32_t*)(e + QB_ENTRY_OFF_QID);
            if (qid == 0 || sdk_qid_has(qid)) continue;   // keep SDK quests
            *(uint32_t*)(e + 0x24) = 0;                    // journal gate off
            *(uint32_t*)(e + 0x10) = 0;                    // kompass marker
            *(uint32_t*)(e + 0x14) = 0;
            // NOTE: do NOT touch +0x04 — state=100 renders the quest
            // GREEN/"completed" on the map (worse). Marker stays an
            // open item for tactic A (registry-source block) later.
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Runs at the TOP of the marker resolver FUN_004a5980. For every NON-SDK
// quest: zero entry+0x10/+0x14 (marker coords) AND set any per-class
// marker slot (cQuestMgr+0x3a0/+0x3a4 + C*8, C=0..16) that points at it
// to -1 — the decomp treats slot<0 as "no marker, return 0". Race-free
// (top of the resolver, every refresh). SDK quests keep their markers.
extern "C" void __cdecl marker_suppress() {
    if (!g_hide_vanilla) return;
    __try {
        uintptr_t begin = *(uintptr_t*)QB_REGISTRY_BEGIN_VA;
        uintptr_t end   = *(uintptr_t*)QB_REGISTRY_END_VA;
        if (!begin || end <= begin) return;
        uint32_t n = (uint32_t)((end - begin) / QB_ENTRY_STRIDE);
        for (uint32_t i = 0; i < n; i++) {
            uintptr_t e   = begin + (uintptr_t)i * QB_ENTRY_STRIDE;
            uint32_t  qid = *(uint32_t*)(e + QB_ENTRY_OFF_QID);
            if (qid == 0 || sdk_qid_has(qid)) continue;
            *(uint32_t*)(e + 0x10) = 0;
            *(uint32_t*)(e + 0x14) = 0;
        }
        // Per-class marker index slots: cQuestMgr+0x3a0 (secondary) and
        // +0x3a4 (primary), stride 8, C=0..16. Null any that target a
        // non-SDK entry.
        uintptr_t mgr = g_quest_mgr;
        if (mgr) {
            static const uintptr_t MK_OFF[2] = { 0x3a0, 0x3a4 };
            for (int c = 0; c <= 16; c++) {
                for (int k = 0; k < 2; k++) {
                    int32_t* slot =
                        (int32_t*)(mgr + MK_OFF[k] + (uintptr_t)c * 8);
                    int32_t idx = *slot;
                    if (idx < 0 || (uint32_t)idx >= n) continue;
                    uintptr_t e = begin + (uintptr_t)idx * QB_ENTRY_STRIDE;
                    uint32_t qid = *(uint32_t*)(e + QB_ENTRY_OFF_QID);
                    if (qid != 0 && !sdk_qid_has(qid)) *slot = -1;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(naked) static void __cdecl hook_marker_resolve() {
    __asm {
        pushfd
        push eax
        push ecx
        push edx
        call marker_suppress
        pop  edx
        pop  ecx
        pop  eax
        popfd
        jmp  dword ptr [g_tramp_marker]
    }
}

// __thiscall builder (ECX = journal UI obj). Preserve all regs/flags;
// our helper is cdecl no-arg and only touches the global registry.
__declspec(naked) static void __cdecl hook_journal_build() {
    __asm {
        pushfd
        push eax
        push ecx
        push edx
        call journal_suppress
        pop  edx
        pop  ecx
        pop  eax
        popfd
        jmp  dword ptr [g_tramp_journal]
    }
}

// FUN_0080e780 hook. __cdecl(const char* name) → returns hash in EAX.
//   [esp+0] = return address (pushed by CALL)
//   [esp+4] = const char* name
// We save flags + EAX + ECX + EDX (4 dwords = 16 bytes) before pushing
// the arg, so original [esp+4] is now at [esp+4+16] = [esp+20].
__declspec(naked) static void __cdecl hook_sacred_hash() {
    __asm {
        pushfd
        push eax
        push ecx
        push edx
        push dword ptr [esp+20]       ; arg = original [esp+4] = name pointer
        call read_name_hash
        add  esp, 4
        pop  edx
        pop  ecx
        pop  eax
        popfd
        jmp  dword ptr [g_tramp_hash]
    }
}

static volatile bool g_hooks_installed = false;

// Thin wrapper over the unified installer (hooks/detour.cpp). Every
// runtime_triggers engine hook shares a 7-byte prologue (PROLOGUE_BYTES);
// validate the first 3 bytes against the expected signature, then
// trampoline-detour to the thunk. (Was build_trampoline+patch_jump+this —
// now one shared implementation; emitted bytes unchanged. Goal A2.)
static bool install_hook_sig(uintptr_t target_va, uint8_t** tramp_out,
                             void* thunk, uint8_t e0, uint8_t e1, uint8_t e2) {
    const uint8_t sig[3] = { e0, e1, e2 };
    return hooks::install_trampoline(target_va, PROLOGUE_BYTES, thunk,
                                     tramp_out, sig, 3, "runtime_triggers");
}

// The 6 SEH-prologue hooks: `6A FF 68` (push -1; push <seh>).
static bool install_hook(uintptr_t target_va, uint8_t** tramp_out, void* thunk) {
    return install_hook_sig(target_va, tramp_out, thunk, 0x6A, 0xFF, 0x68);
}

static void install_trampolines() {
    if (g_hooks_installed) return;
    if (!g_attach.exe_module) {
        sdk_log("[runtime_triggers] no exe module — abort");
        return;
    }
    if (!g_ring_cs_init) {
        InitializeCriticalSection(&g_ring_cs);
        g_ring_cs_init = true;
    }
    uintptr_t base = (uintptr_t)g_attach.exe_module;

    bool ok1 = install_hook(base + SELF_TRIGGER_QUEST_RVA,
                            &g_tramp_self_trigger,
                            (void*)&hook_self_trigger_quest);
    bool ok2 = install_hook(base + DIALOG_CHECK_RVA,
                            &g_tramp_dialog_check,
                            (void*)&hook_dialog_check);
    bool ok3 = install_hook(base + FIRE_FUNNEL_RVA,
                            &g_tramp_funnel,
                            (void*)&hook_funnel);
    bool ok4 = install_hook(base + SACRED_HASH_RVA,
                            &g_tramp_hash,
                            (void*)&hook_sacred_hash);
    bool ok5 = install_hook(base + QUEST_WALKER_RVA,
                            &g_tramp_walker,
                            (void*)&hook_quest_walker);
    bool ok6 = install_hook(base + QB_EVENT_CTOR_RVA,
                            &g_tramp_qsolved,
                            (void*)&hook_quest_solved);
    // FUN_0054d9d0 prologue is `81 EC 9C 00 00 00 53` (sub esp,0x9c;
    // push ebx) — NOT the 6A FF 68 SEH shape, so use the sig variant.
    bool ok7 = install_hook_sig(base + ENGINE_TP_RVA,
                                &g_tramp_engine_tp,
                                (void*)&hook_engine_tp,
                                0x81, 0xEC, 0x9C);
    // FUN_006b07e0 prologue `81 EC DC 00 00 00 53`.
    bool ok8 = install_hook_sig(base + JOURNAL_BUILD_RVA,
                                &g_tramp_journal,
                                (void*)&hook_journal_build,
                                0x81, 0xEC, 0xDC);
    // FUN_004a5980 prologue `83 EC 38 8B 44 24 48`.
    bool ok9 = install_hook_sig(base + MARKER_RESOLVE_RVA,
                                &g_tramp_marker,
                                (void*)&hook_marker_resolve,
                                0x83, 0xEC, 0x38);
    // FUN_0048FF10 dialog-answer (tag-0x76), `6A FF 68` SEH prologue.
    bool ok10 = install_hook(base + DIALOG_ANSWER_RVA,
                             &g_tramp_dlg_answer,
                             (void*)&hook_dlg_answer);
    // FUN_00461540 conv-window pump, `6A FF 68` SEH prologue. READ-ONLY
    // probe (no writes) — settles whether FIX-A2 NPCs drive this fn.
    bool ok11 = install_hook(base + DIALOG_PUMP_RVA,
                             &g_tramp_dlg_pump,
                             (void*)&hook_dlg_pump);
    // FUN_0056B130 per-creature dialog driver, `6A FF 68` SEH prologue.
    // READ-ONLY probe (the one non-refuted lead).
    bool ok12 = install_hook(base + DIALOG_DRV_RVA,
                             &g_tramp_dlg_drv,
                             (void*)&hook_dlg_drv);
    // FUN_00465220 dialog-content setter, prologue `56 8B 74` (push esi; mov
    // esi,[esp+8]). READ-ONLY probe of the resolved content-handle format.
    bool ok13 = install_hook_sig(base + DIALOG_CONTENT_RVA,
                                 &g_tramp_dlg_content,
                                 (void*)&hook_dlg_content,
                                 0x56, 0x8B, 0x74);
    g_hooks_installed = ok1 || ok2 || ok3 || ok4 || ok5 || ok6 || ok7 ||
                        ok8 || ok9 || ok10 || ok11 || ok12 || ok13;
    if (g_hooks_installed) {
        set_status("hooks installed (self-trigger=%d, dialog-check=%d, "
                   "funnel=%d, hash=%d, walker=%d, qsolved=%d, engine_tp=%d, "
                   "journal=%d, marker=%d, dlg_answer=%d, dlg_pump=%d, "
                   "dlg_drv=%d)",
                   ok1, ok2, ok3, ok4, ok5, ok6, ok7, ok8, ok9, ok10,
                   ok11, ok12);
    } else {
        set_status("hook install failed on all targets");
    }
}

// =========================================================================
// State adoption from bake
// =========================================================================

void take_state(::lua_State* L) {
    if (!L) return;
    g_L = L;
    g_ready = true;
    // Install trampolines unconditionally — even with zero registered
    // handlers we still want the diagnostic counters & last-seen ring to
    // populate so the modder can discover real trigger names. fire() is a
    // cheap no-op when no handler matches.
    install_trampolines();
}

// Public retry entry — see the header comment. install_trampolines() is
// idempotent (g_hooks_installed is set true only on success, so a pre-decrypt
// all-abort leaves it false and this re-tries). Called from the dump_text
// decrypt gate so the hooks install regardless of the bake/decrypt race.
void ensure_hooks_installed() {
    install_trampolines();
}

}} // namespace sdk::runtime_triggers
