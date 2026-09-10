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
}

}} // namespace sdk::runtime_triggers
