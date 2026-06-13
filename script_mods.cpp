// SacredSDK — in-game script-mod authoring.
//
// Sister of `sdk/re/py/funkcode_swap.py`: same byte-level swap on an inline-
// ASCII identifier inside a .bin script, but driven from the ImGui overlay so
// the user never has to leave Sacred to author a mod.
//
// Pipeline
// --------
//   1. User picks a target .bin (`bin/TYPE_NPC_<class>/FunkCode.bin` etc.)
//   2. We load the file into a buffer and scan it for printable-ASCII runs
//      that look like Sacred identifiers (length ≥ 6, contain `_`).
//   3. User picks A (string to replace) and B (replacement). UI enforces
//      same-length (Sacred's .bin format references strings by offset, so
//      drift would corrupt the file).
//   4. User clicks Apply. We materialise the swap into a fresh buffer and
//      write it to `<game>/custom/<same-relative-path>`. fs_override picks
//      it up on the next Sacred run.
//
// Swaps are accumulated in a small in-memory list; "Apply" flushes them all
// into a single output file. Clear() resets the pending set without writing.

#include "sdk.h"
#include "funkcode.h"   // Goal B4: native FunkCode (de)compiler — replaces python.exe
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <shellapi.h>

#include "imgui/imgui.h"

#pragma comment(lib, "shell32.lib")

namespace sdk { namespace script_mods {

// --- known target .bin paths (relative to game dir) -----------------------
// Tested set: the 8 player classes × {vanilla bin/, addon bin/Addon/}.
// Sacred ships StartCode/FunkCode/QuestCode/QuestPoolCode in each TYPE_NPC_*
// directory but FunkCode.bin is what holds the dialogue identifiers.
static const char* g_target_paths[] = {
    "bin\\TYPE_NPC_SERAPHIM\\FunkCode.bin",
    "bin\\TYPE_NPC_GLADIATOR\\FunkCode.bin",
    "bin\\TYPE_NPC_MAGICIAN\\FunkCode.bin",
    "bin\\TYPE_NPC_ELVE\\FunkCode.bin",
    "bin\\TYPE_NPC_DARKELVE\\FunkCode.bin",
    "bin\\TYPE_NPC_VAMPIRELADY\\FunkCode.bin",
    "bin\\TYPE_NPC_ZWERG\\FunkCode.bin",
    "bin\\TYPE_NPC_DAEMONIN\\FunkCode.bin",
    "bin\\Addon\\TYPE_NPC_SERAPHIM\\FunkCode.bin",
    "bin\\Addon\\TYPE_NPC_GLADIATOR\\FunkCode.bin",
    "bin\\Addon\\TYPE_NPC_MAGICIAN\\FunkCode.bin",
    "bin\\Addon\\TYPE_NPC_ELVE\\FunkCode.bin",
};
static const int g_num_targets = (int)(sizeof(g_target_paths) / sizeof(g_target_paths[0]));

// --- pool of extracted identifiers ----------------------------------------
//
// Static-sized to avoid CRT heap churn inside the overlay frame loop.
// Sacred's bin files yield ~500-1500 identifiers each; 4096 is enough head-
// room for any single file plus filter results.
struct Ident {
    uint32_t offset;
    uint16_t length;
    char     text[80];      // identifiers we care about are < 64 chars
};
static constexpr int IDENT_CAP = 4096;
static Ident  g_idents[IDENT_CAP];
static int    g_n_idents = 0;
static char   g_loaded_path[MAX_PATH] = "";  // relative path of currently-loaded file
static size_t g_loaded_size = 0;

// File-data buffer for the currently loaded source file. ~4 MB is enough for
// the biggest FunkCode.bin (3.96 MB) plus headroom.
static constexpr size_t FILE_BUF_CAP = 8 * 1024 * 1024;
static uint8_t g_file_buf[FILE_BUF_CAP];

// --- pending swap list ----------------------------------------------------
struct Swap {
    char old_s[80];
    char new_s[80];
};
static constexpr int SWAP_CAP = 32;
static Swap g_swaps[SWAP_CAP];
static int  g_n_swaps = 0;

// --- UI state -------------------------------------------------------------
static int  g_target_idx = 0;      // which entry in g_target_paths is loaded/selected
static int  g_pick_a     = -1;     // index into g_idents (filtered list ignored here)
static int  g_pick_b     = -1;
static char g_filter_a[64] = "";
static char g_filter_b[64] = "";
static char g_status[256] = "(idle — pick a file and click Scan)";

static void set_status(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
    sdk_log("[script_mods] %s", g_status);
}

// --- helpers --------------------------------------------------------------
static void resolve_abs(const char* rel, char out[MAX_PATH]) {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    _snprintf_s(out, MAX_PATH, _TRUNCATE, "%s\\%s", exe, rel);
}

// (Goal B4) run_python_sync() is GONE — the FunkCode .fkasm decompile/compile
// round-trip is now native C++ (funkcode.cpp), gated byte-exact against the
// retail corpus. No more CreateProcessA("python.exe"); zero runtime python dep.

// mkdir -p for the parent directories of `path`.
static void ensure_parent_dirs(const char* path) {
    char dir[MAX_PATH];
    strncpy_s(dir, _TRUNCATE, path, _TRUNCATE);
    for (char* p = dir + 1; *p; p++) {
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryA(dir, nullptr);
            *p = '\\';
        }
    }
}

static bool is_ident_char(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '_' || c == ':';
}

// Scan g_file_buf[0..n) for printable-ASCII runs of length ≥ 6 that look
// like Sacred identifiers (contain an underscore — rules out plain words).
// Dedups by text so we don't show the same string 96 times.
static void scan_identifiers(size_t n) {
    g_n_idents = 0;
    size_t i = 0;
    while (i < n) {
        // Find a run of identifier chars.
        size_t start = i;
        while (i < n && is_ident_char(g_file_buf[i])) i++;
        size_t len = i - start;
        if (len >= 6 && len < sizeof(g_idents[0].text)) {
            bool has_underscore = false;
            for (size_t k = 0; k < len; k++) {
                if (g_file_buf[start + k] == '_') { has_underscore = true; break; }
            }
            if (has_underscore) {
                // Dedup against existing.
                bool dup = false;
                for (int k = 0; k < g_n_idents; k++) {
                    if (g_idents[k].length == (uint16_t)len
                        && memcmp(g_idents[k].text, &g_file_buf[start], len) == 0) {
                        dup = true; break;
                    }
                }
                if (!dup && g_n_idents < IDENT_CAP) {
                    Ident& e = g_idents[g_n_idents++];
                    e.offset = (uint32_t)start;
                    e.length = (uint16_t)len;
                    memcpy(e.text, &g_file_buf[start], len);
                    e.text[len] = 0;
                }
            }
        }
        if (i == start) i++;  // ensure progress on non-ident bytes
    }
}

static bool load_target(int target_idx) {
    if (target_idx < 0 || target_idx >= g_num_targets) return false;
    const char* rel = g_target_paths[target_idx];
    char abs[MAX_PATH];
    resolve_abs(rel, abs);

    FILE* f = nullptr;
    if (fopen_s(&f, abs, "rb") != 0 || !f) {
        set_status("load: cannot open %s (err=%d)", abs, errno);
        g_loaded_path[0] = 0;
        g_loaded_size = 0;
        g_n_idents = 0;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (size_t)sz > FILE_BUF_CAP) {
        set_status("load: size %ld out of range for %s", sz, abs);
        fclose(f);
        return false;
    }
    size_t got = fread(g_file_buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        set_status("load: short read %zu / %ld for %s", got, sz, abs);
        return false;
    }
    g_loaded_size = got;
    strncpy_s(g_loaded_path, _TRUNCATE, rel, _TRUNCATE);
    scan_identifiers(got);
    g_pick_a = g_pick_b = -1;
    set_status("loaded %s (%zu bytes, %d identifiers)", rel, got, g_n_idents);
    return true;
}

static void clear_swaps() {
    g_n_swaps = 0;
    set_status("cleared pending swaps");
}

static void add_swap() {
    if (g_pick_a < 0 || g_pick_b < 0
        || g_pick_a >= g_n_idents || g_pick_b >= g_n_idents) {
        set_status("add: select both A and B first");
        return;
    }
    const Ident& a = g_idents[g_pick_a];
    const Ident& b = g_idents[g_pick_b];
    if (a.length != b.length) {
        set_status("add: length mismatch %u vs %u — swap rejected", a.length, b.length);
        return;
    }
    if (g_n_swaps >= SWAP_CAP) {
        set_status("add: swap list full (%d)", SWAP_CAP);
        return;
    }
    Swap& s = g_swaps[g_n_swaps++];
    strncpy_s(s.old_s, _TRUNCATE, a.text, _TRUNCATE);
    strncpy_s(s.new_s, _TRUNCATE, b.text, _TRUNCATE);
    set_status("added swap: '%s' -> '%s' (%u chars)", a.text, b.text, a.length);
}

// In-place byte replace inside g_file_buf[0..n).
static int apply_swap_inplace(const char* old_s, const char* new_s, size_t n) {
    size_t L = strlen(old_s);
    if (L != strlen(new_s) || L == 0) return 0;
    int count = 0;
    size_t i = 0;
    while (i + L <= n) {
        if (memcmp(&g_file_buf[i], old_s, L) == 0) {
            memcpy(&g_file_buf[i], new_s, L);
            count++;
            i += L;
        } else {
            i++;
        }
    }
    return count;
}

static void apply_to_custom() {
    if (!g_loaded_size) {
        set_status("apply: no file loaded");
        return;
    }
    if (g_n_swaps == 0) {
        set_status("apply: no swaps queued");
        return;
    }

    // Re-load from vanilla so previous apply()'s don't accumulate.
    if (!load_target(g_target_idx)) return;

    int total = 0;
    for (int i = 0; i < g_n_swaps; i++) {
        int c = apply_swap_inplace(g_swaps[i].old_s, g_swaps[i].new_s, g_loaded_size);
        sdk_log("[script_mods]   swap '%s' -> '%s': %d hits",
                g_swaps[i].old_s, g_swaps[i].new_s, c);
        total += c;
    }
    if (total == 0) {
        set_status("apply: 0 hits — vanilla unchanged, not writing");
        return;
    }

    // Build destination path: <game>\custom\<g_loaded_path>
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;

    char dst[MAX_PATH];
    _snprintf_s(dst, _TRUNCATE, "%s\\custom\\%s", exe, g_loaded_path);

    // mkdir -p the parent directories of dst.
    char dir[MAX_PATH];
    strncpy_s(dir, _TRUNCATE, dst, _TRUNCATE);
    for (char* p = dir + 1; *p; p++) {
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryA(dir, nullptr);
            *p = '\\';
        }
    }

    FILE* f = nullptr;
    if (fopen_s(&f, dst, "wb") != 0 || !f) {
        set_status("apply: cannot open %s for write (err=%d)", dst, errno);
        return;
    }
    size_t wrote = fwrite(g_file_buf, 1, g_loaded_size, f);
    fclose(f);
    set_status("APPLIED %d swaps, %d hits, %zu bytes -> %s", g_n_swaps, total, wrote, dst);
}

// --- decompile/compile workflow -------------------------------------------
//
// Resolve the canonical .fkasm path for the current target:
//   <game>\custom\scripts\<rel-without-.bin>.fkasm
// e.g. "bin\TYPE_NPC_SERAPHIM\FunkCode.bin" -> "custom\scripts\bin\TYPE_NPC_SERAPHIM\FunkCode.fkasm"
static void resolve_fkasm_path(int target_idx, char out[MAX_PATH]) {
    const char* rel = g_target_paths[target_idx];
    char rel_no_ext[MAX_PATH];
    strncpy_s(rel_no_ext, _TRUNCATE, rel, _TRUNCATE);
    // Strip ".bin" tail (case-insensitive)
    size_t L = strlen(rel_no_ext);
    if (L >= 4 && _stricmp(rel_no_ext + L - 4, ".bin") == 0) {
        rel_no_ext[L - 4] = 0;
    }
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    _snprintf_s(out, MAX_PATH, _TRUNCATE,
                "%s\\custom\\scripts\\%s.fkasm", exe, rel_no_ext);
}

static void resolve_custom_bin_path(int target_idx, char out[MAX_PATH]) {
    const char* rel = g_target_paths[target_idx];
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    _snprintf_s(out, MAX_PATH, _TRUNCATE, "%s\\custom\\%s", exe, rel);
}

static void action_decompile() {
    char src[MAX_PATH], dst[MAX_PATH];
    resolve_abs(g_target_paths[g_target_idx], src);
    resolve_fkasm_path(g_target_idx, dst);
    ensure_parent_dirs(dst);

    std::string err;
    if (sdk::funkcode::decompile_file(src, dst, err)) {
        set_status("decompile OK: %s", dst);
    } else {
        set_status("decompile failed: %s", err.c_str());
    }
}

static void action_open_in_editor() {
    char fkasm[MAX_PATH];
    resolve_fkasm_path(g_target_idx, fkasm);
    DWORD attr = GetFileAttributesA(fkasm);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        set_status("open: %s does not exist — Decompile first", fkasm);
        return;
    }
    HINSTANCE r = ShellExecuteA(nullptr, "open", fkasm, nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        set_status("open: ShellExecuteA failed (code=%lld)", (long long)(INT_PTR)r);
    } else {
        set_status("open: launched %s in default editor", fkasm);
    }
}

static void action_compile_and_deploy() {
    char fkasm[MAX_PATH], dst[MAX_PATH];
    resolve_fkasm_path(g_target_idx, fkasm);
    DWORD attr = GetFileAttributesA(fkasm);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        set_status("compile: %s does not exist — Decompile first", fkasm);
        return;
    }
    resolve_custom_bin_path(g_target_idx, dst);
    ensure_parent_dirs(dst);

    std::string err;
    if (sdk::funkcode::compile_file(fkasm, dst, err)) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExA(dst, GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER sz; sz.LowPart = fad.nFileSizeLow; sz.HighPart = fad.nFileSizeHigh;
            set_status("compile OK: %s (%llu bytes) — restart Sacred to see effect",
                       dst, sz.QuadPart);
        } else {
            set_status("compile claimed OK but file missing: %s", dst);
        }
    } else {
        set_status("compile failed: %s", err.c_str());
    }
}

// --- ImGui panel ----------------------------------------------------------
//
// Two filterable selectors over the identifier list, an "add" button to push
// into the pending swap set, and apply/clear. Sized to fit in the right side
// of the SacredSDK overlay window.
void draw_panel() {
    if (!ImGui::CollapsingHeader("Script mods (byte-swap)", 0)) return;

    // --- file picker
    ImGui::Text("Target file:");
    if (ImGui::BeginCombo("##target_file", g_target_paths[g_target_idx])) {
        for (int i = 0; i < g_num_targets; i++) {
            bool sel = (i == g_target_idx);
            if (ImGui::Selectable(g_target_paths[i], sel)) g_target_idx = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan")) load_target(g_target_idx);

    // --- decompile/compile workflow row
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
        "Full round-trip via .fkasm (native — no python needed):");
    if (ImGui::Button("Decompile -> .fkasm")) action_decompile();
    ImGui::SameLine();
    if (ImGui::Button("Open in editor"))     action_open_in_editor();
    ImGui::SameLine();
    if (ImGui::Button("Compile & deploy"))   action_compile_and_deploy();
    {
        char fkasm[MAX_PATH];
        resolve_fkasm_path(g_target_idx, fkasm);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "fkasm: %s", fkasm);
    }
    ImGui::Separator();

    if (g_loaded_path[0]) {
        ImGui::Text("Loaded: %s  (%zu bytes, %d identifiers)",
                    g_loaded_path, g_loaded_size, g_n_idents);
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "(no file scanned yet — click Scan)");
        ImGui::Separator();
        ImGui::TextWrapped("status: %s", g_status);
        return;
    }

    ImGui::Separator();

    // --- selector A (string to replace)
    ImGui::Text("Replace [A]:");
    ImGui::InputTextWithHint("##filter_a", "filter…", g_filter_a, sizeof(g_filter_a));
    if (ImGui::BeginListBox("##list_a", ImVec2(-FLT_MIN, 6 * ImGui::GetTextLineHeightWithSpacing()))) {
        for (int i = 0; i < g_n_idents; i++) {
            if (g_filter_a[0] && !strstr(g_idents[i].text, g_filter_a)) continue;
            bool sel = (i == g_pick_a);
            char label[96];
            _snprintf_s(label, _TRUNCATE, "%s##a%d", g_idents[i].text, i);
            if (ImGui::Selectable(label, sel)) g_pick_a = i;
        }
        ImGui::EndListBox();
    }
    if (g_pick_a >= 0)
        ImGui::Text("A = '%s' (%u chars)", g_idents[g_pick_a].text, g_idents[g_pick_a].length);

    // --- selector B (replacement)
    ImGui::Text("With [B] (same length only):");
    ImGui::InputTextWithHint("##filter_b", "filter…", g_filter_b, sizeof(g_filter_b));
    if (ImGui::BeginListBox("##list_b", ImVec2(-FLT_MIN, 6 * ImGui::GetTextLineHeightWithSpacing()))) {
        uint16_t target_len = (g_pick_a >= 0) ? g_idents[g_pick_a].length : 0;
        for (int i = 0; i < g_n_idents; i++) {
            // Enforce same length so we don't ever corrupt the file via UI.
            if (target_len && g_idents[i].length != target_len) continue;
            if (g_filter_b[0] && !strstr(g_idents[i].text, g_filter_b)) continue;
            if (g_pick_a >= 0 && i == g_pick_a) continue;        // can't swap with self
            bool sel = (i == g_pick_b);
            char label[96];
            _snprintf_s(label, _TRUNCATE, "%s##b%d", g_idents[i].text, i);
            if (ImGui::Selectable(label, sel)) g_pick_b = i;
        }
        ImGui::EndListBox();
    }
    if (g_pick_b >= 0)
        ImGui::Text("B = '%s' (%u chars)", g_idents[g_pick_b].text, g_idents[g_pick_b].length);

    ImGui::Separator();

    // --- swap queue
    if (ImGui::Button("Add to swap list")) add_swap();
    ImGui::SameLine();
    ImGui::Text("queued: %d / %d", g_n_swaps, SWAP_CAP);

    if (g_n_swaps > 0) {
        if (ImGui::BeginListBox("##swap_list", ImVec2(-FLT_MIN, 4 * ImGui::GetTextLineHeightWithSpacing()))) {
            for (int i = 0; i < g_n_swaps; i++) {
                ImGui::Text("%s  ->  %s", g_swaps[i].old_s, g_swaps[i].new_s);
            }
            ImGui::EndListBox();
        }
    }

    if (ImGui::Button("Apply (write custom/)")) apply_to_custom();
    ImGui::SameLine();
    if (ImGui::Button("Clear queue")) clear_swaps();

    ImGui::Separator();
    ImGui::TextWrapped("status: %s", g_status);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        "Apply writes to <game>/custom/<same-path>. Restart Sacred to see the effect.");
}

}} // namespace sdk::script_mods
