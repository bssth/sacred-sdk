// hd/geometry.h — the layout numbers a custom resolution needs.
//
// Sacred's 2D layer is written around a fixed 1024x768 frame: half-extents,
// clip rectangles, panel anchors and a few ratios are compiled into the code as
// immediates. ReBorn made the engine resolution-independent by computing the
// same numbers for W x H into a block of globals and pointing the engine at
// them. We compute the identical numbers here (`sdk/.claude/knowledge/re/
// reborn_hd_port.md` holds the derivation) and hand them to the patch engine,
// which bakes them into the very same instruction operands.
//
// Why a block addressed by ReBorn's VAs: every generated patch record names the
// value it needs by the address ReBorn's own table used, so the generator does
// not have to invent a naming scheme and a record stays readable next to the
// disassembly it came from. The block lives in OUR DLL — in our build that
// address range holds SSE constants and is not free.
//
// At 1024x768 every value equals the constant already compiled into the engine,
// so applying the whole HD set at 1024x768 must not change a single byte of
// behaviour. That is the regression test.
#pragma once
#include <cstdint>

namespace sdk { namespace hd {

// The mirrored range, in ReBorn's addresses. ReBorn's own table ends at
// 0xA1F0D8; the slots from 0xA1F100 up are ours, for values ReBorn computes
// inline on the x87 stack and writes straight into engine operands:
constexpr uint32_t kSlotLo = 0x00A1EF40;
constexpr uint32_t kSlotHi = 0x00A1F114;
constexpr uint32_t kSlotTile256W = 0x00A1F100;   // round(256 * W / 1024), x87 nearest-even
constexpr uint32_t kSlotTile256H = 0x00A1F104;   // round(256 * H / 768)
constexpr uint32_t kSlotPixels4  = 0x00A1F108;   // 4 * W * H, a 32-bit frame buffer size
constexpr uint32_t kSlotOffX2    = 0x00A1F10C;   // 2 * offX, for operands ReBorn shifts twice
constexpr uint32_t kSlotOffY2    = 0x00A1F110;   // 2 * offY

// Compute every slot for W x H. Idempotent; call again to change resolution
// (the already-applied patches keep their old values, so only do that before
// patching).
void init(int w, int h);

// init() with the size from the config: `[hd] width/height` in sdk.ini, else
// ReBorn's own `SR_HD_WIDTH`/`SR_HD_HEIGHT` in Settings.cfg (so his configurator
// keeps working), else 1024x768 — at which the whole table is a no-op.
void init_from_config();

bool ready();
int  width();
int  height();

// Address of a slot inside OUR block, for patches that read the value at run
// time. Null if the address is outside the mirrored range.
void* slot_addr(uint32_t reborn_va);

// Value of a slot, as the 4 raw bytes the operand would hold (int or float
// bits, whichever that slot is). False if out of range or not initialised.
bool slot_u32(uint32_t reborn_va, uint32_t* out);

// For logs / the overlay: a one-line summary.
const char* status();

}} // namespace sdk::hd
