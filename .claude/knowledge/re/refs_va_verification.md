# refs VA byte-sig verification vs our build (2026-06-13)

Disassembled the key absolute VAs from the refs reports at their file offsets
(`VA - 0x400000`) in our `sdk/Sacred_decrypted.exe` to settle whether the
community "Underworld" addresses transfer to our Steam **Gold** build.

**Headline:** the GAME is Sacred + Underworld, but the **German standalone
Underworld EXE the community tools target is NOT the same binary** as our Steam
Gold `Sacred.exe` — function layouts differ. So:

| VA | Source | Prologue seen | Verdict |
|---|---|---|---|
| `0x0066EF40` FUN_0066ef40 (control) | our RE | `mov eax,fs:[0]; push -1; push 0x871e1c` (SEH) | ✓ mapping correct |
| `0x008EC328` item TYPE catalog | our RE | `00 00 00 00 "TYPE"` (magic @+4) | ✓ **transfers** |
| `0x00890A30` hostility matrix | our RE | `01 01 01 01 …` (relation bytes) | ✓ **transfers** |
| `0x00553080` ui_create_window_by_name | refs (UW) | `push esi; mov esi,ecx; call …` | ✓ aligns to *a* function (semantics unconfirmed) |
| `0x0066F1C0` debug_print | refs (UW) | decodes mid-instruction (garbage) | ✗ **does NOT transfer** |
| `0x00615FD0` console | refs (UW) | `lea ecx,[esp+0x50]; call …` (mid-fn) | ✗ **does NOT transfer** |
| `0x008485E2` engine_alloc | refs (UW) | `mov ebp,1; movsx eax,[ecx]; inc ecx` (mid-fn) | ✗ **does NOT transfer** |

## Rules for the ports (sdk/ports/)
- **Our own RE-corpus offsets are authoritative** (item TYPE catalog, hostility
  matrix, all the `re/` specs, the talk signal, FunkCode, dialog, quest, etc.).
  Use freely.
- **refs FUNCTION VAs (German-UW) are LEADS, not addresses.** Most don't align.
  `engine_bindings.h` must mark every refs VA `TODO(re-find)` — locate the
  function in OUR build by string/xref signature, don't call the refs VA.
- **refs FORMAT/STRUCT knowledge is build-independent and trustworthy** — the
  PAX hero-save layout, PAK/keyx/wldx/texture formats, balance.bin offset tables,
  CSV data, MP protocol are file/data formats, not code VAs. Port them as-is.
- **Hero-save offsets** still need the Classic-vs-UW version gate (`+0x48`); our
  saves are UW-layout, but keep the version switch.

Method: capstone x86 32-bit, file offset = VA-0x400000, no ASLR.

## PAX hero-save format — VERIFIED against real saves (2026-06-13)
Parsed `save/hero06.pax` (22720 B) + `save/Hero00.pax` (22750 B). The refs PAX
decode is byte-exact on our real saves:
- `+0x00` magic = **`AMH\x1B`** (0x1b484d41) — the **Underworld** variant (NOT
  AMH\x07 Classic). → our saves use UW stat offsets; `hero_save.h`/`hero_stats.h`
  should DEFAULT to the UW layout, Classic behind the version switch.
- Section index `@0x100`, 12-byte `{type:u32, offset:u32, sizeInflated:u32}`.
  Live sections: **0xC7 stats** (off 0x1C0, ~2013-2374 B inflated), **0xCA/0xCB
  items** (4444 each), **0xC8 world/placement** (5306-21148), **0xC3/0xC4**
  special/name (556 each).
- **`0xBAADC0DE` framing** present at 0x1C0 (start of the C7 section) — confirms
  the custom zlib frame; there is NO raw `0x789C` zlib header, so the codec must
  emit/parse the 0xBAADC0DE wrapper, not stock zlib stream magic.
CONCLUSION: the hero-save port is the lowest-risk, highest-value wire-up — the
format reads our real data. Only remaining: the zlib inflate/deflate of each
framed section + the stat-field offsets within the decompressed C7 blob.
