# SacredSDK

A modding toolkit for **Sacred Gold** (Ascaron, 2004) — write mods in **Lua**
without modifying a single original game file.

[![Verified](https://img.shields.io/badge/verified-Steam%20build%202.0.2.28%20%282006--10--13%29-brightgreen)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()
[![Status](https://img.shields.io/badge/status-alpha-orange)]()
![MIU](https://img.shields.io/badge/made_in-Ukraine-ffd700?labelColor=0057b7)

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/V7V51ZQ1RH)

---

## What is it

SacredSDK is a DLL that Sacred loads at startup through the `ijl15.dll` proxy
slot — no patching of `Sacred.exe`, no edits to the game's data. Once it is up
it gives you three things:

1. **A Lua layer over the engine's own script format.** Your `.lua` files are
   compiled to the bytecode Sacred already speaks (`FunkCode.bin`) and served to
   the engine in place of the originals.
2. **A runtime**, so a mod can spawn NPCs, open dialogs, register quests, watch
   for kills, react to the hero walking somewhere — while the game runs.
3. **The engine's own verbs**, one Lua builder per script record, copied from
   records the shipped quests use. When your mod pays gold, moves an NPC, writes
   a journal line or closes a road, *the engine does it*, with its own sounds,
   its own popups and its own savegame.

The install stays bit-for-bit untouched: Steam's "Verify integrity" stays green,
and uninstalling is renaming one DLL back.

---

## Install

**From a release:** extract the archive into your Sacred Gold folder and run
`install.cmd`. It renames the game's own `ijl15.dll` to `ijl15_real.dll` and puts
the SDK proxy in its place; `uninstall.cmd` puts it back. Nothing else changes.

**From source:** build `SacredSDK.vcxproj` (Release | Win32, VS 2022 / v143),
then from the game root:

```cmd
copy ijl15.dll ijl15_real.dll        :: once: keep the original
copy sdk\Release\ijl15.dll ijl15.dll
```

---

## Your first mod

```lua
-- save as: <game>/custom/lua/bin/TYPE_NPC_GLADIATOR/FunkCode.lua
local T    = require "text"
local NPCo = require "npcobj"
local V    = require "vars"
local S    = require "sections"

T.named("KOLB_OFFER", "Brigands took the mill. Clear them out and I will pay.")

V.on_ready(function()                         -- the world is up (new game or save)
  local o = NPCo.spawn_template("quest_npc", { type = 257, pos = "CPOS:HERO" })
  if not o then return end
  o:teleport(2793, 2284)
  o:bind_quest("Sergeant Kolb", true)         -- a real dialog entry + the "!" marker
  o:dialog{
    text = "KOLB_OFFER",
    buttons = { { label = S.ACCEPT, on = function() require("reward").give_gold(500) end },
                { label = S.REJECT } },
  }
end)

return {}
```

Start Sacred and walk up to him. No bytecode, no offsets, no patched files.

Ready-made recipes for the usual questions — renaming an NPC, changing a reward,
reacting to a kill, closing a road — are in **[MODDING_COOKBOOK.md](MODDING_COOKBOOK.md)**.

---

## What it can do

Everything below has been watched working in game, not inferred from a
disassembly.

| | |
|---|---|
| **Quests** | Real entries in the engine's quest registry: SetUpQuest / TriggerQuest / ExitQuest drive them, and the engine writes the journal, the category, the compass column and the fanfares itself. Objectives use its own kill and pickup counters, which count, display "3 of 5" and survive a save. |
| **Dialog** | Your own nodes on your own NPCs: your text, up to four answer buttons, each button a Lua function. Quest markers over their heads, node switching, popups with no NPC at all. |
| **NPCs** | Spawn from templates (guard, merchant, smith, trainer, enemy, companion…), name them, level them, equip them, make them follow, fight, walk somewhere, play an animation, faint and get back up. A `persona` keeps a character — the same handle, the same name — across savegames and chapters. |
| **The world** | Chests with loot, clickable objects, map icons, item drops, particle marks, barriers that close a road, and rectangles on the ground that run your code when the hero walks in. |
| **Text** | New strings written into `global.res` at bake time, dialog lines swapped by name, the game's own on-screen banners. |
| **Vanilla scripts** | `sacred.disasm` decompiles a shipped `.bin` on the spot — 125,236 records out of 3.97 MB in 721 ms — so a mod can rewrite the game's own quests and bake them back byte-exactly, with nothing to prepare. |
| **Cut scenes** | Cinema mode, queued walks and animations, camera focus. |
| **State** | Engine variables, hero quest bits, and a savegame hook — a mod's progress is in the player's save, not in a sidecar file. |

The reference is the **[wiki](../../wiki)**: Installation, Writing Your First
Mod, Native Quests, Runtime NPCs, Dialog, Vanilla Verbs, World Objects, Zones and
Barriers, Cut Scenes, the Lua API.

---

## How the mod tree is laid out

Two trees, and the player's wins:

```
<Sacred Gold>/
├── ijl15.dll               ← the SDK proxy
├── ijl15_real.dll          ← the game's original, renamed
├── bin/, scripts/, …       ← vanilla, never touched
├── sdk/custom/lua/         ← THE FRAMEWORK (ships with the SDK)
│   ├── lib/                ← the standard library
│   └── examples/           ← copy-paste starters
└── custom/                 ← YOURS
    ├── lua/bin/…           ← your mods, mirroring the game's bin/
    ├── lua/lib/…           ← optional: your copy of a framework module
    ├── bin/                ← generated by the bake, served to the engine
    └── scripts/            ← generated (global.res with your new strings)
```

A file at `custom/lua/<path>` beats the one at `sdk/custom/lua/<path>` — mod for
mod, module for module. Baked output always lands in `custom/`, so updating the
SDK never touches your work.

---

## Repository

```
sdk/
├── *.cpp, *.inc, engine/, hooks/, ports/   the DLL
├── lua/                                    embedded Lua 5.4
├── custom/                                 the Lua framework that ships with it
├── packaging/                              install.cmd, release packaging, CI checks
├── re/py/                                  the FunkCode pipeline and RE tooling
├── wiki/                                   the documentation (submodule)
├── MODDING_GUIDE.md                        the model, end to end
└── MODDING_COOKBOOK.md                     "how do I ..." recipes
```

CI builds the DLL on every push, smoke-tests its exports, parses every Lua file
the SDK ships, and round-trips the FunkCode (de)compiler against a synthetic
corpus plus the DLL's own opcode table. A tag starting with `v` builds the
release archive and publishes it.

---

## Verified on

Steam Sacred Gold, build **2.0.2.28** (2006-10-13); the GOG build of the same
version is byte-identical where it matters. The DLL checks the build it attached
to and says so in `sdk/logs/sdk_loaded.log`.

---

## Non-goals

- Multiplayer mod scripting (the LAN protocol is understood in part, but nothing
  is built on it).
- Asset replacement (textures, models, sounds) — those are `.pak` / Granny
  formats; community tools handle them.
- EXE patching in the "ReBorn HD" sense. SacredSDK is a runtime DLL.

---

## Acknowledgements

- **Thorium** (2007 unofficial patch 2.29) — the `global.res` loader detour and
  the focus busy-wait fix, both ported to runtime here.
- **SonicMouse** (SacredGameTools) — save-file format, `TINCAT2.DLL` layout.
- **The Resacred remake project** — pak/keyx formats, item and tile structs.
- The broader Sacred modding community, for two decades of notes.

---

## License

MIT, see [LICENSE](LICENSE). This is fan modding work: no Ascaron / Encore code
or content is redistributed here or in the release archive.
