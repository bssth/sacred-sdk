# `_vanilla/` — decompiled snapshots of the game's own scripts

This directory is where `lib/vanilla.lua` looks for a Lua rendering of a shipped
`.bin` script:

```
_vanilla/bin/TYPE_NPC_SERAPHIM/FunkCode.lua     <- from bin/TYPE_NPC_SERAPHIM/FunkCode.bin
_vanilla/bin/TYPE_NPC_VAMPIRELADY/FunkCode.lua
_vanilla/bin/Addon/NetScript/FunkCode.lua
```

**It ships empty on purpose.** Those files are Ascaron's own quest scripts in
another form — twenty megabytes each — so the SDK neither redistributes them nor
keeps them in its repository, exactly as it does not ship the game's `.exe` or
its decompiled functions. Make the ones you need from your own install; it takes
a few seconds each and never changes the game files.

```bat
cd <Sacred Gold>
python sdk\re\py\funkcode_decompile_lua.py bin\TYPE_NPC_SERAPHIM\FunkCode.bin ^
       -o custom\lua\_vanilla\bin\TYPE_NPC_SERAPHIM\FunkCode.lua
```

Either tree works: your own `custom/lua/_vanilla/...` is searched first, then the
SDK's. Put them in `custom/` if you want them to survive an SDK update.

## What needs them, and what does not

Only `vanilla.load(rel)` — the bake-time path, where a mod reads a whole shipped
script, rewrites parts of it and returns the result to be baked over the
original:

```lua
local v = require "vanilla"
local recs = v.load "bin/TYPE_NPC_SERAPHIM/FunkCode"
v.gsub_strings(recs, "_sera_", "_glad_")
return recs
```

Everything else in the SDK works without a single snapshot: `require "vanilla"`
itself, and every runtime library — `npcobj`, `zones`, `nativequest`, `verbs`,
`sections`, `world`, `persona`, `text`. Modern mods add to the world at runtime
instead of rewriting shipped scripts, which is also why they survive a patch to
the game.

If you want a mod to work either way, ask first:

```lua
if v.have "bin/TYPE_NPC_SERAPHIM/FunkCode" then
  ...                                   -- rewrite the shipped script
else
  sacred.log("no vanilla snapshot -- running the runtime path instead")
end
```
