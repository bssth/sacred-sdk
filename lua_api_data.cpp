// lua_api_data.cpp — self-contained sacred.* data / engine-introspection Lua
// bindings, split out of the runtime_triggers god-file (refactor A5).
//
// These bindings have ZERO coupling to the trigger/quest/NPC runtime machinery:
// they only read the compile-time constexpr port tables (ports/data/*, the
// sacred_hash port) or call the engine resolver (sdk/engine_resolve.cpp) and the
// PAX hero-save reader (sdk/hero_save_probe.cpp). Keeping them here keeps
// runtime_triggers.cpp focused on the actual trigger/dispatch engine.
//
// Registered via install_data_api(L), called from runtime_triggers' install_lua_api.

#include "sdk.h"
#include "ports/data/creature_types.h"     // sacred.creature_name
#include "ports/data/combat_arts.h"        // sacred.combat_art
#include "ports/data/companions.h"         // sacred.companions
#include "ports/data/hero_tables.h"        // sacred.xp_for_level / skill_name / class_skills / survival_bonus
#include "ports/data/weapon_bonus_names.h" // sacred.bonus_name
#include "ports/engine/sacred_hash.h"      // sacred.hash
#include "engine/mem.h"                 // sacred.peek_u32 / nearby_list / npc_ai
#include "engine/singletons.h"          // hero resolve for npc_hire / hero_party
#include <vector>                        // sacred.model_slots queue
#include <string>
#include "engine/build_profile.h"         // sacred.patch_u32 gate

extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
}

namespace sdk { namespace runtime_triggers {

// sacred.read_save(path) -> { class_id, class_name, level, gold, xp, underworld }
// or nil. Reads a hero .pax via the wired PAX port (hero_save_probe.cpp).
static int l_sacred_read_save(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    HeroSaveInfo info;
    if (!read_hero_save(path, info) || !info.ok) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)info.class_id);   lua_setfield(L, -2, "class_id");
    lua_pushstring (L, info.class_name);              lua_setfield(L, -2, "class_name");
    lua_pushinteger(L, (lua_Integer)info.level);      lua_setfield(L, -2, "level");
    lua_pushinteger(L, (lua_Integer)info.gold);       lua_setfield(L, -2, "gold");
    lua_pushinteger(L, (lua_Integer)info.xp);         lua_setfield(L, -2, "xp");
    lua_pushinteger(L, (lua_Integer)info.underworld); lua_setfield(L, -2, "underworld");
    return 1;
}

// ---- data-table bindings (zero-dependency constexpr ports) ----------------

// sacred.creature_name(id) -> name[, note, band] | nil
// `id` is the real creature type id ('ID Dec', 1..714). Accepts the byte-
// swapped 'ID Hex' form too: if the direct id misses, retry via byteswap16.
static int l_sacred_creature_name(lua_State* L) {
    unsigned id = (unsigned)luaL_checkinteger(L, 1);
    const sacred::data::CreatureType* c = sacred::data::find_by_id((uint16_t)id);
    if (!c) c = sacred::data::find_by_hex((uint16_t)id);   // tolerate ID Hex
    if (!c) { lua_pushnil(L); return 1; }
    static const char* kBand[] = { "hero","monster","humanoid_npc","animal","townsfolk","unknown" };
    lua_pushstring(L, c->name);
    if (c->note) lua_pushstring(L, c->note); else lua_pushnil(L);
    lua_pushstring(L, kBand[(int)c->band <= 5 ? (int)c->band : 5]);
    return 3;
}

// sacred.combat_art(packed_id) -> name, class_name[, note] | nil
static int l_sacred_combat_art(lua_State* L) {
    uint32_t packed = (uint32_t)(lua_Unsigned)luaL_checkinteger(L, 1);
    const sacred::data::CombatArt* a = sacred::data::find_by_packed(packed);
    if (!a) { lua_pushnil(L); return 1; }
    lua_pushstring(L, a->name);
    lua_pushstring(L, a->class_name);
    if (a->note) lua_pushstring(L, a->note); else lua_pushnil(L);
    return 3;
}

// sacred.companions(class_id) -> { class_name, model_res, name_res={...} } | nil
// `class_id` is a hero id 1..9 (only 3/4/5/6 have a companion).
static int l_sacred_companions(lua_State* L) {
    unsigned cid = (unsigned)luaL_checkinteger(L, 1);
    const sacred::data::Companion* c = sacred::data::find_by_class_id((uint8_t)cid);
    if (!c) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushstring (L, c->class_name);              lua_setfield(L, -2, "class_name");
    lua_pushinteger(L, (lua_Integer)c->model_res);  lua_setfield(L, -2, "model_res");
    lua_newtable(L);
    for (unsigned i = 0; i < c->name_count; ++i) {
        lua_pushinteger(L, (lua_Integer)c->name_res[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "name_res");
    return 1;
}

// sacred.hash(name) -> uint  (Sacred resource-name hash & 0x7fffffff)
static int l_sacred_hash(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    lua_pushinteger(L, (lua_Integer)sacred::engine::sacred_hash(s, len));
    return 1;
}

// sacred.xp_for_level(level) -> cumulative XP to reach 1-based level (1..206).
static int l_sacred_xp_for_level(lua_State* L) {
    int lvl = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (lua_Integer)sacred::data::hero::xp_for_level(lvl));
    return 1;
}

// sacred.skill_name(id) -> English skill name (0..33) | nil
static int l_sacred_skill_name(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    if (id < 0 || id >= (int)sacred::data::hero::kSkillsCount) { lua_pushnil(L); return 1; }
    const char* n = sacred::data::hero::kSkills[id];
    if (!n || !*n) { lua_pushnil(L); return 1; }
    lua_pushstring(L, n);
    return 1;
}

// sacred.class_skills(class_id) -> { {slot=int, id=int, name=str}, ... } | nil
// The per-class skill layout (class_id 1..9); skips empty (id 0) slots.
static int l_sacred_class_skills(lua_State* L) {
    int cid = (int)luaL_checkinteger(L, 1);
    if (cid < 1 || cid > 9) { lua_pushnil(L); return 1; }
    using namespace sacred::data::hero;
    lua_newtable(L);
    int n = 0;
    for (int slot = 0; slot < (int)kClassSkillRows; ++slot) {
        uint8_t sid = class_skill(cid, slot);
        if (!sid) continue;
        lua_newtable(L);
        lua_pushinteger(L, slot);                 lua_setfield(L, -2, "slot");
        lua_pushinteger(L, sid);                  lua_setfield(L, -2, "id");
        const char* nm = (sid < kSkillsCount) ? kSkills[sid] : nullptr;
        lua_pushstring(L, (nm && *nm) ? nm : "?"); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// sacred.survival_bonus(deaths) -> bonus percentage (0..99)
static int l_sacred_survival_bonus(lua_State* L) {
    int v = (int)luaL_checkinteger(L, 1);
    if (v < 0) v = 0; if (v > 0xFFFF) v = 0xFFFF;
    lua_pushinteger(L, (lua_Integer)sacred::data::hero::survival_bonus_pct((uint16_t)v));
    return 1;
}

// sacred.bonus_name(packed_id) -> German item-bonus name | nil
static int l_sacred_bonus_name(lua_State* L) {
    uint32_t id = (uint32_t)(lua_Unsigned)luaL_checkinteger(L, 1);
    const char* n = sacred::data::name_de_of(id);
    if (!n) { lua_pushnil(L); return 1; }
    lua_pushstring(L, n);
    return 1;
}

// sacred.resolve_engine() -> { inflate, inflate_init, debug_log, resolved }.
// Runs the engine VA resolve/verify NOW (call in-game so code pages are
// decrypted) and returns the cached engine VAs. Read-only.
static int l_sacred_resolve_engine(lua_State* L) {
    engine_resolve::resolve();
    const auto& e = engine_resolve::g_engine;
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)e.inflate);      lua_setfield(L, -2, "inflate");
    lua_pushinteger(L, (lua_Integer)e.inflate_init); lua_setfield(L, -2, "inflate_init");
    lua_pushinteger(L, (lua_Integer)e.debug_log);    lua_setfield(L, -2, "debug_log");
    lua_pushboolean(L, e.resolved ? 1 : 0);          lua_setfield(L, -2, "resolved");
    return 1;
}

// sacred.globalres(handle) -> resolved UTF-8 string | nil
// Resolves a global.res string handle/ident via the engine's own resolver
// (FUN_0080f5e0). Accepts the 0x80000000-high-bit form (e.g. companion name_res
// handles from sacred.companions). Returns nil on miss / global.res not loaded.
static int l_sacred_globalres(lua_State* L) {
    unsigned int handle = (unsigned int)(lua_Unsigned)luaL_checkinteger(L, 1);
    char buf[1024];
    if (!engine_resolve::globalres_string(handle, buf, (int)sizeof(buf))) {
        lua_pushnil(L); return 1;
    }
    lua_pushstring(L, buf);
    return 1;
}


// ---------------------------------------------------------------------------
// Live-memory introspection + native companion mechanisms (2026-09-10).
// RE: .claude/knowledge/re/npc_ai_flags.md (+0x1F4 AI-mode word, nearby list
// DAT_00ad3bc8/DAT_00ad3ce0) and the follow/hire dig (FUN_0054cf70 = hire,
// cCreature vfn[6] = receive command object, cmd 0x10B = the script 'follow').
// ---------------------------------------------------------------------------
static uintptr_t api_reb() {
    HMODULE exe = g_attach.exe_module;
    return exe ? engine::mem::rebase(exe) : 0;
}

// Hero cCreature* via ctx+0x14 (player slot) -> object-manager array.
static uintptr_t api_hero_creature() {
    HMODULE exe = g_attach.exe_module; if (!exe) return 0;
    uintptr_t r = engine::mem::rebase(exe);
    uintptr_t om = engine::singletons::om(r), ctx = engine::singletons::ctx(r);
    if (!om || !ctx) return 0;
    uint32_t idx = 0;
    if (!engine::mem::read<uint32_t>(ctx + 0x14, &idx) || !idx || idx > 0x10) return 0;
    uintptr_t arr = 0, end = 0, c = 0;
    if (!engine::mem::read_ptr(om + 4, &arr) || !arr) return 0;
    if (!engine::mem::read_ptr(om + 8, &end)) return 0;
    if (idx >= (uint32_t)((end - arr) >> 2)) return 0;
    if (!engine::mem::read_ptr(arr + (uintptr_t)idx * 4, &c)) return 0;
    return c;
}

// sacred.peek_u32(va) -> integer | nil   (va = full engine VA, rebased)
static int l_sacred_peek_u32(lua_State* L) {
    uintptr_t va = (uintptr_t)(lua_Unsigned)luaL_checkinteger(L, 1);
    HMODULE exe = g_attach.exe_module; if (!exe) { lua_pushnil(L); return 1; }
    uint32_t v = 0;
    if (!engine::mem::read<uint32_t>(engine::mem::va(exe, va), &v)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

// sacred.hero_slot() -> player slot (ctx+0x14) | nil
static int l_sacred_hero_slot(lua_State* L) {
    HMODULE exe = g_attach.exe_module; if (!exe) { lua_pushnil(L); return 1; }
    uintptr_t ctx = engine::singletons::ctx(engine::mem::rebase(exe));
    uint32_t idx = 0;
    if (!ctx || !engine::mem::read<uint32_t>(ctx + 0x14, &idx)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)idx);
    return 1;
}

// sacred.nearby_list() -> { {h=, d=}, ... }   The engine's per-tick scratch
// list of "targets" for the creature that ticked last (FUN_00547070 builds it,
// sorted by distance, max 0x38). count = DAT_00ad3bc8, entries DAT_00ad3ce0.
static int l_sacred_nearby_list(lua_State* L) {
    uintptr_t r = api_reb();
    uint32_t n = 0;
    lua_newtable(L);
    if (!g_attach.exe_module || !engine::mem::read<uint32_t>(r + 0x00AD3BC8, &n)) return 1;
    if (n > 0x38) n = 0x38;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t h = 0, d = 0;
        engine::mem::read<uint32_t>(r + 0x00AD3CE0 + i * 8, &h);
        engine::mem::read<uint32_t>(r + 0x00AD3CE4 + i * 8, &d);
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, (lua_Integer)h); lua_setfield(L, -2, "h");
        lua_pushinteger(L, (lua_Integer)d); lua_setfield(L, -2, "d");
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

// sacred.hero_party() -> { {h=, v=, f=}, ... }  hero cCreature +0x39c/+0x3a0
// vector (stride 9: u32 handle, u32 value, u8 flag) -- where FUN_0054b200
// registers hirelings / followers. Read by cUI_Manager (0060abd0), 006d7165,
// 00710270 and by the hero teleport (FUN_0054d9d0 moves the party along).
static int l_sacred_hero_party(lua_State* L) {
    uintptr_t hero = api_hero_creature();
    lua_newtable(L);
    if (!hero) return 1;
    uintptr_t b = 0, e = 0;
    if (!engine::mem::read_ptr(hero + 0x39c, &b) || !engine::mem::read_ptr(hero + 0x3a0, &e)) return 1;
    if (!b || e < b) return 1;
    uint32_t n = (uint32_t)((e - b) / 9); if (n > 64) n = 64;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t h = 0, v = 0; uint8_t f = 0;
        engine::mem::read<uint32_t>(b + i * 9 + 0, &h);
        engine::mem::read<uint32_t>(b + i * 9 + 4, &v);
        engine::mem::read<uint8_t> (b + i * 9 + 8, &f);
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, (lua_Integer)h); lua_setfield(L, -2, "h");
        lua_pushinteger(L, (lua_Integer)v); lua_setfield(L, -2, "v");
        lua_pushinteger(L, (lua_Integer)f); lua_setfield(L, -2, "f");
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

// sacred.npc_ai(handle) -> table of the AI-relevant cCreature fields | nil
static int l_sacred_npc_ai(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    uintptr_t c = sdk::player::npc_creature(h);
    if (!c) { lua_pushnil(L); return 1; }
    uint32_t f1f4 = 0, f1f0 = 0, f200 = 0, owner = 0, x = 0, y = 0, hc = 0;
    uint32_t t158 = 0, t15c = 0, t164 = 0;
    uint16_t s261 = 0, s150 = 0, s152 = 0, sfc = 0, sfe = 0, lvl = 0;
    uint8_t oslot = 0, b2b6 = 0;
    engine::mem::read<uint32_t>(c + 0x158, &t158);   // talk: answer/next-action handle
    engine::mem::read<uint32_t>(c + 0x15c, &t15c);   // talk: dialog id
    engine::mem::read<uint32_t>(c + 0x164, &t164);   // talk: tooltip/window id (-1 = none)
    engine::mem::read<uint8_t> (c + 0x2b6, &b2b6);
    engine::mem::read<uint32_t>(c + 0x1F4, &f1f4);
    engine::mem::read<uint32_t>(c + 0x1F0, &f1f0);
    engine::mem::read<uint32_t>(c + 0x200, &f200);
    engine::mem::read<uint32_t>(c + 0x251, &owner);
    engine::mem::read<uint8_t> (c + 0x39,  &oslot);
    engine::mem::read<uint16_t>(c + 0x261, &s261);
    engine::mem::read<uint16_t>(c + 0x150, &s150);
    engine::mem::read<uint16_t>(c + 0x152, &s152);
    engine::mem::read<uint16_t>(c + 0xfc,  &sfc);
    engine::mem::read<uint16_t>(c + 0xfe,  &sfe);
    engine::mem::read<uint16_t>(c + 0x3fe, &lvl);
    engine::mem::read<uint32_t>(c + 0x1C,  &x);
    engine::mem::read<uint32_t>(c + 0x20,  &y);
    engine::mem::read<uint32_t>(c + 0x0c,  &hc);
    lua_createtable(L, 0, 14);
    lua_pushinteger(L, (lua_Integer)f1f4);  lua_setfield(L, -2, "f1f4");
    lua_pushinteger(L, (lua_Integer)f1f0);  lua_setfield(L, -2, "f1f0");
    lua_pushinteger(L, (lua_Integer)f200);  lua_setfield(L, -2, "f200");
    lua_pushinteger(L, (lua_Integer)owner); lua_setfield(L, -2, "owner");
    lua_pushinteger(L, (lua_Integer)oslot); lua_setfield(L, -2, "oslot");
    lua_pushinteger(L, (lua_Integer)s261);  lua_setfield(L, -2, "state");
    lua_pushinteger(L, (lua_Integer)s150);  lua_setfield(L, -2, "s150");
    lua_pushinteger(L, (lua_Integer)s152);  lua_setfield(L, -2, "s152");
    lua_pushinteger(L, (lua_Integer)sfc);   lua_setfield(L, -2, "fc");
    lua_pushinteger(L, (lua_Integer)sfe);   lua_setfield(L, -2, "fe");
    lua_pushinteger(L, (lua_Integer)lvl);   lua_setfield(L, -2, "level");
    lua_pushinteger(L, (lua_Integer)x);     lua_setfield(L, -2, "wx");
    lua_pushinteger(L, (lua_Integer)y);     lua_setfield(L, -2, "wy");
    lua_pushinteger(L, (lua_Integer)hc);    lua_setfield(L, -2, "h");
    lua_pushinteger(L, (lua_Integer)t158);  lua_setfield(L, -2, "t158");
    lua_pushinteger(L, (lua_Integer)t15c);  lua_setfield(L, -2, "t15c");
    lua_pushinteger(L, (lua_Integer)t164);  lua_setfield(L, -2, "t164");
    lua_pushinteger(L, (lua_Integer)b2b6);  lua_setfield(L, -2, "b2b6");
    return 1;
}

// sacred.npc_hire(handle [,flag=1 [,notify=0]]) -> bool
// Native "hireling" recruit: FUN_0054cf70(ECX=hero, creature, flag, notify).
// Engine sets +0x251=hero, +0x1F4=(f&0xEFF62000)|0x100 (hireling AI mode),
// WakeUp, registers into hero+0x39c (FUN_0054b200) and level-syncs.
typedef void (__thiscall* fn_hire_t)(void* hero, void* creature, uint8_t flag, char notify);
static int l_sacred_npc_hire(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    uint8_t flag = (lua_gettop(L) >= 2) ? (uint8_t)luaL_checkinteger(L, 2) : 1;
    char notify  = (lua_gettop(L) >= 3) ? (char)luaL_checkinteger(L, 3) : 0;
    uintptr_t c = sdk::player::npc_creature(h);
    uintptr_t hero = api_hero_creature();
    uintptr_t r = api_reb();
    if (!c || !hero || !g_attach.exe_module) { lua_pushboolean(L, 0); return 1; }
    bool ok = true;
    __try {
        ((fn_hire_t)(r + 0x0054CF70))((void*)hero, (void*)c, flag, notify);
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    sdk_log("[hire] h=%d hero=%p creature=%p flag=%u -> %s", h, (void*)hero, (void*)c,
            (unsigned)flag, ok ? "ok" : "FAULT");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// sacred.npc_cmd(handle, cmd, a [,b=0]) -> bool
// Deliver a command object to the creature through its vfn[6] (vt+0x18),
// exactly like CreateNPC and the AI do. Object: {+0 vtable PTR_FUN_0089095c,
// +4 cmd id, +0x14 a, +0x18 b, rest 0} (0x44 bytes).
//   0x10B (script 'follow'): a=4, b=owner handle/slot -> +0x251=b, +0x1F4|=4,
//                            WakeUp, FUN_0054b200 registration.
//   0x100 (goto/approach):   a=target handle.
typedef void* (__thiscall* fn_recv_t)(void* self, void* cmd);
static int l_sacred_npc_cmd(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    uint32_t cmd = (uint32_t)luaL_checkinteger(L, 2);
    uint32_t a   = (uint32_t)luaL_checkinteger(L, 3);
    uint32_t b   = (lua_gettop(L) >= 4) ? (uint32_t)luaL_checkinteger(L, 4) : 0;
    if (cmd != 0x100 && cmd != 0x10B) {
        sdk_log("[npc_cmd] h=%d cmd=0x%X refused (only 0x100/0x10B are vetted)", h, cmd);
        lua_pushboolean(L, 0); return 1;
    }
    uintptr_t c = sdk::player::npc_creature(h);
    uintptr_t r = api_reb();
    if (!c || !g_attach.exe_module) { lua_pushboolean(L, 0); return 1; }
    uintptr_t vt = 0, fn = 0;
    if (!engine::mem::read_ptr(c, &vt) || !vt || !engine::mem::read_ptr(vt + 0x18, &fn) || !fn) {
        lua_pushboolean(L, 0); return 1;
    }
    uint32_t obj[0x11] = { 0 };
    obj[0] = (uint32_t)(r + 0x0089095C);
    obj[1] = cmd;
    obj[5] = a;
    obj[6] = b;
    bool ok = true;
    __try {
        ((fn_recv_t)fn)((void*)c, (void*)obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    sdk_log("[npc_cmd] h=%d cmd=0x%X a=%u b=%u via vfn@%p -> %s", h, cmd, a, b, (void*)fn,
            ok ? "ok" : "FAULT");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// Model motion slots (lib/classmod.lua, 2026-09-16).
// cGrannyModelManager (singleton [0x00AA4538], ctor FUN_00411000) keeps one
// 0x4AA-byte header per model in a vector at +0x48/+0x4C, the pak\Models.tmp
// record layout (name at +0). Motion slot s of model m is the u32 at
// header+0x70+4*s (FUN_004135d0), an index into the motion vector at +0x54;
// 0 = INVALID_MOTION, which the hero plays as a T-pose.
// ---------------------------------------------------------------------------
struct SlotWrite { uint32_t model; char name[32]; uint32_t slot, from, to; bool logged; };
static std::vector<SlotWrite> g_slot_writes;
static SRWLOCK g_slot_lock = SRWLOCK_INIT;

static const uint32_t MODEL_HDR_SIZE = 0x4AA;
static const uint32_t MODEL_SLOT_MAX = 259;   // header+0x47C onward is per-run data

// sacred.model_slots(model, name, {[slot] = motion, ...}) -> queued count
// Queues writes of empty (0) motion slots; model_slots_tick applies each one
// once the manager holds its headers, only while the header's name matches
// and the slot still holds 0. Safe to call during the bake.
static int l_sacred_model_slots(lua_State* L) {
    uint32_t model = (uint32_t)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    int queued = 0;
    AcquireSRWLockExclusive(&g_slot_lock);
    lua_pushnil(L);
    while (lua_next(L, 3)) {
        if (lua_isinteger(L, -2) && lua_isinteger(L, -1)) {
            SlotWrite w{};
            w.model = model;
            strncpy_s(w.name, _TRUNCATE, name, _TRUNCATE);
            w.slot = (uint32_t)lua_tointeger(L, -2);
            w.to = (uint32_t)lua_tointeger(L, -1);
            if (w.slot < MODEL_SLOT_MAX && w.to) { g_slot_writes.push_back(w); queued++; }
        }
        lua_pop(L, 1);
    }
    ReleaseSRWLockExclusive(&g_slot_lock);
    sdk_log("[model_slots] model #%u '%s': %d slot writes queued", model, name, queued);
    lua_pushinteger(L, queued);
    return 1;
}

// Guarded writes into the exe image (full VAs, rebased), queued from Lua and
// applied from the heartbeat once the build is TRUSTED (post-decrypt pins).
// Each is written only while the live bytes equal `expect`; anything else is
// refused and logged, so a different build never gets a stray write.
//   sacred.patch_u32(va, expect, value, what)
//   sacred.patch_u32_copy(va, expect, src_va, what[, unless])
//       value = the live u32 at src_va at apply time (a jump-table slot of
//       another case); skipped when that value equals `unless`
//   sacred.patch_bytes(va, expect_bytes, new_bytes, what)   (same length, <= 64)
struct Patch {
    uint32_t va = 0, copy_from = 0, unless = 0;
    bool has_unless = false, done = false;
    std::string expect, value;
    char what[64] = {0};
};
static std::vector<Patch> g_patches;

static std::string u32_bytes(uint32_t v) { return std::string((const char*)&v, 4); }

static void queue_patch(Patch&& p) {
    AcquireSRWLockExclusive(&g_slot_lock);
    g_patches.push_back(std::move(p));
    ReleaseSRWLockExclusive(&g_slot_lock);
}

static int l_sacred_patch_u32(lua_State* L) {
    Patch p;
    p.va = (uint32_t)luaL_checkinteger(L, 1);
    p.expect = u32_bytes((uint32_t)luaL_checkinteger(L, 2));
    p.value = u32_bytes((uint32_t)luaL_checkinteger(L, 3));
    strncpy_s(p.what, _TRUNCATE, luaL_optstring(L, 4, "?"), _TRUNCATE);
    queue_patch(std::move(p));
    lua_pushboolean(L, 1);
    return 1;
}

static int l_sacred_patch_u32_copy(lua_State* L) {
    Patch p;
    p.va = (uint32_t)luaL_checkinteger(L, 1);
    p.expect = u32_bytes((uint32_t)luaL_checkinteger(L, 2));
    p.copy_from = (uint32_t)luaL_checkinteger(L, 3);
    strncpy_s(p.what, _TRUNCATE, luaL_optstring(L, 4, "?"), _TRUNCATE);
    if (lua_isinteger(L, 5)) { p.has_unless = true; p.unless = (uint32_t)lua_tointeger(L, 5); }
    queue_patch(std::move(p));
    lua_pushboolean(L, 1);
    return 1;
}

static int l_sacred_patch_bytes(lua_State* L) {
    size_t ne = 0, nv = 0;
    const char* e = luaL_checklstring(L, 2, &ne);
    const char* v = luaL_checklstring(L, 3, &nv);
    luaL_argcheck(L, ne == nv && ne > 0 && ne <= 64, 3, "expect and new bytes must be 1..64 bytes, same length");
    Patch p;
    p.va = (uint32_t)luaL_checkinteger(L, 1);
    p.expect.assign(e, ne);
    p.value.assign(v, nv);
    strncpy_s(p.what, _TRUNCATE, luaL_optstring(L, 4, "?"), _TRUNCATE);
    queue_patch(std::move(p));
    lua_pushboolean(L, 1);
    return 1;
}

static void patches_apply(uintptr_t r) {
    if (!engine::build::can_patch_text()) return;
    for (Patch& p : g_patches) {
        if (p.done) continue;
        p.done = true;
        uintptr_t at = r + p.va;
        size_t n = p.expect.size();
        if (p.copy_from) {
            uint32_t src = 0;
            if (!engine::mem::read<uint32_t>(r + p.copy_from, &src)) {
                sdk_log("[patch] %s: source @%08X unreadable", p.what, p.copy_from);
                continue;
            }
            if (p.has_unless && src == p.unless) {
                sdk_log("[patch] %s: source @%08X holds %08X, skipped", p.what, p.copy_from, src);
                continue;
            }
            p.value = u32_bytes(src);
        }
        if (IsBadReadPtr((void*)at, n)) {
            sdk_log("[patch] %s @%08X unreadable", p.what, p.va);
        } else if (memcmp((void*)at, p.value.data(), n) == 0) {
            sdk_log("[patch] %s @%08X already applied", p.what, p.va);
        } else if (memcmp((void*)at, p.expect.data(), n) != 0) {
            sdk_log("[patch] %s @%08X refused: live bytes differ from the expected ones", p.what, p.va);
        } else {
            DWORD old = 0;
            bool ok = VirtualProtect((void*)at, n, PAGE_EXECUTE_READWRITE, &old) != 0;
            if (ok) {
                memcpy((void*)at, p.value.data(), n);
                VirtualProtect((void*)at, n, old, &old);
                FlushInstructionCache(GetCurrentProcess(), (void*)at, n);
            }
            sdk_log("[patch] %s @%08X (%u bytes) %s", p.what, p.va, (unsigned)n,
                    ok ? "ok" : "VirtualProtect FAILED");
        }
    }
}

// sacred.wear_hide(type) -> true
// Creatures of that type (hero classes whose body classmod replaced) wear no
// item meshes: the borrowed NPC body is already dressed, and a class's armor
// meshes do not fit it. FUN_00555300 (visual refresh of one equip slot) wears
// an item through its only call at 0x00555928 (`call FUN_0044c980`, __thiscall
// ECX = item, 2 args, ret 8, ESI = the creature). The hook answers "not worn"
// for a hidden type, which is vanilla's own failure branch (0x005559AE unloads
// the item model; the item stays in its slot), after the CalcResults(0,1) the
// success branch would have run. Weapons, rings and wings attach to bones on
// another path and stay visible.
static uint32_t g_wear_hide_mask = 0;      // bit t: creature type t (1..31)
static uintptr_t g_wear_orig = 0;          // live FUN_0044c980
static uintptr_t g_calc_results = 0;       // live FUN_005796a0 (CalcResults)

__declspec(naked) static void __cdecl hook_wear_item() {
    __asm {
        mov eax, dword ptr [esi + 0x10]
        cmp eax, 32
        jae wear
        bt dword ptr [g_wear_hide_mask], eax
        jnc wear
        push ecx
        lea ecx, [esi + 0x3A8]
        push 1
        push 0
        call dword ptr [g_calc_results]
        pop ecx
        xor eax, eax
        ret 8
    wear:
        jmp dword ptr [g_wear_orig]
    }
}

static int l_sacred_wear_hide(lua_State* L) {
    lua_Integer t = luaL_checkinteger(L, 1);
    luaL_argcheck(L, t >= 1 && t < 32, 1, "creature type 1..31");
    uintptr_t r = api_reb();
    AcquireSRWLockExclusive(&g_slot_lock);
    bool first = (g_wear_hide_mask == 0);
    g_wear_hide_mask |= 1u << (uint32_t)t;
    ReleaseSRWLockExclusive(&g_slot_lock);
    if (first) {
        g_wear_orig = r + 0x0044C980;
        g_calc_results = r + 0x005796A0;
        const uint8_t old_call[5] = { 0xE8, 0x53, 0x70, 0xEF, 0xFF };
        int32_t rel = (int32_t)((uintptr_t)&hook_wear_item - (r + 0x0055592D));
        Patch p;
        p.va = 0x00555928;
        p.expect.assign((const char*)old_call, 5);
        p.value.assign(1, (char)0xE8);
        p.value.append((const char*)&rel, 4);
        strncpy_s(p.what, _TRUNCATE, "wear hook (hidden item meshes)", _TRUNCATE);
        queue_patch(std::move(p));
    }
    sdk_log("[wear_hide] creature type %d wears no item meshes", (int)t);
    lua_pushboolean(L, 1);
    return 1;
}

// Heartbeat: apply queued slot writes and patches. Cheap when there are none.
void model_slots_tick() {
    if (!g_attach.exe_module) return;
    AcquireSRWLockExclusive(&g_slot_lock);
    if (g_slot_writes.empty() && g_patches.empty()) { ReleaseSRWLockExclusive(&g_slot_lock); return; }
    uintptr_t r = api_reb(), mgr = 0, beg = 0, end = 0;
    patches_apply(r);
    int applied = 0, refused = 0;
    if (engine::mem::read_ptr(r + 0x00AA4538, &mgr) && mgr &&
        engine::mem::read_ptr(mgr + 0x48, &beg) && beg &&
        engine::mem::read_ptr(mgr + 0x4C, &end) && end > beg) {
        uint32_t count = (uint32_t)((end - beg) / MODEL_HDR_SIZE);
        for (SlotWrite& w : g_slot_writes) {
            if (w.model >= count) continue;
            uintptr_t hdr = beg + (uintptr_t)w.model * MODEL_HDR_SIZE;
            char have[32] = {0};
            if (IsBadReadPtr((void*)hdr, sizeof(have))) continue;
            memcpy(have, (void*)hdr, sizeof(have) - 1);
            uint32_t cur = 0;
            if (!engine::mem::read<uint32_t>(hdr + 0x70 + 4 * w.slot, &cur) || cur == w.to) continue;
            if (_stricmp(have, w.name) != 0 || cur != 0) {
                if (!w.logged) {
                    sdk_log("[model_slots] model #%u slot %u refused: header '%s' slot=%u (want '%s' slot=0)",
                            w.model, w.slot, have, cur, w.name);
                    w.logged = true;
                    refused++;
                }
                continue;
            }
            if (engine::mem::write<uint32_t>(hdr + 0x70 + 4 * w.slot, w.to)) applied++;
        }
    }
    ReleaseSRWLockExclusive(&g_slot_lock);
    if (applied || refused)
        sdk_log("[model_slots] applied %d slot writes (%d refused)", applied, refused);
}

// Register all data / engine-introspection bindings onto the `sacred` table
// (already on top of the stack when called from install_lua_api).
void install_data_api(lua_State* L) {
    lua_pushcfunction(L, l_sacred_read_save);      lua_setfield(L, -2, "read_save");
    lua_pushcfunction(L, l_sacred_creature_name);  lua_setfield(L, -2, "creature_name");
    lua_pushcfunction(L, l_sacred_combat_art);     lua_setfield(L, -2, "combat_art");
    lua_pushcfunction(L, l_sacred_companions);     lua_setfield(L, -2, "companions");
    lua_pushcfunction(L, l_sacred_hash);           lua_setfield(L, -2, "hash");
    lua_pushcfunction(L, l_sacred_xp_for_level);   lua_setfield(L, -2, "xp_for_level");
    lua_pushcfunction(L, l_sacred_skill_name);     lua_setfield(L, -2, "skill_name");
    lua_pushcfunction(L, l_sacred_class_skills);   lua_setfield(L, -2, "class_skills");
    lua_pushcfunction(L, l_sacred_survival_bonus); lua_setfield(L, -2, "survival_bonus");
    lua_pushcfunction(L, l_sacred_bonus_name);     lua_setfield(L, -2, "bonus_name");
    lua_pushcfunction(L, l_sacred_resolve_engine); lua_setfield(L, -2, "resolve_engine");
    lua_pushcfunction(L, l_sacred_globalres);      lua_setfield(L, -2, "globalres");
    lua_pushcfunction(L, l_sacred_peek_u32);       lua_setfield(L, -2, "peek_u32");
    lua_pushcfunction(L, l_sacred_hero_slot);      lua_setfield(L, -2, "hero_slot");
    lua_pushcfunction(L, l_sacred_nearby_list);    lua_setfield(L, -2, "nearby_list");
    lua_pushcfunction(L, l_sacred_hero_party);     lua_setfield(L, -2, "hero_party");
    lua_pushcfunction(L, l_sacred_npc_ai);         lua_setfield(L, -2, "npc_ai");
    lua_pushcfunction(L, l_sacred_npc_hire);       lua_setfield(L, -2, "npc_hire");
    lua_pushcfunction(L, l_sacred_npc_cmd);        lua_setfield(L, -2, "npc_cmd");
    lua_pushcfunction(L, l_sacred_model_slots);    lua_setfield(L, -2, "model_slots");
    lua_pushcfunction(L, l_sacred_patch_u32);      lua_setfield(L, -2, "patch_u32");
    lua_pushcfunction(L, l_sacred_patch_u32_copy); lua_setfield(L, -2, "patch_u32_copy");
    lua_pushcfunction(L, l_sacred_patch_bytes);    lua_setfield(L, -2, "patch_bytes");
    lua_pushcfunction(L, l_sacred_wear_hide);      lua_setfield(L, -2, "wear_hide");
}

}} // namespace sdk::runtime_triggers
