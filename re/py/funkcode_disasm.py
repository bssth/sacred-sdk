r"""Sacred FunkCode disassembler -- the operand grammar is the engine's own.

THE AUTHORITY
-------------
Every handler that decodes a record payload calls the field reader
`FUN_00472bc0(&ip, record)` in a loop until it returns 0
(`sdk/re/ghidra/decompiled/00472bc0_FUN_00472bc0.c`; loop sites e.g.
`0047a0c0_FUN_0047a0c0.c:130/675`, `004987b0_FUN_004987b0.c:62/663`,
`004ac940_FUN_004ac940.c:173/183`, walker `00475680_FUN_00475680.c:243/306`).
The reader switches on the byte at `record[ip]`, stores the operand in fixed
context slots (`ctx+0xa460` string, `ctx+0xa860..` / `ctx+0xa880..` ints) and
advances `ip` by an amount that depends ONLY on that byte (plus, for five
families, on the operand value). Handlers never re-size an operand. So the
reader alone decides how many bytes every wire opcode consumes, and `GRAMMAR`
below is a transcription of it.

The switch was re-read from machine code too, not only from the decompile:
`@0x00472c2f lea edi,[eax-1] ; cmp edi,0xa0 ; ja 0x00475504` then a byte index
table at `0x004755d4` and a 40-slot jump table at `0x00475534`. Every GRAMMAR
row carries its jump target (`case_va`) and the decompile lines that advance
`*param_1`; `disasm_tiling_check.py --verify-exe` re-derives the jump table
and fails if a single opcode lands in a different group.

RECORD HEADER (measured, not assumed)
-------------------------------------
The walker (`FUN_00475680:156`, `:194`) and the reader (`FUN_00472bc0:69`)
read a record as `u16 LE tag @+0, u16 LE size @+2`, opcode stream from `+4`
(handlers start the reader at ip = 4: `FUN_0047a0c0:118`, `FUN_004987b0:58`).
`walk_records` below reads `tag = b[0]` and a BIG-endian size at `b[1..2]`,
and the tooling has always called `b[3]` the "flags" byte. The two readings
agree exactly when `b[1] == 0`, `b[3] == 0` and size < 256 -- measured true for
all 1,381,228 records of the 21 distinct canonical FunkCode/StartCode blobs
(max record size 169, `b[1]` and `b[3]` zero in every record). So the
"flags" byte is really the high byte of the engine's LE size; payload-relative
ip 1 == engine ip 4. `walk_records` is exact on this corpus and is kept.

WHAT THE READER DOES NOT COVER -- per-tag raw layouts (`RAW_LAYOUTS`)
----------------------------------------------------------------------
Three tags are read by the walker itself, byte for byte, never through the
reader. Pass `tag=` to `disasm_payload` / `tile_payload` to decode them:

  0x28  80-byte DlgNPC entry copied verbatim into ctx+0x755c (stride 0x50)
        (`FUN_00475680:751-758`, `0x14` dwords from record+4)
  0x29  u16 selector, `switch(*(u16*)(record+4))` cases 1..5
        (`FUN_00475680:978-1062`); it arms `DAT_00ab7898`, which makes the
        walker skip every record up to the next tag 0x2a (`:186-193`)
  0x2f  u32 at record+4 and an ASCIIZ at record+8 (`FUN_00475680:1089-1330`)
        -- 0 records in the corpus, decoded for completeness

Without `tag=` those three are decoded as opcode streams, which is wrong for
them (a 0x29 selector 2 would print as a truncated op 0x02).

RUNTIME-DEPENDENT WIDTHS
------------------------
* `0x04 0x0c 0x0d 0x3c`: i32; `-2` -> ASCIIZ follows, otherwise 2 more u32.
* `0x0b 0x38 0x5f`: u32; `0xFFED2979` (-0x12d687) -> ASCIIZ variable name.
* `0x48 0x49 0x4a 0x5d 0x5e 0x6d 0x6e`: i32 + ASCIIZ; when the i32 is
  negative and the next byte is not 0 a second ASCIIZ (a variable name) follows.
* `0x37 0x4b`: u8 selector; selector 1 -> ASCIIZ follows.
* `0x1f` / `0x9f`: a symbol-reference token `9F EF BE ED FE <name> 00`
  (compiler: `FUN_004547b0:752-756`; parser: `FUN_00453970`). The reader skips
  the token only when the named variable exists in ctx+0x7550; otherwise it
  takes the numeric path (0x1f: u32 after the opcode; 0x9f: the u32 that
  STARTS at the opcode byte, advance 4) and lands inside the token, whose
  next byte (0xFE, no case) ends the record. This decoder assumes the
  variable resolves -- that is the only reading under which the record tiles.

END
---
The reader's `default:` returns 0 WITHOUT advancing (`FUN_00472bc0:857-860`),
and every handler loop stops on 0. Bytes with no case -- 0x00 (below the
switch base), 0x17 0x18 0x21 0x22 0x76 0x94 and 0xa2..0xff -- are therefore
END. Bytes after an END are never read by the engine; this decoder reports
them as a LEFTOVER line instead of pretending to decode them.

FROZEN LEGACY VOCABULARY
------------------------
`funkcode_ops` (the byte-exact .fkasm / Lua (de)serializer) keeps the OLD
labels (`DLG_OP_a`, `U32_qid_a`, ...) on purpose: `sdk/lua_bake_opcodes.inc`
is a row-for-row C++ copy of them. Those labels round-trip bytes exactly but
do not describe the engine's grammar -- this module does. Each GRAMMAR row
names the legacy label (`legacy`) so old shards and dumps can be mapped.
"""
import sys, os, struct, argparse, collections

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from funkcode_tags import label_for as tag_label

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass


# ------------------------------------------------------------------ forms --
F_END   = "end"          # no case: reader returns 0, ip not advanced
F_FLAG  = "flag"         # the opcode byte is the whole operand
F_U8    = "u8"
F_U16   = "u16"
F_U16X2 = "u16x2"
F_U16X4 = "u16x4"
F_I16   = "i16in4"       # 4 wire bytes, engine keeps (int)(short) of the first 2
F_U32   = "u32"
F_U32X2 = "u32x2"
F_U32X3 = "u32x3"
F_U32X4 = "u32x4"
F_STR   = "asciiz"
F_STR2  = "asciiz2"
F_U32S  = "u32+asciiz"
F_POS   = "pos"          # i32; -2 -> ASCIIZ; else + 2 u32
F_INTV  = "u32|var"      # u32; 0xFFED2979 -> ASCIIZ var name
F_INTN  = "i32+asciiz"   # i32 + ASCIIZ (+ ASCIIZ var name when i32 < 0)
F_SEL   = "sel"          # u8 selector; 1 -> ASCIIZ
F_SYM1F = "symref|u32"   # 0x1f
F_SYM9F = "symref|raw3"  # 0x9f

FIXED_WIDTH = {F_FLAG: 0, F_U8: 1, F_U16: 2, F_U16X2: 4, F_U16X4: 8, F_I16: 4,
               F_U32: 4, F_U32X2: 8, F_U32X3: 12, F_U32X4: 16}

POS_NAMED = 0xFFFFFFFE           # -2        FUN_00472bc0:396 / :1591
VAR_MAGIC = 0xFFED2979           # -0x12d687 FUN_00472bc0:677
SYMREF_MARK = 0xFEEDBEEF         # FUN_00453970:20 (bytes EF BE ED FE)
READER_DEFAULT_VA = 0x00475504

GrammarRow = collections.namedtuple(
    "GrammarRow", "op form label case_va lines reads legacy")

_BARE = (0x06, 0x07, 0x08, 0x0e, 0x0f, 0x10, 0x12, 0x13, 0x14, 0x1a, 0x1b, 0x23,
         0x24, 0x25, 0x26, 0x27, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
         0x39, 0x3f, 0x42, 0x43, 0x44, 0x45, 0x46, 0x4c, 0x4e, 0x4f, 0x50, 0x51,
         0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x61, 0x62, 0x64, 0x65, 0x66, 0x70, 0x72,
         0x74, 0x78, 0x7b, 0x7c, 0x80, 0x85, 0x8a, 0x8d, 0x8e, 0x91, 0x96, 0x97,
         0x98, 0x99, 0x9a, 0x9e, 0xa0, 0xa1)
_END = (0x00, 0x17, 0x18, 0x21, 0x22, 0x76, 0x94) + tuple(range(0xa2, 0x100))

# (ops, form, label, jump target, FUN_00472bc0.c lines, what the reader stores)
_GROUPS = [
    ((0x01, 0x29, 0x63, 0x68, 0x69, 0x6a, 0x9d), F_STR, "NAME", 0x00472c4d, "76-374 (advance :120)",
     "ASCIIZ -> ctx+0xa460; resolves res:<n>, DLGNPC, QUESTFELLOW, QUESTNPC, CPOS:hero, CPOS:RES: to an id/position"),
    ((0x02,), F_U32, "U32", 0x00473a27, "375-380, +5 at :2354", "u32 -> ctx+0xa880, FUN_0043ced0(u32)"),
    ((0x03, 0x0a, 0x6b, 0x6c, 0x9b, 0x9c), F_U16, "U16", 0x00474da2, "381-391", "u16 -> ctx+0xa880"),
    ((0x04, 0x0c, 0x0d), F_POS, "POS", 0x00474181, "392-565 (-2: :430; else LAB_00474703 :1637-1647)",
     "i32; -2 -> ASCIIZ position name (CPOS:hero, CPOS:RES:, CPOS:, DefPos key); else X,Y,Z -> ctx+0xa860/64/68"),
    ((0x05, 0x09, 0x41, 0x60, 0x83), F_STR, "STR", 0x00474823, "566-604 (advance :603)", "ASCIIZ -> ctx+0xa460, no resolution"),
    (_BARE, F_FLAG, "FLAG", 0x00474c47, "605-672", "no operand; the handler switches on the opcode itself"),
    ((0x0b, 0x38, 0x5f), F_INTV, "INT", 0x0047487c, "673-730 (+5 at :738)",
     "u32 -> ctx+0xa860; 0xFFED2979 -> ASCIIZ variable name, value looked up in ctx+0x7550"),
    ((0x11, 0x36, 0x75), F_U32, "U32", 0x00474a46, "731-739", "u32 -> ctx+0xa880"),
    ((0x15,), F_U16X4, "U16x4", 0x0047478c, "740-750", "4 x u16 -> ctx+0xa880/84/88/8c"),
    ((0x16,), F_STR, "TEXT", 0x00474dcd, "751-856 (advance :855)",
     "ASCIIZ appended to ctx+0x79f4 (backslash pair -> newline), pushes 0x12341234"),
    ((0x19,), F_U32X3, "U32x3", 0x00473f69, "861-870 (+13 at :1259)", "3 x u32 -> ctx+0xa880/84/88, FUN_0043ced0(a,b,c)"),
    ((0x1c, 0x53, 0x54, 0x55, 0x56, 0x57, 0x86), F_U32, "U32", 0x00474a1e, "871-882", "u32 -> ctx+0xa860"),
    ((0x1d,), F_U32, "RESID", 0x00474f90, "883-1026 (+5 at :1025)", "u32 resource number: itoa + script-resource lookup"),
    ((0x1e,), F_STR, "RESNAME", 0x004751c4, "1027-1198 (advance :1188)", "ASCIIZ script-resource name (FUN_006725e0 / FUN_00672cf0)"),
    ((0x1f,), F_SYM1F, "INT", 0x004749ac, "1199-1213 (machine code 0x4749ac-0x474a19)",
     "symref token (FUN_00453970) or u32 -> ctx+0xa860"),
    ((0x20, 0x2a, 0x4d), F_U32X3, "XYZ", 0x00474065, "1214-1224 (+13 at :1646)", "3 x u32 -> ctx+0xa860/64/68 (the position slots)"),
    ((0x28,), F_U8, "U8", 0x004747f3, "1225-1229", "u8 -> ctx+0xa880"),
    ((0x33, 0x87, 0x88, 0x89), F_U32X2, "U32x2", 0x00473fbc, "1230-1239", "2 x u32 -> ctx+0xa880/84"),
    ((0x34, 0x79), F_U32X2, "U32x2", 0x0047402c, "1240-1247", "2 x u32 -> ctx+0xa860/64"),
    ((0x35,), F_U32X3, "U32x3", 0x00473ff5, "1248-1260", "3 x u32 -> ctx+0xa880/84/88"),
    ((0x37, 0x4b), F_SEL, "SEL", 0x00474c4f, "1261-1357 (machine code 0x474c4f-0x474cae)",
     "u8 selector; selector 1 -> ASCIIZ, res:<n> resolved -> ctx+0xa880"),
    ((0x71,), F_FLAG, "FLAG", 0x00474c52, "1264-1269",
     "no operand: shares the 0x37 body one instruction later; its selector test reads the opcode byte itself (0x71 != 1)"),
    ((0x3a,), F_STR2, "RES2", 0x00473607, "1358-1582 (advance :1392, :1505)",
     "2 x ASCIIZ, each res:<n> resolved -> ctx+0xa880 / ctx+0xa884"),
    ((0x3b, 0x73), F_U32, "U32", 0x00473f54, "1583-1588 (+5 at :2354)", "u32 -> ctx+0xa880"),
    ((0x3c,), F_POS, "POS", 0x0047463f, "1589-1647", "i32; -2 -> ASCIIZ ('hero' -> hero position); else X,Y,Z"),
    ((0x3d,), F_U32X4, "U32x4", 0x00474731, "1648-1659", "4 x u32 -> ctx+0xa860/64/68/6c"),
    ((0x3e, 0x77, 0x81, 0x82), F_STR, "REF", 0x00473df1, "1660-1708 (advance :1695)",
     "ASCIIZ; res:<n> or 'hero' resolved to an id -> ctx+0xa880"),
    ((0x40, 0x95), F_STR, "RES", 0x004738e8, "1709-1819 (advance :1742)", "ASCIIZ; res:<n> resolved -> ctx+0xa880"),
    ((0x47, 0x52, 0x7d, 0x8f), F_STR, "STR", 0x00473a99, "1820-1857 (advance :1856)", "ASCIIZ -> ctx+0xa460, no resolution"),
    ((0x48, 0x49, 0x4a, 0x5d, 0x5e, 0x6d, 0x6e), F_INTN, "INT+STR", 0x00473c48, "1858-1950",
     "i32 -> ctx+0xa860, ASCIIZ -> ctx+0xa460; i32 < 0 and next byte != 0 -> ASCIIZ var name whose value replaces ctx+0xa860"),
    ((0x67,), F_STR, "SPELL", 0x00473257, "1951-2103 (advance :1986)", "ASCIIZ ECS_* spell name -> spell id in ctx+0xa880"),
    ((0x6f,), F_U32S, "U32+RES", 0x00473af2, "2104-2138", "u32 -> ctx+0xa884, ASCIIZ res:<n> resolved -> ctx+0xa880"),
    ((0x7a,), F_U32S, "U32+REF", 0x00474a6e, "2219-2253", "u32 -> ctx+0xa860, ASCIIZ res:<n>/hero resolved -> ctx+0xa880"),
    ((0x7e, 0x7f, 0x8c, 0x90), F_I16, "I16", 0x00473a86, "2345-2355",
     "4 wire bytes; the engine keeps (int)(short) of the first two -> ctx+0xa860"),
    ((0x84,), F_U16X2, "U16x2", 0x004739d2, "2356-2362", "2 x u16 -> ctx+0xa860/64"),
    ((0x8b,), F_U8, "U8", 0x00473a11, "2363-2366", "u8 -> ctx+0xa860"),
    ((0x92,), F_STR2, "STR2", 0x004740af, "2367-2445", "2 x ASCIIZ -> ctx+0xa460 / ctx+0xa560"),
    ((0x93,), F_U16, "U16", 0x00473a58, "2446-2451", "u16 -> ctx+0xa880"),
    ((0x9f,), F_SYM9F, "SYMREF", 0x004754a7, "2452-2464 (machine code 0x4754a7-0x4754e7)",
     "symref token (FUN_00453970), else the u32 that starts AT the opcode byte (advance 4)"),
    (_END, F_END, "END", READER_DEFAULT_VA, "857-860 (default: return 0, ip not advanced)",
     "nothing -- every handler loop stops"),
]

# legacy labels (the frozen funkcode_ops / lua_bake_opcodes.inc vocabulary)
_LEGACY = {
    0x00: "END", 0x01: "DLG_OP_a", 0x02: "U32_TRG", 0x03: "FMT3_a", 0x04: "HERO_OP_a", 0x05: "MATH_a",
    0x09: "MATH_b", 0x0a: "FMT3_b", 0x0b: "U32_qid_a", 0x0c: "HERO_OP_b", 0x0d: "HERO_OP_c",
    0x11: "U32_a", 0x15: "U32PAIR_a", 0x16: "BlockMarker", 0x19: "U32_TRIPLE", 0x1c: "U32_d",
    0x1d: "RES_LOOKUP_C", 0x1e: "STR_REF", 0x1f: "STACK_1F", 0x20: "XYZ_a", 0x28: "C1_a",
    0x29: "DLG_OP_b", 0x2a: "XYZ_b", 0x33: "U32PAIR_b", 0x34: "U32PAIR_f", 0x35: "CMD_35",
    0x36: "U32_b", 0x37: "HALT", 0x38: "U32_qid_b", 0x3a: "ResLookup_3a", 0x3b: "U32_3b",
    0x3c: "HERO_REF", 0x3d: "U32_QUAD", 0x3e: "VAR_LOOKUP_a", 0x40: "RES_LOOKUP_a", 0x41: "MATH_c",
    0x47: "STR_LOOKUP_a", 0x48: "EMIT_a", 0x49: "EMIT_b", 0x4a: "EMIT_c", 0x4b: "BREAK", 0x4d: "XYZ_c",
    0x52: "STR_LOOKUP_b", 0x53: "U32_e", 0x54: "U32_f", 0x55: "U32_g", 0x56: "U32_h", 0x57: "U32_i",
    0x5d: "EMIT_d", 0x5e: "EMIT_e", 0x5f: "U32_qid_c", 0x60: "MATH_d", 0x63: "DLG_OP_c",
    0x67: "VAR_LOOKUP_67", 0x68: "DLG_OP_d", 0x69: "DLG_OP_e", 0x6a: "DLG_OP_f", 0x6b: "FMT3_c",
    0x6c: "FMT3_d", 0x6d: "EMIT_f", 0x6e: "EMIT_g", 0x6f: "ResLookup_6f", 0x71: "InternalGoto_71",
    0x73: "U32_73", 0x75: "U32_c", 0x77: "VAR_LOOKUP_b", 0x79: "U32PAIR_g", 0x7a: "ResLookup_7a",
    0x7d: "STR_LOOKUP_c", 0x7e: "U32_k", 0x7f: "U32_l", 0x81: "VAR_LOOKUP_c", 0x82: "VAR_LOOKUP_d",
    0x83: "MATH_e", 0x84: "U32_o", 0x86: "U32_j", 0x87: "U32PAIR_c", 0x88: "U32PAIR_d",
    0x89: "U32PAIR_e", 0x8b: "U8_8b", 0x8c: "U32_m", 0x8f: "STR_LOOKUP_d", 0x90: "U32_n",
    0x92: "CMD_92", 0x93: "FMT3_g", 0x95: "ResLookup_95", 0x9b: "FMT3_e", 0x9c: "FMT3_f",
    0x9d: "DLG_OP_g", 0x9f: "C3_b",
}

GRAMMAR = {}
for _ops, _form, _label, _va, _lines, _reads in _GROUPS:
    for _op in _ops:
        if _op in GRAMMAR:
            raise AssertionError("opcode 0x%02x in two reader groups" % _op)
        _leg = _LEGACY.get(_op) or ("STACK_%02X" % _op if _form == F_FLAG and _op != 0x71 else None)
        GRAMMAR[_op] = GrammarRow(_op, _form, _label, _va, _lines, _reads, _leg)
if len(GRAMMAR) != 256:
    raise AssertionError("GRAMMAR must cover all 256 byte values, covers %d" % len(GRAMMAR))

# Compatibility exports (same 4-tuple shape as the pre-2026-09-11 table:
# (group, kind, width, label)); `group` is now the reader's jump target.
OPCODE_TABLE = {op: (r.case_va, r.form, FIXED_WIDTH.get(r.form), r.label)
                for op, r in GRAMMAR.items()}
STACK_OPS = frozenset(op for op, r in GRAMMAR.items() if r.form == F_FLAG)
END_OPS = frozenset(op for op, r in GRAMMAR.items() if r.form == F_END)


# ------------------------------------------------------------ record walk --
def walk_records(buf):
    """Top-level record walker: [tag:u8][size:u16 BE, incl. header][payload].
    Yields (offset, tag, size, payload); stops cleanly at a malformed record.
    See the module docstring for why this equals the engine's LE reading on
    the shipped corpus."""
    off = 0
    while off + 3 <= len(buf):
        tag = buf[off]
        size = (buf[off+1] << 8) | buf[off+2]
        if size < 3 or off + size > len(buf):
            return
        yield off, tag, size, buf[off+3: off+size]
        off += size


# --------------------------------------------------------------- decoding --
Op = collections.namedtuple("Op", "ip op form label value next_ip status need")
# status: ok | end | truncated | unterminated ; need = bytes the failing
# operand wanted (truncated) or had left (unterminated)

_u16 = struct.Struct("<H").unpack_from
_i16 = struct.Struct("<h").unpack_from
_u32 = struct.Struct("<I").unpack_from
_i32 = struct.Struct("<i").unpack_from


def _asciiz(p, at):
    """(text, next) or (partial_text, None) if no NUL before the end."""
    e = p.find(0, at)
    if e < 0:
        return p[at:].decode("latin1", "replace"), None
    return p[at:e].decode("latin1", "replace"), e + 1


def _symref_at(p, at):
    """FUN_00453970: token = 0x9f, u32 0xFEEDBEEF, ASCIIZ name."""
    return at + 5 <= len(p) and p[at] == 0x9f and _u32(p, at + 1)[0] == SYMREF_MARK


def decode_op(p, ip):
    """Decode the opcode at p[ip] exactly as FUN_00472bc0 would size it."""
    n = len(p)
    op = p[ip]
    g = GRAMMAR[op]
    form, label = g.form, g.label

    def bad_fixed(w):
        return Op(ip, op, form, label, bytes(p[ip+1:n]), n, "truncated", w)

    def bad_str(at, partial):
        return Op(ip, op, form, label, partial, n, "unterminated", n - at)

    if form == F_END:
        return Op(ip, op, form, label, None, ip + 1, "end", 0)
    if form in FIXED_WIDTH:
        w = FIXED_WIDTH[form]
        if ip + 1 + w > n:
            return bad_fixed(w)
        a = ip + 1
        if form == F_FLAG:   v = None
        elif form == F_U8:   v = p[a]
        elif form == F_U16:  v = _u16(p, a)[0]
        elif form == F_U16X2: v = struct.unpack_from("<2H", p, a)
        elif form == F_U16X4: v = struct.unpack_from("<4H", p, a)
        elif form == F_I16:  v = (_i16(p, a)[0], _u16(p, a + 2)[0])
        elif form == F_U32:  v = _u32(p, a)[0]
        elif form == F_U32X2: v = struct.unpack_from("<2I", p, a)
        elif form == F_U32X3: v = struct.unpack_from("<3I", p, a)
        else:                v = struct.unpack_from("<4I", p, a)
        return Op(ip, op, form, label, v, a + w, "ok", 0)
    if form == F_STR:
        s, nxt = _asciiz(p, ip + 1)
        return Op(ip, op, form, label, s, nxt, "ok", 0) if nxt else bad_str(ip + 1, s)
    if form == F_STR2:
        s1, mid = _asciiz(p, ip + 1)
        if mid is None:
            return bad_str(ip + 1, s1)
        s2, nxt = _asciiz(p, mid)
        return Op(ip, op, form, label, (s1, s2), nxt, "ok", 0) if nxt else bad_str(mid, (s1, s2))
    if form == F_U32S:
        if ip + 5 > n:
            return bad_fixed(4)
        u = _u32(p, ip + 1)[0]
        s, nxt = _asciiz(p, ip + 5)
        return Op(ip, op, form, label, (u, s), nxt, "ok", 0) if nxt else bad_str(ip + 5, (u, s))
    if form == F_POS:
        if ip + 5 > n:
            return bad_fixed(4)
        v = _u32(p, ip + 1)[0]
        if v == POS_NAMED:
            s, nxt = _asciiz(p, ip + 5)
            return Op(ip, op, form, label, ("name", s), nxt, "ok", 0) if nxt else bad_str(ip + 5, s)
        if ip + 13 > n:
            return bad_fixed(12)
        return Op(ip, op, form, label, ("xyz", struct.unpack_from("<3i", p, ip + 1)), ip + 13, "ok", 0)
    if form == F_INTV:
        if ip + 5 > n:
            return bad_fixed(4)
        v = _u32(p, ip + 1)[0]
        if v == VAR_MAGIC:
            s, nxt = _asciiz(p, ip + 5)
            return Op(ip, op, form, label, ("var", s), nxt, "ok", 0) if nxt else bad_str(ip + 5, s)
        return Op(ip, op, form, label, ("u32", v), ip + 5, "ok", 0)
    if form == F_INTN:
        if ip + 5 > n:
            return bad_fixed(4)
        i = _i32(p, ip + 1)[0]
        s, nxt = _asciiz(p, ip + 5)
        if nxt is None:
            return bad_str(ip + 5, (i, s))
        if i < 0 and nxt < n and p[nxt] != 0:        # FUN_00472bc0:1904
            s2, nxt2 = _asciiz(p, nxt)
            if nxt2 is None:
                return bad_str(nxt, (i, s, s2))
            return Op(ip, op, form, label, (i, s, s2), nxt2, "ok", 0)
        return Op(ip, op, form, label, (i, s, None), nxt, "ok", 0)
    if form == F_SEL:
        if ip + 2 > n:
            return bad_fixed(1)
        sel = p[ip + 1]
        if sel != 1:
            return Op(ip, op, form, label, (sel, None), ip + 2, "ok", 0)
        s, nxt = _asciiz(p, ip + 2)
        return Op(ip, op, form, label, (sel, s), nxt, "ok", 0) if nxt else bad_str(ip + 2, s)
    if form == F_SYM1F:
        if _symref_at(p, ip + 1):
            s, nxt = _asciiz(p, ip + 6)
            return Op(ip, op, form, label, ("symref", s), nxt, "ok", 0) if nxt else bad_str(ip + 6, s)
        if ip + 5 > n:
            return bad_fixed(4)
        return Op(ip, op, form, label, ("u32", _u32(p, ip + 1)[0]), ip + 5, "ok", 0)
    if form == F_SYM9F:
        if _symref_at(p, ip):
            s, nxt = _asciiz(p, ip + 5)
            return Op(ip, op, form, label, ("symref", s), nxt, "ok", 0) if nxt else bad_str(ip + 5, s)
        if ip + 4 > n:
            return bad_fixed(3)
        return Op(ip, op, form, label, ("raw", bytes(p[ip+1:ip+4])), ip + 4, "ok", 0)
    raise AssertionError("unhandled form %r" % form)


# ---------------------------------------------------- per-tag raw layouts --
def _raw_field(p, at, kind, label, width):
    n = len(p)
    if at + width > n:
        return Op(at, None, kind, label, bytes(p[at:n]), n, "truncated", width)
    if kind == "i32":  v = _i32(p, at)[0]
    elif kind == "u32": v = _u32(p, at)[0]
    elif kind == "u16": v = _u16(p, at)[0]
    else:               v = bytes(p[at:at+width])
    return Op(at, None, kind, label, v, at + width, "ok", 0)


def _raw_0x28(p):
    """DlgNPC entry: FUN_00475680:751-758 copies 0x14 dwords from record+4
    (= payload[1]) into the table at ctx+0x755c (stride 0x50). +0x00 id and
    +0x04 name are the keys tag 0x1f's handler FUN_0048f9e0 matches on;
    +0x4c is the dialog-content slot it writes. +0x44 / +0x48: meaning
    UNKNOWN here (OPEN_QUESTIONS_wave1 Q43)."""
    ops = [_raw_field(p, 1, "i32", "DLGNPC.id", 4)]
    if ops[-1].status == "ok":
        if len(p) < 69:
            ops.append(Op(5, None, "char64", "DLGNPC.name", bytes(p[5:]), len(p), "truncated", 64))
        else:
            raw = bytes(p[5:69])
            ops.append(Op(5, None, "char64", "DLGNPC.name", raw, 69, "ok", 0))
            for at, lab in ((69, "DLGNPC.+44"), (73, "DLGNPC.+48"), (77, "DLGNPC.+4c")):
                ops.append(_raw_field(p, at, "u32", lab, 4))
                if ops[-1].status != "ok":
                    break
    return ops


def _raw_0x29(p):
    """u16 selector: FUN_00475680:986 switch(*(u16*)(record+4)) -> cases 1..5."""
    return [_raw_field(p, 1, "u16", "SELECT", 2)]


def _raw_0x2f(p):
    """u32 at record+4 (pushed at FUN_00475680:1262/1294/1319) and an ASCIIZ at
    record+8 (copied at :1099-1121, <= 128 bytes). No record in the corpus."""
    ops = [_raw_field(p, 1, "u32", "U32", 4)]
    if ops[-1].status == "ok":
        s, nxt = _asciiz(p, 5)
        ops.append(Op(5, None, F_STR, "NAME", s, nxt or len(p),
                      "ok" if nxt else "unterminated", 0 if nxt else len(p) - 5))
    return ops


RAW_LAYOUTS = {
    0x28: ("DlgNPC entry, 80 raw bytes (walker FUN_00475680:751-758)", _raw_0x28),
    0x29: ("u16 selector (walker FUN_00475680:978-1062)", _raw_0x29),
    0x2f: ("u32 + ASCIIZ (walker FUN_00475680:1089-1330)", _raw_0x2f),
}

Tile = collections.namedtuple("Tile", "ops leftover raw")
# ops: [Op]; leftover: (start, end) of bytes after END, or None; raw: bool


def tile_payload(payload, tag=None):
    """Decode a whole payload (payload[0] = header byte 3, see docstring).
    The walk stops at END (engine semantics) or at the first failing operand."""
    if tag in RAW_LAYOUTS:
        ops = RAW_LAYOUTS[tag][1](payload)
        last = ops[-1].next_ip if ops else 1
        left = (last, len(payload)) if (ops and ops[-1].status == "ok" and last < len(payload)) else None
        return Tile(ops, left, True)
    ops = []
    ip, n = 1, len(payload)
    left = None
    while ip < n:
        o = decode_op(payload, ip)
        ops.append(o)
        if o.status == "end":
            if o.next_ip < n:
                left = (o.next_ip, n)
            break
        if o.status != "ok":
            break
        ip = o.next_ip
    return Tile(ops, left, False)


# -------------------------------------------------------------- rendering --
def _q(s):
    return repr(s)


def render_value(o):
    """Operand text for one Op (the part after the label)."""
    f, v = o.form, o.value
    if o.status == "truncated":
        return f"(truncated, +{o.need}) have {len(v)}B {v.hex()}"
    if o.status == "unterminated":
        return f"(unterminated ASCIIZ, +{o.need}) {v!r}"
    if f in (F_END, F_FLAG):
        return ""
    if f == F_U8:
        return f"u8={v}"
    if f == F_U16:
        return f"u16={v}"
    if f in (F_U16X2, F_U16X4):
        return "u16=" + ", ".join(str(x) for x in v)
    if f == F_I16:
        lo, hi = v
        ext = 0xFFFF if lo < 0 else 0
        return f"i16={lo}" + ("" if hi == ext else f" (upper u16 0x{hi:04x} ignored by the reader)")
    if f == F_U32:
        return f"u32={v}" + (f" (i32 {v - (1 << 32)})" if v >= 0x80000000 else "")
    if f == F_U32X2:
        return "u32=" + ", ".join(str(x) for x in v)
    if f == F_U32X3:
        if o.label == "XYZ":
            return "x=%d y=%d z=%d" % v
        return "u32=" + ", ".join(str(x) for x in v)
    if f == F_U32X4:
        return "u32=" + ", ".join(str(x) for x in v)
    if f == F_STR:
        return _q(v)
    if f == F_STR2:
        return f"{v[0]!r}, {v[1]!r}"
    if f == F_U32S:
        return f"{v[0]} | {v[1]!r}"
    if f == F_POS:
        return f"pos {v[1]!r}" if v[0] == "name" else "x=%d y=%d z=%d" % v[1]
    if f == F_INTV:
        return f"var {v[1]!r}" if v[0] == "var" else (
            f"u32={v[1]}" + (f" (i32 {v[1] - (1 << 32)})" if v[1] >= 0x80000000 else ""))
    if f == F_INTN:
        return f"{v[0]} | {v[1]!r}" + (f", var {v[2]!r}" if v[2] is not None else "")
    if f == F_SEL:
        return f"sel={v[0]}" + (f" {v[1]!r}" if v[1] is not None else "")
    if f in (F_SYM1F, F_SYM9F):
        if v[0] == "symref":
            return f"symref {v[1]!r}"
        if v[0] == "u32":
            return f"u32={v[1]}" + (f" (i32 {v[1] - (1 << 32)})" if v[1] >= 0x80000000 else "")
        return f"raw {v[1].hex()} (the reader takes the u32 starting at the opcode)"
    # raw-layout fields
    if f == "char64":
        name = v.split(b"\0", 1)[0].decode("latin1", "replace")
        tail = v[len(name) + 1:] if b"\0" in v else b""
        junk = "" if not tail.strip(b"\0") else f" (+ non-zero padding {tail.rstrip(b'\0').hex()})"
        return repr(name) + junk
    if f in ("i32", "u32", "u16"):
        return f"{f}={v}"
    return repr(v)


def disasm_one(data, ip):
    """Compatibility wrapper: decode one opcode at data[ip] and return
    (label, operand_text, next_ip). END returns ip+1 so callers' loops keep
    moving; the engine itself stops there."""
    if ip >= len(data):
        return None, "(EOF)", ip + 1
    o = decode_op(data, ip)
    return o.label, render_value(o), max(o.next_ip, ip + 1)


def disasm_payload(payload, indent=4, limit=None, tag=None):
    """Disassemble one record payload -> (lines, histogram, end_ip).

    payload[0] is shown as `flags=` (it is the engine's size high byte and 0
    in every shipped record). Operand lines are
        +IIII  OP  LABEL              operand
    where OP is `--` for fields of a per-tag raw layout (pass `tag=`).
    Failure markers: `(truncated, +N)`, `(unterminated ASCIIZ, +N)`, and a
    LEFTOVER line for bytes after END. end_ip == len(payload) when every
    byte was rendered."""
    histogram = collections.Counter()
    sp = " " * indent
    out = []
    if payload:
        out.append(f"{sp}flags=0x{payload[0]:02x}")
    t = tile_payload(payload, tag)
    end_ip = 1 if payload else 0
    shown = 0
    for o in t.ops:
        if limit is not None and shown >= limit:
            return out, histogram, end_ip
        histogram[o.label] += 1
        opcol = "--" if o.op is None else f"{o.op:02x}"
        out.append(f"{sp}+{o.ip:04x}  {opcol}  {o.label:<18} {render_value(o)}".rstrip())
        shown += 1
        end_ip = min(o.next_ip, len(payload))
    if t.leftover and (limit is None or shown < limit):
        a, b = t.leftover
        chunk = bytes(payload[a:b])
        why = "after END" if not t.raw else "beyond the raw layout"
        out.append(f"{sp}+{a:04x}  {payload[a]:02x}  {'LEFTOVER':<18} "
                   f"(leftover {b - a} bytes {why}: {chunk[:24].hex()}{'...' if len(chunk) > 24 else ''})")
        histogram["LEFTOVER"] += 1
        end_ip = b
    return out, histogram, end_ip


def disasm_file(path, limit_records=20, ops_per_record=200, dump_payload=True):
    data = open(path, "rb").read()
    print(f"# {path}")
    print(f"# {len(data):,} bytes")
    global_hist = collections.Counter()
    record_tag_hist = collections.Counter()

    for i, (off, tag, size, payload) in enumerate(walk_records(data)):
        record_tag_hist[tag] += 1
        if i >= limit_records:
            continue
        tag_ch = chr(tag) if 32 <= tag <= 126 else "?"
        label = tag_label(tag)
        print(f"\n{off:08x}  RECORD tag=0x{tag:02x} '{tag_ch}'  [{label}]  size={size}  payload={size-3}B")
        if not dump_payload:
            continue
        ops, hist, end_ip = disasm_payload(payload, indent=4, limit=ops_per_record, tag=tag)
        for ln in ops:
            print(ln)
        if end_ip < len(payload):
            print(f"    ... ({len(payload) - end_ip} more bytes of payload not shown)")
        global_hist.update(hist)

    print(f"\n# === record-tag histogram (full file) ===")
    for t, c in record_tag_hist.most_common(30):
        tch = chr(t) if 32 <= t <= 126 else "?"
        print(f"#   tag=0x{t:02x} '{tch}'  [{tag_label(t):<22}]  : {c:,}")
    print(f"#")
    print(f"# === opcode-label histogram (limited window) ===")
    for lbl, c in global_hist.most_common(25):
        print(f"#   {c:>6}  {lbl}")


def print_grammar():
    """The operand grammar as a table (one row per reader case group)."""
    print("| ops | form | label | reader jump target | FUN_00472bc0.c | reader stores | legacy label(s) |")
    print("|---|---|---|---|---|---|---|")
    for ops, form, label, va, lines, reads in _GROUPS:
        shown = " ".join(f"{o:02x}" for o in ops) if len(ops) < 20 else \
            f"{len(ops)} ops: " + " ".join(f"{o:02x}" for o in ops[:8]) + " ..."
        leg = ", ".join(sorted({GRAMMAR[o].legacy for o in ops if GRAMMAR[o].legacy}))[:60]
        print(f"| {shown} | {form} | {label} | 0x{va:08x} | {lines} | {reads} | {leg} |")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?", help="path to a FunkCode-format .bin")
    ap.add_argument("--records", type=int, default=12, help="how many records to dump in full")
    ap.add_argument("--ops", type=int, default=200, help="max ops per record")
    ap.add_argument("--grammar", action="store_true", help="print the operand grammar table")
    args = ap.parse_args()
    if args.grammar or not args.file:
        print_grammar()
    if args.file:
        disasm_file(args.file, limit_records=args.records, ops_per_record=args.ops)
