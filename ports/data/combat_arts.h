// SacredSDK / ports / data / combat_arts.h
//
// Combat-art / spell id table for Sacred Gold, as a compile-time constexpr
// table. FUTURE-USE: written but NOT yet wired into SacredSDK.vcxproj or any TU.
//
// SOURCE (authoritative, already verified):
//   - custom/lua/lib/data/combat_arts.lua  (156 rows, transcribed verbatim;
//     itself derived from refs/'sacred_modding - combat_arts.csv').
//   - sdk/.claude/knowledge/re/data_tables.md  §2 ("combat_arts.lua").
//
// ID ENCODING:
//   The map KEY is the packed 32-bit combat-art id (the CSV 'ID' column, dec).
//   It packs two 16-bit words:
//        hi = (id >> 16) & 0xFFFF   -- the combat-art opcode / spell id
//        lo =  id        & 0xFFFF   -- modifier / variant selector
//   e.g. 65536    = 0x0001_0000  hi=0x0001 lo=0x0000  (Phase Shift)
//        65732609 = 0x03EB_0001  hi=0x03EB lo=0x0001  (Stomping Jump)
//   The `hi` word matches `id1` in hero_tables.h (table_CombatArts) and the
//   same (modifier<<16)|opcode packing recurs in weapon_bonus_names.h.
//
// This is pure data + arithmetic; no absolute engine VA, so no
// Sacred_decrypted.exe byte-signature check is required.
//
// TODO(port): expose to a future engine::data namespace (engine::data::combat_arts).
// TODO(port): wire when a CA/spell picker or save-file CA inspector needs id->name.

#ifndef SACRED_SDK_PORTS_DATA_COMBAT_ARTS_H
#define SACRED_SDK_PORTS_DATA_COMBAT_ARTS_H

#include <cstdint>
#include <cstddef>

namespace sacred {
namespace data {

// One combat art / spell, keyed by the packed 32-bit id.
//   class_name : owning class as given in the CSV ("?" = unknown / generic,
//                e.g. monster spells with no confirmed owner). Never null.
//   note       : optional CSV note (nullptr when none).
struct CombatArt {
  std::uint32_t packed;      // the 32-bit key
  std::uint16_t hi;          // (packed >> 16) & 0xFFFF -- opcode / spell id
  std::uint16_t lo;          // packed & 0xFFFF -- modifier / variant
  const char*   name;
  const char*   class_name;
  const char*   note;        // may be nullptr
};

// Split a packed id into (hi, lo). Mirrors combat_arts.lua M.split().
struct PackedWords { std::uint16_t hi; std::uint16_t lo; };
constexpr PackedWords split(std::uint32_t packed) {
  return PackedWords{ static_cast<std::uint16_t>((packed >> 16) & 0xFFFFu),
                      static_cast<std::uint16_t>(packed & 0xFFFFu) };
}
constexpr std::uint32_t pack(std::uint16_t hi, std::uint16_t lo) {
  return (static_cast<std::uint32_t>(hi) << 16) | lo;
}

// 156 entries, transcribed from combat_arts.lua (packed-id ascending).
constexpr CombatArt kCombatArts[] = {
  { 65536u,     0x0001, 0x0000, "Phase Shift", "Battle Mage", nullptr },
  { 131072u,    0x0002, 0x0000, "Stoneskin", "Battle Mage", nullptr },
  { 196608u,    0x0003, 0x0000, "Companion of the Woods", "Wood Elf", nullptr },
  { 262144u,    0x0004, 0x0000, "Water Form", "Battle Mage", nullptr },
  { 327680u,    0x0005, 0x0000, "Fireball", "Battle Mage", nullptr },
  { 393216u,    0x0006, 0x0000, "Pentagram (Bottomless Pit Spell)", "?", "= Bottomless Pit" },
  { 458752u,    0x0007, 0x0000, "Spiritual Healing", "Battle Mage", nullptr },
  { 524288u,    0x0008, 0x0000, "Ball Lightning", "?", "Doesn't seem to do anything" },
  { 589824u,    0x0009, 0x0000, "Power-up", "?", "Doesn't seem to do anything" },
  { 655360u,    0x000A, 0x0000, "Morph", "?", "Doesn't seem to do anything" },
  { 720896u,    0x000B, 0x0000, "Fire Spiral", "Battle Mage", nullptr },
  { 786432u,    0x000C, 0x0000, "Ground Poison (Poison Mist Spell)", "Lich?", "= Poison Mist" },
  { 851968u,    0x000D, 0x0000, "Lightning Bolt", "Seraphim", nullptr },
  { 917504u,    0x000E, 0x0000, "Breeze", "?", "Doesn't seem to do anything" },
  { 983040u,    0x000F, 0x0000, "Celestial Light", "Seraphim", nullptr },
  { 1048576u,   0x0010, 0x0000, "Circle of Fear", "Battle Mage", nullptr },
  { 1114112u,   0x0011, 0x0000, "Rotating Blades of Light", "Seraphim", nullptr },
  { 1179648u,   0x0012, 0x0000, "Irritation", "Seraphim", nullptr },
  { 1245184u,   0x0013, 0x0000, "Conversion", "Seraphim", nullptr },
  { 1310720u,   0x0014, 0x0000, "Light", "Seraphim", nullptr },
  { 1376256u,   0x0015, 0x0000, "Strength of Faith", "Seraphim", nullptr },
  { 1441792u,   0x0016, 0x0000, "Light Shield", "Seraphim", nullptr },
  { 1507328u,   0x0017, 0x0000, "Purgatory", "Battle Mage", nullptr },
  { 1572864u,   0x0018, 0x0000, "Flameskin", "Battle Mage", nullptr },
  { 1638400u,   0x0019, 0x0000, "Petrification", "Battle Mage", nullptr },
  { 1703936u,   0x001A, 0x0000, "Meteor Storm", "Battle Mage", nullptr },
  { 1769472u,   0x001B, 0x0000, "Cataract of Agility", "Battle Mage", nullptr },
  { 1835008u,   0x001C, 0x0000, "Whirlwind", "Battle Mage", nullptr },
  { 1900544u,   0x001D, 0x0000, "Gust of Wind", "Battle Mage", nullptr },
  { 1966080u,   0x001E, 0x0000, "Shield Wall", "Battle Mage", nullptr },
  { 2031616u,   0x001F, 0x0000, "Ghost Meadow", "Battle Mage", nullptr },
  { 2097152u,   0x0020, 0x0000, "Reiki", "Battle Mage", nullptr },
  { 2162688u,   0x0021, 0x0000, "Lighting Strike", "Battle Mage", nullptr },
  { 2228224u,   0x0022, 0x0000, "Ice Shards", "Battle Mage", nullptr },
  { 2293760u,   0x0023, 0x0000, "Ring of Ice", "Battle Mage", nullptr },
  { 2359296u,   0x0024, 0x0000, "Transformation", "Wood Elf", nullptr },
  { 2424832u,   0x0025, 0x0000, "Quick as a Flash", "Wood Elf", nullptr },
  { 2490368u,   0x0026, 0x0000, "Thorn Bush", "Wood Elf", nullptr },
  { 2555904u,   0x0027, 0x0000, "Poisoned Tendrils", "Wood Elf", nullptr },
  { 2621440u,   0x0028, 0x0000, "Plant Cage", "Wood Elf", nullptr },
  { 2686976u,   0x0029, 0x0000, "Call of the Ancestors", "Wood Elf", nullptr },
  { 2752512u,   0x002A, 0x0000, "Recuperation", "Wood Elf", nullptr },
  { 2818048u,   0x002B, 0x0000, "Reanimate", "NPC Magicians", nullptr },
  { 2883584u,   0x002C, 0x0000, "ARACATTACK", "?", nullptr },
  { 2949120u,   0x002D, 0x0000, "EXPLOSION", "?", nullptr },
  { 3014656u,   0x002E, 0x0000, "BATTLEFOG", "?", nullptr },
  { 3080192u,   0x002F, 0x0000, "SOULCATCHER", "?", nullptr },
  { 3145728u,   0x0030, 0x0000, "CONFUSION", "?", nullptr },
  { 3211264u,   0x0031, 0x0000, "Fire Bolt From Above", "Sakkara Demon", nullptr },
  { 3276800u,   0x0032, 0x0000, "Fiery Foot Step", "Sakkara Demon", nullptr },
  { 3342336u,   0x0033, 0x0000, "Poison Ring", "?", nullptr },
  { 3407872u,   0x0034, 0x0000, "Shaddar's Fireball", "Shaddar", nullptr },
  { 3473408u,   0x0035, 0x0000, "Summon Goblin / Healing / Ground Shock", "Goblin Shaman", nullptr },
  { 3604480u,   0x0037, 0x0000, "Greed", "Dwarf", nullptr },
  { 3670016u,   0x0038, 0x0000, "Landmine", "Dwarf", nullptr },
  { 3735552u,   0x0039, 0x0000, "Entrench", "Dwarf", nullptr },
  { 3801088u,   0x003A, 0x0000, "Dwarven Armor", "Dwarf", nullptr },
  { 3866624u,   0x003B, 0x0000, "Dwarven Steel", "Dwarf", nullptr },
  { 3932160u,   0x003C, 0x0000, "Blazing Disc", "Daemon", nullptr },
  { 3997696u,   0x003D, 0x0000, "Paralyze?", "?", nullptr },
  { 4063232u,   0x003E, 0x0000, "Call of Death", "Daemon", nullptr },
  { 4128768u,   0x003F, 0x0000, "Infernal Power", "Daemon", nullptr },
  { 4194304u,   0x0040, 0x0000, "Hell Sphere", "Daemon", nullptr },
  { 4259840u,   0x0041, 0x0000, "Tentacles", "Daemon", nullptr },
  { 4325376u,   0x0042, 0x0000, "Abysmal Choir", "Daemon", nullptr },
  { 4390912u,   0x0043, 0x0000, "Dread", "Daemon", nullptr },
  { 4521984u,   0x0045, 0x0000, "Spikes from the Ground", "Subkari", nullptr },
  { 4587520u,   0x0046, 0x0000, "Fireball 2", "?", nullptr },
  { 4653056u,   0x0047, 0x0000, "Spikes from the Ground", "Subkari", nullptr },
  { 4718592u,   0x0048, 0x0000, "Bolting Terror Magic", "Bolting Terror", nullptr },
  { 65732609u,  0x03EB, 0x0001, "Stomping Jump", "Gladiator", nullptr },
  { 65798145u,  0x03EC, 0x0001, "Explosive Arrow", "Wood Elf", nullptr },
  { 65863681u,  0x03ED, 0x0001, "Multiple Shot", "Wood Elf", nullptr },
  { 65929217u,  0x03EE, 0x0001, "Spider Arrow", "Wood Elf", nullptr },
  { 65994753u,  0x03EF, 0x0001, "Knockback Arrow", "Wood Elf", nullptr },
  { 66060289u,  0x03F0, 0x0001, "Penetrating Arrow", "Wood Elf", nullptr },
  { 66191361u,  0x03F2, 0x0001, "Back-breaker", "Gladiator", nullptr },
  { 66322433u,  0x03F4, 0x0001, "Fist of the Gods", "Gladiator", nullptr },
  { 66387969u,  0x03F5, 0x0001, "Throwing Blades", "Gladiator", nullptr },
  { 66453505u,  0x03F6, 0x0001, "Awe", "Gladiator", nullptr },
  { 66519041u,  0x03F7, 0x0001, "Heroic Courage", "Gladiator", nullptr },
  { 66584577u,  0x03F8, 0x0001, "Dagger Stare", "Gladiator", nullptr },
  { 66650113u,  0x03F9, 0x0001, "Combat Jump", "Seraphim", nullptr },
  { 66715649u,  0x03FA, 0x0001, "BeeEffGee", "Seraphim", nullptr },
  { 66781185u,  0x03FB, 0x0001, "Energy Bolts", "Seraphim", nullptr },
  { 66846721u,  0x03FC, 0x0001, "Whirling Hit", "Seraphim", nullptr },
  { 66912257u,  0x03FD, 0x0001, "Flip-flop", "?", nullptr },
  { 66977793u,  0x03FE, 0x0001, "Turn into Vampire", "Vampiress", nullptr },
  { 67043329u,  0x03FF, 0x0001, "Turn into Knight", "Vampiress", nullptr },
  { 67108865u,  0x0400, 0x0001, "Mind Control", "Vampiress", nullptr },
  { 67174401u,  0x0401, 0x0001, "Mind Control (Vampire)", "Vampiress", nullptr },
  { 67239937u,  0x0402, 0x0001, "Wolf Call", "Vampiress", nullptr },
  { 67305473u,  0x0403, 0x0001, "Wolf Call (Vampire)", "Vampiress", nullptr },
  { 67371009u,  0x0404, 0x0001, "Time Control", "Vampiress", nullptr },
  { 67436545u,  0x0405, 0x0001, "Time Control (Vampire)", "Vampiress", nullptr },
  { 67502081u,  0x0406, 0x0001, "Blood Bite", "Vampiress", nullptr },
  { 67567617u,  0x0407, 0x0001, "Master Bite", "Vampiress", nullptr },
  { 67633153u,  0x0408, 0x0001, "Claw Jump", "Vampiress", nullptr },
  { 67698689u,  0x0409, 0x0001, "Blood Kiss", "Vampiress", nullptr },
  { 67764225u,  0x040A, 0x0001, "Awaken Dead", "Vampiress", nullptr },
  { 67829761u,  0x040B, 0x0001, "Bats: Blood Swarm", "Vampiress", nullptr },
  { 67895297u,  0x040C, 0x0001, "Bats: Confusion", "Vampiress", nullptr },
  { 67960833u,  0x040D, 0x0001, "Bats: Guard", "Vampiress", nullptr },
  { 68026369u,  0x040E, 0x0001, "Multi-hit (Vampire)", "Vampiress", nullptr },
  { 68091905u,  0x040F, 0x0001, "Hard Hit (Vampire)", "Vampiress", nullptr },
  { 68157441u,  0x0410, 0x0001, "Attack (Vampire)", "Vampiress", nullptr },
  { 68222977u,  0x0411, 0x0001, "Combat Kick (Vampire)", "Vampiress", nullptr },
  { 68288513u,  0x0412, 0x0001, "Cobra", "Dark Elf", nullptr },
  { 68354049u,  0x0413, 0x0001, "Mongoose", "Dark Elf", nullptr },
  { 68419585u,  0x0414, 0x0001, "Soul Catcher", "Dark Elf", nullptr },
  { 68485121u,  0x0415, 0x0001, "Pak-Dain (Projectile Deflection)", "Dark Elf", nullptr },
  { 68550657u,  0x0416, 0x0001, "Pak-Nakor (Magic Deflection)", "Dark Elf", nullptr },
  { 68616193u,  0x0417, 0x0001, "Testosterone", "Dark Elf", nullptr },
  { 68681729u,  0x0418, 0x0001, "Adrenaline", "Dark Elf", nullptr },
  { 68747265u,  0x0419, 0x0001, "Explosive Charge", "Dark Elf", nullptr },
  { 68812801u,  0x041A, 0x0001, "Bottomless Pit", "Dark Elf", nullptr },
  { 68878337u,  0x041B, 0x0001, "Confusion", "Dark Elf", nullptr },
  { 68943873u,  0x041C, 0x0001, "Poison Mist", "Dark Elf", nullptr },
  { 69009409u,  0x041D, 0x0001, "Battle Fog", "Dark Elf", nullptr },
  { 69074945u,  0x041E, 0x0001, "Hunter-seeker", "Seraphim", nullptr },
  { 69271553u,  0x0421, 0x0001, "Multi-hit", "Gladiator", nullptr },
  { 69337089u,  0x0422, 0x0001, "Hard Hit", "Gladiator", nullptr },
  { 69402625u,  0x0423, 0x0001, "Attack", "Gladiator", nullptr },
  { 69468161u,  0x0424, 0x0001, "Combat Kick", "Gladiator", nullptr },
  { 69533697u,  0x0425, 0x0001, "Multi-hit", "Wood Elf", nullptr },
  { 69599233u,  0x0426, 0x0001, "Hard Hit", "Wood Elf", nullptr },
  { 69664769u,  0x0427, 0x0001, "Eye for an Eye", "Wood Elf", nullptr },
  { 69730305u,  0x0428, 0x0001, "Combat Kick", "?", nullptr },
  { 69795841u,  0x0429, 0x0001, "Sudden Fury (Multi-hit)", "Dark Elf", nullptr },
  { 69861377u,  0x042A, 0x0001, "Revenge (Hard Hit)", "Dark Elf", nullptr },
  { 69926913u,  0x042B, 0x0001, "Rage (Attack)", "Dark Elf", nullptr },
  { 69992449u,  0x042C, 0x0001, "Combat Kick", "Dark Elf", nullptr },
  { 70057985u,  0x042D, 0x0001, "Multi-hit", "Seraphim", nullptr },
  { 70123521u,  0x042E, 0x0001, "Hard Hit", "Seraphim", nullptr },
  { 70189057u,  0x042F, 0x0001, "Attack", "Seraphim", nullptr },
  { 70254593u,  0x0430, 0x0001, "Combat Kick", "Seraphim", nullptr },
  { 70320129u,  0x0431, 0x0001, "Whirling Claws (Multi-hit)", "Vampiress", nullptr },
  { 70385665u,  0x0432, 0x0001, "Claws of Death (Hard Hit)", "Vampiress", nullptr },
  { 70451201u,  0x0433, 0x0001, "Ripping Claws (Attack)", "Vampiress", nullptr },
  { 70516737u,  0x0434, 0x0001, "Combat Kick", "Vampiress", nullptr },
  { 70647809u,  0x0436, 0x0001, "Wrath", "Dwarf", nullptr },
  { 70713345u,  0x0437, 0x0001, "Heavy Blow", "Dwarf", nullptr },
  { 70778881u,  0x0438, 0x0001, "Assault", "Dwarf", nullptr },
  { 70844417u,  0x0439, 0x0001, "Battle Fog", "?", nullptr },
  { 70909953u,  0x043A, 0x0001, "Battle Rage", "Dwarf", nullptr },
  { 70975489u,  0x043B, 0x0001, "Recoil", "Dwarf", nullptr },
  { 71041025u,  0x043C, 0x0001, "Vehemence", "Dwarf", nullptr },
  { 71106561u,  0x043D, 0x0001, "Flame Thrower", "Dwarf", nullptr },
  { 71172097u,  0x043E, 0x0001, "Mortar Grenade", "Dwarf", nullptr },
  { 71237633u,  0x043F, 0x0001, "Cannon Blast", "Dwarf", nullptr },
  { 71368705u,  0x0441, 0x0001, "War Cry", "Dwarf", nullptr },
  { 71696385u,  0x0446, 0x0001, "Soaring Daemon (Descent)", "Daemon", nullptr },
  { 71892993u,  0x0449, 0x0001, "Battle Daemon (Assail)", "Daemon", nullptr },
  { 71958529u,  0x044A, 0x0001, "Fire Daemon (Wall of Fire)", "Daemon", nullptr },
  { 72024065u,  0x044B, 0x0001, "Poison Daemon (Poison Ring)", "Daemon", nullptr },
  { 72089601u,  0x044C, 0x0001, "Energy Daemon (Charged Bolts)", "Daemon", nullptr },
};

constexpr std::size_t kCombatArtCount =
    sizeof(kCombatArts) / sizeof(kCombatArts[0]);

// Cross-check: row count must match the verified Lua source (156 rows).
static_assert(kCombatArtCount == 156,
              "combat_arts.h: expected 156 rows (see data_tables.md §2)");

// Lookup by the packed 32-bit id. Returns nullptr if absent.
inline const CombatArt* find_by_packed(std::uint32_t packed) {
  for (std::size_t i = 0; i < kCombatArtCount; ++i) {
    if (kCombatArts[i].packed == packed) return &kCombatArts[i];
  }
  return nullptr;
}

// Convenience: lookup by the (hi, lo) word pair.
inline const CombatArt* find_by_words(std::uint16_t hi, std::uint16_t lo) {
  return find_by_packed(pack(hi, lo));
}

}  // namespace data
}  // namespace sacred

#endif  // SACRED_SDK_PORTS_DATA_COMBAT_ARTS_H
