// item_type_catalog.h — walker over the live engine's TYPE_* name/id table and a
// name->type-id resolver mirroring FUN_0043cd90.
//
// SOURCE:
//   sdk/.claude/knowledge/re/items.md §1 ("Unified object create path & type-id
//     space") and re_backlog.md S7:
//     - "5624 `TYPE_*` entries @ 0x008EC328 stride 0x44" (re_backlog S7).
//     - items.md §1: parallel arrays — *id* array @ 0x008EC328 (id = first
//       dword of each 0x44 record), *name* array @ 0x008EC32C (ASCIIZ, +4).
//       i.e. each 0x44-stride record is { i32 id @ +0; char name[] @ +4 }.
//     - Resolver FUN_0043cd90(name) -> type id; walks `uVar6 + 0x37 +
//       DAT_00aab5e4` (that is the *definition* table walk, 0x80 stride). The
//       TYPE_* NAME table that the SDK should walk for name<->id is the
//       0x008EC328 / +0x44-stride table (id at +0, name at +4).
//   Items and creatures share ONE type-id space (items.md TL;DR): the same
//     table holds TYPE_NPC_* (creatures) and TYPE_* (items), ids sequential.
//
// TODO(verify): the absolute VA 0x008EC328 (and that stride is 0x44, count 5624,
//   id@+0, name@+4) — byte-sig vs sdk/Sacred_decrypted.exe before any live use.
//   File offset to inspect = 0x008EC328 - 0x400000 = 0x4EC328. Confirm the first
//   record's id==0 and name=="TYPE_INVALID", record[1] name=="TYPE_NPC_SERAPHIM"
//   (items.md §1 dumped these).
//
// TODO(port): wire when the SDK needs an in-process name<->id catalog (so it can
//   resolve "TYPE_..." without calling FUN_0043cd90 every spawn). FUTURE-USE,
//   standalone: do NOT add to SacredSDK.vcxproj or #include from existing TUs.
//
// This header reads engine memory through a caller-supplied reader so it does
// NOT depend on engine/mem.h. Two modes:
//   (1) live mode — give it the table base pointer (already inside the game's
//       address space, e.g. when running in the injected DLL) and walk directly.
//   (2) snapshot/diagnostic mode — give it a `MemReader` callback that copies
//       `n` bytes from an absolute VA (so an out-of-process tool can use it too).
//
// C++14, MSVC.

#ifndef SACRED_SDK_PORTS_ITEM_TYPE_CATALOG_H
#define SACRED_SDK_PORTS_ITEM_TYPE_CATALOG_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <functional>

namespace sacred {
namespace items {

// --- table geometry (TODO(verify) all three vs Sacred_decrypted.exe) --------
struct TypeTableLayout {
    // Absolute engine VA of record 0. TODO(verify): byte-sig.
    static constexpr std::uintptr_t kTableVA   = 0x008EC328;
    static constexpr std::size_t    kStride    = 0x44;   // bytes per record
    static constexpr std::size_t    kIdOffset  = 0x00;   // i32 type id
    static constexpr std::size_t    kNameOffset= 0x04;   // ASCIIZ name (TYPE_*)
    static constexpr std::size_t    kNameMax   = kStride - kNameOffset; // 0x40
    static constexpr std::uint32_t  kCount     = 5624;   // entries (re_backlog S7)
};

// One decoded catalog row.
struct ItemType {
    std::int32_t id = 0;          // engine type id (NOT the row index)
    std::string  name;           // e.g. "TYPE_INVALID", "TYPE_NPC_SERAPHIM", "TYPE_SWORD_..."
    std::uint32_t row = 0;        // index within the table (0..kCount-1)
    bool valid = false;
};

// Memory reader: copy `len` bytes from absolute VA `va` into `dst`.
// Return true on success. In-process this is just a memcpy; out-of-process a
// tool would back it with ReadProcessMemory. Kept as a std::function so this
// header stays free of any engine/mem.h dependency.
using MemReader = std::function<bool(std::uintptr_t va, void* dst, std::size_t len)>;

// Default in-process reader (only valid when running inside the game process).
inline bool in_process_read(std::uintptr_t va, void* dst, std::size_t len) {
    std::memcpy(dst, reinterpret_cast<const void*>(va), len);
    return true;
}

class ItemTypeCatalog {
public:
    // In-process: walk the live table directly at its known VA.
    ItemTypeCatalog()
        : read_(&in_process_read),
          table_va_(TypeTableLayout::kTableVA),
          count_(TypeTableLayout::kCount) {}

    // Custom reader (e.g. out-of-process) and/or relocated base/count.
    explicit ItemTypeCatalog(MemReader reader,
                             std::uintptr_t table_va = TypeTableLayout::kTableVA,
                             std::uint32_t count = TypeTableLayout::kCount)
        : read_(std::move(reader)), table_va_(table_va), count_(count) {}

    std::uint32_t count() const { return count_; }

    // Read a single record by row index. Returns {valid=false} on OOB/read fail.
    ItemType at(std::uint32_t row) const {
        ItemType out;
        if (row >= count_) return out;
        const std::uintptr_t rec = table_va_ + static_cast<std::uintptr_t>(row) * TypeTableLayout::kStride;

        std::int32_t id = 0;
        if (!read_(rec + TypeTableLayout::kIdOffset, &id, sizeof(id))) return out;

        char buf[TypeTableLayout::kNameMax];
        if (!read_(rec + TypeTableLayout::kNameOffset, buf, sizeof(buf))) return out;
        buf[TypeTableLayout::kNameMax - 1] = '\0'; // guard non-terminated record

        out.id = id;
        out.name.assign(buf, std::strlen(buf));
        out.row = row;
        out.valid = true;
        return out;
    }

    // Walk every record, invoking `fn(const ItemType&)`. Stops early if `fn`
    // returns false. Skips records that fail to read.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::uint32_t r = 0; r < count_; ++r) {
            ItemType t = at(r);
            if (!t.valid) continue;
            if (!fn(static_cast<const ItemType&>(t))) return;
        }
    }

    // Resolve a "TYPE_..." name -> type id, mirroring the engine's FUN_0043cd90
    // (linear scan by name). Returns the engine type id, or `not_found` (default
    // -1) if no record matches. Case-sensitive, exact match (as the engine does).
    std::int32_t item_type_id(const char* name, std::int32_t not_found = -1) const {
        if (!name) return not_found;
        std::int32_t result = not_found;
        for_each([&](const ItemType& t) -> bool {
            if (t.name == name) { result = t.id; return false; } // stop
            return true;
        });
        return result;
    }
    std::int32_t item_type_id(const std::string& name, std::int32_t nf = -1) const {
        return item_type_id(name.c_str(), nf);
    }

    // Reverse: type id -> name (linear; ids are NOT row indices, so a scan is
    // needed unless the caller builds its own map). Returns empty on miss.
    std::string name_of(std::int32_t id) const {
        std::string result;
        for_each([&](const ItemType& t) -> bool {
            if (t.id == id) { result = t.name; return false; }
            return true;
        });
        return result;
    }

private:
    MemReader      read_;
    std::uintptr_t table_va_;
    std::uint32_t  count_;
};

} // namespace items
} // namespace sacred

#endif // SACRED_SDK_PORTS_ITEM_TYPE_CATALOG_H
