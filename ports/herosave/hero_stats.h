// hero_stats.h — typed getters/setters over the PAX 0xC7 "Hero Info" stat section + 0xC3 SpecialInfo.
//
// SOURCE (sdk/.claude/knowledge/refs_native_src.md #1, charmodif Main.pas lines 595-832 +
// CombatArts.pas:18,62-89):
//   - All C7 field offsets below are into the DECOMPRESSED 0xC7 stream.
//   - charmodif reads/writes them ONLY for OpenMethod 2 (Underworld). The Classic/Plus (OpenMethod 1)
//     layout is the same fields shifted by -0x48 — proven by the ONLY offset charmodif gives for both
//     methods: CA count is 0x483 (Method1) vs 0x4CB (Method2) == exactly +0x48 (CombatArts.pas:72/74).
//     We encode BOTH layouts via a base delta and gate on the version word.
//
// VERSION GATE (critical): pass the PaxFile's OpenMethod (from hero_save.h, derived from header +0x03).
//   Method2 (UW, 0x101B) = our build => use the UW offsets as-is.
//   Method1 (Plus, 0x1007) => subtract kClassicDelta (0x48) from every C7 field.
//   TODO(verify): the -0x48 shift is corroborated only by the CA-count pair. Before trusting writes on
//     a Classic/Plus save, dump one and confirm level/XP/gold land at (UW_offset - 0x48). UW (our
//     build) is the validated path.
//
// TODO(port): wire when sacred.read_save/write_save lands. Written-but-not-wired (not in vcxproj).
//
// Standalone: STL + hero_save.h only (for the section-type ids + PaxFile sections). No engine deps.

#ifndef SACRED_PORTS_HEROSAVE_HERO_STATS_H
#define SACRED_PORTS_HEROSAVE_HERO_STATS_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "hero_save.h"

namespace sacred {
namespace herosave {

// ----- C7 "Hero Info" field offsets (UW / OpenMethod 2). Subtract kClassicDelta for OpenMethod 1. ---
//
// (charmodif Main.pas load/save line numbers in comments for traceability.)
namespace c7 {
constexpr size_t kCheatsUsed   = 0x020D;  // WORD  nonzero => cheats        (load 694 / save 787)
constexpr size_t kClass        = 0x03DD;  // BYTE  character/class id        (load 603)
constexpr size_t kExperience   = 0x03E1;  // DWORD cumulative XP             (load 625 / save 772)
constexpr size_t kCPoints      = 0x03F8;  // BYTE  "C-Points"                (load 618 / save 753)
constexpr size_t kSkillIds     = 0x03F9;  // BYTE[8] the 8 chosen skills     (load 656 / save 798)
constexpr size_t kSkillLevels  = 0x0401;  // BYTE[8] level of each skill      (load 681 / save 809)
constexpr size_t kAPoints      = 0x0417;  // WORD  "A-Points"                (load 621 / save 757)
constexpr size_t kGold         = 0x041B;  // DWORD gold                      (load 615 / save 749)
constexpr size_t kLevel        = 0x042B;  // WORD  character level           (load 628 / save 770)
constexpr size_t kCACountUW    = 0x04CB;  // BYTE  combat-art count (UW)      (CombatArts.pas:74)
constexpr size_t kCACountPlus  = 0x0483;  // BYTE  combat-art count (Plus)    (CombatArts.pas:72)
constexpr size_t kSkillSlots   = 8;
}  // namespace c7

// The fixed shift between the Plus/Classic (OpenMethod 1) and UW (OpenMethod 2) C7 layouts.
// UW_offset - kClassicDelta == Plus_offset. (0x4CB - 0x483 == 0x48.)
constexpr size_t kClassicDelta = 0x48;

#pragma pack(push, 1)
// One combat-art record in the C7 stream after the CA count. 22 bytes, packed (CombatArts.pas:21).
struct CARecord {
    uint8_t  unknown1[5];   // usually {0x00,0x02,0x00,0x00,0x00}
    uint16_t id;            // combat-art ID (see tableCombatArts)
    uint8_t  level;
    uint8_t  unknown2[14];
};
static_assert(sizeof(CARecord) == 22, "CARecord must be 22 bytes");

// 0xC3 "Special Info" (TSpecialInfo) — Stream 5, STORED uncompressed. Used by the in-game Import
// list and is the block BHVS reads for the hero summary. (refs_native_src.md / Main.pas:611-832.)
struct SpecialInfo {
    uint16_t heroLevel;            // 0x00 level shown in Import menu
    uint8_t  unknown1[2];          // 0x02
    uint8_t  characterType;        // 0x04 start-location class id
    uint8_t  unknown2[3];          // 0x05
    uint16_t heroName[64];         // 0x08 UTF-16LE, 128 bytes
    uint8_t  unknown3[106];        // 0x88
    uint8_t  levelsUnlock;         // 0xF2 =1 => all difficulties unlocked
    uint8_t  unknown3_1[6];        // 0xF3
    uint8_t  resurrectionsCounter; // 0xF9
    uint8_t  cheatFlag;            // 0xFA odd / 0x81 => cheats used
    uint8_t  unknown4[305];        // 0xFB
};
static_assert(sizeof(SpecialInfo) == 0x22C, "SpecialInfo size");  // last field 0xFB + 305 == 0x22C total
#pragma pack(pop)

// Class ids (charmodif CharsTable / BHVS HeroInfo.h). Use the constants, not raw index 6/7 (the
// printable-name array has a quirk there — see refs_native_src.md cross-checks).
enum class HeroClass : uint8_t {
    Seraphim   = 1,
    Gladiator  = 2,
    BattleMage = 3,  // "Magier"
    DarkElf    = 4,
    WoodElf    = 5,
    Vampiress  = 6,
    Dwarf      = 8,
    Demon      = 9,  // "Daemonin"
    Invalid    = 255,
};

// ----- typed view over a decompressed 0xC7 buffer -----------------------------------------------
//
// Non-owning: wraps a std::vector<uint8_t>& (the PaxFile section). Offsets are resolved through the
// supplied OpenMethod so the same code reads UW and Plus saves.
class HeroStats {
public:
    // 'c7' must be the DECOMPRESSED 0xC7 section (PaxFile::Section(kTypeHeroInfo)).
    // 'method' selects the layout (Method2 = UW = our build; Method1 = Plus, applies -0x48).
    HeroStats(std::vector<uint8_t>& c7, OpenMethod method)
        : buf_(c7), method_(method) {}

    // Convenience: build directly from a loaded PaxFile (uses its OpenMethod). Caller must keep the
    // PaxFile alive (we hold a reference into its section vector).
    static HeroStats FromPax(PaxFile& pax) {
        return HeroStats(pax.Section(kTypeHeroInfo), pax.Method());
    }

    bool valid() const { return method_ != OpenMethod::Unknown && fits(caCountOffset(), 1); }

    // --- scalar getters ---
    uint16_t level()      const { return get<uint16_t>(c7::kLevel); }
    uint32_t experience() const { return get<uint32_t>(c7::kExperience); }
    uint32_t gold()       const { return get<uint32_t>(c7::kGold); }
    HeroClass heroClass() const { return static_cast<HeroClass>(get<uint8_t>(c7::kClass)); }
    uint8_t  cPoints()    const { return get<uint8_t>(c7::kCPoints); }
    uint16_t aPoints()    const { return get<uint16_t>(c7::kAPoints); }
    bool     cheatsUsed() const { return get<uint16_t>(c7::kCheatsUsed) != 0; }

    // --- scalar setters ---
    void setLevel(uint16_t v)      { set<uint16_t>(c7::kLevel, v); }
    void setExperience(uint32_t v) { set<uint32_t>(c7::kExperience, v); }
    void setGold(uint32_t v)       { set<uint32_t>(c7::kGold, v); }
    void setHeroClass(HeroClass v) { set<uint8_t>(c7::kClass, static_cast<uint8_t>(v)); }
    void setCPoints(uint8_t v)     { set<uint8_t>(c7::kCPoints, v); }
    void setAPoints(uint16_t v)    { set<uint16_t>(c7::kAPoints, v); }
    void setCheatsUsed(bool v)     { set<uint16_t>(c7::kCheatsUsed, v ? 1 : 0); }

    // --- skills (8 slots of {id, level}) ---
    uint8_t skillId(size_t slot)    const { return slot < c7::kSkillSlots ? get<uint8_t>(c7::kSkillIds + slot) : 0; }
    uint8_t skillLevel(size_t slot) const { return slot < c7::kSkillSlots ? get<uint8_t>(c7::kSkillLevels + slot) : 0; }
    void setSkillId(size_t slot, uint8_t id)  { if (slot < c7::kSkillSlots) set<uint8_t>(c7::kSkillIds + slot, id); }
    void setSkillLevel(size_t slot, uint8_t l){ if (slot < c7::kSkillSlots) set<uint8_t>(c7::kSkillLevels + slot, l); }

    // --- combat arts ---
    // Offset of the CA-count byte for the active layout.
    size_t caCountOffset() const {
        return (method_ == OpenMethod::Method2) ? c7::kCACountUW : c7::kCACountPlus;
    }
    uint8_t caCount() const {
        size_t off = caCountOffset();
        return fits(off, 1) ? buf_[off] : 0;
    }
    // Copy out all CA records (count read at caCountOffset, then count x 22-byte CARecord).
    std::vector<CARecord> combatArts() const {
        std::vector<CARecord> out;
        size_t off = caCountOffset();
        if (!fits(off, 1)) return out;
        uint8_t n = buf_[off];
        size_t rec = off + 1;
        for (uint8_t i = 0; i < n; ++i) {
            if (!fits(rec, sizeof(CARecord))) break;
            CARecord r;
            std::memcpy(&r, buf_.data() + rec, sizeof(CARecord));
            out.push_back(r);
            rec += sizeof(CARecord);
        }
        return out;
    }
    // NOTE: rewriting the CA block changes the C7 stream length and must preserve the trailing bytes
    // after the block verbatim (charmodif copies the remainder back). A setter therefore needs to
    // rebuild buf_, not patch in place.
    // TODO(port): implement setCombatArts() that rebuilds [0..caCountOffset], writes count + records,
    //             then re-appends the original tail (everything after the old CA block). Left as a
    //             stub here to avoid corrupting the trailing unknown data on a guess.

private:
    std::vector<uint8_t>& buf_;
    OpenMethod method_;

    // Resolve a UW-relative C7 offset for the active layout.
    size_t resolve(size_t uwOffset) const {
        return (method_ == OpenMethod::Method2) ? uwOffset
                                                : (uwOffset - kClassicDelta);
    }
    bool fits(size_t off, size_t n) const { return off + n <= buf_.size(); }

    template <typename T>
    T get(size_t uwOffset) const {
        size_t off = resolve(uwOffset);
        T v = 0;
        if (fits(off, sizeof(T))) {
            for (size_t i = 0; i < sizeof(T); ++i) {
                v |= static_cast<T>(buf_[off + i]) << (8 * i);  // little-endian
            }
        }
        return v;
    }
    template <typename T>
    void set(size_t uwOffset, T v) {
        size_t off = resolve(uwOffset);
        if (fits(off, sizeof(T))) {
            for (size_t i = 0; i < sizeof(T); ++i) {
                buf_[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
            }
        }
    }
};

// ----- typed view over the 0xC3 SpecialInfo section (name etc.) ---------------------------------
class HeroSpecialInfo {
public:
    explicit HeroSpecialInfo(std::vector<uint8_t>& c3) : buf_(c3) {}

    static HeroSpecialInfo FromPax(PaxFile& pax) {
        return HeroSpecialInfo(pax.Section(kTypeSpecialInfo));
    }

    bool valid() const { return buf_.size() >= sizeof(SpecialInfo); }

    // UTF-16LE hero name @ 0x08 (up to 64 code units, NUL-terminated). Returned as UTF-16 (u16string)
    // so the caller decides how to render (names may carry color/icon escape codes — see BHVS
    // RemoveColorAndIcons; not stripped here).
    std::u16string name() const {
        std::u16string s;
        if (buf_.size() < 0x08 + 2) return s;
        size_t maxUnits = 64;
        for (size_t i = 0; i < maxUnits; ++i) {
            size_t off = 0x08 + i * 2;
            if (off + 2 > buf_.size()) break;
            char16_t c = static_cast<char16_t>(buf_[off] | (buf_[off + 1] << 8));
            if (c == 0) break;
            s.push_back(c);
        }
        return s;
    }
    void setName(const std::u16string& n) {
        if (buf_.size() < 0x08 + 64 * 2) return;
        for (size_t i = 0; i < 64; ++i) {
            char16_t c = (i < n.size()) ? n[i] : 0;
            size_t off = 0x08 + i * 2;
            buf_[off]     = static_cast<uint8_t>(c & 0xFF);
            buf_[off + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
        }
    }

    uint16_t importLevel() const { return u16at(0x00); }
    uint8_t  characterType() const { return at(0x04); }
    bool     levelsUnlocked() const { return at(0xF2) != 0; }
    uint8_t  resurrections() const { return at(0xF9); }
    bool     cheatFlag() const { uint8_t f = at(0xFA); return f == 0x81 || (f & 1); }

    void setImportLevel(uint16_t v) { setU16at(0x00, v); }
    void setLevelsUnlocked(bool v)  { setAt(0xF2, v ? 1 : 0); }
    void setResurrections(uint8_t v){ setAt(0xF9, v); }

private:
    std::vector<uint8_t>& buf_;
    uint8_t at(size_t o) const { return o < buf_.size() ? buf_[o] : 0; }
    void setAt(size_t o, uint8_t v) { if (o < buf_.size()) buf_[o] = v; }
    uint16_t u16at(size_t o) const {
        return (o + 1 < buf_.size()) ? static_cast<uint16_t>(buf_[o] | (buf_[o + 1] << 8)) : 0;
    }
    void setU16at(size_t o, uint16_t v) {
        if (o + 1 < buf_.size()) { buf_[o] = v & 0xFF; buf_[o + 1] = (v >> 8) & 0xFF; }
    }
};

}  // namespace herosave
}  // namespace sacred

#endif  // SACRED_PORTS_HEROSAVE_HERO_STATS_H
