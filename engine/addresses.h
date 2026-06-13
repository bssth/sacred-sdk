// engine/addresses.h — ALL absolute engine VAs in ONE place. (Goal A1)
//
// Sacred Gold (Steam build 2.0.2.28). EXE base 0x00400000, NO ASLR, so a VA is
// stable to hardcode. At runtime the hooked module's base may differ in theory,
// so call sites add `reb` (= module_base - 0x00400000) to rebase: a full VA V
// becomes `reb + V`. These constants are the FULL VAs (not RVAs).
//
// This header is pure constants — including it has zero behavior cost. It is the
// single source of truth so a future re-port to another Sacred build is a
// find-in-one-file job. See engine/offsets.h for struct-field offsets.
#pragma once
#include <cstdint>

namespace sdk { namespace engine { namespace addr {
// (pre-C++17 toolchain: namespace-scope `constexpr` has internal linkage — the
//  correct header-constant idiom here; no `inline` variables.)

// --- Singleton roots --------------------------------------------------------
constexpr uintptr_t QM             = 0x00AACF80; // cQuestMgr (ECX of quest/dialog handlers)
constexpr uintptr_t OM              = 0x00AD5C40; // cObjectManager (creature table; arr=*(om+4))
constexpr uintptr_t CTX             = 0x0182EBE8; // per-session context (slot=*(ctx+0x14))
constexpr uintptr_t PLAYER_STRUCT   = 0x006D5C40; // resolve_player_struct: [image_base+this]

// --- Global engine tables ---------------------------------------------------
constexpr uintptr_t QUEST_REGISTRY_BEGIN = 0x00AAD3A4; // journal/quest-display vector begin
constexpr uintptr_t QUEST_REGISTRY_END   = 0x00AAD3A8;
constexpr uintptr_t TRIGGER_NAME_BEGIN   = 0x00AAB708; // trigger-name table (stride 0x54)
constexpr uintptr_t TRIGGER_NAME_END     = 0x00AAB70C;
constexpr uintptr_t TRIGGER_NAME_CAP     = 0x00AAB710;
constexpr uintptr_t HOSTILITY_MATRIX     = 0x00890A30; // 16x16 faction matrix
constexpr uintptr_t ITEM_TYPE_CATALOG    = 0x008EC328; // stride 0x44, 5624 entries
constexpr uintptr_t QUESTSTORE           = 0x00AABF18; // quest store (stride 0x124, +0x120 flags)

// --- Engine functions (FUN_*) we call or hook -------------------------------
// Dialog / content / resource
constexpr uintptr_t FUN_RESOLVE_CONTENT  = 0x00465070; // (qm; name, alloc) -> content handle  [thiscall]
constexpr uintptr_t FUN_BIND_CONTENT     = 0x00465220; // (qm; idx, handle) -> writes DlgNPC entry+0x4c
constexpr uintptr_t FUN_STAMP_IDX        = 0x005498F0; // (cCreature; idx, net) -> +0x245
constexpr uintptr_t FUN_DIALOG_HANDLER   = 0x0048F9E0; // tag 0x1f Dialog (DialogShow_v2)
constexpr uintptr_t FUN_DIALOG_BYNAME    = 0x0048BB40; // tag 0x03 dialog-by-name processor
constexpr uintptr_t FUN_DIALOG_APPLY     = 0x00461540; // dialog apply/show (called by 0x0048BB40)
constexpr uintptr_t FUN_SAVELOAD_SERIAL  = 0x00465690; // quest/dialog save-load serializer
constexpr uintptr_t FUN_GLOBALRES_LOOKUP = 0x0080EAF0; // global.res lookup by sacred_hash key
constexpr uintptr_t FUN_RES_RESOLVE      = 0x006726F0; // handle->text (high-bit -> globalres[h&0x7fffffff])
constexpr uintptr_t FUN_RES_HASH         = 0x0080E780; // string -> sacred_hash (engine reference impl)
constexpr uintptr_t FUN_FIELD_READER     = 0x00472BC0; // opcode field-stream decoder
// TODO(port): FUN_006AFD20 (qm+0x99f4 dialogText vector grow) — NOT in decompiled corpus; the #1 RE gap (O1)
constexpr uintptr_t FUN_DIALOGTEXT_GROW  = 0x006AFD20;

// Record walker / scripting
constexpr uintptr_t FUN_RECORD_WALKER    = 0x00475680; // (qm; ...) record dispatcher, ret 0x18
constexpr uintptr_t FUN_CREATENPC        = 0x00482510; // (ctx; recBuf, unused) CreateNPC handler, ret 8
constexpr uintptr_t FUN_VEC_GROW         = 0x004BB1D0; // std::vector grow/insert (DlgNPC append)

// Spawn / creature control
constexpr uintptr_t FUN_BUILD_POS        = 0x006224B0; // dest-tile resolve
constexpr uintptr_t FUN_CREATE           = 0x005FBA40; // create-by-type
constexpr uintptr_t FUN_SECTOR_RESOLVE   = 0x00635C40;
constexpr uintptr_t FUN_STANCE           = 0x0052E420; // (cCreature; mode, val) -> +0x1F0
constexpr uintptr_t FUN_WAKE             = 0x0059F580; // WakeUp AI
constexpr uintptr_t FUN_DESTROY          = 0x005FBDB0; // DelNPC
constexpr uintptr_t FUN_JOIN             = 0x00450C50; // companion/escort join (writes roster +0x94)

// Resource manager / debug
constexpr uintptr_t FUN_GLOBALRES_LOADER = 0x0080E680; // patch1 detour target (load global.res from disk)
constexpr uintptr_t FUN_FORCE_FG_WAIT    = 0x00811440; // patch6: neutralized busy-wait
constexpr uintptr_t FUN_GLOW            = 0x00408B70; // glow-off (ret 0x14, disabled)
constexpr uintptr_t FUN_DEBUG_LOG        = 0x0066EF40; // engine vararg debug_log (log mirror hook)

}}} // namespace sdk::engine::addr
