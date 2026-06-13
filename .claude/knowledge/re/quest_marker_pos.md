# Quest marker pos + secondary-style marker — 2026-05-16

cQuestMgr @ **0x00AACF80**. Registry DAT_00aad3a4/3a8, stride 0x174,
key quest_id@+8. Active class `C = *(int*)(FUN_007d84a0()+0x14)` (1..16).

---

## Q1 — Hero position in marker / KompassPos space

**Authoritative finding (HIGH).** The marker/KompassPos space is the
**active-hero cCreature object**, fields **+0x1C = X**, **+0x20 = Y**
(u32, the 0..~6300 tile space). Proof chain, all static:

- `FUN_004a5980` resolves the reference ("you are here") via
  `iVar6 = cObjectManager_getData_00603e30()` then uses
  `*(iVar6+0x1c)` / `*(iVar6+0x20)` as hero X/Y (lines 159-160, 376-378).
- The world-map blip (`FUN_006ccbd0` lines 1086-1104, 1307-1308) draws
  the hero from the **same** `getData_00603e30()+0x1c/+0x20`, then runs
  it through `FUN_00622620` (world→map-pixel rotation/scale, matrix
  `_DAT_00ad7790/94/a0/a4`, common `_DAT_00ad7788`).
- A quest target object marker reads the **target's** `+0x1c/+0x20`
  (FUN_004a5980 line 132-133, `+0x10==-1` branch). Literal-coord quests
  feed `entry+0x10/+0x14` straight in (line 147) — i.e. **KompassPos
  values ARE in the cCreature +0x1c/+0x20 space**, no scaling.
- `cObjectManager_getData_00603e30` (__thiscall): reads
  `FUN_007d84a0()+0x14` (active hero **index** 1..16), returns
  `objMgr[+4][index*4]` cast via cObject/cCreature RTTI. ECX = the
  cObjectManager singleton (a global the decompiler elides).

**FUN_006224b0 / FUN_006224e0 are NOT coordinate converters.** They
are 14-byte-record field copies (u16 mapId@+0, X@+2, Y@+4, u8 flag@+6;
006224e0 float→int via __ftol). The "transform" lives in **FUN_00622620
/ FUN_00622660** (world↔screen), used only for drawing, not for the
distance check.

**Resolution of the 11.6 / 23.7 ratio mystery (HIGH).** Your chain
`[[[0x006D5C40]+4]+4]+0x3AC` (→ 72885/149532) is a *different,
finer-grained world field* (sub-tile units), NOT the cCreature
+0x1c/+0x20 the markers use. The ratios are non-uniform precisely
because it is a different field, not a scaling of the same one. Do not
try to scale it; read the engine's own field instead.

**Recipe — `hero_pos()` in KompassPos space:**
```
om   = <cObjectManager singleton ptr>          // see BP note
ctx  = read_u32(0x0182EBE8)                    // FUN_007d84a0 singleton
idx  = read_u32(ctx + 0x14)                    // active hero index 1..16
hero = read_u32( read_u32(om + 4) + idx*4 )    // cCreature object
hx   = read_u32(hero + 0x1C)   // == quest KompassPos X (0..~6300)
hy   = read_u32(hero + 0x20)   // == quest KompassPos Y
```
Distance check vs a quest's marker: compare `(hx,hy)` to
`entry+0x10/+0x14` (KompassPos literal) or slot-3 `cQuestMgr+0x7704/
+0x7708` — same units, plain Euclidean.

**RUNTIME-BP REQUIRED:** the cObjectManager singleton static address is
not exposed in the decompile (ECX elided). Get it once: BP on
`0x00603E30` entry, capture ECX, log it; or BP after world load and
read it from the call site. Confidence: field offsets +0x1c/+0x20 and
the index path **HIGH**; the static `om` base **MEDIUM until BP**.
Simplest robust alternative: call `cObjectManager_getData_00603e30`
itself (__thiscall, ECX=om) and read +0x1c/+0x20 from EAX.

---

## Q2 — Secondary-style marker for quest_id > 100

**The contradiction is resolved (HIGH).** Two independent per-class
slots in `FUN_004a5980`:

| slot | source field | id gate | style/caller |
|---|---|---|---|
| 0 | `cQuestMgr+0x3a4 + C*8` (`DAT_00aad31c[C*2]`), set by tag-0x75 FUN_0048d930 | `entry+8 **≤100**` AND `+4<99` | PRIMARY/class (minimap GREEN `0xff00ff00`) |
| 1 | `cQuestMgr+0x3a0 + C*8` (`DAT_00aad320[C*8]`), set by **FUN_0049dab0** | `entry+8 **≥100**` AND `+4<99` AND NOT `8999<+8<0x251c` | **SECONDARY / side-quest** |
| 2 | `cQuestMgr+0x7674..0x7680`, set by FUN_004a63b0 | none | world-map pin (RED) |
| 3 | `cQuestMgr+0x7704..0x771c`, set by FUN_0048c860 | `+0x7718!=0` | single tracked target (WHITE) |

So vanilla side quests (id>100) are NOT slot 0 — they use **slot 1**,
whose gate *requires* `id≥100`. Slot 0's `id≤100` is the *primary*
class-quest path; that is why id>100 never gets the green primary
marker but DOES get the secondary one. No contradiction: different slot.

**Style/colour selection (HIGH).** `FUN_004a5980`'s `*param_4`
(outColor) is left at init `0xFF000000` for slots 0/2/3 (only slot-1's
priority/range branch, lines 187-194, and the tag-0x3d special at line
284 touch it). **The marker colour/icon is chosen by the CALLER, per
slot index, not by the entry:**
- Minimap `FUN_006e3f70`: slot0 `0xffff0000`, **slot1 `0xff00ff00`**,
  slot2 `0xff0000ff`, slot3 `0xffffffff` (written to in_ECX+0xac8/
  0xae4/0xb00/0xb1c respectively).
- World map `FUN_006ccbd0`: per-slot sprite from the indexed table at
  `&DAT_009e124c` (loop `iVar10=0..3`, `puVar20=&DAT_009e124c++`),
  directional sprites `DAT_009f1b78/1bcc/1c20/1c74`.
The slot index alone selects primary-vs-secondary visuals.

**FUN_0049dab0** (`__thiscall`, ECX=cQuestMgr, 1 arg = entry **index**):
writes `cQuestMgr+0x3a0 + C*8 = index` (falls back to `+0x3a8` if class
out of range). Mirror of slot-0's FUN_0048d930 but for the slot-1
column. FUN_0049da50 reads it back.

**Recipe — make 9512 render SECONDARY (side-quest) style:**
1. Ensure `entry+8 = 9512`, `entry+4 < 99` (type/state; 0 works).
2. Set the literal-coord marker source on the entry (same as slot-0
   recipe): `entry+0x10 = worldX (0..~6300)`, `entry+0x14 = worldY`,
   `entry+0x20 = 0` (priority/range). (0x10 must NOT be -1 or
   0xEEEEEEEE — those are object-follow modes.)
3. Register the entry **index** in the slot-1 column:
   `*(u32*)(0x00AACF80 + 0x3a0 + C*8) = idx;`  (or call FUN_0049dab0,
   __thiscall ECX=0x00AACF80, arg=idx).
4. 9512 passes the slot-1 gate: `9512 ≥ 100` ✔, `+4<99` ✔, and
   `8999 < 9512 < 0x251c(=9500)` is **false** (9512 > 9500) so the
   exclusion does NOT fire ✔. → slot 1 resolves → minimap shows the
   secondary (non-primary) blip, world map shows the secondary sprite.

This replaces the current slot-3 (white, primary-looking) usage. Slot 3
should be cleared (`cQuestMgr+0x7704=0, +0x7718=0`) so it doesn't also
draw.

**Confidence:** slot mapping & gates **HIGH** (direct decompile of
FUN_004a5980 + FUN_006e3f70 + FUN_0049dab0). The exact world-map
sprite per slot from `&DAT_009e124c` **MEDIUM** (table not dumped) —
but the *primary-vs-secondary distinction is the slot index*, which is
HIGH. **RUNTIME-BP to confirm 9512 actually lights slot 1:** BP
`0x004A5980`, param_1(slot)==1, check it returns 1 and outU/outV sane;
and that `+0x3a0+C*8` holds your idx. If the band check surprises,
dump `entry+8` and `entry+4` there.
