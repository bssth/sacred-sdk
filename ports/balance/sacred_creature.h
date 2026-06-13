// sacred_creature.h — Creature.pak "SacredCreature" record layout + Class/Skill/
// Flag enums, with a non-owning reader over a loaded Creature.pak buffer.
//
// SOURCE:
//   sdk/.claude/knowledge/refs_formats_data.md §B2 ("Creature.pak (Balance
//     creature table) — Creature.pak Editor (VB6)"). The layout was recovered
//     from the VB6 editor's `Type SacredCreature` (round-trips the real file):
//       header: char[3] "CIF" magic, Version (==0), u32 CreaturesCnt, byte[4] Unknown
//       record array starts at file offset 0x100 (256); 86 bytes/record:
//         Long   ID            (4)   <- matches characters.csv decimal IDs
//         Integer Class        (2)
//         Byte   Flags         (1)
//         Byte   Unknown1      (1)
//         Integer ExpA, ExpB   (4)   <- experience reward
//         Byte   BaseStrength, BaseEndurance, BaseDexterity,
//                BasePhysReg, BaseMagReg, BaseCharisma (6)
//         Byte   Unknown2[2]   (2)
//         Byte   Skills[18]    (18)  <- skill ids (1..33)
//         Integer BaseWalkSpeed, BaseRunSpeed (4)
//         {Byte BonusLevel; Byte BonusType}[6]  Boni (12)
//         Byte   BoniValue[6]  (6)
//         Byte   Unknown3[26]  (26)
//       => 4+2+1+1+4+6+2+18+4+12+6+26 = 86 bytes. (verified in §B2.)
//       trailing ReservedSpace = FileLen - (Cnt*86 + 256), preserved on save.
//   Class enum (ClXxx) and Flags bits and the Skill 1..33 space: §B2 +
//     re/data_tables.md §4 (`Skills` 0..33 English names).
//
// NOTE: Creature.pak is an ON-DISK data file. No engine VA -> nothing to byte-sig
//   against Sacred_decrypted.exe here; the validation surface is the file (the
//   "CIF" magic + the round-tripping editor source).
//
// TODO(port): wire when the SDK reads creature base stats/skills/exp by id at
//   runtime (balance mods / diagnostics overlay). FUTURE-USE, standalone: do NOT
//   add to SacredSDK.vcxproj or #include from existing TUs.
//
// Caller owns the loaded Creature.pak buffer; this header only decodes fixed-
// offset little-endian fields out of it. No engine/mem.h dependency.
//
// C++14, MSVC.

#ifndef SACRED_SDK_PORTS_SACRED_CREATURE_H
#define SACRED_SDK_PORTS_SACRED_CREATURE_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>

namespace sacred {
namespace creature {

// --- enums (verbatim from refs_formats_data.md §B2) -------------------------

// Creature Class (VB `ClXxx`). 16-bit field in the record.
enum class Class : std::int16_t {
    Hero      = 1,
    Monster   = 2,
    NPC       = 3,
    Horse     = 4,
    Undead    = 5,
    Animal    = 6,
    Mercenary = 7,
    Goblin    = 8,
    Demon     = 9,
    Dragon    = 10,
    Energy    = 11,
    Elve      = 12,
    Enemy     = 13,
    Human     = 14,
    Dryade    = 15,
};

// Flags byte (bitfield). VB names kept verbatim.
enum CreatureFlag : std::uint8_t {
    FlFly      = 0x01,
    FlBig      = 0x02,
    // 0x04 / 0x08 unknown / unused per §B2
    FlNoShadow = 0x10,
    FlGhost    = 0x20,
    FlBanane   = 0x40,
    FlKurve    = 0x80,
};

// Skill ids occupy 1..33 (data_tables.md §4 `Skills` 0..33; 0 = none/invalid).
// The Skills[18] array stores up to 18 skill ids per creature. Names live in
// data_tables.md's `hero_tables.lua > Skills`; only the id space is needed here.
// (No exhaustive enum transcribed — the 18-byte array carries raw ids.)
enum : std::uint8_t {
    kSkillNone = 0,
    kSkillIdMin = 1,
    kSkillIdMax = 33,
    kSkillCount = 18,   // Skills[18]
    kBoniCount  = 6,    // Boni[6] + BoniValue[6]
};

// --- file/record geometry ---------------------------------------------------
struct CreaturePakLayout {
    // Header.
    static constexpr std::size_t kMagicOffset   = 0x00; // char[3] "CIF"
    static constexpr char        kMagic[4]      = {'C','I','F','\0'};
    // VB reads Signatur(3) then Version then Cnt(u32) then Unknown[4]. The
    // record array is Seek'd to byte offset 256 (1-based VB seek 257).
    static constexpr std::size_t kRecordsOffset = 0x100; // 256
    static constexpr std::size_t kRecordSize    = 86;

    // Field offsets *within* a record (little-endian).
    static constexpr std::size_t kID            = 0;   // i32
    static constexpr std::size_t kClass         = 4;   // i16
    static constexpr std::size_t kFlags         = 6;   // u8
    static constexpr std::size_t kUnknown1      = 7;   // u8
    static constexpr std::size_t kExpA          = 8;   // i16
    static constexpr std::size_t kExpB          = 10;  // i16
    static constexpr std::size_t kBaseStrength  = 12;  // u8
    static constexpr std::size_t kBaseEndurance = 13;  // u8
    static constexpr std::size_t kBaseDexterity = 14;  // u8
    static constexpr std::size_t kBasePhysReg   = 15;  // u8
    static constexpr std::size_t kBaseMagReg    = 16;  // u8
    static constexpr std::size_t kBaseCharisma  = 17;  // u8
    static constexpr std::size_t kUnknown2      = 18;  // u8[2]
    static constexpr std::size_t kSkills        = 20;  // u8[18]
    static constexpr std::size_t kBaseWalkSpeed = 38;  // i16
    static constexpr std::size_t kBaseRunSpeed  = 40;  // i16
    static constexpr std::size_t kBoni          = 42;  // {u8 level; u8 type}[6] = 12 bytes
    static constexpr std::size_t kBoniValue     = 54;  // u8[6]
    static constexpr std::size_t kUnknown3      = 60;  // u8[26]  -> ends at 86
};

// One decoded creature bonus pair.
struct CreatureBonus {
    std::uint8_t level = 0;
    std::uint8_t type  = 0;
    std::uint8_t value = 0; // from the parallel BoniValue[6] array
};

// Fully decoded SacredCreature record.
struct SacredCreature {
    std::int32_t  id = 0;            // engine creature type id (decimal; matches characters.csv)
    Class         clazz = Class::Monster;
    std::uint8_t  flags = 0;         // CreatureFlag bits
    std::int16_t  expA = 0, expB = 0;
    std::uint8_t  baseStrength = 0, baseEndurance = 0, baseDexterity = 0;
    std::uint8_t  basePhysReg = 0, baseMagReg = 0, baseCharisma = 0;
    std::array<std::uint8_t, kSkillCount> skills{}; // skill ids (0 = none)
    std::int16_t  baseWalkSpeed = 0, baseRunSpeed = 0;
    std::array<CreatureBonus, kBoniCount> boni{};
    bool          valid = false;

    bool has_flag(CreatureFlag f) const { return (flags & f) != 0; }
};

// --- little-endian scalar helpers (host=x86, but explicit for clarity) ------
namespace detail {
    inline std::uint16_t rd_u16(const std::uint8_t* p) {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }
    inline std::int16_t rd_i16(const std::uint8_t* p) {
        return static_cast<std::int16_t>(rd_u16(p));
    }
    inline std::int32_t rd_i32(const std::uint8_t* p) {
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(p[0]) |
            (static_cast<std::uint32_t>(p[1]) << 8) |
            (static_cast<std::uint32_t>(p[2]) << 16) |
            (static_cast<std::uint32_t>(p[3]) << 24));
    }
}

// --- non-owning reader over a loaded Creature.pak ---------------------------
class CreaturePak {
public:
    CreaturePak() = default;
    // `data`/`size` = a loaded Creature.pak file. Not owned; must outlive this.
    CreaturePak(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool valid() const { return data_ != nullptr; }

    // "CIF" magic check (refs §B2: NOT "SCR").
    bool magic_ok() const {
        return data_ && size_ >= 3 &&
               data_[0] == 'C' && data_[1] == 'I' && data_[2] == 'F';
    }

    // CreaturesCnt as stored. VB reads it after the 3-byte signature + Version;
    // the exact byte offset of the u32 count is ambiguous in the VB source
    // (Signatur, then Version, then Cnt). We expose a best-effort accessor AND a
    // derived count from file size (which is unambiguous and what callers should
    // prefer for iteration).
    // TODO(verify): pin the stored-count byte offset against a real Creature.pak
    //   (compare stored_count() vs derived_count()).
    std::uint32_t stored_count_at(std::size_t off) const {
        if (!data_ || off + 4 > size_) return 0;
        return static_cast<std::uint32_t>(detail::rd_i32(data_ + off));
    }

    // Number of whole 86-byte records that fit between the record array start
    // (0x100) and end of file. Authoritative for iteration regardless of where
    // the stored count lives.
    std::uint32_t derived_count() const {
        if (!data_ || size_ <= CreaturePakLayout::kRecordsOffset) return 0;
        std::size_t avail = size_ - CreaturePakLayout::kRecordsOffset;
        return static_cast<std::uint32_t>(avail / CreaturePakLayout::kRecordSize);
    }

    // Decode record `index` (0-based). {valid=false} on OOB.
    SacredCreature at(std::uint32_t index) const {
        SacredCreature c;
        const std::size_t rec = CreaturePakLayout::kRecordsOffset +
                                static_cast<std::size_t>(index) * CreaturePakLayout::kRecordSize;
        if (!data_ || rec + CreaturePakLayout::kRecordSize > size_) return c;
        const std::uint8_t* p = data_ + rec;

        c.id    = detail::rd_i32(p + CreaturePakLayout::kID);
        c.clazz = static_cast<Class>(detail::rd_i16(p + CreaturePakLayout::kClass));
        c.flags = p[CreaturePakLayout::kFlags];
        c.expA  = detail::rd_i16(p + CreaturePakLayout::kExpA);
        c.expB  = detail::rd_i16(p + CreaturePakLayout::kExpB);
        c.baseStrength  = p[CreaturePakLayout::kBaseStrength];
        c.baseEndurance = p[CreaturePakLayout::kBaseEndurance];
        c.baseDexterity = p[CreaturePakLayout::kBaseDexterity];
        c.basePhysReg   = p[CreaturePakLayout::kBasePhysReg];
        c.baseMagReg    = p[CreaturePakLayout::kBaseMagReg];
        c.baseCharisma  = p[CreaturePakLayout::kBaseCharisma];
        std::memcpy(c.skills.data(), p + CreaturePakLayout::kSkills, kSkillCount);
        c.baseWalkSpeed = detail::rd_i16(p + CreaturePakLayout::kBaseWalkSpeed);
        c.baseRunSpeed  = detail::rd_i16(p + CreaturePakLayout::kBaseRunSpeed);
        for (std::size_t i = 0; i < kBoniCount; ++i) {
            c.boni[i].level = p[CreaturePakLayout::kBoni + i * 2 + 0];
            c.boni[i].type  = p[CreaturePakLayout::kBoni + i * 2 + 1];
            c.boni[i].value = p[CreaturePakLayout::kBoniValue + i];
        }
        c.valid = true;
        return c;
    }

    // Iterate every decodable record; `fn(const SacredCreature&)` returns false
    // to stop early.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        const std::uint32_t n = derived_count();
        for (std::uint32_t i = 0; i < n; ++i) {
            SacredCreature c = at(i);
            if (!c.valid) continue;
            if (!fn(static_cast<const SacredCreature&>(c))) return;
        }
    }

    // Find by engine creature type id (linear; ids are not record indices).
    SacredCreature find_by_id(std::int32_t id) const {
        SacredCreature result;
        for_each([&](const SacredCreature& c) -> bool {
            if (c.id == id) { result = c; return false; }
            return true;
        });
        return result;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t         size_ = 0;
};

} // namespace creature
} // namespace sacred

#endif // SACRED_SDK_PORTS_SACRED_CREATURE_H
