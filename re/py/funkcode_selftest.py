"""Self-contained round-trip test for the FunkCode (de)compiler pair.

`funkcode_roundtrip_test.py` is the real thing: it walks the game's own
`bin/**/*.bin` and asserts `compile(decompile(x)) == x` for every byte. It needs
a Sacred install, so CI cannot run it.

This test needs nothing. It SYNTHESISES records covering every operand form in
the engine's field-reader grammar (`funkcode_disasm.GRAMMAR`, which is a
transcription of `FUN_00472bc0`), plus a fixed corpus of the exact record shapes
the SDK's Lua builders emit, and round-trips those. A change to the opcode table
that breaks an operand family fails here, on any machine, in under a second.

    python re/py/funkcode_selftest.py            # synthetic corpus only
    python re/py/funkcode_selftest.py --root .   # ALSO the real bin/ tree, if present

Exit code 0 = every stream survived the round trip.
"""
import argparse
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import funkcode_disasm as dis
import funkcode_decompile as dec
import funkcode_compile as cmp

U32 = lambda v: struct.pack("<I", v & 0xFFFFFFFF)
I32 = lambda v: struct.pack("<i", v)
U16 = lambda v: struct.pack("<H", v & 0xFFFF)
SYMREF = b"\x9f\xef\xbe\xed\xfe"


def zstr(s):
    return s.encode("latin1") + b"\x00"


def record(tag, payload):
    """TLV as the engine reads it: tag u8, size u16 BE including the 3-byte header."""
    size = len(payload) + 3
    assert size <= 255, "a record over 255 bytes would misread in the engine"
    return bytes([tag, (size >> 8) & 0xFF, size & 0xFF]) + payload


# --- one operand per form ---------------------------------------------------
# The values are deliberately boring: nothing negative where a negative number
# changes the width (i32+asciiz), and no variable sentinel (-0x12D687).
def operand_bytes(op, form):
    b = bytes([op])
    if form == "flag":        return b
    if form == "u8":          return b + bytes([3])
    if form == "u16":         return b + U16(0x1234)
    if form == "u16x2":       return b + U16(1) + U16(2)
    if form == "u16x4":       return b + U16(1) + U16(2) + U16(3) + U16(4)
    if form == "u32":         return b + U32(0x11223344)
    if form == "u32x2":       return b + U32(7) + U32(8)
    if form == "u32x3":       return b + U32(7) + U32(8) + U32(9)
    if form == "u32x4":       return b + U32(7) + U32(8) + U32(9) + U32(10)
    if form == "u32|var":     return b + U32(99)
    if form == "i16in4":      return b + U32(5)
    if form == "asciiz":      return b + zstr("res:SELFTEST")
    if form == "asciiz2":     return b + zstr("hero") + zstr("res:SELFTEST")
    if form == "u32+asciiz":  return b + U32(42) + zstr("selftest_section")
    if form == "i32+asciiz":  return b + I32(7) + zstr("SelfTestVar")
    if form == "pos":         return b + I32(1234) + U32(2345) + U32(0)
    if form == "sel":         return b + bytes([0])
    if form == "symref|u32":  return b + U32(4)
    if form == "symref|raw3": return b + SYMREF + zstr("selftest_symbol")
    return None


def synthetic_stream():
    """One record per wire opcode, plus the variants whose WIDTH depends on the
    operand value -- those are where an opcode-table slip actually bites."""
    out = []
    # A tag whose payload the walker reads through the field reader (0x28/0x29/
    # 0x2f are raw layouts and are covered separately by the fixed corpus).
    TAG = 0x08
    for op in sorted(dis.GRAMMAR):
        row = dis.GRAMMAR[op]
        if row.form == "end":
            continue
        ob = operand_bytes(op, row.form)
        if ob is None:
            continue
        out.append(record(TAG, b"\x00" + ob + b"\x00"))
    # value-dependent widths
    out.append(record(TAG, b"\x00" + bytes([0x04]) + I32(-2) + zstr("CPOS:HERO") + b"\x00"))
    out.append(record(TAG, b"\x00" + bytes([0x37]) + bytes([1]) + zstr("sel_name") + b"\x00"))
    out.append(record(TAG, b"\x00" + bytes([0x1f]) + SYMREF + zstr("symbolic") + b"\x00"))
    return b"".join(out)


# --- the shapes the SDK actually emits --------------------------------------
# Each one is a record a Lua builder in custom/lua/lib/ produces, byte for byte.
def sdk_corpus():
    r = []
    r.append(record(0x1A, b"\x00\x1e" + zstr("MY_TEXT_KEY")))                      # Text
    r.append(record(0x3C, b"\x00\x01" + zstr("res:1037") + b"\x01" + zstr("btn_yes")))  # SetButton
    r.append(record(0x84, b"\x00\x0b" + I32(0) + b"\x01" + zstr("res:MY_BANNER")))  # InfoPlayer
    r.append(record(0x15, b"\x00\x0b" + U32(9501)))                                 # SetUpQuest
    r.append(record(0x14, b"\x00\x0b" + U32(9501)))                                 # TriggerQuest
    r.append(record(0x0F, b"\x00\x0b" + U32(9501)))                                 # ExitQuest
    r.append(record(0x35, b"\x00\x0b" + U32(9501) + b"\x0b" + U32(0)
                    + b"\x01" + zstr("res:MY_TITLE")))                              # QuestBook
    r.append(record(0x3F, b"\x00\x0b" + U32(2793) + b"\x0b" + U32(2284)
                    + b"\x0b" + U32(9501)))                                         # KompassPos
    r.append(record(0x04, b"\x00\x01" + zstr("SDKZONE")
                    + b"\x0c" + I32(2799) + U32(2278) + U32(0)
                    + b"\x0d" + I32(2811) + U32(2290) + U32(0)))                    # SetBaseTrigger
    r.append(record(0x05, b"\x00\x01" + zstr("SDKZONE")))                           # DelBaseTrigger
    r.append(record(0x30, b"\x00\x01" + zstr("sdkwall") + bytes([0x28, 0x00, 0x27])))  # CreateTrigger
    r.append(record(0x31, b"\x00\x01" + zstr("sdkwall") + bytes([0x07, 0x26])))     # SetTriggerState
    r.append(record(0x32, b"\x00\x01" + zstr("sdkwall")
                    + bytes([0x2a]) + I32(2792) + U32(2272) + U32(0)))              # TriggerPatch
    r.append(record(0x08, b"\x00\x01" + zstr("res:MY_CHEST") + b"\x02" + U32(5209)
                    + b"\x04" + I32(-2) + zstr("CPOS:HERO")))                       # CreateObj
    r.append(record(0x01, b"\x00\x02" + U32(257) + b"\x04" + I32(-2) + zstr("CPOS:HERO")))  # CreateNPC
    r.append(record(0x87, b"\x00\x02" + U32(304) + b"\x0b" + I32(5)
                    + b"\x05" + zstr("kill_done")))                                 # SetOnKill
    r.append(record(0x38, b"\x00\x0b" + U32(0x0F000001) + b"\x38" + U32(4)
                    + b"\x01" + zstr("timer_section")))                             # SetTimer
    r.append(record(0x3A, b"\x00\x48" + I32(1) + zstr("SDKQ_99")))                  # IF IsVarEq
    r.append(record(0x3B, b"\x00"))                                                 # ELSE
    r.append(record(0x3E, b"\x00"))                                                 # NOP
    r.append(record(0x6F, b"\x00"))                                                 # SectionEnd
    r.append(record(0x58, b"\x00"))                                                 # SetAnimMode
    r.append(record(0x86, b"\x00"))                                                 # SetCinemaMode
    r.append(record(0x75, b"\x00\x0b" + U32(9501)))                                 # ActivateQuest
    r.append(record(0x57, b"\x00\x0b" + U32(2)))                                    # SetQuestInfo
    r.append(record(0x45, b"\x00\x01" + zstr("SDKBITS") + b"\x0b" + U32(3)))        # UnsetVarBit
    r.append(record(0x4F, b"\x00\x01" + zstr("res:19554") + b"\x02" + U32(232)))    # Morph
    r.append(record(0x79, b"\x00\x1d" + U32(30000)))                                # AutoSave
    r.append(record(0x63, b"\x00\x0b" + I32(3100) + b"\x0b" + I32(1839)
                    + b"\x01\x00" + b"\x0b" + U32(0x0000ACFF) + b"\x0b" + U32(1)))  # Partikel
    return b"".join(r)


# The decompiler falls back to a HEX line for any record its frozen mnemonic
# vocabulary cannot spell, and a hex record still round-trips byte-exactly. So
# byte equality alone would not notice a broken opcode table -- it would just
# quietly emit more hex. These baselines pin how much of each corpus is spelled
# in mnemonics; if that number moves, the vocabulary moved with it, and
# `sdk/lua_bake_opcodes.inc` (the DLL baker's row-for-row copy) has to move too.
BASELINES = {
    "synthetic grammar": (154, 4),
    "SDK record shapes": (19, 10),
}


def roundtrip(name, blob):
    try:
        text = dec.decompile(blob)
        rebuilt = cmp.compile(text)
    except Exception as e:
        print("  EXC  %-22s %s: %s" % (name, type(e).__name__, e))
        return False
    if rebuilt == blob:
        ok = True
        m = re.search(r"mnemonic=(\d+)\s+hex-fallback=(\d+)", text)
        counts = (int(m.group(1)), int(m.group(2))) if m else None
        want = BASELINES.get(name)
        if want and counts and counts != want:
            print("  FAIL %-22s mnemonic/hex counts %s, expected %s -- the opcode "
                  "vocabulary changed; lua_bake_opcodes.inc must change with it"
                  % (name, counts, want))
            ok = False
        else:
            print("  OK   %-22s %d records, %d bytes%s"
                  % (name, len(list(dis.walk_records(blob))), len(blob),
                     "" if not counts else "  (mnemonic %d, hex %d)" % counts))
        return ok
    n = min(len(blob), len(rebuilt))
    where = next((i for i in range(n) if blob[i] != rebuilt[i]), n)
    print("  FAIL %-22s first diff @ 0x%x: orig=%s rebuilt=%s"
          % (name, where, blob[max(0, where - 4):where + 8].hex(),
             rebuilt[max(0, where - 4):where + 8].hex()))
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", help="a Sacred install: also round-trip every bin/**/*.bin under it")
    args = ap.parse_args()

    print("FunkCode (de)compiler self-test")
    ok = True

    # The DLL's baker carries a C++ copy of the Python opcode vocabulary. They
    # must stay row-for-row identical, and both are in the repo, so this check
    # needs no game files either.
    import disasm_tiling_check as tiling
    rows, problems = tiling.check_legacy_vocabulary()
    if rows is None:
        print("  SKIP vocabulary check: %s" % problems)
    elif problems:
        ok = False
        print("  FAIL vocabulary: funkcode_ops vs lua_bake_opcodes.inc")
        for p in problems:
            print("       " + p)
    else:
        print("  OK   vocabulary           funkcode_ops == lua_bake_opcodes.inc, %d rows" % rows)

    ok &= roundtrip("synthetic grammar", synthetic_stream())
    ok &= roundtrip("SDK record shapes", sdk_corpus())

    if args.root:
        files = sorted(glob.glob(os.path.join(args.root, "bin", "**", "*.bin"), recursive=True))
        print("real corpus: %d file(s) under %s" % (len(files), args.root))
        for path in files:
            with open(path, "rb") as f:
                ok &= roundtrip(os.path.basename(os.path.dirname(path)) + "/" + os.path.basename(path), f.read())

    print("RESULT:", "all streams round-tripped" if ok else "FAILURES ABOVE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
