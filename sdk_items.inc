// sdk_items.inc - the hero's items: backpack, equipment and the quest-item list
// (SDK_GAPS gaps 6 and 7). Included by runtime_triggers.cpp after sdk_vars.inc.
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
//                    others (FUN_005da2d0(i, 0)); the grid is +0x115c wide and
//                    +0x115e high (FUN_005da710)
//   hero + 0x1A4     19 equipment slots of item handles (0x12 = the mount)
// An item object keeps its type in the low 16 bits of +0x10, like a creature.
//
//   sacred.hero_items()             -> { {where="bag"|"equip"|"quest", slot, ref, type}, ... } | nil
//   sacred.hero_item_count(type)    -> items of that type in the backpack and equipment | nil
//   sacred.hero_has_item(type)      -> bool | nil: carried, or on the quest-item list
//   sacred.hero_put_item(type)      -> the new item's handle | nil: into the backpack
//   sacred.hero_take_item(type[, n])-> how many were taken | nil: out of the backpack / off the hero
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

// The inventory block of a creature (by its own handle at +0x0C), or 0. Call
// inside __try.
static uintptr_t inv_block_of(uintptr_t creature) {
    uint32_t owner = *(uint32_t*)(creature + 0x0C) & 0xFFFF;
    uintptr_t blocks = *(uintptr_t*)INV_BLOCKS;
    return (blocks && owner < INV_MAX_OWNER) ? blocks + owner * INV_BLOCK_SIZE : 0;
}

// The hero's items into out[0..max): backpack, equipment, then the quest list.
// Returns the count, or -1 without a hero.
static int inv_scan(InvItem* out, int max) {
    uintptr_t hero = sdk::player::hero_base();
    if (!hero) return -1;
    int n = 0;
    __try {
        uintptr_t inv = inv_block_of(hero);
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

// sacred.hero_put_item(type) -> item handle | nil
// A new item of `type` into the hero's backpack through the engine's own
// cCreature_inventory_putItem_00549260 (thiscall on the creature: type, 1,
// item 0 = create one, owner 0 = the creature itself; the engine's caller at
// 0x60dced pushes the same). It asks for room of the item's grid size
// (FUN_00428d60, FUN_005daba0), creates the item (create_005fb530) and places it
// (FUN_005d9f00). nil = no room (the engine plays its "full" sound and logs
// "<no empty slots>") or no hero. A CreateObj op 0x61 give only lists a quest
// item (reward.give_quest_item); this is a real item.
typedef uint32_t (__thiscall* put_item_t)(void* creature, uint32_t type, int one,
                                           uint32_t item, uint32_t owner);
static const put_item_t kPutItem = (put_item_t)0x00549260;

static uint32_t put_item_now(uintptr_t hero, uint32_t type, bool* fault) {
    uint32_t h = 0;
    *fault = false;
    __try { h = kPutItem((void*)hero, type, 1, 0, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; }
    return h;
}

static int l_sacred_hero_put_item(lua_State* L) {
    lua_Integer t = luaL_checkinteger(L, 1);
    uintptr_t hero = sdk::player::hero_base();
    uint32_t h = 0;
    bool fault = false;
    if (hero && t > 0 && t < 0x7e60 && (uintptr_t)g_attach.exe_module == 0x00400000)
        h = put_item_now(hero, (uint32_t)t, &fault);
    sdk_log("[items] put type %d into the hero's backpack -> %u%s", (int)t, h, fault ? " (fault)" : "");
    if (h) lua_pushinteger(L, h); else lua_pushnil(L);
    return 1;
}

// sacred.hero_take_item(type[, n = 1]) -> how many were taken | nil
// Takes items of `type` from the hero the way vanilla's DelOBJ (tag 0x37,
// FUN_00497f80 from 0x4980a6) takes a held item: clear the quest flag
// (+0x14 bit 0x800); if the object manager says it is held (FUN_005fe4b0(h, 2)),
// off the hero when worn (FUN_00549430 -> FUN_00556f10) or out of the backpack
// grid (FUN_005da710 -> FUN_005da630(h, 1)), else detach it (FUN_005fc8b0); then
// destroy it (cObjectManager_destroy_005fbdb0(h, 1, 0, 1)). All thiscall, the
// argument counts checked against each function's ret. The backpack goes
// first, then the equipment; quest-list entries are not touched.
typedef int  (__thiscall* om_held_t)(void* om, uint32_t h, int kind);
typedef void (__thiscall* om_detach_t)(void* om, uint32_t h);
typedef char (__thiscall* cre_wears_t)(void* cre, uint32_t h);
typedef void (__thiscall* cre_unequip_t)(void* cre, uint32_t h);
typedef char (__thiscall* inv_holds_t)(void* inv, uint32_t h);
typedef void (__thiscall* inv_remove_t)(void* inv, uint32_t h, int flag);
typedef void (__thiscall* om_destroy_t)(void* om, uint32_t h, int a, int b, int c);

static bool take_item_now(uintptr_t hero, uint32_t h, bool* fault) {
    *fault = false;
    __try {
        void* om = *(void**)0x00AD5C40;
        uintptr_t obj = sdk::player::npc_creature((int)h);
        if (!om || !obj) return false;
        *(uint32_t*)(obj + 0x14) &= ~0x800u;
        if (((om_held_t)0x005FE4B0)(om, h, 2)) {
            uintptr_t inv = inv_block_of(hero);
            if (((cre_wears_t)0x00549430)((void*)hero, h))
                ((cre_unequip_t)0x00556F10)((void*)hero, h);
            else if (inv && ((inv_holds_t)0x005DA710)((void*)inv, h))
                ((inv_remove_t)0x005DA630)((void*)inv, h, 1);
            else
                return false;                   // held, but not by the hero: leave it
        } else {
            ((om_detach_t)0x005FC8B0)(om, h);
        }
        ((om_destroy_t)0x005FBDB0)(om, h, 1, 0, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; }
    return false;
}

static int l_sacred_hero_take_item(lua_State* L) {
    uint32_t type = (uint32_t)luaL_checkinteger(L, 1);
    int want = (int)luaL_optinteger(L, 2, 1);
    uintptr_t hero = sdk::player::hero_base();
    InvItem items[INV_MAX_ITEMS];
    int n = inv_scan(items, INV_MAX_ITEMS);
    if (n < 0 || !hero || (uintptr_t)g_attach.exe_module != 0x00400000) { lua_pushnil(L); return 1; }
    int took = 0;
    bool fault = false;
    for (int i = 0; i < n && took < want && !fault; i++) {    // backpack entries come first
        if (items[i].type != type || items[i].where == INV_QUEST) continue;
        if (take_item_now(hero, items[i].ref, &fault)) took++;
    }
    sdk_log("[items] took %d of type %u from the hero%s", took, type, fault ? " (fault)" : "");
    lua_pushinteger(L, took);
    return 1;
}

// ---- creature type classes: the engine's own "this one does not die" ----------
// The death path asks the TYPE, not the creature: `FUN_00426a80(type)` returns
// true when the type's class byte is 5, and both death sites then skip dying
// while the creature's max health is non-zero (@0x533c88 and @0x534778:
// `call 0x426a80; test al,al; je <die>; mov eax,[ebp+0x4d4]; test eax,eax;
// jne <skip>`). That is how a shipped quest companion survives a beating.
//
// Where the byte lives: the type manager (*0x00AAB5E4) holds a per-type record
// of 0x80 bytes; its +0x1a is a u16 index into a second table at
// manager+0x563008 with a stride of 0x56, and the class byte is at +4 of that
// entry. Sibling helpers test the same byte for 2 (0x426ad0) and 9 (0x426b20),
// so the byte is a category, and 5 is the death-skipping one.
static uint8_t* creature_class_ptr(int type) {
    if (type <= 0 || type >= 0x7E60) return nullptr;
    __try {
        uintptr_t tm = *(uintptr_t*)0x00AAB5E4;
        if (!tm) return nullptr;
        uint16_t idx = *(uint16_t*)(tm + (uintptr_t)type * 0x80 + 0x1A);
        uintptr_t tbl = *(uintptr_t*)(tm + 0x563008);
        if (!tbl) return nullptr;
        return (uint8_t*)(tbl + (uintptr_t)idx * 0x56 + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// sacred.creature_class(type) -> class | nil
static int l_sacred_creature_class(lua_State* L) {
    int type = (int)luaL_checkinteger(L, 1);
    uint8_t* p = creature_class_ptr(type);
    if (!p) { lua_pushnil(L); return 1; }
    uint8_t v = 0;
    __try { v = *p; } __except (EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, v);
    return 1;
}

// sacred.creature_class_set(type, class) -> old | nil
// Writing 5 makes every creature of that type survive a killing blow (the engine
// skips the death). It is a TYPE-wide change, so use a type your mod owns.
static int l_sacred_creature_class_set(lua_State* L) {
    int type = (int)luaL_checkinteger(L, 1);
    int cls  = (int)luaL_checkinteger(L, 2);
    if (cls < 0 || cls > 255) return luaL_error(L, "creature_class_set: class 0..255");
    uint8_t* p = creature_class_ptr(type);
    if (!p) { lua_pushnil(L); return 1; }
    uint8_t old = 0;
    __try { old = *p; *p = (uint8_t)cls; }
    __except (EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    sdk_log("[types] creature type %d class %u -> %u", type, old, (unsigned)cls);
    lua_pushinteger(L, old);
    return 1;
}

// ---- putting a fallen companion back on his feet ------------------------------
// FUN_0053e460 is the engine's own "restore this creature": it sets health back
// to its maximum (+0x4d8 and +0x4d0 from +0x4d4 @0x53e554-0x53e567) and clears
// the fields around it. The engine calls it on the party paths -- when a member
// is dropped or released, FUN_0054d380 @0x54d3ad runs it first if the creature
// carries the "gets back up" bit (+0x2B7 bit 8), and the release path in
// FUN_0052e590 @0x52e723 does the same with the argument 0. Argument 0 is the
// one without the gold penalty branch, so that is what we pass.
typedef void (__thiscall* fn_restore_t)(void* creature, int mode);
static const fn_restore_t kRestore = (fn_restore_t)0x0053E460;

// sacred.npc_revive(handle) -> bool
// Health back to full through the engine's own routine. Whether a creature the
// engine already marked as fallen stands up again is the engine's business --
// persona.lua falls back to putting the character in the world again.
static int l_sacred_npc_revive(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    bool ok = false;
    __try {
        uintptr_t om = *(uintptr_t*)0x00AD5C40;
        uintptr_t arr = om ? *(uintptr_t*)(om + 4) : 0;
        void* c = (arr && h > 0) ? *(void**)(arr + (uintptr_t)h * 4) : nullptr;
        if (c) { kRestore(c, 0); ok = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    sdk_log("[npc] revive h=%d -> %s", h, ok ? "called" : "failed");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
