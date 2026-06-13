// SacredSDK / ports / data / companions.h
//
// Per-class summonable companion resources for Sacred Gold, as a compile-time
// constexpr table. FUTURE-USE: written but NOT yet wired into the project.
//
// SOURCE (authoritative, already verified):
//   - custom/lua/lib/data/companions.lua  (4 rows, verified;
//     itself derived from refs/'sacred_modding - companions.csv').
//   - sdk/.claude/knowledge/re/data_tables.md  §3 ("companions.lua").
//
// Only four hero classes have a named companion creature:
//   Battle Mage, Dark Elf, Wood Elf, Vampiress.
// (Vampiress lists 3 name handles; the other three list 2.)
//
// ID ENCODING (IMPORTANT):
//   All values are global.res string/model HANDLES in the 0x0019xxxx /
//   0x001Axxxx band -- NOT creature type ids. Resolve them through global.res
//   to get the actual model / display string.
//     model_res  = res handle for the companion's model
//     name_res[] = ordered res handles for the companion's name variants
//                  (engine picks one, typically by index)
//
// `class_id` reuses the hero band ids from creature_types.h (1..9):
//   3 = Battle Mage, 4 = Dark Elf, 5 = Wood Elf, 6 = Vampiress.
//
// This table unblocks the companion roster panel (see session-state memo).
//
// TODO(port): expose to a future engine::data namespace (engine::data::companions).
// TODO(port): wire when the companion roster panel resolves model+name via global.res.

#ifndef SACRED_SDK_PORTS_DATA_COMPANIONS_H
#define SACRED_SDK_PORTS_DATA_COMPANIONS_H

#include <cstdint>
#include <cstddef>

namespace sacred {
namespace data {

// A global.res handle (0x0019xxxx / 0x001Axxxx band). Alias for clarity; this
// header does NOT resolve it -- that is the engine's global.res job.
using ResHandle = std::uint32_t;

// Max name variants any class lists (Vampiress = 3). Fixed-size avoids a
// heap/std::vector dependency. `name_count` says how many of name_res[] are
// valid; trailing slots are 0.
constexpr std::size_t kMaxCompanionNames = 3;

struct Companion {
  std::uint8_t class_id;                       // hero id 1..9 (see creature_types.h)
  const char*  class_name;                     // display name (matches creatures hero)
  ResHandle    model_res;                       // companion model res handle
  std::uint8_t name_count;                      // valid entries in name_res[]
  ResHandle    name_res[kMaxCompanionNames];    // name-variant res handles
};

// 4 entries, transcribed from companions.lua.
constexpr Companion kCompanions[] = {
  { 3, "Battle Mage", 0x001A0153u, 2, { 0x001A014Cu, 0x001A032Cu, 0u } },
  { 4, "Dark Elf",    0x0019FA57u, 2, { 0x0019FA50u, 0x0019F8F1u, 0u } },
  { 5, "Wood Elf",    0x0019FA3Du, 2, { 0x0019FA36u, 0x0019F8D7u, 0u } },
  { 6, "Vampiress",   0x0019F6EAu, 3, { 0x0019F6E3u, 0x0019F46Du, 0x0019F418u } },
};

constexpr std::size_t kCompanionCount =
    sizeof(kCompanions) / sizeof(kCompanions[0]);

// Cross-check: row count must match the verified Lua source (4 rows).
static_assert(kCompanionCount == 4,
              "companions.h: expected 4 rows (see data_tables.md §3)");

// Lookup by hero class id (1..9). Returns nullptr if the class has no companion.
inline const Companion* find_by_class_id(std::uint8_t class_id) {
  for (std::size_t i = 0; i < kCompanionCount; ++i) {
    if (kCompanions[i].class_id == class_id) return &kCompanions[i];
  }
  return nullptr;
}

}  // namespace data
}  // namespace sacred

#endif  // SACRED_SDK_PORTS_DATA_COMPANIONS_H
