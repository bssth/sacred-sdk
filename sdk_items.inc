// sdk_items.inc - the hero's items: backpack, equipment and the quest-item list
// (SDK_GAPS gap 6). Included by runtime_triggers.cpp after sdk_vars.inc.
//
// The layout mirrors the engine's own HasItem: condition (op 0x3a, asm
// 0x49881f-0x4989b6):
//   inventory block  [0x00AD4448] + (owner handle & 0xFFFF) * 0x1180, for
//                    owners below 0x20 (FUN_005debc0 = lazy getter of the block
//                    array, FUN_005dec90 = pick a block, 0 from 0x20 up)
//     +0x00/+0x04    vector of {u32 type, u32 res}: the quest items handed out
//                    by CreateObj op 0x61 (addMainQuestItem); FUN_005d8490
//                    matches a type or a res against it
//     +0x18          256 backpack cells of 0x0c bytes; the dword is the item's
//                    object handle in the cell where the item starts, 0 in the
//                    others (FUN_005da2d0(i, 0))
//   hero + 0x1A4     19 equipment slots of item handles (0x12 = the mount)
// An item object keeps its type in the low 16 bits of +0x10, like a creature.
//
//   sacred.hero_items()          -> { {where="bag"|"equip"|"quest", slot, ref, type}, ... } | nil
//   sacred.hero_item_count(type) -> items of that type in the backpack and equipment | nil
//   sacred.hero_has_item(type)   -> bool | nil: carried, or on the quest-item list
// nil = no hero (menu, loading). `ref` is the item handle, or the res id of a
// quest-list entry.

constexpr uintptr_t INV_BLOCKS      = 0x00AD4448;
constexpr uintptr_t INV_BLOCK_SIZE  = 0x1180;
constexpr uint32_t  INV_MAX_OWNER   = 0x20;
constexpr uintptr_t INV_OFF_BAG     = 0x18;
constexpr uintptr_t INV_CELL_SIZE   = 0x0C;
constexpr int       INV_BAG_CELLS   = 256;
constexpr int       INV_EQUIP_SLOTS = 19;
constexpr int       INV_QUEST_MAX   = 64;
constexpr int       INV_MAX_ITEMS   = INV_BAG_CELLS + INV_EQUIP_SLOTS + INV_QUEST_MAX;

enum { INV_BAG, INV_EQUIP, INV_QUEST };
struct InvItem { int where, slot; uint32_t ref, type; };

// The type of the object behind handle `h`, or 0 when there is none.
static uint32_t inv_type_of(uint32_t h) {
    uintptr_t o = h ? sdk::player::npc_creature((int)h) : 0;
    return o ? (*(uint32_t*)(o + 0x10) & 0xFFFF) : 0;
}

// The hero's items into out[0..max): backpack, equipment, then the quest list.
// Returns the count, or -1 without a hero.
static int inv_scan(InvItem* out, int max) {
    uintptr_t hero = sdk::player::hero_base();
    if (!hero) return -1;
    int n = 0;
    __try {
        uint32_t owner = *(uint32_t*)(hero + 0x0C) & 0xFFFF;     // the hero's own handle
        uintptr_t blocks = *(uintptr_t*)INV_BLOCKS;
        uintptr_t inv = (blocks && owner < INV_MAX_OWNER) ? blocks + owner * INV_BLOCK_SIZE : 0;
        for (int i = 0; inv && i < INV_BAG_CELLS && n < max; i++) {
            uint32_t h = *(uint32_t*)(inv + INV_OFF_BAG + i * INV_CELL_SIZE);
            uint32_t t = inv_type_of(h);
            if (t) out[n++] = { INV_BAG, i, h, t };
        }
        for (int s = 0; s < INV_EQUIP_SLOTS && n < max; s++) {
            uint32_t h = *(uint32_t*)(hero + HERO_OFF_EQUIP_BASE + s * 4);
            uint32_t t = inv_type_of(h);
            if (t) out[n++] = { INV_EQUIP, s, h, t };
        }
        uintptr_t qb = inv ? *(uintptr_t*)inv : 0, qe = inv ? *(uintptr_t*)(inv + 4) : 0;
        int qn = (qb && qe > qb) ? (int)((qe - qb) / 8) : 0;
        for (int i = 0; i < qn && i < INV_QUEST_MAX && n < max; i++)
            out[n++] = { INV_QUEST, i, *(uint32_t*)(qb + i * 8 + 4), *(uint32_t*)(qb + i * 8) };
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// Items of `type` the hero carries (backpack + equipment); -1 without a hero.
// *listed = the type is on the quest-item list.
static int hero_item_count_type(uint32_t type, bool* listed) {
    InvItem items[INV_MAX_ITEMS];
    int n = inv_scan(items, INV_MAX_ITEMS);
    if (listed) *listed = false;
    if (n < 0) return -1;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (items[i].type != type) continue;
        if (items[i].where == INV_QUEST) { if (listed) *listed = true; }
        else count++;
    }
    return count;
}

static int l_sacred_hero_items(lua_State* L) {
    InvItem items[INV_MAX_ITEMS];
    int n = inv_scan(items, INV_MAX_ITEMS);
    if (n < 0) { lua_pushnil(L); return 1; }
    static const char* const where[] = { "bag", "equip", "quest" };
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, where[items[i].where]);  lua_setfield(L, -2, "where");
        lua_pushinteger(L, items[i].slot);         lua_setfield(L, -2, "slot");
        lua_pushinteger(L, items[i].ref);          lua_setfield(L, -2, "ref");
        lua_pushinteger(L, items[i].type);         lua_setfield(L, -2, "type");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_sacred_hero_item_count(lua_State* L) {
    int n = hero_item_count_type((uint32_t)luaL_checkinteger(L, 1), nullptr);
    if (n < 0) lua_pushnil(L); else lua_pushinteger(L, n);
    return 1;
}

static int l_sacred_hero_has_item(lua_State* L) {
    int r = sacred_hero_has_item((int)luaL_checkinteger(L, 1));
    if (r < 0) lua_pushnil(L); else lua_pushboolean(L, r);
    return 1;
}
