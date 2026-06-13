// engine/debug.cpp — player-state DIAGNOSTIC dumps, split out of the
// player_state.cpp god-file (refactor A4, continued). These are pure-read
// introspection helpers (no game-state mutation): walk the cObjectManager
// creature array / hero equip slots and log compact field lines for RE diffing.
// All four are declared in sdk.h (namespace sdk::player) and called from the
// sacred.* Lua bindings (npc_dump / scan_creatures / hero_weapon_dump /
// dump_vanilla_of). Zero coupling to player_state's static dialog/quest helpers.

#include "../sdk.h"
#include "player_internal.h"   // safe_read / safe_read_ptr (engine::mem forwarders)

namespace sdk { namespace player {

// --- diag: find & field-dump the first VANILLA creature of type+class ----
// So we can DIFF a real vanilla active soldier (type 257 Valorian, class
// 7) vs our spawn and find the exact extra bit we set wrong (the BFG
// combat-art + the red glow are RE errors, not bugs). Skips handles in
// [skip_lo,skip_hi] (our spawned scene NPCs).
void dump_vanilla_of(int filter_type, int ai_class, int skip_lo,
                     int skip_hi, const char* tag) {
    HMODULE exe = g_attach.exe_module; if (!exe) return;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om=0, arr=0, arr_end=0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return;
    if (!safe_read_ptr(om + 4, &arr) || !arr) return;
    if (!safe_read_ptr(om + 8, &arr_end) || arr_end <= arr) return;
    uint32_t n = (uint32_t)((arr_end - arr) >> 2);
    for (uint32_t i = 0; i < n; i++) {
        if ((int)i >= skip_lo && (int)i <= skip_hi) continue;
        uintptr_t c = 0;
        if (!safe_read_ptr(arr + (uintptr_t)i * 4, &c) || !c) continue;
        uint32_t t=0, st=0;
        if (!safe_read<uint32_t>(c + 0x10, &t)) continue;
        if ((int)(t & 0xFFFF) != filter_type) continue;
        if (!safe_read<uint32_t>(c + 0x1f0, &st)) continue;
        if ((int)st != ai_class) continue;
        npc_field_dump((int)i, tag);     // reuse the wide field dumper
        return;
    }
    sdk_log("[dump:%s] none found (type=%d class=%d)", tag,
            filter_type, ai_class);
}

// --- diag: dump the HERO's equipped items + their TYPE ids ---------------
// Pure reads (safe). Hero equip slots are cCreature+0x1A4+slot*4 (same map
// as player_state::read: +0x1D4=weapon_l slot0xC, +0x1D8=weapon_r 0xD,
// +0x1A4=helmet slot0 …). Each slot holds an ITEM OBJECT ref; resolve it
// via the om array and read its type (+0x10). Gives us the Vampiress
// starting-sword TYPE id empirically (to then equip onto guards).
void hero_weapon_dump() {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om = 0, ctx = 0, arr = 0, arr_end = 0, hero = 0;
    uint32_t idx = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return;
    if (!safe_read_ptr(reb + 0x0182EBE8, &ctx) || !ctx) return;
    if (!safe_read<uint32_t>(ctx + 0x14, &idx) || !idx) return;
    if (!safe_read_ptr(om + 4, &arr) || !arr) return;
    if (!safe_read_ptr(om + 8, &arr_end)) return;
    if (idx >= (uint32_t)((arr_end - arr) >> 2)) return;
    if (!safe_read_ptr(arr + (uintptr_t)idx * 4, &hero) || !hero) return;
    uint32_t htype = 0; safe_read<uint32_t>(hero + 0x10, &htype);
    sdk_log("[heroeq] hero idx=%u type=%u — equip slots (slot:ref->type):",
            idx, htype & 0xFFFF);
    for (int slot = 0; slot <= 0x12; ++slot) {
        uint32_t ref = 0;
        if (!safe_read<uint32_t>(hero + 0x1A4 + (uintptr_t)slot * 4, &ref))
            continue;
        if (!ref) continue;
        uintptr_t itm = 0; uint32_t itype = 0, iflags = 0;
        if ((uint32_t)ref < (uint32_t)((arr_end - arr) >> 2) &&
            safe_read_ptr(arr + (uintptr_t)ref * 4, &itm) && itm) {
            safe_read<uint32_t>(itm + 0x10, &itype);
            safe_read<uint32_t>(itm + 0x14, &iflags);
        }
        sdk_log("[heroeq]   slot 0x%02X: ref=%u -> obj=%p type=%u f14=%08X",
                slot, ref, (void*)itm, itype & 0xFFFF, iflags);
    }
}

// --- scan ALL live creatures (find a correct vanilla guard to copy) ------
// Walk the cObjectManager creature array and log a compact field line per
// live creature so we can identify a real vanilla guard (proactively
// fights enemies, friendly to player) and diff its config vs our spawn.
// filter_type<0 = all; else only that type id. Cap = how many to log.
void scan_creatures(int filter_type, int cap) {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om = 0, arr = 0, arr_end = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return;
    if (!safe_read_ptr(om + 4, &arr) || !arr) return;
    if (!safe_read_ptr(om + 8, &arr_end) || arr_end <= arr) return;
    uint32_t n = (uint32_t)((arr_end - arr) >> 2);
    int logged = 0;
    sdk_log("[scan] begin: %u slots, filter_type=%d cap=%d", n,
            filter_type, cap);
    for (uint32_t i = 0; i < n && logged < cap; i++) {
        uintptr_t c = 0;
        if (!safe_read_ptr(arr + (uintptr_t)i * 4, &c) || !c) continue;
        uint32_t t = 0;
        if (!safe_read<uint32_t>(c + 0x10, &t)) continue;
        int type = (int)(t & 0xFFFF);
        if (type <= 0 || type > 0x2000) continue;        // not a creature
        if (filter_type >= 0 && type != filter_type) continue;
        uint32_t st=0,fac=0,f14=0,hp=0,lvl=0,qb=0,p200=0,p204=0,
                 p4d4=0,p2b4=0;
        safe_read<uint32_t>(c + 0x1f0, &st);
        safe_read<uint32_t>(c + 0x1f4, &fac);
        safe_read<uint32_t>(c + 0x14,  &f14);
        safe_read<uint32_t>(c + 0x4d8, &hp);
        safe_read<uint32_t>(c + 0x24,  &lvl);
        safe_read<uint32_t>(c + 0x96,  &qb);
        safe_read<uint32_t>(c + 0x200, &p200);   // AI-ctrl word (we force 0x40200000)
        safe_read<uint32_t>(c + 0x204, &p204);   // AI sub (WakeUp copies +0x204->+0x208)
        safe_read<uint32_t>(c + 0x4d4, &p4d4);   // max HP
        safe_read<uint32_t>(c + 0x2b4, &p2b4);   // combat-art/flags cluster (+0x2b7 stationary)
        sdk_log("[scan] h=%u t=%d st=%08X fac=%08X f14=%08X hp=%u maxhp=%u "
                "lvl=%02X qb=%04X p200=%08X p204=%08X p2b4=%08X",
                i, type, st, fac, f14, hp, p4d4, lvl & 0xff, qb & 0xffff,
                p200, p204, p2b4);
        logged++;
    }
    sdk_log("[scan] end: logged=%d", logged);
}

// --- diagnostic: dump the render-suspect cCreature fields ----------------
// Both glow RE theories were falsified in-game. Stop guessing: log the
// actual fields so we can DIFF a glowing bound NPC vs a non-glowing guard
// and pin the real source. Call via sacred.npc_dump(handle,"tag").
void npc_field_dump(int handle, const char* tag) {
    uintptr_t c = npc_creature(handle);
    if (!c) { sdk_log("[dump:%s] h=%d -> no creature", tag ? tag : "", handle);
              return; }
    uint32_t v10=0,v14=0,vb0=0,vbc=0,vc0=0,vfc=0,v150=0,v1f0=0,v1f4=0,
             v200=0,v204=0,v244=0,v4d8=0,v588=0,v58c=0;
    uint16_t vfe=0,v152=0;
    safe_read<uint32_t>(c+0x10,&v10);   safe_read<uint32_t>(c+0x14,&v14);
    safe_read<uint32_t>(c+0xb0,&vb0);   safe_read<uint32_t>(c+0xbc,&vbc);
    safe_read<uint32_t>(c+0xc0,&vc0);   safe_read<uint32_t>(c+0xfc,&vfc);
    safe_read<uint16_t>(c+0xfe,&vfe);   safe_read<uint16_t>(c+0x152,&v152);
    safe_read<uint32_t>(c+0x150,&v150); safe_read<uint32_t>(c+0x1f0,&v1f0);
    safe_read<uint32_t>(c+0x1f4,&v1f4); safe_read<uint32_t>(c+0x200,&v200);
    safe_read<uint32_t>(c+0x204,&v204);
    safe_read<uint32_t>(c+0x244,&v244); safe_read<uint32_t>(c+0x4d8,&v4d8);
    safe_read<uint32_t>(c+0x588,&v588); safe_read<uint32_t>(c+0x58c,&v58c);
    sdk_log("[dump:%s] h=%d c=%p +10=%08X +14=%08X +b0=%08X +bc=%08X "
            "+c0=%08X +fc=%08X +fe=%04X +150=%08X +152=%04X +1f0=%08X "
            "+1f4=%08X +200=%08X +204=%08X +244=%08X(+245>>8=%02X) "
            "+4d8=%08X ring588=%08X/%08X",
            tag ? tag : "", handle, (void*)c, v10, v14, vb0, vbc, vc0,
            vfc, vfe, v150, v152, v1f0, v1f4, v200, v204, v244,
            (v244>>8)&0xff, v4d8, v588, v58c);
}

}} // namespace sdk::player
