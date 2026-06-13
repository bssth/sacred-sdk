// hostility_matrix.h — pure C++ faction-query over Sacred Gold's 16x16 hostility
// matrix, plus a port of the FUN_00423580 hostility predicate (incl. the
// peaceful-override bit and the hero-redirect special case).
//
// SOURCE:
//   sdk/.claude/knowledge/re/combat_init.md §4 ("Faction model") and its
//     "Ally-team binding" / "MATRIX POLARITY" corrections (the authoritative,
//     decompiled-and-hexdumped account):
//     - Hostility = a 16x16 BYTE matrix indexed by the two creatures' AI/faction
//       class at cCreature+0x1F0:  MATRIX[ A.class*16 + B.class ].
//     - POLARITY (corrected, HIGH conf, supersedes earlier notes):
//         byte 0x00 = "A ATTACKS B"   (HOSTILE)
//         byte 0x01 = "ignore"        (NOT hostile)
//       FUN_00423580 computes `local_9 = (MATRIX[A*16+B] == 0)` and returns it;
//       caller treats return != 0 as HOSTILE. (combat_init.md §2 "MATRIX POLARITY".)
//     - The live matrix at VA 0x008EB548 is anti-tamper SCRAMBLED (all 0x01);
//       FUN_00423580 (:57-66) detects this and restores the REAL matrix from VA
//       0x00890A30 (copies 0x40 dwords = 256 bytes). So 0x00890A30 holds the real
//       16x16. (combat_init.md §4.)
//     - Clusters: player/ally = class {1,3,4,7}; monster = {2,5,8,9,10,11,12,14,15};
//       class 13 = universal non-combatant (row/col all "ignore").
//   re_backlog.md S3 corroborates the same matrix VA, index formula, and polarity.
//
//   Hero-redirect (FUN_00423580:45-52, 130-133): when B's *static* def-table
//     class byte (FUN_004266f0(B.+0x10)) marks B a playable hero AND B has a
//     controller ref at B+0x1ec, the predicate redirects to that controller's
//     +0x1F0 for B's effective class. Only reachable when A is already in the
//     ally cluster (the :67 short-circuit jumps past it otherwise).
//   Override bits in cCreature+0x1F4 (combat_init.md §4):
//     - `+0x1F4 & 0x40000` = peaceful/no-aggro: at :150-157 it INVERTS the
//       friend/foe verdict (effectively forces "ignore" for plain matrix use).
//       Modeled here as `kPeacefulOverride`.
//   Grudge mode `+0x200 & 0x10000000` and the +0x39c grudge list are NOT modeled
//     (per-target dynamic state, outside a pure matrix query) — see TODO below.
//
// TODO(verify): absolute VAs 0x00890A30 (real matrix), 0x008EB548 (scrambled
//   shadow), and the predicate FUN_00423580 @0x00423580 — byte-sig vs
//   sdk/Sacred_decrypted.exe. File offset of the real matrix = 0x490A30.
//   Confirm 256 bytes, row 1 (hero) = `1 1 0 1 1 0 1 1 0 0 0 0 0 0 0 0`
//   (combat_init.md §4 dumped this), and M[2][1]==0x00, M[3][1]==0x01.
//
// TODO(port): wire when the SDK exposes faction/aggro queries to Lua or needs
//   an in-process friend/foe test without calling FUN_00423580. FUTURE-USE,
//   standalone: do NOT add to SacredSDK.vcxproj or #include from existing TUs.
//
// This header takes the 256-byte matrix as a buffer (the caller reads it from
// 0x00890A30, or from a captured fixture) and the two creatures' relevant
// fields as plain values — it does NOT dereference engine memory itself and has
// NO engine/mem.h dependency.
//
// C++14, MSVC.

#ifndef SACRED_SDK_PORTS_HOSTILITY_MATRIX_H
#define SACRED_SDK_PORTS_HOSTILITY_MATRIX_H

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace sacred {
namespace faction {

// --- engine constants (TODO(verify) all vs Sacred_decrypted.exe) ------------
struct HostilityLayout {
    static constexpr std::uintptr_t kRealMatrixVA      = 0x00890A30; // real 16x16
    static constexpr std::uintptr_t kScrambledMatrixVA = 0x008EB548; // anti-tamper shadow
    static constexpr std::uintptr_t kPredicateVA       = 0x00423580; // FUN_00423580
    static constexpr std::size_t    kDim               = 16;         // 16x16
    static constexpr std::size_t    kBytes             = kDim * kDim; // 256

    // cCreature field offsets used by the faction query (re_backlog S3 / combat_init).
    static constexpr std::size_t kOffClass   = 0x1F0; // i32 AI behaviour class AND faction id
    static constexpr std::size_t kOffFlags   = 0x1F4; // u32 faction/side + behaviour flags
    static constexpr std::size_t kOffCtrl    = 0x1EC; // controller ref (hero-redirect target)
    static constexpr std::size_t kOffAi200   = 0x200; // u32 AI-controller/aggro word (grudge bit)

    // Flag bits.
    static constexpr std::uint32_t kPeacefulOverride = 0x00040000; // +0x1F4: inverts/forces ignore
    static constexpr std::uint32_t kGrudgeMode       = 0x10000000; // +0x200: per-target grudge path
};

// Matrix polarity (from the corrected combat_init.md §2/§4).
enum : std::uint8_t {
    kCellAttack = 0x00, // A attacks B  -> HOSTILE
    kCellIgnore = 0x01, // ignore       -> not hostile
};

// Cluster membership helpers (combat_init.md §4 / re_backlog S3).
inline bool is_ally_cluster(std::int32_t cls) {
    return cls == 1 || cls == 3 || cls == 4 || cls == 7;
}
inline bool is_monster_cluster(std::int32_t cls) {
    switch (cls) {
        case 2: case 5: case 8: case 9: case 10:
        case 11: case 12: case 14: case 15: return true;
        default: return false;
    }
}
inline bool is_noncombatant(std::int32_t cls) { return cls == 13; }

// --- the matrix wrapper -----------------------------------------------------
// Non-owning view over the 256-byte real matrix (caller reads it from
// kRealMatrixVA, or supplies a fixture). Indexed [A][B] = M[A*16 + B].
class HostilityMatrix {
public:
    HostilityMatrix() = default;
    // `bytes` must point at 256 bytes (the real, de-scrambled matrix).
    explicit HostilityMatrix(const std::uint8_t* bytes) : m_(bytes) {}

    bool valid() const { return m_ != nullptr; }

    // Raw cell value. Returns kCellIgnore (safe default) if class is OOB/null.
    std::uint8_t cell(std::int32_t a, std::int32_t b) const {
        if (!m_ || a < 0 || b < 0 ||
            a >= static_cast<int>(HostilityLayout::kDim) ||
            b >= static_cast<int>(HostilityLayout::kDim)) {
            return kCellIgnore;
        }
        return m_[a * HostilityLayout::kDim + b];
    }

    // Pure matrix verdict: does class A attack class B? (cell == 0x00).
    bool attacks(std::int32_t classA, std::int32_t classB) const {
        return cell(classA, classB) == kCellAttack;
    }

    // Heuristic: detect the anti-tamper scrambled state (all cells 0x01). If the
    // caller accidentally read 0x008EB548 instead of 0x00890A30, this is true.
    bool looks_scrambled() const {
        if (!m_) return false;
        for (std::size_t i = 0; i < HostilityLayout::kBytes; ++i) {
            if (m_[i] != kCellIgnore) return false;
        }
        return true;
    }

private:
    const std::uint8_t* m_ = nullptr;
};

// --- restore the real matrix from the scrambled shadow ----------------------
// Mirrors FUN_00423580:57-66 — if the active matrix is scrambled (all 0x01),
// copy 256 bytes from the real-matrix source. `active` and `realSrc` are
// 256-byte buffers the caller read from 0x008EB548 and 0x00890A30 respectively.
// Returns true if a restore was performed.
inline bool restore_if_scrambled(std::uint8_t* active, const std::uint8_t* realSrc) {
    if (!active || !realSrc) return false;
    HostilityMatrix view(active);
    if (!view.looks_scrambled()) return false;
    std::memcpy(active, realSrc, HostilityLayout::kBytes);
    return true;
}

// --- a creature's faction inputs for the predicate --------------------------
// Plain value struct so this header never dereferences a cCreature itself.
// Fill it from the live object: aiClass = *(i32*)(cre+0x1F0),
// flags1F4 = *(u32*)(cre+0x1F4), heroDefClass = FUN_004266f0(*(cre+0x10)),
// ctrlAiClass = (cre+0x1ec ? *(i32*)(*(cre+0x1ec)+0x1F0) : aiClass).
struct FactionActor {
    std::int32_t aiClass     = 0;     // cCreature+0x1F0
    std::uint32_t flags1F4   = 0;     // cCreature+0x1F4
    // Hero-redirect inputs (only consulted for the *target* B):
    bool         isPlayableHero = false; // FUN_004266f0(B.defClass)==hero marker
    bool         hasController  = false; // B+0x1ec != 0
    std::int32_t controllerAiClass = 0;  // *(controller+0x1F0)
};

// Port of FUN_00423580 as a pure predicate: "does A want to attack B?"
//   - Same-object exemption (FUN_00423580:23) is the caller's job (it has the
//     handles); call only for A != B.
//   - Peaceful override on either party (+0x1F4 & 0x40000) forces NOT hostile
//     for plain matrix queries (combat_init.md §4: the bit inverts the verdict;
//     for a non-hostile-intent query the safe, documented effect is "won't aggro").
//   - Hero-redirect: when B is a playable hero with a controller AND A is in the
//     ally cluster, B's effective class becomes the controller's +0x1F0 (so an
//     ally is friendly to the hero's avatar). Reachable only when the raw
//     A-vs-B cell did NOT already short-circuit to "ignore".
//
// NOTE: grudge mode (+0x200 & 0x10000000) and the +0x39c per-target grudge list
//   are NOT modeled here (dynamic per-target state). If the caller knows the
//   pair is in grudge mode, this matrix-only result is not authoritative.
//   TODO(port): add a grudge-aware overload once the +0x39c list ABI is mapped.
inline bool is_hostile(const HostilityMatrix& m,
                       const FactionActor& A,
                       const FactionActor& B) {
    // Peaceful override on either side => never aggro (matrix-query semantics).
    if ((A.flags1F4 & HostilityLayout::kPeacefulOverride) ||
        (B.flags1F4 & HostilityLayout::kPeacefulOverride)) {
        return false;
    }

    // Raw matrix verdict with B's nominal class.
    std::int32_t bClass = B.aiClass;
    bool hostile = m.attacks(A.aiClass, bClass);

    // Hero-redirect: only consulted when the raw cell is "attack" (i.e. the
    // :67 short-circuit did NOT fire) AND A is ally-cluster AND B is a hero
    // with a live controller. Re-evaluate against the controller's class.
    if (hostile && is_ally_cluster(A.aiClass) &&
        B.isPlayableHero && B.hasController) {
        bClass  = B.controllerAiClass;
        hostile = m.attacks(A.aiClass, bClass);
    }
    return hostile;
}

} // namespace faction
} // namespace sacred

#endif // SACRED_SDK_PORTS_HOSTILITY_MATRIX_H
