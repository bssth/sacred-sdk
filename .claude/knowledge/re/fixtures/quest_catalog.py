#!/usr/bin/env python3
# quest_catalog.py - walk Vampiress campaign FunkCode.bin, decode every record,
# group into quests, emit a readable storyline catalog + global summary.
#
# Format: flat TLV stream. tag:u8, size:u16 BIG-ENDIAN (size INCLUDES the
# 3-byte header), payload = size-3 bytes. Walk start..EOF; parses cleanly.
#
# Quest model (reverse-engineered, see vampiress_quests.md / npc_model.md /
# questbook_recon.md / FUN_00475680 walker dispatch):
#   tag 0x01  CreateNPC          (opcode sub-stream; npc.lua type->name)
#   tag 0x03  DialogShow         (res:<textid> + dialog node tag)
#   tag 0x1a  QuestTrigger       (HQ_/NQ_/GQ_ state-node string id)
#   tag 0x35  QuestLogSet        (journal text refs: res:HQ_..._Log_*)
#   tag 0x68  Sound/VoiceRef     (SOUND_FX_... / quest-state voice line)
#   tag 0x3a/0x42  block/role hdr (RTYPE_NPC_* speaker, or u16 node seq)
#   tag 0x3c  DialogButton       (res:<id> response + btn_/ok/nq_ablehnen)
#   tag 0x2e  Move/Teleport      (tptarget_/pos token)
#   tag 0x69/0x7d reward block   (dq_belohnung* / goodie / item)
#   ... full dispatch in TAGNAME below (from FUN_00475680).
#
# Output: vampiress_quests.md (catalog) printed to stdout / written by caller.

import sys, struct, re, collections, os

BIN = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin"
NPC_LUA = r"E:\SteamLibrary\steamapps\common\Sacred Gold\custom\lua\lib\npc.lua"

# ---------------------------------------------------------------------------
# creature type-id -> name, parsed from npc.lua  add(id,"hex","name","note")
# ---------------------------------------------------------------------------
def load_npc_names():
    m = {}
    try:
        txt = open(NPC_LUA, "r", encoding="utf-8", errors="replace").read()
    except OSError:
        return m
    for mo in re.finditer(r'add\(\s*(\d+)\s*,\s*"[0-9A-Fa-f]+"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', txt):
        idn = int(mo.group(1)); name = mo.group(2); note = mo.group(3)
        m[idn] = name + (" (%s)" % note if note else "")
    return m

NPC = load_npc_names()

# ---------------------------------------------------------------------------
# Tag -> human name (derived from walker dispatch FUN_00475680 + handler VAs;
# names per scratch RE docs: npc_model.md, questbook_recon.md, quest_lifecycle.md)
# ---------------------------------------------------------------------------
TAGNAME = {
    0x01: "CreateNPC",            # FUN_00482510
    0x02: "CreateObject/Item",    # FUN_0048ae90
    0x03: "DialogShow",           # FUN_0048bb40
    0x04: "Move/Goto",            # FUN_004819a0
    0x05: "SetName/Label",        # FUN_004817f0
    0x06: "SpawnGroup",           # FUN_00481d80
    0x07: "Anim/Emote",           # FUN_00485c10
    0x08: "WaitFlag/Block08",     # opcode sub-stream
    0x0b: "Block0b",              # opcode sub-stream
    0x0c: "Block0c",
    0x0d: "Block0d",
    0x0e: "Block0e",
    0x0f: "Effect0f",             # FUN_0048ee20
    0x11: "Set11",                # FUN_0048d480
    0x12: "Set12",                # FUN_0048da40
    0x13: "Set13",                # FUN_0048dd10
    0x14: "Set14",                # FUN_0048f030
    0x15: "Set15",                # FUN_0048f7f0
    0x16: "Label16",              # FUN_004813d0
    0x17: "QuestStateA",          # FUN_00478780 (quest state / context)
    0x18: "Cond18",               # FUN_0048c860
    0x19: "Set19",                # FUN_00490a30
    0x1a: "QuestTrigger",         # opcode sub-stream; HQ_/NQ_ state node id
    0x1f: "Quest1f",              # FUN_0048f9e0
    0x20: "Quest20",              # FUN_00490be0
    0x21: "Quest21",              # FUN_00490cc0
    0x23: "Cond23",               # FUN_0047c610
    0x24: "Set24",                # FUN_0048e4d0
    0x25: "Set25",                # FUN_0048f330
    0x26: "Set26",                # FUN_0048f330
    0x27: "Set27",                # FUN_004918b0
    0x28: "NetBroadcast28",       # FUN_00416780 set-state chain
    0x29: "ObjMgrGetData",        # NPC-array by name (stride 0x34)
    0x2b: "Side2b",
    0x2c: "Sound2c",              # FUN_005498f0
    0x2d: "Set2d",                # FUN_00491b30
    0x2e: "Move/Teleport2e",      # FUN_00491d40 (tptarget_/pos)
    0x2f: "Cond2f",
    0x30: "Quest30",              # FUN_00496520
    0x31: "Quest31",              # FUN_004968a0
    0x32: "Quest32",              # FUN_00496f20
    0x33: "Side33",
    0x34: "Quest34",              # FUN_0048e280
    0x35: "QuestLogSet",          # FUN_00496080 (journal text refs +0x24..)
    0x36: "Effect36",             # FUN_0048ee20
    0x37: "Quest37",              # FUN_00497f80
    0x38: "Quest38",              # FUN_004982d0
    0x39: "Quest39",              # FUN_004985e0
    0x3a: "BlockHdr/Role",        # FUN_004987b0 (RTYPE_NPC_* speaker / node seq)
    0x3b: "BlockEnd",             # FUN_0066ef40
    0x3c: "DialogButton",         # response option (res:<id>+btn_/ok/nq_)
    0x3d: "Cond3d",
    0x3e: "NodeMark3e",
    0x3f: "QuestKompassPos",      # FUN_0049a4b0 (marker +0x10/+0x14/+0x20)
    0x40: "QuestKompassOBJ",      # FUN_0049ac80 (marker obj)
    0x41: "Quest41",              # FUN_0049b2b0
    0x42: "BlockHdr/Role2",       # FUN_xxxx (RTYPE/node seq, like 0x3a)
    0x43: "Quest43",              # FUN_0049b2b0
    0x44: "HeroQBit_set",         # FUN_0049b840
    0x45: "Quest45",              # FUN_0049c160
    0x46: "Quest46",              # FUN_0049daf0
    0x47: "Quest47",              # FUN_0049dcf0
    0x48: "Quest48",              # FUN_0049e210
    0x49: "Quest49",              # FUN_0049e760
    0x4a: "Quest4a",              # FUN_0049fc50
    0x4b: "Quest4b",              # FUN_0049c930
    0x4c: "Quest4c",              # FUN_0049cec0
    0x4d: "QuestRemove",          # FUN_0048e600 (delete registry entry)
    0x4e: "Quest4e",              # FUN_004a02a0
    0x4f: "Quest4f",              # FUN_004a15a0
    0x56: "Quest56",              # FUN_004a1a50
    0x57: "QuestPageSet",         # FUN_004a6ea0 (+0x16C journal page/tab)
    0x58: "Sound58",              # FUN_00808e50
    0x59: "Quest59",              # FUN_004a4040
    0x5a: "Quest5a",              # FUN_004a1f20
    0x5b: "Quest5b",              # FUN_004a2550
    0x5c: "Quest5c",              # FUN_004a2b40
    0x5d: "Quest5d",              # FUN_004a4310
    0x5e: "Quest5e",              # FUN_004a6950
    0x5f: "Quest5f",              # FUN_004a7760
    0x60: "Quest60",              # FUN_004a79d0
    0x61: "Quest61",              # FUN_004a81f0
    0x62: "Quest62",              # FUN_004a8390
    0x67: "Set67",                # FUN_0048df30
    0x68: "Sound/VoiceRef",       # FUN_004a9730 (SOUND_FX / quest voice line)
    0x69: "Reward/Goodie",        # FUN_0049d450 (dq_belohnung*)
    0x6a: "Set6a",                # FUN_00465280
    0x6b: "QuestStateB",          # opcode sub-stream
    0x6c: "QuestStateB2",         # FUN_004790c0
    0x6d: "Quest6d",              # FUN_004793d0
    0x6e: "Quest6e",              # FUN_00478d80
    0x6f: "QuestStateC",          # FUN_0047b480
    0x70: "QuestStateC2",         # FUN_0047b480
    0x71: "Quest71",              # FUN_0047b770
    0x72: "Cond72",               # opcode sub-stream
    0x73: "Set73",                # FUN_004ab940 (most common tag)
    0x74: "Quest74",              # FUN_004abb60
    0x75: "QuestClassSlot",       # FUN_0048d930 (per-class marker slot)
    0x76: "Quest76",              # FUN_0048ff10
    0x77: "DQ_QuestSetup",        # FUN_00490500 (daily-quest pool +0x31c)
    0x78: "Sound78",              # FUN_006a1660
    0x79: "Set79",                # FUN_004ac740
    0x7a: "NPC_FieldSet",         # NPC-array entry +0x18 (wire field 0x38)
    0x7b: "Set7b",                # FUN_00491090
    0x7c: "Cond7c",               # opcode sub-stream
    0x7d: "Reward/ItemBlock",     # FUN_004ac940 (goodie/item, 0xEEEEEEEE rec)
    0x7e: "Sound7e",              # FUN_00696060
    0x7f: "Quest7f",              # FUN_004adaa0
    0x80: "Quest80",              # FUN_004add60
    0x81: "Sound81",              # FUN_006791c0
    0x82: "Quest82",
    0x83: "Sound83",              # FUN_00641720
    0x84: "Quest84",              # FUN_004ae350
    0x85: "Side85",               # FUN_0047a0c0
    0x86: "Sound86",              # FUN_00808e50 + FUN_0054e6d0
    0x87: "Quest87",              # FUN_004af190
    0x88: "Quest88",              # FUN_004af8c0
    0x89: "Quest89",              # FUN_004afff0
    0x8a: "Side8a",               # FUN_004b0790
    0x8b: "Quest8b",              # FUN_004b0970
    0x8c: "Quest8c",              # FUN_004b0c00
}

# ---------------------------------------------------------------------------
# CreateNPC (tag 0x01) opcode sub-stream decoder (from npc_decode2.py /
# npc_model.md / FUN_00472bc0 + FUN_00482510)
# ---------------------------------------------------------------------------
W4  = {0x02, 0x11, 0x36, 0x75}
W4P = {0x04, 0x0c, 0x0d, 0x2a}
W2  = {0x03, 0x0a, 0x6b, 0x6c, 0x93, 0x9b, 0x9c}
W8  = {0x15}
W12 = {0x19}
WS  = {0x01, 0x29, 0x63, 0x68, 0x69, 0x6a, 0x9d, 0x16, 0x05, 0x09}

SIDE_OP = {
    0x08: "ally(local_6c8=1)", 0x0e: "neutral(local_6c8=0)",
    0x2b: "side(EBX=8)", 0x2e: "side(EBX|=0x10)",
    0x2f: "ENEMY(EBX=0x40)", 0x31: "ENEMY(EBX=0x40)",
    0x30: "side(EBX=0x20)", 0x32: "side(EBX=0x80)", 0x42: "side(EBX=0x400)",
}

def decode_createnpc(pl):
    """Return dict: type, type_name, sub_id, name, pos, level, side, flags,
    link (existing-NPC link), group, raw opcode list."""
    out = {"type": None, "type_name": None, "sub_id": None, "name": None,
           "pos": None, "level": None, "side": [], "group": None,
           "link": None, "stationary": False, "invuln": False,
           "awake": False, "ops": []}
    if not pl:
        return out
    off = 1  # skip leading flags byte
    n = len(pl)
    seen_type = False
    while off < n:
        op = pl[off]
        if op == 0x00:
            out["ops"].append("END")
            break
        if op in W4:
            if off + 5 > n:
                out["ops"].append("op%02x:TRUNC" % op); break
            v = struct.unpack_from("<i", pl, off + 1)[0]; off += 5
            if op == 0x02:
                if not seen_type:
                    out["type"] = v
                    out["type_name"] = NPC.get(v, "type#%d" % v)
                    seen_type = True
                else:
                    out["sub_id"] = v
            elif op == 0x11:
                out["group"] = v
            out["ops"].append("op%02x=%d" % (op, v))
        elif op in W4P:
            if off + 5 > n:
                out["ops"].append("op%02x:TRUNC" % op); break
            v = struct.unpack_from("<i", pl, off + 1)[0]
            if v == -2:
                e = pl.find(b"\0", off + 5); e = n if e < 0 else e
                s = pl[off + 5:e].decode("latin1", "replace")
                out["pos"] = s; off = e + 1
                out["ops"].append("POS=%r" % s)
            else:
                out["pos"] = "vx#%d" % v; off += 5
                out["ops"].append("POS=vx#%d" % v)
        elif op in W2:
            if off + 3 > n:
                out["ops"].append("op%02x:TRUNC" % op); break
            v = struct.unpack_from("<H", pl, off + 1)[0]; off += 3
            if op == 0x03:
                out["level"] = v
            elif op == 0x6b and v > 0:
                out["stationary"] = True
            out["ops"].append("op%02x=%d" % (op, v))
        elif op in W8:
            if off + 9 > n:
                out["ops"].append("op%02x:TRUNC" % op); break
            vs = struct.unpack_from("<4H", pl, off + 1); off += 9
            out["ops"].append("rect%s" % (vs,))
        elif op in W12:
            if off + 13 > n:
                out["ops"].append("op%02x:TRUNC" % op); break
            vs = struct.unpack_from("<3i", pl, off + 1); off += 13
            out["ops"].append("3i32%s" % (vs,))
        elif op in WS:
            e = pl.find(b"\0", off + 1); e = n if e < 0 else e
            s = pl[off + 1:e].decode("latin1", "replace"); off = e + 1
            if op == 0x01:
                out["name"] = s
            elif op == 0x09:
                out["link"] = s
            out["ops"].append("op%02x=%r" % (op, s))
        else:
            if op in SIDE_OP:
                out["side"].append(SIDE_OP[op])
            if op == 0x12:
                out["awake"] = True
            if op == 0xa1:
                out["invuln"] = True
            out["ops"].append("flag%02x" % op)
            off += 1
    return out

# ---------------------------------------------------------------------------
# Walk records
# ---------------------------------------------------------------------------
def walk(d):
    off = 0; recs = []
    while off + 3 <= len(d):
        tag = d[off]
        size = (d[off + 1] << 8) | d[off + 2]
        if size < 3 or off + size > len(d):
            recs.append((off, "ERROR", size, b""))
            break
        recs.append((off, tag, size, d[off + 3:off + size]))
        off += size
    return recs, off

PRINT_RE = re.compile(rb"[\x20-\x7e]{3,}")
def ascii_runs(pl):
    return [m.decode("latin1") for m in PRINT_RE.findall(pl)]

# Quest-id string forms:
#   HQ (Hauptquest/main):  HQ_<ch>_<quest>_<step>_<role>_NPC_<purpose>_<state>
#       -> quest grouped by HQ_<ch>_<quest>_<step>  (the storyline node)
#   NQ/GQ/GDQ (side/guild): <LINE>_<numericId>_<role>_<state>
#       -> quest grouped by <LINE>_<numericId>  (one quest per numeric id;
#          role/state are sub-nodes inside that one quest)
QID_RE = re.compile(r"^(HQ|NQ|GQ|GDQ|DQ)_(.+)$")
def quest_key(s):
    m = QID_RE.match(s)
    if not m:
        return None
    line = m.group(1)
    parts = re.split(r"[._]", s)
    if line == "HQ" and len(parts) >= 4 and re.match(r"^[0-9]", parts[1]):
        return "_".join(parts[:4])               # HQ_3_1_4
    # NQ/GQ/GDQ/DQ: group by leading numeric id only
    if len(parts) >= 2 and re.match(r"^[0-9]", parts[1]):
        return "%s_%s" % (line, parts[1])        # NQ_5001 / GQ_9010
    if len(parts) >= 2:
        return "%s_%s" % (line, parts[1])
    return line

def chapter_of(qk):
    label = {"NQ": "side quests", "GQ": "guild/generic",
             "GDQ": "guild daily", "DQ": "daily quests"}
    m = re.match(r"^(HQ|NQ|GQ|GDQ|DQ)_([0-9]+)", qk or "")
    if m:
        line, num = m.group(1), m.group(2)
        if line == "HQ":
            return "HQ chapter %s (main story)" % num
        return "%s line (%s)" % (line, label.get(line, line))
    m2 = re.match(r"^(HQ|NQ|GQ|GDQ|DQ)_", qk or "")
    if m2:
        line = m2.group(1)
        if line == "HQ":
            return "HQ misc/global (main story)"
        return "%s line (%s, named ids)" % (line, label.get(line, line))
    return "misc"

# ---------------------------------------------------------------------------
def main():
    d = open(BIN, "rb").read()
    recs, consumed = walk(d)
    clean = consumed == len(d) and recs and recs[-1][1] != "ERROR"

    # ---- global tag histogram ----
    hist = collections.Counter()
    for r in recs:
        if r[1] == "ERROR":
            continue
        hist[r[1]] += 1

    # ---- pre-decode CreateNPC, collect strings ----
    npc_names = collections.Counter()
    npc_types = collections.Counter()
    all_strings = collections.Counter()
    cn_cache = {}
    for i, r in enumerate(recs):
        if r[1] == "ERROR":
            continue
        o, t, s, pl = r
        for st in ascii_runs(pl):
            all_strings[st] += 1
        if t == 0x01:
            cn = decode_createnpc(pl)
            cn_cache[i] = cn
            if cn["name"]:
                npc_names[cn["name"]] += 1
            if cn["type_name"]:
                npc_types[cn["type_name"]] += 1

    # ---- quest segmentation: assign every record to the current quest_key.
    # The quest-id strings live mostly in tags 0x1a/0x35/0x68/0x3c.  We sweep
    # the stream; the most-recent HQ_/NQ_/GQ_ key (4-component) opened by a
    # 0x1a/0x35/0x68 record becomes the active quest until a different key
    # appears.  Records before the first key go to PRELUDE.
    quests = collections.OrderedDict()       # qkey -> list of record idx
    order = []
    cur = "PRELUDE"
    quests[cur] = []
    qfull = {}                               # qkey -> set(full id strings)
    for i, r in enumerate(recs):
        if r[1] == "ERROR":
            continue
        o, t, s, pl = r
        newkey = None
        if t in (0x1a, 0x35, 0x68, 0x3c, 0x17, 0x44, 0x69):
            for st in ascii_runs(pl):
                # strip leading res:/Res:/RES:/SOUND_FX_ wrappers
                bare = re.sub(r"^(res:|Res:|RES:|SOUND_FX_|sound_fx_)", "", st)
                qk = quest_key(bare)
                if qk and re.match(r"^(HQ|NQ|GQ|GDQ)_", qk):
                    newkey = qk
                    qfull.setdefault(qk, set()).add(st)
                    break
        if newkey and newkey != cur:
            cur = newkey
            if cur not in quests:
                quests[cur] = []
                order.append(cur)
        quests[cur].append(i)

    # ---- emit catalog ----
    L = []
    P = L.append
    P("# Vampiress campaign — FunkCode storyline catalog\n")
    P("Auto-generated by `sdk/tools/scratch/quest_catalog.py` from "
      "`bin/TYPE_NPC_VAMPIRELADY/FunkCode.bin`.  Descriptive RE; no source "
      "modified.  Cross-refs: `npc_model.md`, `questbook_recon.md`, "
      "`quest_lifecycle.md`, walker dispatch `FUN_00475680`.\n")

    # global summary
    P("## Global summary\n")
    P("| Metric | Value |")
    P("|---|---|")
    P("| File size | %d bytes |" % len(d))
    P("| Records parsed | %d |" % sum(1 for r in recs if r[1] != "ERROR"))
    P("| Bytes consumed | %d / %d |" % (consumed, len(d)))
    P("| Clean parse to EOF | %s |" % ("YES" if clean else "NO — see error tail"))
    P("| Distinct tags | %d |" % len(hist))
    P("| CreateNPC (0x01) records | %d |" % hist.get(0x01, 0))
    P("| Distinct spawned NPC unique-names | %d |" % len(npc_names))
    P("| Distinct creature types spawned | %d |" % len(npc_types))
    P("| Distinct ASCII string literals | %d |" % len(all_strings))
    P("| Quest groups (HQ/NQ/GQ/GDQ keys) | %d |"
      % sum(1 for k in quests if re.match(r"^(HQ|NQ|GQ|GDQ)_", k)))
    P("")

    P("### Tag histogram (all %d record kinds, by count)\n" % len(hist))
    P("| tag | name | count |")
    P("|---|---|---|")
    for t, c in hist.most_common():
        P("| 0x%02x | %s | %d |" % (t, TAGNAME.get(t, "?"), c))
    P("")

    # chapter overview
    chapters = collections.OrderedDict()
    for qk in quests:
        if not re.match(r"^(HQ|NQ|GQ|GDQ)_", qk):
            continue
        ch = chapter_of(qk)
        chapters.setdefault(ch, []).append(qk)
    P("### Storyline chapters / quest-line overview\n")
    P("Quest keys are the 4-component dialog/state ids "
      "`<LINE>_<chapter>_<quest>_<step>` (HQ=Hauptquest/main, NQ=Nebenquest/"
      "side, GQ/GDQ=generic/guild). Full state nodes (`_Qstart`, `_Qoffen`, "
      "`_Qsieg`, `_Qend`, `_noQuest`, role infixes `_vamp_`/`_glad_`/...) are "
      "listed per quest below.\n")
    for ch, qks in chapters.items():
        P("- **%s** — %d quest groups: %s"
          % (ch, len(qks), ", ".join(qks)))
    P("")

    # per-quest detail
    P("## Quests (record-by-record)\n")
    for qk, idxs in quests.items():
        if not idxs:
            continue
        # tag mix for this quest
        qh = collections.Counter(recs[i][1] for i in idxs if recs[i][1] != "ERROR")
        full = sorted(qfull.get(qk, []))
        P("### %s" % qk)
        P("- records: %d  | span 0x%06x..0x%06x"
          % (len(idxs), recs[idxs[0]][0], recs[idxs[-1]][0]))
        if full:
            P("- state/text nodes: %s" % ", ".join(full[:40])
              + (" …(+%d)" % (len(full) - 40) if len(full) > 40 else ""))
        P("- tag mix: %s"
          % ", ".join("0x%02x×%d" % (t, c) for t, c in qh.most_common()))

        # NPCs spawned in this quest
        spawns = []
        for i in idxs:
            if recs[i][1] == "ERROR" or recs[i][1] != 0x01:
                continue
            cn = cn_cache.get(i)
            if not cn:
                continue
            tag_off = recs[i][0]
            desc = "%s" % (cn["type_name"] or "type?")
            if cn["name"]:
                desc += ' "%s"' % cn["name"]
            extra = []
            if cn["pos"]:
                extra.append("@%s" % cn["pos"])
            if cn["level"]:
                extra.append("lvl%d" % cn["level"])
            if cn["side"]:
                extra.append("side=%s" % "/".join(cn["side"]))
            if cn["group"] is not None:
                extra.append("grp%d" % cn["group"])
            if cn["stationary"]:
                extra.append("stationary")
            if cn["invuln"]:
                extra.append("invuln")
            if cn["awake"]:
                extra.append("awake")
            if cn["link"]:
                extra.append("link->%s" % cn["link"])
            spawns.append((tag_off, desc, extra))
        if spawns:
            P("- **NPCs / spawns (%d):**" % len(spawns))
            for off, desc, extra in spawns:
                P("  - `0x%06x` %s  %s" % (off, desc,
                                           " ".join(extra)))

        # dialog text refs, buttons, sounds, moves, rewards, markers
        def collect(tags):
            out = []
            for i in idxs:
                if recs[i][1] == "ERROR" or recs[i][1] not in tags:
                    continue
                ss = ascii_runs(recs[i][3])
                if ss:
                    out.append((recs[i][0], recs[i][1], ss))
            return out

        dlg = collect({0x03})
        if dlg:
            P("- **Dialog nodes (0x03 DialogShow, %d):**" % len(dlg))
            for off, t, ss in dlg[:60]:
                P("  - `0x%06x` %s" % (off, " | ".join(ss)))
            if len(dlg) > 60:
                P("  - …(+%d more)" % (len(dlg) - 60))

        btn = collect({0x3c})
        if btn:
            P("- **Dialog responses / buttons (0x3c, %d):**" % len(btn))
            for off, t, ss in btn[:40]:
                P("  - `0x%06x` %s" % (off, " | ".join(ss)))
            if len(btn) > 40:
                P("  - …(+%d more)" % (len(btn) - 40))

        snd = collect({0x68, 0x58, 0x78, 0x7e, 0x81, 0x83, 0x86, 0x2c})
        if snd:
            P("- **Voice / sound refs (%d):**" % len(snd))
            seen = set()
            shown = 0
            for off, t, ss in snd:
                key = tuple(ss)
                if key in seen:
                    continue
                seen.add(key)
                P("  - `0x%06x` 0x%02x %s" % (off, t, " | ".join(ss)))
                shown += 1
                if shown >= 40:
                    P("  - …(+%d more, deduped)" % (len(snd) - shown))
                    break

        mv = collect({0x2e, 0x04})
        if mv:
            P("- **Moves / teleports (0x2e/0x04, %d):**" % len(mv))
            for off, t, ss in mv[:30]:
                P("  - `0x%06x` 0x%02x %s" % (off, t, " | ".join(ss)))
            if len(mv) > 30:
                P("  - …(+%d more)" % (len(mv) - 30))

        rw = collect({0x69, 0x7d})
        if rw:
            P("- **Rewards / items / goodies (0x69/0x7d, %d):**" % len(rw))
            for off, t, ss in rw[:30]:
                P("  - `0x%06x` 0x%02x %s" % (off, t, " | ".join(ss)))
            if len(rw) > 30:
                P("  - …(+%d more)" % (len(rw) - 30))

        mk = collect({0x3f, 0x40, 0x75, 0x57})
        if mk:
            P("- **Quest markers / journal page (0x3f/0x40/0x75/0x57, %d):**"
              % len(mk))
            for off, t, ss in mk[:20]:
                P("  - `0x%06x` 0x%02x %s" % (off, t, " | ".join(ss)))
        P("")

    # ---- appendices ----
    P("## Appendix A — all spawned creature types\n")
    P("| creature (npc.lua) | spawn count |")
    P("|---|---|")
    for nm, c in npc_types.most_common():
        P("| %s | %d |" % (nm, c))
    P("")

    P("## Appendix B — all unique spawned NPC names (tag 0x01 op 0x01)\n")
    P("%d distinct.\n" % len(npc_names))
    for nm, c in sorted(npc_names.items()):
        P("- `%s`%s" % (nm, "  ×%d" % c if c > 1 else ""))
    P("")

    P("## Appendix C — string-literal index (keys: positions, dialog, "
      "quest, resources, sounds)\n")
    groups = collections.OrderedDict([
        ("Quest state nodes (HQ_/NQ_/GQ_/GDQ_)", re.compile(r"^(HQ|NQ|GQ|GDQ)_")),
        ("Daily-quest (DQ_)",       re.compile(r"^DQ_")),
        ("Dialog ids (DLG_/dlg_)",  re.compile(r"^(DLG_|dlg_)")),
        ("Positions (pos_/POS_/CPOS:/LOC_)", re.compile(r"^(pos_|POS_|CPOS:|LOC_)")),
        ("Resources (res:/Res:/RES:)", re.compile(r"^(res:|Res:|RES:)")),
        ("Sounds (SOUND_/SFX_)",    re.compile(r"^(SOUND_|SFX_)")),
        ("Teleport targets (tp*)",  re.compile(r"^tp")),
        ("Pools (POOL_)",           re.compile(r"^POOL_")),
        ("RTYPE roles",             re.compile(r"^RTYPE_")),
    ])
    used = set()
    for title, rx in groups.items():
        items = sorted(s for s in all_strings if rx.match(s))
        for s in items:
            used.add(s)
        P("### %s — %d distinct\n" % (title, len(items)))
        for s in items[:400]:
            P("- `%s`%s" % (s, "  ×%d" % all_strings[s] if all_strings[s] > 1 else ""))
        if len(items) > 400:
            P("- …(+%d more)" % (len(items) - 400))
        P("")
    other = sorted(s for s in all_strings if s not in used and len(s) >= 4)
    P("### Other string literals (len>=4, not in groups above) — %d distinct\n"
      % len(other))
    for s in other[:300]:
        P("- `%s`%s" % (s, "  ×%d" % all_strings[s] if all_strings[s] > 1 else ""))
    if len(other) > 300:
        P("- …(+%d more)" % (len(other) - 300))
    P("")

    P("## Notes / cross-check flags\n")
    P("- **Item / reward placements:** tags 0x69 (`Reward/Goodie`, "
      "`dq_belohnung*`) and 0x7d (`Reward/ItemBlock`, the `0xEEEEEEEE` "
      "lead record). No quest-registry field grants rewards "
      "(quest_lifecycle.md §4) — reward is script-driven here.")
    P("- **Voice files:** tag 0x68 carries the per-quest-state voice line "
      "key (the same `HQ_..._Qoffen` id, sometimes wrapped `SOUND_FX_`). "
      "Tags 0x58/0x78/0x7e/0x81/0x83/0x86/0x2c are other sound emitters.")
    P("- **Teleports / moves:** tag 0x2e (`tptarget_*`, `pos_*`) and tag "
      "0x04 (CreateNPC/Move goto). CreateNPC POSITION tokens "
      "(`CPOS:HERO`, `CPOS:RES:*`, DefPos keys) are in Appendix C / per-NPC.")
    P("- **Quest registry:** numeric quest_id (entry+8, stride 0x174) is "
      "*not* an ASCII literal in the stream; the HQ_/NQ_ strings are "
      "dialog/journal *text-resource* keys (`qb_resolve` 0x00672740). "
      "Quest boundaries here are inferred from those keys.")
    P("- **Block structure:** tag 0x3a/0x42 (`BlockHdr/Role`) carries the "
      "speaker `RTYPE_NPC_*` or a u16 node-sequence number; 0x3b ends a "
      "block. Dialog buttons (0x3c) reference text-id `res:NNNN` plus a "
      "choice key (`btn_ok`, `btn_decline`, `nq_ablehnen`, `DQ_OK`).")
    P("- **Side/faction** on spawns decoded per npc_model.md (op 0x08 ally, "
      "0x2f/0x31 enemy, 0x0e neutral; MED confidence — runtime BP open).")
    if not clean:
        P("- **PARSE WARNING:** stream did not consume to EOF; tail record "
          "flagged ERROR. Investigate offset %d." % consumed)

    out_md = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\scratch\vampiress_quests.md"
    open(out_md, "w", encoding="utf-8").write("\n".join(L))
    sys.stdout.write("wrote %s (%d lines, %d quests, %d records)\n"
                     % (out_md, len(L),
                        sum(1 for k in quests if re.match(r"^(HQ|NQ|GQ|GDQ)_", k)),
                        sum(1 for r in recs if r[1] != "ERROR")))

if __name__ == "__main__":
    main()
