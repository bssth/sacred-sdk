# `custom/` — override layer

Drop replacement files here mirroring the Sacred install tree.  The
SacredSDK DLL (`ijl15.dll`) hooks `CreateFileA` and transparently swaps
any read pointing at `<game_dir>\<sub>\<file>` for `<game_dir>\custom\<sub>\<file>`
when the override exists.

**Two trees, and the player's wins.** This copy of the tree is the one that
ships with the SDK, at `<game_dir>\sdk\custom\`: the standard library under
`lua/lib/` and the starter mods under `lua/examples/`. A player's own tree lives
at `<game_dir>\custom\`, and for every relative path — a mod, a library module,
an overridden game file — the player's file is used and this one is skipped.
Everything the bake *writes* goes to the player's tree, so this one stays exactly
as shipped and an SDK update never overwrites anyone's work.

## Examples

```
custom\
├── scripts\
│   └── us\
│       └── global.res                  ← localised text mod
├── bin\
│   ├── Balance.bin                     ← stats / item rebalance
│   └── TYPE_NPC_GLADIATOR\
│       └── FunkCode.bin                ← edited quest scripts
```

That's it. No registry, no settings file. Sacred reads the override on
the next launch without knowing anything changed.

## What is and isn't redirected

✅ Anything inside subdirectories of the game install opened read-only.
✅ Both relative paths (`scripts\us\global.res`, `.\bin\Balance.bin`) and
   absolute paths inside the game dir.

❌ The game executable, our DLL, and other top-level files. Sacred never
   re-reads those at runtime anyway.
❌ Write opens (save games, logs). Always go to vanilla.
❌ Paths containing `..\` (sandbox safety).
❌ Anything outside the game install dir.

## Editing helpers

`sdk/tools/globalres_modify.py` writes its output into
`custom/scripts/us/global.res` by default (cloning the vanilla file on
first run). Pure text-mods need nothing more.

For raw byte mods, just copy the vanilla file:

```cmd
xcopy /Y bin\Balance.bin custom\bin\Balance.bin
```

Then edit `custom\bin\Balance.bin` directly.

## Status from the overlay

The `Custom/ overrides` panel in the in-game ImGui overlay shows:

- `CreateFileA opens` — total file-open calls Sacred has made.
- `redirected` — how many actually resolved to `custom\` files.
- `last` — the most recently redirected path.

If you drop a file here and the counter doesn't tick when Sacred should
have loaded it, double-check the path matches the install tree exactly
(case is forgiving, but the directory structure must mirror).
