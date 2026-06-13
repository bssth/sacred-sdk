// command_ring.h — cCreature scripted-command ring append (move/teleport/voice).
//
// SOURCE:
//   - sdk/.claude/knowledge/re/triggers_dialog_move.md §(B),§(C) and the ring
//     table; re_backlog.md S8 + "C++-port candidates" #5.
//   - Handlers FUN_0049e210 (NPC_Goto MP), FUN_00491d40 (Teleport MP),
//     FUN_004a9730 (PlaySound creature branch) all write the same ring.
//
// RING (cCreature):
//   +0x588 = ring BEGIN ptr, +0x58c = ring END ptr, element stride = 0x44.
//   element[+0x00] = command type:
//     1   = MoveTo   (NPC_Goto)
//     9   = Teleport
//     0xe = PlaySound / Voice
//   Type-specific args (offsets WITHIN the 0x44 element), per the §C table:
//     type 1  (MoveTo)  : +0x40 X, +0x3c Y, +0x38 sector, +0x30 run/walk mode
//     type 9  (Teleport): +0x40 X, +0x3c Y, +0x38 sector, +0x34 aux handle
//     type 0xe(Voice)   : +0x40 sound id (SOUND_FX table id; see get_snd note)
//
//   To append, the engine grows the ring with FUN_004be490 (reserve) /
//   FUN_004b9900 (shift) when full, writes the element at the tail, and bumps
//   +0x58c by 0x44. This is the single runtime hook point to drive an NPC
//   WITHOUT editing FunkCode (triggers_dialog_move.md §C "Runtime recipe").
//
// This header is self-contained: it depends only on standard headers and a
// caller-supplied memory accessor. It does NOT resolve the creature (the
// caller passes a creature base pointer) and does NOT itself read process
// memory — the caller supplies read/write callbacks so this can run in-process
// (raw pointers) or out-of-process (RPM/WPM) unchanged.
//
// CRITICAL BLOCKING UNKNOWN (do NOT present a guess as fact):
//   The exact ABI of the grow helpers FUN_004be490 / FUN_004b9900 is NOT
//   verified (triggers_dialog_move.md "Open items" #2/#3: needs a live BP on
//   FUN_004a9730 ~0x4a98xx / the MP branches to capture the arg convention).
//   Therefore AppendCommand below has TWO modes:
//     (a) in-place fast path: if the ring has spare capacity the caller has
//         already reserved, write directly + bump end (fully known, safe);
//     (b) grow path: declared as function-pointer stubs tagged TODO(verify)
//         — they MUST be confirmed by BP before use. Calling the grow path
//         without that confirmation is unsafe and is gated behind a flag.
//
// TODO(port): wire when FUN_004be490/FUN_004b9900 ABI is BP-confirmed AND the
//   creature-resolve path (om=*(0x00AD5C40); arr=*(om+4); c=*(arr+idx*4)) is
//   available to the caller. Currently UNWIRED.

#ifndef SACRED_SDK_PORTS_ENGINE_COMMAND_RING_H
#define SACRED_SDK_PORTS_ENGINE_COMMAND_RING_H

#include <cstdint>
#include <functional>

namespace sacred {
namespace engine {

// ---- ring layout constants (triggers_dialog_move.md §C) --------------------
namespace creature_ring {
constexpr uint32_t kBeginOff   = 0x588; // cCreature+0x588 -> element begin ptr
constexpr uint32_t kEndOff     = 0x58c; // cCreature+0x58c -> element end ptr
constexpr uint32_t kStride     = 0x44;  // element size

// element-relative field offsets
constexpr uint32_t kElemType   = 0x00;  // command type (u32)
constexpr uint32_t kElemX      = 0x40;  // X (move/teleport) OR sound id (voice)
constexpr uint32_t kElemY      = 0x3c;  // Y
constexpr uint32_t kElemSector = 0x38;  // sector
constexpr uint32_t kElemAux    = 0x34;  // teleport aux handle
constexpr uint32_t kElemMode   = 0x30;  // move run/walk mode
} // namespace creature_ring

enum class CommandType : uint32_t {
    MoveTo    = 1,
    Teleport  = 9,
    PlaySound = 0xe,
};

// A decoded command to enqueue. Unused fields are ignored per type.
struct RingCommand {
    CommandType type = CommandType::MoveTo;
    int32_t x = 0;       // MoveTo/Teleport: world X.  PlaySound: SOUND_FX id.
    int32_t y = 0;       // MoveTo/Teleport: world Y.
    int32_t sector = 0;  // MoveTo/Teleport: sector.
    int32_t aux = 0;     // Teleport: aux handle.
    int32_t mode = 0;    // MoveTo: run/walk mode (e.g. 0x1000000 = run).
};

// Minimal memory accessor the caller supplies. `base` is a flat address space:
// for in-process use these wrap raw pointer reads/writes; for out-of-process
// they wrap ReadProcessMemory/WriteProcessMemory. Addresses are absolute.
struct MemAccess {
    std::function<uint32_t(uint32_t addr)>            read32;
    std::function<void(uint32_t addr, uint32_t val)>  write32;
    // Zero `len` bytes starting at `addr` (used to clear a fresh element).
    std::function<void(uint32_t addr, uint32_t len)>  zero;
};

// ---- engine grow-helper ABI (UNVERIFIED — must BP-confirm) ------------------
//
// Declared as plain typedefs of the documented thiscall helpers so a future
// wiring can bind them after a byte-sig check + BP capture. NOT called by the
// safe path below.
//
//   FUN_004be490 — ring reserve/grow (re_backlog/triggers §C). VA in OUR build
//                  UNCONFIRMED; byte-sig present at file offset 0xbe490.
//   FUN_004b9900 — ring shift. VA UNCONFIRMED; byte-sig present at 0xb9900.
//
// TODO(verify): byte-sig vs Sacred_decrypted.exe AND confirm the exact arg
//   convention (thiscall ECX=cCreature? extra count arg?) with a live BP on
//   FUN_004a9730 / the NPC_Goto MP branch before binding these.
constexpr uint32_t kFunRingReserveVA = 0x004be490; // TODO(verify)
constexpr uint32_t kFunRingShiftVA   = 0x004b9900; // TODO(verify)

// Encode a RingCommand into element fields via the accessor, at element base
// `elem_addr` (already a 0x44 slot owned by the ring). Zeroes the element
// first, then writes the type-specific fields. Pure write helper; assumes the
// slot is valid/reserved.
inline void EncodeCommandInto(const MemAccess& mem,
                              uint32_t elem_addr,
                              const RingCommand& cmd) {
    using namespace creature_ring;
    mem.zero(elem_addr, kStride);
    mem.write32(elem_addr + kElemType, static_cast<uint32_t>(cmd.type));
    switch (cmd.type) {
        case CommandType::MoveTo:
            mem.write32(elem_addr + kElemX,      static_cast<uint32_t>(cmd.x));
            mem.write32(elem_addr + kElemY,      static_cast<uint32_t>(cmd.y));
            mem.write32(elem_addr + kElemSector, static_cast<uint32_t>(cmd.sector));
            mem.write32(elem_addr + kElemMode,   static_cast<uint32_t>(cmd.mode));
            break;
        case CommandType::Teleport:
            mem.write32(elem_addr + kElemX,      static_cast<uint32_t>(cmd.x));
            mem.write32(elem_addr + kElemY,      static_cast<uint32_t>(cmd.y));
            mem.write32(elem_addr + kElemSector, static_cast<uint32_t>(cmd.sector));
            mem.write32(elem_addr + kElemAux,    static_cast<uint32_t>(cmd.aux));
            break;
        case CommandType::PlaySound:
            // +0x40 carries the SOUND_FX numeric id (get via FUN_00676170 or a
            // direct scan of the table @0x00964870 stride 0x44 — see notes).
            mem.write32(elem_addr + kElemX, static_cast<uint32_t>(cmd.x));
            break;
    }
}

// Result of an append attempt.
enum class AppendResult {
    Ok,             // element written, end bumped
    NeedsGrow,      // ring is at the caller-provided capacity; grow required
                    // (the grow ABI is UNVERIFIED — see kFunRing* above)
};

// Append a command to the ring of the creature at `creature_addr`.
//
// SAFE fast path only: the ring's spare capacity (in elements) is supplied by
// the caller as `spare_elems` — i.e. how many 0x44 slots exist beyond `end`
// before the backing buffer must grow. (The caller learns this from the
// reserve/grow bookkeeping; the ring does not store a capacity word we have
// verified.) If spare_elems == 0 we return NeedsGrow rather than guess at the
// grow helper. When spare exists, we write at the current end and bump
// +0x58c by 0x44 — this part is fully known and safe.
inline AppendResult AppendCommand(const MemAccess& mem,
                                  uint32_t creature_addr,
                                  const RingCommand& cmd,
                                  uint32_t spare_elems) {
    using namespace creature_ring;
    if (spare_elems == 0) return AppendResult::NeedsGrow;

    const uint32_t end_addr = mem.read32(creature_addr + kEndOff);
    EncodeCommandInto(mem, end_addr, cmd);
    mem.write32(creature_addr + kEndOff, end_addr + kStride);
    return AppendResult::Ok;
}

// Convenience constructors mirroring the three documented uses.
inline RingCommand MakeMove(int32_t x, int32_t y, int32_t sector, bool run) {
    RingCommand c; c.type = CommandType::MoveTo; c.x = x; c.y = y;
    c.sector = sector; c.mode = run ? 0x1000000 : 0; return c;
}
inline RingCommand MakeTeleport(int32_t x, int32_t y, int32_t sector, int32_t aux) {
    RingCommand c; c.type = CommandType::Teleport; c.x = x; c.y = y;
    c.sector = sector; c.aux = aux; return c;
}
inline RingCommand MakeVoice(int32_t sound_id) {
    RingCommand c; c.type = CommandType::PlaySound; c.x = sound_id; return c;
}

} // namespace engine
} // namespace sacred

#endif // SACRED_SDK_PORTS_ENGINE_COMMAND_RING_H
