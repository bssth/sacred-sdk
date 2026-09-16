# SacredSDK cookbook

Short answers to "how do I …". Each recipe is a whole mod: save it as
`custom/lua/mods/<name>.lua` (make the folders), restart the game, done.
`MODDING_GUIDE.md` explains the model behind them, and the wiki is the reference.

These mods do their work while the game runs and end in `return {}`, an empty
list of script records. Keep them out of `custom/lua/bin/`: a file there bakes to
the same path under `custom/bin/`, and the game reads it in place of its own
script. A recipe saved as `custom/lua/bin/TYPE_NPC_GLADIATOR/FunkCode.lua` would
hand the game an empty `FunkCode.bin` for that class. `mods/` mirrors no game
file, so its output is never read in place of one.

Everything here has been run in game on Sacred Gold 2.0.2.28. Where the engine
does something surprising, the recipe says so instead of pretending.

**Contents**

1. [Rename an NPC](#1-rename-an-npc)
2. [Change what a quest pays out](#2-change-what-a-quest-pays-out)
3. [Add a line to an existing quest's journal entry](#3-add-a-line-to-an-existing-quests-journal-entry)
4. [React when the player kills a particular monster](#4-react-when-the-player-kills-a-particular-monster)
5. [A bonus that repeats on the game clock](#5-a-bonus-that-repeats-on-the-game-clock)
6. [Do something when the player walks somewhere](#6-do-something-when-the-player-walks-somewhere)
7. [Close a road, then open it](#7-close-a-road-then-open-it)
8. [A quest giver with your own dialog and buttons](#8-a-quest-giver-with-your-own-dialog-and-buttons)
9. [Remember something across save and load](#9-remember-something-across-save-and-load)
10. [Put a chest in the world](#10-put-a-chest-in-the-world)

---

## 1. Rename an NPC

Names come from the game's string table, so a new name is a new string: `T.named`
registers one at bake time and `o:set_name` hangs it on the creature.

```lua
local T    = require "text"
local NPCo = require "npcobj"
local V    = require "vars"

T.named("MY_GUARD_NAME", "Sergeant Kolb")       -- goes into global.res at bake

V.on_ready(function()
  local o = NPCo.spawn_template("friendly_town_guard",
                                { type = 257, pos = "CPOS:HERO" })
  if o then
    o:set_name("res:MY_GUARD_NAME")             -- the nameplate the player sees
    o:teleport(2793, 2284)
  end
end)

return {}
```

`set_name` works on any creature you hold a handle for. To rename one the game
placed, find it first — `sacred.scan_creatures()` walks what is loaded and gives
you handles and types.

---

## 2. Change what a quest pays out

Rewards are the engine's own: the gold pops up as the engine's "+N", the item goes
through the inventory code, and the random roll is the same one vanilla uses.

```lua
local R  = require "reward"
local Vb = require "verbs"
local A  = require "actions"

-- when your quest is handed in:
A.run(Vb.add_gold(1500))          -- "+1500" over the hero, the engine's popup
R.give_item(2001)                 -- straight into the backpack, by item type
A.run(Vb.add_exp(4000))           -- experience, through addExperience

-- or let the engine roll a reward the way a vanilla quest does:
A.run(Vb.gewinn(1, 12, 0))        -- (mask, level, tier) -> gold + XP + up to one item
```

`Vb.gewinn` scales with the hero and the region, which is why a shipped quest at
level 40 does not hand out a level-3 dagger. Hand-picking the item with
`R.give_item` is the way to give something specific.

---

## 3. Add a line to an existing quest's journal entry

The journal record works on **any** entry, including the game's own — it looks the
quest up by id. Sub 0 writes the title, anything else appends a body line (ten at
most).

```lua
local Vb = require "verbs"
local A  = require "actions"
local T  = require "text"
local V  = require "vars"

T.named("MY_EXTRA_LINE", "The smith mentioned a cave east of the mill.")

V.on_ready(function()
  A.run(Vb.quest_log(9511, 1, "res:MY_EXTRA_LINE"))   -- 9511 = the quest's id
end)

return {}
```

Quest ids are in the wiki's quest tables. Writing to a quest the player has not
started yet does nothing — there is no entry to append to.

---

## 4. React when the player kills a particular monster

Use the engine's own kill counter: it counts, shows its "3 of 5" message, keeps
the number in the savegame and runs your section at zero.

```lua
local O  = require "objectives"
local V  = require "vars"
local Vb = require "verbs"
local A  = require "actions"

O.declare("brigands_dead", function()
  sacred.log("all five are down")
  A.run(Vb.add_gold(500))
end)

V.on_ready(function()
  O.start_kills("brigands_dead", 304, 5)      -- 5 creatures of type 304 (Brigand)
end)

return {}
```

**Never point a counter at something that rises again.** A skeleton dies as one
type and comes back as another, so the count never reaches zero — the engine
itself skips the death of the "undead" class.

---

## 5. A bonus that repeats on the game clock

There is no wall clock, but the game clock is a real one: 24× faster than
reality, so a game minute is about 2.5 seconds and a game day about an hour of
play. `set_timer` fires a section once, and a section that re-arms it repeats.

```lua
local S  = require "sections"
local Vb = require "verbs"
local A  = require "actions"
local V  = require "vars"

local DAY = 1440                       -- game minutes
local ID  = Vb.SDK_TIMER + 1

S.define("daily_bonus")                -- an empty section: the Lua below is the body
sacred.on_trigger("SECTION:daily_bonus", function()
  local n = (V.get("MY_DAILY_COUNT") or 0) + 1
  V.set("MY_DAILY_COUNT", n)
  A.run(Vb.add_gold(250))
  sacred.log("daily bonus #" .. n)
  A.run(Vb.set_timer(ID, DAY, "daily_bonus"))        -- arm the next one
end)

V.on_ready(function()
  A.run(Vb.set_timer(ID, DAY, "daily_bonus"))
end)

return {}
```

Timers live in the savegame, but a timer whose target section is missing when it
fires is dropped — so define the section every launch, unconditionally, as above.

---

## 6. Do something when the player walks somewhere

```lua
local Z  = require "zones"
local Vb = require "verbs"
local T  = require "text"
local V  = require "vars"

T.named("AMBUSH_WARNING", "You hear bowstrings drawn in the trees.")

V.on_ready(function()
  Z.define("mill_ambush", {
    rect     = { 2799, 2278, 2811, 2290 },        -- x1, y1, x2, y2
    records  = { Vb.info("AMBUSH_WARNING") },     -- the engine shows this at the step
    once     = true,                              -- springs once, ever
    on_enter = function(z, who)
      sacred.log(who .. " stepped into the ambush")
      -- spawn the archers here
    end,
  }):arm()
end)

return {}
```

The raw engine trigger pulses **every heartbeat for every creature inside**;
`zones.lua` turns that into one event per entry. Arm zones on every world load —
they are not in the savegame.

---

## 7. Close a road, then open it

```lua
local Z = require "zones"
local V = require "vars"

V.on_ready(function()
  local gate = Z.barrier("north_gate", Z.line(2792, 2272, 2800, 2272, 2))
  gate:close()                                  -- closed and locked: nobody through
  -- later, when the quest allows it:
  -- gate:open()
end)

return {}
```

A barrier is invisible — the engine draws nothing at all. Mark it so the player
understands what stopped them: `Vb.map_icon(x, y, 2)`, a fence object, a guard.

---

## 8. A quest giver with your own dialog and buttons

```lua
local NPCo = require "npcobj"
local T    = require "text"
local S    = require "sections"
local V    = require "vars"

T.named("KOLB_OFFER", "Brigands took the mill. Clear them out and I will pay.")
T.named("KOLB_THANKS", "The mill is ours again. You have my thanks.")

V.on_ready(function()
  local o = NPCo.spawn_template("quest_npc", { type = 257, pos = "CPOS:HERO" })
  if not o then return end
  o:teleport(2793, 2284)
  o:bind_quest("Sergeant Kolb", true)           -- a real dialog entry + the "!" marker
  o:dialog{
    text = "KOLB_OFFER",
    buttons = {
      { label = S.ACCEPT, on = function(npc)
          npc:dialog{ text = "KOLB_THANKS" }    -- swap his node for the next one
          -- start your quest here
        end },
      { label = S.REJECT },                     -- no handler: just closes
    },
  }
end)

return {}
```

Four buttons show at most. `S.OK`, `S.ACCEPT` and `S.REJECT` are the game's own
labels; any `res:` key works, including one you made with `T.named`.

---

## 9. Remember something across save and load

Engine variables are saved with the game, so anything you keep there survives.

```lua
local V = require "vars"

V.on_ready(function(loaded)
  local step = V.get("MY_QUEST_STEP") or 0
  sacred.log(("world up (%s), my step is %d"):format(loaded and "savegame" or "new", step))
  V.set("MY_QUEST_STEP", step + 1)
end)

return {}
```

Two rules learned the hard way:

* **Adopt, never respawn.** A savegame brings your NPCs back with the same
  handles. Save the handle in a variable and re-wrap it (`persona.lua` does this
  for you) instead of spawning a second copy.
* `V.on_ready` is the only safe "the world is up" signal; a new game builds its
  world after the hero exists, and anything spawned in between lands in limbo.

---

## 10. Put a chest in the world

```lua
local W = require "world"
local V = require "vars"

V.on_ready(function()
  W.chest{
    x = 2795, y = 2286,
    type = W.CHEST.wood,
    gold = 300,                    -- paid once, remembered in the savegame
    spill = { 5134, 5134 },        -- and two piles thrown on the floor
    on_open = function() sacred.log("the chest was opened") end,
  }
end)

return {}
```

The engine's own `FillChest` does nothing for a chest the SDK created — that is
measured, not guessed. Use `gold` for an exact amount and `spill` for loot on the
floor, which is what a vanilla chest does anyway. A pile is worth what the engine
decides for the hero's level, so the type only changes how it looks.
