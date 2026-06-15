# SacredStudio — plan & next steps

WPF editor that places NPCs / points / zones / items on the Sacred world and emits
runtime Lua (`npcobj` + `sacred.*`, see `../docs/24-storyline.md`). Runtime replay
only — never edits `.bin`. Coordinate space, archetypes, architecture: see
`../CLAUDE.md` ("Active subproject: SacredStudio").

## Current state (works)
- Builds 0/0, runs. 3-pane UI: tools+list / iso map / properties+Lua preview.
- Place NPC/Point/Zone/Item by clicking; edit in properties; live two-way binding
  moves markers. Save/Load `*.sstudio.json`; Export `init.lua`.
- **REAL terrain backdrop (DONE 2026-06-13):** `MapBackdrop.cs` loads the Python
  viewer's baked LOD16 chunks (2048² PNG; chunk (cx,cy) → iso rect [cx·32768..]²,
  factor 16; same iso as the editor → aligns 1:1 with markers). `IsoMapView`
  draws visible chunks (viewport cull + bitmap cache) under the grid, `FitToMap()`
  frames the world on load. Source dir: bundled `Assets/map` else the viewer
  cache `E:\sacred_viewer\…\lod16`. Toolbar: Fit map / Terrain / Grid toggles.
- **GUI polished** (MainWindow.xaml restyle): header bar w/ logo, pill tool
  toggles, legend, card panels, styled TextBox/Button/Tabs/ListBox, dark+gold theme.
- ⚠️ Backdrop loads SYNCHRONOUSLY on first paint → a one-time multi-second freeze
  at fit-to-map (all ~113 chunks). NEXT: async chunk load + InvalidateVisual on
  ready (or bundle downscaled chunks into Assets/map to shrink decode time).

## How to run / build
```
& "E:\dotnet\dotnet.exe" build "…\sdk\SacredStudio\SacredStudio.sln"
& "E:\dotnet\dotnet.exe" run  --project "…\sdk\SacredStudio\SacredStudio.csproj"
```
Rider: open `SacredStudio.sln`; .NET CLI path must be `E:\dotnet\dotnet.exe`
(see `../CLAUDE.md` for the resolver-manifest fix — do not lose it).

## Next steps (priority order)

### 1. Verify the codegen against the real runtime
- Cross-check emitted Lua (`N.spawn_template`, `:teleport/:stance/:equip/:level/
  :make_quest_giver`, `sacred.spawn_item`, `M.in_zone`) against the actual
  `custom/lua/lib/*.lua` (npcobj, npc_templates) — confirm method names, arg order,
  slot constants (0x0D = main weapon), and the `M.apply()` invocation contract
  (which DLL runtime hook calls it, and when — hero spawn / map load?).
- Wire one generated mod end-to-end in-game; fix any signature drift.

### 2. Real map backdrop (biggest UX win)
- Today you click on an empty grid. Options, cheapest first:
  - (a) Load the baked **LOD16 overview** from the Python viewer's E: cache as a
    static backdrop image under the iso grid (need to export it to PNG first — the
    viewer stores it pickled, so add a tiny export, or re-bake to PNG).
  - (b) Live bridge: have the Python viewer expose hover/camera over a local socket
    and overlay the C# editor — more work, higher fidelity.
  - (c) Full GL terrain in C# (OpenTK `GLControl` in `WindowsFormsHost`) — most work;
    only if (a)/(b) prove insufficient.
- Keep `IsoMapView` transforms (`IsoToScreen`/`ScreenToWorld`) as the single source
  of truth so any backdrop aligns with markers.

### 3. Type palettes instead of raw ids
- NPC type dropdown from `npc.lua` (TYPE_* table); item type dropdown from
  `items_gen.lua` (5624 items). Parse those into a side JSON the editor loads, so
  users pick names (emitted as `NPC.X` / `ITEM.X`) instead of integer ids.

### 4. Editing ergonomics
- Drag to move markers; drag-rectangle to draw zones (currently fixed 4×4).
- Multi-select + box-select; copy/paste; undo/redo (command stack on `AppState`).
- Snap-to-tile already implicit; add grid-snap toggle + coordinate entry jump.

### 5. Project ↔ deployment
- "Deploy" button: write `init.lua` straight into `custom/lua/<ModName>/` and
  trigger the DLL's lua rebake (doc 14/15 overlay "Rebake all .lua") for hot reload.
- Validate before emit (dupe names, out-of-world coords, empty required fields).

### 6. SDK integration (later)
- If the editor proves useful, extract the world parsers (KEYX/WLDX → sector/tile)
  into `sdk/tools/world/` so the editor (and other tools) read real world data
  instead of relying on the Python viewer. Tracked loosely in `../docs/TOOLS_PLAN.md`.

## Open questions to resolve with the runtime
- Exact `M.apply()` trigger hook + signature (ctx args?).
- Sub-id semantics per archetype; which deltas are safe at runtime vs need spawn-time.
- Zone representation the runtime wants (poly vs AABB) — currently AABB + `in_zone`.

## Housekeeping
- Throwaway: `E:\evalprobe`, `E:\dotnet-install.ps1` — delete once the toolchain is
  confirmed stable.
- VS Installer Repair of the ".NET SDK" component would restore VS builds too
  (needs C: space). Not required for Rider workflow.
