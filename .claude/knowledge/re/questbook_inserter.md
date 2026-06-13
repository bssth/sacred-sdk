# Quest-display registry inserter — 2026-05-15

## TL;DR

**Inserter VA: `FUN_00806d20` @ `0x00806d20`** (confidence **HIGH**).

Grow primitive: **`FUN_004b5370` @ `0x004b5370`** — `vector<questEntry_t>::resize(N)`.

Allocator (per-element): **`FUN_008080a0(class=5, type=0x11)` @ `0x008080a0`** — typed-object factory; case `(5, 0x11)` does `operator new(0x174)` then runs the entry ctor and installs vtable `&PTR_FUN_008911d8`.

Copy ctor (used by resize to populate new slots): **`FUN_005533d0` @ `0x005533d0`** — bound via vtable at `0x008911f0`.

The array begin/end live at globals **`DAT_00aad3a4` (begin)** and **`DAT_00aad3a8` (end)** — the same memory the recon called `[cQuestManager+0x424..0x428]`. (The "cQuestManager" pointer in the 6 mutator handlers is just an instance pointer whose `+0x424` happens to equal these globals; the vector is effectively a singleton.)

## Pseudocode of FUN_00806d20

```c
// Called from FUN_007dc260 (record-dispatcher) when tag-byte == 0x9a.
void cQuestRegistry::loadEntry(IStream* stream /* this is param_1 */)
{
    /* implicit ECX = some load-context whose +0x30 holds the IStream vtable */
    IStream* io = *(IStream**)(this + 0x30);
    uint32_t idx = stream[0];   // first dword of the record = target slot

    io->vtbl->op_C(0);                    // begin-record
    io->vtbl->op_8(stream+1, stream-1);   // header glue
    io->vtbl->op_C(0);                    // end-header

    // GROW the registry if idx is past the current end:
    if ((DAT_00aad3a8 - DAT_00aad3a4) / 0x174 <= idx)
        FUN_004b5370(idx + 1);            // vector::resize(idx+1)

    // Pointer to the (now-existing) target slot:
    uint8_t* entry = DAT_00aad3a4 + idx * 0x174;

    // Deserialize 8 fixed blocks straight into the slot:
    io->vtbl->read(entry + 0x000, 4);     // header word
    io->vtbl->read(entry + 0x004, 4);     // type/flags
    io->vtbl->read(entry + 0x008, 4);     // quest_id (the scan key!)
    io->vtbl->read(entry + 0x010, 0x14);  // 5 dwords (state region)
    io->vtbl->read(entry + 0x024, 4);     // log slot 0
    io->vtbl->read(entry + 0x028, 0x28);  // log slots 1..10
    io->vtbl->read(entry + 0x050, 0x118); // big middle blob
    io->vtbl->read(entry + 0x168, 0xc);   // tail (incl. +0x16C)

    // Active-hero bookkeeping for the special "hero" entries:
    int qid_at_4 = *(int*)(idx*0x174 + 4 + DAT_00aad3a4);  // entry+4 (NOT +8 here)
    if (qid_at_4 == 100 || qid_at_4 == 0x65) {
        if (idx == FUN_0049da50()) {                      // active-hero idx
            FUN_0049dab0((DAT_00aad3a8 - DAT_00aad3a4) / 0x174);
            return;
        }
        FUN_0049da80();
    }
}
```

Note the `entry+4` check (not `+8`): the special-hero token sits in a different field
than the per-class quest_id. That matches the layout the recon inferred.

## Callers — when does the inserter fire?

`FUN_00806d20` has exactly **one** xref:

- `0x007df853` inside `FUN_007dc260` — the master `case 0x9a:` of a 256-entry
  record-tag switch. `FUN_007dc260` itself has 15 callers, all engine
  glue: `cUI_Manager_Instance_0060abd0` (×8), `cObjectManager_load_00602d10`,
  `cEngine_renderThread_0060e350`, `cCommand_armaStop_execute_00756250`,
  `cUI_Manager_endThread_00757060`, etc.

So **the inserter fires exclusively when an `0x9a` record arrives in the
event/object stream** — that stream is fed by both **savegame load**
(`cObjectManager::load`) and **network sync** (engine render thread).
Nothing in the FunkCode bytecode can trigger it; nothing in normal
gameplay creates new entries unless an `0x9a` record arrives.

That confirms the recon's mystery: **new quest_ids silently no-op
because no code path generates an `0x9a` record for them at runtime.**
The list is fixed at the moment the world first emits its `0x9a`
records (presumably during cObjectManager load).

## Concrete SDK plan

Two ways to call from the SDK:

### Option A — direct insert (minimal)

Just grow + write the slot, bypassing the record dispatcher entirely:

```cpp
typedef void (__cdecl *vec_resize_t)(uint32_t new_count);
const vec_resize_t questRegistry_resize = (vec_resize_t)0x004b5370;

uint8_t** const arrBegin = (uint8_t**)0x00aad3a4;
uint8_t** const arrEnd   = (uint8_t**)0x00aad3a8;

uint32_t entryCount() { return (uint32_t)((*arrEnd - *arrBegin) / 0x174); }

uint8_t* questRegistry_pushSlot(uint32_t idx /* must be == entryCount() */) {
    if (idx >= entryCount()) questRegistry_resize(idx + 1);
    return *arrBegin + idx * 0x174;
}
```

Caveats — `FUN_004b5370` uses **`__thiscall`** with ECX pointing to the
vector object (begin at +0, end at +4, capacity-end at +8). Set ECX
to `(void*)0x00aad3a4` before calling. In MSVC inline-asm:

```cpp
__asm {
    mov ecx, 0x00aad3a4
    push new_count
    mov  eax, 0x004b5370
    call eax
    add  esp, 4   // it's __thiscall, callee pops 'this' but caller pops the arg? verify
}
```

The callee-pops convention has to be verified against the disassembly
prologue/epilogue of `FUN_004b5370` before relying on it.

After the resize call, the slot at `*arrBegin + idx*0x174` is a
**default-constructed** entry (vtable already installed by the resize's
internal copy from a stack template). You then write `entry+8 =
your_quest_id` and any other fields you need before the first FunkCode
mutator runs against it.

### Option B — emulate an 0x9a record (cleanest)

Build a fake byte stream mimicking what `FUN_00806d20` reads (1 dword
header + 8 read-blocks totalling 0x174 bytes payload), wrap it in
whatever IStream the call-context expects, and call `FUN_00806d20`
with that as `param_1` and the load-context as ECX. This preserves
the active-hero bookkeeping for `quest_id == 100/0x65`.

This is messier — requires reverse-engineering the IStream vtable —
but matches the engine's intended path.

### Recommended

Use **Option A** for prototyping (just call the resize and stamp
`entry+8 = quest_id`). Confirm the FunkCode mutators now hit the
new entry. If the active-hero hook is needed, switch to Option B.

## Confidence

| Claim | Confidence |
|---|---|
| `FUN_00806d20` is the only inserter VA | **high** — it's the only call site that grows then writes via `DAT_00aad3a4` |
| `FUN_004b5370` is the resize primitive | **high** — its body is textbook `vector::resize` and the `(end-begin)/0x174 vs N` precondition matches |
| `DAT_00aad3a4` / `DAT_00aad3a8` are the same memory as `[cQuestManager+0x424/0x428]` | **high** — 31 reads from `DAT_00aad3a4` include the 6 mutator functions found in the prior recon |
| Element ctor is `FUN_008080a0(5, 0x11) → operator new(0x174)` | **medium-high** — vtable `0x008911d8` matches between FUN_005533d0 (copy ctor), FUN_008080a0 case 5/0x11, and the entry's runtime vtable |
| Inserter only fires from save/network record `0x9a` | **high** — 1 xref, inside `FUN_007dc260` switch case `0x9a` |
| Calling Option A from SDK works without crashing | **medium** — the calling convention is plausible but unconfirmed; needs a runtime smoke test |

## Files touched

- `.claude/knowledge/re/scan_0x174.py` — byte-scanner for `0x174` immediates
- `.claude/knowledge/re/scan_0x174_hits.txt` — full hit list (49 likely-instruction)
- `.claude/knowledge/re/classify_0x174.py` — classifier (loop-step / call / write-back)
- `re/ghidra/decompiled/00806d20_FUN_00806d20.c` — **the inserter**
- `re/ghidra/decompiled/004b5370_FUN_004b5370.c` — vector::resize
- `re/ghidra/decompiled/008080a0_FUN_008080a0.c` — typed factory (case 5/0x11)
- `re/ghidra/decompiled/005533d0_FUN_005533d0.c` — entry copy ctor
- `re/ghidra/decompiled/004bf0f0_FUN_004bf0f0.c` — vector grow inner
- `re/ghidra/decompiled/007dc260_FUN_007dc260.c` — master record dispatcher (case `0x9a`)
