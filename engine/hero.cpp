// engine/hero.cpp — hero read-only snapshot + world position. (Goal A4)
//
// Split verbatim out of the old 1644-line player_state.cpp god file. The
// namespace is unchanged (sdk::player), so every call site + the sdk.h
// declarations are untouched — this is a pure relocation, no logic edits.
#include "../sdk.h"
#include "addresses.h"
#include "offsets.h"
#include "mem.h"
#include "singletons.h"
#include "player_internal.h"
#include <stdint.h>

namespace sdk { namespace player {

// --- Static name tables ---------------------------------------------------
// Class id mapping from sacred_modding - characters.csv. The class id is the
// HIGH byte of a u16; the low byte is sub-variant (e.g. Vampiress 06=human,
// 07=vampire). For the player struct field at +0x10 (u16 LE) the value is
// stored as class << 8 in the CT NPC struct, but the "Character Class" view
// in CT reads it as 2 Bytes and shows e.g. 4 for Dark Elf — so the field is
// effectively just the class index (low part is variant). We index by the
// class index 0..9 for now.

const char* CLASS_NAMES[10] = {
    "(none)",
    "Seraphim",
    "Gladiator",
    "Battle Mage",
    "Dark Elf",
    "Wood Elf",
    "Vampiress (human form)",
    "Vampiress (vampire form)",
    "Dwarf",
    "Daemon"
};

const char* class_name(uint16_t id) {
    if (id == 0 || id >= sizeof(CLASS_NAMES) / sizeof(CLASS_NAMES[0]))
        return nullptr;
    return CLASS_NAMES[id];
}

// Skill id mapping from CT dropdown list in the "Skill N" entries.
const char* SKILL_NAMES[34] = {
    "(none)",              //  0
    "Heavenly Magic",      //  1
    "Weapon Lore",         //  2
    "Long-handled Weapons",//  3
    "Sword Lore",          //  4
    "Axe Lore",            //  5
    "Dual Wielding",       //  6
    "Range Combat",        //  7
    "Agility",             //  8
    "Parrying",            //  9
    "Constitution",        // 10
    "Armor",               // 11
    "Meditation",          // 12
    "Blade Combat",        // 13
    "Magic Lore",          // 14
    "Fire Magic",          // 15
    "Water Magic",         // 16
    "Earth Magic",         // 17
    "Air Magic",           // 18
    "Moon Magic",          // 19
    "Vampirism",           // 20
    "Trading",             // 21
    "Riding",              // 22
    "Disarming",           // 23
    "Unarmed Combat",      // 24
    "Concentration",       // 25
    "Ballistics",          // 26
    "Trap Lore",           // 27
    "Bloodlust",           // 28
    "Weapon Technology",   // 29
    "Two-handed Weapons",  // 30
    "Dwarven Lore",        // 31
    "Hellpower",           // 32
    "Forge Lore"           // 33
};

const char* skill_name(uint8_t id) {
    if (id >= sizeof(SKILL_NAMES) / sizeof(SKILL_NAMES[0])) return nullptr;
    return SKILL_NAMES[id];
}

// --- The chain ------------------------------------------------------------
// CT Address: "Sacred.exe"+006D5C40
// CT Offsets: [4D8, 3AC, 4, 4]   (top→bottom; bottom is applied first)
//
// Resolved chain (offsets in apply-order, i.e. bottom-up):
//     base   = [image_base + 0x6D5C40]
//     a      = [base   + 0x004]
//     b      = [a      + 0x004]
//     player = [b      + 0x3AC]    ← struct base
//     fields = player + <field-offset>  (NO further deref — final offset is the read)

static uintptr_t resolve_player_struct() {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return 0;
    uintptr_t image_base = reinterpret_cast<uintptr_t>(exe);

    uintptr_t p = 0;
    if (!safe_read_ptr(image_base + 0x006D5C40, &p)) return 0;
    if (!safe_read_ptr(p + 0x004,                 &p)) return 0;
    if (!safe_read_ptr(p + 0x004,                 &p)) return 0;
    if (!safe_read_ptr(p + 0x3AC,                 &p)) return 0;
    return p;
}

// Public — see sdk.h. Used by runtime_triggers for direct struct writes
// (give_gold / has_item) without going through Sacred's bytecode
// interpreter.
uintptr_t hero_base() { return resolve_player_struct(); }

// Hero position in QUEST-MARKER / KompassPos space (0..~6300), NOT the
// resolve_player_struct() chain (that's a different finer-grained field,
// ~73000/149000 — proven by RE, see .claude/knowledge/re/quest_marker_pos.md).
// Path mirrors cObjectManager::getData (FUN_00603e30): the active-hero
// cCreature object's +0x1C (X) / +0x20 (Y).
//   om   = *(0x00AD5C40)            cObjectManager singleton
//   ctx  = *(0x0182EBE8)            active-context singleton
//   idx  = *(ctx + 0x14)            active hero index (1..16)
//   hero = *(*(om+4) + idx*4)       cCreature*
bool world_pos(int32_t* x, int32_t* y) {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return false;
    uintptr_t rebase = reinterpret_cast<uintptr_t>(exe) - 0x00400000;

    uintptr_t om = 0, ctx = 0, arr = 0, arr_end = 0, hero = 0;
    uint32_t  idx = 0;
    if (!safe_read_ptr(rebase + 0x00AD5C40, &om)  || !om)  return false;
    if (!safe_read_ptr(rebase + 0x0182EBE8, &ctx) || !ctx) return false;
    if (!safe_read<uint32_t>(ctx + 0x14, &idx))   return false;
    if (idx == 0 || idx > 0x10000)                return false;
    if (!safe_read_ptr(om + 4, &arr)     || !arr) return false;
    if (!safe_read_ptr(om + 8, &arr_end))         return false;
    if (idx >= (uint32_t)((arr_end - arr) >> 2))  return false;  // getData bound
    if (!safe_read_ptr(arr + idx * 4, &hero) || !hero) return false;

    int32_t hwx = 0, hwy = 0;
    if (!safe_read<int32_t>(hero + 0x1C, &hwx)) return false;
    if (!safe_read<int32_t>(hero + 0x20, &hwy)) return false;
    // cCreature+0x1C/+0x20 is raw HERO-WORLD space (~1.7e5). Quest
    // KompassPos (entry+0x10/+0x14, what set_kompass/set_marker take) is
    // the cell space: hero_world = (KompassPos + 0.5) * 53.66563034.
    // Invert so callers (F7 dump, on_tick distance vs KX,KY) work in the
    // SAME units as the marker. Constants RE'd from FUN_004a5980's
    // fild/fadd[0x890670]/fmul[0x890dc0] path (.claude/knowledge/re/quest_polish.md).
    const double B = 53.66563034057617;
    const double A = 0.5;
    // world coords are non-negative; +0.5 for round-to-nearest.
    if (x) *x = (int32_t)((double)hwx / B - A + 0.5);
    if (y) *y = (int32_t)((double)hwy / B - A + 0.5);
    return true;
}

}} // namespace sdk::player
