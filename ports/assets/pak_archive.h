// pak_archive.h — Sacred Gold PAK container readers (THREE distinct, separately-modeled families).
//
// SOURCES:
//   sdk/.claude/knowledge/refs_formats_data.md
//     - B1 "Generic resource PAK (SND/TEX/MDL)" (lines 97-117) -> ResourcePak below.
//     - B2 "Creature.pak (CIF)" (lines 119-147)               -> CreatureCif below.
//     - Port candidate P5 (resource-PAK reader), P6 (Creature.pak reader).
//   sdk/.claude/knowledge/refs_resacred.md
//     - "PAK container (PakHeader 256B + PakSubFileDesc[])" (line 57) -> ResacredPak below.
//     - Port candidate #2 (PAK container reader, entry#0-sentinel quirk).
//
// IMPORTANT: these are THREE different on-disk layouts that share the ".pak" extension. They are
// modeled as three separate types on purpose — do NOT conflate them:
//   (a) ResourcePak  — SND/TEX/MDL magic, entryCount @0x04, FIXED 12-byte offset table @0x100,
//                      payloads raw/uncompressed at absolute file offsets.
//   (b) ResacredPak  — Resacred's PakHeader (256B) + 12-byte PakSubFileDesc[] descriptors right
//                      after the header; entry #0 is often a skipped sentinel.
//   (c) CreatureCif  — Creature.pak: "CIF" magic, then a flat array of FIXED 86-byte records
//                      beginning at file offset 0x101 (VB 1-based Seek 257 == 0-based byte 256
//                      for the header; first record byte is 256, "0x101" is the VB seek token).
//
// Dependency-light: standard headers only. No engine/sdk.h, no engine/mem.h. The caller supplies
// the whole file as a contiguous byte buffer; these readers do not perform any file I/O so they
// stay testable and portable.
//
// TODO(port): wire when the SDK needs to enumerate/extract Sacred assets at runtime instead of
//   (or alongside) live-RAM reads. Until then this is FUTURE-USE: written, not #included anywhere,
//   not added to SacredSDK.vcxproj.
// TODO(verify): these formats use NO absolute engine VAs — all offsets are in-file constants from
//   the editor sources / Resacred RE, so no byte-sig vs Sacred_decrypted.exe is required here.

#ifndef SACRED_PORTS_ASSETS_PAK_ARCHIVE_H
#define SACRED_PORTS_ASSETS_PAK_ARCHIVE_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace sacred {
namespace assets {

// ============================================================================================
//  (a) RESOURCE PAK  — SND.pak / TEX.pak / MDL.pak   (refs_formats_data.md B1)
// ============================================================================================
//
//   0x00  char[3]  magic = "SND" | "TEX" | "MDL"   (ASCII, no NUL)
//   0x03  u8       pad/version (skipped — code seeks to 0x04)
//   0x04  u32      entryCount
//   0x08..0xFF     reserved header padding
//   0x100          entry table: entryCount * 12 bytes, each:
//                     u32 type    (subtype; SND: 0x20=wav, 0x21=mp3)
//                     u32 offset  (ABSOLUTE file offset of payload)
//                     u32 length  (payload byte length)
//                  entries with type==0 && offset==0 && length==0 are holes (skip).
//   Payloads are stored verbatim (NO compression).

constexpr std::size_t kResourcePakTableOffset = 0x100; // entry table is fixed at 256
constexpr std::size_t kResourcePakEntrySize   = 12;    // {u32 type, u32 offset, u32 length}

enum class ResourcePakKind : uint8_t { Unknown, Snd, Tex, Mdl };

struct ResourcePakEntry {
    uint32_t type;    // resource subtype (e.g. SND 0x20=wav, 0x21=mp3)
    uint32_t offset;  // absolute file offset of payload
    uint32_t length;  // payload length in bytes
    bool IsHole() const { return type == 0 && offset == 0 && length == 0; }
};

class ResourcePak {
public:
    // Parse from a whole-file buffer. Returns false on malformed/too-small input.
    // 'data' must remain valid for the lifetime of any Payload() call (we keep a pointer).
    bool Open(const uint8_t* data, std::size_t size);

    ResourcePakKind kind() const { return kind_; }
    uint8_t version() const { return version_; }
    const std::vector<ResourcePakEntry>& entries() const { return entries_; }

    // View into the payload bytes for entry i (NOT copied). Returns nullptr if out of range or
    // the payload extends past the buffer. *outLen receives the byte length.
    const uint8_t* Payload(std::size_t i, std::size_t* outLen) const;

    // Map a SND/TEX/MDL subtype to a conventional file extension (PakExtractor naming):
    //   0x20 -> "wav", 0x21 -> "mp3", else "<hex>" (caller formats). Returns nullptr if unknown.
    static const char* SndExtForType(uint32_t type);

private:
    const uint8_t* base_ = nullptr;
    std::size_t    size_ = 0;
    ResourcePakKind kind_ = ResourcePakKind::Unknown;
    uint8_t version_ = 0;
    std::vector<ResourcePakEntry> entries_;
};

// ============================================================================================
//  (b) RESACRED PAK  — generic PakHeader (256B) + PakSubFileDesc[]   (refs_resacred.md line 57)
// ============================================================================================
//
//   PakHeader (256 bytes):
//     0x00 char  type[3];      // 3-char tag
//     0x03 u8    version;
//     0x04 i32   entryCount;
//     0x08 u8    pad8[8];
//     0x10 i32   worldX;
//     0x14 i32   worldY;
//     0x18 u8    pad232[232];  // fills out to 0x100
//   Then entryCount descriptors immediately follow the header (at 0x100), each 12 bytes:
//     i32 type; i32 offset; i32 size;
//   QUIRK: entry #0 is often a skipped/invalid sentinel. Floor & texture loaders advance
//   (fileDesc++; entryCount--) before iterating. We expose SkipSentinel() to do the same.

#pragma pack(push, 1)
struct ResacredPakHeader {
    char     type[3];
    uint8_t  version;
    int32_t  entryCount;
    uint8_t  pad8[8];
    int32_t  worldX;
    int32_t  worldY;
    uint8_t  pad232[232];
};
struct ResacredPakDesc {
    int32_t type;
    int32_t offset; // absolute file offset of the sub-file payload
    int32_t size;   // payload size in bytes
};
#pragma pack(pop)
static_assert(sizeof(ResacredPakHeader) == 256, "ResacredPakHeader must be 256 bytes");
static_assert(sizeof(ResacredPakDesc) == 12, "ResacredPakDesc must be 12 bytes");

constexpr std::size_t kResacredPakHeaderSize = 256;

class ResacredPak {
public:
    bool Open(const uint8_t* data, std::size_t size);

    const ResacredPakHeader& header() const { return header_; }
    const std::vector<ResacredPakDesc>& descs() const { return descs_; }

    // Advance past the entry#0 sentinel (Floor/texture/static/item loaders do this). Returns a
    // begin index of 1 if there is at least one entry, else 0. Use for ranged iteration:
    //   for (size_t i = pak.SkipSentinel(); i < pak.descs().size(); ++i) { ... }
    std::size_t SkipSentinel() const { return descs_.empty() ? 0 : 1; }

    // Payload view for descriptor i (not copied). nullptr if out of range / past buffer.
    const uint8_t* Payload(std::size_t i, std::size_t* outLen) const;

private:
    const uint8_t* base_ = nullptr;
    std::size_t    size_ = 0;
    ResacredPakHeader header_{};
    std::vector<ResacredPakDesc> descs_;
};

// ============================================================================================
//  (c) CREATURE.PAK  — "CIF" header + flat 86-byte record array  (refs_formats_data.md B2)
// ============================================================================================
//
//   0x00  char[3]  Signatur = "CIF"   (NOT "SCR")
//   0x03  ...      Version (== 0 required)
//                  u32 CreaturesCnt
//                  u8[4] Unknown
//   First record at byte offset 256 (VB `Seek FNr, 257` is 1-based == 0-based 256; the knowledge
//   note writes this as "0x101" using the VB seek token). Each record = 86 bytes, layout:
//     Long    ID            (4)   matches characters.csv decimal IDs
//     Integer Class         (2)   1 Hero..15 Dryade (see ClassId)
//     Byte    Flags         (1)   FlFly=1 FlBig=2 FlNoShadow=16 FlGhost=32 FlBanane=64 FlKurve=128
//     Byte    Unknown1      (1)
//     Integer ExpA, ExpB    (4)
//     Byte    BaseStrength, BaseEndurance, BaseDexterity, BasePhysReg, BaseMagReg, BaseCharisma (6)
//     Byte    Unknown2[2]   (2)
//     Byte    Skills[18]    (18)
//     Integer BaseWalkSpeed, BaseRunSpeed (4)
//     {Byte BonusLevel; Byte BonusType}[6]  (12)
//     Byte    BoniValue[6]  (6)
//     Byte    Unknown3[26]  (26)
//   = 4+2+1+1+4+6+2+18+4+12+6+26 = 86.
//   Trailing reserved = FileLen - (Cnt*86 + 256).

constexpr std::size_t kCreatureCifRecordOffset = 256; // first record byte (VB seek 257)
constexpr std::size_t kCreatureCifRecordSize   = 86;

enum class CreatureClassId : uint16_t {
    Hero = 1, Monster = 2, NPC = 3, Horse = 4, Undead = 5, Animal = 6, Mercenary = 7,
    Goblin = 8, Demon = 9, Dragon = 10, Energy = 11, Elve = 12, Enemy = 13, Human = 14,
    Dryade = 15
};

// Flags bitmask (Creature.pak Editor, VB).
enum CreatureFlag : uint8_t {
    FlFly = 1, FlBig = 2, FlNoShadow = 16, FlGhost = 32, FlBanane = 64, FlKurve = 128
};

#pragma pack(push, 1)
struct CreatureCifRecord {
    int32_t  id;             // matches characters.csv decimal IDs
    int16_t  classId;        // CreatureClassId
    uint8_t  flags;          // CreatureFlag bitmask
    uint8_t  unknown1;
    int16_t  expA;
    int16_t  expB;
    uint8_t  baseStrength;
    uint8_t  baseEndurance;
    uint8_t  baseDexterity;
    uint8_t  basePhysReg;
    uint8_t  baseMagReg;
    uint8_t  baseCharisma;
    uint8_t  unknown2[2];
    uint8_t  skills[18];     // skill ids (1..33)
    int16_t  baseWalkSpeed;
    int16_t  baseRunSpeed;
    struct { uint8_t level; uint8_t type; } boni[6]; // 12 bytes
    uint8_t  boniValue[6];
    uint8_t  unknown3[26];
};
#pragma pack(pop)
static_assert(sizeof(CreatureCifRecord) == kCreatureCifRecordSize,
              "CreatureCifRecord must be 86 bytes");

class CreatureCif {
public:
    bool Open(const uint8_t* data, std::size_t size);

    uint8_t  version() const { return version_; }
    uint32_t declaredCount() const { return declaredCount_; }

    // Number of fully-present 86-byte records the buffer actually contains (>= 0, <= declared).
    std::size_t recordCount() const { return records_; }

    // Read record i into 'out'. Returns false if i is out of range. Copies 86 bytes (the records
    // are packed, so a struct copy is layout-exact under MSVC #pragma pack(1)).
    bool Record(std::size_t i, CreatureCifRecord* out) const;

private:
    const uint8_t* base_ = nullptr;
    std::size_t    size_ = 0;
    uint8_t  version_ = 0;
    uint32_t declaredCount_ = 0;
    std::size_t records_ = 0;
};

} // namespace assets
} // namespace sacred

#endif // SACRED_PORTS_ASSETS_PAK_ARCHIVE_H
