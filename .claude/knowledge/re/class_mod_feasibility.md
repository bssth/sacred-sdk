# Replacing a character class — feasibility (revised 2026-09-16)

Supersedes the 2026-05-16 version of this file. Its "8-bit mask, all bits used", "Balance.bin is
global-only" and "per-class tables: location TBD" were wrong or incomplete.
Full evidence (VAs, offsets, counts, confidence tags) lives in `classes/`:
- `classes/exe.md`: Sacred.exe class ids, tables, and the per-class hardcoded-branch map.
- `classes/assets.md`: per-class asset and content footprint in the pak/bin/res files.
- `classes/community.md`: community sources, what ReBorn changed and how, precedents.
- `classes/tsv/`: models.pak index, class-exclusive Weapon.pak items, hero voice sounds.
The research sections were static work; the Implementation section at the end is live-tested.

## Identity
- Class id = creature type at `cCreature+0x10`:
  - 1 Seraphim, 2 Gladiator, 3 Battle Mage, 4 Dark Elf, 5 Wood Elf
  - 6 Vampiress, 7 Vampiress in vampire form
  - 8 Dwarf, 9 Daemon
  - The debug switch at 0x7f4e6b prints these class names.
- The class mask is **32-bit**, both in memory and in Weapon.pak (u32 at record `+0x82`).
  - Classic classes use bits 0..5; Dwarf 0x400000, Daemon 0x800000; bits 6..21 are free.
  - `cEngine_initGame` accepts class 1..12.
  - So the mask does NOT block a 9th class. What makes a 9th class impractical:
    - 62 per-class jump tables in 27 functions;
    - the 8-slot class-select table `0x9E3248`;
    - fixed 8x Balance tables;
    - the voice switches;
    - a full art set.
  - Replacing a slot stays the route.

## Where a class is defined (all data unless noted)
| aspect | where | how to change |
|---|---|---|
| base attributes, 2 start skills, 15-skill pool, walk/run speed | `pak/Creature.pak` (rows 1..9; 6 and 7 are separate rows) | custom/pak file override, or a heap write |
| attribute growth | no table: `base + base*(level-1)/10` (FUN_007f49a0) | via base |
| start equipment (8x20) / start inventory (8x8) | Balance.bin → `0xAD47B0` / `0xAD4A30` | custom/bin or a runtime write |
| start position, intro quests | `bin/TYPE_NPC_<CLASS>/StartCode.bin` + FunkCode window | custom/bin + SDK Lua quests |
| combat-art roster | rune items (Weapon.pak ItemTyp 29, class mask `+0x82`) + schools in `bin/wpmod.bin` (parser FUN_0042cd50, **layout not decoded**) | data |
| combat-art effects | exe code: moves `0x94FDAC`, spells `0x952A38` (no class field) | C hooks only |
| model | per-TYPE entry, file name at `+0x37` (FUN_00426250); motions follow the model | one record; the art is separate work |
| armor visuals | every wearable piece is a per-class mesh (73..105 per class) | art |
| voice | sound.pak, class suffix picked by code switches (table 0x564870) | swap files |
| class-select / info text | global.res ids 0x424..0x429; Dwarf/Daemon use `UI_DEFAULT_*_INFO` keys | T patcher |

## Single-class code branches (what separates the slots)
Counts are hand-triaged sites comparing against ONE class id.
- **Seraphim: ~1.** Horse seat only.
- **Gladiator: ~2.** Stance pick 0x54698e; CalcResults 0x57b013.
- **Battle Mage: ~3.**
- **Dark Elf: ~2.**
- **Wood Elf: ~6.**
- **Daemon: ~17.** Form stats, flight sounds, own CA ids 0x446/0x449..0x44C.
- **Dwarf: ~27.** Guns, forge UI x9, `MOUSE_CANTRIDE` 0x6121e3.
- **Vampiress: ~34.** Transform FUN_00557040 6<->7, save/load re-apply, day/night CA pairs FUN_00552370.

The per-class switch cases (voice, basic-move remap FUN_00580a10, default CA, colour) just keep the
old class's behaviour until their jump-table entries are rewritten.

## Body animation coverage (decides which combat arts a body can play)
| body | CAST_MAGIC | ATTACK_SPECIAL / SPECIAL_* | HORSE |
|---|---|---|---|
| Seraphim | 11 | 16 | 11 |
| Gladiator | **0** | 22 | 8 |
| Battle Mage | 13 | **0** | 10 |
| Dark Elf | **0** | 26 | 10 |
| Wood Elf | 9 | 14 | 10 |
| Vampiress (VMPD, VMPN) | 2 each | 20 each | 9 each |
| Dwarf | 5 | 16 | **0** |
| Daemon | 7 | 19 | 7 |

The unused `AMAZ_*` set is an NPC set: 45 motions, 9 casts, no ATTACK_SPECIAL, 9 armor meshes. It is not a
ready hero body. Townsfolk-as-hero mods hit the same wall: no attack or cast animations.

## Verdict: which slot
- **Seraphim is the best general slot.**
  - Fewest single-class branches (~1).
  - The only body with both cast and special-attack animations, plus full horse animations.
  - Cost: its own 3-quest chapter (+182 records) and 12 wing items.
- **Gladiator for a pure melee class.**
  - ~2 branches; no spells, forms or summons.
  - Cost: the largest intro script (+336 records). Co-op `NetScriptCamp` StartCode is a byte copy of it.
- **Battle Mage for a pure caster.**
  - ~3 branches.
  - No special-attack animations, so no melee combat arts.
- **Avoid:**
  - Vampiress: dual form, ~34 branches, and it is the SDK storyline's class.
  - Dwarf: guns, forge, can't ride, ~27 branches; ReBorn also reworks its assets. It has the smallest
    data footprint, but the hardcode outweighs that.
  - Daemon: forms, flight, own CA ids; the world reacts to it as a demon.
- Existing saves of the replaced class break: skill ids come from the class pool, CA ids are
  class-specific, and script section indices are cached. So a replaced class means new heroes only.

## Tiers
1. **Data only:** name/texts, stats, skill pool, start kit, a CA roster built from existing CAs the
   body can animate, intro and start quests, voice.
2. **DLL hooks:** rewrite the slot's few single-class branches and its switch-table cases.
3. **New CA effects:** C code per CA; the CA engine is not RE'd.
4. **New body:** Granny model on the Biped skeleton, ~70 motions, and every class armor mesh. This is
   an art track.

## Open / next probes
- Decode the `wpmod.bin` roster layout.
- Live: swap one Creature.pak row via custom/pak and check the class select + skill pool.
- Live: give a body a CA from another class and see which motion plays; FUN_005467a0 /
  FUN_00542b20 hold hand-tuned per-class motion ids.
- GameServer.exe class checks: not examined.

## NPC body candidates per slot (2026-09-16, motion-coverage scan)
Tools: `classes/tools/{scan,list,pairs}.py`. They read Models.tmp slot tables and Items.pak creature records. The scripts import `gres.py` from the scratchpad; copy that in before re-running.

**Coverage metric:** motion suffixes of the hero body that the NPC body also has. The rest gets filled by classmod fallbacks.

**Prerequisite, proven on Battle Mage → Alcalata (type 271):** body + texture + `parts=false` + slot fill all work in game.

| slot | pick (type, model) | coverage | notes |
|---|---|---|---|
| Seraphim 1 | Amazon 316 AMAZONE.GRN | 44/71 (9 casts, 7 horse, 0 specials) | alternatives: Shareefa 697 (19/71, 10 casts, "The Magician Shareefa" HQ_5_8_1); Baroness Vilya 699 (33/71). Wings items may float. |
| Gladiator 2 | Prince Valor 322 VALOR.GRN | 25/72 (no specials → every combat art plays a basic attack) | main-campaign NPC. Alternative: Shalinor 326 (43/72, 14 specials). |
| Dark Elf 4 | Shalinor the Dark Elf 326 DARKELVE2B.GRN | 60/84 (19 specials, horse 4) | alternatives: Morgwath 320, Elendiar 327 (the Wood Elf's class quest), Ice Elf Champion 146 (84/84). |
| Wood Elf 5 | Ice Elf Priestess 141 ICEELVE_DRUID.GRN | 90/90 (a copy of the Wood Elf motion set) | generic. Named alternative: Elven Sorcerer 321 (19/90). |
| Vampiress 6/7 | Baroness Vilya 699 BARONESS_OUTDOOR.GRN | day 27/72, night 12/43 | classmod gives 6 and 7 the same body (no visual transformation). This is the SDK storyline's class. |
| Dwarf 8 | Wilbur 83 WILBUR.GRN | 9/50 | no dwarf NPC body exists; the gun combat arts fall back. Alternative: Smith 688 (11/50). |
| Daemon 9 | Anducar 182 ANDUCAR.GRN (tex 7950) | 16/70 | the Daemon's own antagonist; size unchecked; Daemon forms not covered. Alternative: Shalinor 326 (44/70). |

## Implementation: `custom/lua/lib/classmod.lua` (live-tested 2026-09-16, all 8 slots)
Mods: `custom/lua/classes/{alcalata,amazon,valor,shalinor,ice_elf_priestess,vilya,wilbur,anducar}.lua`, one `CM.replace(class, spec)` each.
`classmod.flush` is a bake finalize module (lua_bake.cpp `FINALIZE_MODULES`).

| piece | mechanism | evidence |
|---|---|---|
| name, select label, description | global.res keys `"<type>"`, `UI_DEFAULT_*`, `1060..1067` / `UI_DEFAULT_*_INFO` via `T.named` | live |
| body | Items.pak TYPE record +0x08 texture, +0x37 model name, +0x70 models.pak index copied from the donor type → `custom/pak/Items.pak` | live |
| motion slots | empty slots (the donor has 0 where the hero body plays a motion) filled with the donor's closest motion: `sacred.model_slots` writes the manager vector `[0xAA4538]+0x48`, header+0x70+4s (Models.tmp layout) | live (select T-pose gone) |
| default part | FUN_00426580 jump table 0x4265E8 (index class-1) → "none" case 0x4265E0 | live (cowl bits gone) |
| portraits | small FUN_00435d30 JT 0x436694 (t-1); large FUN_004345c0 JT 0x435264 (t≤9) / 0x435288 (t-0x21); hero cell ← donor cell (`sacred.patch_u32_copy`) | live |
| select preview skin | 3 × 27-byte rewrites at 0x6F1DB0 / 0x6F5685 / 0x6F59A3: `push 0; call 401bd0` → getTex(class) into [edi+0x34] | live |
| select preview items | select table 0x9E3248 item ids zeroed for the class | live |
| worn item meshes | naked hook on `call 0x44C980` at 0x555928 (ESI = creature): a masked type runs CalcResults(0,1) and returns 0 (vanilla wear-failed branch) | live |
| Vampiress | per-type bodies: day 699 BARONESS_OUTDOOR, night 224 VILYAUW (pale UW Vilya, own portraits) | live |

All exe writes go through the DLL queue in `lua_api_data.cpp`:
- `patch_u32`, `patch_u32_copy`, `patch_bytes`, `wear_hide`;
- applied from `heartbeat()` once the build is TRUSTED, only over the expected bytes.

Open:
- `GUI_QBP_<class>` painted image (FUN_006b0e00, JT 0x6B1854; which panel shows it is unknown).
- The 0x6F1CE1 preview ghost branch.
- Valor once shrank for ~2 s after a talk; not reproduced. Suspect: slot 177 VALO_TALK_A, the only donor motion in a slot the Gladiator never plays.
- Donor model slot tables are shared with the NPCs that use them.

RE reports behind these rows: `classes/armor_visuals_report.md`, `classes/portrait_skin_report.md`.
