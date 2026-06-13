// balance_bin.h — typed accessor over Sacred Gold's `balance.bin` tuning blob.
//
// SOURCE:
//   sdk/.claude/knowledge/refs_magician_net.md §3 ("balance.bin format
//     (SacredMagician)") — the int/float offset tables transcribed below were
//     reverse-engineered by SacredMagician (Kotlin GUI editor, LoadBalanceBinData.kt
//     / SaveBalanceBinData.kt). All fields are 4-byte little-endian; two kinds:
//     int32 and float32. In-place patch, NO checksum / structure rewrite needed.
//   sdk/.claude/knowledge/refs_formats_data.md §B4 — independent VB6
//     "Balance.bin_Editor" region-spawn-count offsets (0x5779.. SOUTHKERN ...),
//     cross-checked against the Magician region offsets (22392.. = 0x5778; note
//     the editors index a different field within the per-region block).
//
// NOTE: `balance.bin` is an ON-DISK data file (no engine VA). There is nothing
//   to byte-sig against Sacred_decrypted.exe here — the validation surface is the
//   file itself (offsets verified by the two community editors above).
//
// TODO(port): wire when the SDK gains a balance-tuning feature (live difficulty/
//   XP/skill-gate editing). Until then this is a FUTURE-USE, standalone accessor:
//   header-only, no engine deps. Do NOT add to SacredSDK.vcxproj or #include it
//   from existing TUs.
//
// Design: caller owns the file buffer (a contiguous `uint8_t*` of the loaded
//   balance.bin). This class only reads/writes 4-byte LE ints/floats at absolute
//   byte offsets and exposes the known offset constants. No allocation, no I/O.
//
// C++14, MSVC.

#ifndef SACRED_SDK_PORTS_BALANCE_BIN_H
#define SACRED_SDK_PORTS_BALANCE_BIN_H

#include <cstdint>
#include <cstddef>
#include <cstring>   // std::memcpy

namespace sacred {
namespace balance {

// ---------------------------------------------------------------------------
// Known absolute byte offsets (little-endian). From refs_magician_net.md §3.
// ---------------------------------------------------------------------------

// Player skill-unlock levels (int32). The character-level at which each of the
// six skill slots unlocks.
struct SkillUnlockOffsets {
    static constexpr std::size_t kSkill1 = 780;
    static constexpr std::size_t kSkill2 = 784;
    static constexpr std::size_t kSkill3 = 788;
    static constexpr std::size_t kSkill4 = 792;
    static constexpr std::size_t kSkill5 = 796;
    static constexpr std::size_t kSkill6 = 800;
    // 1-based index -> offset (i in [1..6]); returns 0 (invalid) out of range.
    static constexpr std::size_t at(unsigned i) {
        return (i >= 1 && i <= 6) ? (kSkill1 + (i - 1) * 4u) : 0u;
    }
};

// Difficulty min/max character-level gates (int32). silver/gold/platinum/niobium
// minimum level to enter; platinum max.
struct DifficultyGateOffsets {
    static constexpr std::size_t kSilverMin   = 2488;
    static constexpr std::size_t kGoldMin     = 2492;
    static constexpr std::size_t kPlatinumMin = 2496;
    static constexpr std::size_t kNiobiumMin  = 2500;
    static constexpr std::size_t kPlatinumMax = 2504;
};

// Per-difficulty multiplier kind (rows) and tier (columns). FLOAT32.
// Layout note (from §3): within each row, Bronze sits +20 bytes above Silver;
// Gold/Platinum/Niobium/Global each step +4 from Silver. The table below stores
// the Silver base offset per row; per-tier offsets are derived in `MultiplierOffsets`.
enum class MultStat {
    AttackDefense = 0,  // "AW/VW"
    HitPoints     = 1,
    Damage        = 2,
    Resistance    = 3,
    Experience    = 4,
};

enum class MultTier {
    Silver = 0,    // base
    Bronze = 1,    // base + 20
    Gold   = 2,    // base + 4
    Platinum = 3,  // base + 8
    Niobium  = 4,  // base + 12
    Global = 5,    // base + 16 (a.k.a. server/global; Experience row = "server")
};

struct MultiplierOffsets {
    // Silver-base offset per MultStat, in declaration order (refs §3 table).
    static constexpr std::size_t kSilverBase[5] = {
        1812, // AttackDefense
        1836, // HitPoints
        1860, // Damage
        1884, // Resistance
        1908, // Experience
    };

    // Tier delta from the Silver base. Bronze is the irregular one (+20).
    static constexpr std::size_t tier_delta(MultTier t) {
        return (t == MultTier::Silver)   ? 0u
             : (t == MultTier::Bronze)   ? 20u
             : (t == MultTier::Gold)     ? 4u
             : (t == MultTier::Platinum) ? 8u
             : (t == MultTier::Niobium)  ? 12u
             : /* Global */                16u;
    }

    static constexpr std::size_t at(MultStat stat, MultTier tier) {
        return kSilverBase[static_cast<int>(stat)] + tier_delta(tier);
    }
};

// Region monster-quantity counts (int32), one per region, stride 64 bytes.
// From refs_magician_net.md §3. (refs_formats_data.md §B4 lists the same regions
// at neighbouring offsets — a different field inside each 0x40-byte region block.)
enum class Region {
    SouthCenter      = 0,
    NorthCenter      = 1,
    Swamp            = 2,
    West             = 3,
    North            = 4,
    Lava             = 5,
    Shaddar          = 6,
    UpperUnderworld  = 7,
    LowerUnderworld  = 8,
};

struct RegionCountOffsets {
    static constexpr std::size_t kBase   = 22392; // SouthCenter
    static constexpr std::size_t kStride = 64;
    static constexpr std::size_t at(Region r) {
        return kBase + static_cast<std::size_t>(r) * kStride;
    }
};

// ---------------------------------------------------------------------------
// BalanceBin — non-owning typed view over a loaded balance.bin buffer.
// ---------------------------------------------------------------------------
class BalanceBin {
public:
    BalanceBin() = default;
    // `data` must point at the start of a loaded balance.bin; `size` is its byte
    // length. The buffer is NOT copied or owned; it must outlive this object.
    BalanceBin(std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool valid() const { return data_ != nullptr; }
    std::size_t size() const { return size_; }
    std::uint8_t* data() const { return data_; }

    // Bounds check: is a 4-byte field at `off` fully inside the buffer?
    bool in_bounds(std::size_t off) const {
        return data_ != nullptr && off + 4 <= size_;
    }

    // --- raw LE 4-byte accessors -------------------------------------------
    // GetInt/GetFloat return `fallback` if the offset is out of bounds.
    std::int32_t GetInt(std::size_t off, std::int32_t fallback = 0) const {
        std::int32_t v;
        if (!read_raw(off, &v)) return fallback;
        return v;
    }
    float GetFloat(std::size_t off, float fallback = 0.0f) const {
        float v;
        if (!read_raw(off, &v)) return fallback;
        return v;
    }

    // SetInt/SetFloat patch in place; return false if out of bounds (no write).
    // No checksum recompute (the format has none — §3).
    bool SetInt(std::size_t off, std::int32_t value) { return write_raw(off, value); }
    bool SetFloat(std::size_t off, float value)       { return write_raw(off, value); }

    // --- convenience typed accessors over the known tables -----------------
    std::int32_t skill_unlock(unsigned slot1to6) const {
        return GetInt(SkillUnlockOffsets::at(slot1to6));
    }
    bool set_skill_unlock(unsigned slot1to6, std::int32_t level) {
        std::size_t off = SkillUnlockOffsets::at(slot1to6);
        return off ? SetInt(off, level) : false;
    }

    float multiplier(MultStat stat, MultTier tier) const {
        return GetFloat(MultiplierOffsets::at(stat, tier));
    }
    bool set_multiplier(MultStat stat, MultTier tier, float v) {
        return SetFloat(MultiplierOffsets::at(stat, tier), v);
    }

    std::int32_t region_count(Region r) const {
        return GetInt(RegionCountOffsets::at(r));
    }
    bool set_region_count(Region r, std::int32_t count) {
        return SetInt(RegionCountOffsets::at(r), count);
    }

private:
    template <typename T>
    bool read_raw(std::size_t off, T* out) const {
        static_assert(sizeof(T) == 4, "balance.bin fields are 4 bytes");
        if (!in_bounds(off)) return false;
        // balance.bin is little-endian; on x86 (the only Sacred target) a plain
        // memcpy reproduces the byte order. If this header is ever built on a
        // big-endian host the bytes would need swapping — TODO(port): add a
        // byteswap if cross-endian support is ever required.
        std::memcpy(out, data_ + off, 4);
        return true;
    }
    template <typename T>
    bool write_raw(std::size_t off, T value) {
        static_assert(sizeof(T) == 4, "balance.bin fields are 4 bytes");
        if (!in_bounds(off)) return false;
        std::memcpy(data_ + off, &value, 4);
        return true;
    }

    std::uint8_t* data_ = nullptr;
    std::size_t   size_ = 0;
};

} // namespace balance
} // namespace sacred

#endif // SACRED_SDK_PORTS_BALANCE_BIN_H
