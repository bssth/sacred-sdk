// quest_entry.h — typed cQuestMgr quest-entry (0x174 stride) writer.
//
// SOURCE:
//   - sdk/.claude/knowledge/re/quest_lifecycle.md §1 (marker), §2 (objective),
//     §3 (completion); re_backlog.md S6 + "C++-port candidates" #6.
//   - Registry: *(0x00AAD3A4)=begin / *(0x00AAD3A8)=end, stride 0x174,
//     key=quest_id u32 @ entry+0x08 (mirrors qm+0x424/+0x428).
//   - cQuestMgr singleton @ 0x00AACF80 (ECX in all handlers).
//
// ENTRY FIELDS (quest_lifecycle.md, all relative to a 0x174-byte entry):
//   +0x00  type/state: 3 = MAIN/story (also "completed" styling), 4 = SIDE.
//   +0x04  journal sub-type (1/2 normal, 100/0x65 special); ==2 also required
//          for world-map icon list membership.
//   +0x08  quest_id (u32 key).
//   +0x0C  bit0: 1 = objective step DONE (filled bullet) / 0 = OPEN (hollow).
//   +0x10  marker source X (literal-coord), OR 0xEEEEEEEE/-1 = follow object.
//   +0x14  marker source Y (literal-coord), OR object id when +0x10 is a sentinel.
//   +0x20  marker priority/range.
//   +0x24  log title text handle (render GATE; must be != 0).
//   +0x28  "1/1"-style counter / sub-id text handle.
//   +0x2C..+0x4C  10 u32 sub-step text handle slots.
//   +0x16C tab page id (u16): 0 = first tab, else tab N+1.
//
// Text handles come from qb_resolve("NAME") =
//   (uint(__cdecl*)(const char*))0x00672740  -> returns hash|0x80000000
//   (0 if the name is not in the loaded dict). Declared below, TODO(verify).
//
// Per-class marker registration (quest_lifecycle.md §1): base qm+0x3a0 stride
//   8; the class-quest marker SLOT (an entry INDEX) is written at
//   qm+0x3a4 + C*8, where C = active class id (1..16) from
//   *(int*)(FUN_007d84a0()+0x14). The slot-0 path requires entry+8 <= 100; for
//   high quest ids use the slot-3 single-target store at qm+0x7704/+0x7708/
//   +0x7718 instead.
//
// This header is self-contained: standard headers + a caller-supplied MemAccess
// (matches command_ring.h's accessor shape so a wiring layer can share one). It
// does NOT resolve the registry or the active class — the caller passes the
// entry base address (and class/qm bases where needed).
//
// TODO(port): wire when the registry walk (find entry by quest_id) and
//   qb_resolve are available to the caller AND the per-build field offsets are
//   BP-reconfirmed (quest_lifecycle.md flags some as MEDIUM confidence).
//   Currently UNWIRED — not in SacredSDK.vcxproj, not #included.

#ifndef SACRED_SDK_PORTS_ENGINE_QUEST_ENTRY_H
#define SACRED_SDK_PORTS_ENGINE_QUEST_ENTRY_H

#include <cstdint>
#include <functional>

namespace sacred {
namespace engine {

// ---- registry / singleton VAs (TODO(verify) byte-sig vs Sacred_decrypted.exe)
namespace quest_reg {
constexpr uint32_t kQuestMgrSingletonVA = 0x00AACF80; // cQuestMgr object base
constexpr uint32_t kRegistryBeginPtrVA  = 0x00AAD3A4; // *(this) = begin
constexpr uint32_t kRegistryEndPtrVA    = 0x00AAD3A8; // *(this) = end
constexpr uint32_t kEntryStride         = 0x174;

// qb_resolve: name -> text handle (hash|0x80000000). __cdecl.
// TODO(verify): byte-sig vs Sacred_decrypted.exe.
constexpr uint32_t kQbResolveVA         = 0x00672740;

// class id getter FUN_007d84a0() then +0x14 = active class (1..16).
// TODO(verify): byte-sig vs Sacred_decrypted.exe.
constexpr uint32_t kActiveClassGetterVA = 0x007d84a0;
} // namespace quest_reg

// Entry-relative field offsets (quest_lifecycle.md).
namespace quest_entry_off {
constexpr uint32_t kType        = 0x00;  // 3 = MAIN/done-style, 4 = SIDE
constexpr uint32_t kSubType     = 0x04;  // 1/2 normal, 100/0x65 special; ==2 map-icon
constexpr uint32_t kQuestId     = 0x08;  // u32 key
constexpr uint32_t kFlags       = 0x0C;  // bit0 = step done
constexpr uint32_t kMarkerX     = 0x10;  // X or 0xEEEEEEEE/-1 sentinel
constexpr uint32_t kMarkerY     = 0x14;  // Y or object id
constexpr uint32_t kMarkerPrio  = 0x20;  // priority/range
constexpr uint32_t kTitle       = 0x24;  // log title handle (render gate)
constexpr uint32_t kCounter     = 0x28;  // "1/1" counter/sub-id handle
constexpr uint32_t kSubStep0    = 0x2C;  // first of 10 u32 sub-step handles
constexpr uint32_t kSubStepEnd  = 0x4C;  // last sub-step slot (inclusive)
constexpr uint32_t kTabPage     = 0x16C; // u16 tab page

// marker sentinels for +0x10
constexpr uint32_t kMarkerFollowObj   = 0xEEEEEEEE; // resolve object in +0x14
constexpr uint32_t kMarkerFollowLive  = 0xFFFFFFFF; // object in +0x14, live pos
} // namespace quest_entry_off

// Single-target world-map marker store (slot 3) — reliable for HIGH quest ids
// (quest_lifecycle.md §1). Offsets relative to the cQuestMgr singleton base.
namespace quest_marker_slot3 {
constexpr uint32_t kX   = 0x7704; // worldX  (or 0xFFFFFFFF = NPC mode)
constexpr uint32_t kY   = 0x7708; // worldY  (or object id in NPC mode)
constexpr uint32_t kF1  = 0x7718; // must be != 0 for the literal-coord path
constexpr uint32_t kF2  = 0x771c;
} // namespace quest_marker_slot3

// Caller-supplied memory accessor (same shape as command_ring.h::MemAccess so
// a wiring layer can reuse one instance). Absolute addresses; in- or
// out-of-process transparent.
struct MemAccess {
    std::function<uint32_t(uint32_t addr)>           read32;
    std::function<void(uint32_t addr, uint32_t val)> write32;
};

// Typed view over one 0x174-byte entry. Holds only the entry base address and
// the accessor; all reads/writes go through MemAccess. Stateless beyond that.
class QuestEntry {
public:
    QuestEntry(const MemAccess& mem, uint32_t entry_addr)
        : mem_(mem), base_(entry_addr) {}

    uint32_t address() const { return base_; }

    uint32_t quest_id() const {
        return mem_.read32(base_ + quest_entry_off::kQuestId);
    }

    // ---- objective / journal (quest_lifecycle.md §2) -----------------------
    void set_title(uint32_t text_handle) {
        mem_.write32(base_ + quest_entry_off::kTitle, text_handle);
    }
    void set_counter(uint32_t text_handle) {
        mem_.write32(base_ + quest_entry_off::kCounter, text_handle);
    }
    // idx 0..9 -> entry+0x2C + idx*4
    void set_substep(uint32_t idx, uint32_t text_handle) {
        if (idx > 9) return; // 10-slot list (+0x2C..+0x4C)
        mem_.write32(base_ + quest_entry_off::kSubStep0 + idx * 4, text_handle);
    }
    void set_tab_page(uint16_t page) {
        // 16-bit field; preserve the high half of the dword.
        const uint32_t dw = mem_.read32(base_ + quest_entry_off::kTabPage);
        mem_.write32(base_ + quest_entry_off::kTabPage,
                     (dw & 0xffff0000u) | page);
    }
    void set_type(uint32_t type) { // 3 = MAIN, 4 = SIDE
        mem_.write32(base_ + quest_entry_off::kType, type);
    }
    void set_subtype(uint32_t st) {
        mem_.write32(base_ + quest_entry_off::kSubType, st);
    }

    // Step open/done bullet (quest_lifecycle.md §2: +0x0C bit0).
    void set_step_done(bool done) {
        const uint32_t f = mem_.read32(base_ + quest_entry_off::kFlags);
        mem_.write32(base_ + quest_entry_off::kFlags,
                     done ? (f | 1u) : (f & ~1u));
    }

    // ---- completion (quest_lifecycle.md §3a) -------------------------------
    // "Completed" styling, quest stays in book: +0x00 = 3. Reversible.
    void mark_completed_styling() {
        mem_.write32(base_ + quest_entry_off::kType, 3);
    }

    // ---- on-map / minimap marker (quest_lifecycle.md §1) -------------------
    // Literal-coord marker on THIS entry (slot-0 family). NOTE the slot-0 gate
    // requires entry+8 <= 100; for high quest ids prefer the slot-3 store
    // (free function WriteSlot3Marker below).
    void set_literal_marker(int32_t worldX, int32_t worldY, uint32_t prio = 0) {
        mem_.write32(base_ + quest_entry_off::kMarkerX, static_cast<uint32_t>(worldX));
        mem_.write32(base_ + quest_entry_off::kMarkerY, static_cast<uint32_t>(worldY));
        mem_.write32(base_ + quest_entry_off::kMarkerPrio, prio);
    }
    // Follow an object (NPC) marker: +0x10 sentinel, +0x14 object id.
    void set_object_marker(uint32_t object_id, bool live_pos) {
        mem_.write32(base_ + quest_entry_off::kMarkerX,
                     live_pos ? quest_entry_off::kMarkerFollowLive
                              : quest_entry_off::kMarkerFollowObj);
        mem_.write32(base_ + quest_entry_off::kMarkerY, object_id);
    }

private:
    MemAccess mem_;
    uint32_t  base_;
};

// Find an entry by quest_id by linear-scanning the registry [begin,end) at
// stride 0x174 (key @ entry+0x08). Returns the entry address, or 0 if absent.
// `qm_singleton_addr` is the cQuestMgr base (default = the documented VA).
// TODO(verify): registry begin/end pointer VAs vs Sacred_decrypted.exe.
inline uint32_t FindQuestEntry(const MemAccess& mem,
                               uint32_t quest_id,
                               uint32_t registry_begin_ptr_va = quest_reg::kRegistryBeginPtrVA,
                               uint32_t registry_end_ptr_va   = quest_reg::kRegistryEndPtrVA) {
    const uint32_t begin = mem.read32(registry_begin_ptr_va);
    const uint32_t end   = mem.read32(registry_end_ptr_va);
    if (begin == 0 || end <= begin) return 0;
    for (uint32_t e = begin; e + quest_reg::kEntryStride <= end; e += quest_reg::kEntryStride) {
        if (mem.read32(e + quest_entry_off::kQuestId) == quest_id) return e;
    }
    return 0;
}

// Slot-3 single-target world-map marker (reliable for HIGH quest ids). Writes
// the literal-coord variant into the cQuestMgr singleton store.
// `qm_addr` = cQuestMgr base.
inline void WriteSlot3LiteralMarker(const MemAccess& mem,
                                    uint32_t qm_addr,
                                    int32_t worldX, int32_t worldY) {
    using namespace quest_marker_slot3;
    mem.write32(qm_addr + kX, static_cast<uint32_t>(worldX));
    mem.write32(qm_addr + kY, static_cast<uint32_t>(worldY));
    mem.write32(qm_addr + kF1, 1); // non-zero enables the literal-coord path
    mem.write32(qm_addr + kF2, 1);
}

// Slot-3 NPC-follow marker variant.
inline void WriteSlot3ObjectMarker(const MemAccess& mem,
                                   uint32_t qm_addr,
                                   uint32_t object_id) {
    using namespace quest_marker_slot3;
    mem.write32(qm_addr + kX, 0xFFFFFFFFu); // NPC mode sentinel
    mem.write32(qm_addr + kY, object_id);
}

} // namespace engine
} // namespace sacred

#endif // SACRED_SDK_PORTS_ENGINE_QUEST_ENTRY_H
