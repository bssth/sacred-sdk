# Knowledge Index — Sacred Gold SDK

One-line index of every knowledge report + the canonical RE specs. Read `MASTER_PLAN.md`
(`../plans/`) for the ordered execution plan that consumes all of these.
Game EXE base `0x00400000`, no ASLR → file offset = VA − 0x400000.

## Synthesized plan
- **`../plans/MASTER_PLAN.md`** — the ordered cross-goal execution plan (A refactor / B tools-elimination / C RE sweep / D refs ports / E workspace), with risks, dependency order, and "do this next".

## Knowledge reports (this dir)
- **`sdk_architecture.md`** — audit of all 17 injected-DLL C++ sources (~10.6K LOC); per-file table, the reusable infra spine (detour/IAT/SEH/singletons/decrypt-gate/Lua registry), the 2 god files, and the target `core/engine/hooks/bindings/features/build/ui` namespace design + 8-step build-stays-green migration. **9 port/refactor candidates.**
- **`tools_audit.md`** — `tools/` elimination plan; the ONLY runtime python coupling (`script_mods.cpp run_python_sync`, 2 sites, 5-script closure); classifies all 123 `.py` (5 runtime / ~25 keepers / ~17 dead), indexes 30 scratch docs + 298 ghidra `.c`; funkcode C++ port plan. **4 candidates.**
- **`re_backlog.md`** — the single prioritized RE roadmap: 8 SOLVED+locked areas (talk signal, FunkCode ABI, spawn/combat init, global.res, quest journal, item/equip, voice/move/teleport), 8 ranked OPEN targets (O1 dialog-text … O8 network) each with next experiment, 15 corpus gaps. **8 port candidates.**
- **`refs_resacred.md`** — Resacred (C++/GL Sacred remake, 2019): the asset-layer Rosetta stone. PAK/keyx/wldx/texture/static/item formats + zlib codec + world↔screen iso math, all byte-exact. Ships 2 binary IDA DBs of the Sacred EXE (catalog only). **8 port candidates.**
- **`refs_tools.md`** — 4 community tool repos: SacredGameTools (authoritative PAX save codec, inventory/item layout, 0xC7 stat offsets, 236-spell table, TINCAT2 CRC32), SacredUtils (Settings.cfg keys, core-DLL manifest, global.res↔CSV), UW-Helper (HP pointer chain), Lumina (startcode.bin spawn offsets). **5 candidates.**
- **`refs_native_src.md`** — 7 native source archives: PAX hero format double-decoded (HeroDump C++ + charmodif Pascal), Inoff Patch source (~20 confirmed German-UW engine VAs + IAT thunk table + live globals), hero data tables (206-XP, 34 skills, 123 CAs). **8 candidates.**
- **`refs_magician_net.md`** — Sacred multiplayer protocol (unique to corpus): TCP lobby frame + TINCAT2 CRC32, SERVERPACKET, UDP server-announce codec, SAC REST matchmaking, balance.bin offset table, hero .sav container. **6 candidates.**
- **`refs_formats_data.md`** — 4 community CSVs (713 creature types, 156 combat arts, companions, ~625 string IDs) + format structs from editor source (resource PAK, Creature.pak "CIF", Weapon.pak SacredItem + ~520-entry bonus map, balance.bin region offsets). **7 candidates (P1-P7).**

> **Total `TODO(port)` candidates across reports: ~55** (deduped & ordered into D-tiers in MASTER_PLAN Goal D).

## Canonical RE specs — `scratch/*.md` (CANON; to relocate to `knowledge/re/` per Goal E)
Currently in `sdk/.claude/knowledge/re/`. 27 authoritative; topic groups below.

**FunkCode / scripting:** `funkcode_tags.md` (TAG master table, ~140 tags).
**Quest / journal:** `quest_lifecycle.md` (cQuestMgr @0x00AACF80), `questbook_inserter.md`, `questbook_render.md`, `questbook_resolver.md` (res:NAME→+0x24), `quest_marker_pos.md`, `quest_storyline.md`, `quest_polish.md`, `quest_fanfare.md`, `questbook_recon.md` (partly STALE).
**NPC / dialog:** `talk_trigger.md` (THE blocker — `+0x200` bit 0x400), `dialog_native_roadmap.md` (Path A, current), `dialog_runtime.md`, `dlgnpc_bind.md`, `npc_model.md`, `npc_templates.md`, `combat_init.md` (HP/AI init, hostility matrix @0x890A30), `runtime_spawn.md`, `triggers_dialog_move.md` (move/teleport/voice command ring).
**Items / save:** `items.md`, `items_equip.md` (equip ABI, TYPE space @0x008EC328), `gold_safe.md` (+0x3EE save-corruption fix), `class_mod_feasibility.md` (8-bit class mask full).
**Render / resolution:** `render_dims.md`, `resolution.md`, `reborn_hd.md`, `spawn_point.md` — *consolidate the render trio*.
**Format:** `globalres_format.md` (byte-exact, 23123 slots).
**Archive (STALE):** `HANDOFF.md` (superseded by dialog_native_roadmap + MEMORY), `README.md` (meta).
**Fixtures:** `scratch/captures/` (session/vanilla logs), `scratch/refs_extract/` (ReBorn changelist).

## Memory (resume points) — `../memory/`
- **`MEMORY.md`** — memory index.
- **`session-state-2026-06-13.md`** — RESUME POINT: deployed build md5 bc69043b, build/deploy/run recipe, what works, pending polish (native dialog text, roster panel). Read first when resuming.
- **`talk-signal-0x200.md`** — the validated "player talked to NPC" signal (cCreature+0x200 bit 0x400).
- **`sdk-refactor-initiative.md`** — the refactor effort tracker.
