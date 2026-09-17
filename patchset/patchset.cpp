// patchset/patchset.cpp — see patchset.h.
//
// ORDER OF OPERATIONS, and why it is what it is:
//
//   1. The build profile must be TRUSTED. Its pins cover every page these
//      records touch, and they are verified immediately before install() runs,
//      so a page that is still SecuROM ciphertext can never be patched. That is
//      the same guarantee dump_text.cpp's four-site gate gives the legacy
//      patches, generalised — and it costs no extra polling.
//   2. Dry pass: every site of every enabled record must match its `expect`
//      bytes exactly, full length. Not a prefix: a 3-byte prologue check proves
//      "decrypted", it does not prove "nobody else already patched byte 4".
//   3. Commit pass: stubs first, then sites in address order, journalling the
//      original bytes. A failure mid-record rolls that record back and moves on
//      to the next one; one bad record never takes the others down.
//
// REVERT is deliberately limited. Rewriting live instruction bytes while another
// thread may be executing them is not atomic, and Sacred is multithreaded. The
// journal exists for the rollback path above and for offline diffing, not for
// toggling features while the game runs.
#include "../sdk.h"
#include "patchset.h"
#include "../engine/build_profile.h"
#include "../core/config.h"
#include "../iat_hook.h"
#include "../hd/geometry.h"
#include <cstring>
#include "../imgui/imgui.h"

namespace sdk { namespace patchset {

// ---------------------------------------------------------------------------
//  Records
// ---------------------------------------------------------------------------
// Two-phase include: the tables first, then the rows inside the initializer.
#define SDK_PATCH_DATA
#include "records_builtin.inc"
#undef SDK_PATCH_DATA

static const Record kBuiltin[] = {
#define SDK_PATCH_ROWS
#include "records_builtin.inc"
#undef SDK_PATCH_ROWS
};
static constexpr int kBuiltinN = (int)(sizeof(kBuiltin) / sizeof(kBuiltin[0]));

// A table generated from a third-party binary is optional and never committed.
// Without it the DLL builds and runs exactly as it does today, minus that feature.
#if defined(__has_include)
#  if __has_include("records_generated.inc")
#    define SDK_HAVE_GENERATED_RECORDS 1
#  endif
#endif
#ifdef SDK_HAVE_GENERATED_RECORDS
#  define SDK_PATCH_DATA
#  include "records_generated.inc"
#  undef SDK_PATCH_DATA
static const Record kGenerated[] = {
#  define SDK_PATCH_ROWS
#  include "records_generated.inc"
#  undef SDK_PATCH_ROWS
};
static constexpr int kGeneratedN = (int)(sizeof(kGenerated) / sizeof(kGenerated[0]));
#else
static const Record* kGenerated = nullptr;
static constexpr int kGeneratedN = 0;
#endif

// ---------------------------------------------------------------------------
//  Per-record state + revert journal (fixed storage; a revert must never fail
//  for lack of memory)
// ---------------------------------------------------------------------------
// Room for the generated HD table (one record per patched function) on top of
// the built-in fixes.
constexpr int MAX_RECORDS = 512;
constexpr int MAX_JOURNAL = 2048;
// A site can be a whole function when ReBorn's rewrite of it is ported as one.
constexpr int MAX_SITE_LEN = 16384;
constexpr int JOURNAL_POOL = 1 << 21;   // saved original bytes of every site, all records

struct RecState {
    const Record* rec;
    St            st;
    char          detail[112];
    int           journal_first, journal_n;
    uintptr_t     cave_lo, cave_hi;   // this record's stubs, for describe_address()
};
struct JournalEntry {
    uintptr_t addr;
    uint16_t  len;
    uint32_t  pool_off;              // into g_journal_pool
};

static RecState     g_rs[MAX_RECORDS];
static int          g_rs_n = 0;
static JournalEntry g_journal[MAX_JOURNAL];
static uint8_t      g_journal_pool[JOURNAL_POOL];
static uint32_t     g_journal_pool_used = 0;
// Scratch for a site's live and new bytes. install() runs once, on one thread.
static uint8_t      g_scratch_live[MAX_SITE_LEN];
static uint8_t      g_scratch_out[MAX_SITE_LEN];
static int          g_journal_n = 0;
static char         g_status[192] = "patchset: not run";
static int          g_applied = 0, g_skipped = 0, g_failed = 0;
static bool         g_ran = false;

const char* status()        { return g_status; }
int         applied_count() { return g_applied; }
int         skipped_count() { return g_skipped; }
int         failed_count()  { return g_failed; }

int record_count() { return g_rs_n; }

bool describe_address(uintptr_t va, char* buf, size_t n) {
    for (int i = 0; i < g_rs_n; ++i) {
        const RecState& rs = g_rs[i];
        if (rs.st != St::Applied) continue;
        if (rs.cave_lo && va >= rs.cave_lo && va < rs.cave_hi) {
            // stubs were allocated in order, each rounded up to 16 bytes
            uintptr_t at = rs.cave_lo;
            for (uint8_t k = 0; k < rs.rec->nstubs; ++k) {
                const Stub& st = rs.rec->stubs[k];
                uintptr_t next = at + ((st.len + 15) & ~(size_t)15);
                if (va < next) {
                    _snprintf_s(buf, n, _TRUNCATE, "%s stub %u +0x%x", rs.rec->key, st.id,
                                (unsigned)(va - at));
                    return true;
                }
                at = next;
            }
        }
        for (int j = rs.journal_first; j < rs.journal_first + rs.journal_n && j < g_journal_n; ++j)
            if (va >= g_journal[j].addr && va < g_journal[j].addr + g_journal[j].len) {
                _snprintf_s(buf, n, _TRUNCATE, "%s site @%08x +0x%x", rs.rec->key,
                            (unsigned)g_journal[j].addr, (unsigned)(va - g_journal[j].addr));
                return true;
            }
    }
    return false;
}

bool record_at(int i, RecordInfo* out) {
    if (!out || i < 0 || i >= g_rs_n) return false;
    const RecState& rs = g_rs[i];
    *out = RecordInfo{ rs.rec->key, rs.rec->name, rs.st, rs.detail };
    return true;
}

// ---------------------------------------------------------------------------
//  RWX cave. One reservation for every stub, instead of one VirtualAlloc per
//  hook. In a 32-bit process every user address is < 2 GB, so an E9 rel32 from
//  engine .text always reaches it — we assert that rather than assume it.
// ---------------------------------------------------------------------------
namespace cave {
    static uint8_t* g_base = nullptr;
    static size_t   g_size = 0, g_used = 0;

    static bool ensure() {
        if (g_base) return true;
        g_size = 0x10000;
        g_base = (uint8_t*)VirtualAlloc(nullptr, g_size, MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
        if (!g_base) {
            sdk_log("[patchset] cave VirtualAlloc failed: %lu", GetLastError());
            return false;
        }
        memset(g_base, 0xCC, g_size);
        sdk_log("[patchset] cave %p (%zu bytes)", g_base, g_size);
        return true;
    }
    static uint8_t* alloc(size_t n) {
        if (!ensure()) return nullptr;
        size_t need = (n + 15) & ~(size_t)15;
        if (g_used + need > g_size) return nullptr;
        uint8_t* p = g_base + g_used;
        g_used += need;
        return p;
    }
    static void rewind_to(size_t mark) { if (mark <= g_used) g_used = mark; }
    size_t used()     { return g_used; }
    size_t capacity() { return g_size; }
}

// ---------------------------------------------------------------------------
//  Leaf SEH helpers. MSVC forbids __try in a function holding objects with
//  destructors, so every guarded memory touch lives in its own tiny function.
// ---------------------------------------------------------------------------
static bool seh_read(const void* src, void* dst, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool seh_write_code(uintptr_t addr, const void* src, size_t n) {
    DWORD old = 0;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    bool ok;
    __try { memcpy((void*)addr, src, n); ok = true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    DWORD dummy = 0;
    VirtualProtect((void*)addr, n, old, &dummy);
    FlushInstructionCache(GetCurrentProcess(), (const void*)addr, n);
    return ok;
}

// ---------------------------------------------------------------------------
//  Fixups
// ---------------------------------------------------------------------------
struct ApplyCtx {
    uintptr_t reb;
    const Record* rec;
    uintptr_t stub_addr[256];   // by stub id
    uintptr_t site_va;          // live VA of the site being emitted (0 for stubs)
    const uint8_t* site_expect; // original bytes of that site
    const uint8_t* site_bytes;  // replacement bytes of that site, before fixups (null for stubs)
};

static bool resolve_fixup(const ApplyCtx& cx, const Fixup& f, uintptr_t write_at,
                          uint32_t* out, char* why, size_t why_n) {
    const bool site_relative = (f.kind == Fix::Rel32SitePlus ||
                                f.kind == Fix::Rel32SiteJcc8 ||
                                f.kind == Fix::Rel32SiteJcc32);
    if (site_relative && (!cx.site_va || !cx.site_expect)) {
        _snprintf_s(why, why_n, _TRUNCATE, "site-relative fixup with no anchor site");
        return false;
    }
    switch (f.kind) {
        case Fix::None:
            *out = 0; return true;

        case Fix::Rel32ToVA: {
            uintptr_t tgt = cx.reb + f.arg;
            *out = (uint32_t)(int32_t)(tgt - (write_at + 4));
            return true;
        }
        case Fix::Rel32ToStub: {
            if (f.arg > 255 || !cx.stub_addr[f.arg]) {
                _snprintf_s(why, why_n, _TRUNCATE, "stub %u unresolved", f.arg);
                return false;
            }
            *out = (uint32_t)(int32_t)(cx.stub_addr[f.arg] - (write_at + 4));
            return true;
        }
        case Fix::Abs32ToStub: {
            if (f.arg > 255 || !cx.stub_addr[f.arg]) {
                _snprintf_s(why, why_n, _TRUNCATE, "stub %u unresolved", f.arg);
                return false;
            }
            *out = (uint32_t)cx.stub_addr[f.arg];
            return true;
        }
        case Fix::Rel32SitePlus: {
            uintptr_t tgt = cx.site_va + f.arg;
            *out = (uint32_t)(int32_t)(tgt - (write_at + 4));
            return true;
        }
        case Fix::Rel32SiteJcc8: {
            int8_t d = (int8_t)cx.site_expect[1];
            uintptr_t tgt = cx.site_va + 2 + d;
            *out = (uint32_t)(int32_t)(tgt - (write_at + 4));
            return true;
        }
        case Fix::Rel32SiteJcc32: {
            int32_t d;
            memcpy(&d, cx.site_expect + 2, 4);
            uintptr_t tgt = cx.site_va + 6 + d;
            *out = (uint32_t)(int32_t)(tgt - (write_at + 4));
            return true;
        }
        case Fix::Abs32ToVA:
            *out = (uint32_t)(cx.reb + f.arg);
            return true;

        case Fix::Abs32Const: {
            // Relocated stubs sometimes read a float that ReBorn kept somewhere we
            // have no equivalent of (its own code tail, even its PE header). One
            // pooled read-only copy per distinct value serves every record.
            static uint32_t pool[64];
            static int      pool_n = 0;
            int k = 0;
            while (k < pool_n && pool[k] != f.arg) ++k;
            if (k == pool_n) {
                if (pool_n == 64) {
                    _snprintf_s(why, why_n, _TRUNCATE, "constant pool full");
                    return false;
                }
                pool[pool_n++] = f.arg;
            }
            *out = (uint32_t)(uintptr_t)&pool[k];
            return true;
        }
        case Fix::Abs32Geom: {
            void* p = hd::slot_addr(f.arg);
            if (!p) {
                _snprintf_s(why, why_n, _TRUNCATE, "geometry slot %08x unavailable", f.arg);
                return false;
            }
            *out = (uint32_t)(uintptr_t)p;
            return true;
        }
        case Fix::Imm32GeomSet:
        case Fix::Imm32GeomAdd:
        case Fix::Imm16GeomAdd:
        case Fix::Imm16GeomSet: {
            uint32_t slot = 0;
            if (!hd::slot_u32(f.arg, &slot)) {
                _snprintf_s(why, why_n, _TRUNCATE, "geometry slot %08x unavailable", f.arg);
                return false;
            }
            if (!cx.site_bytes) {
                _snprintf_s(why, why_n, _TRUNCATE, "geometry operand fixup outside a site");
                return false;
            }
            // The operand the value is added to is the one the site WRITES. For an
            // operand patch that is our own instruction; for a ported block it is
            // the constant ReBorn's rewrite put there.
            const bool w16 = (f.kind == Fix::Imm16GeomAdd || f.kind == Fix::Imm16GeomSet);
            uint32_t orig = 0;
            memcpy(&orig, cx.site_bytes + f.ofs, w16 ? 2 : 4);
            if (f.kind == Fix::Imm32GeomSet) {
                *out = slot;
            } else if (f.kind == Fix::Imm16GeomSet) {
                *out = slot & 0xFFFF;
            } else if (f.kind == Fix::Imm32GeomAdd) {
                *out = (uint32_t)((int32_t)orig + (int32_t)slot);
            } else {
                *out = (uint32_t)(uint16_t)((int16_t)orig + (int32_t)slot);
            }
            // At 1024x768 every layout value equals the constant in the operand,
            // so a mismatch means our formula for that slot is wrong. Refuse
            // rather than write a number nobody has checked.
            if (hd::width() == 1024 && hd::height() == 768 && *out != orig) {
                _snprintf_s(why, why_n, _TRUNCATE,
                            "geometry slot %08x = %08x but the original operand is %08x at 1024x768",
                            f.arg, *out, orig);
                return false;
            }
            return true;
        }
        case Fix::Abs32IatSlot: {
            if (f.arg >= cx.rec->nimp) {
                _snprintf_s(why, why_n, _TRUNCATE, "import index %u out of range", f.arg);
                return false;
            }
            const ImportRef& ir = cx.rec->imports[f.arg];
            void** slot = iat::find_slot(g_attach.exe_module, ir.dll, ir.fn);
            if (!slot) {
                _snprintf_s(why, why_n, _TRUNCATE, "import %s!%s not found", ir.dll, ir.fn);
                return false;
            }
            *out = (uint32_t)(uintptr_t)slot;
            return true;
        }
    }
    _snprintf_s(why, why_n, _TRUNCATE, "unknown fixup kind");
    return false;
}

// ---------------------------------------------------------------------------
//  Gating
// ---------------------------------------------------------------------------
static bool record_enabled(const Record& r) {
    if (!config::get_bool("patches", "enable", true)) return false;
    // The generated HD table is one feature: `[hd] enable` is the default for
    // every record in it, and a single record can still be forced either way by
    // its own key under [patches].
    if (r.group && strcmp(r.group, "hd") == 0)
        return config::get_bool("patches", r.key, config::get_bool("hd", "enable", false));
    return config::get_bool("patches", r.key, r.default_on);
}

// A record's `conflicts` names either another record's key or a legacy patch.
static bool conflict_active(const Record& r, char* why, size_t why_n) {
    if (!r.conflicts) return false;
    if (strcmp(r.conflicts, "legacy_patch6") == 0) {
        if (patches::g_patch6_active) {
            _snprintf_s(why, why_n, _TRUNCATE, "conflicts with legacy patch6 (active)");
            return true;
        }
        return false;
    }
    if (strcmp(r.conflicts, "hooks.multi_instance") == 0) {
        if (config::get_bool("hooks", "multi_instance", false)) {
            _snprintf_s(why, why_n, _TRUNCATE, "conflicts with the CreateMutexA IAT hook");
            return true;
        }
        return false;
    }
    // Another patchset record.
    for (int i = 0; i < g_rs_n; ++i)
        if (strcmp(g_rs[i].rec->key, r.conflicts) == 0 && g_rs[i].st == St::Applied) {
            _snprintf_s(why, why_n, _TRUNCATE, "conflicts with '%s' (applied)", r.conflicts);
            return true;
        }
    return false;
}

// ---------------------------------------------------------------------------
//  Apply
// ---------------------------------------------------------------------------
static void set_state(RecState& rs, St st, const char* fmt, ...) {
    rs.st = st;
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(rs.detail, sizeof(rs.detail), _TRUNCATE, fmt, ap);
    va_end(ap);
}

static bool apply_record(RecState& rs, uintptr_t reb) {
    const Record& r = *rs.rec;
    char why[128] = {0};

    // ---- dry pass: every site must read back exactly what we expect --------
    for (uint8_t i = 0; i < r.nsites; ++i) {
        const Site& s = r.sites[i];
        if (s.len > MAX_SITE_LEN) {
            set_state(rs, St::Skipped, "site %u longer than %d bytes", i, MAX_SITE_LEN);
            return false;
        }
        uint8_t* live = g_scratch_live;
        if (!seh_read((const void*)(reb + s.va), live, s.len)) {
            set_state(rs, St::Skipped, "site %u @%08x unreadable", i, s.va);
            return false;
        }
        if (memcmp(live, s.expect, s.len) != 0) {
            // Already applied? Then say so rather than crying mismatch.
            bool same_as_ours = (memcmp(live, s.bytes, s.len) == 0);
            set_state(rs, St::Skipped, same_as_ours
                          ? "site %u @%08x already patched"
                          : "site %u @%08x bytes differ from expected",
                      i, s.va);
            return false;
        }
    }

    ApplyCtx cx = {};
    cx.reb = reb;
    cx.rec = &r;

    const size_t cave_mark = cave::used();
    const int    journal_mark = g_journal_n;
    rs.journal_first = journal_mark;
    rs.journal_n = 0;

    // ---- commit: stubs first, so sites can point at them -------------------
    for (uint8_t i = 0; i < r.nstubs; ++i) {
        const Stub& st = r.stubs[i];
        uint8_t* dst = cave::alloc(st.len);
        if (!dst) {
            cave::rewind_to(cave_mark);
            set_state(rs, St::Skipped, "cave exhausted (need %u bytes)", st.len);
            return false;
        }
        memcpy(dst, st.bytes, st.len);
        cx.stub_addr[st.id] = (uintptr_t)dst;
    }
    // Second sweep applies stub fixups now that every stub has an address.
    for (uint8_t i = 0; i < r.nstubs; ++i) {
        const Stub& st = r.stubs[i];
        uint8_t* dst = (uint8_t*)cx.stub_addr[st.id];
        // A stub's site-relative fixups resolve against the site it serves. Without
        // this the context still holds no site at all, and a jump-back fixup would
        // dereference a null `site_expect`.
        if (st.anchor_site < r.nsites) {
            cx.site_va     = reb + r.sites[st.anchor_site].va;
            cx.site_expect = r.sites[st.anchor_site].expect;
            cx.site_bytes  = nullptr;
        } else {
            cx.site_va     = 0;
            cx.site_expect = nullptr;
            cx.site_bytes  = nullptr;
        }
        for (uint8_t k = 0; k < st.nfx; ++k) {
            uint32_t v = 0;
            if (!resolve_fixup(cx, st.fx[k], (uintptr_t)dst + st.fx[k].ofs, &v, why, sizeof(why))) {
                cave::rewind_to(cave_mark);
                set_state(rs, St::Skipped, "stub %u: %s", st.id, why);
                return false;
            }
            memcpy(dst + st.fx[k].ofs, &v, 4);
        }
    }
    if (r.nstubs) FlushInstructionCache(GetCurrentProcess(), cave::g_base, cave::used());
    rs.cave_lo = r.nstubs ? (uintptr_t)cave::g_base + cave_mark : 0;
    rs.cave_hi = r.nstubs ? (uintptr_t)cave::g_base + cave::used() : 0;

    // Log every emitted stub byte for byte. The post-patch .text dump cannot see
    // the cave, so this line is what lets a stub be disassembled and checked
    // offline after a live run.
    for (uint8_t i = 0; i < r.nstubs; ++i) {
        const Stub& st = r.stubs[i];
        const uint8_t* p = (const uint8_t*)cx.stub_addr[st.id];
        char hex[3 * 64 + 1] = {0};
        for (uint16_t k = 0; k < st.len && k < 64; ++k)
            _snprintf_s(hex + 3 * k, sizeof(hex) - 3 * k, _TRUNCATE, "%02x ", p[k]);
        sdk_log("[patchset] %s stub %u @%p (%u bytes): %s",
                r.key, st.id, (void*)p, (unsigned)st.len, hex);
    }

    // ---- commit: sites -----------------------------------------------------
    for (uint8_t i = 0; i < r.nsites; ++i) {
        const Site& s = r.sites[i];
        uintptr_t at = reb + s.va;

        uint8_t* out = g_scratch_out;
        memcpy(out, s.bytes, s.len);
        cx.site_va = at;
        cx.site_expect = s.expect;
        cx.site_bytes = s.bytes;
        bool bad = false;
        for (uint8_t k = 0; k < s.nfx; ++k) {
            uint32_t v = 0;
            if (!resolve_fixup(cx, s.fx[k], at + s.fx[k].ofs, &v, why, sizeof(why))) {
                set_state(rs, St::Failed, "site %u: %s", i, why);
                bad = true; break;
            }
            const bool w16 = (s.fx[k].kind == Fix::Imm16GeomAdd || s.fx[k].kind == Fix::Imm16GeomSet);
            memcpy(out + s.fx[k].ofs, &v, w16 ? 2 : 4);
        }
        if (bad) { revert(r.key); cave::rewind_to(cave_mark); return false; }

        if (g_journal_n >= MAX_JOURNAL || g_journal_pool_used + s.len > JOURNAL_POOL) {
            set_state(rs, St::Failed, "journal full");
            revert(r.key); cave::rewind_to(cave_mark); return false;
        }
        JournalEntry& je = g_journal[g_journal_n];
        je.addr = at; je.len = s.len; je.pool_off = g_journal_pool_used;
        if (!seh_read((const void*)at, g_journal_pool + je.pool_off, s.len)) {
            set_state(rs, St::Failed, "site %u unreadable at commit", i);
            revert(r.key); cave::rewind_to(cave_mark); return false;
        }
        ++g_journal_n; ++rs.journal_n;
        g_journal_pool_used += s.len;

        if (!seh_write_code(at, out, s.len)) {
            set_state(rs, St::Failed, "site %u write failed @%08x", i, s.va);
            revert(r.key); cave::rewind_to(cave_mark); return false;
        }
    }

    set_state(rs, St::Applied, "%u site%s%s", r.nsites, r.nsites == 1 ? "" : "s",
              r.nstubs ? ", stub in cave" : "");
    return true;
}

void install() {
    if (g_ran) return;
    g_ran = true;

    if (!config::get_bool("patches", "enable", true)) {
        _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "disabled by sdk.ini [patches] enable=0");
        sdk_log("[patchset] %s", g_status);
        return;
    }
    if (!engine::build::can_patch_text()) {
        _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                    "build not TRUSTED (%s) — no .text patching",
                    engine::build::state_text());
        sdk_log("[patchset] %s", g_status);
        return;
    }

    const uintptr_t reb = engine::build::rebase();

    g_rs_n = 0;
    for (int i = 0; i < kBuiltinN && g_rs_n < MAX_RECORDS; ++i)
        g_rs[g_rs_n++] = RecState{ &kBuiltin[i], St::Off, "", 0, 0 };
    for (int i = 0; i < kGeneratedN && g_rs_n < MAX_RECORDS; ++i)
        g_rs[g_rs_n++] = RecState{ &kGenerated[i], St::Off, "", 0, 0 };

    g_applied = g_skipped = g_failed = 0;
    for (int i = 0; i < g_rs_n; ++i) {
        RecState& rs = g_rs[i];
        const Record& r = *rs.rec;

        if (!record_enabled(r)) {
            set_state(rs, St::Off, "off (sdk.ini)");
            sdk_log("[patchset] %-26s : off", r.key);
            continue;
        }
        char why[128] = {0};
        if (conflict_active(r, why, sizeof(why))) {
            set_state(rs, St::Skipped, "%s", why);
            ++g_skipped;
            sdk_log("[patchset] %-26s : SKIPPED (%s)", r.key, rs.detail);
            continue;
        }
        if (apply_record(rs, reb)) {
            ++g_applied;
            sdk_log("[patchset] %-26s : ACTIVE  (%s)  [%s]", r.key, rs.detail, r.attribution);
        } else if (rs.st == St::Failed) {
            ++g_failed;
            sdk_log("[patchset] %-26s : FAILED  (%s)", r.key, rs.detail);
        } else {
            ++g_skipped;
            sdk_log("[patchset] %-26s : SKIPPED (%s)", r.key, rs.detail);
        }
    }

    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                "%d applied, %d skipped, %d failed; cave %zu/%zu bytes",
                g_applied, g_skipped, g_failed, cave::used(), cave::capacity());
    sdk_log("[patchset] %s", g_status);
}

// ---------------------------------------------------------------------------
//  Revert
// ---------------------------------------------------------------------------
static void revert_slice(int first, int n) {
    for (int i = first + n - 1; i >= first; --i) {
        JournalEntry& je = g_journal[i];
        if (!je.addr) continue;
        seh_write_code(je.addr, g_journal_pool + je.pool_off, je.len);
        je.addr = 0;
    }
}

bool revert(const char* key) {
    for (int i = 0; i < g_rs_n; ++i) {
        if (strcmp(g_rs[i].rec->key, key) != 0) continue;
        revert_slice(g_rs[i].journal_first, g_rs[i].journal_n);
        g_rs[i].journal_n = 0;
        if (g_rs[i].st == St::Applied) { set_state(g_rs[i], St::Off, "reverted"); --g_applied; }
        sdk_log("[patchset] reverted %s", key);
        return true;
    }
    return false;
}

void revert_all() {
    for (int i = g_rs_n - 1; i >= 0; --i)
        if (g_rs[i].journal_n) revert(g_rs[i].rec->key);
    sdk_log("[patchset] revert_all done");
}

// ---------------------------------------------------------------------------
//  Live verification — the cheap in-game half of the regression oracle. The
//  other half is diffing logs/text_dump.bin against logs/text_dump_post.bin.
// ---------------------------------------------------------------------------
VerifyResult verify_live() {
    const uintptr_t reb = engine::build::rebase();
    int as_expect = 0, as_ours = 0, other = 0;
    for (int i = 0; i < g_rs_n; ++i) {
        const Record& r = *g_rs[i].rec;
        for (uint8_t k = 0; k < r.nsites; ++k) {
            const Site& s = r.sites[k];
            static uint8_t live[MAX_SITE_LEN];   // the overlay button / console command, one at a time
            if (!seh_read((const void*)(reb + s.va), live, s.len)) { ++other; continue; }
            if      (memcmp(live, s.expect, s.len) == 0) ++as_expect;
            else if (memcmp(live, s.bytes,  s.len) == 0) ++as_ours;
            else {
                ++other;
                sdk_log("[patchset] verify: %s site @%08x is NEITHER original nor ours",
                        r.key, s.va);
            }
        }
    }
    sdk_log("[patchset] verify: %d original, %d patched, %d unexpected",
            as_expect, as_ours, other);
    return VerifyResult{ as_expect, as_ours, other };
}


// ---------------------------------------------------------------------------
//  Overlay panel
// ---------------------------------------------------------------------------
void draw_panel() {
    if (!ImGui::CollapsingHeader("Engine patches (patchset)", 0)) return;

    ImGui::Text("build: %s  %s", engine::build::state_text(),
                engine::build::profile() ? engine::build::profile()->id : "(no profile)");
    const auto& fp = engine::build::fingerprint();
    if (fp.valid)
        ImGui::Text("ts=%08x img=%08x sum=%08x", fp.timestamp, fp.size_of_image, fp.checksum);
    ImGui::TextWrapped("%s", status());
    ImGui::Separator();

    for (int i = 0; i < g_rs_n; ++i) {
        const Record& r = *g_rs[i].rec;
        ImVec4 col;
        const char* tag;
        switch (g_rs[i].st) {
            case St::Applied: col = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); tag = "ACTIVE ";  break;
            case St::Skipped: col = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); tag = "SKIPPED"; break;
            case St::Failed:  col = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); tag = "FAILED "; break;
            default:          col = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); tag = "off    "; break;
        }
        ImGui::TextColored(col, "%s", tag);
        ImGui::SameLine();
        ImGui::Text("%-26s %s", r.key, g_rs[i].detail);
        if (ImGui::IsItemHovered() && r.attribution)
            ImGui::SetTooltip("%s\n%s", r.name, r.attribution);
    }

    ImGui::Separator();
    if (ImGui::Button("Verify live bytes")) verify_live();
    ImGui::SameLine();
    if (ImGui::Button("Revert all")) revert_all();
    ImGui::SameLine();
    if (ImGui::Button("Re-read sdk.ini"))
        sdk_log("[patchset] sdk.ini re-read: %d keys", config::reload());
    ImGui::TextDisabled("Toggling a patch needs a restart: rewriting live "
                        "instructions under running threads is not atomic.");
}

}} // namespace sdk::patchset
