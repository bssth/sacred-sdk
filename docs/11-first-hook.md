# First real hook — Player Inspector (read-only)

This is the "training wheels" hook: we never patch instructions, only read
memory via known offsets. If the chain doesn't resolve (e.g. main menu, no
character loaded) we render a greyed `(no player loaded)` row — no crash.

## Anatomy of a CE pointer chain

CE's offset list is applied bottom-up. For the Health entry:

```
Address  : "Sacred.exe"+006D5C40
Offsets  : 4D8, 3AC, 4, 4
```

means

```
p = *(uintptr_t*)(image_base + 0x6D5C40)
p = *(uintptr_t*)(p + 4)        ← last offset, applied first
p = *(uintptr_t*)(p + 4)
p = *(uintptr_t*)(p + 0x3AC)    ← struct base
hp = *(int32_t*)(p + 0x4D8)     ← top offset is the FIELD READ, not a deref
```

For all other CT entries with the same chain shape, only the top offset
(the field offset) changes. We resolve the struct base once and read each
field directly from `base + field_offset`.

## Player struct field map (from CT)

All offsets relative to the resolved struct base.

| Offset | Type | Field |
|-------:|------|-------|
| `+0x010` | u16 | class id (1=Seraphim, 2=Gladiator, …, 9=Daemon) |
| `+0x1A4` | u32 | helmet |
| `+0x1A8` | u32 | cuirass |
| `+0x1AC` | u32 | belt |
| `+0x1B0` | u32 | boots |
| `+0x1B4` | u32 | gauntlets |
| `+0x1B8` | u32 | bracers |
| `+0x1BC` | u32 | amulet 1 |
| `+0x1C0` | u32 | amulet 2 |
| `+0x1C4` | u32 | ring 1 |
| `+0x1C8` | u32 | ring 2 |
| `+0x1CC` | u32 | ring 3 |
| `+0x1D0` | u32 | ring 4 |
| `+0x1D4` | u32 | active weapon left |
| `+0x1D8` | u32 | active weapon right |
| `+0x1DC` | u32 | cannon |
| `+0x1E0` | u32 | shoulder plates |
| `+0x1E4` | u32 | greaves |
| `+0x1E8` | u32 | wings |
| `+0x3CC..+0x3D3` | byte ×8 | skill type ids |
| `+0x3CC` | byte | skill slot 1 |
| ... | ... | ... |
| `+0x3D3` | byte | skill slot 8 |
| `+0x4D8` | i32 | current health |

Skill ids 1..33 mapped to names in `player_state.cpp::SKILL_NAMES`.

## Why it's not a "hook" yet — and why that's OK

This module never patches code; it just walks live memory. It is the
**foundation** the actual hooks will rely on:

1. Verifies the SDK can resolve real game state stably (validates our DLL
   layout, CT data, and the no-ASLR assumption).
2. Gives instant visual feedback: any future hook that *modifies* state will
   show its effect in this panel within one frame.
3. Establishes the pattern for read-only inspector panels (NPCs nearby,
   spawned creatures, world position, etc.) — all of which use the same CT
   methodology.

The next step — a proper code hook — will be MinHook on
`FUN_0080eaf0` (the resource dictionary lookup at va:0x0080EAF0) so we can:
- Log every (hash, returned_text) pair the game ever asks for, dramatically
  expanding our `hash_names.csv`.
- Inject replacement strings by returning a fake entry for chosen hashes
  (text-mod without repacking `global.res`).

## Build

```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  "E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\SacredSDK.vcxproj" ^
  -p:Configuration=Release -p:Platform=Win32
copy /Y "...\sdk\Release\ijl15.dll" "...\Sacred Gold\ijl15.dll"
```

## Files added

- `sdk/player_state.cpp` — chain resolver + `Snapshot::read()` + name tables
- `sdk/sdk.h` — added `sdk::player` namespace declarations
- `sdk/overlay.cpp` — new `Player (read-only)` CollapsingHeader
- `sdk/SacredSDK.vcxproj` — registered `player_state.cpp`

## Side discoveries this session

- **`hash(str(int_id))` is the bit-31-clear lookup transform.** All 823
  community names.csv entries (ids 17000..17822) match `global.res` under
  `sacred_hash(str(id))`. So `FUN_0080e780(numeric_id)` decimal-stringifies
  before hashing. Helper `sacred_hash.hash_for_id(int)` added.
- **Combat-art ids are bitfield-shifted indices**, not hashes (multiples of
  `0x10000`). They're a FunkCode payload type, not a global.res key.
- **Character-class id is the high byte of a u16** in the NPC struct; the
  player struct stores only the index (1..9) in u16 form.
