// SacredSDK / ports / data / hero_tables.h
//
// Core hero progression / definition tables for Sacred Gold, as compile-time
// constexpr data. FUTURE-USE: written but NOT yet wired into SacredSDK.vcxproj
// or any existing TU.
//
// SOURCE (authoritative, already verified):
//   - custom/lua/lib/data/hero_tables.lua  (transcribed verbatim).
//   - sdk/.claude/knowledge/re/data_tables.md  section 4 ("hero_tables.lua").
//   - sdk/.claude/knowledge/refs_native_src.md (hero tables provenance).
//   Ultimately from the community "Sacred Character Modifier" v0.15.0.16
//   (public) sources by LinkZ (GPLv2), under
//   refs_extract/sacred_charmodif_src/...:
//     ExpTable      <- ExpTable.pas         cExpTable           (206 entries)
//     Chars         <- CharsTable.pas       cStr_CharacterClass (10 entries 0..9)
//     Skills        <- SkillsTable.pas      c_st_Skills (EN)    (34 entries 0..33)
//     ClassSkills   <- SkillsTable.pas      c_st_CCSkills       (17 x 9)
//     CombatArts    <- tableCombatArts.pas  table_CombatArts    (123 entries)
//     SurvivalBonus <- tableSurvivalBonus.pas table_SurvivalBonus (79 entries)
//
// These are the same constants the retail engine uses for character save files.
// All values are pure data (no absolute engine VA), so no Sacred_decrypted.exe
// byte-signature check is required.
//
// CLASS IDS (used by Chars, ClassSkills columns, CombatArtDef.class_id):
//   1 Seraphim, 2 Gladiator, 3 Battle Mage, 4 Dark Elf, 5 Wood Elf,
//   6 Vampiress, 7 '-' (vampire form / dash slot), 8 Dwarf, 9 Demon.
//   These match the save-file class byte and creature_types.h hero band ids.
//   (Note: classes.lua uses class bitmasks 1,2,4,...; these are index ids.)
//
// TODO(port): expose to a future engine::data namespace (engine::data::hero).
// TODO(port): wire when the hero save reader/editor needs XP curves, skill
//             names, per-class CA layouts, or the survival-bonus lookup.

#ifndef SACRED_SDK_PORTS_DATA_HERO_TABLES_H
#define SACRED_SDK_PORTS_DATA_HERO_TABLES_H

#include <cstdint>
#include <cstddef>

namespace sacred {
namespace data {
namespace hero {

// ---------------------------------------------------------------------------
// ExpTable: cumulative XP required to reach each level.
// IMPORTANT: 1-based in the source (ExpTable[1] = 0 XP at level 1). Here it is
// stored 0-based, so kExpTable[0] = 0 corresponds to level 1, kExpTable[205]
// corresponds to level 206. Use xp_for_level() to index by 1-based level.
// Values fit in uint32 (max = 2117050200 at level 206); data_tables.md notes
// the .pas declares array[1..206] but initialises exactly 206 values.
// ---------------------------------------------------------------------------
constexpr std::uint32_t kExpTable[] = {
  0, 300, 1200, 3000, 5900, 10200, 16400, 24700, 35600, 49500,
  66900, 88100, 113700, 144300, 180200, 222200, 270800, 326700, 390300, 462500,
  543900, 635100, 737000, 850300, 975800, 1114200, 1266500, 1433400, 1615800, 1814700,
  2030900, 2265400, 2519200, 2793200, 3088600, 3406200, 3747300, 4112900, 4504100, 4922000,
  5367900, 5842800, 6348100, 6884900, 7454500, 8058200, 8697400, 9373200, 10087200, 10840600,
  11634900, 12471500, 13351800, 14277400, 15249700, 16270200, 17340600, 18462300, 19637000, 20866300,
  22151900, 23495300, 24898300, 26362700, 27890000, 29482200, 31141000, 32868300, 34665700, 36535300,
  38478900, 40498300, 42595600, 44772700, 47031600, 49374200, 51802700, 54319000, 56925200, 59623500,
  62415900, 65304600, 68291800, 71379600, 74570400, 77866200, 81269500, 84782500, 88407500, 92146800,
  96002900, 99978000, 104074700, 108295300, 112642300, 117118200, 121725600, 126466800, 131344600, 136361400,
  141519900, 146822700, 152272400, 157871800, 163623500, 169530200, 175594800, 181819900, 188208400, 194763100,
  201486900, 208382500, 215452900, 222701100, 230129800, 237742200, 245541200, 253529900, 261711100, 270088100,
  278663900, 287441500, 296424200, 305615100, 315017400, 324634200, 334468900, 344524600, 354804600, 365312300,
  376050900, 387023800, 398234400, 409686000, 421382200, 433326200, 445521700, 457972100, 470680900, 483651600,
  496887900, 510393200, 524171300, 538225700, 552560100, 567178200, 582083800, 597280400, 612772000, 628562200,
  644654900, 661053900, 677763000, 694786200, 712127300, 729790200, 747779000, 766097500, 784749800, 803739900,
  823071900, 842749700, 862777500, 883159500, 903899600, 925002200, 946471400, 968311500, 990526500, 1013120900,
  1036098900, 1059464700, 1083222800, 1107377500, 1131933200, 1156894200, 1182265100, 1208050200, 1234254000, 1260881100,
  1287935900, 1315423000, 1343347000, 1371712400, 1400524000, 1429786200, 1459503900, 1489681700, 1520324300, 1551436400,
  1583022900, 1615088400, 1647637900, 1680676100, 1714207900, 1748238200, 1782772000, 1817814000, 1853369400, 1889443000,
  1926039900, 1963165100, 2000823600, 2039020600, 2077761100, 2117050200,
};
constexpr std::size_t kExpTableCount = sizeof(kExpTable) / sizeof(kExpTable[0]);
static_assert(kExpTableCount == 206, "hero_tables.h: ExpTable must have 206 entries");

// XP needed to reach a given 1-based level (clamped to [1, 206]).
inline std::uint32_t xp_for_level(int level) {
  if (level < 1) level = 1;
  if (level > static_cast<int>(kExpTableCount)) level = static_cast<int>(kExpTableCount);
  return kExpTable[level - 1];
}

// ---------------------------------------------------------------------------
// Chars: class id -> display name. Index = class id 0..9. 0 and 7 are "-"
// (unknown / unused / vampire-form dash slots). The dash slots carry the
// literal "-" as in the source.
// ---------------------------------------------------------------------------
constexpr const char* const kChars[] = {
  "-",            // 0
  "Seraphim",     // 1
  "Gladiator",    // 2
  "Battle Mage",  // 3
  "Dark Elf",     // 4
  "Wood Elf",     // 5
  "Vampiress",    // 6
  "-",            // 7 (vampire form / dash)
  "Dwarf",        // 8
  "Demon",        // 9
};
constexpr std::size_t kCharsCount = sizeof(kChars) / sizeof(kChars[0]);
static_assert(kCharsCount == 10, "hero_tables.h: Chars must have 10 entries (0..9)");

// ---------------------------------------------------------------------------
// Skills: skill id -> English name. Index = skill id 0..33. Index 0 = "-"
// (none); index 30 is an unused addon slot ("") preserved from the source.
// German/Russian columns exist in SkillsTable.pas but only EN is ported.
// ---------------------------------------------------------------------------
constexpr const char* const kSkills[] = {
  "-",                  // 0
  "Celestial Magic",    // 1
  "Weapon Lore",        // 2
  "Polearms",           // 3
  "Sword Lore",         // 4
  "Axe Lore",           // 5
  "Dual Wield",         // 6
  "Ranged Combat",      // 7
  "Agility",            // 8
  "Parrying",           // 9
  "Constitution",       // 10
  "Armor",              // 11
  "Meditation",         // 12
  "Blade Combat",       // 13
  "Magic Lore",         // 14
  "Fire Magic",         // 15
  "Water Magic",        // 16
  "Earth Magic",        // 17
  "Air Magic",          // 18
  "Moon Magic",         // 19
  "Vampirism",          // 20
  "Trading",            // 21
  "Horse Riding",       // 22
  "Disarm",             // 23
  "Unarmed Combat",     // 24
  "Concentration",      // 25
  "Ballistics",         // 26
  "Trap Lore",          // 27
  "Bloodlust",          // 28
  "Weapon Technology",  // 29
  "",                   // 30 (unused addon slot)
  "Dwarven Lore",       // 31
  "Hell Power",         // 32
  "Forge Lore",         // 33
};
constexpr std::size_t kSkillsCount = sizeof(kSkills) / sizeof(kSkills[0]);
static_assert(kSkillsCount == 34, "hero_tables.h: Skills must have 34 entries (0..33)");

// ---------------------------------------------------------------------------
// ClassSkills: per-class skill layout. kClassSkills[row][col] = skill id.
// row 0..16 are the skill 'slots'; col 0..8 corresponds to class id 1..9
// (col index = class_id - 1; col 6 = class id 7, the unused/dash class, all 0).
// Mirrors c_st_CCSkills (source cols 1..9).
// ---------------------------------------------------------------------------
constexpr std::uint8_t kClassSkills[][9] = {
  {  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  { 14,  2, 14, 13,  8, 20,  0,  2, 14 },
  {  2, 25, 12, 25,  2,  2,  0, 10,  2 },
  { 12,  5, 15, 27, 14, 10,  0, 29, 32 },
  { 25,  6, 17, 26, 12, 14,  0,  5, 10 },
  {  1, 24, 16,  2, 19, 25,  0, 31,  4 },
  {  3,  7, 18,  6,  7,  4,  0, 11, 11 },
  {  4,  4,  2, 10, 25,  5,  0, 33,  8 },
  { 10, 10,  4,  8, 10, 11,  0,  8, 12 },
  {  9, 11,  3, 23, 22,  9,  0, 21,  5 },
  { 22,  8, 22, 22,  3, 23,  0,  7,  6 },
  {  6, 22,  8, 24, 21, 22,  0, 25, 25 },
  { 11, 23, 21, 11,  4,  8,  0,  9, 22 },
  {  7,  3, 10,  4, 11,  3,  0, 24,  9 },
  {  8,  9, 23,  9, 23, 21,  0,  4, 23 },
  { 21, 21,  9, 21,  9,  7,  0, 23,  3 },
  {  0,  0,  0,  0,  0, 28,  0,  0,  0 },
};
constexpr std::size_t kClassSkillRows = sizeof(kClassSkills) / sizeof(kClassSkills[0]);
static_assert(kClassSkillRows == 17, "hero_tables.h: ClassSkills must have 17 rows");

// Skill id for (class_id 1..9, slot 0..16). Returns 0 if out of range.
inline std::uint8_t class_skill(int class_id, int slot) {
  if (class_id < 1 || class_id > 9) return 0;
  if (slot < 0 || slot >= static_cast<int>(kClassSkillRows)) return 0;
  return kClassSkills[slot][class_id - 1];
}

// ---------------------------------------------------------------------------
// CombatArts: combat-art definitions per hero class. Mirrors table_CombatArts.
//   class_id : save-file class id (1=Seraphim .. 9=Demon; see kChars)
//   num      : the per-class CA slot number (1-based)
//   catype   : 1 = spell (RunenMagie), 2 = combat art (Kampftechnik)
//   id1      : primary CA opcode (matches the hi word in combat_arts.h)
//   id2      : secondary opcode, 0xFFFF = none
// NOTE: some name values carry a backslash (Vampiress dual-form arts, e.g.
// "Turn into Vampire\Knight"); they are escaped here as C++ string literals.
// ---------------------------------------------------------------------------
struct CombatArtDef {
  std::uint8_t  class_id;
  std::uint8_t  num;
  std::uint8_t  catype;   // 1 = spell, 2 = combat art
  std::uint16_t id1;      // primary opcode
  std::uint16_t id2;      // secondary opcode, 0xFFFF = none
  const char*   name;
};

constexpr CombatArtDef kCombatArts[] = {
  { 1,  1, 2, 0x03F9, 0xFFFF, "Combat Jump" },
  { 1,  2, 2, 0x03FA, 0xFFFF, "BeeEffGee" },
  { 1,  3, 2, 0x041E, 0xFFFF, "Hunter-seeker" },
  { 1,  4, 2, 0x03FC, 0xFFFF, "Whirling Hit" },
  { 1,  5, 2, 0x042D, 0xFFFF, "Multi-hit" },
  { 1,  6, 2, 0x042E, 0xFFFF, "Hard Hit" },
  { 1,  7, 2, 0x042F, 0xFFFF, "Attack" },
  { 1,  8, 2, 0x0430, 0xFFFF, "Combat Kick" },
  { 1,  9, 1, 0x0012, 0xFFFF, "Irritation" },
  { 1, 10, 1, 0x0013, 0xFFFF, "Conversion" },
  { 1, 11, 1, 0x0011, 0xFFFF, "Rotating Blades of Light" },
  { 1, 12, 1, 0x000D, 0xFFFF, "Lightning Strike" },
  { 1, 13, 1, 0x0016, 0xFFFF, "Shield of Light" },
  { 1, 14, 1, 0x0014, 0xFFFF, "Light" },
  { 1, 15, 1, 0x000F, 0xFFFF, "Celestial Light" },
  { 1, 16, 1, 0x0015, 0xFFFF, "Strength of Faith" },
  { 1, 17, 2, 0x03FB, 0xFFFF, "Energy Bolts" },
  { 2,  1, 2, 0x0422, 0xFFFF, "Hard Hit" },
  { 2,  2, 2, 0x0423, 0xFFFF, "Attack" },
  { 2,  3, 2, 0x0421, 0xFFFF, "Multi-hit" },
  { 2,  4, 2, 0x03EB, 0xFFFF, "Stomping Jump" },
  { 2,  5, 2, 0x03F4, 0xFFFF, "Fist of the Gods" },
  { 2,  6, 2, 0x0424, 0xFFFF, "Combat kick" },
  { 2,  7, 2, 0x03F2, 0xFFFF, "Back-breaker" },
  { 2,  8, 2, 0x03F5, 0xFFFF, "Throwing Blades" },
  { 2,  9, 2, 0x03F6, 0xFFFF, "Awe" },
  { 2, 10, 2, 0x03F7, 0xFFFF, "Heroic Courage" },
  { 2, 11, 2, 0x03F8, 0xFFFF, "Dagger Stare" },
  { 3,  1, 1, 0x0005, 0xFFFF, "Fire Ball" },
  { 3,  2, 1, 0x0018, 0xFFFF, "Flame Skin" },
  { 3,  3, 1, 0x0017, 0xFFFF, "Purgatory" },
  { 3,  4, 1, 0x000B, 0xFFFF, "Fire Spiral" },
  { 3,  5, 1, 0x0002, 0xFFFF, "Stone Skin" },
  { 3,  6, 1, 0x0019, 0xFFFF, "Petrification" },
  { 3,  7, 1, 0x0010, 0xFFFF, "Circle of Fear" },
  { 3,  8, 1, 0x001A, 0xFFFF, "Meteor Strom" },
  { 3,  9, 1, 0x001C, 0xFFFF, "Whirlwind" },
  { 3, 10, 1, 0x001D, 0xFFFF, "Gust of Wind" },
  { 3, 11, 1, 0x0001, 0xFFFF, "Phase Shift" },
  { 3, 12, 1, 0x0021, 0xFFFF, "Lightning Strike" },
  { 3, 13, 1, 0x001B, 0xFFFF, "Cataract of Agility" },
  { 3, 14, 1, 0x0022, 0xFFFF, "Ice Shards" },
  { 3, 15, 1, 0x0023, 0xFFFF, "Ring of Ice" },
  { 3, 16, 1, 0x0004, 0xFFFF, "Water Form" },
  { 3, 17, 1, 0x0007, 0xFFFF, "Spiritual Healing" },
  { 3, 18, 1, 0x001E, 0xFFFF, "Shield Wall" },
  { 3, 19, 1, 0x001F, 0xFFFF, "Ghost Meadow" },
  { 3, 20, 1, 0x0020, 0xFFFF, "Reiki" },
  { 4,  1, 2, 0x0412, 0xFFFF, "Cobra" },
  { 4,  2, 2, 0x0413, 0xFFFF, "Mongoose" },
  { 4,  3, 2, 0x0414, 0xFFFF, "Soul Catcher" },
  { 4,  4, 2, 0x0416, 0xFFFF, "Pak-Nakor (Magic Deflection)" },
  { 4,  5, 2, 0x0415, 0xFFFF, "Pak-Dain (Projectile Deflection)" },
  { 4,  6, 2, 0x0429, 0xFFFF, "Sudden Fury (Multi-hit)" },
  { 4,  7, 2, 0x042A, 0xFFFF, "Revege (Hard Hit)" },
  { 4,  8, 2, 0x042B, 0xFFFF, "Rage (Attack)" },
  { 4,  9, 2, 0x042C, 0xFFFF, "Combat Kick" },
  { 4, 10, 2, 0x041C, 0xFFFF, "Poison Mist" },
  { 4, 11, 2, 0x041B, 0xFFFF, "Confusion" },
  { 4, 12, 2, 0x041D, 0xFFFF, "Battle Fog" },
  { 4, 13, 2, 0x0419, 0xFFFF, "Explosive Charge" },
  { 4, 14, 2, 0x041A, 0xFFFF, "Bottomless Pit" },
  { 4, 15, 2, 0x0417, 0xFFFF, "Testosterone" },
  { 4, 16, 2, 0x0418, 0xFFFF, "Adrenaline" },
  { 5,  1, 2, 0x03EF, 0xFFFF, "Knockback Arrow" },
  { 5,  2, 2, 0x03EE, 0xFFFF, "Spider Arrow" },
  { 5,  3, 2, 0x03F0, 0xFFFF, "Penetrating Arrow" },
  { 5,  4, 2, 0x03EC, 0xFFFF, "Explosive Arrow" },
  { 5,  5, 2, 0x0425, 0xFFFF, "Multi-hit" },
  { 5,  6, 2, 0x0426, 0xFFFF, "Hard Hit" },
  { 5,  7, 2, 0x0427, 0xFFFF, "Eye for an Eye (Attack)" },
  { 5,  8, 2, 0x03ED, 0xFFFF, "Multiple Shot" },
  { 5,  9, 1, 0x0025, 0xFFFF, "Quick as a Flash" },
  { 5, 10, 1, 0x0026, 0xFFFF, "Thorn Bush" },
  { 5, 11, 1, 0x0003, 0xFFFF, "Companion of the Woods" },
  { 5, 12, 1, 0x0027, 0xFFFF, "Poisoned Tendrils" },
  { 5, 13, 1, 0x0028, 0xFFFF, "Plant Cage" },
  { 5, 14, 1, 0x0024, 0xFFFF, "Transformation" },
  { 5, 15, 1, 0x0029, 0xFFFF, "Call of the Ancestors" },
  { 5, 16, 1, 0x002A, 0xFFFF, "Regeneration" },
  { 6,  1, 2, 0x03FE, 0x03FF, "Turn into Vampire\\\\Knight" },
  { 6,  2, 2, 0x0400, 0x0401, "Mind Control" },
  { 6,  3, 2, 0x0402, 0x0403, "Wolf Call" },
  { 6,  4, 2, 0x0404, 0x0405, "Time Control" },
  { 6,  5, 2, 0x0431, 0x040E, "Whirling Claws \\\\ Multi-Hit" },
  { 6,  6, 2, 0x0432, 0x040F, "Claws of Death \\\\ Hard Hit" },
  { 6,  7, 2, 0x0433, 0x0410, "Ripping Claws \\\\ Attack" },
  { 6,  8, 2, 0x0434, 0x0411, "Combat Kick" },
  { 6,  9, 2, 0x0406, 0xFFFF, "Blood Bite" },
  { 6, 10, 2, 0x0407, 0xFFFF, "Master Bite" },
  { 6, 11, 2, 0x0408, 0xFFFF, "Combat Jump" },
  { 6, 12, 2, 0x0409, 0xFFFF, "Blood Kiss" },
  { 6, 13, 2, 0x040A, 0xFFFF, "Awaken Dead" },
  { 6, 14, 2, 0x040B, 0xFFFF, "Bats (Blood Swarm)" },
  { 6, 15, 2, 0x040C, 0xFFFF, "Bats (Confusion)" },
  { 6, 16, 2, 0x040D, 0xFFFF, "Bats (Guard)" },
  { 8,  1, 2, 0x0437, 0xFFFF, "Heavy Blow" },
  { 8,  2, 2, 0x0436, 0xFFFF, "Wrath" },
  { 8,  3, 2, 0x0438, 0xFFFF, "Assault" },
  { 8,  4, 2, 0x043A, 0xFFFF, "Battle Rage" },
  { 8,  5, 2, 0x0441, 0xFFFF, "War Cry" },
  { 8,  6, 2, 0x043B, 0xFFFF, "Recoil" },
  { 8,  7, 2, 0x043C, 0xFFFF, "Vehemence" },
  { 8,  8, 1, 0x0037, 0xFFFF, "Greed" },
  { 8,  9, 2, 0x043D, 0xFFFF, "Flame Thrower" },
  { 8, 10, 2, 0x043E, 0xFFFF, "Mortar Grenade" },
  { 8, 11, 2, 0x043F, 0xFFFF, "Cannon Blast" },
  { 8, 12, 1, 0x0038, 0xFFFF, "Landmine" },
  { 8, 13, 1, 0x0039, 0xFFFF, "Entrench" },
  { 8, 14, 1, 0x003A, 0xFFFF, "Dwarven Armor" },
  { 8, 15, 1, 0x003B, 0xFFFF, "Dwarven Steel" },
  { 9,  1, 2, 0x0443, 0x0446, "Soaring Daemon" },
  { 9,  2, 2, 0x0449, 0x0444, "Battle Daemon" },
  { 9,  3, 2, 0x044E, 0x044A, "Fire Daemon" },
  { 9,  4, 2, 0x044B, 0x044D, "Poison Daemon" },
  { 9,  5, 2, 0x044C, 0x044F, "Energy Daemon" },
  { 9,  6, 1, 0x003F, 0xFFFF, "Infernal Power" },
  { 9,  7, 1, 0x0040, 0xFFFF, "Hell Sphere" },
  { 9,  8, 1, 0x003C, 0xFFFF, "Blazing Disc" },
  { 9,  9, 1, 0x003E, 0xFFFF, "Call of Death" },
  { 9, 10, 1, 0x0041, 0xFFFF, "Tentacles" },
  { 9, 11, 1, 0x0042, 0xFFFF, "Abysmal Choir" },
  { 9, 12, 1, 0x0043, 0xFFFF, "Dread" },
};
constexpr std::size_t kCombatArtCount = sizeof(kCombatArts) / sizeof(kCombatArts[0]);
static_assert(kCombatArtCount == 123, "hero_tables.h: CombatArts must have 123 entries");

// ---------------------------------------------------------------------------
// SurvivalBonus: for a survival-bonus death-count value v, the matching row is
// the one with min <= v <= max; p is the bonus percentage. 0-based, 79 rows.
// Mirrors table_SurvivalBonus. Use survival_bonus_pct(v).
// ---------------------------------------------------------------------------
struct SurvivalBonusRange {
  std::uint16_t min;
  std::uint16_t max;
  std::uint8_t  p;   // bonus percentage
};

constexpr SurvivalBonusRange kSurvivalBonus[] = {
  { 0x0, 0x0, 0 },
  { 0x1, 0x1, 2 },
  { 0x2, 0x2, 5 },
  { 0x3, 0x3, 8 },
  { 0x4, 0x4, 11 },
  { 0x5, 0x5, 14 },
  { 0x6, 0x6, 16 },
  { 0x7, 0x7, 18 },
  { 0x8, 0x8, 21 },
  { 0x9, 0x9, 23 },
  { 0xA, 0xA, 25 },
  { 0xB, 0xB, 27 },
  { 0xC, 0xC, 29 },
  { 0xD, 0xD, 31 },
  { 0xE, 0xE, 32 },
  { 0xF, 0xF, 34 },
  { 0x10, 0x10, 35 },
  { 0x11, 0x11, 37 },
  { 0x12, 0x12, 38 },
  { 0x13, 0x13, 40 },
  { 0x14, 0x14, 41 },
  { 0x15, 0x15, 42 },
  { 0x16, 0x16, 43 },
  { 0x17, 0x17, 44 },
  { 0x18, 0x18, 45 },
  { 0x19, 0x19, 46 },
  { 0x1A, 0x1A, 47 },
  { 0x1B, 0x1B, 48 },
  { 0x1C, 0x1C, 49 },
  { 0x1D, 0x1D, 50 },
  { 0x1E, 0x1E, 51 },
  { 0x1F, 0x20, 52 },
  { 0x21, 0x21, 53 },
  { 0x22, 0x22, 54 },
  { 0x23, 0x24, 55 },
  { 0x25, 0x25, 56 },
  { 0x26, 0x27, 57 },
  { 0x28, 0x28, 58 },
  { 0x29, 0x2A, 59 },
  { 0x2B, 0x2C, 60 },
  { 0x2D, 0x2E, 61 },
  { 0x2F, 0x30, 62 },
  { 0x31, 0x32, 63 },
  { 0x33, 0x34, 64 },
  { 0x35, 0x36, 65 },
  { 0x37, 0x39, 66 },
  { 0x3A, 0x3B, 67 },
  { 0x3C, 0x3F, 68 },
  { 0x40, 0x41, 69 },
  { 0x42, 0x45, 70 },
  { 0x46, 0x48, 71 },
  { 0x49, 0x4C, 72 },
  { 0x4D, 0x50, 73 },
  { 0x51, 0x54, 74 },
  { 0x55, 0x59, 75 },
  { 0x5A, 0x5E, 76 },
  { 0x5F, 0x64, 77 },
  { 0x65, 0x6A, 78 },
  { 0x6B, 0x70, 79 },
  { 0x71, 0x78, 80 },
  { 0x79, 0x80, 81 },
  { 0x81, 0x89, 82 },
  { 0x8A, 0x93, 83 },
  { 0x94, 0x9F, 84 },
  { 0xA0, 0xAD, 85 },
  { 0xAE, 0xBC, 86 },
  { 0xBD, 0xCE, 87 },
  { 0xCF, 0xE3, 88 },
  { 0xE4, 0xFD, 89 },
  { 0xFE, 0x10C, 90 },
  { 0x10D, 0x143, 91 },
  { 0x144, 0x176, 92 },
  { 0x177, 0x1B8, 93 },
  { 0x1B9, 0x216, 94 },
  { 0x217, 0x2A3, 95 },
  { 0x2A4, 0x38D, 96 },
  { 0x38E, 0x562, 97 },
  { 0x563, 0xAE0, 98 },
  { 0xAE1, 0xFFFF, 99 },
};
constexpr std::size_t kSurvivalBonusCount = sizeof(kSurvivalBonus) / sizeof(kSurvivalBonus[0]);
static_assert(kSurvivalBonusCount == 79, "hero_tables.h: SurvivalBonus must have 79 entries");

// Survival-bonus percentage for a death-count value v (0 if no row matches,
// though the last row covers up to 0xFFFF).
inline std::uint8_t survival_bonus_pct(std::uint16_t v) {
  for (std::size_t i = 0; i < kSurvivalBonusCount; ++i) {
    if (v >= kSurvivalBonus[i].min && v <= kSurvivalBonus[i].max)
      return kSurvivalBonus[i].p;
  }
  return 0;
}

}  // namespace hero
}  // namespace data
}  // namespace sacred

#endif  // SACRED_SDK_PORTS_DATA_HERO_TABLES_H
