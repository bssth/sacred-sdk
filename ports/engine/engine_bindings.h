// engine_bindings.h — named engine VAs + thin in-process call wrappers.
//
// ★ VERIFIED STATICALLY 2026-06-13 against sdk/Sacred_decrypted.exe (which is
//   FULLY decrypted — the file maps 1:1, VA = file_offset + 0x400000, base
//   0x00400000, no ASLR, so these == the live VAs; see
//   .claude/knowledge/re/our_build_vas.md). The earlier "refs UW VAs don't
//   transfer / land mid-function" warnings were an ARTIFACT of disassembling the
//   ENCRYPTED on-disk Sacred.exe by mistake. Re-checked on the decrypted dump
//   with capstone: the VAs below marked [VERIFIED] have clean prologues AND
//   confirmed behaviour (string xrefs / call structure). VAs marked
//   [CODE-OK,LABEL?] have valid code but the refs LABEL is unconfirmed (e.g.
//   0x00553080 is actually a scalar-deleting destructor, NOT a window creator) —
//   do NOT call those on the refs label alone.
//
// SOURCE: refs_native_src.md (German UW patch table) as LEADS; ground truth =
//   capstone on Sacred_decrypted.exe (this session) + the Ghidra corpus symbols.
//
// This header is self-contained (standard headers only). The call wrappers are
// IN-PROCESS (cast VA -> fn ptr); compiled, invoked only from inside Sacred's
// address space. The zlib + globalres entries are ALSO pinned + runtime
// byte-sig-verified in sdk/engine_resolve.cpp (the live, self-checking path).

#ifndef SACRED_SDK_PORTS_ENGINE_ENGINE_BINDINGS_H
#define SACRED_SDK_PORTS_ENGINE_ENGINE_BINDINGS_H

#include <cstdint>
#include <cstddef>

namespace sacred {
namespace engine {
namespace va {

// ---- zlib (VERIFIED, statically resolved + sig-checked in engine_resolve) ---

// inflate(z_streamp strm, int flush). FPO prologue
// `8b 54 24 04 83 ec 44 …` (mov edx,[esp+4]; sub esp,44h). Holds both
// "incorrect header check"@0x00963ca0 and "invalid block type"@0x00963c2c.
constexpr uint32_t kInflate              = 0x00669b10;  // [VERIFIED]
// inflateInit_(z_streamp, const char* version, int stream_size). Called by
// uncompress with version="1.2.1"@0x00963b20, stream_size=0x38.
constexpr uint32_t kInflateInit_         = 0x00669af0;  // [VERIFIED]
// inflateEnd(z_streamp).
constexpr uint32_t kInflateEnd           = 0x0066b180;  // [VERIFIED]
// int __cdecl uncompress(Bytef* dst, uLongf* dstLen, const Bytef* src,
// uLong srcLen). The stock zlib wrapper (sub esp,38h z_stream; init/inflate
// (Z_FINISH)/end; plain ret). Used to drop the external zlib1.dll dep.
constexpr uint32_t kUncompress           = 0x0066e160;  // [VERIFIED]

// ---- code addresses (VERIFIED on Sacred_decrypted.exe) ---------------------

// global.res resolver: void* __stdcall(uint32_t handle). Masks handle&0x7fffffff
// when the high bit is set, loads the global.res manager singleton from
// [0x0182ED50] into ECX, tail-calls the thiscall resolver FUN_0080eaf0(mgr,id),
// `ret 4`. Returns a resource-record pointer (NULL on miss; 12 in-build
// callers). The resource RECORD layout (offset to its name string) is not yet
// mapped — needs a live read before exposing handle->string.
constexpr uint32_t kGlobalResResolve     = 0x006726F0;  // [VERIFIED]
constexpr uint32_t kGlobalResInner       = 0x0080EAF0;  // thiscall(mgr,id)
constexpr uint32_t kGlobalResMgrPtr      = 0x0182ED50;  // singleton ptr cell

// printf-like debug logger that writes DEBUG.LOG / ExpLog.LOG. References
// "DEBUG.LOG"@0x00964010. (Replaces the WRONG refs lead 0x0066F1C0, which is
// mid-function garbage in the encrypted file and unrelated code here.)
constexpr uint32_t kDebugPrint           = 0x0066E6A0;  // [VERIFIED]

// engine allocator: cObjectManager::allocate, __thiscall(this = cObjectManager
// @[0x00AD5C40], size) -> ptr in EAX. `mov esi,ecx; mov eax,[esp+4]=size;
// reads [esi+0x34/0x38/0x44]`. Corpus-named (005fafe0_cObjectManager_allocate).
// Use THIS, not the refs lead 0x008485E2 (which is mid-function here).
constexpr uint32_t kObjMgrAllocate       = 0x005FAFE0;  // [VERIFIED]
constexpr uint32_t kObjMgrPtr            = 0x00AD5C40;  // cObjectManager ptr cell

// ---- refs leads: valid code, but LABEL UNCONFIRMED — DO NOT call blindly ----

// refs called this "ui_create_window_by_name", but the bytes here are a clean
// __thiscall scalar-deleting destructor (`push esi; mov esi,ecx; … test
// byte[esp+8],1; call free; mov eax,esi; ret 4`). Label is WRONG; kept only as
// a documented landmark. TODO(verify): find the real window-create by xref to a
// "UI_WND_*" name string before any use.
constexpr uint32_t kUnk_0x553080         = 0x00553080;  // [CODE-OK,LABEL?]
// refs "console handler". Valid code (`lea ecx,[esp+0x50]; call; test al,al`)
// but purpose unconfirmed here. TODO(verify) by console-string xref.
constexpr uint32_t kUnk_console_0x615FD0 = 0x00615FD0;  // [CODE-OK,LABEL?]

// ---- data addresses --------------------------------------------------------

// live game-mode flag: BYTE 0 = Singleplayer, 1 = Multiplayer
// (#GameModeAddr_German). Runtime state — can't be byte-sig'd statically.
// TODO(verify): confirm by a live read / code xref before relying on it.
constexpr uint32_t kGameModeFlag         = 0x0182CB6C;  // [UNVERIFIED data]

// ---- constants -------------------------------------------------------------

// Global.res on-disk obfuscation: each WORD XORed with this key
// (refs_native_src.md; the loader's `xor` scheme). The on-disk Steam file is
// reportedly already plain (read via the Patch-1 detour) — this key applies to
// the embedded BINARY-0x6B copy. TODO(verify): confirm against the BINARY-0x6B
// unscramble loop at the loader region before relying on it for decode.
constexpr uint16_t kGlobalResXorKey      = 0x45AD;

} // namespace va

// ---- thin in-process call wrappers (FUTURE-USE; never invoked here) --------
//
// Each casts a VERIFIED VA to the engine's calling convention. Compiled but
// invoked only inside Sacred's address space. The zlib + globalres entries are
// also exposed (resolved + sig-checked) via sdk/engine_resolve.cpp — prefer that
// path at runtime; these casts are the static, no-resolver convenience form.

// global.res resolve: void* __stdcall(handle) -> resource record ptr (NULL on
// miss). VERIFIED. Safe to call in-process (reads the global.res singleton);
// SEH-guard at the call site in case global.res isn't loaded yet.
inline void* GlobalResResolve(uint32_t handle) {
    using Fn = void* (__stdcall*)(uint32_t);
    return reinterpret_cast<Fn>(va::kGlobalResResolve)(handle);
}

// cObjectManager::allocate(size) -> ptr. __thiscall: ECX = the cObjectManager
// (deref [kObjMgrPtr]), one stack arg = size. VERIFIED. Caller must pass a valid
// `this`; do NOT call before the object manager is constructed.
inline void* ObjMgrAllocate(void* obj_mgr_this, std::size_t size) {
    using Fn = void* (__thiscall*)(void*, std::size_t);
    return reinterpret_cast<Fn>(va::kObjMgrAllocate)(obj_mgr_this, size);
}

// debug_print(msg): the DEBUG.LOG writer. VERIFIED VA, but its exact arg shape
// (it starts by reading a timestamp via rdtsc and takes a struct ptr in ESI/arg)
// is NOT a plain printf(const char*) — modeled loosely. TODO(verify) prototype
// before calling; for now treat the VA as a documented landmark.
constexpr uint32_t kDebugPrintVA = 0x0066E6A0;  // see TODO(verify) above

// ---- live data reads (in-process; safe only when injected) -----------------

// Read the live game-mode flag (0 = SP, 1 = MP). TODO(verify) address/anchor.
inline uint8_t ReadGameMode() {
    return *reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(va::kGameModeFlag));
}
inline bool IsSinglePlayer() { return ReadGameMode() == 0; }

// XOR-decode a Global.res BINARY-0x6B buffer in place (per-WORD, key 0x45AD).
// NOTE: refs_native_src.md cautions the original is a CHAINED xor
// (`xor [ecx-2],[ecx]`), i.e. NOT a plain per-WORD XOR — the loader loop at
// kGlobalResLoader must be re-read before trusting this. Provided as the simple
// per-WORD variant the table documents; TODO(verify) against the actual loop.
inline void DecodeGlobalResWordXor(uint16_t* words, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        words[i] ^= va::kGlobalResXorKey;
}

} // namespace engine
} // namespace sacred

#endif // SACRED_SDK_PORTS_ENGINE_ENGINE_BINDINGS_H
