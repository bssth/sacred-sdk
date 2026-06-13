// hero_save.h — Sacred Gold hero-save (".pax") container reader/writer.
//
// SOURCE (triple-corroborated, see sdk/.claude/knowledge/refs_native_src.md #1):
//   - HeroDump (C++/MFC):  E:/refs_extract/HeroDump_Quellcode/SacredHeroFile.cpp / PaxSection.h
//   - charmodif (Pascal):  E:/refs_extract/sacred_charmodif_src/.../classHero.pas
//                          (CompileHero @150 / DecompileHero @211 — the canonical read+rewrite)
//   - shlib / BHVS (C++):  E:/refs_extract/BHVS_quellcode/HeroInfo.cpp (direct-read header path)
//   - zlib framing:        sdk/.claude/knowledge/refs_resacred.md (plain zlib streams)
//
// BYTE-SIGNATURE VERIFICATION against sdk/Sacred_decrypted.exe (EXE base 0x00400000, no ASLR;
// file offset = VA - 0x400000):
//   - Compression sentinel 0xBAADC0DE present @ file 0x1B71C4 (VA 0x5B71C4), 5 sites total.  [confirmed]
//   - Save path template "Save\Hero%.2d.pax" present @ ~VA 0x95E4E1  => filenames Hero00.pax .. Hero07.pax. [confirmed]
//   - "inflate" symbol present @ VA 0x895549 => engine ships zlib inflate.  [confirmed]
//   The AMH magic / version words are not constant-pooled as contiguous literals (the engine builds
//   /compares the header field-wise), so they are not byte-sig-checkable as a string here.
//   TODO(verify): when wiring, confirm the per-WORD version marker on a real Steam-Gold Hero0?.pax
//                 dump equals 0x101B (UW). The Steam Gold build should be UW (= our build).
//
// Container layout (Hero00.pax):
//   0x000  Header (raw, first 0x1C0 bytes copied verbatim by charmodif):
//          +0x00 BYTE[3] magic = {'A','M','H'} = {0x41,0x4D,0x48}
//          +0x03 WORD    version / SubHdr1: 0x1006="1.6"(unsupported), 0x1007="1.7/1.8"(Plus, OpenMethod 1),
//                        0x101B="2.21 UW"(Underworld, OpenMethod 2 == our build).
//                        (BHVS flags "is UW" when the byte at +0x03 >= 0x1A.)
//          +0x05 BYTE[3] null
//          +0x08 WORD    SubHdr2 = 0x022C
//          +0x0A BYTE[30] unknown
//          +0x1E WORD    VersionID (build sub-version; 0x2A3B for v1.8)
//   0x100  Allocation table: 10 entries x 12 bytes = TATRecord { u32 type; u32 offset; u32 unpackedSize }.
//   <var>  Each entry's 'offset' points at a payload. A COMPRESSED payload begins with TZSectionHeader:
//          { u32 BadCode=0xBAADC0DE; u32 compressedSize; u8 null[24]; } then a zlib stream of that size.
//          If the first DWORD at 'offset' != 0xBAADC0DE the payload is STORED (length = unpackedSize).
//          Special case: section type 0xC4 is always 40/64 bytes uncompressed (HeroDump hardcodes 64).
//
// Logical stream <-> table mapping (charmodif csXxx):
//   table[0]=0xC7 HeroInfo (compressed, the stat block)   table[1]=0xCA   table[2]=0xCB   table[3]=0xC8
//   table[4]=0xC3 SpecialInfo (STORED)   table[5]=0xC4 AdditionalInfo (STORED)   table[8]=0xCD (compressed)
//   table[9]=0xCE (STORED).  Indices 6,7 are unused/skipped on rewrite.
//   On write the order is: [0..3] compressed, [4] raw, [5] raw, [8] compressed, [9] raw; the 0x1C0 header
//   is copied verbatim, then the table is rewritten in place at 0x100 with the new offsets.
//
// TODO(port): wire when sacred.read_save / sacred.write_save lands AND zlib is linked (SACRED_HAVE_ZLIB).
//             This class is written-but-not-wired: not added to SacredSDK.vcxproj, not #included anywhere.
//
// Standalone: depends only on the STL + zlib_codec.h (this directory). No engine/sdk.h, no engine/mem.h.

#ifndef SACRED_PORTS_HEROSAVE_HERO_SAVE_H
#define SACRED_PORTS_HEROSAVE_HERO_SAVE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sacred {
namespace herosave {

// ----- on-disk POD layouts (offsets verified above) -------------------------------------------

#pragma pack(push, 1)

// Allocation-table record @ 0x100, 12 bytes. (HeroDump SECTIONENTRY / charmodif TATRecord.)
struct PaxAllocEntry {
    uint32_t type;          // section data-type id (0xC7, 0xC3, 0xC4, ...); 0 = unused slot
    uint32_t offset;        // absolute file offset of the payload (or its TZSectionHeader)
    uint32_t unpackedSize;  // size of the section AFTER inflate (== stored length when uncompressed)
};
static_assert(sizeof(PaxAllocEntry) == 12, "PaxAllocEntry must be 12 bytes");

// Compressed-payload framing header, 32 bytes. (HeroDump tSection prefix / charmodif TZSectionHeader.)
struct PaxZSectionHeader {
    uint32_t badCode;            // == kCompressionSentinel (0xBAADC0DE) when compressed
    uint32_t compressedSize;     // length of the zlib stream that follows this 32-byte header
    uint8_t  reserved[24];       // zero-filled
};
static_assert(sizeof(PaxZSectionHeader) == 32, "PaxZSectionHeader must be 32 bytes");

#pragma pack(pop)

// ----- constants ------------------------------------------------------------------------------

constexpr uint32_t kCompressionSentinel = 0xBAADC0DEu;  // confirmed in EXE @ VA 0x5B71C4
constexpr size_t   kAllocTableOffset    = 0x100;        // allocation table location
constexpr size_t   kAllocTableCount     = 10;           // 10 x 12-byte entries
constexpr size_t   kHeaderCopyLen       = 0x1C0;        // bytes of header copied verbatim on rewrite

// PAX format / version words (header +0x03).
constexpr uint16_t kVersionPlus17 = 0x1007;  // Sacred Plus 1.7/1.8 (OpenMethod 1)
constexpr uint16_t kVersionUW221  = 0x101B;  // Sacred Underworld 2.21 (OpenMethod 2) — OUR build
constexpr uint8_t  kUwVersionByteMin = 0x1A; // BHVS: header[3] >= 0x1A  => Underworld

// Section data-type ids (charmodif csXxx mapping).
constexpr uint32_t kTypeHeroInfo       = 0xC7;  // the stat block (hero_stats.h reads this)
constexpr uint32_t kTypeSpecialInfo    = 0xC3;  // TSpecialInfo (name/level for the Import list)
constexpr uint32_t kTypeAdditionalInfo = 0xC4;  // 40/64-byte uncompressed special-case
constexpr uint32_t kTypeCA             = 0xCA;
constexpr uint32_t kTypeCB             = 0xCB;
constexpr uint32_t kTypeC8             = 0xC8;
constexpr uint32_t kTypeCD             = 0xCD;
constexpr uint32_t kTypeCE             = 0xCE;

// HeroDump hardcodes the 0xC4 section's uncompressed length. charmodif uses the table size; the
// note in refs_native_src.md says "40-byte uncompressed special-case" while HeroDump uses 64.
// TODO(verify): on a real Steam-Gold dump, confirm whether table[5].unpackedSize is 40 or 64 for
//               the 0xC4 section. We default to honoring the allocation-table size and only fall
//               back to this clamp when the table size looks wrong.
constexpr size_t kType0xC4HardLen = 64;

// Which PAX version this save reports.
enum class PaxVersion {
    Unknown,
    Plus_1x,   // 0x1007 (OpenMethod 1)
    UW_221,    // 0x101B (OpenMethod 2) — the layout our stat getters target
};

// OpenMethod, as charmodif names it. The hero_stats field offsets differ by +0x48 between methods
// (Classic/Plus vs UW). hero_stats.h encodes BOTH layouts and selects on this.
enum class OpenMethod {
    Unknown = 0,
    Method1 = 1,  // Sacred Plus 1.7/1.8
    Method2 = 2,  // Sacred Underworld 2.21 — our build
};

enum class LoadStatus {
    Ok = 0,
    FileNotFound,
    TooSmall,         // file shorter than header + alloc table
    BadMagic,         // header magic != "AMH"
    UnsupportedVersion,
    DecompressFailed, // a compressed section failed to inflate (zlib not linked or stream error)
    Truncated,        // a section offset/size runs past EOF
    IoError
};

// ----- container ------------------------------------------------------------------------------

// Reads / mutates / rewrites a Hero??.pax file as a map of section-type -> DECOMPRESSED bytes.
//
// Typical use (once wired):
//   PaxFile f;
//   if (f.Load(L"Save/Hero00.pax") == LoadStatus::Ok) {
//       auto& heroInfo = f.Section(kTypeHeroInfo);   // HeroStats wraps this
//       ... mutate heroInfo ...
//       f.Save(L"Save/Hero00.pax");
//   }
class PaxFile {
public:
    PaxFile() = default;

    // Load + parse + inflate every section. Returns Ok on full success.
    LoadStatus Load(const std::wstring& path);

    // Parse from an in-memory file image (e.g. read by the caller / engine). 'rawFile' is the whole
    // .pax file. This is the testable core; Load() is a thin file-reading wrapper over it.
    LoadStatus Parse(const std::vector<uint8_t>& rawFile);

    // Re-frame all sections (deflating the compressed ones), rebuild the alloc table, and write out.
    // Reproduces charmodif's CompileHero write order EXACTLY so the engine still accepts the save.
    // Returns true on success. Requires zlib (SACRED_HAVE_ZLIB) for the compressed sections.
    bool Save(const std::wstring& path);

    // Serialize to an in-memory image (the bytes Save() would write). Empty on failure.
    std::vector<uint8_t> Serialize() const;

    // Accessors over the decompressed section payloads, keyed by section type (0xC7, 0xC3, ...).
    bool HasSection(uint32_t type) const { return sections_.count(type) != 0; }
    std::vector<uint8_t>&       Section(uint32_t type) { return sections_[type]; }
    const std::vector<uint8_t>* SectionPtr(uint32_t type) const;
    std::map<uint32_t, std::vector<uint8_t>>&       Sections()       { return sections_; }
    const std::map<uint32_t, std::vector<uint8_t>>& Sections() const { return sections_; }

    // Header / version queries (valid after a successful Load/Parse).
    PaxVersion   Version()    const { return version_; }
    OpenMethod   Method()     const;
    bool         IsUnderworld() const;
    uint16_t     VersionWord() const { return versionWord_; }   // header +0x03
    const std::vector<uint8_t>& RawHeader() const { return header_; }  // first 0x1C0 bytes

private:
    // True if a section type is stored COMPRESSED on disk (so Save() must deflate it).
    // From charmodif's write order: 0xC7,0xCA,0xCB,0xC8 (table 0..3) and 0xCD (table 8) are compressed;
    // 0xC3,0xC4,0xCE (table 4,5,9) are stored raw.
    static bool IsCompressedType(uint32_t type);

    std::vector<uint8_t> header_;     // verbatim first 0x1C0 bytes (preserved across rewrite)
    uint16_t versionWord_ = 0;        // header +0x03
    PaxVersion version_ = PaxVersion::Unknown;

    // Decompressed section payloads, keyed by type id. Insertion order is not relied upon; Save()
    // rebuilds the alloc table in the fixed engine order regardless.
    std::map<uint32_t, std::vector<uint8_t>> sections_;

    // The original alloc-table types in their on-disk slot order (0..9), so Serialize() can place
    // sections back into the same slots / preserve unused (type==0) and the 6,7 skip slots.
    PaxAllocEntry origTable_[kAllocTableCount] = {};
    bool haveOrigTable_ = false;
};

}  // namespace herosave
}  // namespace sacred

#endif  // SACRED_PORTS_HEROSAVE_HERO_SAVE_H
