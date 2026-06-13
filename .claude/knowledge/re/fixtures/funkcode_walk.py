#!/usr/bin/env python3
"""
FunkCode TLV stream walker + tag histogram.

Record framing (confirmed against walker FUN_00475680 line 156 and
npc_decode2.py:97):
    tag : u8
    size: u16 BIG-ENDIAN  (size INCLUDES the 3-byte header)
    payload: (size-3) bytes

The walker (FUN_00475680) reads the tag as (ushort)local_a0c.pVFTable
(byte 0) and the size as local_a0c.pVFTable._2_2_ (bytes 2-3). It only
dispatches tags 1..0x8c; tag 0 and 0x2b are no-ops/separators.

Inside a payload, opcodes are read by FUN_00472bc0; integer operands
are LITTLE-endian. This script only does the outer TLV walk + a tag
histogram; per-opcode decode lives in npc_decode2.py for tag 0x01.

Usage: python funkcode_walk.py [path-to-FunkCode.bin]
"""
import sys, struct, collections

DEFAULT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin"

# Human names recovered from the script compiler / handler analysis.
# (subsystem, name) — see funkcode_tags.md for evidence/confidence.
TAGNAME = {
    0x00: "END / pad (no-op)",
    0x01: "CreateNPC",
    0x02: "CreateItem/Object (FUN_0048ae90)",
    0x03: "DialogShow (FUN_0048bb40)",
    0x04: "rec04 FUN_004819a0",
    0x05: "rec05 FUN_004817f0",
    0x07: "rec07 FUN_00481d80",
    0x08: "rec08 FUN_00485c10",
    0x0a: "IF-name-NOTset block (skip if flag&2==0)",
    0x0b: "IF-name-set block (skip if flag&2!=0)",
    0x0c: "IF block flag&4==0",
    0x0d: "IF block flag&4!=0",
    0x0e: "SET-flag-by-name (DAT_00aab708 +0x50)",
    0x0f: "rec0f FUN_0048ee20",
    0x11: "rec11 FUN_0048d480",
    0x12: "rec12 FUN_0048da40",
    0x13: "rec13 FUN_0048dd10",
    0x14: "rec14 FUN_0048f030",
    0x15: "rec15 FUN_0048f7f0",
    0x16: "rec16 FUN_004813d0",
    0x17: "QuestStateA (FUN_00478780)",
    0x18: "cond FUN_0048c860 (skip-block)",
    0x19: "rec19 FUN_00490a30",
    0x1a: "scope/flag rec (FUN_00472bc0 stream; sets ctx+0x15)",
    0x1f: "DialogShow2 (FUN_0048f9e0)",
    0x20: "rec20 FUN_00490be0",
    0x21: "rec21 FUN_00490cc0",
    0x23: "rec23 FUN_0047c610",
    0x24: "rec24 FUN_0048e4d0",
    0x25: "rec25 FUN_0048f330",
    0x26: "rec26 FUN_0048f330",
    0x27: "rec27 FUN_004918b0",
    0x28: "NetEvent broadcast/queue",
    0x29: "ObjMgrGetData cond (NPC-array gate)",
    0x2b: "no-op (case 0/0x2b)",
    0x2c: "rec2c clear-net-slot FUN_005498f0",
    0x2d: "rec2d FUN_00491b30",
    0x2e: "rec2e FUN_00491d40",
    0x2f: "resource-anchor select (in_ECX+0x752c)",
    0x30: "rec30 FUN_00496520",
    0x31: "rec31 FUN_004968a0",
    0x32: "rec32 FUN_00496f20",
    0x33: "rec33 FUN_0047c610 (alias of 0x23)",
    0x34: "rec34 FUN_0048e280",
    0x35: "QuestLogSet/Journal (FUN_00496080)",
    0x36: "rec36 FUN_0048ee20 (alias of 0x0f)",
    0x37: "rec37 FUN_00497f80",
    0x38: "rec38 FUN_004982d0",
    0x39: "rec39 FUN_004985e0",
    0x3a: "IF (open-ended block; skip to 0x3b/0x42)",
    0x3b: "ELSE (skip block to end)",
    0x3c: "rec3c FUN_00499ba0",
    0x3d: "rec3d (consume opcode stream, no-op)",
    0x3f: "rec3f FUN_0049a4b0",
    0x40: "rec40 FUN_0049ac80",
    0x41: "rec41 FUN_0049b2b0",
    0x42: "ELSEIF / BlockReader (skip alt block)",
    0x43: "rec43 FUN_0049b2b0 (alias 0x41)",
    0x44: "HeroQBit_set (FUN_0049b840)",
    0x45: "rec45 FUN_0049c160",
    0x46: "rec46 FUN_0049daf0",
    0x47: "rec47 FUN_0049dcf0",
    0x48: "rec48 FUN_0049e210",
    0x49: "rec49 FUN_0049e760",
    0x4a: "rec4a FUN_0049fc50",
    0x4b: "rec4b FUN_0049c930",
    0x4c: "rec4c FUN_0049cec0",
    0x4d: "rec4d FUN_0048e600",
    0x4e: "rec4e FUN_004a02a0",
    0x4f: "rec4f FUN_004a15a0",
    0x56: "rec56 FUN_004a1a50",
    0x57: "rec57 FUN_004a6ea0",
    0x58: "KernelEvent rec (cKernel 0x18)",
    0x59: "rec59 FUN_004a4040",
    0x5a: "rec5a FUN_004a1f20",
    0x5b: "rec5b FUN_004a2550",
    0x5c: "rec5c FUN_004a2b40",
    0x5d: "rec5d FUN_004a4310",
    0x5e: "rec5e FUN_004a6950",
    0x5f: "rec5f FUN_004a7760",
    0x60: "rec60 FUN_004a79d0",
    0x61: "rec61 FUN_004a81f0",
    0x62: "rec62 FUN_004a8390",
    0x63: "rec63 FUN_004a8bb0",
    0x64: "rec64 FUN_004a9670",
    0x67: "rec67 FUN_0048df30",
    0x68: "rec68 FUN_004a9730 (dialog/quest, param_4 branch)",
    0x69: "rec69 FUN_0049d450",
    0x6a: "rec6a FUN_00465280",
    0x6b: "rec6b store vec into ctx+0x7684 (opcode 0xb)",
    0x6c: "rec6c FUN_004790c0",
    0x6d: "rec6d FUN_004793d0",
    0x6e: "rec6e FUN_00478d80",
    0x6f: "rec6f (uVar9=3 marker)",
    0x70: "rec70 FUN_0047b480",
    0x71: "rec71 FUN_0047b770",
    0x72: "rec72 sound? (FUN_004c1140 by idx 0xa860, ptr 0xa880)",
    0x73: "rec73 FUN_004ab940",
    0x74: "rec74 FUN_004abb60",
    0x75: "rec75 FUN_0048d930",
    0x76: "rec76 FUN_0048ff10",
    0x77: "rec77 FUN_00490500",
    0x78: "rec78 FUN_006a1660/006a16e0",
    0x79: "rec79 FUN_004ac740",
    0x7a: "NPC_FieldSet (NPC-array[idx]+0x18 = vec)",
    0x7b: "rec7b FUN_00491090",
    0x7c: "rec7c sound play (FUN_0061d450..0061db10)",
    0x7d: "rec7d FUN_004ac940",
    0x7e: "rec7e FUN_006770e0/00696060",
    0x7f: "rec7f FUN_004adaa0",
    0x80: "rec80 FUN_004add60",
    0x81: "rec81 FUN_006770e0/006791c0",
    0x82: "Teleporter (cInterpretSQW_Teleporter_004ae040)",
    0x83: "rec83 FUN_006413e0/00641720 (toggle by vec)",
    0x84: "rec84 FUN_004ae350",
    0x85: "cond FUN_0047a0c0 (skip-block)",
    0x86: "KernelEvent rec2 (cKernel 0x19 + objmgr)",
    0x87: "rec87 FUN_004af190",
    0x88: "rec88 FUN_004af8c0",
    0x89: "rec89 FUN_004afff0",
    0x8a: "rec8a FUN_004b0790",
    0x8b: "rec8b FUN_004b0970",
    0x8c: "rec8c FUN_004b0c00",
}


def walk(data):
    off = 0
    recs = []
    n = len(data)
    while off + 3 <= n:
        tag = data[off]
        size = (data[off + 1] << 8) | data[off + 2]   # BIG-endian, incl header
        if size < 3:
            recs.append((off, tag, size, b"", "BAD_SIZE<3"))
            break
        if off + size > n:
            recs.append((off, tag, size, b"", "OVERRUN"))
            break
        pl = data[off + 3:off + size]
        recs.append((off, tag, size, pl, ""))
        off += size
    return recs, off, n


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    with open(path, "rb") as f:
        data = f.read()
    recs, end, n = walk(data)

    bad = [r for r in recs if r[4]]
    hist = collections.Counter(r[1] for r in recs if not r[4])

    print(f"file        : {path}")
    print(f"size        : {n} bytes")
    print(f"records     : {len(recs)}")
    print(f"parsed upto : 0x{end:x} ({end}) "
          f"{'CLEAN (full file consumed)' if end == n else 'STOPPED EARLY'}")
    if bad:
        print(f"!! bad records: {bad[:5]}")
    print()
    print(f"{'tag':>5} {'count':>7}  name")
    print("-" * 70)
    for tag, cnt in sorted(hist.items(), key=lambda kv: -kv[1]):
        nm = TAGNAME.get(tag, "??? UNKNOWN")
        print(f" 0x{tag:02x} {cnt:7d}  {nm}")
    print("-" * 70)
    print(f"distinct tags: {len(hist)}")

    # Storyline-relevant tags of interest.
    interest = {0x01: "CreateNPC", 0x02: "CreateItem", 0x03: "DialogShow",
                0x1f: "DialogShow2", 0x17: "QuestStateA",
                0x35: "QuestLog/Journal", 0x44: "HeroQBit_set",
                0x0a: "IF flag&2==0", 0x0b: "IF flag&2!=0",
                0x0c: "IF flag&4==0", 0x0d: "IF flag&4!=0",
                0x0e: "SET flag by name", 0x3a: "IF block",
                0x3b: "ELSE", 0x42: "ELSEIF/BlockReader",
                0x29: "ObjMgr cond", 0x7a: "NPC_FieldSet",
                0x82: "Teleporter", 0x6b: "store vec"}
    print("\nstoryline-relevant tag counts:")
    for t in sorted(interest):
        print(f" 0x{t:02x} {hist.get(t,0):6d}  {interest[t]}")


if __name__ == "__main__":
    main()
