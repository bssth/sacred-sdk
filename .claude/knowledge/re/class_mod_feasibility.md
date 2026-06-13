# Adding / replacing a character class — feasibility (2026-05-16)

## Hard constraint: classes are an 8-bit mask, all 8 bits used

From community-refs (cross-verified vs char.cpp, SacredGameTools, CT):

```
Seraphim=1  Gladiator=2  BattleMage=4  DarkElf=8
WoodElf=16  Vampiress=32 Dwarf=64      Daemon=128
AllClassic=63 (6 Classic)   AllUW=192 (Dwarf+Daemon, the Underworld DLC)
```

This single **BYTE** bitmask is used pervasively: item "usable-by-class"
restriction, combat-art / skill gating, quest class-restriction
(`questbook` per-class slot table we just REd), and the save format's
class field. **All 8 bits are taken** (Ascaron used 64/128 for the
Underworld expansion). A 9th class needs that mask widened to 16-bit in
*every* consumer (items, CAs, skills, quests, .res, save) + the
class-select UI count + a full asset set. That's engine surgery, not a
mod. **Verdict: adding a 9th class — infeasible via the SDK.**

Note: don't confuse identifiers —
- the "1..16" we use (`*([0x0182EBE8]+0x14)`) is the active **hero/player
  slot** index (MP), not a class count;
- "Creature classes (15)" (Hero/Monster/NPC/Horse/...) is the creature
  *taxonomy*, unrelated to the 8 playable classes;
- playable class id = `heroBase+0x010` (1=Sera … 9=Daemon).

## Replacing / repurposing one of the 8 slots — feasible, in tiers

Pick a slot (e.g. a class you don't play) and overwrite what it *is*:

| Aspect | Feasibility | Mechanism |
|---|---|---|
| Name / class-select & UI text | ✅ trivial | our `T` / global.res patcher (done tech) |
| Quests & scripts for that class | ✅ done tech | `custom/lua/bin/TYPE_NPC_<CLASS>/` bake |
| Start stats / level curves / skill & combat-art list | 🟡 data — needs RE | per-class tables (NOT Balance.bin — that's global difficulty/region only). Location TBD in Sacred.exe data / a typed `.res` |
| Class-select portrait / icons | 🟡 asset swap | image resources; reskin-style override |
| 3D model + animations | 🔴 heavy, out of SDK scope | Granny `.gr2` (tools exist: PakExtractor, grn2gr2, sacredtools — art pipeline, not code) |

So a **"total-conversion of an existing class slot"** (new identity,
scripts, balance, 2D art) is realistic SDK territory; a new 3D model is
an art project orthogonal to the SDK.

## Concrete next RE step (if pursued)

Locate the **per-class character-definition tables**: starting
attributes, the per-class skill list, the combat-art roster, and
level-up curves. Balance.bin (REd) is global-only, so these live either
in `Sacred.exe` data referenced by `class_id`, or a typed resource.
Method: hook/trace reads keyed by `heroBase+0x010` (class_id) or the
class bit during character creation (`cHero`/`classHero` path noted in
community-refs `sacred_charmodif/classHero.pas`). One focused recon
agent could pin these, after which "rebalance/redefine class N" becomes
a data mod on top of the existing bake + global.res tooling.

## Recommendation

Frame the feature as **"class total-conversion"** (replace slot, keep
count=8), not "add class". v1 deliverable with current tech alone:
rename + custom quests/scripts + text for a chosen slot. v2 (after the
per-class table RE): full stat/skill/CA redefinition. 3D model =
separate art track, explicitly out of SDK scope.
