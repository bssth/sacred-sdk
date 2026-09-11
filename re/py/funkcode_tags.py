"""FunkCode record TAG -> name, handler, purpose -- reconciled 2026-09-11.

NAMES: the compiler's own keyword -> tag tables (the authority named in
sdk/.claude/knowledge/quests/README.md §4.1). They are three static resolvers
in the exe; each fills a stack array of keyword-string pointers and a parallel
array of tag bytes and returns tag[i] for the keyword that matched:

    FUN_00452370  60 keywords  `return local_f4[local_1ec];`   (:213)
    FUN_00451be0  61 keywords  `return local_f8[iVar4];`       (:200)
    FUN_00452910   6 keywords  `_DAT_00aab714 = (short)local_500[iVar9];` (:109)

Re-extracted mechanically from those decompiles with every string pointer
resolved from sdk/Sacred_decrypted.exe (`disasm_tiling_check.py
--verify-tags` repeats the extraction and fails on any drift). The keyword
is what the script author typed, so it is what a record MEANS.

HANDLERS: the record walker FUN_00475680 `switch((ushort)record)` (case line
in WALKER_CASE). "inline" = the walker handles the tag itself.

Labels ending in `*` have NO compiler keyword (the compiler emits the tag
without a keyword, or the walker handles it and no keyword produces it). They
are descriptive names taken from the walker's behaviour -- not recovered
names. Seven tags qualify that occur in the corpus: 0x00 0x27 0x28 0x29 0x2a
0x2b 0x2c 0x6f (0x2f: walker case, 0 records).

ALIASES keeps every label the pre-2026-09-11 table printed, so shard files
generated before this change (`shards/**/*.txt`) stay searchable:
`tag_for("QuestLogSet") == 0x35`.

Tag vs opcode: a TAG is a record type (byte 0 of a record); an OPCODE is an
operand marker inside a payload (funkcode_disasm.GRAMMAR). The same hex value
means unrelated things in the two spaces (README §4.1 second table).
"""

# tag: (label, handler VA or None for inline/none, purpose)
TAGS = {
    0x00: ("END*",                 None,         "no keyword; walker case 0 -> caseD_0: returns 0, the run stops (FUN_00475680:195, :1626-1628)"),
    0x01: ("CreateNPC",            "0x00482510", "spawn an NPC (re/npc_model.md)"),
    0x02: ("SetObjState",          "0x0048ae90", "set an object's state"),
    0x03: ("SetNPCState",          "0x0048bb40", "set an NPC's state / dialog node"),
    0x04: ("SetBaseTrigger",       "0x004819a0", "register an area/base trigger"),
    0x05: ("DelBaseTrigger",       "0x004817f0", "remove a base trigger"),
    0x06: ("TalkTo",               None,         "keyword only; no walker case (default: continue); 0 records"),
    0x07: ("EquipNPC",             "0x00481d80", "equip an NPC"),
    0x08: ("CreateObj",            "0x00485c10", "spawn an object ('CreateOBJ: %s mehrfach' in handler)"),
    0x09: ("StartScript",          None,         "keyword only; no walker case (default: continue); 0 records"),
    0x0a: ("QuestDone",            None,         "condition, inline: quest registry DAT_00aabf18 entry byte +0x120 & 2 (walker :239-313)"),
    0x0b: ("QuestNotDone",         None,         "condition, inline (walker :314-389)"),
    0x0c: ("QuestInProgress",      None,         "condition, inline (walker :390-464)"),
    0x0d: ("QuestNotInProgress",   None,         "condition, inline (walker :465-539)"),
    0x0e: ("DeleteFunktionBlock",  None,         "inline (walker :540-608); returns 2 -> section runner FUN_0046ba90:118-143 sets the section entry byte +0x50"),
    0x0f: ("ExitQuest",            "0x0048ee20", "abort a quest (shares the handler with 0x36 LoseQuest)"),
    0x10: ("MoveOver",             None,         "keyword only; no walker case (default: continue); 0 records"),
    0x11: ("AddExp",               "0x0048d480", "give experience"),
    0x12: ("AddGold",              "0x0048da40", "give gold (kernel event 6 / 0x3ec)"),
    0x13: ("AddGems",              "0x0048dd10", "give gems"),
    0x14: ("TriggerQuest",         "0x0048f030", "start/advance a quest by id (scans DAT_00aabf18 stride 0x124)"),
    0x15: ("SetUpQuest",           "0x0048f7f0", "declare/register a quest"),
    0x16: ("CallFunktion",         "0x004813d0", "call a named script section (section table stride 0x54)"),
    0x17: ("DefPos",               "0x00478780", "declare a named position (DefPos store ctx+0x334)"),
    0x18: ("GroupIsDead",          "0x0048c860", "condition: monster group dead"),
    0x19: ("CheckForQuestPool",    "0x00490a30", "condition: quest-pool availability"),
    0x1a: ("Text",                 None,         "dialog/info text line, inline (walker :668-691); keywords 'Text' and 'Text:'"),
    0x1f: ("Dialog",               "0x0048f9e0", "start a dialog; keyword 'Dialog:'"),
    0x20: ("Belohnungen",          "0x00490be0", "quest reward block; keyword 'Belohnungen:'"),
    0x21: ("HideTmpToDo",          "0x00490cc0", "hide a temporary objective; keyword 'HideTmpToDo:'"),
    0x23: ("FillRegion",           "0x0047c610", "populate a region (shares the handler with 0x33 FillSector)"),
    0x24: ("AddHandel",            "0x0048e4d0", "add merchant stock"),
    0x25: ("SetRG",                "0x0048f330", "set a region gate flag (shares the handler with 0x26)"),
    0x26: ("UnsetRG",              "0x0048f330", "clear a region gate flag"),
    0x27: ("Subsys_27*",           "0x004918b0", "no keyword; handler is a reader loop with no strings -- meaning UNKNOWN"),
    0x28: ("DlgNPCDecl*",          None,         "no keyword; inline RAW (walker :740-977): the 80 payload bytes after byte 3 are copied verbatim into the DlgNPC table ctx+0x755c (stride 0x50); FUN_004bb1d0 is only the vector-grow helper"),
    0x29: ("NpcFlagGuard*",        None,         "no keyword; inline RAW (walker :978-1062): u16 selector 1..5 tests a bit of the quest-NPC entry flags; unless it passes, DAT_00ab7898 makes the walker skip records up to tag 0x2a (:186-193)"),
    0x2a: ("NpcFlagGuardEnd*",     None,         "no keyword; no walker case: closes the 0x29 skip (walker :186-193), otherwise default: continue"),
    0x2b: ("END_ALT*",             None,         "no keyword; walker case 0x2b -> caseD_0 like tag 0x00: returns 0, the run stops"),
    0x2c: ("NPC_UWR_dispatch*",    "0x00549920", "no keyword; handler builds NPCUWR_<a><b> dialog names, then the walker clears dialog flag 0x80000 (walker :1063-1076)"),
    0x2d: ("StartPosition",        "0x00491b30", "set the hero start position"),
    0x2e: ("Teleport",             "0x00491d40", "teleport an NPC ('Teleport failed: resnum=%d')"),
    0x2f: ("Subsys_2f*",           None,         "no keyword; inline RAW (walker :1089-1330): u32 + ASCIIZ; 0 records in the corpus"),
    0x30: ("CreateTrigger",        "0x00496520", "define a named script trigger"),
    0x31: ("SetTriggerState",      "0x004968a0", "arm/disarm a trigger"),
    0x32: ("TriggerPatch",         "0x00496f20", "patch a trigger"),
    0x33: ("FillSector",           "0x0047c610", "populate a sector (shares the handler with 0x23 FillRegion)"),
    0x34: ("SetHP",                "0x0048e280", "set hit points"),
    0x35: ("QuestBook",            "0x00496080", "write a quest-log entry"),
    0x36: ("LoseQuest",            "0x0048ee20", "fail a quest (shares the handler with 0x0f ExitQuest)"),
    0x37: ("DelNPC/DelOBJ",        "0x00497f80", "despawn a named NPC or object (two keywords, one tag)"),
    0x38: ("SetTimer",             "0x004982d0", "timer / delay"),
    0x39: ("DelTimer",             "0x004985e0", "cancel a timer"),
    0x3a: ("IF",                   "0x004987b0", "conditional; predicate evaluator FUN_004987b0"),
    0x3b: ("ELSE",                 None,         "conditional else, inline (walker :1501-1536)"),
    0x3c: ("SetButton",            "0x00499ba0", "dialog answer button"),
    0x3d: ("ChDlgText",            None,         "change dialog text, inline reader loop (walker :1542-1556)"),
    0x3e: ("NOP",                  None,         "keyword 'NOP'; no walker case (default: continue)"),
    0x3f: ("QuestKompassPos",      "0x0049a4b0", "quest compass -> position ('QuestkompassPos failed')"),
    0x40: ("QuestKompassObj",      "0x0049ac80", "quest compass -> object ('QuestkompassOBJ failed')"),
    0x41: ("DefNum",               "0x0049b2b0", "declare a named number (shares the handler with 0x43 SetVar)"),
    0x42: ("ELSEIF",               None,         "conditional else-if, inline (walker :1573-1628)"),
    0x43: ("SetVar",               "0x0049b2b0", "set a script variable"),
    0x44: ("SetVarBit",            "0x0049b840", "set a variable bit"),
    0x45: ("UnsetVarBit",          "0x0049c160", "clear a variable bit"),
    0x46: ("SetFocus",             "0x0049daf0", "camera/UI focus on an entity"),
    0x47: ("NPC_TalkTo",           "0x0049dcf0", "two NPCs converse"),
    0x48: ("NPC_Goto",             "0x0049e210", "NPC walks to a point"),
    0x49: ("SetGroupState",        "0x0049e760", "set a monster group's AI state"),
    0x4a: ("GroupGoto",            "0x0049fc50", "move a whole group"),
    0x4b: ("IncVar",               "0x0049c930", "increment a variable"),
    0x4c: ("DecVar",               "0x0049cec0", "decrement a variable"),
    0x4d: ("UnTriggerQuest",       "0x0048e600", "reset a quest trigger"),
    0x4e: ("FillChest",            "0x004a02a0", "fill a container ('Truhe %s falscht benannt')"),
    0x4f: ("Morph",                "0x004a15a0", "morph/transform an entity"),
    0x50: ("SetST",                None,         "keyword only; no walker case (default: continue); 0 records"),
    0x51: ("SetGS",                None,         "keyword only; no walker case; 0 records"),
    0x52: ("SetWI",                None,         "keyword only; no walker case; 0 records"),
    0x53: ("SetRP",                None,         "keyword only; no walker case; 0 records"),
    0x54: ("SetRM",                None,         "keyword only; no walker case; 0 records"),
    0x55: ("SetCH",                None,         "keyword only; no walker case; 0 records"),
    0x56: ("SetIcon",              "0x004a1a50", "set an NPC's marker icon"),
    0x57: ("SetQuestInfo",         "0x004a6ea0", "quest info / journal page"),
    0x58: ("SetAnimMode",          None,         "anim mode, inline kernel event (walker :1701-1723)"),
    0x59: ("SetPlayMode",          "0x004a4040", "play / cinematic mode"),
    0x5a: ("Wait",                 "0x004a1f20", "wait"),
    0x5b: ("PlayAnim",             "0x004a2550", "play an NPC animation"),
    0x5c: ("Attack",               "0x004a2b40", "force an attack; the handler also teleports directly when there is no listener ('Figur wird direkt Teleportiert weil ohne Listner')"),
    0x5d: ("ActObj",               "0x004a4310", "activate an object"),
    0x5e: ("MouseEvent",           "0x004a6950", "click/use trigger"),
    0x5f: ("Popup",                "0x004a7760", "popup message (signposts)"),
    0x60: ("ReplaceSpawn",         "0x004a79d0", "swap a spawn"),
    0x61: ("SetMapIcon",           "0x004a81f0", "world-map icon"),
    0x62: ("SecInfo",              "0x004a8390", "sector info ('Setup Sector:%d,%d')"),
    0x63: ("Partikel",             "0x004a8bb0", "particle effect"),
    0x64: ("SpawnValues",          "0x004a9670", "tune spawn parameters"),
    0x67: ("SetCV",                "0x0048df30", "set a client variable"),
    0x68: ("PlaySound",            "0x004a9730", "play a sound / voice line"),
    0x69: ("RndVar",               "0x0049d450", "randomise a variable"),
    0x6a: ("SetRgnDialog",         "0x00465280", "bind a region's ambient dialog"),
    0x6b: ("BalanceSpawnItem",     None,         "spawn a balanced item, inline reader loop (walker :1813-1832)"),
    0x6c: ("AddPoolPos",           "0x004790c0", "add a spawn-pool position"),
    0x6d: ("GetPoolPos",           "0x004793d0", "take a spawn-pool position ('Clear Pool: %s')"),
    0x6e: ("RenPos",               "0x00478d80", "rename a position"),
    0x6f: ("SectionEnd*",          None,         "no keyword; inline (walker :1848-1850): returns 3, the section runner FUN_0046ba90:145 leaves its record loop by the normal path"),
    0x70: ("AddPoolRgn",           "0x0047b480", "add a spawn-pool region"),
    0x71: ("GetPoolRgn",           "0x0047b770", "take a spawn-pool region"),
    0x72: ("Waffenpool",           None,         "weapon pool, inline reader loop (walker :1861-1901)"),
    0x73: ("MakeGroup",            "0x004ab940", "create a monster group"),
    0x74: ("RegionChange",         "0x004abb60", "region-enter transition"),
    0x75: ("ActivateQuest",        "0x0048d930", "activate a quest"),
    0x76: ("SetClientVars",        "0x0048ff10", "set client variables"),
    0x77: ("GetClientVars",        "0x00490500", "get client variables"),
    0x78: ("GameEnd",              "0x006a1660", "end the game"),
    0x79: ("AutoSave",             "0x004ac740", "autosave"),
    0x7a: ("SolveTime",            None,         "quest time limit, inline reader loop (walker :1943-1964); keyword 'SolveTime:'"),
    0x7b: ("Strafen",              "0x00491090", "quest penalty block; keyword 'Strafen:'"),
    0x7c: ("RegionFrei",           None,         "free a region, inline reader loop (walker :1971-2014)"),
    0x7d: ("Gewinn",               "0x004ac940", "reward roll"),
    0x7e: ("RejectQuest",          None,         "reject a quest, inline reader loop (walker :2020-2033)"),
    0x7f: ("PlayMusic",            "0x004adaa0", "play a music track"),
    0x80: ("PlayJingle",           "0x004add60", "play a jingle"),
    0x81: ("SetScriptSoundControle", None,       "sound control, inline reader loop (walker :2044-2059)"),
    0x82: ("Teleporter",           None,         "teleporter, inline (walker :2060-2064)"),
    0x83: ("GameActive",           None,         "condition, inline reader loop (walker :2065-2094)"),
    0x84: ("InfoPlayer",           "0x004ae350", "banner + chime to the player"),
    0x85: ("GetPoolPosition",      "0x0047a0c0", "pool position ('GetPoolPos failed. Pool (%s) is empty')"),
    0x86: ("SetCinemaMode",        None,         "cinematic camera, inline kernel event (walker :2104-2139)"),
    0x87: ("SetOnKill",            "0x004af190", "objective hook: on kill"),
    0x88: ("SetOnCollect",         "0x004af8c0", "objective hook: on collect"),
    0x89: ("SetDrop",              "0x004afff0", "objective hook: set a drop"),
    0x8a: ("GiveStat",             "0x004b0790", "grant a stat"),
    0x8b: ("GiveSkill",            "0x004b0970", "grant a skill"),
    0x8c: ("MachVersteck",         "0x004b0c00", "hide spot ('versteck %d, %d, %d')"),
}

# every keyword the three resolvers map to each tag
KEYWORDS = {
    0x01: ("CreateNPC",), 0x02: ("SetObjState",), 0x03: ("SetNPCState",), 0x04: ("SetBaseTrigger",),
    0x05: ("DelBaseTrigger",), 0x06: ("TalkTo",), 0x07: ("EquipNPC",), 0x08: ("CreateObj",),
    0x09: ("StartScript",), 0x0a: ("QuestDone",), 0x0b: ("QuestNotDone",), 0x0c: ("QuestInProgress",),
    0x0d: ("QuestNotInProgress",), 0x0e: ("DeleteFunktionBlock",), 0x0f: ("ExitQuest",), 0x10: ("MoveOver",),
    0x11: ("AddExp",), 0x12: ("AddGold",), 0x13: ("AddGems",), 0x14: ("TriggerQuest",), 0x15: ("SetUpQuest",),
    0x16: ("CallFunktion",), 0x17: ("DefPos",), 0x18: ("GroupIsDead",), 0x19: ("CheckForQuestPool",),
    0x1a: ("Text", "Text:"), 0x1f: ("Dialog:",), 0x20: ("Belohnungen:",), 0x21: ("HideTmpToDo:",),
    0x23: ("FillRegion",), 0x24: ("AddHandel",), 0x25: ("SetRG",), 0x26: ("UnsetRG",), 0x2d: ("StartPosition",),
    0x2e: ("Teleport",), 0x30: ("CreateTrigger",), 0x31: ("SetTriggerState",), 0x32: ("TriggerPatch",),
    0x33: ("FillSector",), 0x34: ("SetHP",), 0x35: ("QuestBook",), 0x36: ("LoseQuest",),
    0x37: ("DelNPC", "DelOBJ"), 0x38: ("SetTimer",), 0x39: ("DelTimer",), 0x3a: ("IF",), 0x3b: ("ELSE",),
    0x3c: ("SetButton",), 0x3d: ("ChDlgText",), 0x3e: ("NOP",), 0x3f: ("QuestKompassPos",),
    0x40: ("QuestKompassObj",), 0x41: ("DefNum",), 0x42: ("ELSEIF",), 0x43: ("SetVar",), 0x44: ("SetVarBit",),
    0x45: ("UnsetVarBit",), 0x46: ("SetFocus",), 0x47: ("NPC_TalkTo",), 0x48: ("NPC_Goto",),
    0x49: ("SetGroupState",), 0x4a: ("GroupGoto",), 0x4b: ("IncVar",), 0x4c: ("DecVar",),
    0x4d: ("UnTriggerQuest",), 0x4e: ("FillChest",), 0x4f: ("Morph",), 0x50: ("SetST",), 0x51: ("SetGS",),
    0x52: ("SetWI",), 0x53: ("SetRP",), 0x54: ("SetRM",), 0x55: ("SetCH",), 0x56: ("SetIcon",),
    0x57: ("SetQuestInfo",), 0x58: ("SetAnimMode",), 0x59: ("SetPlayMode",), 0x5a: ("Wait",),
    0x5b: ("PlayAnim",), 0x5c: ("Attack",), 0x5d: ("ActObj",), 0x5e: ("MouseEvent",), 0x5f: ("Popup",),
    0x60: ("ReplaceSpawn",), 0x61: ("SetMapIcon",), 0x62: ("SecInfo",), 0x63: ("Partikel",),
    0x64: ("SpawnValues",), 0x67: ("SetCV",), 0x68: ("PlaySound",), 0x69: ("RndVar",),
    0x6a: ("SetRgnDialog",), 0x6b: ("BalanceSpawnItem",), 0x6c: ("AddPoolPos",), 0x6d: ("GetPoolPos",),
    0x6e: ("RenPos",), 0x70: ("AddPoolRgn",), 0x71: ("GetPoolRgn",), 0x72: ("Waffenpool",),
    0x73: ("MakeGroup",), 0x74: ("RegionChange",), 0x75: ("ActivateQuest",), 0x76: ("SetClientVars",),
    0x77: ("GetClientVars",), 0x78: ("GameEnd",), 0x79: ("AutoSave",), 0x7a: ("SolveTime:",),
    0x7b: ("Strafen:",), 0x7c: ("RegionFrei",), 0x7d: ("Gewinn",), 0x7e: ("RejectQuest",),
    0x7f: ("PlayMusic",), 0x80: ("PlayJingle",), 0x81: ("SetScriptSoundControle",), 0x82: ("Teleporter",),
    0x83: ("GameActive",), 0x84: ("InfoPlayer",), 0x85: ("GetPoolPosition",), 0x86: ("SetCinemaMode",),
    0x87: ("SetOnKill",), 0x88: ("SetOnCollect",), 0x89: ("SetDrop",), 0x8a: ("GiveStat",),
    0x8b: ("GiveSkill",), 0x8c: ("MachVersteck",),
}

# FUN_00475680.c line of `case 0xNN:` (absent = no case, the walker's default)
WALKER_CASE = {
    0x00: 195, 0x01: 198, 0x02: 204, 0x03: 210, 0x04: 216, 0x05: 221, 0x07: 228, 0x08: 233, 0x0a: 239,
    0x0b: 314, 0x0c: 390, 0x0d: 465, 0x0e: 540, 0x0f: 609, 0x11: 616, 0x12: 622, 0x13: 628, 0x14: 634,
    0x15: 640, 0x16: 646, 0x17: 652, 0x18: 657, 0x19: 662, 0x1a: 668, 0x1f: 692, 0x20: 698, 0x21: 704,
    0x23: 710, 0x24: 717, 0x25: 723, 0x26: 729, 0x27: 735, 0x28: 740, 0x29: 978, 0x2b: 196, 0x2c: 1063,
    0x2d: 1077, 0x2e: 1083, 0x2f: 1089, 0x30: 1389, 0x31: 1394, 0x32: 1399, 0x33: 711, 0x34: 1405,
    0x35: 1411, 0x36: 1417, 0x37: 1424, 0x38: 1429, 0x39: 1434, 0x3a: 1439, 0x3b: 1501, 0x3c: 1537,
    0x3d: 1542, 0x3f: 1557, 0x40: 1562, 0x41: 1567, 0x42: 1573, 0x43: 1568, 0x44: 1629, 0x45: 1634,
    0x46: 1639, 0x47: 1644, 0x48: 1649, 0x49: 1654, 0x4a: 1659, 0x4b: 1664, 0x4c: 1669, 0x4d: 1674,
    0x4e: 1680, 0x4f: 1686, 0x56: 1691, 0x57: 1696, 0x58: 1701, 0x59: 1724, 0x5a: 1729, 0x5b: 1734,
    0x5c: 1739, 0x5d: 1744, 0x5e: 1749, 0x5f: 1754, 0x60: 1759, 0x61: 1764, 0x62: 1769, 0x63: 1774,
    0x64: 1779, 0x67: 1784, 0x68: 1789, 0x69: 1803, 0x6a: 1808, 0x6b: 1813, 0x6c: 1833, 0x6d: 1838,
    0x6e: 1843, 0x6f: 1848, 0x70: 1851, 0x71: 1856, 0x72: 1861, 0x73: 1902, 0x74: 1907, 0x75: 1913,
    0x76: 1918, 0x77: 1924, 0x78: 1930, 0x79: 1938, 0x7a: 1943, 0x7b: 1965, 0x7c: 1971, 0x7d: 2015,
    0x7e: 2020, 0x7f: 2034, 0x80: 2039, 0x81: 2044, 0x82: 2060, 0x83: 2065, 0x84: 2095, 0x85: 2100,
    0x86: 2104, 0x87: 2143, 0x88: 2149, 0x89: 2155, 0x8a: 2161, 0x8b: 2166, 0x8c: 2171,
}

# labels the pre-2026-09-11 table printed (still in every shard generated before)
ALIASES = {
    0x00: ("END",), 0x02: ("TriggerSetState",), 0x03: ("DialogShow_v1",), 0x04: ("Subsys_04",),
    0x05: ("Subsys_05",), 0x07: ("Subsys_07",), 0x08: ("CreateOBJ",), 0x0a: ("InlinePoolWalker_a",),
    0x0b: ("InlinePoolWalker_b",), 0x0c: ("InlinePoolWalker_c",), 0x0d: ("InlinePoolWalker_d",),
    0x0e: ("InlinePoolWalker_e",), 0x0f: ("Subsys_0f",), 0x11: ("HeroOp_a",), 0x12: ("HeroOp_b",),
    0x13: ("HeroOp_c",), 0x14: ("Subsys_14",), 0x15: ("Subsys_15",), 0x16: ("ObjMgrOp_16",),
    0x17: ("QuestStateA",), 0x18: ("CheckGroup",), 0x19: ("Subsys_19",), 0x1a: ("QuestTrigger",),
    0x1f: ("DialogShow_v2",), 0x20: ("Subsys_20",), 0x23: ("BigSubsys_23_cleanup",), 0x24: ("HeroOp_d",),
    0x25: ("PlaySound_a",), 0x26: ("PlaySound_b",), 0x27: ("Subsys_27",), 0x28: ("Subsys_28",),
    0x29: ("ObjMgrGetData",), 0x2b: ("END_ALT",), 0x2c: ("NPC_UWR_dispatch",), 0x2d: ("Subsys_2d",),
    0x2f: ("Subsys_2f",), 0x30: ("Subsys_30",), 0x31: ("TriggerReset",), 0x32: ("MiscAction",),
    0x33: ("BigSubsys_33_cleanup",), 0x34: ("HeroOp_e",), 0x35: ("QuestLogSet",), 0x36: ("Subsys_36",),
    0x37: ("ItemDrop",), 0x38: ("Subsys_38",), 0x39: ("Subsys_39",), 0x3a: ("ConditionalEval",),
    0x3b: ("ELSE_jump",), 0x3c: ("ResRef",), 0x3d: ("InlineHandler_3d",), 0x40: ("QuestKompassOBJ",),
    0x41: ("VarDecl_int",), 0x42: ("BlockReader",), 0x43: ("VarDecl_C",), 0x44: ("HeroQBit_set",),
    0x45: ("HeroQBit_clear",), 0x46: ("HeroTargetCheck",), 0x47: ("DialogShow_v3",), 0x48: ("HeroOp_f",),
    0x49: ("CreateLake",), 0x4a: ("ObjectCreate",), 0x4b: ("VarAssign_b",), 0x4c: ("VarAssign_c",),
    0x4d: ("Subsys_4d",), 0x4e: ("ChestSetup",), 0x4f: ("EventHandler_4f",), 0x56: ("DialogShow_v4",),
    0x57: ("Subsys_57",), 0x59: ("HeroEvent",), 0x5a: ("HeroOp_g",), 0x5b: ("UI_InventoryOp",),
    0x5c: ("DirectTeleport",), 0x5d: ("Subsys_5d",), 0x5e: ("Hideout_a",), 0x5f: ("DialogShow_v5",),
    0x60: ("Subsys_60",), 0x61: ("Subsys_61",), 0x62: ("SetupSector",), 0x63: ("HeroOp_h",),
    0x64: ("SectorPosOp",), 0x67: ("HeroOp_i",), 0x68: ("PlayFX_HeroSound",), 0x69: ("VarAssign_int",),
    0x6a: ("Subsys_6a",), 0x6b: ("InlineHandler_6b",), 0x6c: ("QuestStateB",), 0x6d: ("PoolClear_v1",),
    0x6e: ("Subsys_6e",), 0x70: ("QuestStateC",), 0x71: ("PoolClear_v2",), 0x73: ("StatementBuild",),
    0x74: ("Subsys_74",), 0x75: ("Subsys_75",), 0x76: ("Subsys_76",), 0x77: ("DQ_QuestSetup",),
    0x78: ("Subsys_78",), 0x79: ("EventHandler_79",), 0x7a: ("NPC_FieldSet",), 0x7b: ("Subsys_7b",),
    0x7c: ("InlineHandler_7c",), 0x7d: ("EventBroadcast",), 0x80: ("PlayFX_c",), 0x84: ("ShowJokeImage",),
    0x85: ("PoolGetPos",), 0x87: ("Subsys_87",), 0x88: ("Subsys_88",), 0x89: ("Subsys_89",),
    0x8a: ("UI_TaskbarOp_b",), 0x8b: ("UI_TaskbarOp_c",), 0x8c: ("Hideout_b",),
}

# tags whose payload the walker reads raw, never through FUN_00472bc0
# (funkcode_disasm.RAW_LAYOUTS decodes them)
RAW_PAYLOAD_TAGS = (0x28, 0x29, 0x2f)


def label_for(tag):
    info = TAGS.get(tag)
    if info:
        return info[0]
    return f"TAG_0x{tag:02x}"


def keyword_for(tag):
    """The compiler keyword(s) for a tag, or () when none exists."""
    return KEYWORDS.get(tag, ())


def aliases_for(tag):
    """Labels older tables printed for this tag."""
    return ALIASES.get(tag, ())


def tag_for(name):
    """Tag number for a label, keyword or old alias (case-insensitive,
    trailing ':' and '*' ignored). None if unknown."""
    key = name.strip().rstrip(":*").lower()
    for t, (lab, _va, _p) in TAGS.items():
        if lab.rstrip("*").lower() == key:
            return t
    for table in (KEYWORDS, ALIASES):
        for t, names in table.items():
            if any(n.rstrip(":").lower() == key for n in names):
                return t
    for t, (lab, _va, _p) in TAGS.items():       # 'DelNPC/DelOBJ' halves
        if key in (p.lower() for p in lab.split("/")):
            return t
    return None


if __name__ == "__main__":
    print("| tag | label | keyword(s) | handler | walker case | old label | purpose |")
    print("|---|---|---|---|---|---|---|")
    for t in sorted(TAGS):
        lab, va, purpose = TAGS[t]
        kw = ", ".join(keyword_for(t)) or "-"
        al = ", ".join(aliases_for(t)) or "-"
        wc = f":{WALKER_CASE[t]}" if t in WALKER_CASE else "default"
        print(f"| 0x{t:02x} | {lab} | {kw} | {va or 'inline/none'} | {wc} | {al} | {purpose} |")
