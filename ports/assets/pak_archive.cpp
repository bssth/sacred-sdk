// pak_archive.cpp — implementations for the three Sacred PAK families.
//
// SOURCE: see pak_archive.h doc block (refs_formats_data.md B1/B2, refs_resacred.md PAK row).
//
// TODO(port): wire when the SDK enumerates/extracts Sacred assets at runtime. FUTURE-USE only —
//   not added to SacredSDK.vcxproj, not #included from any existing TU.

#include "pak_archive.h"

#include <cstring>

namespace sacred {
namespace assets {

namespace {

// Little-endian unaligned readers (Sacred files are LE; x86 host is LE but we stay explicit so
// this is correct even on a BE host and free of alignment UB).
inline uint32_t ReadU32LE(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

// ============================================================================================
//  (a) ResourcePak
// ============================================================================================

bool ResourcePak::Open(const uint8_t* data, std::size_t size) {
    base_ = nullptr;
    kind_ = ResourcePakKind::Unknown;
    version_ = 0;
    entries_.clear();

    if (!data || size < kResourcePakTableOffset) return false;

    // Magic dispatch on the 3-char tag.
    if (std::memcmp(data, "SND", 3) == 0)      kind_ = ResourcePakKind::Snd;
    else if (std::memcmp(data, "TEX", 3) == 0) kind_ = ResourcePakKind::Tex;
    else if (std::memcmp(data, "MDL", 3) == 0) kind_ = ResourcePakKind::Mdl;
    else return false;

    version_ = data[3];
    const uint32_t entryCount = ReadU32LE(data + 0x04);

    // Bound the table so we never read past the buffer.
    const std::size_t tableBytes = static_cast<std::size_t>(entryCount) * kResourcePakEntrySize;
    if (kResourcePakTableOffset > size) return false;
    if (tableBytes > size - kResourcePakTableOffset) return false; // overflow-safe

    entries_.reserve(entryCount);
    const uint8_t* e = data + kResourcePakTableOffset;
    for (uint32_t i = 0; i < entryCount; ++i, e += kResourcePakEntrySize) {
        ResourcePakEntry ent;
        ent.type   = ReadU32LE(e + 0);
        ent.offset = ReadU32LE(e + 4);
        ent.length = ReadU32LE(e + 8);
        entries_.push_back(ent);
    }

    base_ = data;
    size_ = size;
    return true;
}

const uint8_t* ResourcePak::Payload(std::size_t i, std::size_t* outLen) const {
    if (outLen) *outLen = 0;
    if (!base_ || i >= entries_.size()) return nullptr;
    const ResourcePakEntry& ent = entries_[i];
    if (ent.IsHole()) return nullptr;
    if (ent.offset > size_) return nullptr;
    if (ent.length > size_ - ent.offset) return nullptr; // overflow-safe bound check
    if (outLen) *outLen = ent.length;
    return base_ + ent.offset;
}

const char* ResourcePak::SndExtForType(uint32_t type) {
    switch (type) {
        case 0x20: return "wav";
        case 0x21: return "mp3";
        default:   return nullptr; // caller may fall back to a hex extension
    }
}

// ============================================================================================
//  (b) ResacredPak
// ============================================================================================

bool ResacredPak::Open(const uint8_t* data, std::size_t size) {
    base_ = nullptr;
    descs_.clear();
    std::memset(&header_, 0, sizeof(header_));

    if (!data || size < kResacredPakHeaderSize) return false;

    std::memcpy(&header_, data, sizeof(header_)); // packed struct == on-disk layout

    if (header_.entryCount < 0) return false;
    const std::size_t count = static_cast<std::size_t>(header_.entryCount);
    const std::size_t tableBytes = count * sizeof(ResacredPakDesc);
    if (tableBytes > size - kResacredPakHeaderSize) return false; // overflow-safe

    descs_.resize(count);
    if (count) std::memcpy(descs_.data(), data + kResacredPakHeaderSize, tableBytes);

    base_ = data;
    size_ = size;
    return true;
}

const uint8_t* ResacredPak::Payload(std::size_t i, std::size_t* outLen) const {
    if (outLen) *outLen = 0;
    if (!base_ || i >= descs_.size()) return nullptr;
    const ResacredPakDesc& d = descs_[i];
    if (d.offset < 0 || d.size < 0) return nullptr;
    const std::size_t off = static_cast<std::size_t>(d.offset);
    const std::size_t len = static_cast<std::size_t>(d.size);
    if (off > size_ || len > size_ - off) return nullptr;
    if (outLen) *outLen = len;
    return base_ + off;
}

// ============================================================================================
//  (c) CreatureCif
// ============================================================================================

bool CreatureCif::Open(const uint8_t* data, std::size_t size) {
    base_ = nullptr;
    version_ = 0;
    declaredCount_ = 0;
    records_ = 0;

    if (!data || size < kCreatureCifRecordOffset) return false;
    if (std::memcmp(data, "CIF", 3) != 0) return false;

    // Layout per VB editor: after "CIF" comes Version, then u32 CreaturesCnt, then u8[4] Unknown.
    // The VB read order leaves the count immediately after the 1-byte version; we read both but
    // the exact byte position of CreaturesCnt within 0x04..0x100 is editor-defined. The record
    // array is what matters and it is FIXED at byte 256, so we derive the present record count
    // from the buffer size and clamp the declared count to it.
    version_ = data[3];
    declaredCount_ = ReadU32LE(data + 0x04); // best-effort; see note above

    const std::size_t avail = size - kCreatureCifRecordOffset;
    const std::size_t present = avail / kCreatureCifRecordSize;
    records_ = present;
    // If the header's declared count is sane and smaller, honor it (trailing bytes are reserved).
    if (declaredCount_ != 0 && declaredCount_ < present) {
        records_ = declaredCount_;
    }

    base_ = data;
    size_ = size;
    return true;
}

bool CreatureCif::Record(std::size_t i, CreatureCifRecord* out) const {
    if (!base_ || !out || i >= records_) return false;
    const std::size_t off = kCreatureCifRecordOffset + i * kCreatureCifRecordSize;
    if (off + kCreatureCifRecordSize > size_) return false;
    std::memcpy(out, base_ + off, kCreatureCifRecordSize);
    return true;
}

} // namespace assets
} // namespace sacred
