# `_vanilla/` — optional snapshots of the game's own scripts

**You do not need anything in here.** Since `sacred.disasm` landed, `vanilla.load`
decompiles the game's own `.bin` on the spot, from the player's own install:

```lua
local v = require "vanilla"
local recs = v.load "bin/TYPE_NPC_SERAPHIM/FunkCode"   -- nothing to prepare
v.gsub_strings(recs, "_sera_", "_glad_")
return recs                                            -- baked over the original
```

Measured on a whole shipped script: 125,236 records out of 3.97 MB in **721 ms**,
and baking them straight back produced a file byte-identical to the original.

## What this directory is for, then

A snapshot is a decompiled script written out as Lua — readable, greppable and
**editable**. Put one here and `vanilla.load` prefers it over the live decode, so
this is how you keep hand edits:

```
_vanilla/bin/TYPE_NPC_SERAPHIM/FunkCode.lua     <- from bin/TYPE_NPC_SERAPHIM/FunkCode.bin
```

Make one from your own install, whenever you want to read or hand-edit a script:

```bat
cd <Sacred Gold>
python sdk\re\py\funkcode_decompile_lua.py bin\TYPE_NPC_SERAPHIM\FunkCode.bin ^
       -o custom\lua\_vanilla\bin\TYPE_NPC_SERAPHIM\FunkCode.lua
```

Either tree works: your own `custom/lua/_vanilla/...` is searched first, then the
SDK's. Keep them in `custom/` so an SDK update never touches them.

**They are not shipped, and never will be.** A snapshot is Ascaron's own quest
script in another form — twenty megabytes of it — so the SDK does not
redistribute one, exactly as it ships no game `.exe` and no decompiled engine
code. The live decode needs none, because the bytes come from the player's own
copy of the game.

## What needs a script at all

Only the bake-time path — a mod that reads a whole shipped script, rewrites parts
of it and returns the result to be baked over the original. Everything else in
the SDK touches no vanilla script: `npcobj`, `zones`, `nativequest`, `verbs`,
`sections`, `world`, `persona`, `text` all add to the world at runtime, which is
also why they survive a patch to the game.

If a mod should work either way, ask first:

```lua
if v.have "bin/TYPE_NPC_SERAPHIM/FunkCode" then
  ...                                   -- rewrite the shipped script
else
  sacred.log("no vanilla script available -- running the runtime path instead")
end
```
