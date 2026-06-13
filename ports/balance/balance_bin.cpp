// balance_bin.cpp — out-of-line storage for BalanceBin's constexpr tables.
//
// SOURCE: see balance_bin.h header banner (refs_magician_net.md §3,
//   refs_formats_data.md §B4).
//
// TODO(port): wire when the SDK gains a balance-tuning feature. This TU is
//   FUTURE-USE: do NOT add it to SacredSDK.vcxproj yet.
//
// Under C++14 a `static constexpr` data member that is ODR-used (e.g. its
// address taken, or a non-constexpr context reads kSilverBase[i]) needs a
// namespace-scope definition. These provide it so the header can be used in
// any context without linker errors. (Harmless/redundant under C++17 inline
// variables, but we target C++14/MSVC.)

#include "balance_bin.h"

namespace sacred {
namespace balance {

constexpr std::size_t SkillUnlockOffsets::kSkill1;
constexpr std::size_t SkillUnlockOffsets::kSkill2;
constexpr std::size_t SkillUnlockOffsets::kSkill3;
constexpr std::size_t SkillUnlockOffsets::kSkill4;
constexpr std::size_t SkillUnlockOffsets::kSkill5;
constexpr std::size_t SkillUnlockOffsets::kSkill6;

constexpr std::size_t DifficultyGateOffsets::kSilverMin;
constexpr std::size_t DifficultyGateOffsets::kGoldMin;
constexpr std::size_t DifficultyGateOffsets::kPlatinumMin;
constexpr std::size_t DifficultyGateOffsets::kNiobiumMin;
constexpr std::size_t DifficultyGateOffsets::kPlatinumMax;

constexpr std::size_t MultiplierOffsets::kSilverBase[5];

constexpr std::size_t RegionCountOffsets::kBase;
constexpr std::size_t RegionCountOffsets::kStride;

} // namespace balance
} // namespace sacred
