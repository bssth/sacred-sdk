// sdk_vars.inc — included by runtime_triggers.cpp, inside namespace
// sdk::runtime_triggers, after sdk_sections.inc (it reuses kOpNew, sec_call_is
// and sec_call_write). The engine's script VARIABLES, and two savegame hooks so
// Lua can rebuild SDK quest state after a load (SDK_GAPS gaps 13 and 1).
//
// The variable table (.claude/knowledge/quests/RE_conditions_vars.md §2) is a
// std::vector at qm+0x7550 = 0x00AB44D0 (begin), +4 end, +8 capacity, of 0x24-byte
// entries: char name[0x20], then an i32 value at +0x20. The engine looks a name
// up with a linear _stricmp plus an equal-length test, so names are
// case-insensitive exact matches. Every vanilla reader uses this table (the IsVar*
// predicates, INT-variable and symref operands, +VAR() interpolation,
// GetPoolPosition), and the savegame serializer FUN_00465690 writes it out and
// reads it back. The older sacred.state_get / state_set read another table: the
// named positions at qm+0x334.
//
// SetVar (FUN_0049b2b0) updates an entry in place or appends one. FUN_0045ee20,
// which it calls after either, is an empty stub. Its only other effect is net
// event 0x1c7 / 0x1c6 in multiplayer, which the SDK does not send, so co-op peers
// do not see SDK writes.
//
// Savegames: cEngine_save (0x61af02) and cEngine_load (0x61c1ac) both call
// FUN_00465690 (thiscall on qm, two stack args, ret 8) with a direct E8. Both
// calls are pointed at thunks that count the call and tail-jump to the original.
// After a load the heartbeat fires the Lua trigger "SDK:SAVE_LOADED".
// on_world_load is not enough for that: it fires only when the journal holds
// none of the SDK's quests, and a savegame may already have restored them.

constexpr uintptr_t VARS_BEGIN    = 0x00AB44D0;       // qm+0x7550
constexpr uintptr_t VARS_END      = 0x00AB44D4;
constexpr uintptr_t VARS_CAP      = 0x00AB44D8;
constexpr uintptr_t VAR_STRIDE    = 0x24;
constexpr uintptr_t VAR_NAME_LEN  = 0x20;             // char name[0x20], NUL included
constexpr uintptr_t VAR_OFF_VALUE = 0x20;
constexpr size_t    VAR_NAME_MAX  = VAR_NAME_LEN - 1;
constexpr uintptr_t MP_FLAG_VA    = 0x0182EBEC;       // DAT_0182ebec: multiplayer session

struct VarTab { uintptr_t b, e, c; unsigned n; };
struct VarRow { char name[VAR_NAME_LEN]; int32_t v; };

// Snapshot the vector. False if it does not look like one; a vector that never
// allocated is {0, 0, 0} and counts as empty.
static bool var_tab(VarTab* t) {
    __try {
        t->b = *(volatile uintptr_t*)VARS_BEGIN;
        t->e = *(volatile uintptr_t*)VARS_END;
        t->c = *(volatile uintptr_t*)VARS_CAP;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!t->b) { t->n = 0; return !t->e && !t->c; }
    if (t->e < t->b || t->c < t->e) return false;
    if ((t->e - t->b) % VAR_STRIDE || (t->c - t->b) % VAR_STRIDE) return false;
    t->n = (unsigned)((t->e - t->b) / VAR_STRIDE);
    return t->n <= 200000;
}

static unsigned var_count() {
    VarTab t;
    return var_tab(&t) ? t.n : 0;
}

// The engine's lookup: case-insensitive, equal length. Index or -1.
static int var_find(const VarTab& t, const char* name) {
    size_t len = strlen(name);
    __try {
        for (unsigned i = 0; i < t.n; i++) {
            const char* en = (const char*)(t.b + i * VAR_STRIDE);
            if (strnlen(en, VAR_NAME_LEN) == len && _strnicmp(en, name, len) == 0) return (int)i;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static bool var_value(const char* name, int32_t* out) {
    VarTab t;
    if (!var_tab(&t)) return false;
    int i = var_find(t, name);
    if (i < 0) return false;
    __try { *out = *(const int32_t*)(t.b + i * VAR_STRIDE + VAR_OFF_VALUE); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Update in place, or append the way SetVar does. When the vector is full it is
// grown into a block from the engine's own operator new, so the engine's vector
// code can grow or free it later. The old block is left allocated: a script
// handler further up the stack may still hold a pointer into it.
static bool var_write(const char* name, int32_t v, bool* appended) {
    VarTab t;
    if (!var_tab(&t)) return false;
    int i = var_find(t, name);
    *appended = (i < 0);
    uint8_t rec[VAR_STRIDE];
    memset(rec, 0, sizeof rec);
    memcpy(rec, name, strlen(name));
    *(int32_t*)(rec + VAR_OFF_VALUE) = v;
    __try {
        if (i >= 0) {
            *(int32_t*)(t.b + i * VAR_STRIDE + VAR_OFF_VALUE) = v;
        } else if (t.b && t.e < t.c) {
            memcpy((void*)t.e, rec, VAR_STRIDE);
            *(volatile uintptr_t*)VARS_END = t.e + VAR_STRIDE;
        } else {
            unsigned cap = t.n + (t.n / 2 > 16 ? t.n / 2 : 16);
            uint8_t* blk = (uint8_t*)kOpNew((size_t)cap * VAR_STRIDE);
            if (!blk) return false;
            if (t.n) memcpy(blk, (const void*)t.b, (size_t)t.n * VAR_STRIDE);
            memcpy(blk + (size_t)t.n * VAR_STRIDE, rec, VAR_STRIDE);
            *(volatile uintptr_t*)VARS_BEGIN = (uintptr_t)blk;
            *(volatile uintptr_t*)VARS_END   = (uintptr_t)(blk + (size_t)(t.n + 1) * VAR_STRIDE);
            *(volatile uintptr_t*)VARS_CAP   = (uintptr_t)(blk + (size_t)cap * VAR_STRIDE);
            sdk_log("[vars] table grown: %u entries, capacity %u", t.n + 1, cap);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Copy the variables whose name starts with `prefix` (case-insensitive; null =
// all) into `out`. Returns the count, or -1 if the table is unusable.
static int var_snapshot(VarRow* out, int max, const char* prefix) {
    VarTab t;
    if (!var_tab(&t)) return -1;
    size_t pl = prefix ? strlen(prefix) : 0;
    int k = 0;
    __try {
        for (unsigned i = 0; i < t.n && k < max; i++) {
            const char* en = (const char*)(t.b + i * VAR_STRIDE);
            size_t len = strnlen(en, VAR_NAME_LEN);
            if (len == 0 || len == VAR_NAME_LEN) continue;
            if (pl && (len < pl || _strnicmp(en, prefix, pl) != 0)) continue;
            memcpy(out[k].name, en, len);
            out[k].name[len] = 0;
            out[k].v = *(const int32_t*)(en + VAR_OFF_VALUE);
            k++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return k;
}

static bool g_var_warn_world = false, g_var_warn_mp = false;

// Every SDK write goes through here: needs a loaded world, logs each change.
static bool var_store(const char* who, const char* name, int32_t v) {
    if (!g_quest_mgr) {
        if (!g_var_warn_world) {
            g_var_warn_world = true;
            sdk_log("[vars] %s('%s') refused: no world loaded yet", who, name);
        }
        return false;
    }
    int32_t old = 0;
    bool had = var_value(name, &old);
    if (had && old == v) return true;
    bool appended = false;
    if (!var_write(name, v, &appended)) {
        sdk_log("[vars] %s('%s', %d) failed: the variable table is not usable", who, name, v);
        return false;
    }
    if (appended) sdk_log("[vars] + %s = %d (new; %u variables)", name, v, var_count());
    else          sdk_log("[vars] %s = %d (was %d)", name, v, old);
    if (!g_var_warn_mp && *(volatile uint8_t*)MP_FLAG_VA) {
        g_var_warn_mp = true;
        sdk_log("[vars] multiplayer session: SDK variable writes stay on this machine");
    }
    return true;
}

static const char* var_name_arg(lua_State* L, int i) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, i, &n);
    if (n < 1 || n > VAR_NAME_MAX)
        luaL_error(L, "variable name '%s' must be 1..%d characters", s, (int)VAR_NAME_MAX);
    if (_stricmp(s, "HeroQBit") == 0)
        luaL_error(L, "'HeroQBit' is the hero's quest-bit field, not a variable");
    return s;
}

static int32_t var_int_arg(lua_State* L, int i) {
    lua_Integer v = luaL_checkinteger(L, i);
    if (v < -2147483647LL - 1 || v > 2147483647LL)
        luaL_error(L, "variable value %lld is outside int32", (long long)v);
    return (int32_t)v;
}

// sacred.var_get(name) -> integer | nil          (ctx:get_var: base 2)
static int var_lua_get(lua_State* L, int base) {
    const char* name = var_name_arg(L, base);
    int32_t v = 0;
    if (var_value(name, &v)) lua_pushinteger(L, v);
    else                     lua_pushnil(L);
    return 1;
}

// sacred.var_set(name, value) -> true | false    (ctx:set_var: base 2)
// Creates a missing variable, like SetVar.
static int var_lua_set(lua_State* L, int base) {
    const char* name = var_name_arg(L, base);
    int32_t v = var_int_arg(L, base + 1);
    lua_pushboolean(L, var_store("var_set", name, v) ? 1 : 0);
    return 1;
}

static int l_sacred_var_get(lua_State* L) { return var_lua_get(L, 1); }
static int l_sacred_var_set(lua_State* L) { return var_lua_set(L, 1); }

// sacred.var_inc(name[, n = 1]) -> the value afterwards | nil. Vanilla IncVar: a
// sum below the old value (negative n, overflow) writes nothing, and a missing
// variable is created with n.
static int l_sacred_var_inc(lua_State* L) {
    const char* name = var_name_arg(L, 1);
    lua_Integer n = luaL_optinteger(L, 2, 1);
    int32_t cur = 0;
    long long nv;
    if (!var_value(name, &cur)) nv = n;
    else {
        nv = (long long)cur + n;
        if (nv < cur || nv > 2147483647LL) nv = cur;
    }
    if (nv < -2147483647LL - 1 || nv > 2147483647LL)
        return luaL_error(L, "var_inc: %lld is outside int32", nv);
    if (!var_store("var_inc", name, (int32_t)nv)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)nv);
    return 1;
}

// sacred.var_dec(name[, n = 1]) -> the value afterwards | nil. Vanilla DecVar:
// max(0, value - n), and a missing variable is created with n (the engine's own
// rule, kept so SDK and vanilla counters behave alike).
static int l_sacred_var_dec(lua_State* L) {
    const char* name = var_name_arg(L, 1);
    lua_Integer n = luaL_optinteger(L, 2, 1);
    int32_t cur = 0;
    long long nv;
    if (!var_value(name, &cur)) nv = n;
    else {
        nv = (long long)cur - n;
        if (nv < 0) nv = 0;
        if (nv > 2147483647LL) nv = 2147483647LL;
    }
    if (nv < -2147483647LL - 1 || nv > 2147483647LL)
        return luaL_error(L, "var_dec: %lld is outside int32", nv);
    if (!var_store("var_dec", name, (int32_t)nv)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)nv);
    return 1;
}

// sacred.vars([prefix]) -> { name = value, ... }   (prefix is case-insensitive)
static int l_sacred_vars(lua_State* L) {
    const char* prefix = luaL_optstring(L, 1, nullptr);
    unsigned n = var_count();
    VarRow* rows = n ? (VarRow*)HeapAlloc(GetProcessHeap(), 0, n * sizeof(VarRow)) : nullptr;
    int k = rows ? var_snapshot(rows, (int)n, prefix) : 0;
    lua_createtable(L, 0, k > 0 ? k : 0);
    for (int i = 0; i < k; i++) {
        lua_pushinteger(L, rows[i].v);
        lua_setfield(L, -2, rows[i].name);
    }
    if (rows) HeapFree(GetProcessHeap(), 0, rows);
    return 1;
}

// sacred.var_dump([prefix]) -> count, with one log line per variable.
static int l_sacred_var_dump(lua_State* L) {
    const char* prefix = luaL_optstring(L, 1, nullptr);
    unsigned n = var_count();
    VarRow* rows = n ? (VarRow*)HeapAlloc(GetProcessHeap(), 0, n * sizeof(VarRow)) : nullptr;
    int k = rows ? var_snapshot(rows, (int)n, prefix) : 0;
    sdk_log("[vars] === %d of %u variables%s%s ===", k, n,
            prefix ? " starting with " : "", prefix ? prefix : "");
    for (int i = 0; i < k; i++) sdk_log("[vars]   %-31s = %d", rows[i].name, rows[i].v);
    if (rows) HeapFree(GetProcessHeap(), 0, rows);
    lua_pushinteger(L, k);
    return 1;
}

// ---- savegame hooks ------------------------------------------------------

static volatile LONG g_qs_loads = 0, g_qs_saves = 0;  // bumped by the thunks
static LONG          g_qs_loads_seen = 0, g_qs_saves_seen = 0;
static LONG          g_tp_arm_loads = 0;              // g_qs_loads when the spawn hijack was armed
static uintptr_t     g_qs_target = 0x00465690;        // FUN_00465690, the quest-state serializer
static bool          g_qs_patched = false, g_qs_warned = false;

__declspec(naked) static void qs_load_thunk() {
    __asm {
        lock inc dword ptr [g_qs_loads]
        jmp  dword ptr [g_qs_target]
    }
}

__declspec(naked) static void qs_save_thunk() {
    __asm {
        lock inc dword ptr [g_qs_saves]
        jmp  dword ptr [g_qs_target]
    }
}

// ---- world builds ------------------------------------------------------------
// FUN_0046f9b0 (thiscall on qm, two stack args, ret 8) loads a world's scripts:
// Vectoren.bin, the FunkCode.bin the section table points into, the quest
// registry. It runs once for every world the engine builds, from two direct E8
// calls:
//   0x0060c2dc  in FUN_0060b29f                 a new game
//   0x004676b7  in FUN_00465690 (the serializer)  a savegame load
// Both calls go through counting thunks, and sacred.world_serial() returns the
// count, so Lua knows the moment a new world is being built. on_world_load
// cannot tell: it keys on the journal vector moving, which happened in a running
// world and not at all on a second new game (log of 2026-09-11). A new-game
// build also arms the start-placement hijack when sacred.set_new_game_spawn(kx,
// ky) is set; a savegame load never does.

static volatile LONG g_world_builds = 0, g_newgame_builds = 0;   // bumped by the thunks
static LONG          g_world_builds_seen = 0, g_newgame_builds_seen = 0;
static uintptr_t     g_ld_target = 0x0046F9B0;                   // FUN_0046f9b0
static bool          g_ld_patched = false, g_ld_warned = false;
static bool          g_ng_spawn_on = false;
static int           g_ng_spawn_kx = 0, g_ng_spawn_ky = 0;

// Runs inside the engine's new-game world build, before the start placement.
extern "C" void __cdecl sdk_newgame_build() {
    if (!g_ng_spawn_on) return;
    g_tp_ov.kx       = g_ng_spawn_kx;
    g_tp_ov.ky       = g_ng_spawn_ky;
    g_tp_ov.lvl_set  = false;              // keep the engine's own level
    g_tp_ov.until_ms = 0;                  // one-shot
    g_tp_ov.armed    = true;
    g_tp_log         = 0;
    g_tp_arm_loads   = g_qs_loads;
    sdk_log("[tp] new game: start-placement hijack armed -> (%d,%d)", g_ng_spawn_kx, g_ng_spawn_ky);
}

__declspec(naked) static void ld_newgame_thunk() {
    __asm {
        lock inc dword ptr [g_world_builds]
        lock inc dword ptr [g_newgame_builds]
        pushfd
        pushad
        call sdk_newgame_build
        popad
        popfd
        jmp  dword ptr [g_ld_target]
    }
}

__declspec(naked) static void ld_load_thunk() {
    __asm {
        lock inc dword ptr [g_world_builds]
        jmp  dword ptr [g_ld_target]
    }
}

// sacred.world_serial() -> integer: worlds the engine has built in this process
// (new games and savegame loads). A change means the world your handles and
// flags belong to is gone.
static int l_sacred_world_serial(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_world_builds);
    return 1;
}

// Sections the engine itself has loaded, the SDK's appended ones excluded. The
// table is emptied when a world is torn down (quit to the menu) and filled by
// the loader when the next one is built. In between the new game's hero already
// exists (log of 2026-09-11: the same hero pointer before and after the build),
// so hero presence cannot tell that limbo apart; this can.
static unsigned world_sections() {
    uintptr_t b = 0, e = 0;
    __try {
        b = *(volatile uintptr_t*)0x00AAB708;
        e = *(volatile uintptr_t*)0x00AAB70C;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!b || e < b) return 0;
    unsigned n = (unsigned)((e - b) / 0x54);
    if (b == g_sec_block && n >= (unsigned)g_sec_injected) n -= (unsigned)g_sec_injected;
    return n;
}

// sacred.world_alive() -> bool: a world's scripts are loaded. False in the
// menus and between a world's teardown and the next world build.
static int l_sacred_world_alive(lua_State* L) {
    lua_pushboolean(L, world_sections() >= 100 ? 1 : 0);
    return 1;
}

// sacred.set_new_game_spawn(kx, ky) -> true: every new game puts the hero at
// (kx, ky) (KompassPos) instead of the campaign start; savegame loads are never
// moved. sacred.set_new_game_spawn() switches it off.
static int l_sacred_set_new_game_spawn(lua_State* L) {
    if (lua_isnoneornil(L, 1)) {
        g_ng_spawn_on = false;
    } else {
        g_ng_spawn_kx = (int)luaL_checkinteger(L, 1);
        g_ng_spawn_ky = (int)luaL_checkinteger(L, 2);
        g_ng_spawn_on = true;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// Called from heartbeat(), outside the Lua guard, on the game thread.
static void vars_tick() {
    HMODULE exe = g_attach.exe_module;
    bool exe_ok = exe && (uintptr_t)exe == 0x00400000;
    if (exe_ok && !g_qs_patched && !g_qs_warned) {
        bool ld = sec_call_is(0x0061C1AC, 0x00465690);
        bool sv = sec_call_is(0x0061AF02, 0x00465690);
        if (ld && sv) {                                              // both or neither
            sec_call_write(0x0061C1AC, (void*)&qs_load_thunk);
            sec_call_write(0x0061AF02, (void*)&qs_save_thunk);
            g_qs_patched = true;
            sdk_log("[vars] savegame hooks live (quest-state serializer: load 0x61c1ac, save 0x61af02)");
        } else {
            g_qs_warned = true;
            sdk_log("[vars] savegame call sites not as expected (load %d, save %d): "
                    "no SDK:SAVE_LOADED", ld ? 1 : 0, sv ? 1 : 0);
        }
    }
    if (exe_ok && !g_ld_patched && !g_ld_warned) {
        bool ng = sec_call_is(0x0060C2DC, 0x0046F9B0);
        bool ld = sec_call_is(0x004676B7, 0x0046F9B0);
        if (ng && ld) {                                              // both or neither
            sec_call_write(0x0060C2DC, (void*)&ld_newgame_thunk);
            sec_call_write(0x004676B7, (void*)&ld_load_thunk);
            g_ld_patched = true;
            sdk_log("[vars] world-build hooks live (script loader: new game 0x60c2dc, savegame 0x4676b7)");
        } else {
            g_ld_warned = true;
            sdk_log("[vars] world-build call sites not as expected (new game %d, savegame %d): "
                    "world_serial stays 0, no new-game spawn", ng ? 1 : 0, ld ? 1 : 0);
        }
    }
    LONG wb = g_world_builds;
    if (wb != g_world_builds_seen) {
        LONG ngb = g_newgame_builds;
        sdk_log("[vars] world build #%ld: %s", wb,
                ngb != g_newgame_builds_seen ? "a new game" : "a savegame load");
        g_world_builds_seen   = wb;
        g_newgame_builds_seen = ngb;
    }
    LONG s = g_qs_saves;
    if (s != g_qs_saves_seen) {
        g_qs_saves_seen = s;
        sdk_log("[vars] game saved (quest state with %u variables)", var_count());
    }
    LONG l = g_qs_loads;
    if (l != g_qs_loads_seen) {
        g_qs_loads_seen = l;
        sdk_log("[vars] savegame loaded (quest state with %u variables) -> SDK:SAVE_LOADED",
                var_count());
        fire("SDK:SAVE_LOADED");
    }
}
