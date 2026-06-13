// engine/offsets.h — ALL engine struct-field offsets in ONE place. (Goal A1)
//
// Pure constants; including this has zero behavior cost. Grouped by struct.
// Confidence: every value here has been used in live SDK code or proven by RE
// this project. See engine/addresses.h for absolute VAs, engine/mem.h for the
// SEH-safe accessors that consume these.
#pragma once
#include <cstdint>

namespace sdk { namespace engine { namespace off {

// --- cCreature --------------------------------------------------------------
namespace cre {
constexpr uintptr_t CONTENT_HANDLE = 0x00C; // dialog content handle (vanilla = own handle)
constexpr uintptr_t TYPE           = 0x010; // low word = creature type id
constexpr uintptr_t HERO_CLASS     = 0x010; // hero class (same field, hero context)
constexpr uintptr_t FLAGS          = 0x014; // bit 0x80000=dialog gate, 0x40000000=placed, 0x200000=invuln
constexpr uintptr_t STANCE         = 0x1F0; // 0=class-default, 2=aggressive
constexpr uintptr_t FACTION        = 0x1F4; // faction/side word; bit0=awake/active
constexpr uintptr_t TALK_SIGNAL    = 0x200; // bit 0x400 = "player talking to this NPC" (the SDK talk signal)
constexpr uintptr_t DLG_OBJIDX     = 0x245; // DlgNPC array index selector (byte)
constexpr uintptr_t OWNER_SLOT     = 0x251; // companion owner slot
constexpr uintptr_t HP_CUR         = 0x4D4;
constexpr uintptr_t HP_MAX         = 0x4D8;
constexpr uintptr_t CMD_RING_BEGIN = 0x588; // scripted-command ring begin ptr (stride 0x44)
constexpr uintptr_t CMD_RING_END   = 0x58C;
constexpr uintptr_t NAME_FIELD_BUF = 0xA460; // opcode field-string scratch (the cstr a record field is read into)
constexpr uintptr_t POS_X          = 0xA860; // field-decoded position scratch
constexpr uintptr_t POS_Y          = 0xA864;
constexpr uintptr_t POS_SECTOR     = 0xA868;
constexpr uintptr_t FIELD_DWORD    = 0xA880; // field-decoded dword scratch
constexpr uintptr_t TALK_BIT       = 0x400;  // the bit within TALK_SIGNAL
}

// --- hero (player struct) ---------------------------------------------------
namespace hero {
constexpr uintptr_t CLASS   = 0x010;
constexpr uintptr_t EQUIP   = 0x1A4;
constexpr uintptr_t GOLD    = 0x3EE;
constexpr uintptr_t SKILLS  = 0x3CC; // skills block (also 0x3D4)
constexpr uintptr_t SKILLS2 = 0x3D4;
constexpr uintptr_t HP_CUR  = 0x4D4;
constexpr uintptr_t HP_MAX  = 0x4D8;
}

// --- cQuestMgr (qm @ addr::QM) ----------------------------------------------
namespace qm {
constexpr uintptr_t NAMEARRA_BEGIN   = 0x358;  // CreateNPC name buffer vector (stride 0x44, name@+0x04)
constexpr uintptr_t NAMEARRA_END     = 0x35C;
constexpr uintptr_t STATE_STORE      = 0x334;  // named-state / DefPos store (stride 0x64, key cstr@+4)
constexpr uintptr_t STATE_STORE_END  = 0x338;
constexpr uintptr_t ROSTER_BEGIN     = 0x31C;  // quest-NPC roster panel array (stride 0x34) — companion portraits
constexpr uintptr_t JOURNAL_BEGIN    = 0x424;  // journal registry (stride 0x174)
constexpr uintptr_t JOURNAL_END      = 0x428;
constexpr uintptr_t DLGNPC_BEGIN     = 0x755C; // DlgNPC/dialog array (stride 0x50)
constexpr uintptr_t DLGNPC_END       = 0x7560;
constexpr uintptr_t DLGNPC_CAP       = 0x7564;
constexpr uintptr_t CONTENT_BEGIN    = 0x765C; // content table name->handle (stride 0x48, handle@+0x44)
constexpr uintptr_t CONTENT_END      = 0x7660;
constexpr uintptr_t CONTENT_TREE     = 0x7668; // AVL tree for temp content-id allocation (0x2076..0x20cf)
constexpr uintptr_t DIALOGTEXT_BUF   = 0x79F4; // dialogText save-section buffer (0x2000)
constexpr uintptr_t DIALOGTEXT_VEC   = 0x99F4; // runtime dialogText store (vector begin)
constexpr uintptr_t DIALOGTEXT_END   = 0x99F8;
constexpr uintptr_t DIALOGTEXT_CAP   = 0x99FC;
}

// --- DlgNPC entry (stride 0x50, base qm+DLGNPC_BEGIN) ------------------------
namespace dlgnpc {
constexpr uintptr_t STRIDE   = 0x50;
constexpr uintptr_t KEY      = 0x00; // bound creature handle
constexpr uintptr_t NAME     = 0x04; // ASCIIZ name (fixed buffer)
constexpr uintptr_t CONTENT  = 0x44; // content handle (set by save-load restore)
constexpr uintptr_t MARKER   = 0x48; // overhead marker glyph (0x0B="?!", 0x0D=none, 0x08=off)
constexpr uintptr_t CONTENT4C= 0x4C; // ACTIVE content handle the renderer reads
}

// --- content table entry (stride 0x48, base qm+CONTENT_BEGIN) ---------------
namespace content {
constexpr uintptr_t STRIDE = 0x48;
constexpr uintptr_t HANDLE = 0x44; // the small content id (name keyed; +0..+0x43 = name)
}

// --- cCreature command-ring element (stride 0x44) ---------------------------
namespace ring {
constexpr uintptr_t STRIDE = 0x44;
constexpr uintptr_t TYPE   = 0x00; // 1=MoveTo, 9=Teleport, 0xe=PlaySound
constexpr uintptr_t ARG0   = 0x40; // X / sound id
constexpr uintptr_t ARG1   = 0x3C; // Y
constexpr uintptr_t ARG2   = 0x38; // sector
}

// --- common flag bits -------------------------------------------------------
constexpr uint32_t FLAG_DIALOG_GATE = 0x00080000; // cre+0x14
constexpr uint32_t FLAG_PLACED      = 0x40000000; // cre+0x14 (teleported/placed)
constexpr uint32_t FLAG_INVULN      = 0x00200000; // cre+0x14
constexpr uint32_t RES_HIGH_BIT     = 0x80000000; // content handle high bit -> direct global.res hash path

}}} // namespace sdk::engine::off
