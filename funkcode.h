// funkcode.h — native C++ FunkCode (de)compiler. (Goal B1/B2)
//
// Replaces the runtime python.exe dependency in script_mods.cpp (the only one
// in the SDK): `funkcode_compile.py` + `funkcode_decompile.py` + their
// funkcode_ops/disasm/tags deps are ported here. The opcode table is shared
// with the Lua baker via lua_bake_opcodes.inc (single source of truth).
//
// Contract (byte-exact, verified by the 132-retail-.bin roundtrip oracle):
//     compile(decompile(B)) == B    for every retail FunkCode.bin
//
// .fkasm text format (line-oriented; '#'/blank = comment):
//     REC tt              start a record; tt = 1-byte tag in hex
//     FLAGS bb            one flags byte (first payload byte)
//     OP <LABEL> args...  mnemonic opcode (see lua_bake_opcodes.inc)
//     HEX b0 b1 ...       raw payload bytes (fallback when no mnemonic fits)
//     TAIL b0 b1 ...      literal bytes outside record framing
// Record size is auto-computed: 3 + len(payload), u16 big-endian.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sdk { namespace funkcode {

// bin -> .fkasm text. Prefers mnemonic OP lines (verified by re-assembly to
// match the original bytes); falls back to HEX rows where a record can't be
// cleanly mnemonic-decoded, so the result always round-trips byte-exact.
std::string decompile(const uint8_t* buf, size_t n);

// .fkasm text -> bin. Returns false + err on a malformed line. `out` is the
// assembled byte stream.
bool compile(const std::string& text, std::vector<uint8_t>& out, std::string& err);

// Convenience: read src .bin, write dst .fkasm. Returns false + err on I/O or
// (for the self-test) a roundtrip mismatch.
bool decompile_file(const char* src_bin, const char* dst_fkasm, std::string& err);
bool compile_file(const char* src_fkasm, const char* dst_bin, std::string& err);

// Self-test: for buf, assert compile(decompile(buf)) == buf. Returns true on
// byte-exact roundtrip; on mismatch fills err with the first differing offset.
bool roundtrip_ok(const uint8_t* buf, size_t n, std::string& err);

}} // namespace sdk::funkcode
