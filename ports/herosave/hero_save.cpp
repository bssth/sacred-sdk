// hero_save.cpp — PaxFile container implementation.
//
// SOURCE: see hero_save.h. Parse() mirrors HeroDump SacredHeroFile.cpp::Load (alloc table @0x100,
// per-section 0xBAADC0DE detection, 0xC4 special-case). Serialize() reproduces charmodif
// classHero.pas::CompileHero write order verbatim ([0..3] zlib, [4],[5] raw, [8] zlib, [9] raw,
// header 0x1C0 copied, alloc table rewritten @0x100).
//
// TODO(port): wire when sacred.read_save/write_save lands. Compressed sections require zlib
//             (SACRED_HAVE_ZLIB); without it Load() of a compressed section returns DecompressFailed
//             and Save() of one fails.

#include "hero_save.h"
#include "zlib_codec.h"   // CodecStatus / Inflate / Deflate (wiring fix)

#include <cstring>
#include <fstream>

namespace sacred {
namespace herosave {

namespace {

// Little-endian readers (the EXE is x86; structs are LE on disk). Defined locally so this TU has no
// dependency on engine/mem.h, per the port rules.
template <typename T>
T readLE(const uint8_t* p) {
    T v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<T>(p[i]) << (8 * i);
    }
    return v;
}

PaxVersion classifyVersion(uint16_t w) {
    switch (w) {
        case kVersionUW221:  return PaxVersion::UW_221;
        case kVersionPlus17: return PaxVersion::Plus_1x;
        default:             return PaxVersion::Unknown;
    }
}

}  // namespace

const std::vector<uint8_t>* PaxFile::SectionPtr(uint32_t type) const {
    auto it = sections_.find(type);
    return (it == sections_.end()) ? nullptr : &it->second;
}

OpenMethod PaxFile::Method() const {
    switch (version_) {
        case PaxVersion::UW_221:  return OpenMethod::Method2;
        case PaxVersion::Plus_1x: return OpenMethod::Method1;
        default:                  return OpenMethod::Unknown;
    }
}

bool PaxFile::IsUnderworld() const {
    // Primary: explicit version word. Fallback: BHVS heuristic (header byte +0x03 >= 0x1A).
    if (version_ == PaxVersion::UW_221) {
        return true;
    }
    return (versionWord_ & 0xFFu) >= kUwVersionByteMin;
}

bool PaxFile::IsCompressedType(uint32_t type) {
    switch (type) {
        case kTypeHeroInfo:  // 0xC7
        case kTypeCA:        // 0xCA
        case kTypeCB:        // 0xCB
        case kTypeC8:        // 0xC8
        case kTypeCD:        // 0xCD
            return true;
        default:
            return false;  // 0xC3, 0xC4, 0xCE stored raw
    }
}

LoadStatus PaxFile::Load(const std::wstring& path) {
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in) {
        return LoadStatus::FileNotFound;
    }
    std::streamoff len = in.tellg();
    if (len <= 0) {
        return LoadStatus::TooSmall;
    }
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> raw(static_cast<size_t>(len));
    if (!in.read(reinterpret_cast<char*>(raw.data()), len)) {
        return LoadStatus::IoError;
    }
    return Parse(raw);
}

LoadStatus PaxFile::Parse(const std::vector<uint8_t>& raw) {
    sections_.clear();
    header_.clear();
    haveOrigTable_ = false;
    version_ = PaxVersion::Unknown;
    versionWord_ = 0;

    if (raw.size() < kAllocTableOffset + kAllocTableCount * sizeof(PaxAllocEntry)) {
        return LoadStatus::TooSmall;
    }

    // Header magic "AMH".
    if (!(raw[0] == 0x41 && raw[1] == 0x4D && raw[2] == 0x48)) {
        return LoadStatus::BadMagic;
    }

    versionWord_ = readLE<uint16_t>(raw.data() + 0x03);
    version_ = classifyVersion(versionWord_);
    // Don't hard-fail unknown versions outright (a Steam-Gold build word might differ); but flag
    // truly unsupported old formats.
    if (version_ == PaxVersion::Unknown && !IsUnderworld()) {
        // TODO(verify): if a real Steam Hero0?.pax reports a word other than 0x101B, widen this.
        return LoadStatus::UnsupportedVersion;
    }

    // Preserve the verbatim header region.
    size_t headerLen = (raw.size() >= kHeaderCopyLen) ? kHeaderCopyLen : raw.size();
    header_.assign(raw.begin(), raw.begin() + headerLen);

    // Read the 10-entry allocation table.
    const uint8_t* tablePtr = raw.data() + kAllocTableOffset;
    for (size_t i = 0; i < kAllocTableCount; ++i) {
        const uint8_t* e = tablePtr + i * sizeof(PaxAllocEntry);
        origTable_[i].type         = readLE<uint32_t>(e + 0);
        origTable_[i].offset       = readLE<uint32_t>(e + 4);
        origTable_[i].unpackedSize = readLE<uint32_t>(e + 8);
    }
    haveOrigTable_ = true;

    // Walk each used entry.
    for (size_t i = 0; i < kAllocTableCount; ++i) {
        const PaxAllocEntry& ent = origTable_[i];
        if (ent.type == 0) {
            continue;  // unused slot
        }
        if (ent.offset + sizeof(uint32_t) > raw.size()) {
            return LoadStatus::Truncated;
        }

        uint32_t firstDword = readLE<uint32_t>(raw.data() + ent.offset);
        if (firstDword == kCompressionSentinel) {
            // Compressed: TZSectionHeader then a zlib stream of compressedSize.
            if (ent.offset + sizeof(PaxZSectionHeader) > raw.size()) {
                return LoadStatus::Truncated;
            }
            uint32_t compSize = readLE<uint32_t>(raw.data() + ent.offset + 4);
            size_t streamStart = ent.offset + sizeof(PaxZSectionHeader);
            if (streamStart + compSize > raw.size()) {
                return LoadStatus::Truncated;
            }
            std::vector<uint8_t> out;
            CodecStatus cs = Inflate(raw.data() + streamStart, compSize, ent.unpackedSize, out);
            if (cs != CodecStatus::Ok) {
                return LoadStatus::DecompressFailed;
            }
            sections_[ent.type] = std::move(out);
        } else {
            // Stored uncompressed: length = unpackedSize, with the 0xC4 special-case clamp.
            size_t storedLen = ent.unpackedSize;
            if (ent.type == kTypeAdditionalInfo) {
                // HeroDump hardcodes 64 for 0xC4; honor table size unless it looks wrong.
                if (storedLen == 0 || storedLen > raw.size()) {
                    storedLen = kType0xC4HardLen;
                }
            }
            if (ent.offset + storedLen > raw.size()) {
                return LoadStatus::Truncated;
            }
            sections_[ent.type].assign(raw.begin() + ent.offset,
                                       raw.begin() + ent.offset + storedLen);
        }
    }

    return LoadStatus::Ok;
}

std::vector<uint8_t> PaxFile::Serialize() const {
    std::vector<uint8_t> out;
    if (!haveOrigTable_ || header_.size() < kAllocTableOffset) {
        return out;  // nothing valid loaded
    }

    // Start with the verbatim header (>= 0x100). We rewrite the alloc table at 0x100 at the end, so
    // the bytes between 0x100 and the end of the header copy are placeholder (overwritten below).
    out = header_;
    if (out.size() < kHeaderCopyLen) {
        out.resize(kHeaderCopyLen, 0);  // ensure the full 0x1C0 region exists
    }

    // Working copy of the table we will patch with new offsets, then write back at 0x100.
    PaxAllocEntry table[kAllocTableCount];
    std::memcpy(table, origTable_, sizeof(table));

    // Helper: append a stored (raw) section for the given type if present; record offset/size.
    auto appendRaw = [&](size_t slot, uint32_t type) -> bool {
        auto it = sections_.find(type);
        if (it == sections_.end()) {
            // Not present: leave the slot's existing type/size, but zero offset (engine may skip).
            return true;
        }
        table[slot].type = type;
        table[slot].offset = static_cast<uint32_t>(out.size());
        table[slot].unpackedSize = static_cast<uint32_t>(it->second.size());
        out.insert(out.end(), it->second.begin(), it->second.end());
        return true;
    };

    // Helper: append a compressed section (TZSectionHeader + zlib stream) for the given type.
    auto appendCompressed = [&](size_t slot, uint32_t type) -> bool {
        auto it = sections_.find(type);
        if (it == sections_.end()) {
            return true;
        }
        std::vector<uint8_t> z;
        if (Deflate(it->second, z) != CodecStatus::Ok) {
            return false;  // zlib not linked or deflate error
        }
        table[slot].type = type;
        table[slot].offset = static_cast<uint32_t>(out.size());
        table[slot].unpackedSize = static_cast<uint32_t>(it->second.size());

        PaxZSectionHeader sh;
        std::memset(&sh, 0, sizeof(sh));
        sh.badCode = kCompressionSentinel;
        sh.compressedSize = static_cast<uint32_t>(z.size());
        const uint8_t* shp = reinterpret_cast<const uint8_t*>(&sh);
        out.insert(out.end(), shp, shp + sizeof(sh));
        out.insert(out.end(), z.begin(), z.end());
        return true;
    };

    // EXACT charmodif CompileHero order:
    //   table[0..3] compressed = C7, CA, CB, C8
    //   table[4] raw = C3 ; table[5] raw = C4
    //   table[8] compressed = CD
    //   table[9] raw = CE
    // table[6],[7] are not rewritten (left as-is from origTable_, typically unused).
    const uint32_t compTypes[4] = {kTypeHeroInfo, kTypeCA, kTypeCB, kTypeC8};
    for (size_t i = 0; i < 4; ++i) {
        // Prefer the type the section currently occupies in this slot; fall back to the canonical id.
        uint32_t t = (origTable_[i].type != 0) ? origTable_[i].type : compTypes[i];
        if (!appendCompressed(i, t)) {
            return std::vector<uint8_t>();  // fail (zlib missing)
        }
    }
    appendRaw(4, (origTable_[4].type != 0) ? origTable_[4].type : kTypeSpecialInfo);
    appendRaw(5, (origTable_[5].type != 0) ? origTable_[5].type : kTypeAdditionalInfo);
    if (!appendCompressed(8, (origTable_[8].type != 0) ? origTable_[8].type : kTypeCD)) {
        return std::vector<uint8_t>();
    }
    appendRaw(9, (origTable_[9].type != 0) ? origTable_[9].type : kTypeCE);

    // Rewrite the alloc table at 0x100 (LE).
    uint8_t* tp = out.data() + kAllocTableOffset;
    for (size_t i = 0; i < kAllocTableCount; ++i) {
        const PaxAllocEntry& e = table[i];
        auto put = [&](size_t base, uint32_t v) {
            tp[base + 0] = static_cast<uint8_t>(v & 0xFF);
            tp[base + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            tp[base + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            tp[base + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
        };
        size_t off = i * sizeof(PaxAllocEntry);
        put(off + 0, e.type);
        put(off + 4, e.offset);
        put(off + 8, e.unpackedSize);
    }

    return out;
}

bool PaxFile::Save(const std::wstring& path) {
    std::vector<uint8_t> image = Serialize();
    if (image.empty()) {
        return false;
    }
    std::ofstream o(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!o) {
        return false;
    }
    o.write(reinterpret_cast<const char*>(image.data()),
            static_cast<std::streamsize>(image.size()));
    return static_cast<bool>(o);
}

}  // namespace herosave
}  // namespace sacred
