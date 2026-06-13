// SacredSDK — Lua-driven script baker.
//
// Embeds Lua 5.4 inside the DLL. On attach we spawn a worker that scans
// `<game>/custom/lua/**.lua`, executes each file through Lua, expects it to
// return a list-of-records table, assembles the bytes into a FunkCode .bin,
// and writes the result to `<game>/custom/<mirrored-path>.bin` where
// fs_override picks it up at the next file open.
//
// .lua file shape
// ---------------
// Each .lua mod returns a list. Each record is `{ tag, flags, op1, op2, ... }`.
// Each op is `{ "LABEL", arg1, arg2, ... }`. Arg conventions per opcode kind:
//
//   stack/halt/const-w0 : no args
//   const w=1..16       : integer args (1, 1, 1, 2, 3, 4) of the obvious widths;
//                         for w=3 (FMT3) the single arg is a 3-byte Lua string.
//   cstr1               : one string
//   cstr2               : two strings
//   cstr1+1 / cstr1+5   : string + 1-or-5-byte Lua string (the trailer)
//   u32+cstr1           : integer + string
//   u32+cstr2           : integer + two strings
//
// Strings are Lua byte-strings; we treat them as latin1. NUL is forbidden
// inside a cstring (the bytecode is null-terminated).

#include "sdk.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
}

namespace sdk { namespace lua_bake {

volatile long g_baked_files = 0;
volatile long g_baked_records = 0;
volatile bool g_busy = false;
static char g_last_status[512] = "(not run yet)";
const char* status() { return g_last_status; }
long baked_files() { return g_baked_files; }
long baked_records() { return g_baked_records; }
bool busy() { return g_busy; }

static void set_status(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_last_status, sizeof(g_last_status), fmt, ap);
    va_end(ap);
    sdk_log("[lua_bake] %s", g_last_status);
}

// --- opcode table (port of funkcode_ops.LABEL_TO_OP) ----------------------
enum Kind : uint8_t {
    KIND_UNKNOWN = 0,
    KIND_STACK,        // 1 byte (op)
    KIND_HALT,         // 1 byte (op)
    KIND_CONST,        // 1 + width bytes
    KIND_CSTR1,        // 1 + null-terminated string
    KIND_CSTR2,        // 1 + two null-terminated strings
    KIND_CSTR1_1,      // 1 + null-terminated string + 1 raw byte
    KIND_CSTR1_5,      // 1 + null-terminated string + 5 raw bytes
    KIND_U32_CSTR1,    // 1 + 4-byte u32 + null-terminated string
    KIND_U32_CSTR2,    // 1 + 4-byte u32 + two null-terminated strings
};

struct OpInfo {
    const char* label;
    uint8_t     opcode;
    Kind        kind;
    uint8_t     width;
};

// Opcode table — the single source of truth shared with funkcode.cpp.
// (Historically generated; now maintained by hand in lua_bake_opcodes.inc,
// which is checked in. Keep it in sync with the FunkCode disassembler.)
static const OpInfo OP_TABLE[] = {
#include "lua_bake_opcodes.inc"
};
static const int OP_TABLE_N = sizeof(OP_TABLE) / sizeof(OP_TABLE[0]);

static std::unordered_map<std::string, const OpInfo*> g_label_map;
static void ensure_label_map() {
    if (!g_label_map.empty()) return;
    g_label_map.reserve(OP_TABLE_N * 2);
    for (int i = 0; i < OP_TABLE_N; i++) {
        g_label_map[OP_TABLE[i].label] = &OP_TABLE[i];
    }
}

// --- encoder helpers ------------------------------------------------------
static void put_u32_le(std::string& out, uint32_t v) {
    out.push_back((char)(v & 0xff));
    out.push_back((char)((v >> 8) & 0xff));
    out.push_back((char)((v >> 16) & 0xff));
    out.push_back((char)((v >> 24) & 0xff));
}

// Read one Lua arg from the stack slot `idx`. Coerces:
//   - integer / number -> int64_t
//   - string -> bytes (sets `is_str=true`)
static bool read_arg_str(lua_State* L, int idx, std::string& out, bool& is_str) {
    int t = lua_type(L, idx);
    if (t == LUA_TSTRING) {
        size_t n = 0;
        const char* p = lua_tolstring(L, idx, &n);
        out.assign(p, n);
        is_str = true;
        return true;
    }
    if (t == LUA_TNUMBER) {
        is_str = false;
        return true;
    }
    return false;
}

// Assemble one opcode to bytes. `args_start_idx` is the absolute stack index
// of the first arg (i.e. op-table[2] etc.) and `n_args` is how many.
static bool assemble_op_from_stack(lua_State* L, const OpInfo* info,
                                    int op_table_idx, int n_args_total,
                                    std::string& out, char err[256])
{
    out.push_back((char)info->opcode);
    int n_args = n_args_total - 1;  // first is the label

    auto getarg = [&](int k) {
        // op_table_idx is the Lua-stack absolute index of the op table; arg k
        // (1-based across the op contents starting at the LABEL=1) lives at
        // op_table[1 + k].  We push and let caller pop.
        lua_rawgeti(L, op_table_idx, 1 + k);
    };

    switch (info->kind) {
    case KIND_STACK:
    case KIND_HALT:
        if (n_args != 0) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: expected 0 args, got %d",
                        info->label, n_args);
            return false;
        }
        return true;
    case KIND_CONST: {
        int w = info->width;
        if (w == 0) {
            if (n_args != 0) {
                _snprintf_s(err, 256, _TRUNCATE, "%s: expected 0 args, got %d",
                            info->label, n_args);
                return false;
            }
            return true;
        }
        if (w == 3) {
            // single 3-byte string arg
            if (n_args != 1) {
                _snprintf_s(err, 256, _TRUNCATE, "%s: needs 1 byte-string arg of length 3",
                            info->label);
                return false;
            }
            getarg(1);
            size_t n = 0;
            const char* p = lua_tolstring(L, -1, &n);
            if (!p || n != 3) {
                _snprintf_s(err, 256, _TRUNCATE, "%s: 3-byte string arg required (got %zu)",
                            info->label, n);
                lua_pop(L, 1);
                return false;
            }
            out.append(p, 3);
            lua_pop(L, 1);
            return true;
        }
        // numeric widths: 1, 2, 4, 8, 12, 16
        int expect = (w == 1 || w == 2 || w == 4) ? 1
                   : (w == 8) ? 2
                   : (w == 12) ? 3
                   : (w == 16) ? 4 : 0;
        if (expect == 0) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: unhandled width %d", info->label, w);
            return false;
        }
        if (n_args != expect) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: needs %d numeric args, got %d",
                        info->label, expect, n_args);
            return false;
        }
        if (w == 1) {
            getarg(1);
            uint8_t v = (uint8_t)(lua_tointeger(L, -1) & 0xff);
            lua_pop(L, 1);
            out.push_back((char)v);
        } else if (w == 2) {
            getarg(1);
            uint16_t v = (uint16_t)(lua_tointeger(L, -1) & 0xffff);
            lua_pop(L, 1);
            out.push_back((char)(v & 0xff));
            out.push_back((char)((v >> 8) & 0xff));
        } else {
            // multi-u32: emit `expect` u32s LE
            for (int k = 1; k <= expect; k++) {
                getarg(k);
                uint32_t v = (uint32_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
                put_u32_le(out, v);
            }
        }
        return true;
    }
    case KIND_CSTR1:
    case KIND_CSTR2: {
        int expect = (info->kind == KIND_CSTR1) ? 1 : 2;
        if (n_args != expect) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: needs %d string arg(s), got %d",
                        info->label, expect, n_args);
            return false;
        }
        for (int k = 1; k <= expect; k++) {
            getarg(k);
            size_t n = 0;
            const char* p = lua_tolstring(L, -1, &n);
            if (!p) {
                _snprintf_s(err, 256, _TRUNCATE, "%s: arg %d must be a string", info->label, k);
                lua_pop(L, 1);
                return false;
            }
            out.append(p, n);
            out.push_back('\0');
            lua_pop(L, 1);
        }
        return true;
    }
    case KIND_CSTR1_1:
    case KIND_CSTR1_5: {
        int tail_len = (info->kind == KIND_CSTR1_1) ? 1 : 5;
        if (n_args != 2) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: needs (string, tail-string)", info->label);
            return false;
        }
        getarg(1);
        size_t n = 0;
        const char* p = lua_tolstring(L, -1, &n);
        if (!p) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: arg 1 must be a string", info->label);
            lua_pop(L, 1);
            return false;
        }
        out.append(p, n);
        out.push_back('\0');
        lua_pop(L, 1);
        getarg(2);
        n = 0;
        p = lua_tolstring(L, -1, &n);
        if (!p || (int)n != tail_len) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: tail must be %d-byte string (got %zu)",
                        info->label, tail_len, n);
            lua_pop(L, 1);
            return false;
        }
        out.append(p, tail_len);
        lua_pop(L, 1);
        return true;
    }
    case KIND_U32_CSTR1:
    case KIND_U32_CSTR2: {
        int expect_strs = (info->kind == KIND_U32_CSTR1) ? 1 : 2;
        if (n_args != 1 + expect_strs) {
            _snprintf_s(err, 256, _TRUNCATE, "%s: needs u32 + %d string(s)",
                        info->label, expect_strs);
            return false;
        }
        getarg(1);
        uint32_t v = (uint32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
        put_u32_le(out, v);
        for (int k = 0; k < expect_strs; k++) {
            getarg(2 + k);
            size_t n = 0;
            const char* p = lua_tolstring(L, -1, &n);
            if (!p) {
                _snprintf_s(err, 256, _TRUNCATE, "%s: string arg required", info->label);
                lua_pop(L, 1);
                return false;
            }
            out.append(p, n);
            out.push_back('\0');
            lua_pop(L, 1);
        }
        return true;
    }
    default:
        _snprintf_s(err, 256, _TRUNCATE, "%s: unknown kind %d", info->label, info->kind);
        return false;
    }
}

// --- main bake: take the table on top of stack, produce .bin bytes --------
static bool table_to_bytes(lua_State* L, std::string& out, char err[256]) {
    if (lua_type(L, -1) != LUA_TTABLE) {
        _snprintf_s(err, 256, _TRUNCATE, "script must return a table (got %s)",
                    luaL_typename(L, -1));
        return false;
    }
    int n_records = (int)lua_rawlen(L, -1);
    int rec_count = 0;
    for (int i = 1; i <= n_records; i++) {
        lua_rawgeti(L, -1, i);  // push record
        int rec_idx = lua_gettop(L);
        if (lua_type(L, -1) != LUA_TTABLE) {
            _snprintf_s(err, 256, _TRUNCATE,
                        "record %d not a table (got %s)", i, luaL_typename(L, -1));
            lua_pop(L, 1);
            return false;
        }
        int rec_len = (int)lua_rawlen(L, -1);
        if (rec_len < 2) {
            _snprintf_s(err, 256, _TRUNCATE, "record %d too short (need tag+flags)", i);
            lua_pop(L, 1);
            return false;
        }
        lua_rawgeti(L, rec_idx, 1);
        uint8_t tag = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, rec_idx, 2);
        uint8_t flags = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        std::string payload;
        payload.push_back((char)flags);

        // Each remaining element is an op table.
        for (int j = 3; j <= rec_len; j++) {
            lua_rawgeti(L, rec_idx, j);
            int op_idx = lua_gettop(L);
            if (lua_type(L, -1) != LUA_TTABLE) {
                _snprintf_s(err, 256, _TRUNCATE,
                            "record %d op %d not a table", i, j - 2);
                lua_pop(L, 1); lua_pop(L, 1);
                return false;
            }
            int op_total = (int)lua_rawlen(L, -1);
            if (op_total < 1) {
                _snprintf_s(err, 256, _TRUNCATE,
                            "record %d op %d empty (need label)", i, j - 2);
                lua_pop(L, 1); lua_pop(L, 1);
                return false;
            }
            lua_rawgeti(L, op_idx, 1);
            const char* label = lua_tostring(L, -1);
            if (!label) {
                _snprintf_s(err, 256, _TRUNCATE,
                            "record %d op %d label not a string", i, j - 2);
                lua_pop(L, 1); lua_pop(L, 1); lua_pop(L, 1);
                return false;
            }
            // `_HEX` pseudo-op: copy raw hex-encoded bytes verbatim. Used by
            // the Python decompiler for opcodes that can't be cleanly mnemonized
            // (so round-trip stays byte-perfect). Lua mods can also write
            // `{"_HEX", "deadbeef"}` to inject literal bytes.
            if (strcmp(label, "_HEX") == 0) {
                lua_pop(L, 1);  // pop label
                if (op_total != 2) {
                    _snprintf_s(err, 256, _TRUNCATE,
                                "record %d op %d: _HEX needs exactly 1 hex string arg",
                                i, j - 2);
                    lua_pop(L, 1); lua_pop(L, 1);
                    return false;
                }
                lua_rawgeti(L, op_idx, 2);
                size_t n = 0;
                const char* hp = lua_tolstring(L, -1, &n);
                if (!hp || (n & 1)) {
                    _snprintf_s(err, 256, _TRUNCATE,
                                "record %d op %d: _HEX arg must be even-length hex string",
                                i, j - 2);
                    lua_pop(L, 1); lua_pop(L, 1); lua_pop(L, 1);
                    return false;
                }
                for (size_t k = 0; k < n; k += 2) {
                    auto hd = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int hi = hd(hp[k]);
                    int lo = hd(hp[k + 1]);
                    if (hi < 0 || lo < 0) {
                        _snprintf_s(err, 256, _TRUNCATE,
                                    "record %d op %d: bad hex byte at %zu", i, j - 2, k);
                        lua_pop(L, 1); lua_pop(L, 1); lua_pop(L, 1);
                        return false;
                    }
                    payload.push_back((char)((hi << 4) | lo));
                }
                lua_pop(L, 1);  // pop hex string
                lua_pop(L, 1);  // pop op table
                continue;
            }

            auto it = g_label_map.find(label);
            if (it == g_label_map.end()) {
                _snprintf_s(err, 256, _TRUNCATE,
                            "record %d op %d: unknown opcode label '%s'",
                            i, j - 2, label);
                lua_pop(L, 1); lua_pop(L, 1); lua_pop(L, 1);
                return false;
            }
            lua_pop(L, 1);  // pop label string

            char op_err[256];
            if (!assemble_op_from_stack(L, it->second, op_idx, op_total, payload, op_err)) {
                _snprintf_s(err, 256, _TRUNCATE,
                            "record %d op %d: %s", i, j - 2, op_err);
                lua_pop(L, 1); lua_pop(L, 1);
                return false;
            }
            lua_pop(L, 1);  // pop op table
        }

        // Emit record header: tag, size (big-endian), payload.
        int size = 3 + (int)payload.size();
        if (size > 0xFFFF) {
            _snprintf_s(err, 256, _TRUNCATE,
                        "record %d payload too large (%zu bytes)", i, payload.size());
            lua_pop(L, 1);
            return false;
        }
        out.push_back((char)tag);
        out.push_back((char)((size >> 8) & 0xff));
        out.push_back((char)(size & 0xff));
        out.append(payload);

        lua_pop(L, 1);  // pop record
        rec_count++;
    }
    InterlockedExchangeAdd(&g_baked_records, rec_count);
    return true;
}

// --- file walker ----------------------------------------------------------
// Resolve `<game>/custom/lua/` and `<game>/custom/`.
static void resolve_dirs(char lua_dir[MAX_PATH], char custom_dir[MAX_PATH]) {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    _snprintf_s(lua_dir,    MAX_PATH, _TRUNCATE, "%s\\custom\\lua",   exe);
    _snprintf_s(custom_dir, MAX_PATH, _TRUNCATE, "%s\\custom",        exe);
}

static void mkdirs(const char* path) {
    char dir[MAX_PATH];
    strncpy_s(dir, _TRUNCATE, path, _TRUNCATE);
    for (char* p = dir + 1; *p; p++) {
        if (*p == '\\') { *p = 0; CreateDirectoryA(dir, nullptr); *p = '\\'; }
    }
}

// Lua-side `sacred.log("msg")` -> appends to sdk_log + the overlay ring.
static int l_sacred_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    sdk_log("[lua] %s", msg);
    return 0;
}
// Lua-side `sacred.read_file(rel)` -> reads bytes of `<game>/<rel>`, returns a
// Lua string of bytes. Used by lib/vanilla.lua to ingest raw .bin files.
static int l_sacred_read_file(lua_State* L) {
    const char* rel = luaL_checkstring(L, 1);
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    char abs[MAX_PATH];
    _snprintf_s(abs, _TRUNCATE, "%s\\%s", exe, rel);
    FILE* f = nullptr;
    if (fopen_s(&f, abs, "rb") != 0 || !f) {
        lua_pushnil(L);
        lua_pushfstring(L, "open failed: %s (errno=%d)", abs, errno);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf;
    buf.resize((size_t)n);
    fread(&buf[0], 1, (size_t)n, f);
    fclose(f);
    lua_pushlstring(L, buf.data(), buf.size());
    return 1;
}

// Lua-side `sacred.write_file(rel, bytes)` -> writes bytes to `<game>/<rel>`.
// Restricted to the `custom/` tree so a buggy mod can never overwrite vanilla.
// Used by lib/text.lua to patch `custom/scripts/<lang>/global.res`.
static int l_sacred_write_file(lua_State* L) {
    const char* rel = luaL_checkstring(L, 1);
    size_t n = 0;
    const char* data = luaL_checklstring(L, 2, &n);
    if (strstr(rel, "..")) {
        return luaL_error(L, "sacred.write_file: path traversal denied: %s", rel);
    }
    if (strncmp(rel, "custom\\", 7) != 0 && strncmp(rel, "custom/", 7) != 0) {
        return luaL_error(L,
            "sacred.write_file: restricted to custom/ tree, got: %s", rel);
    }
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    char abs[MAX_PATH];
    _snprintf_s(abs, _TRUNCATE, "%s\\%s", exe, rel);
    mkdirs(abs);
    FILE* f = nullptr;
    if (fopen_s(&f, abs, "wb") != 0 || !f) {
        return luaL_error(L, "sacred.write_file: open failed: %s (errno=%d)",
                          abs, errno);
    }
    size_t wrote = fwrite(data, 1, n, f);
    fclose(f);
    if (wrote != n) {
        return luaL_error(L, "sacred.write_file: short write %zu/%zu", wrote, n);
    }
    sdk_log("[lua_bake] sacred.write_file '%s' (%zu bytes)", rel, n);
    lua_pushinteger(L, (lua_Integer)n);
    return 1;
}

// Register the `sacred` table on the global env of a fresh state. Anything
// users need from C lives here.
static void register_sacred_api(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_sacred_log);        lua_setfield(L, -2, "log");
    lua_pushcfunction(L, l_sacred_read_file);  lua_setfield(L, -2, "read_file");
    lua_pushcfunction(L, l_sacred_write_file); lua_setfield(L, -2, "write_file");
    lua_setglobal(L, "sacred");

    // Extend the `sacred` table with runtime-trigger entries
    // (sacred.on_trigger / sacred.clear_triggers). Defined in
    // sdk/runtime_triggers.cpp. Mods can call these during bake; the
    // closures persist into the runtime state we hand off below.
    runtime_triggers::install_lua_api(L);
}

// Override package.path / package.cpath so `require("vanilla")` etc. find
// our libs in custom/lua/lib/ and the rest of the user's mod tree.
static void configure_package_path(lua_State* L) {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;

    lua_getglobal(L, "package");
    // path: lib/?.lua first (our blessed helpers), then ?.lua under custom/lua/.
    lua_pushfstring(L,
        "%s\\custom\\lua\\lib\\?.lua;"
        "%s\\custom\\lua\\lib\\?\\init.lua;"
        "%s\\custom\\lua\\?.lua;"
        "%s\\custom\\lua\\?\\init.lua",
        exe, exe, exe, exe);
    lua_setfield(L, -2, "path");
    // Disable cpath entirely — we don't want users loading random DLLs into
    // Sacred's process from the script tree.
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);
}

// Bake one .lua file using a SHARED lua_State. The shared state means that
// `require`d modules (lib/text, lib/state, …) accumulate state across the
// whole bake — that's how lib/text.lua can collect inline-T() strings from
// every mod and write a single combined custom/scripts/<lang>/global.res at
// the end. Each mod's `return {records}` table is consumed and dropped
// before the next mod runs, so per-mod failures stay isolated.
static bool bake_one_file_using(lua_State* L,
                                const char* lua_path,
                                const char* out_bin_path)
{
    int top_before = lua_gettop(L);
    int r = luaL_loadfile(L, lua_path);
    if (r != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        set_status("%s: load error: %s", lua_path, msg ? msg : "?");
        lua_settop(L, top_before);
        return false;
    }
    r = lua_pcall(L, 0, 1, 0);
    if (r != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        set_status("%s: lua error: %s", lua_path, msg ? msg : "?");
        lua_settop(L, top_before);
        return false;
    }
    if (lua_type(L, -1) != LUA_TTABLE) {
        set_status("%s: script must return a records table", lua_path);
        lua_settop(L, top_before);
        return false;
    }
    std::string bytes;
    bytes.reserve(1 << 20);
    char err[256];
    if (!table_to_bytes(L, bytes, err)) {
        set_status("%s: assemble failed: %s", lua_path, err);
        lua_settop(L, top_before);
        return false;
    }
    lua_settop(L, top_before);  // drop the returned table

    mkdirs(out_bin_path);
    FILE* f = nullptr;
    if (fopen_s(&f, out_bin_path, "wb") != 0 || !f) {
        set_status("%s: cannot open output (err=%d)", out_bin_path, errno);
        return false;
    }
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    sdk_log("[lua_bake] baked '%s' -> '%s' (%zu bytes)", lua_path, out_bin_path, bytes.size());
    InterlockedIncrement(&g_baked_files);
    return true;
}

// Back-compat shim used by callers we haven't updated yet. Creates a private
// state, runs the bake, tears down. Avoid for the auto-bake worker — it uses
// a single shared state via `bake_one_file_using`.
static bool bake_one_file(const char* lua_path, const char* out_bin_path) {
    lua_State* L = luaL_newstate();
    if (!L) {
        set_status("luaL_newstate returned NULL");
        return false;
    }
    luaL_openlibs(L);
    register_sacred_api(L);
    configure_package_path(L);
    bool ok = bake_one_file_using(L, lua_path, out_bin_path);
    lua_close(L);
    return ok;
}

// Walk `<lua_dir>` recursively. For each `<lua_dir>/<rel>.lua` produce
// `<custom_dir>/<rel>.bin`. Skips files that fail to bake (logged).
// Uses the shared lua_State `L` so module-level state (e.g. lib/text.lua's
// inline-string registry) accumulates across all baked mods.
static int walk_and_bake(lua_State* L,
                          const char* lua_dir, const char* custom_dir,
                          const char* sub_prefix)
{
    // sub_prefix accumulates the relative path under lua/. Example final
    // mapping: lua/bin/TYPE_NPC_SERAPHIM/FunkCode.lua -> custom/bin/TYPE_NPC_SERAPHIM/FunkCode.bin
    char glob[MAX_PATH];
    if (sub_prefix && sub_prefix[0]) {
        _snprintf_s(glob, _TRUNCATE, "%s\\%s\\*", lua_dir, sub_prefix);
    } else {
        _snprintf_s(glob, _TRUNCATE, "%s\\*", lua_dir);
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int baked = 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        // Skip lib/ (helper modules loaded via `require`) and anything
        // starting with `_` (vanilla snapshots, internal storage). These
        // are NOT mods — they're support data.
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (_stricmp(fd.cFileName, "lib") == 0) continue;
            // examples/ are illustrative docs, NOT mods: executing them
            // registered duplicate on_tick spawns over the real mod
            // ("three Captains" bug). Copy patterns into bin/ to use them.
            if (_stricmp(fd.cFileName, "examples") == 0) continue;
            if (fd.cFileName[0] == '_') continue;
        }
        char full_lua[MAX_PATH];
        if (sub_prefix && sub_prefix[0]) {
            _snprintf_s(full_lua, _TRUNCATE, "%s\\%s\\%s", lua_dir, sub_prefix, fd.cFileName);
        } else {
            _snprintf_s(full_lua, _TRUNCATE, "%s\\%s", lua_dir, fd.cFileName);
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char child_prefix[MAX_PATH];
            if (sub_prefix && sub_prefix[0]) {
                _snprintf_s(child_prefix, _TRUNCATE, "%s\\%s", sub_prefix, fd.cFileName);
            } else {
                _snprintf_s(child_prefix, _TRUNCATE, "%s", fd.cFileName);
            }
            baked += walk_and_bake(L, lua_dir, custom_dir, child_prefix);
            continue;
        }
        // Must end in ".lua"
        size_t fname_len = strlen(fd.cFileName);
        if (fname_len < 4 ||
            _stricmp(fd.cFileName + fname_len - 4, ".lua") != 0) continue;
        // Mirror to <custom_dir>/<sub_prefix>/<name>.bin
        char rel_no_ext[MAX_PATH];
        if (sub_prefix && sub_prefix[0]) {
            _snprintf_s(rel_no_ext, _TRUNCATE, "%s\\", sub_prefix);
        } else {
            rel_no_ext[0] = 0;
        }
        size_t cur = strlen(rel_no_ext);
        size_t name_len = fname_len - 4;
        if (cur + name_len + 5 >= MAX_PATH) continue;
        memcpy(rel_no_ext + cur, fd.cFileName, name_len);
        rel_no_ext[cur + name_len] = 0;

        char out_bin[MAX_PATH];
        _snprintf_s(out_bin, _TRUNCATE, "%s\\%s.bin", custom_dir, rel_no_ext);
        if (bake_one_file_using(L, full_lua, out_bin)) baked++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return baked;
}

// After all .lua mods have run, give finalize hooks a chance to act. We poke
// `package.loaded["text"].flush` — if any mod required `text` (for inline T()
// strings), this is where they get written into custom/scripts/<lang>/global.res.
// Other modules can plug in by adding themselves to this finalize list (see
// FINALIZE_MODULES below).
static const char* const FINALIZE_MODULES[] = { "text", nullptr };

static void run_finalize_hooks(lua_State* L) {
    for (int i = 0; FINALIZE_MODULES[i]; i++) {
        const char* modname = FINALIZE_MODULES[i];
        int top = lua_gettop(L);
        lua_getglobal(L, "package");
        if (!lua_istable(L, -1)) { lua_settop(L, top); continue; }
        lua_getfield(L, -1, "loaded");
        if (!lua_istable(L, -1)) { lua_settop(L, top); continue; }
        lua_getfield(L, -1, modname);
        if (!lua_istable(L, -1)) { lua_settop(L, top); continue; }
        lua_getfield(L, -1, "flush");
        if (!lua_isfunction(L, -1)) { lua_settop(L, top); continue; }
        int r = lua_pcall(L, 0, 0, 0);
        if (r != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            set_status("finalize '%s.flush' error: %s", modname, msg ? msg : "?");
        } else {
            sdk_log("[lua_bake] finalize: %s.flush() ran clean", modname);
        }
        lua_settop(L, top);
    }
}

void bake_all() {
    if (g_busy) return;
    g_busy = true;
    ensure_label_map();

    char lua_dir[MAX_PATH], custom_dir[MAX_PATH];
    resolve_dirs(lua_dir, custom_dir);

    DWORD attrs = GetFileAttributesA(lua_dir);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        set_status("no custom/lua directory — skipping bake");
        g_busy = false;
        return;
    }
    g_baked_files = 0;
    g_baked_records = 0;

    // One shared state for the whole bake pass — required so module-level
    // accumulators (lib/text.lua's inline-string registry) survive across
    // files. Per-mod isolation is preserved by clearing the Lua stack between
    // dofile calls; module *table mutations* persist by design.
    lua_State* L = luaL_newstate();
    if (!L) {
        set_status("luaL_newstate returned NULL");
        g_busy = false;
        return;
    }
    luaL_openlibs(L);
    register_sacred_api(L);
    configure_package_path(L);

    int n = walk_and_bake(L, lua_dir, custom_dir, "");

    // Run finalize hooks (text.flush, …) regardless of bake count so a
    // standalone string-only mod still gets its global.res written.
    run_finalize_hooks(L);

    // Hand the state to runtime_triggers — it owns it for the rest of the
    // process lifetime. Lua closures registered via sacred.on_trigger() stay
    // alive in the registry; the trampoline (installed by take_state if any
    // handlers exist) fires them when Sacred dispatches matching triggers.
    runtime_triggers::take_state(L);
    // DO NOT lua_close(L) — runtime owns it now.

    if (n == 0) {
        set_status("custom/lua/ has no .lua mods to bake");
    } else {
        set_status("baked %d Lua mod%s (%ld records total)",
                   n, n == 1 ? "" : "s", g_baked_records);
    }
    g_busy = false;
}

// --- worker thread (runs once, just after attach) -------------------------
static DWORD WINAPI worker_main(LPVOID) {
    bake_all();
    return 0;
}

void start_auto_bake() {
    HANDLE h = CreateThread(nullptr, 0, worker_main, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

}} // namespace sdk::lua_bake
