# global.res resolver chain — mapped statically (2026-06-13)

Fully traced from `Sacred_decrypted.exe` (capstone). All VAs are live VAs (the
file maps 1:1, base 0x00400000, no ASLR — see `our_build_vas.md`). This is the
handle→string path that unblocks companion names, item names, and possibly the
parked dialog-text problem.

## Singleton
- global.res manager pointer cell: **`[0x0182ED50]`** (deref → `mgr`).

## Public stdcall wrappers (all `__stdcall(uint32 handle)`, `ret 4`)
| VA | does |
|---|---|
| **`0x006726F0`** | if `handle & 0x80000000`: `handle &= 0x7FFFFFFF`. `ecx = mgr`; tail-call `FUN_0080EAF0(mgr, handle)`. The "build a string object from the resource" path (the one most callers use; 12 call sites). |
| `0x00672700` | same as above but WITHOUT the high-bit mask (entry expects handle already in `eax`). |
| `0x00672720` | `ecx = mgr`; `FUN_0080E980(mgr, handle)` — a DIFFERENT inner (other resource class / typed getter). |
| `0x00672740` | `FUN_0080EAA0(mgr, handle)` → if non-null, `FUN_0080EAF0(mgr, that)` — chains an indirection then resolves. |

## Inner string builder — `FUN_0080EAF0(mgr, id)` (`__thiscall`, `ecx=mgr`)
1. map/lookup via `FUN_00448ED0` (hash or tree find on `[mgr+8]`).
2. `FUN_0080F5E0(mgr, key)` → `ebp` = pointer to a **UTF-16** string (the raw
   resource content). NULL → return null.
3. wide-strlen it (`cmp word[ebp],0; add ecx,2; …`), then `FUN_005CD800(ebp,
   ebp+len*2)` builds a string object; returns that object (callers treat the
   result as usable text — caller `0x00452D79` scans `[result]` byte-wise for
   `'-' '+' '.'` i.e. numeric parsing, so the object's data is reachable as a
   char buffer at offset 0, but the EXACT return ABI of 0x6726F0 — by-value
   struct vs pointer — is NOT yet pinned; one live probe settles it).

## The actual table lookup — `FUN_0080F5E0(mgr, key)` (`__thiscall`)
This is the global.res record search. Record format (VERIFIED from the code):
- `esi = [mgr+0]` = base of the entries array.
- entries are **16 bytes** each (`shl 4` indexing), **sorted ascending by ident**.
- **entry layout (16 B):**
  - `+0x00`  count/size field (used as `value >> 1` = `sar eax,1`)
  - `+0x04`  **ident (uint32)** — the search key (== `sacred_hash(name) & 0x7fffffff`)
  - `+0x08`  data offset/length (also `sar 1` to get the real value)
  - `+0x0C`  (4 more bytes — role TBD)
- algorithm: **binary search** on `+0x04` (`mov edx,eax; shl edx,4; cmp ebx,[edx+esi+4]`).
- on hit: reads the NEXT entry's `+0` as a size, `malloc((size*2)+2)` via
  `FUN_00849842`, copies the UTF-16 string out (so it returns a HEAP `wchar_t*`
  the caller must free with the matching free `FUN_00849xxx`).

## How to finish handle→string (one live step)
The static map is complete; what's left needs ONE live read (we're injected):
1. call `0x006726F0(handle)` for a KNOWN handle (e.g. a companion name_res like
   `0x0019F6E3`) and dump 32 bytes of the result + `[result]`/`[result+0]`.
2. that pins whether 0x6726F0 returns a `char*`, a `wchar_t*`, or a small struct
   {ptr,len}. Then `sacred.globalres(handle)` → UTF-8 is a ~10-line binding:
   resolve → (if wide) WideCharToMultiByte → push string. SEH-guard the call
   (faults if global.res isn't loaded yet).
3. Alternatively call `FUN_0080F5E0(mgr,key)` directly for the raw `wchar_t*`
   (mgr = `*(void**)0x0182ED50`), but remember it ALLOCATES — free it the same
   way, or just copy-out and accept the small leak for a one-shot lookup.

## Why this matters
- companion `name_res` / `model_res` handles (`companions.h`) resolve here.
- item/creature display names resolve here.
- the PARKED dialog-text store may be reachable: dialog content is keyed by the
  same `sacred_hash & 0x7fffffff` ident the binary search uses — if the runtime
  NPC's text is a global.res entry, this resolver reaches it (worth a live retry
  once handle→string works).
