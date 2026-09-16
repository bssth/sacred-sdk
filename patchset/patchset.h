// patchset/patchset.h — data-driven engine patches.
//
// Why this exists: patches.cpp grew one hand-written function per patch, each
// with its own signature check, its own VirtualProtect dance, a shared
// last-writer-wins status string, no config gate and no way to undo anything.
// That does not scale to the dozens of sites the ReBorn feature port needs.
//
// Here a patch is DATA: a record naming a group of sites, the bytes we expect
// to find, the bytes to write, and a list of fixups for anything that has to be
// computed at apply time (a rel32 to our own stub, the address of an IAT slot,
// a branch target decoded out of the original instruction). The engine does the
// rest: verify, apply atomically per record, journal the original bytes, report.
//
// Attribution: the built-in records are ports of Thorium's (SacredVault)
// published unofficial-patch 2.29/2.30 x86 listings. Records generated from a
// third-party binary live in a separate, gitignored .inc and are never shipped.
#pragma once
#include <cstdint>
#include <cstddef>

namespace sdk { namespace patchset {

// How to compute a 4-byte field inside `bytes` at apply time.
enum class Fix : uint8_t {
    None = 0,
    Rel32ToVA,      // arg = engine VA          -> rel32 from (write_addr + ofs + 4)
    Rel32ToStub,    // arg = stub id            -> rel32 to that stub's cave address
    Rel32SitePlus,  // arg = N                  -> rel32 to (site_va + N)
    Rel32SiteJcc8,  // arg = ignored            -> rel32 to (site_va + 2 + int8 expect[1])
    Rel32SiteJcc32, // arg = ignored            -> rel32 to (site_va + 6 + int32 expect[2..5])
    Abs32IatSlot,   // arg = index into Record::imports -> absolute ADDRESS OF the IAT slot
    Abs32ToStub,    // arg = stub id            -> absolute address of that stub
    // HD geometry (hd/geometry.h). `arg` is the slot's address in ReBorn's own
    // table, which is how the generated records name a layout value.
    Abs32Geom,      // -> address of that slot inside our geometry block
    Imm32GeomSet,   // -> the slot's four bytes (int or float, as the operand is)
    Imm32GeomAdd,   // -> original imm32 at this offset + the slot's int32
    Imm16GeomAdd,   // -> original imm16 at this offset + the slot's int32 (2-byte field)
    // Relocated code that reads engine data or a constant.
    Abs32ToVA,      // arg = engine VA          -> that VA, rebased
    Abs32Const,     // arg = 4 raw bytes        -> address of a read-only copy in our DLL
};

struct Fixup {
    uint16_t ofs;    // byte offset of the field within `bytes` (4 bytes, 2 for Imm16GeomAdd)
    Fix      kind;
    uint32_t arg;
};

struct ImportRef { const char* dll; const char* fn; };

// A stub emitted into our own RWX cave. Referenced from sites by `id`.
// `anchor_site` is the index into Record::sites of the site this stub serves.
// Site-relative fixups inside the stub (Rel32SitePlus / Rel32SiteJcc8 /
// Rel32SiteJcc32) resolve against that site's address and original bytes, which
// is what lets a stub jump back into the engine without a hardcoded second VA.
struct Stub {
    uint8_t        id;
    uint16_t       len;
    const uint8_t* bytes;
    const Fixup*   fx;
    uint8_t        nfx;
    uint8_t        anchor_site;
};

// One contiguous run of bytes we overwrite in engine .text.
struct Site {
    uint32_t       va;       // full VA, pre-rebase
    uint16_t       len;      // == length of expect == length of bytes
    const uint8_t* expect;   // full-length; must match live memory exactly
    const uint8_t* bytes;    // replacement, before fixups
    const Fixup*   fx;
    uint8_t        nfx;
};

struct Record {
    const char* key;           // sdk.ini key under [patches]
    const char* name;          // human name for logs/overlay
    const char* group;         // feature group ("fixes", "hd", …)
    const char* attribution;   // who figured this out
    const char* conflicts;     // key of a record/legacy patch this cannot coexist with
    bool        default_on;
    const ImportRef* imports;  uint8_t nimp;
    const Stub*      stubs;    uint8_t nstubs;
    const Site*      sites;    uint8_t nsites;
};

enum class St : uint8_t { Off, Applied, Skipped, Failed };

// Apply every enabled record. Safe to call once, after .text is decrypted and
// the build profile is TRUSTED. Refuses outright otherwise.
void install();

// Restore the original bytes of one record / everything. Only meaningful before
// the engine has executed the patched code; see the header note in patchset.cpp.
bool revert(const char* key);
void revert_all();

// Re-read live bytes at every site and classify them. For the overlay button
// and the `sdk verify` console command.
struct VerifyResult { int original, patched, unexpected; };
VerifyResult verify_live();

void        draw_panel();
const char* status();
int         applied_count();
int         skipped_count();
int         failed_count();

// Read-only view of the per-record state, for surfaces other than the overlay.
// Valid after install(); `detail` is empty for a record that is simply off.
struct RecordInfo { const char* key; const char* name; St st; const char* detail; };
int  record_count();
bool record_at(int i, RecordInfo* out);

}} // namespace sdk::patchset
