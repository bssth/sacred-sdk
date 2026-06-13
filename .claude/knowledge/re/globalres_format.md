# scripts/US/global.res — on-disk format (RE, byte-exact verified)

Verified against the real 23123-slot file: a from-scratch rebuild of the index +
blob from the model below reproduces the file **byte-for-byte, 0 mismatches**.
Resolver math from `FUN_0080f5e0` decompile; loader is `FUN_0080e680` (it only
running-XOR-decrypts the embedded BINARY-107 copy — the on-disk file is plain,
same layout, read via the Patch-1 detour).

## 1. Layout (confidence: certain)

```
[0 .. blob_start)            INDEX: N records, 16 bytes each:
                               u32 d0      ; see below — NOT a per-slot length
                               u32 ident   ; sacred_hash(name) & 0x7fffffff
                               u32 off     ; absolute byte offset (see below)
                               u32 pad     ; usually 0 (4 vanilla slots have 1/3/5 — cosmetic, unused)
[blob_start .. blob_start+4) u32 = 2 * (code-units of the LAST slot, slot N-1)
[blob_start+4 .. eof)        text payloads, UTF-16-LE, contiguous, NO terminators,
                             NO per-entry header. Stored in INDEX order.
```

- `blob_start = u32 @ file offset 8 = off[0]`. `N = blob_start / 16` = 23123.
- `d0[0] = N` (the binary-search hi bound). For k>=1: `d0[k] = 2 * units(k-1)`
  where `units(j)` = exact UTF-16 code-unit count of slot j's text (no NUL).
- `off[0] = blob_start`. `off[k+1] = off[k] + 2*units(k)`. Payload of slot k
  begins at `off[k] + 4`; payloads are packed back-to-back.
- The 4 "slack" bytes at `blob_start` ARE `d0[N]` conceptually (= `2*units(N-1)`),
  the length the loader reads for the last slot. The file's final 4 bytes are
  simply the last 2 chars of slot N-1's text — there is **no separate tail
  sentinel**. (text.lua's "4-byte tail" / "4-byte entry header" / `raw_len =
  len+4` notes are WRONG; they only round-tripped because it copies span bytes
  verbatim and never re-derives — fine for in-place edits, broken for appends.)

## 2. FUN_0080f5e0 extraction (resolved, certain)

`piVar1` = int* to start of resource buffer (index then blob, one block).
Binary search: `hi=piVar1[0]` (=N), `mid=(lo+hi)/2`, key vs `piVar1[mid*4+1]`
(= `ident`). **Signed** int32 compare, but all idents `< 0x80000000` so signed
== unsigned here (sort ascending either way).

On hit at `mid`:
- length (UTF-16 code units): `uVar6 = piVar1[(mid+1)*4] >> 1` = **`d0[mid+1] >> 1`**.
  The suspicious `(mid+1)*4` is real and correct: length lives in the NEXT
  record's `d0`. For `mid = N-1` it reads `piVar1[N*4]` = the dword at
  `blob_start` = `2*units(N-1)`.
- source pointer: `(char*)piVar1 + (piVar1[mid*4+2] >> 1)*2 + 4`
  = `base + off[mid] + 4` (offs are always even, `(off>>1)*2 == off`).
- copies `uVar6` units, then writes a NUL itself. So the terminator is NOT on
  disk — the engine appends it.

Consequence: **the index can be reordered freely; `off` is an absolute pointer,
fully independent of index order** — *provided* `d0[k+1]` still equals
`2*units(k)` for the slot that index position k points at.

## 3. Invariants a correct file must satisfy (certain unless noted)

a. Index sorted **ascending by `ident`** (unsigned == signed for this data).
b. `d0[0] = N` (total slot count, = file_offset_8 / 16).
c. For k = 1..N-1: `d0[k] = 2 * units(text of slot k-1)`.
   Extra dword at `blob_start` = `2 * units(text of slot N-1)`.
d. `off[0] = blob_start = N*16`; `off[k+1] = off[k] + 2*units(k)`.
   (Blob physical order MUST equal index order — `d0[k+1]` is the length for
   whatever slot sits at index k, so text order and index order are coupled.)
e. No alignment/terminator/sentinel bytes anywhere. File ends exactly at
   `off[N-1] + 4 + 2*units(N-1)`.
f. `units` = number of UTF-16 LE code units (bytes/2), terminator excluded.

## 4. Concrete `_emit` algorithm for text.lua (spec only — do not edit here)

Replace the parse/emit model entirely. Per-slot keep only: `ident`,
`text` (decoded UTF-16-LE string, or raw unit bytes), `pad`.

```
1. Parse vanilla:
   blob_start = u32@8 ; N = blob_start/16
   for k in 0..N-1: read d0,ident,off,pad at k*16
   last_len_dw = u32 @ blob_start
   units(k) = (k<N-1) ? d0[k+1]>>1 : last_len_dw>>1
   text_bytes(k) = blob[ off[k]+4 : off[k]+4 + 2*units(k) ]   (UTF-16-LE, no term)
2. Build slot list = all originals (ident,text_bytes,pad) PLUS, for each
   registered (name,s) with no ident collision, a new slot:
       ident = sacred_hash(name)        (& 0x7fffffff)
       text_bytes = utf16le(s)          NO trailing \0\0
       pad = 0
   For collisions: replace that slot's text_bytes (reskin) — already works.
3. Sort the FULL list ascending by ident as unsigned u32. (Dedupe equal
   idents — last write wins.)  M = #list.
4. Compute units[k] = #text_bytes[k] / 2 for each k in 0..M-1.
5. Emit index (M records, 16 bytes each):
       d0[0]   = M
       d0[k]   = 2*units[k-1]            for k = 1..M-1
       ident[k]= list[k].ident
       off[0]  = M*16
       off[k+1]= off[k] + 2*units[k]
       pad[k]  = 0   (pad is unused; 0 is safe)
6. Emit blob:
       u32  = 2*units[M-1]               (the "extra length" dword)
       then text_bytes[0] .. text_bytes[M-1] concatenated (in this sorted
       order — physical order MUST match index order)
7. File = index ++ blob.  No tail, no padding, no terminators.
```

`utf16le(s)` = per the existing `utf16le_z` but **drop the `"\0\0"`**
(terminator must NOT be stored). `sacred_hash` in text.lua is already verified.

## Confidence / runtime caveats

- Format, offsets, d0, length math, binary search: **certain** — byte-exact
  rebuild of all 23123 slots, plus 300 random + edge-slot decode checks pass.
- Signed-vs-unsigned ident sort is academically ambiguous (no ident has bit31
  set, and sacred_hash masks `& 0x7fffffff`, so it can never matter). Sort
  unsigned ascending — safe.
- The only thing not provable purely statically: that the engine never reads
  past `off[M-1]+4+2*units(M-1)` (a runtime BP on `FUN_0080f5e0` would 100%
  confirm). The model already explains every byte of the vanilla file with no
  leftover, so this is very low risk.
