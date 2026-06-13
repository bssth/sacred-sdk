# OUR-build VAs: game zlib + engine_bindings (2026-06-13)

Goal: find the game's own `inflate`/`uncompress` (for hero-save decompression)
and re-find the engine_bindings helpers in **OUR** build (Steam Sacred Gold,
base `0x00400000`, no ASLR).

## TL;DR / headline  ⚠️ CORRECTED 2026-06-13 — earlier claim was WRONG

- **`sdk/Sacred_decrypted.exe` IS FULLY DECRYPTED in `.text`.** The earlier
  "still encrypted on disk" claim in this doc was a MISTAKE (it used a bad
  daa-density threshold of >800x `0x27`/page; in reality 0/1167 .text pages hit
  that, in BOTH files). Re-verified this session: the decrypted dump has REAL
  x86 at every known VA — control funcs match exactly: `FUN_00811440` =
  `51 53 55 8b` (what patch6 expects), `cEngine_save@0x619bf0` = `6a ff 68 …
  64 a1` (SEH prologue), `cObjectManager_allocate@0x5fafe0` = `83 ec 24 …`,
  `cStatsManager_Instance@0x425290` = `a1 20 ad aa 00`. The on-disk
  **`Sacred.exe` IS encrypted** (its .text is garbage at those same VAs); the
  two files differ in 99.6% of .text bytes.
- **The file maps 1:1: for EVERY section, `VA = file_offset + 0x400000`** (.text
  roff 0x1000→VA 0x401000, .data roff 0x4e7000→VA 0x8e7000, etc.). Image base
  0x00400000, no ASLR ⇒ the LIVE process image == the decrypted file
  byte-for-byte. **So engine VAs ARE resolvable STATICALLY and equal the live
  VAs.** (The old "-0x8C248 .data rebase" delta below is ALSO suspect — string
  VAs computed straight from file offset are what the code actually references;
  see the corrected zlib anchor VAs in §1.)
- **zlib resolved STATICALLY this session (from Sacred_decrypted.exe):**
  `inflate @ 0x00669b10` (FPO: `mov edx,[esp+4]; sub esp,44h; …` =
  `inflate(z_streamp,int)`; contains both "incorrect header check"@0x00963ca0
  and "invalid block type"@0x00963c2c), `inflateInit2_-chain @ 0x006643a0`
  (refs "1.2.1"@0x008943e8), debug logger `@ 0x0066e6a0` (refs
  "DEBUG.LOG"@0x00964010). These are PINNED + runtime byte-sig-verified in
  `sdk/engine_resolve.cpp`. uncompress = still TODO (small inflate-wrapper).
- The Ghidra corpus (`re/ghidra/decompiled/*.c`) remains a useful symbol map,
  but raw bytes can now be read straight from `Sacred_decrypted.exe` too.
- **CRITICAL hero-save caveat (from `refs_va_verification.md`):** PAX sections
  are framed with a custom **`0xBAADC0DE`** marker, **NOT** a raw `0x78 0x9C`
  zlib stream header. So the game does NOT feed raw `uncompress` on the section
  bytes — there is a wrapper that strips/handles the `0xBAADC0DE` frame and then
  calls `inflate`. Calling stock `uncompress(dst,&dlen, src+frameHdr, …)` will
  fail unless you skip the frame. Reproduce the game's framing, or call the
  game's wrapper, not bare `uncompress`.

## .data rebase fact (needed to read any string VA correctly)

At runtime the packer relocates `.data`: section-header VA `0x8e7000` is the
on-disk/unpacked layout, but the **live image** puts these strings ~`0xcd0000+`.
Proven anchor: the string `"incorrect header check"` (file off 5651616) →
**real VA `0x00cd7a58`**, which exactly matches a corpus reference
`DAT_00cd7a58`. Delta from naive section-linear VA = **`-0x8C248` (−574024)**.
All "real VA" values below use this validated delta and are cross-checked against
corpus `DAT_` references where one exists.

---

## 1. zlib (PRIMARY) — zlib **1.2.1**, statically linked

`grep` of the EXE: the `'uncompress' x11` are substrings inside `.data`
debug/error message strings (first at `0xcd3cf4`), NOT export names (static link
has none). `'inflate' x1` / `'deflate' x1` = the version-copyright banners. So
the strings confirm **zlib 1.2.1** but give no direct function VA.

### zlib string anchors — REAL runtime VAs (validated, all in CLEAN .data pages)

| string (zlib 1.2.1) | OUR real VA | role |
|---|---|---|
| `"1.2.1"` (version) | `0x00c080f9` | returned by `zlibVersion()`; checked by `inflateInit2_` |
| `"deflate 1.2.1 Copyright …"` | `0x00c080f1`→banner | deflate copyright |
| `"inflate 1.2.1 Copyright …"` | `0x00c09301` | inflate copyright (ref'd by inflate state machine) |
| `"need dictionary"` | `0x00cd7bf4` | z_errmsg[2] |
| `"incompatible version"` | `0x00cd7b84` | z_errmsg (Z_VERSION_ERROR) |
| `"buffer error"` | `0x00cd7b9c` | z_errmsg |
| `"data error"` | `0x00cd7bc0` | z_errmsg |
| `"stream error"` | `0x00cd7bcc` | z_errmsg |
| `"incorrect header check"` | `0x00cd7a58` | inflate (BAD) — **proven anchor** |
| `"unknown compression method"` | `0x00cd7a84` | inflate header parse |
| `"invalid window size"` | `0x00cd7a70` | inflate header parse |
| `"unknown header flags set"` | `0x00cd7a3c` | inflate (gzip) |
| `"header crc mismatch"` | `0x00cd7a28` | inflate (gzip) |
| `"incorrect data check"` | `0x00cd7a10` | inflate CHECK |
| `"incorrect length check"` | `0x00cd79f8` | inflate LENGTH |
| `"invalid block type"` | `0x00cd79e4` | inflate |
| `"invalid stored block lengths"` | `0x00cd79c4` | inflate STORED |
| `"invalid code lengths set"` | `0x00cd79a8` | inflate_table |
| `"invalid bit length repeat"` | `0x00cd798c` | inflate CODELENS |
| `"invalid literal/lengths set"` | `0x00cd7970` | inflate_table |
| `"invalid distances set"` | `0x00cd7958` | inflate_table |
| `"invalid literal/length code"` | `0x00cd793c` | inflate fast |
| `"invalid distance code"` | `0x00cd7924` | inflate fast |
| `"invalid distance too far back"` | `0x00cd7904` | inflate fast |

| symbol | OUR VA | signature / ABI | evidence | confidence |
|---|---|---|---|---|
| `inflate(z_streamp, int flush)` | **UNFOUND statically** — code page encrypted on disk. Runtime band: the function that, in live memory, references `0x00cd7a58` ("incorrect header check") + the cluster `0x00cd79xx`. | cdecl, 2 args; returns `int` (Z_OK/Z_STREAM_END/Z_*). | strings validated; code not statically readable | n/a (find live) |
| `inflateInit2_` / `inflateInit_` | **UNFOUND statically** | the function referencing `"1.2.1"`@`0xc080f9` and writing the inflate state. cdecl. | version string anchor | n/a (find live) |
| `uncompress(Bytef* dest, uLongf* destLen, const Bytef* src, uLong srcLen)` | **UNFOUND statically** — small wrapper, references no error string; reached only via the inflate it calls. | cdecl, 4 args; returns `int`. | static link, no name; co-located with inflate | n/a (find live) |

### How to FIND inflate/uncompress at runtime (from the injected DLL)

We run inside the live process where SecuROM has decrypted everything. Use a
**memory signature scan**, not a fixed VA:

1. Module base = `GetModuleHandleA("Sacred.exe")` → `reb` (== `0x00400000`).
2. The error strings ARE at fixed real VAs above (validated, in clean .data).
   Scan the live `.text` (VA `0x401000`..`0x88f000`) for a 4-byte little-endian
   immediate equal to **`0x00cd7a58`** (`"incorrect header check"`). The
   instruction is a `mov [strm+msg], imm32` / `push imm32` **inside `inflate`**.
   Walk back to the function prologue (`push ebp; mov ebp,esp` or
   `mov eax,fs:[0]` SEH setup) to get `&inflate`.
3. `uncompress` is the small function that `call`s `inflateInit_`/`inflate`/
   `inflateEnd` in sequence with a local `z_stream` on the stack; find the
   `call inflate` site (rel32 to step-2's target) whose enclosing function also
   pushes 4 args and references no error string → `&uncompress`.
4. Alternatively scan for `"1.2.1"`@`0xc080f9` ref → `inflateInit2_`, then its
   single caller chain. (Belt-and-suspenders.)

Do this **once at init**, cache the resolved VAs. Then:

### How to CALL the game's uncompress from our injected DLL (in-process)

```cpp
// resolved once via the signature scan above; reb == GetModuleHandleA(0)
typedef int (__cdecl *zlib_uncompress_t)(
    unsigned char* dest, unsigned long* destLen,
    const unsigned char* src, unsigned long  srcLen);
static zlib_uncompress_t game_uncompress =
    (zlib_uncompress_t)(reb + (resolved_uncompress_rva));

unsigned long dlen = sizeInflated;          // from the PAX section index (the
                                            // 12-byte {type,offset,sizeInflated})
std::vector<unsigned char> out(dlen);
// IMPORTANT: do NOT pass the raw 0xBAADC0DE-framed bytes. Either:
//   (a) skip the game's frame header so `src` points at a real zlib stream
//       (first byte should be 0x78), OR
//   (b) call the game's own frame-aware wrapper instead of bare uncompress.
int rc = game_uncompress(out.data(), &dlen, framed_src + FRAME_HDR, framed_len - FRAME_HDR);
// rc==0 (Z_OK) on success; dlen := actual inflated length.
```

ABI: **cdecl**, caller cleans the stack, 4 stack args, return in `eax`. No
`this`. Safe to call cross-thread as long as each call uses its own buffers
(stock `uncompress` allocates its own `z_stream`).

Open item before wiring: determine `FRAME_HDR` size / the exact `0xBAADC0DE`
layout (live BP on the section loader `cEngine_save`-adjacent loader, or compare
a section's first inflated bytes). `0xBAADC0DE` does NOT appear in the EXE as a
literal (it is computed/written by code), consistent with a runtime frame.

---

## 2. engine_bindings (re-found + VERIFIED in OUR build) — CORRECTED 2026-06-13

⚠️ The old version of this table said several VAs "do NOT transfer / decode
mid-fn". That was WRONG — it disassembled the ENCRYPTED on-disk `Sacred.exe`.
Re-checked on the DECRYPTED dump (`Sacred_decrypted.exe`, capstone): the refs
UW VAs DO point at valid code in our build (we ARE Underworld). The catch is the
refs LABELS, not the addresses — some addresses are real code but a different
function than refs named. VAs == live VAs (file maps 1:1). Now pinned + runtime
sig-verified in `sdk/engine_resolve.cpp` and documented in `ports/engine/engine_bindings.h`.

| symbol | OUR VA | signature / ABI | evidence | status |
|---|---|---|---|---|
| zlib `inflate` | **`0x00669b10`** | `__cdecl(z_streamp,int)` | holds "incorrect header check"@`0x00963ca0` + "invalid block type"@`0x00963c2c`; FPO `mov edx,[esp+4];sub esp,44h` | **VERIFIED** |
| zlib `uncompress` | **`0x0066e160`** | `__cdecl(dst,&dlen,src,slen)` | inflateInit_("1.2.1"@`0x00963b20`)/inflate(Z_FINISH)/inflateEnd structure | **VERIFIED** (drops zlib1.dll) |
| zlib `inflateInit_` / `inflateEnd` | `0x00669af0` / `0x0066b180` | cdecl | called by uncompress | **VERIFIED** |
| global.res resolve | **`0x006726f0`** | `void* __stdcall(handle)` → resource ptr | `and eax,0x7fffffff`; mgr singleton `[0x0182ED50]`; inner thiscall `FUN_0080eaf0`; 12 callers | **VERIFIED** (record layout TBD) |
| debug/printf logger | **`0x0066e6a0`** | rdtsc + struct arg (NOT plain printf) | refs "DEBUG.LOG"@`0x00964010` (real string VA; the old `0xcd7dc8` was wrong) | **VERIFIED VA** (proto TBD) |
| allocator `cObjectManager::allocate` | **`0x005FAFE0`** | `__thiscall(this,size)` → ptr; this=`[0x00AD5C40]` | corpus-named + `mov esi,ecx; mov eax,[esp+4]` | **VERIFIED** |
| refs "ui_create_window_by_name" | `0x00553080` | — | valid code, but it's a **scalar-deleting destructor**, NOT a window creator — refs LABEL WRONG | CODE-OK, label? |
| refs "console handler" | `0x00615FD0` | — | valid code (`lea ecx,[esp+0x50];call;test al,al`); purpose unconfirmed | CODE-OK, label? |
| engine_alloc (refs `0x008485E2`) | — | — | this exact VA IS mid-fn here; use `0x5FAFE0` | use allocator above |

### Corpus-confirmed engine VAs reusable now (OUR build, authoritative)
From `re/ghidra/decompiled/` filenames (resolved symbols):
`cObjectManager_allocate 0x5FAFE0`, `cObjectManager_create 0x5FB360/0x5FB530/0x5FBA40`,
`cObjectManager_getData 0x5FCCB0/0x5FE000/0x603E30`, `cItemDataMgr_push 0x5FC8B0`,
`cEngine_creature_equipItem 0x611560`, `cEngine_dropItem 0x611620`,
`cEngine_save 0x619BF0`, `cWorld_getParentObject 0x6354D0/0x636310`,
`cCreature_inventory_putItem 0x549260`, `cCreature_equipment_equip 0x555E00`,
`cScriptCompiler_parseStatement 0x66FDF0`, `cTextureLoader_Instance 0x815F70`,
`cStatsManager_Instance 0x425290`. Talk/dialog (live-verified, from memory):
`FUN_00465070` (content resolver), `FUN_0048bb40` (tag-0x03), `FUN_00461540`,
`FUN_00465220`, `FUN_005498f0`, `FUN_006726f0` (globalres resolver).

---

## Method notes (this session)
- capstone x86-32, no-ASLR, file off = VA − 0x400000 for `.text`.
- daa-density (`0x27` byte count per 0x1000 page) reliably separates decrypted
  (<~150) from still-encrypted (~2000+) pages. The zlib code band and many
  engine funcs are ~2000 → encrypted on disk.
- `.data` string real VA = naive-section-linear VA − 0x8C248, anchored & proven
  by `DAT_00cd7a58` == `"incorrect header check"`.
