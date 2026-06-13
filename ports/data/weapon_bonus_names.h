// SacredSDK / ports / data / weapon_bonus_names.h
//
// Weapon / item bonus id -> name table for Sacred Gold, as a compile-time
// constexpr table. FUTURE-USE: written but NOT yet wired into SacredSDK.vcxproj
// or any existing TU.
//
// SOURCE (authoritative): the community "Weapon.pak Editor" (VB6) v0.2.3.0,
// MainModule.bas: InitBonusNames() -- BonusName(id)=string. Extracted from
//   refs/Weapon.pak_Editor.zip
//     -> "Weapon.pak Editor/Version 0.2.3.0/MainModule.bas"
// Documented in sdk/.claude/knowledge/refs_formats_data.md (B3 / P7) and
// refs_tools.md (the Spells.h 236-entry overlap). This v0.2.3.0 source defines
// exactly 300 BonusName(id) entries (the refs note's "~520" was an upper
// estimate; the actual table here is 300).
//
// LANGUAGE / FIDELITY (IMPORTANT):
//   The names are the ORIGINAL GERMAN strings from MainModule.bas, transcribed
//   VERBATIM (UTF-8). They are NOT translated to English, to avoid introducing
//   guessed translations (per the "correct over exhaustive" rule). Ids 1..73
//   correspond to the same spells/arts as combat_arts.h / hero_tables.h, so an
//   English label can be cross-referenced there by opcode. A proper EN column
//   would translate via the game's speditor.ini locale map (refs_formats_data.md
//   P7) -- left as a TODO below.
//   TODO(verify): translate German -> English via speditor.ini when an EN UI
//                 label table is needed; cross-check ids 1..73 against
//                 combat_arts.h hi-word opcodes.
//
// ID ENCODING:
//   The bonus id is the engine-wide packed value (modifier<<16)|opcode -- the
//   same packing used by combat_arts.h. The low-id bands (1.., 601.., 801..,
//   1000..) have hi=0 (plain opcodes). The high "scaled-stat" bands pack a
//   modifier 1..6 in the high word:
//     0x0001xxxx (66337..)  Staerke      / strength scaling
//     0x0002xxxx (131873..) Geschick     / dexterity scaling
//     0x0003xxxx (197409..) Wiederstand  / resistance scaling
//     0x0004xxxx (262945..) Phys. Regen  scaling
//     0x0005xxxx (328481..) Mental Regen scaling
//     0x0006xxxx (394017..) Charisma     scaling
//   Use packed_hi()/packed_lo() to split. This is pure data (no absolute engine
//   VA), so no Sacred_decrypted.exe byte-signature check is required.
//
// TODO(port): expose to a future engine::data namespace (engine::data::bonus).
// TODO(port): wire when an item-bonus inspector / Weapon.pak tooling needs
//             id->name (and once an EN translation column exists).

#ifndef SACRED_SDK_PORTS_DATA_WEAPON_BONUS_NAMES_H
#define SACRED_SDK_PORTS_DATA_WEAPON_BONUS_NAMES_H

#include <cstdint>
#include <cstddef>

namespace sacred {
namespace data {

// One bonus-id -> name entry. `name_de` is the original German label (verbatim,
// UTF-8). No English column yet (see TODO(verify) in the file header).
struct WeaponBonusName {
  std::uint32_t id;
  const char*   name_de;
};

// High/low word of a packed bonus id (matches combat_arts.h split()).
constexpr std::uint16_t packed_hi(std::uint32_t id) {
  return static_cast<std::uint16_t>((id >> 16) & 0xFFFFu);
}
constexpr std::uint16_t packed_lo(std::uint32_t id) {
  return static_cast<std::uint16_t>(id & 0xFFFFu);
}

// 300 entries, transcribed from InitBonusNames() (id-ascending).
constexpr WeaponBonusName kWeaponBonusNames[] = {
  { 1u, "Phasenverschiebung +" },
  { 2u, "Steinhaut +" },
  { 3u, "Gefährtin des Waldes +" },
  { 4u, "Wasserform +" },
  { 5u, "Feuerball +" },
  { 6u, "Pentagram +" },
  { 7u, "Spirituelle Heilung +" },
  { 8u, "Kettenblitz +" },
  { 9u, "Power up +" },
  { 10u, "Verwandlung +" },
  { 11u, "Feuerspirale +" },
  { 12u, "Ground Poisen +" },
  { 13u, "Blitzschlag +" },
  { 14u, "Breeze +" },
  { 15u, "Celestial Light +" },
  { 16u, "Bannkreis der Angst +" },
  { 17u, "Rotierende Lichtklingen +" },
  { 18u, "Irritation +" },
  { 19u, "Bekehrung +" },
  { 20u, "Licht +" },
  { 21u, "Glaubensstärke +" },
  { 22u, "Lichtschild +" },
  { 23u, "Himmelsleuchten +" },
  { 24u, "Flammenhaut +" },
  { 25u, "Petrefication +" },
  { 26u, "Meteoritenhagel +" },
  { 27u, "Katarakt der Wendigkeit +" },
  { 28u, "Wirbelwind +" },
  { 29u, "Windböe +" },
  { 30u, "Schildwall +" },
  { 31u, "Aue des Geistes +" },
  { 32u, "Reiki +" },
  { 33u, "Blitzschlag +" },
  { 34u, "Eissplitter +" },
  { 35u, "Frostring +" },
  { 36u, "Transformation +" },
  { 37u, "Wieselflink +" },
  { 38u, "Dörnenbusch +" },
  { 39u, "Giftranken +" },
  { 40u, "Pflanzenkäfig +" },
  { 41u, "Ruf der Ahnen +" },
  { 42u, "Genesung +" },
  { 43u, "Gefährtin des Waldes +" },
  { 44u, "Aura of Defense +" },
  { 45u, "Aura of Resistance +" },
  { 46u, "Golem +" },
  { 47u, "Aura of Attack +" },
  { 48u, "Aura of Healing +" },
  { 49u, "Höllendiskus +" },
  { 50u, "Feuerwand +" },
  { 51u, "Giftwolke +" },
  { 52u, "Dimension Shift +" },
  { 53u, "Dimension Shift +" },
  { 54u, "Gefährtin des Waldes +" },
  { 55u, "Golem +" },
  { 56u, "Aura of Healing +" },
  { 57u, "Aura of Resistance +" },
  { 58u, "Aura of Attack +" },
  { 59u, "Aura of Defense +" },
  { 60u, "Höllendiskus +" },
  { 61u, "Feuerwand +" },
  { 62u, "Giftwolke +" },
  { 63u, "Dimension Shift +" },
  { 64u, "Ruf des Todes +" },
  { 65u, "Aura of Fighting +" },
  { 66u, "Aura of the Soul +" },
  { 67u, "Tentakel +" },
  { 68u, "Chor der Hölle +" },
  { 69u, "Hell Scream +" },
  { 70u, "Feuerdämon +" },
  { 71u, "Giftdämon +" },
  { 72u, "Energiedämon +" },
  { 73u, "Himmelsmagie +" },
  { 601u, "Waffenkunde +" },
  { 602u, "Stangenwaffen +" },
  { 603u, "Schwertkunde +" },
  { 604u, "Axtkampf +" },
  { 605u, "Zweiwaffenkampf +" },
  { 606u, "Fernkampf +" },
  { 607u, "Wendigkeit +" },
  { 608u, "Parade +" },
  { 609u, "Konstitution +" },
  { 610u, "Rüstung +" },
  { 611u, "Meditation +" },
  { 612u, "Schwertkunde +" },
  { 613u, "Magiekunde +" },
  { 614u, "Feuermagie +" },
  { 615u, "Wassermagie +" },
  { 616u, "Erdmagie +" },
  { 617u, "Luftmagie +" },
  { 618u, "Mondmagie +" },
  { 619u, "Vampirismus +" },
  { 620u, "Handel +" },
  { 621u, "Reiten +" },
  { 622u, "Entwaffnung +" },
  { 623u, "Faustkampf +" },
  { 624u, "Konzentration +" },
  { 625u, "Ballistik +" },
  { 626u, "Fallenkunst +" },
  { 627u, "Blutdurst +" },
  { 801u, "Physisch +%" },
  { 802u, "Feuer +%" },
  { 803u, "Magisch +%" },
  { 804u, "Gift +%" },
  { 805u, "Physisch +%" },
  { 806u, "Feuer +%" },
  { 807u, "Magisch +%" },
  { 808u, "Gift +%" },
  { 809u, "Angriff +%" },
  { 810u, "Verteidigung +%" },
  { 811u, "Angriffstempo +" },
  { 812u, "Geschwindigkeit +" },
  { 813u, "Regeneration Zauber +%" },
  { 814u, "Regeneration Special Move +%" },
  { 815u, "Stärke +" },
  { 816u, "Geschick +" },
  { 817u, "Wiederstand +" },
  { 818u, "Physische Regeneration +" },
  { 819u, "Mentale Regeneration +" },
  { 820u, "Charisma +" },
  { 840u, "Besondere Gegenstände finden +" },
  { 841u, "Leben absaugen +%" },
  { 842u, "Eigenes Leben verlieren +%" },
  { 843u, "Erfahrung von Gegnern +%" },
  { 844u, "Auf alle Magiesprüche +" },
  { 845u, "Auf alle Special Moves +" },
  { 846u, "Zerteilen +%" },
  { 847u, "Alle Resistenzen +%" },
  { 848u, "Angriff und Verteidigung +%" },
  { 849u, "Auf alle Fertigkeiten +" },
  { 850u, "Verlängerte Nacht +%" },
  { 851u, "Verlängerter Tag +%" },
  { 852u, "Gegner sterben bei Blickkontakt +%" },
  { 853u, "Minimap aufdecken +" },
  { 854u, "Regeneration Specialmoves" },
  { 855u, "Chance Fluggegner zu binden +%" },
  { 856u, "Chance, bei Treffer Gold von Gegner zu erhalten +%" },
  { 857u, "Chance auf offene Wunden +%" },
  { 858u, "Wiederstand gegen Zauber +" },
  { 859u, "Chance einen kritischen Treffer zu landen +%" },
  { 861u, "Widerstand gegen Betäubung: +" },
  { 862u, "Gegner schocken +" },
  { 863u, "Untote entgültig bannen +" },
  { 864u, "Ermöglicht das Angreifen von Tieren" },
  { 865u, "Erweiterter Lichradius +" },
  { 866u, "Schaden von Goldvorat abziehen +%" },
  { 867u, "Verwundung erhöht ausgeteilten Schaden +%" },
  { 1000u, "Rundumschlag +" },
  { 1001u, "Harter Schlag +" },
  { 1002u, "Attacke +" },
  { 1003u, "Stampfsprung +" },
  { 1004u, "Explosionspfeil +" },
  { 1005u, "Mehrfachschuss +" },
  { 1006u, "Spinnenpfeil +" },
  { 1007u, "Keulenpfeil +" },
  { 1008u, "Klingenpfeil +" },
  { 1009u, "Kampftritt +" },
  { 1010u, "Rückenbrecher +" },
  { 1012u, "Fausthieb der Götter +" },
  { 1013u, "Schleuderklingen +" },
  { 1014u, "Ehrfurcht +" },
  { 1015u, "Sturm und Drang +" },
  { 1016u, "Blicke wie Klingen +" },
  { 1017u, "Kampfsprung +" },
  { 1018u, "BeeEffGee +" },
  { 1019u, "Energieblitze +" },
  { 1020u, "Wirbelschlag +" },
  { 1021u, "Flip Flop" },
  { 1022u, "Vampirform +" },
  { 1023u, "Ritterform +" },
  { 1024u, "Beherrschung +" },
  { 1025u, "Beherrschung +" },
  { 1026u, "Wolfsruf +" },
  { 1027u, "Wolfsruf +" },
  { 1028u, "Zeitbeherrschung +" },
  { 1029u, "Zeitbeherrschung +" },
  { 1030u, "Blutbiss +" },
  { 1031u, "Meisterbiss +" },
  { 1032u, "Todesklauen +" },
  { 1033u, "Blutkuss +" },
  { 1034u, "Erweckung +" },
  { 1035u, "Fledermäuse: Blutschwarm +" },
  { 1036u, "Fledermäuse: Verwirrung +" },
  { 1037u, "Fledermäuse: Wächter +" },
  { 1038u, "Rundumschlag +" },
  { 1039u, "Harter Schlag +" },
  { 1040u, "Attacke +" },
  { 1041u, "Kampftritt +" },
  { 1042u, "Kobra +" },
  { 1043u, "Mungo +" },
  { 1044u, "Seelenräuber +" },
  { 1045u, "Pak'Dain (Projektilumleitung) +" },
  { 1046u, "Pak'Nakor (Magieumleitung) +" },
  { 1047u, "Testosteron +" },
  { 1048u, "Adrenalin +" },
  { 1049u, "Sprengsatz +" },
  { 1050u, "Höllenschlund +" },
  { 1051u, "Verwirrung +" },
  { 1052u, "Gifthauch +" },
  { 1053u, "Schlachtennebel +" },
  { 1054u, "Jägersucher +" },
  { 1057u, "Rundumschlag +" },
  { 1058u, "Harter Schlag +" },
  { 1059u, "Attacke +" },
  { 1060u, "Kampftritt +" },
  { 1061u, "Rundumschlag +" },
  { 1062u, "Harter Schlag +" },
  { 1063u, "Auge um Auge (Attacke) +" },
  { 1064u, "Kampftritt +" },
  { 1065u, "Gefährtin des Waldes +" },
  { 1066u, "Revange +" },
  { 1067u, "Rage +" },
  { 1069u, "Rundumschlag +" },
  { 1070u, "Harter Schlag +" },
  { 1071u, "Attacke +" },
  { 1072u, "Kampftritt +" },
  { 1073u, "Wirbelklauen +" },
  { 1074u, "Todesklauen +" },
  { 1075u, "Todesklauen +" },
  { 1076u, "Kampftritt +" },
  { 1078u, "Schlachtennebel +" },
  { 1079u, "Schlachtennebel +" },
  { 1080u, "Schlachtennebel +" },
  { 1081u, "Schlachtennebel +" },
  { 1082u, "Rundumschlag +" },
  { 1083u, "Attacke +" },
  { 1084u, "Mörsergranate +" },
  { 1085u, "Flammenwerfer +" },
  { 1086u, "Kanonenschuss +" },
  { 1087u, "Tellerminen +" },
  { 1088u, "Eingraben +" },
  { 1089u, "Rundumschlag +" },
  { 1090u, "Harter Schlag +" },
  { 1091u, "Attacke +" },
  { 1092u, "Attacke +" },
  { 1093u, "Flugdämon +" },
  { 1094u, "Sturzflug +" },
  { 1095u, "Sturzflug +" },
  { 1096u, "Kampfdämon +" },
  { 1097u, "Kampfdämon (Attacke) +" },
  { 66337u, "Physisch Stärke +%" },
  { 66338u, "Feuer Stärke +" },
  { 66339u, "Magisch Stärke +" },
  { 66340u, "Gift Stärke +" },
  { 66341u, "Physisch Stärke +" },
  { 66342u, "Feuer Stärke +" },
  { 66343u, "Magisch Stärke +" },
  { 66344u, "Gift Stärke +" },
  { 66345u, "Angriff Stärke +" },
  { 66346u, "Verteidigung Stärke +" },
  { 131873u, "Physisch Geschick +" },
  { 131874u, "Feuer Geschick +" },
  { 131875u, "Magisch Geschick +" },
  { 131876u, "Gift Geschick +" },
  { 131877u, "Physisch Geschick +" },
  { 131878u, "Feuer Geschick +" },
  { 131879u, "Magisch Geschick +" },
  { 131880u, "Gift Geschick +" },
  { 131881u, "Geschick auf Angriff +" },
  { 131882u, "Verteidigung Geschick +" },
  { 197409u, "Physisch Wiederstand +" },
  { 197410u, "Feuer Wiederstand +%" },
  { 197411u, "Magisch Wiederstand +" },
  { 197412u, "Gift Wiederstand +" },
  { 197413u, "Physisch Wiederstand +" },
  { 197414u, "Feuer Wiederstand +" },
  { 197415u, "Magisch Wiederstand +" },
  { 197416u, "Gift Wiederstand +" },
  { 197417u, "Wiederstand +" },
  { 197418u, "Verteidigung Wiederstand +" },
  { 262945u, "Physisch Physische Regeneration +" },
  { 262946u, "Feuer Physische Regeneration +" },
  { 262947u, "Magisch Physische Regeneration +" },
  { 262948u, "Gift Physische Regeneration +" },
  { 262949u, "Physisch Physische Regeneration +" },
  { 262950u, "Feuer Physische Regeneration +" },
  { 262951u, "Magisch Physische Regeneration +" },
  { 262952u, "Gift Physische Regeneration +" },
  { 262953u, "Angriff Physische Regeneration +" },
  { 262954u, "Verteidigung Physische Regeneration +" },
  { 328481u, "Physisch Mentale Regeneration +" },
  { 328482u, "Feuer Mentale Regeneration +" },
  { 328483u, "Magisch Mentale Regeneration +" },
  { 328484u, "Gift Mentale Regeneration +" },
  { 328485u, "Physisch Mentale Regeneration +" },
  { 328486u, "Feuer Mentale Regeneration +" },
  { 328487u, "Magisch Mentale Regeneration +" },
  { 328488u, "Gift Mentale Regeneration +" },
  { 328489u, "Angriff Mentale Regeneration +" },
  { 328490u, "Verteidigung Mentale Regeneration +" },
  { 394017u, "Physisch Charisma +" },
  { 394018u, "Feuer Charisma +" },
  { 394019u, "Magisch Charisma +" },
  { 394020u, "Gift Charisma +" },
  { 394021u, "Physisch Charisma +" },
  { 394022u, "Feuer Charisma +" },
  { 394023u, "Magisch Charisma +" },
  { 394024u, "Gift Charisma +" },
  { 394025u, "Angriff Charisma +" },
  { 394026u, "Verteidigung Charisma +" },
};

constexpr std::size_t kWeaponBonusNameCount =
    sizeof(kWeaponBonusNames) / sizeof(kWeaponBonusNames[0]);

// Cross-check: row count must match the InitBonusNames() source (300 entries
// in Weapon.pak Editor v0.2.3.0).
static_assert(kWeaponBonusNameCount == 300,
              "weapon_bonus_names.h: expected 300 rows (Weapon.pak Editor v0.2.3.0)");

// Linear lookup by packed bonus id. Returns nullptr if absent. The table is
// id-ascending but sparse (gaps within bands), so a linear scan is used to keep
// this header dependency-free. Returns the German name via the entry.
inline const WeaponBonusName* find_by_id(std::uint32_t id) {
  for (std::size_t i = 0; i < kWeaponBonusNameCount; ++i) {
    if (kWeaponBonusNames[i].id == id) return &kWeaponBonusNames[i];
  }
  return nullptr;
}

// Convenience: the German name for a bonus id, or nullptr if absent.
inline const char* name_de_of(std::uint32_t id) {
  const WeaponBonusName* e = find_by_id(id);
  return e ? e->name_de : nullptr;
}

}  // namespace data
}  // namespace sacred

#endif  // SACRED_SDK_PORTS_DATA_WEAPON_BONUS_NAMES_H
