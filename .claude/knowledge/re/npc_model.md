# Sacred Gold — NPC model (CreateNPC, faction, quest binding) — RE report

Target: Steam build 2.0.2.28, `sdk\Sacred_decrypted.exe`, base 0x00400000,
no ASLR (file offset == VA − 0x400000). Evidence: Ghidra decompiles in
`sdk/re/ghidra/decompiled/` (`00482510`, `00475680`, `00472bc0`),
opcode-faithful decoder `sdk/.claude/knowledge/re/npc_decode2.py` cross-checked
against 6,118 real `tag 0x01` records in
`bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin`.

---

## TL;DR

- **CreateNPC = FunkCode tag 0x01 → `FUN_00482510`.** Its body is a loop:
  `op = FUN_00472bc0()` (the field/opcode reader) → big `switch(op)` → repeat
  until `op==0`. **The switch case = the on-wire opcode byte** (interpreter
  `return local_114;` at `00472bc0:2469`, i.e. it returns the wire byte
  verbatim, except `0`=END and `0x20`=resolved CPOS:hero position). HIGH.
- Wire grammar inside a 0x01 payload (after the 1 flags byte): a stream of
  `op:u8` then a value whose width is fixed **by the opcode** (NOT the flat
  type table in doc 06 — that table is for *other* record families):
  - `0x02,0x11,0x36,0x75` → `+ i32 LE` (5 bytes total)
  - `0x04,0x0c,0x0d` → `+ i32 LE`; **if i32 == -2 (0xFFFFFFFE) → ASCIIZ
    position string follows** (the POSITION field). HIGH.
  - `0x03,0x0a,0x6b,0x6c,0x93,0x9b,0x9c` → `+ u16` (3 bytes)
  - `0x15` → `+ 4×u16` (9 bytes, a DefPos rectangle)
  - `0x19` → `+ 3×i32` (13 bytes)
  - `0x01,0x29,0x63,0x68,0x69,0x6a,0x9d,0x16` → `+ ASCIIZ`
  - everything else → **bare 1-byte flag/side opcode** (no value)
- **Position** is opcode `0x04` with i32 `-2` + ASCIIZ. Forms (resolved in
  `00472bc0` cases 4 & the case-1 cstr path): `CPOS:hero` (hero's tile, →
  `0xa860/64/68` X/Y/sector), `CPOS:RES:<res>` (resource-anchored),
  `DLGNPC`/`QUESTNPC`/`QUESTFELLOW` (existing creature lookup), a bare
  **named DefPos key** (looked up in `cQuestManager+0x334`), or a numeric
  i32 (vector/`vx` index — the `vx=%d` in the failure string). HIGH.
- **Faction/side lives at `cCreature+0x1F4`** (Ghidra `pTVar12[0x3e].spare`,
  8-byte TypeDescriptor stride ⇒ 0x3e*8+4). CreateNPC builds it in register
  `EBX` from the side/flag opcodes and commits with one store
  `pTVar12[0x3e].spare = unaff_EBX;` (`00482510:1354`). Adjacent
  `cCreature+0x1F0` (`[0x3e].pVFTable`) is the creature subtype (compared
  ==6/==7). HIGH on offset; MED on exact side-bit semantics (needs BP).
- **Quest-NPC binding = `cCreature+0x96` (i16)** = the quest/dialog id; the
  engine finds "the quest NPC" by scanning creatures for
  `*(i16*)(creature+0x96) == DAT_00aacf98` (current quest id) — see
  `00472bc0:165` (QUESTNPC resolver) and `0046cbe0:222`. It is **not** a
  plain CreateNPC field; it is stamped when the spawn is registered into the
  quest manager's NPC array (`in_ECX+0x35c`, stride 0x44/0x34) via the
  `0x01`/`0x05`/`0x29` name buffers. NPC_FieldSet (0x7a) addresses that
  array by index. MED — exact stamp site needs a BP.
- **NPC_FieldSet (tag 0x7a)** is inlined in walker `FUN_00475680:1943`.
  It selects the NPC-array entry `*(i16*)(ctx+0x94)`
  (`base=in_ECX+0x31c`, stride **0x34**) and, for wire field `0x38`, writes
  the value `*(u32*)(in_ECX+0xa860)` into **entry+0x18**. Only `+0x18` is
  writable via 0x7a in this build. HIGH.

---

## CreateNPC (tag 0x01) payload format

Record header (all FunkCode): `tag:u8=0x01  size:u16 BE (incl. 3-byte hdr)`.
Payload = `flags:u8 (≈0x00)` then opcode stream as above. Loop in
`FUN_00482510`: `iVar23 = FUN_00472bc0(); switch(iVar23){...}; ... while(iVar23!=0)`.
`in_ECX` = the parser/context block; the reader stashes the decoded value at
fixed slots: `+0xa880` generic u32, `+0xa860/+0xa864/+0xa868` X/Y/sector
triple, `+0x9a460` (`0xa460`) the cstring scratch, `+0xa880..0xa88c` a rect.

### Field/opcode table (wire byte → CreateNPC effect)

| op | wire | CreateNPC action (var in `00482510`) | conf |
|---|---|---|---|
| `0x02` | +i32 | **Type / creature class id** → `local_6a4` (1st 0x02 only); class fed to `cObjectManager_create_005fb530/…a40`. A 2nd `0x02` is appended to the name/id buffer (`local_654`) = **NPC sub/instance id** | HIGH |
| `0x04`/`0x0c`/`0x0d`/`0x2a` | +i32(−2)+cstr | **POSITION target** (see below). i32≠−2 ⇒ numeric `vx` vector index | HIGH |
| `0x01` | +cstr | **name / instance id** (uniqueness-checked: `NON_UNIQUE` log `0094e228`) | HIGH |
| `0x05` | +cstr | name2 buffer `acStack_214` | HIGH |
| `0x60` | (lit) | name from template literal `DAT_0094e1d4` | MED |
| `0x63` | +cstr | name3 `acStack_114` + `local_6ac` | MED |
| `0x03` | +u16 | **level/rank** `local_63c` → `FUN_0044ddc0((float)level)` | HIGH |
| `0x73` | +i32 | `local_650` → `*(i16*)(creature+0x4b*8+3)` (2nd level/rank slot) | MED |
| `0x11` | +i32 | **group/team id** `pTStack_640` → `FUN_004a9ef0`, also `creature[0x49]+1` | HIGH |
| `0x15` | +4u16 | **DefPos rectangle** (`0xa880..0xa88c`) → `FUN_004c33c0` register spawn area | MED |
| `0x33` | — | DefPos pair (`0xa880/0xa884`) | LOW |
| `0x36` | +i32 | scale/size float `local_698` | MED |
| `0x67` | +i32(0xa880) | `local_694` → `creature[0x56]+2` / `[0x4c]+1` (loot/sound set) | MED |
| `0x6b` | +i32(0xa880) | `pTStack_6e4`; if >0 ⇒ `creature[0x56]+3 \|= 8` (**STATIONARY / sleeping**) | MED |
| `0x75` | +i32 | `local_690` → `FUN_00450e20` (aux skill/spawn) | LOW |
| `0x90` | +i32(0xa860) | `local_688` → `*(u8*)(creature+0x80*8)` (anim/skin index) | MED |
| `0x09` | +cstr | **link to EXISTING dialog NPC by name** (no spawn) | HIGH |
| `0x0a` | +u16 | link existing dialog NPC by id | HIGH |
| `0x12` | (flag) | `EBX\|=1` → **awake/active**; triggers `cCreature_WakeUp_0059f580` | HIGH |
| `0x13` | flag | `EBX` bit2 | MED |
| `0x14` | flag | `EBX &= ~…` (clear low) | MED |
| `0x08` | flag | `local_6c8=1` (side: ally-leaning) | MED |
| `0x0e` | flag | `local_6c8=0` (side) | MED |
| `0x2b` | flag | `local_6c8=0`, `EBX=8` (side) | MED |
| `0x2e` | flag | `local_6c8=0`, `EBX\|=0x10` (side) | MED |
| `0x2f`/`0x31` | flag | `EBX=0x40` (side — **enemy/aggressive candidate**) | MED |
| `0x30` | flag | `local_6c8=0`, `EBX=0x20` (side) | MED |
| `0x32` | flag | `local_6c8=0`, `EBX=0x80` (side) | MED |
| `0x42` | flag | `local_6c8=0`, `EBX=0x400` (side) | MED |
| `0x43..0x46,0x4c,0x50,0x74,0x85,0x8a` | flag | further `EBX` bits (0x800,0x1000,0x2000,0x4000,0x8000,0x10000,0x8000000,0x2000000,0x10000000) | LOW |
| `0x1a/0x23/0x24/0x25/0x64/0x70/0x71/0x72` | flag | `local_6e0` bits (0x1/0x10/4/8/0x20/0x400/0x800/0x1000); `0x800`→FX flag 0x40, `0x1000`→`[0x3e].spare\|=0x80000` + add to team list | MED |
| `0x37/0x39/0x4b/0x65` | flag | `bVar7/bVar8` toggles (visual/aux) | LOW |
| `0x7b` | flag | `local_6dc[3]=1` | LOW |
| `0x8d` | flag | `local_6d8[7]=1` → blood/FX clone spawn (`FX_BLOODLINES_TGA`) | LOW |
| `0xa1` | flag | `local_6dc[2]=1` → `creature[2].spare \|= 0x200000` (**invulnerable/essential candidate**) | MED |
| `0x00` | END | terminates the record loop | HIGH |

`creature[N]` = Ghidra 8-byte-stride index; byte offset = `N*8` (`.pVFTable`)
or `N*8+4` (`.spare`).

### POSITION resolution (interpreter `FUN_00472bc0`, cases 4 & 1)

Opcode `0x04` carries i32; **`-2` ⇒ ASCIIZ position string** copied to
`in_ECX+0xa760`, then matched (`FUN_0085aa60`) in order:

1. `"CPOS:hero"` (`s_CPOS_hero_0094e998`) → hero creature pos:
   `0xa860=*(hero+0x1C)` X, `0xa864=*(hero+0x20)` Y, `0xa868=*(hero+0x24)`
   sector. Returns special code **0x20**.
2. `"CPOS:RES:<name>"` (`s_CPOS_RES__0094e98c`) → resolves a script
   resource, then that creature's `+0x1C/+0x20/+0x24`.
3. `"DLGNPC"` / `"QUESTNPC"` / `"QUESTFELLOW"` (case-1 cstr path,
   `0094ea30/e9cc/ea00`) → scans the quest NPC array for the matching
   creature (`+0x96 == DAT_00aacf98`) and uses its handle (`0xa880`).
4. otherwise: treated as a **named DefPos key** → looked up in the
   `cQuestManager+0x334` store (the SDK's named-position store; tile units),
   yielding `0xa860/0xa864`.
5. i32 ≠ −2 ⇒ literal **`vx` vector index** (the `vx=%d` in
   `s_CreateNPC_failed__vx__d__Type__d_0094eca8`).

Then CreateNPC writes the resolved coords into the new `cCreature`:
`*(i16*)(c+0x18)=…; *(i32*)(c+0x1C)=X; *(i32*)(c+0x20)=Y; *(u8*)(c+0x24)=sector`
(`00482510:881-884`) — matches ground-truth `+0x1C/+0x20`.

### Decoded real examples (Vampiress `FunkCode.bin`)

```
@0x008372 sz=40  00 02 20000000 02 b0060000 79 00 00 ff 00 01 00 00 00 04 feffffff "CPOS:HERO" 08 12
  0x02 i32=32        Type = SKELETON (npc.lua id 32)
  0x02 i32=1712      NPC sub/instance id (2nd 0x02 → name buffer)
  0x79 flag          float-pair marker (orientation)
  0xff,0x00          aux flags
  0x01 cstr=""       name (empty → engine auto-name, unique-checked)
  0x04 POS="CPOS:HERO"  spawn at hero tile
  0x08 flag          side (local_6c8=1)
  0x12 flag          awake/active → WakeUp
@0x0084fd sz=40  …02 22000000…   Type=34 LICH, else identical
@0x0086da sz=35  …02 21000000…   Type=33 ZOMBIE, no 2nd 0x02 (no sub-id)
@0x00a005 sz=35  …02 30000000…   Type=48
@0x00a0d8 sz=35  …02 5e000000…   Type=94
```
All 6,118 records decode cleanly under this grammar; the canonical
ground-truth record decodes byte-exactly (see `npc_decode2.py` "CANONICAL").

---

## cCreature property map

Struct stride/access from `00482510` (`creature[N]`=8-byte units) +
ground-truth.

| Offset | Meaning | Source / how set |
|---|---|---|
| `+0x10` | class/type id | ground-truth |
| `+0x18` | (i16) tile-ish; written with resolved pos hi | `00482510:881` |
| `+0x1C` | **world X** (i32, hero-world units) | `00482510:882`; GT |
| `+0x20` | **world Y** (i32) | `00482510:883`; GT |
| `+0x24` | sector (u8) | `00482510:884` |
| `+0x30` | bit16 = "is on map / spawnable" gate | `00482510:821,860` |
| `+0x96` | **quest/dialog binding id (i16)** | matched vs `DAT_00aacf98`; `00472bc0:165`, `0046cbe0:222` |
| `+0x1F0` | creature **subtype** (==6/==7 special) | `[0x3e].pVFTable` `00482510:1114/1135/1157` |
| `+0x1F4` | **faction/side + behavior flag word** | `[0x3e].spare = EBX` `00482510:1354`; OR-ed `\|0x80000` at 1618/1801 |
| `+0x10`(`[2].spare`) bit `0x200000` | invulnerable/essential candidate | `00482510:1166` (set by op `0xa1`) |
| `+0x10` bit `0x40000000` | "teleported/placed" | `00482510:946` |
| `creature[0x40].pVFTable` bit 0x10/0x200 | AI/active sub-flags | `00482510:1005,1510` |
| `creature[0x49]+1` (≈+0x24D) | group/team handle | op `0x11`, `00482510:1378` |
| `creature[0x4b].spare+3` | rank/level-2 (i16) | op `0x73`, `00482510:1500` |
| `creature[0x56].spare+3` bit 8 | **STATIONARY/sleep** | op `0x6b`, `00482510:1367` |
| `creature[0x80].pVFTable` (u8) | anim/skin index | op `0x90`, `00482510:1108` |

Level is applied via `FUN_0044ddc0((float)local_63c)` (op `0x03`).

---

## Faction / side

- **Storage:** `cCreature+0x1F4` (a flag/side word). CreateNPC accumulates
  it in `EBX` across the bare side opcodes, then commits once:
  `pTVar12[0x3e].spare = unaff_EBX;` (`00482510:1354`). Bits later OR-ed:
  `|0x80000` when in a team list (1618), `|1`⇒WakeUp + `[0x40].spare=0x40200000`.
- **Side opcodes** (single bytes, no payload): `0x08`,`0x0e`,`0x2b`,`0x2e`,
  `0x2f/0x31`,`0x30`,`0x32`,`0x42`. They set `local_6c8` (a tri-state:
  −1 default / 0 / 1) and distinct `EBX` bits (8,0x10,0x20,0x40,0x80,0x400).
  `local_6c8` then drives a `cObjectManager_getData_00603e30` + `FUN_0052e420`
  call with selector `uVar29 ∈ {2,3,7,0xe,0xe…}` (`00482510:1110-1162`) —
  this is the **team/AI-stance assignment** call. Group/team **id** is the
  separate `0x11` field (→ `FUN_004a9ef0`, `creature[0x49]`).
- **Hostility decision:** combat target selection reads `+0x1F4` side bits
  and the team handle; the exact "is enemy of hero" predicate was not
  isolated to a single decompiled function in this pass. The mapping
  *which* side-opcode = friend vs neutral vs enemy is **MED** and must be
  pinned at runtime (BP on `00482510:1354` write + observe behavior, or BP
  the `FUN_0052e420` stance call args). Working hypothesis from bit layout:
  default(no side op)=class-default (monsters hostile, townsfolk neutral);
  `0x08`/`local_6c8=1`→ally; `0x2f/0x31` (EBX 0x40)→forced hostile.

---

## Quest-NPC binding

- The engine identifies a quest NPC by `*(i16*)(cCreature+0x96) ==
  DAT_00aacf98` (the active quest id). Confirmed in the `QUESTNPC`/
  `QUESTFELLOW` resolvers (`00472bc0:138,165,277`) and quest/dialog
  binder `FUN_0046cbe0:222` (`creature+0x96` vs quest-context id).
- CreateNPC does **not** expose `+0x96` as a plain field. The link is
  established because the spawn is **registered into the quest manager's
  NPC array**: when the `0x01`/`0x05`/`0x29` name buffer is present,
  `00482510:1250-1286` copies a 0x44-byte record (with the new creature
  handle at `auStack_4d8[0]=local_68c`) into `*(…*)(in_ECX+0x35c)`. That
  array (base `in_ECX+0x31c`, stride **0x34** for the parallel index used by
  0x7a/0x29) is what dialog/quest records later resolve by name → giving the
  creature its quest identity. The `+0x96` stamp itself is applied by the
  quest-context path (QuestStateA `0x17`/`FUN_00478780`, which manages the
  `+0x334` store) — exact write site **needs a BP** (LOW-MED).
- **Minimal quest-NPC record set** (from vanilla patterns):
  1. `tag 0x01 CreateNPC`: `0x02`=Type, `0x01`="<UniqueName>",
     `0x04`/-2/"<DefPos or CPOS:…>", a side op, `0x12` (awake).
  2. `tag 0x03/0x1f DialogShow`: references the same `<UniqueName>` (the
     dialog dispatcher looks the creature up via the quest NPC array; the
     `DLGNPC`/`QUESTNPC` position token also resolves to it).
  3. Quest wiring: `tag 0x17 QuestStateA` / `0x35 QuestLogSet` /
     `0x44 HeroQBit_set` reference the quest id; the NPC participates
     because its array entry name matches what these records query, and its
     `+0x96` carries the quest id.

---

## NPC_FieldSet (0x7a) capabilities

`FUN_00475680` `case 0x7a:` (lines 1943-1964):

```
idx = *(i16*)(ctx + 0x94);                 // current NPC-array index
base= *(int*)(in_ECX + 0x31c);             // NPC array base, stride 0x34
guard: 0 <= idx < (*(in_ECX+0x320)-base)/0x34
loop op = FUN_00472bc0():
   if (op == 0x38)   *(u32*)(base + 0x18 + idx*0x34) = *(u32*)(in_ECX+0xa860);
   (op == 0x1c is explicitly skipped/no-op)
until op == 0
```

- **Only one writable slot via 0x7a in this build: NPC-array entry `+0x18`,
  written from wire field `0x38` (an i32 in the `0xa860` slot).** Entry+0x18
  is an NPC parameter the dialog/quest system reads (per the tag-table note
  "sets field +0x18 in NPC array entry"). HIGH on mechanism; the *semantic*
  of entry+0x18 (e.g. dialog-state / quest-step for that NPC) is MED — needs
  a BP to confirm what consumers read it.
- The target NPC is whichever entry the **enclosing statement** selected
  (`ctx+0x94` index) — i.e. 0x7a is used right after addressing an NPC by
  name (same array `+0x31c`/0x34 used by tag `0x29` ObjMgrGetData at
  `00475680:979`).

---

## Proposed Lua SDK API

(names + params only; each maps to a record/field emission)

```lua
-- 1. Spawn. opts: {name=, sub_id=, level=, side=, group=, stationary=,
--                   scale=, awake=true, invulnerable=, anim=}
handle = spawn_npc(type_id, pos, opts)
--  → emit tag 0x01:
--      0x02 i32 = type_id                              (Type)
--      if opts.sub_id:  2nd 0x02 i32 = opts.sub_id
--      if opts.name:    0x01 cstr = opts.name          (unique)
--      0x04 i32=-2 + ASCIIZ = pos                      (POSITION)
--         pos = "CPOS:HERO" | "<defpos_key>" | "CPOS:RES:<r>" | vx-int
--      if opts.level:   0x03 u16 = opts.level
--      if opts.group:   0x11 i32 = opts.group
--      if opts.scale:   0x36 i32 = float bits
--      if opts.stationary: 0x6b i32 = 1
--      if opts.invulnerable: 0xa1                      (flag)
--      side opcode from set_npc_faction map
--      if opts.awake ~= false: 0x12                    (flag → WakeUp)
--      0x00 (END)

-- 2. Faction. side ∈ {"enemy","ally","neutral"} (+ "default")
set_npc_faction(handle, side)
--  → CreateNPC: choose side opcode:
--      ally    → 0x08            (local_6c8=1)
--      enemy   → 0x2f  (EBX 0x40)            [MED — verify w/ BP]
--      neutral → 0x0e / none     (class default)
--  → runtime alt (post-spawn): write cCreature+0x1F4 directly
--      (om=*(0x00AD5C40); arr=*(om+4); c=*(arr+handle*4); *(u32*)(c+0x1F4)=bits)

-- 3. Quest NPC. binds spawned NPC to a quest id + optional dialog
make_quest_npc(handle, quest_id, dialog_name)
--  → ensure spawn used a unique 0x01 name == dialog_name
--  → emit tag 0x03/0x1f DialogShow referencing dialog_name
--  → emit tag 0x17 QuestStateA / 0x35 QuestLogSet for quest_id
--  → runtime alt: write *(i16*)(cCreature+0x96) = quest_id

-- 4. Misc props
set_npc_props(handle, {level=, rank=, group=, anim=, stationary=,
                       invulnerable=, scale=})
--  → level  : tag 0x01 op 0x03  OR runtime FUN_0044ddc0
--  → group  : tag 0x7a (field 0x38 → NPC-array+0x18)  OR 0x01 op 0x11
--  → anim   : 0x01 op 0x90 → cCreature+0x400(=[0x80])
--  → stationary: 0x01 op 0x6b → cCreature+0x2B3 bit8 ([0x56]+3|=8)
--  → invuln : 0x01 op 0xa1 → cCreature+0x10 bit 0x200000
```

---

## Open items needing a runtime breakpoint

1. **Side-bit → friend/neutral/enemy mapping (HIGH priority).** BP write
   `00482510:1354` (`mov [edi+0x1F4], ebx`) and the `FUN_0052e420` stance
   call (~`00482510:1149/0x4836aa`); spawn one NPC per side opcode
   (0x08/0x0e/0x2f/0x30/0x32) and observe hostility + read final +0x1F4.
2. **`cCreature+0x96` stamp site.** BP on writes to `creature+0x96` during a
   quest CreateNPC to confirm which tag/function sets the quest binding id.
3. **NPC-array entry `+0x18` semantics** (what 0x7a field 0x38 means to
   consumers): BP read of `(base+0x18+idx*0x34)` after a 0x7a record.
4. **2nd `0x02` field role** (sub/instance id vs name fragment): inspect
   `local_654`/`TStack_494` buffer contents at `00482510:721` for a record
   with two 0x02s (Vampiress @0x008372).
5. **`local_6c8` tri-state default (−1)** vs class default faction: confirm a
   no-side-opcode spawn inherits Balance.bin class hostility.
6. Confirm struct byte-offsets (`+0x1F4`, `+0x96`, `+0x2B3`) against a live
   `cCreature` (om=`*(0x00AD5C40)`, arr=`*(om+4)`, c=`*(arr+idx*4)`).
