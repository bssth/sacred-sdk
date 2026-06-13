# SacredSDK

A modern modding toolkit for **Sacred Gold** (Ascaron, 2004) — write
mods in **Lua** without ever modifying the original game files.

[![Verified](https://img.shields.io/badge/verified-Steam%20build%202.0.2.28%20%282006--10--13%29-brightgreen)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()
[![Status](https://img.shields.io/badge/status-alpha-orange)]()
![MIU](https://img.shields.io/badge/made_in-Ukraine-ffd700?labelColor=0057b7)

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/V7V51ZQ1RH)

---

## What is it

SacredSDK is a DLL that Sacred loads at startup (via the `ijl15.dll`
proxy slot — no patching of `Sacred.exe`). On load it:

1. **Bakes Lua mods** at `custom/lua/**/*.lua` into Sacred's native
   bytecode (the `FunkCode.bin` format used by every quest, dialog,
   and creature in the game).
2. **Patches resource lookups** so any file Sacred opens can be
   transparently swapped for `custom/<same-path>`.
3. **Hooks the resource resolver** so Lua mods can react to game-time
   events (NPC dialog, kill banners, quest progress) and mutate hero
   state (gold, inventory).

The Steam install stays bit-for-bit untouched — mods live entirely
under `<game>/custom/`. Uninstall = delete that directory.

---

## What you can do today

| Capability | Status | Notes |
|---|---|---|
| **Reskin any vanilla quest's text** | ✅ stable | `T.named("HQ_3_2_1_Log_Title", "My new title")` |
| **Inline-string mods** | ✅ stable | `T"Some text"` auto-registers in `global.res` |
| **Per-class boot scripts** | ✅ stable | qbits, variables at hero spawn |
| **Dialog scenes from Lua** | ✅ stable | `d.trigger / d.line / d.emit` builders |
| **Native `has_item` predicate** | ✅ stable | bytecode-level inventory check |
| **Native `give_gold` opcode** | ✅ stable | bake-time gold ops |
| **Runtime callbacks** (`sacred.on_trigger`) | ✅ working | fires on resource queries |
| **`ctx:give_gold(N)`** in handlers | ✅ working | event-bus, plays coin sound + floating text |
| **`ctx:has_item(N)`** (equipment) | ✅ working | scans 18 wear slots |
| **`ctx:notify(text)`** | ✅ working | top-of-screen toast banner |
| **In-game overlay** | ✅ working | F11 capture / F12 hide / F10 snapshot |
| `ctx:has_item` (backpack) | 🟡 TODO | needs bag-array RE |
| `ctx:set_qbit` runtime | 🟡 stub | bake-time works |
| Native engine banner | 🟡 TODO | currently ImGui overlay toast |
| Brand-new quest at spawn | ❌ blocked | needs quest-book RE |

See [docs/community-refs.md](docs/community-refs.md) for the full
wishlist with effort estimates.

---

## Quick start

```lua
-- save as: <game>/custom/lua/bin/TYPE_NPC_SERAPHIM/FunkCode.lua
local T = require "text"
local v = require "vanilla"

-- Re-skin the Seraphim main quest into a custom narrative.
T.named("HQ_3_2_1_Log_Title",  "The Lost Tome of Ancaria")
T.named("HQ_3_2_1_Log_Header", "Chapter 1 — A Curious Heist")
T.named("HQ_3_2_1_Log_Qstart",
  "A young monk runs up to you in Bellevue, breathless. " ..
  "An ancient tome was stolen from the monastery library...")

return v.load "bin/TYPE_NPC_SERAPHIM/FunkCode"
```

Launch Sacred → start a new Seraphim → accept Leandra's quest → open
the journal. You'll see your new title and text where vanilla had
"River Pirates".

The full reference is in **[docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md)** —
read that first.

---

## Anatomy

```
<Sacred Gold install>/
├── ijl15.dll               ← SacredSDK proxy (replaces stock)
├── ijl15_real.dll          ← original, renamed
├── bin/, scripts/, …       ← vanilla, never touched
└── custom/
    ├── lua/
    │   ├── lib/            ← stdlib (quest / dialog / text / events / …)
    │   ├── examples/       ← copy-paste starters (01..07)
    │   └── bin/            ← YOUR mods live here, mirror of bin/
    │       └── TYPE_NPC_*/
    │           ├── QuestCode.lua
    │           └── FunkCode.lua
    └── bin/                ← auto-generated .bin files (served to Sacred)
```

There are three authoring layers (high → low):

1. **High level** — `lib/quest.lua`, `lib/dialog.lua`, `lib/text.lua`.
   One Lua line per quest primitive.
2. **Mid level** — `lib/funkcode.lua`, `lib/raw.lua`. Direct bytecode
   builders by opcode name.
3. **Low level** — `lib/unsafe.lua`, `raw.hex"…"`. Raw byte literals.

Round-trip is byte-perfect for 132/132 vanilla `.bin` files.

---

## Examples (in `custom/lua/examples/`)

| File | Demonstrates |
|---|---|
| `01_hello.lua` | minimal mod that declares state |
| `02_text_swap.lua` | bulk-rewrite vanilla via `gsub` |
| `03_dialog_block.lua` | author a dialog scene from scratch |
| `04_full_quest.lua` | small standalone quest |
| `05_conditional_dialog.lua` | native Sacred branching skeleton |
| `06_sidequest.lua` | **start here** — full side-quest template |
| `07_runtime_triggers.lua` | `sacred.on_trigger` patterns |

---

## Project status

**Alpha.** Core pipeline (Lua → bake → byte-identical bytecode) is
proven and stable. Runtime hooks are working but require knowing
which resource ids Sacred queries during your event of interest
(discovery is iterative; see `docs/MODDING_GUIDE.md` § "Runtime
trigger hooks").

Public surface is **`docs/MODDING_GUIDE.md`** and the `custom/lua/lib/`
modules. The C++ side (DLL implementation, hooks, RE scripts) is in
the same SCM repository but separately gated — see § "Repository
contents" below.

---

## Repository contents

```
sdk/
├── README.md               ← you are here
├── docs/
│   ├── MODDING_GUIDE.md    ← READ THIS FIRST
│   ├── README.md           ← guide to the rest of the doc set
│   ├── 01..22-*.md         ← RE journey (historical, technical)
│   ├── community-refs.md   ← mined intel from community RE tools
│   └── roadmap.md          ← where we are, where we're going
└── tools/
    ├── README.md           ← walkthrough: "I want to X → run Y"
    ├── *.py                ← FunkCode pipeline, globalres edits, quest
    │                          dumpers, sacred_hash, balance_diff, …
    ├── hash_names.csv      ← 23 123-entry hash dictionary
    ├── smoke_test_proxy.bat
    └── ghidra/             ← Java scripts for headless Ghidra RE
        └── README.md
```

The **DLL sources** (C++) and the **Lua stdlib bundled into the DLL**
ship in a separate distribution channel (binary release) and will be
opened in a later push when the public API surface stabilises.

If you just want to run mods today, the binary release ships with all
the runtime bits pre-built — clone this repo only if you want to
read, contribute to, or extend the documentation and the Python
tooling.

---

## Verified on

Steam Sacred Gold, build **2.0.2.28** (2006-10-13). The DLL doesn't
do version detection; other builds may work but have not been
tested. If you try one, please open an issue.

---

## Non-goals

- Multiplayer mod scripting (Sacred LAN protocol RE is on the
  wishlist but not started).
- Asset replacement (textures, models, sounds) — these are in
  `.pak`/Granny formats; see `docs/community-refs.md` for the
  community tools that handle them.
- "ReBorn HD"-style EXE patching — SacredSDK is a runtime DLL only,
  not a patch.

---

## Acknowledgements

- **Thorium** (2007 unofficial patch 2.29) — recovered global.res
  loader detour and focus-busy-wait fix that we ported to runtime.
- **SonicMouse** (SacredGameTools) — save-file format,
  `TINCAT2.DLL` networking layout.
- **The Resacred remake project** — pak/keyx format reverse
  engineering, item/tile structs.
- The broader Sacred modding community for two decades of
  reverse-engineering notes.

---

## License

MIT. See `LICENSE` (or fall back to standard MIT terms — this is
strictly fan modding work; no Ascaron/Encore IP is redistributed).
