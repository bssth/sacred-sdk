// sacred_hash.h — Sacred Gold resource-name hash (pure function port).
//
// SOURCE:
//   - sdk/.claude/knowledge/re_backlog.md S4/S5 + "C++-port candidates" #1
//   - sdk/.claude/knowledge/re/globalres_format.md §1 ("ident = sacred_hash(name) & 0x7fffffff")
//   - Engine fn FUN_0080e780 (id->string->hash path) — same hash as the
//     direct-lookup path FUN_0080eaf0 / global.res resolver FUN_0080f5e0.
//   - Verified Lua port: custom/lua/lib/text.lua sacred_hash() (matches
//     sdk/tools/sacred_hash.py self-test + all 823 ids in hash_names.csv).
//
// ALGORITHM (verified byte-exact via the 23123-slot global.res rebuild):
//   MUL = 0x71 (113), MOD = 0x3b9ac9f7 (999999991).
//   Per char (uppercased ASCII a..z -> A..Z): the running accumulator is
//   updated as  h = signed_mod( (int32)(oc + (uint32)h*MUL), MOD )  where
//   signed_mod reproduces the engine's signed C "%" (sign follows dividend)
//   on a value first reinterpreted as a signed 32-bit int. The result is
//   finally masked with 0x7fffffff (the on-disk ident).
//
// This is a self-contained, dependency-free pure function (C++14, MSVC).
//
// TODO(port): wire when a C++ consumer needs name->ident without the Lua
//   layer (global.res emitter below, the DlgNPC content table, the SOUND_FX
//   / TYPE catalogs — all key on this same hash). Currently UNWIRED.

#ifndef SACRED_SDK_PORTS_ENGINE_SACRED_HASH_H
#define SACRED_SDK_PORTS_ENGINE_SACRED_HASH_H

#include <cstdint>
#include <cstddef>

namespace sacred {
namespace engine {

// Reproduces the engine's signed 32-bit modulo (remainder takes the sign of
// the dividend), matching the "signed-mod dance" in FUN_0080e780 / text.lua.
inline int32_t signed_mod_i32(int32_t value, int32_t mod) {
    // C/C++ % already follows the dividend's sign for signed operands, so a
    // direct % matches the engine. Kept as a named helper for clarity and to
    // mirror the documented "si % MOD with sign restore" steps exactly.
    return value % mod;
}

// Core accumulator step. `acc` is the running hash (treated as uint32 for the
// multiply, then reinterpreted signed for the modulo), `oc` is the (already
// uppercased) byte. Returns the new accumulator as uint32 (0..MOD-1, but
// stored full-width like the engine before the final &0x7fffffff).
inline uint32_t sacred_hash_step(uint32_t acc, uint8_t oc) {
    const int32_t kMul = 0x71;          // 113
    const int32_t kMod = 0x3b9ac9f7;    // 999999991

    // (uint32) oc + acc*MUL, wrapping in 32 bits, then reinterpret as signed.
    const uint32_t prod = acc * static_cast<uint32_t>(kMul); // wraps mod 2^32
    const uint32_t sum  = static_cast<uint32_t>(oc) + prod;  // wraps mod 2^32
    const int32_t  si   = static_cast<int32_t>(sum);         // signed reinterpret
    const int32_t  r    = signed_mod_i32(si, kMod);          // sign follows si

    // Restore to a non-negative 32-bit accumulator (engine keeps it u32 wide
    // between iterations; sign only mattered for the modulo above).
    return static_cast<uint32_t>(r);
}

// Hash a length-delimited byte buffer (NUL not required, NUL not consumed).
// ASCII a..z are uppercased; all other bytes pass through unchanged. Returns
// the on-disk ident: hash & 0x7fffffff.
inline uint32_t sacred_hash(const char* data, size_t len) {
    uint32_t h = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t oc = static_cast<uint8_t>(data[i]);
        if (oc >= 0x61 && oc <= 0x7a) oc = static_cast<uint8_t>(oc - 0x20);
        h = sacred_hash_step(h, oc);
    }
    return h & 0x7fffffffu;
}

// Convenience overload for NUL-terminated C strings.
inline uint32_t sacred_hash(const char* cstr) {
    size_t len = 0;
    if (cstr) { while (cstr[len] != '\0') ++len; }
    return sacred_hash(cstr, len);
}

} // namespace engine
} // namespace sacred

#endif // SACRED_SDK_PORTS_ENGINE_SACRED_HASH_H
