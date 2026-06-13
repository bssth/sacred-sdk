# 06 — FunkCode.bin value-type dictionary

Curated table of value types found inside record payloads. Built from manual analysis plus iterative type-inference passes (those probe scripts have since been removed; the final dictionary lives in `sdk\re\py\funkcode_ops.py`). Field-byte coverage with this dict: 76.4% on SERAPHIM.

## Grammar reminder

```
payload := flags:u8  field*
field   := type:u8  value:(encoding determined by type)
```

`flags` is almost always `0x00`. Each `field` is a TLV-ish `(type, value)` pair where the type byte alone determines value width/encoding. The same byte value can mean different things as a top-level **tag** vs. as a value **type** — context (outer record's tag) disambiguates.

## Table

| Type | Encoding | Bytes | Note |
|---|---|---|---|
| `0x07` | u8 | 1 | small enum |
| `0x08` | u8 | 1 | small enum/flag |
| `0x17` | u8 | 1 | 100% u8 in inference |
| `0x39` | u8 | 1 | small enum |
| `0x45` | u8 | 1 | small enum |
| `0x48` | u8 | 1 | small enum (distinct from outer tag `H`) |
| `0x50` | u8 | 1 | 100% u8 in inference |
| `0xe0` | u8 | 1 | 100% u8 in inference |
| `0x0a` | u16 | 2 | small integer |
| `0x28` | u16 | 2 | always 2 bytes in histogram |
| `0x69` | u16 | 2 | value-type 0x69 (distinct from outer tag 0x69 'i') |
| `0x93` | u16 | 2 | small integer |
| `0x02` | u32 | 4 | i32 — can be negative (e.g. -2 = 0xFFFFFFFE) |
| `0x04` | u32 | 4 | i32 — can be negative |
| `0x0b` | u32 | 4 | default integer literal — most common |
| `0x11` | u32 | 4 | integer (e.g. 5001) |
| `0x14` | u32 | 4 | integer |
| `0x15` | u32 | 4 | integer |
| `0x1c` | u32 | 4 | integer (key/index role) |
| `0x1d` | u32 | 4 | resource id (see note below; earlier inference saw 96% u16 but 4-byte is correct) |
| `0x1f` | u32 | 4 | integer |
| `0x38` | u32 | 4 | integer (counter) |
| `0x49` | u32 | 4 | integer |
| `0x4a` | u32 | 4 | integer (small values, e.g. 22) |
| `0x53` | u32 | 4 | integer |
| `0x6b` | u32 | 4 | integer (large bit-flag-style values) |
| `0x6d` | u32 | 4 | integer |
| `0x75` | u32 | 4 | integer |
| `0x86` | u32 | 4 | sequence/counter integer (seen in `N` records) |
| `0x87` | u64 | 8 | u64 (likely 2x u32 tuple) — pervasive in 's' statement records |
| `0x88` | u64 | 8 | u64 (tuple) — sibling of 0x87 |
| `0x89` | u64 | 8 | u64 (tuple) — sibling of 0x87 |
| `0x01` | cstr | variable (ASCIIZ) | named symbol reference (most common cstr role) |
| `0x05` | cstr | variable (ASCIIZ) | system-ID strings (e.g. 'od_6001') |
| `0x09` | cstr | variable (ASCIIZ) | cstr |
| `0x16` | cstr | variable (ASCIIZ) | string literal (sound IDs, quest templates) |
| `0x1e` | cstr | variable (ASCIIZ) | cstr |
| `0x2a` | cstr | variable (ASCIIZ) | <-- overridden by u16 above |
| `0x30` | cstr | variable (ASCIIZ) | cstr |
| `0x32` | cstr | variable (ASCIIZ) | quest/region ID strings (e.g. '210') |
| `0x33` | cstr | variable (ASCIIZ) | cstr |
| `0x40` | cstr | variable (ASCIIZ) | cstr |
| `0x41` | cstr | variable (ASCIIZ) | cstr |
| `0x47` | cstr | variable (ASCIIZ) | cstr |
| `0x4d` | cstr | variable (ASCIIZ) | cstr |
| `0x52` | cstr | variable (ASCIIZ) | cstr |
| `0x5f` | cstr | variable (ASCIIZ) | cstr |
| `0x67` | cstr | variable (ASCIIZ) | cstr |
| `0x6f` | cstr | variable (ASCIIZ) | cstr |
| `0x79` | cstr | variable (ASCIIZ) | cstr |
| `0x7c` | cstr | variable (ASCIIZ) | cstr |
| `0x7d` | cstr | variable (ASCIIZ) | cstr (sentinel-block contents) |
| `0x8f` | cstr | variable (ASCIIZ) | cstr |

Note on `0x1d`: confirmed via the interpreter decompile (see [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md)) as a u32 resource id, formatted with `":res:%d"` and resolved through `cScriptResource` RTTI.

## Coverage

- Records fully parsed: 90981/125055 = 72.75%
- Field bytes parsed: 2739564/3587542 = 76.36%
- Distinct types in dictionary: 53
- Distinct types still unresolved: see below

## Remaining unresolved type bytes

These bytes appear as value-type tags but could not be pinned to a single encoding from automated inference + sampling. Candidates for the next round and for cross-checking against `Sacred.exe`'s parser:

| Type | Stalls | Hypothesis |
|---|---|---|
| `0x8b` | 7482 |  |
| `0x36` | 5557 | high frequency — likely u8 enum, but the stall context keeps the parser misaligned. Probably context-dependent. |
| `0x00` | 4954 |  |
| `0x61` | 1378 |  |
| `0x70` | 1241 |  |
| `0x29` | 1164 |  |
| `0x12` | 954 |  |
| `0x43` | 779 |  |
| `0x42` | 717 | also outer tag `B`; as value type seems to introduce a length-prefixed substring or nested record. |
| `0x2b` | 684 |  |
| `0x4c` | 573 |  |
| `0x31` | 499 | between cstr and u32 — looks like cstr for short numeric IDs ('210'-style). |
| `0x0c` | 368 | appears inside `0x04` records as `0c <u32> <type>` — likely u32. |
| `0x74` | 291 |  |
| `0x44` | 273 |  |

## Alignment-cascade insight

Many of the "remaining unresolved types" above (especially `0x8b`, `0x36`, `0x00`) are not real type bytes. Inspection shows they are interior bytes of `u32` values the parser lands on after misaligning on an upstream record. Example: `0b 8b 05 00 00 00` is `(type=0x0b, u32=0x0000058b)` — the `0x8b` is the low byte of the value, not a type tag.

So 76% coverage understates how well the grammar is understood. The actual issue: one or two u8-tagged types (likely from the inference rounds) is sometimes a u16 or u32 in another context, and the mismatch propagates downstream. Two ways to resolve:

1. Per-outer-tag schemas — re-do inference conditioned on the outer record tag, so the same byte can have different widths in `s` vs. `d` records.
2. Read the parser in `Sacred.exe` — find the function that loads `FunkCode.bin` (xref the literal `"%s\FunkCode.bin"` in `.rdata`), follow it to the value-decode switch table, and lift the canonical encodings.

Option 2 is cheaper. (Done — the dispatcher is `FUN_00472bc0`; see [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md). It confirms `0x1c`/`0x53`/`0x86` as u32, adds `0x54`–`0x57` as u32, and resolves `0x1d` as a resource id.)

## Why coverage plateaus

The grammar is almost flat-typed `(type, value)*`, but a few records carry encodings the type alone doesn't disambiguate — notably `0x42` in some contexts and the high-frequency `0x36` and `0x55` bytes. Likely explanations:

1. Context-dependent typing. The same value-type byte means different widths in different outer-tag contexts (e.g. `0x02` u32 inside `s`, u8 inside `d`).
2. Length-prefixed sub-records. Some types may read `[len:u8 or u16][nested bytes:len]`. Pascal-strings explain a few cases, not all.
3. Polymorphic types. A single type byte may be a discriminated union (flags select int-vs-string).

Cheapest resolution: read the parser inside `Sacred.exe` — find the function that reads `%s\FunkCode.bin`, follow to the field-decoder switch, read the case table. Gives canonical encodings without further inference.
