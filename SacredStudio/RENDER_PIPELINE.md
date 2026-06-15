# Sacred map render pipeline — for the native C# GL port

Mapped from `E:\sacred_viewer\viewer.py` (working Python/GL renderer). Strategy:
**offline Python exports art+geometry → C# GL renders it** (no Python at runtime).
All multi-byte reads little-endian. Game data: `<game>\Pak\{tiles,texture,mixed,ITEMS}.pak`,
`<game>\World\{Static,Floor}.pak`, `<game>\World\sectors.{keyx,wldx}`.

## PAK container (all .pak share this)
- Header 0x100; entry **count @ +4** (u32, fallback u16 by `(size-0x100)/0x0C`).
- Descriptor table @0x100, `count`×0x0C: `+0x00`u32 type, `+0x04`u32 payload off (abs),
  `+0x08`u32 payload size. Index 0 = null sentinel (STATIC/FLOOR/ITEMS).

## texture.pak (the pixels) — `Pak/texture.pak`
Per record: embedded header at payload `+0x00`: name(0x20 cstr), `+0x20`u16 W, `+0x22`u16 H,
`+0x24`u8 **type**. Pixels at **payload+0x50** for `size` bytes.
- type 6 = raw **BGRA8888** (swizzle→RGBA).
- type 4 = **zlib-compressed then ARGB4444** (16bpp LE): R=((v>>8)&0xF)*17, G=((v>>4)&0xF)*17,
  B=(v&0xF)*17, A=((v>>12)&0xF)*17. No RLE, no colorkey (black is valid; alpha from format).

## tiles.pak (tile def, no pixels) — `Pak/tiles.pak`
Per record payload: `+0x00` texture filename (0x20 cstr, → texture.pak), `+0x24`u32 **tile_number**
(sub-tile 0..17). `TilesPak.get(tile_id)` → {filename, tile_number}.

## Terrain tile → texture
tile_id → tiles.pak → (sheet filename, tile_number); subtile = tile_number % 18 indexes
`TILE_POSITIONS` (18 [x,y]: x=col*104+(odd row?52:0), y=row*25) → crop **100×50** from sheet.
Many tile_ids share a sheet, differ by subtile.

## KEYX / WLDX sectors
KEYX entry 0x300: `+0x024`u32 sector_id; `+0x3C/+0x40`s32 abs X/Y; `+0x0D4`u32 tiles_rel,
`+0x0D8`u32 tiles_size; `+0x0EC`u32 comp_off, `+0x0F0`u32 comp_size (into sectors.wldx);
`+0x2E0/+0x2E1`u8 liquid styles. origin=round((raw+0x19)*scale/64)*64, scale=min adjacent
spacing; grid=origin/64.  load_sector: read comp_off/size from wldx → **zlib.decompress** →
slice [tiles_rel:+tiles_size] = tile block (64×64 tiles × 0x20 = 131072 B, row-major lx fastest).

### Tile descriptor (0x20 bytes), tile (lx,ly) @ (ly*64+lx)*0x20
- `+0x00`u32 **terrain tile_id**
- `+0x04`u32 STATIC chain head; `+0x0C`u32 FLOOR chain head
- `+0x10,+0x11,+0x13,+0x12` s8×4 liquid corner raw (NOTE 0x13 before 0x12)
- `+0x14..+0x17` u8×4 **corner tints** (L,T,R,B)
- `+0x18..+0x1B` s8×4 **corner heights** (×2.5 → px; reference renderer keeps FLAT — optional)
- `+0x1F`u8 liquid surface type (&0xF0 = 0x90/0xA0 selector)

### Static records (World/Static.pak, 0x40 each; walk tile +0x04 chains)
`+0x04`u32 type_id (→ITEMS.PAK), `+0x08`u32 flags (&0x290 ⇒ skip), `+0x0C`u16 sector_id,
`+0x0E`s32 **projected_x**, `+0x12`s32 **projected_y** (already iso-pixel anchor — do NOT
world_to_iso), `+0x1F`u32 next_static_id, `+0x2B`s16 surface_render_layer.
type_id → ITEMS.PAK(0x80 rec) `+0x10` mixed_base_group_id → MixedPak2D group.

### FLOOR records (World/Floor.pak, 0x10 each; walk tile +0x0C chains)
`+0x04`u32 ref: low17=tile_id_a, high15=tile_id_b (blend); `+0x0C`u32 next. Resolve tile_ids
like terrain; blend = a's RGB + b's alpha.

## MIXED sprite (static art) — `Pak/mixed.pak`
group = list of 0x40 pieces: name(0x20 atlas) + `+0x04`u16 right,`+0x06`u16 bottom,`+0x08`s16
left,`+0x0A`s16 top (dest rect), `+0x10..+0x1C` 4×f32 UV (src rect 0..1 in atlas). Compose:
crop atlas UV→scale to dest→blit, bbox-track. **anchor=(-min_x,-min_y)**. → {w,h,anchor_x,anchor_y,RGBA}.

## Render geometry (C# does this)
world_to_iso(wx,wy)=((wx-wy)*48,(wx+wy)*24) = top-left of 96×48 cell.
**Terrain diamond** (per tile): center=(iso_x+48, iso_y+24); 4 corners at ±48.2 / ±24.2
(`ENGINE_FLOOR_RENDER_HALF_W/H`=48.2/24.2 — oversized to avoid seams). TRIANGLE_FAN: center,
top,right,bottom,left,top. Corner UVs: map `ENGINE_DIAMOND_UV_PIXELS` {left(2.512,24.012),
top(50.512,1.012), bottom(50.0,48.512), right(98.012,23.5)} within the 100×50 crop rect →
atlas UV. Corner color = tint/255 (grayscale), center = avg. Heights optional (subtract per
corner Y, higher=smaller screen-Y).
**Statics**: billboard quad (axis-aligned, NOT iso). foot_iso=(projected_x+47.8, projected_y-0.3);
screen = pan + foot_iso*zoom; top-left = foot - anchor*zoom; size = w*zoom×h*zoom. NEAREST filter.
**Draw order** (painter): terrain (sort wx+wy then wy) → FLOOR → liquids → statics (5 queues
q0..q4 + wx+wy). GL: RGBA8, CLAMP_TO_EDGE, NEAREST; blend SRC_ALPHA,ONE_MINUS_SRC_ALPHA.

## Export plan (offline Python, reuse viewer loaders; SDL_VIDEODRIVER=dummy)
GLOBAL: terrain_atlas.png + terrain_index.json (tile_id→{page,x,y,100,50}); sprite_atlas.png +
sprite_index.json (group_id→{page,x,y,w,h,anchor_x,anchor_y}).
PER SECTOR: {sector_id, origin_x, origin_y, tiles:[{texture_id, corner_tints[4]}],
statics:[{group_id, projected_x, projected_y}], (opt: floor[], liquid styles)}.
MVP = terrain + statics; FLOOR/liquids/heights = incremental.
viewer fns to reuse: TilesPak, TexturePak, MixedPak2D, StaticPak, FloorPak, ItemsPakTypeTable,
load_keyx_entries, build_keyx_absolute_layout, load_sector, build_tile_sources,
TileSurfaceStore.prebuild_base_tiles, collect_used_tile_ids, collect_tile_chained_static_records,
resolve_static_mixed_group, MixedSpriteStore.build_sprite, collect_floor_overlay_instances.
