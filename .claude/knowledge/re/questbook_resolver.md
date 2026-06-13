# Quest-log `res:NAME` -> +0x24 handle resolver — 2026-05-15

## TL;DR

The 32-bit value tag-0x35 stamps into `entry+0x24` (== `*(cQuestMgr+0xa880)`)
is produced inside the FunkCode interpreter `FUN_00472bc0` **case 1**
(the `01 <cstring> 00` opcode). It takes **one of two forms** depending
on the string content:

1. **Resource-name string** (the `res:NAME` case): handle =
   **`FUN_00672740(const char* name)` = `hash31(name) | 0x80000000`**.
   Self-contained, deterministic, callable standalone. (Confidence HIGH)
2. **Object-reference string** (`QUESTNPC`/`QUESTFELLOW`/`DLGNPC`/`CPOS_hero`):
   handle = `FUN_0044a1c0()` = a runtime **object-instance id** (small
   sequential index into the creature/object table, stride 0x80, via
   `FUN_00428830`). **This is what the vanilla `d106/d107/d108` slots
   were** — three adjacent quest NPCs spawned consecutively, NOT hashes.
   (Confidence HIGH — explains the +1 adjacency a hash can't.)

## The resolver (resource-name path)

`FUN_00472bc0` case 1: copies the opcode cstring into scratch
`cQuestMgr+0xa460`; `res:` prefixed names land in the generic branch
which re-copies from `+0xa464` (the 4-byte `res:` prefix is skipped —
pass the name **without** `res:`). Then:

```
FUN_00672740 @ 0x00672740   __cdecl, 1 arg = const char* name (no "res:")
  uint h = FUN_0080eaa0(name);        // 0x0080eaa0 hash, returns h & 0x7fffffff
  if (h && FUN_0080eaf0(h))           // 0x0080eaf0 dict lookup, must be loaded
      return h | 0x80000000;          // <-- the +0x24 handle
  return 0;                           // unknown name -> 0 (gate fails, row hidden)
```

`hash31`: `h=0; for c in name: h = (xform(c) + h*0x71) % 0x3b9ac9f7; return h & 0x7fffffff`
where `xform = FUN_0084bce6(c)` (locale upcase/transliterate). Identical
code is inlined in `FUN_0080e780`, `FUN_0080e980`, `FUN_0080eaa0`.

The `0x80000000` bit is the engine's "this is a hashed resource id, not
a string ptr" tag — see the unified resolver `FUN_006726f0`:
`(id & 0x80000000) ? FUN_0080eaf0(id & 0x7fffffff) : FUN_0080e780(id)`.
The downstream journal text formatter (`FUN_006b4940`/`FUN_006b5530`,
called from `FUN_006b07e0`) consumes +0x24/+0x28 through that same
hash-or-ptr convention, so a `h|0x80000000` value is correctly typed.

## Calling it from our DLL

`FUN_00672740` is leaf-ish, `__cdecl`, needs **no `this`/ECX**, only
requires the resource dictionary to be loaded (it is, post world-load —
`FUN_0080eaf0` walks the global `cScriptResource` cache singleton
`DAT_017e7f68`, populated by then).

```c
// returns the u32 to stamp into entry+0x24 (and +0x28/+0x2C — same
// kind of value for log lines 2/3; tag-0x35 writes them via the same
// unaff_EBX in FUN_00496080's sub_idx%10 / scan loop).
typedef unsigned (__cdecl *resolve_t)(const char* name);
static const resolve_t qb_resolve = (resolve_t)0x00672740;

unsigned questlog_handle(const char* name /* e.g. "MYQUEST_LINE1", NO "res:" */) {
    unsigned h = qb_resolve(name);   // hash|0x80000000, or 0 if name unknown
    return h;                        // stamp into *(u32*)(entry+0x24)
}
```

If you only have the raw `res:NAME` literal, pass `name+4`. If the name
isn't a registered resource the call returns 0 and the row stays hidden
(`FUN_006b07e0` gate `+0x24 != 0`) — so the resource string must exist
in the loaded dictionary (e.g. added via the game's `.res`/string
tables). Resolving a name the dictionary doesn't know cannot be faked
by this function.

`+0x28`, `+0x2C`, … (log lines 2,3,…) take the **same** form: in
`FUN_00496080` the value written is always `unaff_EBX` = the same
`*(cQuestMgr+0xa880)`, just routed to +0x28+ when `sub_idx%10 != 0`.
So one `questlog_handle()` per log line, identical convention.

## Confidence

| Claim | Conf |
|---|---|
| +0x24 = `FUN_00672740(name)` = `hash31\|0x80000000` for resource strings | HIGH (decompiled chain 00472bc0->006725e0/00672720/00672740/0080eaa0) |
| vanilla d106/d107/d108 = object-instance ids (FUN_0044a1c0/FUN_00428830), not hashes | HIGH (consecutive ids impossible for the multiplicative hash; matches obj-table stride 0x80) |
| `FUN_00672740` callable standalone, __cdecl, 1 char* arg, no ECX | HIGH (no `in_ECX`, no globals beyond the dict singleton) |
| `res:` prefix is stripped (pass bare name) | MEDIUM — generic branch copies from +0xa464 (=+0xa460+4); confirm exact prefix bytes with a runtime BP on FUN_00672740 arg |
| Dictionary loaded post world-load | HIGH (consistent with existing render recon; FUN_0080eaf0 uses populated DAT_017e7f68) |
| Unknown name -> 0 (cannot synthesize) | HIGH (explicit `if (FUN_0080eaf0(h)) ... else return 0`) |

## Runtime check to close the loop

BP on `0x00672740` entry during normal play; dump the `const char*`
arg and EAX at ret. Confirms (a) exact string form passed (with/without
`res:`), (b) `EAX == entry+0x24` for a resource-text quest. For an NPC
quest, BP `0x0044a1c0` instead and confirm EAX matches the d1xx slots.

## Files
- `re/ghidra/decompiled/00472bc0_FUN_00472bc0.c` (case 1, lines 76-374)
- `re/ghidra/decompiled/00672740_FUN_00672740.c` (**the resolver**)
- `re/ghidra/decompiled/0080eaa0_FUN_0080eaa0.c` (hash31)
- `re/ghidra/decompiled/0080eaf0_FUN_0080eaf0.c` (dict lookup)
- `re/ghidra/decompiled/006726f0_FUN_006726f0.c` (hash-or-ptr convention)
- `re/ghidra/decompiled/0044a1c0_FUN_0044a1c0.c` + `00428830_FUN_00428830.c` (obj-id path = vanilla slots)
- `re/ghidra/decompiled/00496080_FUN_00496080.c` (writes +0x24/+0x28 from a880)
