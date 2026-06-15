// SacredSDK — read-only player-state inspector.
//
// All addresses + offsets are taken verbatim from the public Cheat Engine
// table "Sacred (Public) 1.0.CT" and verified against the version-2006-10-13
// Steam build. Image base is fixed at 0x00400000 (Sacred.exe has no ASLR),
// so RVAs are stable across runs.
//
// Pointer chain semantics — CE's offset list is applied bottom-up. For an
// "Address" of `Sacred.exe+RVA` and offsets [A, B, C, D]:
//
//     p = *(uintptr_t*)(image_base + RVA)
//     p = *(uintptr_t*)(p + D)        // last offset, applied first
//     p = *(uintptr_t*)(p + C)
//     p = *(uintptr_t*)(p + B)
//     value = *(T*)(p + A)            // top offset is the final read, NOT a deref
//
// All reads are guarded by IsBadReadPtr (slow but safe) so a torn chain
// during loading screens doesn't crash us — we just report `valid=false`.

#include "sdk.h"
#include "engine/addresses.h"   // Goal A1: centralized engine VAs (engine::addr::*)
#include "engine/offsets.h"     // Goal A1: centralized struct offsets (engine::off::*)
#include "engine/mem.h"         // Goal A1: SEH-safe accessors (engine::mem::*)
#include "engine/singletons.h"  // Goal A3: qm()/om()/ctx() canonical accessors
#include "engine/player_internal.h" // Goal A4: shared safe_* accessors for the split
#include "ports/engine/sacred_hash.h" // resource-name hash (dialog-text globalres probe)
#include <stdint.h>

namespace sdk { namespace player {

// --- Hero snapshot / world_pos / name tables MOVED to engine/hero.cpp (A4).
// class_name, skill_name, CLASS_NAMES, SKILL_NAMES, resolve_player_struct,
// hero_base, world_pos now live there (same sdk::player namespace). ---

// Teleport the active hero to KompassPos (kx, ky) — the SAME space F7
// dumps and questbook_set_kompass/marker take.
//
// This calls the ENGINE's own teleport, FUN_0054d9d0 (the function the
// Teleport/DirectTeleport FunkCode handlers use). It is __thiscall with
// ECX = the hero cCreature*, args (tileX, tileY, level, flag). Unlike a
// raw +0x1C/+0x20 write (which desynced the streaming sector → fade to
// black), this does the full move: collision/placement check, sector
// switch, trigger + follower fixup. Coordinates are tile / KompassPos
// units (proven by the engine's own 0x189c/0x188f endgame constants and
// FUN_006224b0 tile→world conversion inside it). level 0 = same world.
// Returns false if the hero chain isn't resolved yet OR the engine
// rejected the placement (returns 0 — non-destructive). Retry next tick.
typedef int(__thiscall* fn_engine_tp)(void* self, int x, int y, int level,
                                      int flag);

// Spawn a creature/NPC at KompassPos (kx,ky) via the engine's OWN create
// path — the exact recipe FUN_0054d9d0 uses:
//   pos = FUN_006224b0(this=&buf, 0, X, Y, 0)   build a position struct
//   handle = cObjectManager::create_005fba40(this=om, type, &buf, 0,1,0)
// `type` = creature class id (npc.lua, 1..714). Coords are KompassPos —
// the SAME space the working hero teleport used. Returns the new creature
// handle (>0) or 0 on failure. __thiscall fn-ptrs (ECX = this).
typedef void (__thiscall* fn_pos_build)(void* self, uint16_t a,
                                        uint32_t x, uint32_t y, uint8_t d);
typedef int  (__thiscall* fn_obj_create)(void* self, uint32_t type,
                                         void* pos, uint32_t p3,
                                         char p4, uint32_t p5);
// FUN_00635c40: __thiscall(ECX = cWorld, ushort* pos) — fills pos->sector
// from cWorld+0x284 grid[(X>>6)*0x80+(Y>>6)]; returns 0 if off-map.
typedef char (__thiscall* fn_sector_resolve)(void* world, void* pos);

// Spawn `type` RIGHT AT THE HERO — no sector resolution needed: the hero
// is, by definition, standing in a valid (sector,X,Y,level). We copy the
// hero cCreature's own position bytes VERBATIM into the pos struct (the
// exact encoding the engine itself uses for a placed creature: +0x18 u16
// sector, +0x1C/+0x20 X/Y, +0x24 u8 level) and create() with that. This
// sidesteps the parked-at-default failure of coordinate spawns until the
// cWorld sector-map singleton is found. Returns handle or 0.
int spawn_npc_here(int type) {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return 0;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om = 0, ctx = 0, arr = 0, arr_end = 0, hero = 0;
    uint32_t idx = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return 0;
    if (!safe_read_ptr(reb + 0x0182EBE8, &ctx) || !ctx) return 0;
    if (!safe_read<uint32_t>(ctx + 0x14, &idx) || !idx || idx >= 0x10000)
        return 0;
    if (!safe_read_ptr(om + 4, &arr) || !arr) return 0;
    if (!safe_read_ptr(om + 8, &arr_end)) return 0;
    if (idx >= (uint32_t)((arr_end - arr) >> 2)) return 0;
    if (!safe_read_ptr(arr + (uintptr_t)idx * 4, &hero) || !hero) return 0;

    uint16_t sector = 0; uint32_t hx = 0, hy = 0; uint8_t level = 0;
    if (!safe_read<uint16_t>(hero + 0x18, &sector)) return 0;
    if (!safe_read<uint32_t>(hero + 0x1C, &hx)) return 0;
    if (!safe_read<uint32_t>(hero + 0x20, &hy)) return 0;
    safe_read<uint8_t>(hero + 0x24, &level);

    fn_pos_build  build  = (fn_pos_build)(reb + 0x006224b0);
    fn_obj_create create = (fn_obj_create)(reb + 0x005fba40);
    int handle = 0;
    __try {
        uint8_t pos[16] = { 0 };
        // verbatim hero pos bytes — whatever encoding the engine uses
        build(pos, sector, hx, hy, level);
        handle = create((void*)om, (uint32_t)type, pos, 0, 1, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return handle;
    }
    return handle;
}

// Spawn `type` at KompassPos (kx,ky) by borrowing the HERO's sector +
// level (valid for anything in/near the hero's 64-tile cell — the
// storyline use case) and converting the target tile to hero-world units
// the same encoding spawn_npc_here proved works: world = (k+0.5)*53.6656.
// Sidesteps the failing FUN_00635c40 resolve. Returns handle or 0.
int spawn_npc_at(int type, int32_t kx, int32_t ky) {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return 0;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om = 0, ctx = 0, arr = 0, arr_end = 0, hero = 0;
    uint32_t idx = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return 0;
    if (!safe_read_ptr(reb + 0x0182EBE8, &ctx) || !ctx) return 0;
    if (!safe_read<uint32_t>(ctx + 0x14, &idx) || !idx || idx >= 0x10000)
        return 0;
    if (!safe_read_ptr(om + 4, &arr) || !arr) return 0;
    if (!safe_read_ptr(om + 8, &arr_end)) return 0;
    if (idx >= (uint32_t)((arr_end - arr) >> 2)) return 0;
    if (!safe_read_ptr(arr + (uintptr_t)idx * 4, &hero) || !hero) return 0;

    uint16_t sector = 0; uint8_t level = 0;
    if (!safe_read<uint16_t>(hero + 0x18, &sector)) return 0;
    safe_read<uint8_t>(hero + 0x24, &level);

    const double B = 53.66563034057617, A = 0.5;
    uint32_t wx = (uint32_t)(int32_t)(((double)kx + A) * B);
    uint32_t wy = (uint32_t)(int32_t)(((double)ky + A) * B);

    fn_pos_build  build  = (fn_pos_build)(reb + 0x006224b0);
    fn_obj_create create = (fn_obj_create)(reb + 0x005fba40);
    int handle = 0;
    __try {
        uint8_t pos[16] = { 0 };
        build(pos, sector, wx, wy, level);
        handle = create((void*)om, (uint32_t)type, pos, 0, 1, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return handle;
    }
    return handle;
}

int spawn_npc(int type, int32_t kx, int32_t ky) {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return 0;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return 0;

    fn_pos_build      build   = (fn_pos_build)(reb + 0x006224b0);
    fn_obj_create     create  = (fn_obj_create)(reb + 0x005fba40);
    fn_sector_resolve resolve = (fn_sector_resolve)(reb + 0x00635c40);

    // cWorld / sector-map singleton (runtime_spawn.md): *(0x00AD3560),
    // null-fallback to *(*(0x00AD5C40)) — both proven the same object.
    uintptr_t world = 0;
    safe_read_ptr(reb + 0x00AD3560, &world);
    if (!world) safe_read_ptr(om, &world);   // (*cObjectManager)[0]
    if (!world) return 0;

    // Recipe (mirrors FUN_004a2b40): build pos with INTEGER tile X/Y,
    // resolve the sector via cWorld, then create. resolve()==0 ⇒ the
    // tile is off-map / unloaded → abort (don't park a ghost).
    int handle = 0;
    __try {
        uint8_t pos[16] = { 0 };       // {u16 sector@0, i32 X@4, i32 Y@8, u8 lvl@12}
        build(pos, 0, (uint32_t)kx, (uint32_t)ky, 0);
        char ok = resolve((void*)world, pos);
        if (!ok) return 0;             // off-map: no spawn
        handle = create((void*)om, (uint32_t)type, pos, 0, 1, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return handle;
    }
    return handle;
}

// Resolve a creature handle (as returned by spawn_npc / engine) to its
// cCreature*. Chain: om = *(0x00AD5C40); arr = *(om+4); creature =
// *(arr + handle*4). Same array world_pos walks. 0 if out of range.
uintptr_t npc_creature(int handle) {
    HMODULE exe = g_attach.exe_module;
    if (!exe || handle <= 0) return 0;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om = 0, arr = 0, arr_end = 0, c = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return 0;
    if (!safe_read_ptr(om + 4, &arr) || !arr) return 0;
    if (!safe_read_ptr(om + 8, &arr_end)) return 0;
    if ((uint32_t)handle >= (uint32_t)((arr_end - arr) >> 2)) return 0;
    if (!safe_read_ptr(arr + (uintptr_t)handle * 4, &c) || !c) return 0;
    return c;
}

// Read type (+0x10), KompassPos (from +0x1C/+0x20 world), faction word
// (+0x1F4). Any out-pointer may be null. false if handle unresolvable.
bool npc_info(int handle, int* type, int32_t* kx, int32_t* ky,
              uint32_t* faction) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    const double B = 53.66563034057617, A = 0.5;
    bool ok = true;
    if (type) {
        uint32_t t = 0;
        ok &= safe_read<uint32_t>(c + 0x10, &t);
        *type = (int)(t & 0xFFFF);
    }
    if (kx || ky) {
        int32_t hwx = 0, hwy = 0;
        ok &= safe_read<int32_t>(c + 0x1C, &hwx);
        ok &= safe_read<int32_t>(c + 0x20, &hwy);
        if (kx) *kx = (int32_t)((double)hwx / B - A + 0.5);
        if (ky) *ky = (int32_t)((double)hwy / B - A + 0.5);
    }
    if (faction) ok &= safe_read<uint32_t>(c + 0x1F4, faction);
    return ok;
}

bool npc_set_faction(int handle, uint32_t faction) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    return safe_write<uint32_t>(c + 0x1F4, faction);
}

// Activate the creature's AI (the engine path the CreateNPC 0x12 "awake"
// opcode triggers). Without this a runtime-created creature just stands
// there even when hit. cCreature_WakeUp_0059f580 __thiscall(ECX=creature).
typedef void (__thiscall* fn_wake)(void* self);
bool npc_wake(int handle) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    __try { ((fn_wake)(reb + 0x0059F580))((void*)c); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Invulnerable / essential flag — the CreateNPC 0x01-payload opcode 0xa1
// path: cCreature `[2].spare |= 0x200000`  (byte off 2*8+4 = 0x14).
// `on=false` clears it. Read-modify-write.
bool npc_set_invulnerable(int handle, bool on) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    uint32_t v = 0;
    if (!safe_read<uint32_t>(c + 0x14, &v)) return false;
    v = on ? (v | 0x200000u) : (v & ~0x200000u);
    return safe_write<uint32_t>(c + 0x14, v);
}

// STATIONARY flag — CreateNPC op 0x6b path: byte at cCreature+0x2B7
// (Ghidra [0x56].spare+3) bit 0x08. Set => the creature holds its post
// (no patrol/wander); clear => free to roam.
bool npc_set_stationary(int handle, bool on) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    uint8_t b = 0;
    if (!safe_read<uint8_t>(c + 0x2B7, &b)) return false;
    b = on ? (uint8_t)(b | 0x08) : (uint8_t)(b & ~0x08);
    return safe_write<uint8_t>(c + 0x2B7, b);
}

// Stance / behavior. FUN_0052e420 __thiscall(ECX=creature, int mode, u32):
//   mode 0 -> creature+0x1F0 = class-default stance (FUN_0043adc0(type)):
//             a Skeleton then behaves like a real vanilla skeleton
//             (hostile), townsfolk neutral, etc. THE missing piece — a
//             runtime-created creature has +0x1F0 = 0 ⇒ aimless wander.
//   mode 1 -> creature+0x1F0 = value (explicit stance override).
typedef void (__thiscall* fn_stance)(void* self, int mode, uint32_t val);
bool npc_set_stance(int handle, int mode, uint32_t value) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    __try { ((fn_stance)(reb + 0x0052E420))((void*)c, mode, value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// ── Companions + dynamic NPC behavior ─────────────────────────────────
// RE: combat_init.md "## Companions + dynamic NPC behavior". All HIGH-
// confidence (raw-disasm field semantics + already-proven engine ABIs
// FUN_0052e420 / FUN_0059f580 / cObjectManager destroy). Hero player
// slot = *(ctx+0x14), ctx=*(0x0182EBE8) (same chain as world_pos).

static uint32_t hero_slot(uintptr_t reb) {
    uintptr_t ctx = 0; uint32_t idx = 0;
    if (!safe_read_ptr(reb + 0x0182EBE8, &ctx) || !ctx) return 0;
    if (!safe_read<uint32_t>(ctx + 0x14, &idx)) return 0;
    if (idx == 0 || idx > 0x10) return 0;          // valid player slot 1..16
    return idx;
}

// COMPANION (A-recipe): party-follow + fights for hero. cCreature+0x1F4
// bit 0x4 = "owner-substituted summon/pet"; +0x251 = owner handle (hero
// slot). FUN_00423480 then treats it AS the hero for friend/foe, and
// FUN_00542b20:1121 leashes it to the hero (SP, engine-driven, no
// script). +bit0 awake, clear 0x40000 peaceful; +0x1F0=7 ally; WakeUp.
bool npc_make_companion(int handle) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uint32_t hs = hero_slot(reb);
    if (!hs) { sdk_log("[companion] h=%d no hero slot", handle);
               return false; }
    uint32_t f = 0;
    __try {
        safe_write<uint32_t>(c + 0x251, hs);              // owner = hero
        if (safe_read<uint32_t>(c + 0x1F4, &f))
            safe_write<uint32_t>(c + 0x1F4,
                                 (f | 0x4u | 0x1u) & ~0x40000u);
        uint8_t b = 0;                                    // un-stationary
        if (safe_read<uint8_t>(c + 0x2B7, &b))
            safe_write<uint8_t>(c + 0x2B7, (uint8_t)(b & ~0x08));
        ((fn_stance)(reb + 0x0052E420))((void*)c, 1, 7);  // +0x1F0=7 ally
        ((fn_wake)(reb + 0x0059F580))((void*)c);          // WakeUp
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    sdk_log("[companion] h=%d -> hero slot %u (follow+fight)", handle, hs);
    return true;
}

// DISMISS (B-recipe, exact inverse): clear summon bit + owner so the
// engine stops substituting/leashing; independent neutral matrix class.
bool npc_dismiss(int handle) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uint32_t f = 0;
    __try {
        if (safe_read<uint32_t>(c + 0x1F4, &f))
            safe_write<uint32_t>(c + 0x1F4, f & ~0x4u);
        safe_write<uint32_t>(c + 0x251, 0);
        ((fn_stance)(reb + 0x0052E420))((void*)c, 1, 3);  // neutral
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    sdk_log("[companion] h=%d dismissed", handle);
    return true;
}

// COMPANION ROSTER PANEL (Q2, quest_lifecycle.md). The on-screen
// escort/companion list is driven by the cQuestMgr quest-NPC array at
// cQuestMgr+0x31c(begin)/+0x320(end), stride 0x34; each live entry
// (+0x14!=0) holds a std::vector of 0x2c-byte member sub-records at
// +0x1c(begin)/+0x20(end)/+0x24(cap). A creature joins via
// FUN_00450C50(__thiscall ECX=cCreature, i16 idx; ret 8 — sole
// persistent store *(i16)(c+0x94)=idx, rest is SP-skipped net) plus
// +0x200|=0x200, +0x96=quest_id, and one 0x2c sub-record
// {[0]=*(c+0xc) handle, [1]=0x100, [2](i16)=idx} pushed into the
// entry's vector. CRASH-SAFE CHOICE: push IN-PLACE only when the
// vector has spare capacity (end!=cap); if full we DON'T call the
// engine grow FUN_004B82E0 (its ABI is not pinned — guessing it = the
// realloc crashes we will not repeat) — we no-op (companions still
// follow/fight via the +0x1F4/+0x251 path; only the panel face is
// skipped). Slot = first live entry (+0x14!=0); scanned, not guessed.
typedef void (__thiscall* fn_roster_join)(void* c, int idx);  // 0x450C50

// Read-only diagnostic: dump the quest-NPC roster array (qm+0x31c, stride 0x34)
// so we can see whether ANY live entry exists and capture a vanilla entry's
// 0x34-byte layout (needed to natively create our own entry). Pure reads.
void roster_dump(const char* tag) {
    HMODULE exe = g_attach.exe_module; if (!exe) return;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t qm  = engine::singletons::qm(reb);
    uintptr_t b = 0, e = 0, cap = 0;
    bool okb = safe_read_ptr(qm + 0x31c, &b);
    bool oke = safe_read_ptr(qm + 0x320, &e);
    safe_read_ptr(qm + 0x324, &cap);
    int cnt = (okb && oke && b && e >= b) ? (int)((e - b) / 0x34) : -1;
    sdk_log("[roster-dump:%s] qm=%p +31c begin=%08X end=%08X cap=%08X count=%d",
            tag, (void*)qm, (unsigned)b, (unsigned)e, (unsigned)cap, cnt);
    if (cnt <= 0 || cnt > 4096) return;
    __try {
        for (int i = 0; i < cnt && i < 16; i++) {
            uintptr_t en = b + (uintptr_t)i * 0x34;
            uint32_t w[13];
            for (int k = 0; k < 13; k++) safe_read<uint32_t>(en + k * 4, &w[k]);
            uintptr_t vb = w[7], ve = w[8], vc = w[9];   // +0x1c/+0x20/+0x24
            int members = (vb && ve >= vb) ? (int)((ve - vb) / 0x2c) : -1;
            sdk_log("[roster-dump:%s] #%d @%08X live(+14)=%08X | +00=%08X +04=%08X "
                    "+08=%08X +10=%08X | vec b=%08X e=%08X c=%08X members=%d",
                    tag, i, (unsigned)en, w[5], w[0], w[1], w[2], w[4],
                    (unsigned)vb, (unsigned)ve, (unsigned)vc, members);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[roster-dump:%s] faulted", tag);
    }
}

bool npc_roster_add(int handle, int quest_id) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t qm  = engine::singletons::qm(reb);
    roster_dump("add");                            // capture array state every add
    uintptr_t b = 0, e = 0;
    if (!safe_read_ptr(qm + 0x31c, &b) || !b) {
        sdk_log("[roster] h=%d ABORT: qm+0x31c array is NULL/empty (no quest-NPC "
                "slot exists — needs native entry creation)", handle);
        return false;
    }
    if (!safe_read_ptr(qm + 0x320, &e) || e < b) return false;
    int cnt = (int)((e - b) / 0x34);
    if (cnt <= 0 || cnt > 4096) {
        sdk_log("[roster] h=%d ABORT: array count=%d (empty)", handle, cnt);
        return false;
    }
    bool done = false;
    __try {
        for (int i = 0; i < cnt; i++) {
            uintptr_t entry = b + (uintptr_t)i * 0x34;
            uint32_t live = 0;
            if (!safe_read<uint32_t>(entry + 0x14, &live) || live == 0)
                continue;                         // need a live slot
            uintptr_t vb = 0, ve = 0, vc = 0;
            if (!safe_read_ptr(entry + 0x1c, &vb) ||
                !safe_read_ptr(entry + 0x20, &ve) ||
                !safe_read_ptr(entry + 0x24, &vc)) continue;
            if (ve == vc || ve < vb) continue;    // no spare cap -> skip
            // identity stamps (HIGH: capstone-exact single stores)
            ((fn_roster_join)(reb + 0x00450C50))((void*)c, i); // +0x94=i
            safe_write<int16_t>(c + 0x96, (int16_t)quest_id);
            uint32_t v200 = 0;
            if (safe_read<uint32_t>(c + 0x200, &v200))
                safe_write<uint32_t>(c + 0x200, v200 | 0x200u);
            // push the 0x2c sub-record IN PLACE (spare cap confirmed).
            uint32_t hc = 0; safe_read<uint32_t>(c + 0x0c, &hc);
            for (int k = 0; k < 0x2c; k += 4)
                safe_write<uint32_t>(ve + k, 0);
            safe_write<uint32_t>(ve + 0x00, hc);   // member handle
            safe_write<uint32_t>(ve + 0x04, 0x100);
            safe_write<int16_t> (ve + 0x08, (int16_t)i);
            safe_write<uint32_t>(entry + 0x20,
                                 (uint32_t)(ve + 0x2c));        // end+=0x2c
            sdk_log("[roster] h=%d -> array slot %d (q=%d) panel-add",
                    handle, i, quest_id);
            done = true;
            break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!done) sdk_log("[roster] h=%d no usable slot (panel skipped; "
                       "follow/fight still active)", handle);
    return done;
}

// REMOVE: inverse — clear +0x200&0x200, +0x94=-1, and compact the
// member's 0x2c sub-record out of its array-entry vector (copy the
// last record over it, end-=0x2c). All in-place, SEH-guarded.
bool npc_roster_remove(int handle) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t qm  = engine::singletons::qm(reb);
    int16_t idx = -1;
    __try {
        safe_read<int16_t>(c + 0x94, &idx);
        uint32_t v200 = 0;
        if (safe_read<uint32_t>(c + 0x200, &v200))
            safe_write<uint32_t>(c + 0x200, v200 & ~0x200u);
        uint32_t hc = 0; safe_read<uint32_t>(c + 0x0c, &hc);
        uintptr_t b = 0;
        if (idx >= 0 && safe_read_ptr(qm + 0x31c, &b) && b) {
            uintptr_t entry = b + (uintptr_t)idx * 0x34;
            uintptr_t vb = 0, ve = 0;
            if (safe_read_ptr(entry + 0x1c, &vb) &&
                safe_read_ptr(entry + 0x20, &ve) && ve > vb) {
                for (uintptr_t p = vb; p + 0x2c <= ve; p += 0x2c) {
                    uint32_t rh = 0;
                    if (safe_read<uint32_t>(p, &rh) && rh == hc) {
                        uintptr_t last = ve - 0x2c;
                        if (p != last)
                            for (int k = 0; k < 0x2c; k += 4) {
                                uint32_t w = 0;
                                safe_read<uint32_t>(last + k, &w);
                                safe_write<uint32_t>(p + k, w);
                            }
                        safe_write<uint32_t>(entry + 0x20,
                                             (uint32_t)last);  // end-=0x2c
                        break;
                    }
                }
            }
        }
        safe_write<int16_t>(c + 0x94, (int16_t)-1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    sdk_log("[roster] h=%d removed from panel", handle);
    return true;
}

// DESPAWN (C-2): clean engine removal. DelNPC 0x37 handler FUN_00497f80
// calls cObjectManager destroy leaf @0x005FBDB0 __cdecl(handle,1,0,1).
typedef void (__cdecl* fn_destroy)(int handle, int, int, int);
bool npc_despawn(int handle) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;                           // already gone
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    __try { ((fn_destroy)(reb + 0x005FBDB0))(handle, 1, 0, 1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    sdk_log("[npc] h=%d despawned", handle);
    return true;
}

// SET DISPOSITION (C-1): +0x1F0 matrix class via FUN_0052e420(1,val),
// then WakeUp re-aggro so it takes effect mid-game. Caller passes the
// matrix class value (hostile-to-hero=2, ally=7, neutral/immune=13).
bool npc_set_disposition(int handle, uint32_t matrix_class) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    __try {
        ((fn_stance)(reb + 0x0052E420))((void*)c, 1, matrix_class);
        ((fn_wake)(reb + 0x0059F580))((void*)c);          // re-aggro
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    sdk_log("[npc] h=%d disposition -> matrix class %u", handle,
            matrix_class);
    return true;
}

// Set level/rank. CORRECTED (combat_init.md): FUN_0044ddc0 is the
// facing/orientation setter (cos/sin → +0x70/74/78), NOT level — that was
// an npc_model.md erratum and is why "level had no effect". Level/rank is
// the plain u8 at cCreature+0x24. HP auto-scales from it once the creature
// is an active combatant (see npc_make_combatant).
bool npc_set_level(int handle, int level) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    if (level < 1) level = 1; if (level > 255) level = 255;
    return safe_write<uint8_t>(c + 0x24, (uint8_t)level);
}

// Turn a bare cObjectManager::create'd creature into a REAL combatant by
// replaying CreateNPC's post-create block (combat_init.md §2), which the
// bare spawn path skips — this is why our NPCs were 1-hit and passive:
//   B  level         *(u8*)(c+0x24) = level   (HP scales from this)
//   C  AI class       FUN_0052e420(ECX=c, 1, ai_class) → c+0x1F0=ai_class
//   D  faction commit *(u32*)(c+0x1F4) = 1   (bit0 awake; NO 0x40000
//                       'peaceful' bit → stays proactive)
//   E  WakeUp         FUN_0059f580(ECX=c)  (needs +0x200&0x40000==0,+0xfc==0)
//   F  arm AI ctrl    *(u32*)(c+0x200) = 0x40200000
//   +  clear STATIONARY (+0x2B7 bit8) so it can move to engage
// ai_class (hostility-matrix class @0x00890A30, [A*16+B]; CORRECTED
// polarity: matrix byte 0x00 = A ATTACKS B, 0x01 = friendly/ignore —
// the prior note had it inverted, which is why class 2 wrongly attacked
// the hero):
//   3 (or 7) = ALLY DEFENDER — attacks monsters, NEVER the hero/allies
//       (hero is class 1; M[3][1]=friendly). This is the correct value
//       and matches CreateNPC's no-side default (00482510:1114).
//   2 = a MONSTER class (hostile to the class-1 hero) — do NOT use for allies.
//   1 = hero/player cluster; 13 = immune non-combatant (town/quest).
typedef void (__thiscall* fn_stance2)(void* self, int mode, uint32_t val);
typedef void (__thiscall* fn_wake2)(void* self);
bool npc_make_combatant(int handle, int level, int ai_class) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    if (level < 1) level = 1; if (level > 255) level = 255;
    // CORRECTED from a live diff vs a real vanilla Valorian Soldier
    // (type 257, class 7): the combat_init.md reconstruction had two
    // RE errors that caused the Seraphim-BFG combat-art + the red glow:
    //   - +0x1F4 must be 0x00400000 (the active-soldier faction value),
    //     NOT 0x1. The wrong value mis-drove faction/attack selection.
    //   - +0x200 must stay 0x00000000. We were forcing the magic
    //     0x40200000 ("AI arm") — that is the extra bit that hung the
    //     Seraphim energy combat-art + glow on the creature. Vanilla
    //     soldiers have +0x200 == 0 in steady state; WakeUp alone
    //     activates the AI (it sets +0xfe / copies +0x204->+0x208).
    __try {
        *(uint8_t*)(c + 0x24) = (uint8_t)level;                 // B level
        ((fn_stance2)(reb + 0x0052E420))((void*)c, 1,
                                         (uint32_t)ai_class);    // C +0x1F0
        *(uint32_t*)(c + 0x1F4) = 0x00400000;   // D faction (vanilla value)
        // WakeUp precondition: +0x200 bit 0x40000 clear (vanilla +0x200=0)
        uint32_t f200 = 0;
        if (safe_read<uint32_t>(c + 0x200, &f200) && (f200 & 0x40000u))
            safe_write<uint32_t>(c + 0x200, f200 & ~0x40000u);
        ((fn_wake2)(reb + 0x0059F580))((void*)c);                // E WakeUp
        // F: do NOT write +0x200 (leave engine/vanilla 0 — the RE error).
        uint8_t b = 0;                                           // un-stationary
        if (safe_read<uint8_t>(c + 0x2B7, &b))
            safe_write<uint8_t>(c + 0x2B7, (uint8_t)(b & ~0x08));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// ===========================================================================
// Storyline runtime layer (RE: .claude/knowledge/re/quest_storyline.md, items.md,
// triggers_dialog_move.md). All additive, __try-guarded, non-destructive.
// cQuestMgr singleton = 0x00AACF80 (ECX in every quest/dialog handler).
// ===========================================================================

// --- B) custom NPC display name -------------------------------------------
// The name a quest/dialog NPC shows is NOT on cCreature; it lives, keyed by
// the creature handle at entry+0, in two cQuestMgr vectors:
//   NameArrA : begin qm+0x358 end qm+0x35c stride 0x44  name @ entry+0x04
//   DlgNPC   : begin qm+0x755c end qm+0x7560 stride 0x50  name @ entry+0x04
// We overwrite the in-place fixed buffer of whichever entry already keys
// this handle (engine-faithful: it finds entries the same way, entry[0]==h).
// NOTE: a *purely* runtime-spawned creature (cObjectManager::create, not the
// FunkCode CreateNPC handler) usually has NO such entry yet, so this is a
// no-op for it until a DlgNPC entry exists (open item — see the report).
// Returns true only if an entry was found and rewritten.
static bool qm_name_write(uintptr_t begin_ptr, uintptr_t end_ptr,
                          unsigned stride, int handle, const char* name) {
    uintptr_t b = 0, e = 0;
    if (!safe_read_ptr(begin_ptr, &b) || !b) return false;
    if (!safe_read_ptr(end_ptr,   &e) || e < b) return false;
    for (uintptr_t p = b; p + stride <= e; p += stride) {
        uint32_t key = 0;
        if (!safe_read<uint32_t>(p, &key)) break;
        if (key == (uint32_t)handle) {
            char* dst = (char*)(p + 4);
            unsigned cap = stride - 4;          // bytes available for the name
            if (IsBadWritePtr(dst, cap)) return false;
            unsigned i = 0;
            for (; name[i] && i < cap - 1; ++i) dst[i] = name[i];
            dst[i] = 0;
            return true;
        }
    }
    return false;
}

bool set_npc_name(int handle, const char* name) {
    HMODULE exe = g_attach.exe_module;
    if (!exe || !name) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t qm = engine::singletons::qm(reb);
    bool any = false;
    __try {
        // DlgNPC (the dialog/QUESTNPC/tooltip array — stride 0x50)
        any |= qm_name_write(qm + 0x755c, qm + 0x7560, 0x50, handle, name);
        // NameArrA (CreateNPC name buffer — stride 0x44)
        any |= qm_name_write(qm + 0x358,  qm + 0x35c,  0x44, handle, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return any;
}

// --- C) overhead quest-giver marker ---------------------------------------
// CORRECTED (2026-05-16, see quest_storyline.md "Red-FX side-effect"):
//  * The earlier recipe was WRONG. cCreature+0x200 bits 0x1000/0x2000/
//    0x4000/0x4000000 are NPC CLASS-IDENTITY flags (smith / trader /
//    combat-arts-MASTER / trader2) — CreateNPC sets +0x200|0x4000 for the
//    Master-of-Combat-Arts class. Writing it re-classed the NPC as a
//    combo-arts master ⇒ its native combo icon + the trainer's red/orange
//    swirl aura. And cCreature+0x14 bit 0x80000 is overloaded: the 3D
//    model renderer (FUN_0044b230) turns it into a particle swirl. So
//    NEITHER cCreature write is correct — do NOT poke +0x200 or +0x14.
//  * Vanilla quest-givers set ONLY the DlgNPC entry sprite field
//    (entry+0x48) via SetIcon (FUN_004a1a50): value 0x0b = "has quest /
//    talk to me" bubble (NPC_DIALOG_02.TGA), 0x08 = cleared. The renderer
//    shows it when the engine has bound the NPC (it sets +0x14 itself).
//  * A purely runtime-spawned NPC has NO DlgNPC entry (only NameArrA) —
//    same open item as set_npc_name. So this is the engine-faithful,
//    side-effect-free write IF an entry exists, else an honest no-op
//    (returns false). It will NEVER corrupt the NPC again.
// DlgNPC: begin *(qm+0x755c) end *(qm+0x7560) stride 0x50, key@+0=handle,
// sprite@+0x48. qm (cQuestMgr) = 0x00AACF80.
// "?!" combo quest marker. RE re-evaluated 2026-05-16
// (quest_storyline.md "Yellow \"?!\" marker — re-evaluated"): the engine
// has exactly ONE exclam+question glyph = selector case 0x22 ->
// npc_dialog_combo.tga (no yellow variant exists; color is baked in the
// TGA; vanilla secondary quests actually reuse "!" 0x0b). The marker
// glyph selector FUN_00499e90 priority: cCreature+0x200 bit 0x4000 ->
// case 4 -> glyph 0x22, and that case WINS over the DlgNPC entry+0x48
// path (which has an unresolved objIdx-range MED). The old "+0x200 =>
// red aura" ban was the DISPROVEN glow theory; the real glow is the
// invuln ward FX (+0x14&0x200000), unrelated. +0x200&0x4000's ONLY
// readers are this selector and the minimap quest-dot — no FX/combat/
// behaviour reader. So for a (stationary, immortal) quest NPC this is
// fully side-effect-free. Recipe: set cCreature+0x200|=0x4000
// (deterministic "?!", no BP) AND DlgNPC entry+0x48=0x22 (engine-
// faithful, mirrors vanilla FUN_004a1a50); ensure the +0x14&0x80000
// draw gate (bind already sets it). on=false reverts both.
static uintptr_t dlg_entry_by_objidx(uintptr_t reb, int handle,
                                     int* out_idx, int* out_cnt);

bool npc_quest_icon(int handle, bool on) {
    HMODULE exe = g_attach.exe_module;
    if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t qm  = engine::singletons::qm(reb);
    // Overhead-icon atlas DEFINITIVE (quest_storyline.md, textures
    // extracted from pak/texture.pak — ground truth): the genuine "?!"
    // is NPC_DIALOG_01.TGA, selected by DlgNPC entry+0x48 = 0x0A (vanilla
    // uses 0x0a 359x for "?!"). 0x0B = "!" (NPC_DIALOG_02), 0x08 = off.
    // NEVER write cCreature+0x200 — those are CLASS icons (0x4000 = the
    // yellow combat-arts-MASTER figure, the earlier wrong result).
    bool any = false;
    uintptr_t c = npc_creature(handle);
    if (c) {                                   // ensure the draw gate only
        __try {
            uint32_t v14 = 0;
            if (on && safe_read<uint32_t>(c + 0x14, &v14))
                safe_write<uint32_t>(c + 0x14, v14 | 0x80000u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    // Write entry+0x48 at the entry the SELECTOR reads = by-objIdx
    // (cCreature+0x245), NOT by-handle scan — those can differ, which is
    // why the marker silently vanished (engine read a different entry).
    (void)qm;
    uintptr_t oe = dlg_entry_by_objidx(reb, handle, nullptr, nullptr);
    if (oe) {
        __try {
            if (safe_write<uint32_t>(oe + 0x48, on ? 0x0Au : 0x08u))
                any = true;                               // 0x0A = "?!"
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return any;
}

// --- spawn a world ITEM ----------------------------------------------------
// items.md big result: items and creatures share ONE create path and ONE
// type-id space. Spawning a pickup-able ground item is EXACTLY the proven
// creature recipe with an item type id — so this just forwards to the
// hero-sector spawn that already works. `type` = an item type id.
int spawn_item(int type, int32_t kx, int32_t ky) {
    return spawn_npc_at(type, kx, ky);
}

// --- teleport ANY npc (engine path, non-destructive) ----------------------
// FUN_0054d9d0 is __thiscall(ECX=creature, x, y, level, flag); set_world_pos
// already proves it for the hero in KompassPos units. The Teleport(0x2e)
// FunkCode handler uses this same function on the target creature, so the
// only change is ECX = the NPC's cCreature*. Returns false if the engine
// rejected the placement (returns 0 — non-destructive, retry next tick).
bool npc_teleport(int handle, int32_t kx, int32_t ky) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    int rv = 0;
    __try {
        rv = ((fn_engine_tp)(reb + 0x0054D9D0))((void*)c,(int)kx,(int)ky,0,1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return rv != 0;
}

// --- equip an item into an NPC slot (EXPERIMENTAL) ------------------------
// Manual two-step from items.md §3/§6: create the item object position-less
// via cObjectManager::create_005fb530(ECX=om, type,0,1,0) then
// cCreature::equipment_equip_00555e00(ECX=creature, slot, itemRef, sendNet).
// Slot map cross-confirmed by the hero equipment table in read():
//   0=helmet .. 0xC=weapon_l 0xD=weapon_r .. 0x12=mount  (cCreature+0x1A4+slot*4)
// `item_type` = item type id (same id space as creatures). Guarded; if the
// create_005fb530 arity guess is wrong the __try contains it (returns false).
// Confidence MED — do not call on the live campaign until BP-confirmed.
typedef int (__thiscall* fn_obj_create_npos)(void* self, uint32_t type,
                                             uint32_t a3, char a4,
                                             uint32_t a5);
typedef int (__thiscall* fn_equip)(void* self, uint32_t slot, int itemRef,
                                   char sendNet);
bool npc_equip(int handle, int item_type, int slot) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t om = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return false;
    __try {
        fn_obj_create_npos mk = (fn_obj_create_npos)(reb + 0x005FB530);
        fn_equip eq           = (fn_equip)(reb + 0x00555E00);
        int itemRef = mk((void*)om, (uint32_t)item_type, 0, 1, 0);
        if (!itemRef) return false;
        eq((void*)c, (uint32_t)slot, itemRef, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// NOTE: the diagnostic dumps dump_vanilla_of / hero_weapon_dump /
// scan_creatures / npc_field_dump moved to engine/debug.cpp (refactor A4).

// --- set creature HP -----------------------------------------------------
// cCreature+0x4d8 = current HP (PROVEN: zeroing it made the captain a
// "dead but still talks" corpse). Runtime-spawned creatures get weak
// default stats (the FunkCode CreateNPC path does Balance.bin stat init we
// skip), so soldiers die in one hit. Bump current HP; we also probe a few
// neighbour dwords for the max-HP field and raise it too so the engine
// doesn't instantly clamp/regen back down. Returns false if unresolved.
bool npc_set_hp(int handle, int hp) {
    uintptr_t c = npc_creature(handle);
    if (!c) return false;
    bool ok = false;
    __try {
        // combat_init.md: +0x4d4 = MAX HP (the clamp source) — writing
        // only +0x4d8 (current) gets clamped back down next tick (the
        // "dies in one hit"). Write the whole vitality pair.
        ok  = safe_write<uint32_t>(c + 0x4d4, (uint32_t)hp);   // max
        ok &= safe_write<uint32_t>(c + 0x4d8, (uint32_t)hp);   // current
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ok;
}

// --- spawn via the engine's OWN CreateNPC handler (the right way) --------
// Strategic pivot (npc_templates.md): instead of reconstructing combat
// init by poking cCreature bits (endless edge bugs: BFG/glow/1HP/no-
// retaliate), synthesize a dev-authored CreateNPC TLV record from
// npc_templates.lua and let the ENGINE's own FUN_00482510 spawn+init it
// — type-correct HP/AI/faction/combat-arts, exactly like a hand-placed
// dev NPC. FUN_00482510 = __thiscall, ECX = the cQuestMgr/interpreter
// context (exe_base+0x00AACF80, same ctx dlgnpc_bind already drives),
// stack arg = on-disk TLV record buffer (cursor self-inits to 4).
// Header must be `01 00 <size&0xff>` (size guard reads u16@+2 native-LE;
// records <256 B). `payload` = npc_templates M.build() bytes
// (flags 0x00 + opcode stream + END). want_type = the template's
// creature type id, used to disambiguate the new handle. Returns the new
// creature handle (best-effort) or 0.
// ── Runtime dialog arming (R-B) ───────────────────────────────────────
// Replay a Dialog/Button/Sound TLV record through the engine's OWN
// record dispatcher FUN_00475680, exactly like vanilla/save/MP. ABI is
// disasm-pinned (dialog_runtime.md "Dialog-arming call ABI — pinned"):
// __thiscall, ECX=qm=exe+0x00AACF80, SIX stack args, ret 0x18.
//   p1=buffer base, p2=&cursor(int; =0 → record at buf+0; engine
//   advances *cursor by the BE size), p3=-1, p4=0 (no net),
//   p5=0 (no TalkTo pre-bind), p6=0. Modeled on the proven
//   createnpc_engine invocation (same __thiscall/ECX=qm/SEH pattern).
// Record framing: tag:u8 | size:u16 BE incl 3-byte header | payload,
// payload[0]=0x00 lead byte, then opcode-1 ASCIIZ fields, 0x00 END.
// Do NOT pre-resolve text via FUN_00672740 (crashes pre-cache); the
// dispatcher's own FUN_00472bc0 parse resolves the global.res key at
// replay time — pass the BARE registered name (no "res:" prefix).
typedef int (__thiscall* fn_funk_dispatch)(
    void* qm, const void* buf, int* cursor,
    int p3, int p4, char p5, int p6);

static size_t dlg_put_rec(uint8_t* o, uint8_t tag,
                          const char* s1, const char* s2) {
    size_t p = 3; o[p++] = 0x00;                       // lead flags byte
    o[p++] = 0x01; while (s1 && *s1) o[p++] = (uint8_t)*s1++; o[p++] = 0;
    if (s2) { o[p++] = 0x01; while (*s2) o[p++] = (uint8_t)*s2++;
              o[p++] = 0; }
    o[p++] = 0x00;                                     // END
    o[0] = tag; o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)(p & 0xff);
    return p;                                          // == total length
}

// Build a tag-0x03 DialogShow record: field-1 = the text key (must carry the
// "res:" prefix so FUN_00472bc0 case 1 takes the global.res resolve branch),
// field-9 = the DlgNPC bind name, trailing 0x39 = button/flag field, then END.
// The walker (FUN_00475680) self-resolves field-1 res:NAME through
// FUN_006726f0->FUN_0080e780->sacred_hash->global.res — so the NPC shows OUR
// baked text with no content-handle hack. See .claude/knowledge/re/
// dialog_impl_plan.md (verified against 1208 live tag-0x03 records).
static size_t dlg_put_dialog_rec(uint8_t* o, const char* res_key,
                                 const char* dlg_name) {
    size_t p = 3; o[p++] = 0x00;                        // lead flags byte
    o[p++] = 0x01; while (res_key && *res_key) o[p++] = (uint8_t)*res_key++;
    o[p++] = 0;                                         // fid 1: res:<KEY>
    if (dlg_name && *dlg_name) {
        o[p++] = 0x09; while (*dlg_name) o[p++] = (uint8_t)*dlg_name++;
        o[p++] = 0;                                     // fid 9: DlgNPC name
    }
    o[p++] = 0x39;                                      // button/flag field '9'
    o[p++] = 0x00;                                      // END
    o[0] = 0x03; o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)(p & 0xff);
    return p;
}

// p4 = the speaking NPC's cCreature* (dispatcher does
// ebp=RTDynamicCast<cCreature*>(p4); FUN_0048f9e0 skips ENTIRELY if that
// is 0 — the old p4=0 bug = silent no-op). p5 = TalkTo pre-bind gate
// (1 = run it, needed for an interactive bind). cursor=0 in-memory;
// p3=-1 only matters for tag-6/0x10 (irrelevant to 0x1f/0x68).
static bool funk_replay_one(uintptr_t reb, const uint8_t* rec, size_t n,
                            void* p4_creature, char p5) {
    void* qm = (void*)(engine::singletons::qm(reb));
    uint8_t buf[0x208] = {0};                          // < 0x800 walker cap
    if (n == 0 || n > sizeof(buf)) return false;
    for (size_t k = 0; k < n; ++k) buf[k] = rec[k];
    int cursor = 0;
    __try {
        ((fn_funk_dispatch)(reb + 0x00475680))(
            qm, buf, &cursor, -1, (int)(uintptr_t)p4_creature, p5, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// global.res resource-name hash = engine FUN_0080e780. EXACT port of
// custom/lua/lib/text.lua `sacred_hash` (verified vs 823 ids + the
// working captain NAME). The resolvable content handle the engine's
// dialog GUI expects in entry+0x4c is `sacred_hash(name) | 0x80000000`
// (FUN_006726f0: high bit set -> globalres[h & 0x7fffffff]). Computed
// here so we do NOT call the engine resolver FUN_00672740 (it crashes
// uncatchably if hit before its cache exists).
static uint32_t sacred_hash(const char* s) {
    const uint32_t MOD = 999999991u, MUL = 113u;
    uint32_t h = 0;
    for (; s && *s; ++s) {
        uint32_t oc = (uint8_t)*s;
        if (oc >= 0x61 && oc <= 0x7a) oc -= 0x20;          // toupper
        uint32_t prod = (uint32_t)(((uint64_t)h * MUL) & 0xFFFFFFFFu);
        uint32_t su = (oc + prod) & 0xFFFFFFFFu;
        int64_t si = (su >= 0x80000000u)
                       ? (int64_t)su - 0x100000000LL : (int64_t)su;
        int64_t r = (si >= 0) ? (si % MOD) : -((-si) % MOD);
        if (r < 0) r += 0x100000000LL;
        h = (uint32_t)(r & 0xFFFFFFFFu);
    }
    return h & 0x7FFFFFFFu;
}

// The DlgNPC entry the ENGINE actually reads for markers/dialog is
// indexed by objIdx = *(u8/u32)(cCreature+0x245) (selector FUN_00499e90
// case 6: qm+0x755c + objIdx*0x50), NOT by scanning entry+0==handle.
// Returns that entry ptr (validated < count), 0 if objIdx out of range.
static uintptr_t dlg_entry_by_objidx(uintptr_t reb, int handle,
                                     int* out_idx, int* out_cnt) {
    uintptr_t qm = engine::singletons::qm(reb), b = 0, e = 0;
    if (out_idx) *out_idx = -1;
    if (out_cnt) *out_cnt = -1;
    uintptr_t c = npc_creature(handle);
    if (!c) return 0;
    uint32_t idx = 0;
    if (!safe_read<uint32_t>(c + 0x244, &idx)) return 0;
    idx = (idx >> 8) & 0xff;                       // +0x245 byte
    if (out_idx) *out_idx = (int)idx;
    if (!safe_read_ptr(qm + 0x755c, &b) || !b) return 0;
    if (!safe_read_ptr(qm + 0x7560, &e) || e < b) return 0;
    int cnt = (int)((e - b) / 0x50);
    if (out_cnt) *out_cnt = cnt;
    if ((int)idx < 0 || (int)idx >= cnt) return 0;
    return b + (uintptr_t)idx * 0x50;
}

// Diagnostic: compare the by-objIdx entry (what the engine reads) vs
// the by-handle entry (what we used to write). Logs both + objIdx,
// count, by-handle index, cCreature+0x14 (gate bit 0x80000), +0xc.
static void dlg_probe(uintptr_t reb, int handle, const char* when) {
    uintptr_t qm = engine::singletons::qm(reb), b = 0, e = 0;
    uint32_t o48 = 0xFFFFFFFF, o4c = 0xFFFFFFFF;       // by-objIdx entry
    uint32_t h48 = 0xFFFFFFFF, h4c = 0xFFFFFFFF;       // by-handle entry
    uint32_t v14 = 0xFFFFFFFF, vc = 0xFFFFFFFF;
    int hidx = -1, oidx = -1, cnt = -1;
    uintptr_t c = npc_creature(handle);
    if (c) { safe_read<uint32_t>(c + 0x0c, &vc);
             safe_read<uint32_t>(c + 0x14, &v14); }
    uintptr_t oe = dlg_entry_by_objidx(reb, handle, &oidx, &cnt);
    if (oe) { safe_read<uint32_t>(oe + 0x48, &o48);
              safe_read<uint32_t>(oe + 0x4c, &o4c); }
    if (safe_read_ptr(qm + 0x755c, &b) && b &&
        safe_read_ptr(qm + 0x7560, &e) && e >= b) {
        __try {
            int i = 0;
            for (uintptr_t p = b; p + 0x50 <= e; p += 0x50, ++i) {
                uint32_t key = 0;
                if (!safe_read<uint32_t>(p, &key)) break;
                if (key == (uint32_t)handle) {
                    hidx = i;
                    safe_read<uint32_t>(p + 0x48, &h48);
                    safe_read<uint32_t>(p + 0x4c, &h4c);
                    break;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    sdk_log("[dlg_probe:%s] h=%d +14=%08X +0xc=%08X | objIdx=%d cnt=%d "
            "byObj(+48=%08X +4c=%08X) | hIdx=%d byHnd(+48=%08X +4c=%08X)",
            when, handle, v14, vc, oidx, cnt, o48, o4c, hidx, h48, h4c);
}

// FUN_00463240 = the self-contained TalkTo binder ("Talk_%s_Dlg_%d").
// __thiscall, ECX=qm, 4 stack args, ret 0x10 (dialog_runtime.md
// "Talk-route — making the window open"). arg1 = creature REFERENCE KEY
// (FUN_0044a1c0(cCreature), NOT the raw handle), arg2 = DlgNPC name
// (log-only), arg3 = DlgNPC index (= cCreature+0x245, MUST be >=1 so the
// unconditional +0x14|0x80000 at the end keeps the gate ON), arg4 =
// SelfTriggerQuest id (0 = safe, %d only). It sets DlgNPC[arg3]+0x4c =
// *(cCreature+0xc), stamps +0x245, registers the talk block, and
// re-asserts the draw gate — i.e. the exact talk-route a vanilla story
// NPC gets via tag-6 that our runtime spawn never ran.
typedef void (__thiscall* fn_talkbind)(void* qm, uint32_t refKey,
        const char* dlgName, int dlgIdx, int selfTrigId);
typedef uint32_t (__thiscall* fn_refkey)(void* cCreature); // 0x0044A1C0
// Implemented in runtime_triggers.cpp (owns the FUN_00461540 pump hook).
extern "C" void sdk_dlg_pump_register(uint32_t refKey, int handle,
                                      const char* name);
// Arms the dialog-text source probe (read_name_hash logs the render call-site
// resolving this id). Implemented in runtime_triggers.cpp.
extern "C" void sdk_dlgres_probe_arm(uint32_t c10);
// Registers {handle -> text hash} so the dialog driver can gate the FUN_0080f5e0
// redirect to whichever bound NPC is talking. Implemented in runtime_triggers.cpp.
extern "C" void sdk_dlg_text_register(int handle, uint32_t hash);
// Watch a hash on the FINAL resolver FUN_0080eaf0 (text_logger.cpp): logs,
// timestamped, whether the engine resolves OUR text hash while the dialog is open.
extern "C" void text_logger_watch(uint32_t hash);
// Arm the dialog redirect+capture on FUN_0080f5e0 (text_logger.cpp): for `ms`,
// vanilla dialog SENTENCES fetched by the talk window are swapped for our text
// hash (so the window renders OUR line natively), and every key->string is
// logged. our_hash = sacred_hash(text_key).
extern "C" void text_logger_arm_dialog(uint32_t our_hash, unsigned ms);

// Arm a bound DlgNPC to speak AND wire the talk-route so the window
// opens. handle = the NPC; dlg_name = the dlgnpc_bind name; text_key =
// a registered global.res name (text.lua). FIX A2 (dialog_runtime.md):
// call FUN_00463240 (gate + SelfTriggerQuest + talk block), then
// idempotently re-assert the two GUI-read fields at the by-objIdx
// entry. No record replay (it cleared the gate). Crash-proof: every
// engine call SEH-guarded; binder no-ops harmlessly on a refKey miss.
bool dialog_arm(int handle, const char* dlg_name, const char* text_key,
                const char* voice) {
    HMODULE exe = g_attach.exe_module;
    if (!exe || !dlg_name || !*dlg_name || !text_key || !*text_key)
        return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    void* qm  = (void*)(engine::singletons::qm(reb));
    void* cre = (void*)npc_creature(handle);
    if (!cre) { sdk_log("[dialog_arm] h=%d no creature", handle);
                return false; }
    (void)voice;
    dlg_probe(reb, handle, "pre");
    int oidx = -1, cnt = -1;
    dlg_entry_by_objidx(reb, handle, &oidx, &cnt);
    bool route = false;
    if (oidx >= 1 && oidx < cnt) {                 // arg3 must be >=1
        uint32_t rk_real = 0;
        __try { rk_real = ((fn_refkey)(reb + 0x0044A1C0))(cre); }
        __except (EXCEPTION_EXECUTE_HANDLER) { rk_real = 0; }
        uint32_t refKey = rk_real ? rk_real
                                  : (uint32_t)handle;  // <=0x10 passthru
        __try {
            ((fn_talkbind)(reb + 0x00463240))(
                qm, refKey, dlg_name, oidx, 0);
            route = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { route = false; }
        // Register the REAL FUN_0044A1C0 refKey (the value the engine's
        // conversation pump FUN_00461540 receives on a real player talk
        // — probe-confirmed 2026-05-18) so the pump hook can fire
        // "DLGANS:<name>" and advance the quest on actual talk.
        if (rk_real)
            sdk_dlg_pump_register(rk_real, handle, dlg_name);
    }
    // NATIVE DIALOG TEXT via the engine's record walker (Path A, the verified
    // fix — see .claude/knowledge/re/dialog_impl_plan.md). The disproven path
    // wrote a content handle into entry+0x4c / creature+0xc and showed random/
    // wrong text. Instead, bake a tag-0x03 DialogShow record whose field-1 is
    // res:<KEY> and replay it through FUN_00475680: the walker's field parser
    // self-resolves res:<KEY> via FUN_006726f0->FUN_0080e780->sacred_hash->
    // global.res (the SAME store the SDK bakes), so the NPC shows OUR text with
    // NO entry+0x4c / creature+0xc / creature+0x10 write at all. The talk-route
    // (FUN_00463240, above) already set entry+0x4c = creature handle correctly.
    char res_key[96];
    _snprintf_s(res_key, _TRUNCATE, "res:%s", text_key);   // field-1 needs the res: prefix
    uint8_t rec[0x208];
    size_t rn = dlg_put_dialog_rec(rec, res_key, dlg_name);

    // ★ DIALOG-TEXT WALL PROBE (new globalres resolver): resolve the SAME key the
    // engine walker would, straight from live global.res. Tells us decisively
    // whether our baked text is reachable there (display bug) or absent (the text
    // lives in the per-qm dialog store, the real wall). Also probe the dlg_name
    // and the "res:"-prefixed form to catch a key-format mismatch.
    {
        char u[512]; unsigned h1 = sacred::engine::sacred_hash(text_key);
        unsigned h2 = sacred::engine::sacred_hash(res_key);
        bool g1 = engine_resolve::globalres_string(h1, u, (int)sizeof(u));
        sdk_log("[dlgtext-probe] hash('%s')=%08x -> %s", text_key, h1, g1 ? u : "(absent)");
        // Arm the FINAL-resolver watch: catch (timestamped) whether the OPEN
        // dialog window resolves THIS hash through FUN_0080eaf0.
        text_logger_watch(h1);
        // Register {handle -> our hash} for the driver-gated redirect: while THIS
        // NPC's talk window is open, the dialog driver (FUN_0056B130) marks h1
        // active and FUN_0080f5e0 swaps the vanilla sentence for our text. Robust
        // to when the player opens the dialog (no time-from-arm expiry) and to
        // multiple NPCs (each redirects to its own line).
        sdk_dlg_text_register(handle, h1);
        // Capture-only (logs what the window fetches; redirect is driver-gated).
        text_logger_arm_dialog(h1, 20000);
        if (engine_resolve::globalres_string(h2, u, (int)sizeof(u)))
            sdk_log("[dlgtext-probe] hash('%s')=%08x -> %s", res_key, h2, u);

        // ★ SOURCE-FIELD PROBE (read-only, zero-risk). dialog_store.md says the
        // displayed line is resolved from creature+0x10 (→ FUN_006726f0 →
        // FUN_0080e780 → sacred_hash → global.res). Resolve creature+0x10 (and
        // +0xc) through the SAME resolver the renderer uses, so we SEE what the
        // window currently shows ("murderer"?) without hooking anything. If +0x10
        // resolves to the vanilla default, that's the field to redirect; if it
        // doesn't, the render source is a different field and we need the BP.
        uint32_t c10 = 0, c0c = 0;
        if (safe_read<uint32_t>((uintptr_t)cre + 0x10, &c10)) {
            bool gr = engine_resolve::globalres_string(c10, u, (int)sizeof(u));
            sdk_log("[dlgtext-probe] creature+0x10=%08x -> %s", c10, gr ? u : "(no resolve)");
            // Arm the render-callsite probe on this id (see read_name_hash). When
            // the dialog window opens and shows "murderer", we capture WHO resolved
            // it — pinning the field/call-site to redirect.
            if (c10) sdk_dlgres_probe_arm(c10);
        }
        if (safe_read<uint32_t>((uintptr_t)cre + 0x0c, &c0c)) {
            bool gr = engine_resolve::globalres_string(c0c, u, (int)sizeof(u));
            sdk_log("[dlgtext-probe] creature+0x0c=%08x -> %s", c0c, gr ? u : "(no resolve)");
        }
        // What value WOULD make creature+0x10 show our text via the high-bit
        // direct-global.res branch (FUN_0080eaf0): hash|0x80000000.
        sdk_log("[dlgtext-probe] redirect candidate creature+0x10 := %08x (=hash('%s')|0x80000000)",
                h1 | 0x80000000u, text_key);
    }

    // NAME-PRESERVE: the tag-03 walker clears the by-handle DlgNPC entry's +0x4c
    // (the entry keyed by cCreature handle at qm+0x755c) — which is the NPC's
    // NAME content handle. That left the NPC nameless after arming (the name
    // leaked into the trigger ring instead). Snapshot it now, restore it after
    // the replay. (dialog text itself is parked; this just stops the side effect.)
    uint32_t name4c = 0;
    __try {
        uintptr_t b = 0, e = 0;
        if (safe_read_ptr((uintptr_t)qm + 0x755c, &b) && b &&
            safe_read_ptr((uintptr_t)qm + 0x7560, &e) && e >= b) {
            for (uintptr_t p = b; p + 0x50 <= e; p += 0x50) {
                uint32_t key = 0;
                if (!safe_read<uint32_t>(p, &key)) break;
                if (key == (uint32_t)handle) { safe_read<uint32_t>(p + 0x4c, &name4c); break; }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    bool ok = funk_replay_one(reb, rec, rn, cre, 1);
    sdk_log("[dialog_arm] replay tag-03 res:%s on '%s' (%zu B) -> %s",
            text_key, dlg_name, rn, ok ? "dispatched" : "FAIL");

    // Restore the name content if the replay cleared it (re-scan in case the
    // walker reallocated the DlgNPC vector).
    if (name4c) __try {
        uintptr_t b = 0, e = 0;
        if (safe_read_ptr((uintptr_t)qm + 0x755c, &b) && b &&
            safe_read_ptr((uintptr_t)qm + 0x7560, &e) && e >= b) {
            for (uintptr_t p = b; p + 0x50 <= e; p += 0x50) {
                uint32_t key = 0;
                if (!safe_read<uint32_t>(p, &key)) break;
                if (key == (uint32_t)handle) {
                    uint32_t now = 0; safe_read<uint32_t>(p + 0x4c, &now);
                    if (now != name4c) {
                        safe_write<uint32_t>(p + 0x4c, name4c);
                        sdk_log("[dialog_arm] restored NAME content +4c=%08X (walker had cleared it)", name4c);
                    }
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    // Keep the dialog/draw gate on so the window opens; do NOT touch entry+0x4c
    // or creature+0xc (the talk-route set them correctly; the record carries
    // the text). NOT writing creature+0xc also leaves FUN_0048FF10's reward
    // once-guard (keyed on +0xc) intact, so the reward fires once.
    __try {
        uint32_t v14 = 0;
        if (safe_read<uint32_t>((uintptr_t)cre + 0x14, &v14))
            safe_write<uint32_t>((uintptr_t)cre + 0x14, v14 | 0x80000u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    dlg_probe(reb, handle, "post");
    sdk_log("[dialog_arm] h=%d say '%s' oidx=%d route=%d -> %s",
            handle, text_key, oidx, route ? 1 : 0, ok ? "armed" : "FAIL");
    return ok;
}

// Clear an armed dialog + its "?!" marker so it does NOT re-open after a
// quest step (fixes "?! never goes away / dialog & reward re-fire").
// Pure memory: zero the content slot the GUI reads (entry+0x4c at the
// by-objIdx entry AND cCreature+0xc), drop the +0x14&0x80000 dialog/
// draw gate, and set entry+0x48 = 0x08 (no marker). No engine call.
bool dialog_clear(int handle) {
    HMODULE exe = g_attach.exe_module; if (!exe) return false;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t c = npc_creature(handle);
    uintptr_t oe = dlg_entry_by_objidx(reb, handle, nullptr, nullptr);
    bool any = false;
    if (oe) {
        __try {
            safe_write<uint32_t>(oe + 0x4c, 0);          // content -> none
            safe_write<uint32_t>(oe + 0x48, 0x08u);      // marker glyph off
            any = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    // ALSO clear the BY-HANDLE DlgNPC entry (+0x4c/+0x48). dialog_arm armed
    // BOTH the by-objIdx and the by-handle slot, so clearing only the former
    // left the handle-keyed slot holding the content id → the renderer still
    // re-opened the same line on a repeat talk ("re-talk repeats" bug). Mirror
    // the dialog_arm write: scan qm+0x755c for entry+0==handle, zero +0x4c.
    __try {
        uintptr_t b = 0, e = 0, qmb = engine::singletons::qm(reb);
        if (safe_read_ptr(qmb + 0x755c, &b) && b &&
            safe_read_ptr(qmb + 0x7560, &e) && e >= b) {
            for (uintptr_t p = b; p + 0x50 <= e; p += 0x50) {
                uint32_t key = 0;
                if (!safe_read<uint32_t>(p, &key)) break;
                if (key == (uint32_t)handle) {
                    safe_write<uint32_t>(p + 0x4c, 0);   // content -> none
                    safe_write<uint32_t>(p + 0x48, 0x08u); // marker glyph off
                    any = true;
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (c) {
        // Zero ONLY the dialog content (so the window can't re-open).
        // Do NOT clear cCreature+0x14 & 0x80000 — that bit also gates
        // the engine's nameplate/dialog-NPC identity; clearing it made
        // the NPC lose its NAME and become un-talkable. Keeping it set
        // with content=0 + marker=0x08 = named NPC, no "?!", no dialog.
        __try { safe_write<uint32_t>(c + 0x0c, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    sdk_log("[dialog_clear] h=%d -> %s", handle, any ? "cleared" : "no-entry");
    return any;
}

// FUN_00482510 is __thiscall with TWO stack args and `ret 8` (callee
// pops 8). Disasm @0x475828: ECX=ctx; push ebp(arg2); push recBuf(arg1);
// call. Passing only 1 arg desynced the stack (ret 8 over-popped) →
// return-into-garbage crash. arg2 is write-only inside (a reused local),
// pass 0. (npc_templates.md "Engine-CreateNPC invocation — corrected".)
typedef void (__thiscall* fn_createnpc)(void* ctx, void* recBuf,
                                        void* unused_arg2);
int createnpc_engine(const uint8_t* payload, size_t plen, int want_type) {
    HMODULE exe = g_attach.exe_module;
    if (!exe || plen == 0 || plen > 200) return 0;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    void* ctx = (void*)(engine::singletons::qm(reb));
    uintptr_t om = 0, arr = 0, arr_end = 0;
    if (!safe_read_ptr(reb + 0x00AD5C40, &om) || !om) return 0;
    if (!safe_read_ptr(om + 4, &arr) || !arr) return 0;
    if (!safe_read_ptr(om + 8, &arr_end) || arr_end <= arr) return 0;
    uint32_t n = (uint32_t)((arr_end - arr) >> 2);

    // snapshot existing handles of want_type (to find the NEW one after)
    static uint32_t pre[512]; uint32_t npre = 0;
    for (uint32_t i = 0; i < n && npre < 512; i++) {
        uintptr_t c = 0; uint32_t t = 0;
        if (!safe_read_ptr(arr + (uintptr_t)i * 4, &c) || !c) continue;
        if (safe_read<uint32_t>(c + 0x10, &t) &&
            (int)(t & 0xFFFF) == want_type) pre[npre++] = i;
    }

    uint8_t rec[256] = { 0 };
    size_t size = plen + 3;
    rec[0] = 0x01;                       // tag = CreateNPC
    rec[1] = 0x00;                       // BE-hi (==0, record <256B)
    rec[2] = (uint8_t)(size & 0xFF);     // size low byte (LE guard reads this)
    for (size_t k = 0; k < plen; ++k) rec[3 + k] = payload[k];
    __try {
        ((fn_createnpc)(reb + 0x00482510))(ctx, rec, (void*)0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }

    // find a want_type creature not in the pre-snapshot = the new spawn
    if (!safe_read_ptr(om + 8, &arr_end)) return 0;
    n = (uint32_t)((arr_end - arr) >> 2);
    for (uint32_t i = n; i-- > 0; ) {     // scan from the top (new = high idx)
        uintptr_t c = 0; uint32_t t = 0;
        if (!safe_read_ptr(arr + (uintptr_t)i * 4, &c) || !c) continue;
        if (!safe_read<uint32_t>(c + 0x10, &t)) continue;
        if ((int)(t & 0xFFFF) != want_type) continue;
        bool seen = false;
        for (uint32_t j = 0; j < npre; j++) if (pre[j] == i) { seen = true; break; }
        if (!seen) return (int)i;
    }
    return 0;
}

// --- DlgNPC bind: make a runtime spawn a REAL dialog/quest NPC ------------
// THE storyline unlock (.claude/knowledge/re/dlgnpc_bind.md). A runtime-spawned
// creature has no DlgNPC entry, so name + "?!" marker no-op. There is no
// callable "register" engine fn — DlgNPC entries are a plain 0x50-byte POD
// appended to the std::vector at qm+0x755c (begin/end/cap). We append one
// engine-faithfully (the exact thing FUN_00475680 case 0x28 does), point it
// at our handle, set the marker sprite, then stamp cCreature+0x245 = idx
// via the engine helper FUN_005498f0 (the linchpin the marker renderer
// resolves through) and set the +0x14 render gate (every vanilla bind does
// this). After this, set_npc_name (entry+0x04) and npc_quest_icon
// (entry+0x48) hit a real entry and the nameplate + overhead marker work.
// Returns the DlgNPC index, or -1. Do NOT touch cCreature+0x200 (class
// bits → the red aura). Must run once, post-spawn, on the main game tick.
// FUN_004BB1D0 std::vector grow/insert. ECX = &hdr (the 3 ptrs at
// qm+0x755c: begin/end/cap). Args: insertPos, src(0x50 elem), &flag,
// count, after. (case-0x28 calls it (end,&elem,&flag,1,1).) Reallocs +
// updates the header. fn_bind4c=FUN_00465220 (ECX=qm, idx, v).
// fn_stamp_idx=FUN_005498f0 (ECX=cCreature, idx, net) → cCreature+0x245.
typedef void (__thiscall* fn_vec_grow)(void* hdr, void* insertPos,
        const void* src, void* flag, unsigned count, char after);
typedef void (__thiscall* fn_bind4c)(void* qm, unsigned idx, uint32_t v);
typedef void (__thiscall* fn_stamp_idx)(void* cCreature, int idx, char net);

int dlgnpc_bind(int handle, const char* name, int marker_on) {
    HMODULE exe = g_attach.exe_module;
    if (!exe || handle <= 0) return -1;
    uintptr_t reb = reinterpret_cast<uintptr_t>(exe) - 0x00400000;
    uintptr_t qm  = engine::singletons::qm(reb);
    uintptr_t c   = npc_creature(handle);
    if (!c) return -1;

    uintptr_t begin = 0, end = 0, cap = 0;
    if (!safe_read_ptr(qm + 0x755c, &begin) || !begin) return -1;
    if (!safe_read_ptr(qm + 0x7560, &end)   || end < begin) return -1;
    if (!safe_read_ptr(qm + 0x7564, &cap)   || cap < end)   return -1;
    unsigned count = (unsigned)((end - begin) / 0x50);

    int idx = -1;
    for (unsigned i = 0; i < count; i++) {
        int32_t k = 0;
        if (!safe_read<int32_t>(begin + (uintptr_t)i * 0x50, &k)) break;
        if (k == handle) { idx = (int)i; break; }
    }

    // Build the 0x50 element on our stack (exactly the record payload the
    // engine's tag-0x28 handler memcpy's).
    uint8_t elem[0x50];
    for (int k = 0; k < 0x50; ++k) elem[k] = 0;
    *(int32_t*)(elem + 0x00) = handle;                       // bound handle
    if (name) { unsigned i = 0; for (; name[i] && i < 0x3F; ++i)
                    elem[0x04 + i] = (uint8_t)name[i]; }
    *(uint32_t*)(elem + 0x44) = 0;                            // content id
    *(uint32_t*)(elem + 0x48) = marker_on ? 0x0Bu : 0x0Du;    // marker
    *(int32_t*) (elem + 0x4c) = handle;                       // state

    __try {
        if (idx >= 0) {
            // entry exists → just overwrite in place (no vector growth)
            uint8_t* e = (uint8_t*)(begin + (uintptr_t)idx * 0x50);
            if (IsBadWritePtr(e, 0x50)) return -1;
            for (int k = 0; k < 0x50; ++k) e[k] = elem[k];
        } else {
            idx = (int)count;
            if (end != cap) {
                // spare capacity → in-place append (engine case-0x28 path)
                if (IsBadWritePtr((void*)end, 0x50)) return -1;
                for (int k = 0; k < 0x50; ++k) ((uint8_t*)end)[k] = elem[k];
                safe_write<uintptr_t>(qm + 0x7560, end + 0x50);
            } else {
                // no capacity → engine realloc-grow (copies our elem),
                // updates begin/end/cap. RE-READ begin afterwards.
                char flag = 0;
                ((fn_vec_grow)(reb + 0x004BB1D0))(
                    (void*)(qm + 0x755c), (void*)end, elem, &flag, 1, 1);
                if (!safe_read_ptr(qm + 0x755c, &begin) || !begin)
                    return -1;
            }
        }
        // engine-official bind: entry+0x4c via helper (+ dirty/notify)…
        ((fn_bind4c)(reb + 0x00465220))((void*)qm, (unsigned)idx,
                                        (uint32_t)handle);
        // …stamp cCreature+0x245 = idx (renderer resolves through it)…
        ((fn_stamp_idx)(reb + 0x005498F0))((void*)c, idx, 0);
        // …and set the marker render gate. EMPIRICALLY PROVEN in-game:
        // removing this killed the overhead marker, and the red "thrall"
        // glow PERSISTED without it — so +0x14 bit 0x80000 is the MARKER
        // gate and is NOT the glow source. The glow comes from another
        // bind write (prime suspect: the bound-state entry+0x4c /
        // FUN_00465220 — like a Vampiress-raised thrall bound to the
        // player). Keep the marker; glow fix pending the RE verdict.
        uint32_t f14 = 0;
        if (safe_read<uint32_t>(c + 0x14, &f14))
            safe_write<uint32_t>(c + 0x14, f14 | 0x80000u);

        // --- thrall-glow suppression -------------------------------------
        // Two independent RE passes give two non-conflicting culprits for
        // the red "raised-thrall" column (identical to Vampiress
        // raise-dead). Empirically the glow is NOT +0x14&0x80000 (we
        // removed it, glow stayed). Apply BOTH suppressions (different
        // fields, both reversible) — the in-game result adjudicates which
        // RE was right; neither harms a normal NPC:
        //  (A) zero the effect-texture slots the red column binds
        //      (cCreature+0xbc/+0xc0 — plain NPCs have these 0; our spawn
        //       type has them set, hence the visible column).
        //  (B) clear the "raised/charmed thrall" faction bit + its mirror
        //      (cCreature+0x1F4 bit 0x80000, +0xb0 bit 0x400). We never
        //      want our quest NPC flagged as a summoned thrall. +0x4d8
        //      (relationship handle) is left alone — clearing it could
        //      disturb friend/foe; revisit only if glow persists.
        safe_write<uint32_t>(c + 0xbc, 0);
        safe_write<uint32_t>(c + 0xc0, 0);
        uint32_t f1f4 = 0;
        if (safe_read<uint32_t>(c + 0x1F4, &f1f4))
            safe_write<uint32_t>(c + 0x1F4, f1f4 & ~0x80000u);
        uint32_t fb0 = 0;
        if (safe_read<uint32_t>(c + 0xb0, &fb0))
            safe_write<uint32_t>(c + 0xb0, fb0 & ~0x400u);
        // NOTE: cCreature+0x4d8 is HP — proven in-game ("dead but still
        // talks" when zeroed: HP=0 corpse, kept alive as a bound+invuln
        // dialog NPC). It is NOT the glow source; the +0x4d8 dump
        // correlation was spurious (CAP had 100 HP from level 20). Do NOT
        // write +0x4d8.
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return idx;
}

bool set_world_pos(int32_t kx, int32_t ky) {
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
    if (idx >= (uint32_t)((arr_end - arr) >> 2))  return false;
    if (!safe_read_ptr(arr + idx * 4, &hero) || !hero) return false;

    fn_engine_tp tp = reinterpret_cast<fn_engine_tp>(rebase + 0x0054D9D0);
    int rv = tp(reinterpret_cast<void*>(hero), (int)kx, (int)ky, 0, 1);
    return rv != 0;
}

Snapshot read() {
    Snapshot s{};
    uintptr_t base = hero_base();   // (A4) resolve_player_struct moved to engine/hero.cpp
    if (!base) return s;

    s.struct_addr = base;

    bool ok = true;
    ok &= safe_read<uint16_t>(base + 0x010, &s.class_id);
    ok &= safe_read<int32_t> (base + 0x4D8, &s.health);

    // 8 skill bytes at +0x3CC..+0x3D3
    for (int i = 0; i < 8; i++) {
        ok &= safe_read<uint8_t>(base + 0x3CC + i, &s.skills[i]);
    }

    // Equipment slots — all 4-byte item ids.
    ok &= safe_read<uint32_t>(base + 0x1A4, &s.helmet);
    ok &= safe_read<uint32_t>(base + 0x1A8, &s.cuirass);
    ok &= safe_read<uint32_t>(base + 0x1AC, &s.belt);
    ok &= safe_read<uint32_t>(base + 0x1B0, &s.boots);
    ok &= safe_read<uint32_t>(base + 0x1B4, &s.gauntlets);
    ok &= safe_read<uint32_t>(base + 0x1B8, &s.bracers);
    ok &= safe_read<uint32_t>(base + 0x1BC, &s.amulet1);
    ok &= safe_read<uint32_t>(base + 0x1C0, &s.amulet2);
    ok &= safe_read<uint32_t>(base + 0x1C4, &s.ring1);
    ok &= safe_read<uint32_t>(base + 0x1C8, &s.ring2);
    ok &= safe_read<uint32_t>(base + 0x1CC, &s.ring3);
    ok &= safe_read<uint32_t>(base + 0x1D0, &s.ring4);
    ok &= safe_read<uint32_t>(base + 0x1D4, &s.weapon_l);
    ok &= safe_read<uint32_t>(base + 0x1D8, &s.weapon_r);
    ok &= safe_read<uint32_t>(base + 0x1DC, &s.cannon);
    ok &= safe_read<uint32_t>(base + 0x1E0, &s.shoulders);
    ok &= safe_read<uint32_t>(base + 0x1E4, &s.greaves);
    ok &= safe_read<uint32_t>(base + 0x1E8, &s.wings);

    s.valid = ok;
    return s;
}

}} // namespace sdk::player
