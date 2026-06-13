// funkcode.cpp — native FunkCode (de)compiler. (Goal B1/B2)
//
// Faithful C++ port of sdk/re/py/funkcode_{compile,decompile,ops,disasm}.py.
// The opcode table is the SAME data the Lua baker uses (lua_bake_opcodes.inc),
// so compile() emits bytes identical to lua_bake's assembler. Verified by the
// roundtrip oracle: compile(decompile(B)) == B for all retail FunkCode.bin.
#include "funkcode.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>

namespace sdk { namespace funkcode {

// --- opcode table (shared with lua_bake via the .inc) ----------------------
enum Kind : uint8_t {
    KIND_UNKNOWN = 0,
    KIND_STACK, KIND_HALT, KIND_CONST,
    KIND_CSTR1, KIND_CSTR2, KIND_CSTR1_1, KIND_CSTR1_5,
    KIND_U32_CSTR1, KIND_U32_CSTR2,
};
struct OpInfo { const char* label; uint8_t opcode; Kind kind; uint8_t width; };
static const OpInfo OP_TABLE[] = {
#include "lua_bake_opcodes.inc"
};
static const int OP_TABLE_N = (int)(sizeof(OP_TABLE) / sizeof(OP_TABLE[0]));

static const OpInfo* g_by_op[256];
static std::unordered_map<std::string, const OpInfo*> g_by_label;
static bool g_tables_ready = false;
static void ensure_tables() {
    if (g_tables_ready) return;
    for (int i = 0; i < 256; i++) g_by_op[i] = nullptr;
    g_by_label.reserve(OP_TABLE_N * 2);
    for (int i = 0; i < OP_TABLE_N; i++) {
        g_by_op[OP_TABLE[i].opcode] = &OP_TABLE[i];     // last winner (matches lua_bake)
        g_by_label[OP_TABLE[i].label] = &OP_TABLE[i];
    }
    g_tables_ready = true;
}

// --- structured operand ----------------------------------------------------
struct Arg {
    enum T { INT, STR, RAW, TAIL } t;
    uint32_t i = 0;       // INT
    std::string s;        // STR (bytes), RAW (fixed-3), TAIL (fixed-N) bytes
};
struct Op { const OpInfo* info = nullptr; std::vector<Arg> args; };

// --- low-level helpers ------------------------------------------------------
static uint32_t rd_le(const uint8_t* p, int w) {
    uint32_t v = 0; for (int k = 0; k < w; k++) v |= (uint32_t)p[k] << (8 * k); return v;
}
static void wr_le(std::string& out, uint32_t v, int w) {
    for (int k = 0; k < w; k++) out.push_back((char)((v >> (8 * k)) & 0xFF));
}
// read a NUL-terminated cstring from buf[cur..n); returns false if no NUL.
static bool rd_cstr(const uint8_t* buf, size_t n, size_t cur, std::string& out, size_t& next) {
    size_t e = cur;
    while (e < n && buf[e] != 0) e++;
    if (e >= n) return false;            // no terminator
    out.assign((const char*)buf + cur, e - cur);
    next = e + 1;
    return true;
}

// --- decode one opcode at body[ip] -> Op + next_ip (mirror disasm_structured)
static bool decode_op(const uint8_t* p, size_t n, size_t ip, Op& op, size_t& next) {
    if (ip >= n) return false;
    const OpInfo* info = g_by_op[p[ip]];
    if (!info) return false;
    op.info = info; op.args.clear();
    const int w = info->width;
    switch (info->kind) {
    case KIND_STACK:
    case KIND_HALT:
        next = ip + 1; return true;
    case KIND_CONST: {
        if (w == 0) { next = ip + 1; return true; }
        if (ip + 1 + w > n) return false;
        const uint8_t* q = p + ip + 1;
        if (w == 1 || w == 2 || w == 4) {
            Arg a; a.t = Arg::INT; a.i = rd_le(q, w); op.args.push_back(a);
        } else if (w == 8 || w == 12 || w == 16) {
            for (int k = 0; k < w; k += 4) { Arg a; a.t = Arg::INT; a.i = rd_le(q + k, 4); op.args.push_back(a); }
        } else if (w == 3) {
            Arg a; a.t = Arg::RAW; a.s.assign((const char*)q, 3); op.args.push_back(a);
        } else return false;             // unhandled width
        next = ip + 1 + w; return true;
    }
    case KIND_CSTR1:
    case KIND_CSTR2: {
        int nstr = (info->kind == KIND_CSTR1) ? 1 : 2;
        size_t cur = ip + 1;
        for (int k = 0; k < nstr; k++) {
            std::string s; size_t nx;
            if (!rd_cstr(p, n, cur, s, nx)) return false;
            Arg a; a.t = Arg::STR; a.s = s; op.args.push_back(a); cur = nx;
        }
        next = cur; return true;
    }
    case KIND_CSTR1_1:
    case KIND_CSTR1_5: {
        int tail = (info->kind == KIND_CSTR1_1) ? 1 : 5;
        size_t cur = ip + 1; std::string s; size_t nx;
        if (!rd_cstr(p, n, cur, s, nx)) return false;
        if (nx + tail > n) return false;
        Arg a; a.t = Arg::STR; a.s = s; op.args.push_back(a);
        Arg b; b.t = Arg::TAIL; b.s.assign((const char*)p + nx, tail); op.args.push_back(b);
        next = nx + tail; return true;
    }
    case KIND_U32_CSTR1:
    case KIND_U32_CSTR2: {
        if (ip + 1 + 4 > n) return false;
        Arg u; u.t = Arg::INT; u.i = rd_le(p + ip + 1, 4); op.args.push_back(u);
        int nstr = (info->kind == KIND_U32_CSTR1) ? 1 : 2;
        size_t cur = ip + 5;
        for (int k = 0; k < nstr; k++) {
            std::string s; size_t nx;
            if (!rd_cstr(p, n, cur, s, nx)) return false;
            Arg a; a.t = Arg::STR; a.s = s; op.args.push_back(a); cur = nx;
        }
        next = cur; return true;
    }
    default: return false;
    }
}

// --- assemble one Op -> append bytes (mirror funkcode_ops.assemble_op) ------
static bool assemble_op(const Op& op, std::string& out, std::string& err) {
    const OpInfo* info = op.info;
    out.push_back((char)info->opcode);
    const int w = info->width;
    switch (info->kind) {
    case KIND_STACK:
    case KIND_HALT:
        return true;
    case KIND_CONST: {
        if (w == 0) return true;
        if (w == 1 || w == 2 || w == 4) {
            if (op.args.size() != 1) { err = "const: need 1 int"; return false; }
            wr_le(out, op.args[0].i, w); return true;
        }
        if (w == 8 || w == 12 || w == 16) {
            int need = w / 4;
            if ((int)op.args.size() != need) { err = "const: int count"; return false; }
            for (auto& a : op.args) wr_le(out, a.i, 4); return true;
        }
        if (w == 3) {
            if (op.args.size() != 1 || op.args[0].t != Arg::RAW || op.args[0].s.size() != 3) { err = "const3: raw(3)"; return false; }
            out.append(op.args[0].s); return true;
        }
        err = "const: bad width"; return false;
    }
    case KIND_CSTR1:
    case KIND_CSTR2: {
        size_t need = (info->kind == KIND_CSTR1) ? 1 : 2;
        if (op.args.size() != need) { err = "cstr: str count"; return false; }
        for (auto& a : op.args) { out.append(a.s); out.push_back('\0'); }
        return true;
    }
    case KIND_CSTR1_1:
    case KIND_CSTR1_5: {
        size_t tail = (info->kind == KIND_CSTR1_1) ? 1 : 5;
        if (op.args.size() != 2 || op.args[1].s.size() != tail) { err = "cstr+tail: shape"; return false; }
        out.append(op.args[0].s); out.push_back('\0');
        out.append(op.args[1].s);
        return true;
    }
    case KIND_U32_CSTR1:
    case KIND_U32_CSTR2: {
        size_t need = (info->kind == KIND_U32_CSTR1) ? 2 : 3;
        if (op.args.size() != need) { err = "u32+cstr: shape"; return false; }
        wr_le(out, op.args[0].i, 4);
        for (size_t k = 1; k < op.args.size(); k++) { out.append(op.args[k].s); out.push_back('\0'); }
        return true;
    }
    default: err = "assemble: unknown kind"; return false;
    }
}

// --- text quoting (mirror funkcode_ops.quote_str / parse_quoted) -----------
static void append_hex_byte(std::string& out, uint8_t b) {
    char buf[5]; _snprintf_s(buf, _TRUNCATE, "0x%02x", b); out += buf;
}
static std::string quote_str(const std::string& s) {
    std::string out = "\"";
    for (unsigned char b : s) {
        if (b == '"') out += "\\\"";
        else if (b == '\\') out += "\\\\";
        else if (b == '\n') out += "\\n";
        else if (b == '\r') out += "\\r";
        else if (b == '\t') out += "\\t";
        else if (b >= 0x20 && b < 0x7F) out += (char)b;
        else { char t[5]; _snprintf_s(t, _TRUNCATE, "\\x%02x", b); out += t; }
    }
    out += "\"";
    return out;
}
static int hexnib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// parse a quoted "..." (or '...') starting at s[i]; returns bytes + next index.
static bool parse_quoted(const std::string& s, size_t i, std::string& out, size_t& next) {
    if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return false;
    char q = s[i++]; out.clear();
    while (i < s.size()) {
        char c = s[i];
        if (c == q) { next = i + 1; return true; }
        if (c == '\\') {
            if (++i >= s.size()) return false;
            char e = s[i];
            if (e == 'n') out.push_back('\n');
            else if (e == 'r') out.push_back('\r');
            else if (e == 't') out.push_back('\t');
            else if (e == '\\') out.push_back('\\');
            else if (e == '"' || e == '\'') out.push_back(e);
            else if (e == 'x') {
                if (i + 2 >= s.size()) return false;
                int hi = hexnib(s[i + 1]), lo = hexnib(s[i + 2]);
                if (hi < 0 || lo < 0) return false;
                out.push_back((char)((hi << 4) | lo)); i += 2;
            } else return false;
            i++;
        } else { out.push_back(c); i++; }
    }
    return false; // unterminated
}

// --- format an Op as the args portion of an "OP <label> ..." line ----------
static std::string format_op_args(const Op& op) {
    std::string r;
    const OpInfo* info = op.info;
    auto sp = [&]() { if (!r.empty()) r += " "; };
    switch (info->kind) {
    case KIND_STACK: case KIND_HALT: break;
    case KIND_CONST:
        if (info->width == 3) { // raw 3 bytes
            const std::string& raw = op.args[0].s;
            for (size_t k = 0; k < raw.size(); k++) { sp(); append_hex_byte(r, (uint8_t)raw[k]); }
        } else {
            for (auto& a : op.args) { sp(); char b[16]; _snprintf_s(b, _TRUNCATE, "%u", a.i); r += b; }
        }
        break;
    case KIND_CSTR1: case KIND_CSTR2:
        for (auto& a : op.args) { sp(); r += quote_str(a.s); }
        break;
    case KIND_CSTR1_1: case KIND_CSTR1_5: {
        r += quote_str(op.args[0].s);
        const std::string& tail = op.args[1].s;
        for (size_t k = 0; k < tail.size(); k++) { r += " "; append_hex_byte(r, (uint8_t)tail[k]); }
        break;
    }
    case KIND_U32_CSTR1: case KIND_U32_CSTR2: {
        char b[16]; _snprintf_s(b, _TRUNCATE, "%u", op.args[0].i); r += b;
        for (size_t k = 1; k < op.args.size(); k++) { r += " " + quote_str(op.args[k].s); }
        break;
    }
    default: break;
    }
    return r;
}

// --- tokenize the args portion of an OP line (quotes count as one token) ----
struct Tok { bool is_str; std::string v; };
static bool tokenize_args(const std::string& s, std::vector<Tok>& toks) {
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= s.size()) break;
        if (s[i] == '"' || s[i] == '\'') {
            std::string b; size_t nx;
            if (!parse_quoted(s, i, b, nx)) return false;
            toks.push_back({ true, b }); i = nx;
        } else {
            size_t k = i;
            while (k < s.size() && s[k] != ' ' && s[k] != '\t') k++;
            toks.push_back({ false, s.substr(i, k - i) }); i = k;
        }
    }
    return true;
}
static bool parse_int_tok(const std::string& v, uint32_t& out) {
    if (v.size() > 1 && v[0] == '-') { out = (uint32_t)strtoll(v.c_str(), nullptr, 10); return true; }
    int base = (v.size() > 1 && (v[1] == 'x' || v[1] == 'X')) ? 16 : 10;
    char* end = nullptr; unsigned long long val = strtoull(v.c_str(), &end, base);
    if (end == v.c_str()) return false; out = (uint32_t)val; return true;
}
static bool parse_hexbyte_tok(const std::string& v, uint8_t& out) {
    uint32_t x; if (!parse_int_tok(v, x)) {
        char* end = nullptr; x = (uint32_t)strtoul(v.c_str(), &end, 16); if (end == v.c_str()) return false;
    }
    out = (uint8_t)(x & 0xFF); return true;
}

// --- parse "OP <label> args..." (label_and_args excludes the "OP ") --------
static bool parse_op_line(const std::string& label_and_args, Op& op, std::string& err) {
    size_t i = 0;
    while (i < label_and_args.size() && (label_and_args[i] == ' ' || label_and_args[i] == '\t')) i++;
    size_t j = i;
    while (j < label_and_args.size() && label_and_args[j] != ' ' && label_and_args[j] != '\t') j++;
    std::string label = label_and_args.substr(i, j - i);
    auto it = g_by_label.find(label);
    if (it == g_by_label.end()) { err = "unknown opcode label '" + label + "'"; return false; }
    op.info = it->second; op.args.clear();
    std::vector<Tok> toks;
    if (!tokenize_args(label_and_args.substr(j), toks)) { err = "bad token in OP " + label; return false; }
    const OpInfo* info = op.info; const int w = info->width;
    auto need_str = [&](size_t k, std::string& out) -> bool {
        if (k >= toks.size() || !toks[k].is_str) return false; out = toks[k].v; return true; };
    switch (info->kind) {
    case KIND_STACK: case KIND_HALT:
        if (!toks.empty()) { err = label + ": takes no args"; return false; }
        return true;
    case KIND_CONST: {
        if (w == 0) return true;
        if (w == 3) {
            if (toks.size() != 3) { err = label + ": needs 3 raw bytes"; return false; }
            Arg a; a.t = Arg::RAW; a.s.resize(3);
            for (int k = 0; k < 3; k++) { uint8_t b; if (!parse_hexbyte_tok(toks[k].v, b)) { err = "bad hex"; return false; } a.s[k] = (char)b; }
            op.args.push_back(a); return true;
        }
        int n = (w == 1 || w == 2 || w == 4) ? 1 : (w == 8 ? 2 : (w == 12 ? 3 : (w == 16 ? 4 : -1)));
        if (n < 0) { err = label + ": bad width"; return false; }
        if ((int)toks.size() != n) { err = label + ": int count"; return false; }
        for (auto& t : toks) { Arg a; a.t = Arg::INT; if (!parse_int_tok(t.v, a.i)) { err = "bad int"; return false; } op.args.push_back(a); }
        return true;
    }
    case KIND_CSTR1: case KIND_CSTR2: {
        size_t n = (info->kind == KIND_CSTR1) ? 1 : 2;
        if (toks.size() != n) { err = label + ": str count"; return false; }
        for (size_t k = 0; k < n; k++) { std::string s; if (!need_str(k, s)) { err = label + ": want string"; return false; } Arg a; a.t = Arg::STR; a.s = s; op.args.push_back(a); }
        return true;
    }
    case KIND_CSTR1_1: case KIND_CSTR1_5: {
        size_t tail = (info->kind == KIND_CSTR1_1) ? 1 : 5;
        if (toks.size() != 1 + tail) { err = label + ": str+tail count"; return false; }
        std::string s; if (!need_str(0, s)) { err = label + ": want string"; return false; }
        Arg a; a.t = Arg::STR; a.s = s; op.args.push_back(a);
        Arg b; b.t = Arg::TAIL; b.s.resize(tail);
        for (size_t k = 0; k < tail; k++) { uint8_t by; if (!parse_hexbyte_tok(toks[1 + k].v, by)) { err = "bad tail"; return false; } b.s[k] = (char)by; }
        op.args.push_back(b); return true;
    }
    case KIND_U32_CSTR1: case KIND_U32_CSTR2: {
        size_t nstr = (info->kind == KIND_U32_CSTR1) ? 1 : 2;
        if (toks.size() != 1 + nstr) { err = label + ": u32+str count"; return false; }
        Arg u; u.t = Arg::INT; if (!parse_int_tok(toks[0].v, u.i)) { err = "bad u32"; return false; } op.args.push_back(u);
        for (size_t k = 0; k < nstr; k++) { std::string s; if (!need_str(1 + k, s)) { err = label + ": want string"; return false; } Arg a; a.t = Arg::STR; a.s = s; op.args.push_back(a); }
        return true;
    }
    default: err = "parse: unknown kind"; return false;
    }
}

// --- decompile a record's body (after flags) into OP lines, or fail --------
// Returns true + lines if EVERY opcode decodes AND re-assembles byte-exact.
static bool try_mnemonic(const uint8_t* payload, size_t plen, std::vector<std::string>& lines) {
    if (plen == 0) return false;
    const uint8_t* body = payload + 1; size_t blen = plen - 1;
    std::vector<Op> ops; size_t ip = 0;
    while (ip < blen) {
        Op op; size_t nx;
        if (!decode_op(body, blen, ip, op, nx) || nx <= ip) return false;
        ops.push_back(op); ip = nx;
    }
    // verify exact byte-level roundtrip of the decode->assemble
    std::string rebuilt, err;
    for (auto& op : ops) if (!assemble_op(op, rebuilt, err)) return false;
    if (rebuilt.size() != blen || memcmp(rebuilt.data(), body, blen) != 0) return false;
    char fl[24]; _snprintf_s(fl, _TRUNCATE, "FLAGS 0x%02x", payload[0]); lines.push_back(fl);
    for (auto& op : ops) lines.push_back(std::string("OP ") + op.info->label + (op.args.empty() ? "" : " " + format_op_args(op)));
    return true;
}

static void hex_rows(const uint8_t* d, size_t n, const char* kw, std::vector<std::string>& out) {
    for (size_t i = 0; i < n; i += 32) {
        std::string row = kw; row += " ";
        size_t end = (i + 32 < n) ? i + 32 : n;
        for (size_t k = i; k < end; k++) { char b[4]; _snprintf_s(b, _TRUNCATE, "%02x", d[k]); if (k > i) row += " "; row += b; }
        out.push_back(row);
    }
}

std::string decompile(const uint8_t* buf, size_t n) {
    ensure_tables();
    std::vector<std::string> lines;
    lines.push_back("# fkasm v2 — Sacred FunkCode disassembly (native)");
    char hdr[64]; _snprintf_s(hdr, _TRUNCATE, "# input bytes: %zu", n); lines.push_back(hdr);
    lines.push_back("");
    size_t off = 0;
    while (off + 3 <= n) {
        uint8_t tag = buf[off];
        uint32_t size = (buf[off + 1] << 8) | buf[off + 2];
        if (size < 3 || off + size > n) break;          // -> tail
        const uint8_t* payload = buf + off + 3; size_t plen = size - 3;
        char rc[96]; _snprintf_s(rc, _TRUNCATE, "# RECORD off=0x%08zx tag=0x%02x size=%u payload=%zuB", off, tag, size, plen);
        lines.push_back(rc);
        char rl[16]; _snprintf_s(rl, _TRUNCATE, "REC %02x", tag); lines.push_back(rl);
        std::vector<std::string> mnem;
        if (try_mnemonic(payload, plen, mnem)) { for (auto& l : mnem) lines.push_back(l); }
        else hex_rows(payload, plen, "HEX", lines);
        lines.push_back("");
        off += size;
    }
    if (off < n) {                                       // trailing bytes -> TAIL
        char tl[64]; _snprintf_s(tl, _TRUNCATE, "# TAIL off=0x%08zx size=%zu", off, n - off); lines.push_back(tl);
        hex_rows(buf + off, n - off, "TAIL", lines);
        lines.push_back("");
    }
    std::string out;
    for (auto& l : lines) { out += l; out += "\n"; }
    return out;
}

// --- compile .fkasm text -> bytes (mirror funkcode_compile.py) -------------
static bool parse_hex_payload(const std::string& rest, std::string& out, std::string& err) {
    size_t i = 0;
    while (i < rest.size()) {
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) i++;
        if (i >= rest.size()) break;
        size_t k = i; while (k < rest.size() && rest[k] != ' ' && rest[k] != '\t') k++;
        std::string tok = rest.substr(i, k - i); i = k;
        if (tok.size() > 2 && (tok[1] == 'x' || tok[1] == 'X')) tok = tok.substr(2);
        if (tok.size() != 2) { err = "bad hex token '" + tok + "'"; return false; }
        int hi = hexnib(tok[0]), lo = hexnib(tok[1]);
        if (hi < 0 || lo < 0) { err = "bad hex token '" + tok + "'"; return false; }
        out.push_back((char)((hi << 4) | lo));
    }
    return true;
}

bool compile(const std::string& text, std::vector<uint8_t>& out, std::string& err) {
    ensure_tables();
    std::string blob;
    bool have_rec = false; uint8_t cur_tag = 0; std::string cur_payload;
    auto flush = [&]() {
        if (!have_rec) return;
        size_t size = 3 + cur_payload.size();
        blob.push_back((char)cur_tag);
        blob.push_back((char)((size >> 8) & 0xFF));
        blob.push_back((char)(size & 0xFF));
        blob.append(cur_payload);
    };
    size_t pos = 0; int lineno = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string raw = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        size_t adv = (nl == std::string::npos) ? text.size() + 1 : nl + 1; pos = adv; lineno++;
        // strip a '#' comment that appears before the first quote
        size_t cut = raw.find('#');
        size_t fq = std::string::npos;
        { size_t a = raw.find('"'), b = raw.find('\''); fq = (a < b) ? a : b; }
        std::string line;
        if (cut != std::string::npos && (fq == std::string::npos || cut < fq)) line = raw.substr(0, cut);
        else line = raw;
        // rstrip
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        // skip blank
        size_t s0 = 0; while (s0 < line.size() && (line[s0] == ' ' || line[s0] == '\t')) s0++;
        if (s0 >= line.size()) { if (nl == std::string::npos) break; else continue; }
        std::string body = line.substr(s0);
        // keyword
        size_t kw_end = 0; while (kw_end < body.size() && body[kw_end] != ' ' && body[kw_end] != '\t') kw_end++;
        std::string kw = body.substr(0, kw_end);
        std::string rest;
        { size_t r = kw_end; while (r < body.size() && (body[r] == ' ' || body[r] == '\t')) r++; rest = body.substr(r); }
        if (kw == "REC") {
            flush();
            uint32_t t; if (rest.empty() || !parse_int_tok(rest.substr(0, rest.find(' ')), t) ) {
                // tag is hex without 0x; parse as base-16
                char* e = nullptr; t = (uint32_t)strtoul(rest.c_str(), &e, 16);
                if (e == rest.c_str()) { err = "line " + std::to_string(lineno) + ": bad REC tag"; return false; }
            } else { char* e=nullptr; t=(uint32_t)strtoul(rest.c_str(), &e, 16); }
            cur_tag = (uint8_t)t; cur_payload.clear(); have_rec = true;
        } else if (kw == "FLAGS") {
            if (!have_rec) { err = "line " + std::to_string(lineno) + ": FLAGS outside REC"; return false; }
            char* e = nullptr; uint32_t v = (uint32_t)strtoul(rest.c_str(), &e, 0);
            if (e == rest.c_str()) { err = "line " + std::to_string(lineno) + ": bad FLAGS"; return false; }
            cur_payload.push_back((char)(v & 0xFF));
        } else if (kw == "OP") {
            if (!have_rec) { err = "line " + std::to_string(lineno) + ": OP outside REC"; return false; }
            Op op; std::string oerr;
            if (!parse_op_line(rest, op, oerr)) { err = "line " + std::to_string(lineno) + ": " + oerr; return false; }
            if (!assemble_op(op, cur_payload, oerr)) { err = "line " + std::to_string(lineno) + ": " + oerr; return false; }
        } else if (kw == "HEX") {
            if (!have_rec) { err = "line " + std::to_string(lineno) + ": HEX outside REC"; return false; }
            std::string herr; if (!parse_hex_payload(rest, cur_payload, herr)) { err = "line " + std::to_string(lineno) + ": " + herr; return false; }
        } else if (kw == "TAIL") {
            flush(); have_rec = false;
            std::string herr; if (!parse_hex_payload(rest, blob, herr)) { err = "line " + std::to_string(lineno) + ": " + herr; return false; }
        } else {
            err = "line " + std::to_string(lineno) + ": malformed: " + raw; return false;
        }
        if (nl == std::string::npos) break;
    }
    flush();
    out.assign(blob.begin(), blob.end());
    return true;
}

// --- file + self-test helpers ----------------------------------------------
static bool read_file(const char* path, std::vector<uint8_t>& out) {
    FILE* f = nullptr; if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    if (n > 0) { size_t rd = fread(out.data(), 1, (size_t)n, f); out.resize(rd); }
    fclose(f); return true;
}
static bool write_file(const char* path, const void* data, size_t n) {
    FILE* f = nullptr; if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    size_t wr = (n ? fwrite(data, 1, n, f) : 0); fclose(f); return wr == n;
}

bool roundtrip_ok(const uint8_t* buf, size_t n, std::string& err) {
    std::string text = decompile(buf, n);
    std::vector<uint8_t> back; std::string cerr;
    if (!compile(text, back, cerr)) { err = "recompile failed: " + cerr; return false; }
    if (back.size() != n) { char b[96]; _snprintf_s(b, _TRUNCATE, "size %zu != %zu", back.size(), n); err = b; return false; }
    for (size_t i = 0; i < n; i++) if (back[i] != buf[i]) { char b[96]; _snprintf_s(b, _TRUNCATE, "byte mismatch @%zu (%02x != %02x)", i, back[i], buf[i]); err = b; return false; }
    return true;
}

bool decompile_file(const char* src_bin, const char* dst_fkasm, std::string& err) {
    std::vector<uint8_t> buf;
    if (!read_file(src_bin, buf)) { err = "cannot read src .bin"; return false; }
    std::string text = decompile(buf.data(), buf.size());
    // self-check the roundtrip so we never write an unrecompilable .fkasm
    std::string rerr;
    if (!roundtrip_ok(buf.data(), buf.size(), rerr)) { err = "roundtrip failed: " + rerr; return false; }
    if (!write_file(dst_fkasm, text.data(), text.size())) { err = "cannot write .fkasm"; return false; }
    return true;
}

bool compile_file(const char* src_fkasm, const char* dst_bin, std::string& err) {
    std::vector<uint8_t> text;
    if (!read_file(src_fkasm, text)) { err = "cannot read .fkasm"; return false; }
    std::string s((const char*)text.data(), text.size());
    std::vector<uint8_t> out; std::string cerr;
    if (!compile(s, out, cerr)) { err = cerr; return false; }
    if (!write_file(dst_bin, out.data(), out.size())) { err = "cannot write .bin"; return false; }
    return true;
}

}} // namespace sdk::funkcode
