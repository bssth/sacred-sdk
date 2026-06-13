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
}

}} // namespace sdk::runtime_triggers
