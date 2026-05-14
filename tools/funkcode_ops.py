"""Structured opcode (de)serializer for Sacred FunkCode.

Pairs with `funkcode_disasm.OPCODE_TABLE`. The disassembler in funkcode_disasm
returns pretty strings for the human-readable trace; this module gives us
**structured** args (`u32`, `u8`, list-of-strings, raw-tail-bytes) plus a
matching encoder. That's what lets `funkcode_compile.py` accept mnemonic
opcode lines like `OP U32_qid_a 9511` and re-emit the same bytes.

The format of `args` per kind:

    stack / halt / const-w0 :  []
    const w=1   :  [u8]
    const w=2   :  [u16]
    const w=4   :  [u32]
    const w=8   :  [u32, u32]
    const w=12  :  [u32, u32, u32]
    const w=16  :  [u32, u32, u32, u32]
    const w=3   :  ['raw', b'\\xNN\\xNN\\xNN']   (3 raw bytes, no decoded meaning)
    cstr1       :  [str]
    cstr2       :  [str, str]
    cstr1+1     :  [str, ('tail', b'\\xNN')]
    cstr1+5     :  [str, ('tail', b'\\xNN'*5)]
    u32+cstr1   :  [u32, str]
    u32+cstr2   :  [u32, str, str]

`('tail', bytes)` lets us preserve the trailer bytes verbatim for variants
that have a static raw-byte tail after the cstring; those bytes don't have
a known semantic decode.

`disasm_structured(payload, ip)` returns either
    (label, args, next_ip)
on success or
    None
if the opcode is unknown / complex / would overrun. Callers fall back to
raw-HEX emission for the whole record in that case.

`assemble_op(label, args)` returns the bytes for that mnemonic line.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from funkcode_disasm import OPCODE_TABLE, STACK_OPS


# --- inverse table: label -> (op, kind, width) ----------------------------
LABEL_TO_OP = {}
for _op, (_group, _kind, _width, _label) in OPCODE_TABLE.items():
    # Skip non-final entries that got shadowed by a later definition; the
    # dict was built via repeated assignments and we want the LAST winner.
    LABEL_TO_OP[_label] = (_op, _kind, _width)
# Stack op pseudo-labels: STACK_06, STACK_07, ...
for _op in STACK_OPS:
    LABEL_TO_OP[f"STACK_{_op:02X}"] = (_op, "stack", 0)


def _read_cstr(buf, cur):
    end = buf.find(0, cur)
    if end < 0:
        return None
    s = buf[cur:end]
    return s, end + 1


def disasm_structured(payload, ip):
    """Decode one opcode at payload[ip] with structured args. Returns
    (label, args, next_ip) on success, None on failure."""
    if ip >= len(payload):
        return None
    op = payload[ip]
    info = OPCODE_TABLE.get(op)
    if info is None:
        return None
    group, kind, width, label = info

    if kind == "stack" or kind == "halt":
        return (label, [], ip + 1)

    if kind == "const":
        if width is None:
            return None
        if ip + 1 + width > len(payload):
            return None
        chunk = payload[ip + 1 : ip + 1 + width]
        if width == 0:
            return (label, [], ip + 1)
        if width == 1:
            return (label, [chunk[0]], ip + 1 + 1)
        if width == 2:
            return (label, [int.from_bytes(chunk, "little")], ip + 3)
        if width == 4:
            return (label, [int.from_bytes(chunk, "little")], ip + 5)
        if width == 8:
            return (
                label,
                [int.from_bytes(chunk[:4], "little"), int.from_bytes(chunk[4:], "little")],
                ip + 9,
            )
        if width == 12:
            return (
                label,
                [
                    int.from_bytes(chunk[0:4], "little"),
                    int.from_bytes(chunk[4:8], "little"),
                    int.from_bytes(chunk[8:12], "little"),
                ],
                ip + 13,
            )
        if width == 16:
            return (
                label,
                [
                    int.from_bytes(chunk[0:4], "little"),
                    int.from_bytes(chunk[4:8], "little"),
                    int.from_bytes(chunk[8:12], "little"),
                    int.from_bytes(chunk[12:16], "little"),
                ],
                ip + 17,
            )
        # width == 3 (FMT3) — no clean numeric decode; carry raw bytes.
        return (label, [("raw", bytes(chunk))], ip + 1 + width)

    if kind == "cstr1":
        r = _read_cstr(payload, ip + 1)
        if r is None:
            return None
        s, nxt = r
        return (label, [s], nxt)
    if kind == "cstr2":
        r = _read_cstr(payload, ip + 1)
        if r is None:
            return None
        s1, mid = r
        r = _read_cstr(payload, mid)
        if r is None:
            return None
        s2, nxt = r
        return (label, [s1, s2], nxt)
    if kind == "cstr1+1":
        r = _read_cstr(payload, ip + 1)
        if r is None:
            return None
        s, mid = r
        if mid + 1 > len(payload):
            return None
        return (label, [s, ("tail", bytes(payload[mid : mid + 1]))], mid + 1)
    if kind == "cstr1+5":
        r = _read_cstr(payload, ip + 1)
        if r is None:
            return None
        s, mid = r
        if mid + 5 > len(payload):
            return None
        return (label, [s, ("tail", bytes(payload[mid : mid + 5]))], mid + 5)
    if kind == "u32+cstr1":
        if ip + 1 + 4 > len(payload):
            return None
        u = int.from_bytes(payload[ip + 1 : ip + 5], "little")
        r = _read_cstr(payload, ip + 5)
        if r is None:
            return None
        s, nxt = r
        return (label, [u, s], nxt)
    if kind == "u32+cstr2":
        if ip + 1 + 4 > len(payload):
            return None
        u = int.from_bytes(payload[ip + 1 : ip + 5], "little")
        r = _read_cstr(payload, ip + 5)
        if r is None:
            return None
        s1, mid = r
        r = _read_cstr(payload, mid)
        if r is None:
            return None
        s2, nxt = r
        return (label, [u, s1, s2], nxt)
    # complex (none left in the final table, but keep guard)
    return None


def disasm_all(payload):
    """Decode the entire `payload` (after the flags byte) into a list of
    `(label, args)` tuples, or raise `CannotDecode` if anything goes wrong.
    Useful for "can we emit mnemonics for this record?" check."""
    out = []
    ip = 0
    while ip < len(payload):
        r = disasm_structured(payload, ip)
        if r is None:
            raise CannotDecode(f"stuck at payload offset {ip}, op=0x{payload[ip]:02x}")
        label, args, nxt = r
        if nxt <= ip:
            raise CannotDecode(f"zero advance at offset {ip}")
        out.append((label, args))
        ip = nxt
    return out


class CannotDecode(Exception):
    pass


# --- assembler side -------------------------------------------------------
def assemble_op(label, args):
    """Inverse of disasm_structured. Given the label and args structure,
    return the raw bytes of this opcode. Raises ValueError on bad input."""
    info = LABEL_TO_OP.get(label)
    if info is None:
        raise ValueError(f"unknown label {label!r}")
    op, kind, width = info
    out = bytearray([op])

    if kind == "stack" or kind == "halt":
        if args:
            raise ValueError(f"{label}: takes no args, got {args!r}")
        return bytes(out)
    if kind == "const":
        if width == 0:
            if args:
                raise ValueError(f"{label}: takes no args, got {args!r}")
            return bytes(out)
        if width == 1:
            if len(args) != 1:
                raise ValueError(f"{label}: needs 1 u8, got {args!r}")
            out.append(int(args[0]) & 0xFF)
            return bytes(out)
        if width == 2:
            if len(args) != 1:
                raise ValueError(f"{label}: needs 1 u16, got {args!r}")
            out += int(args[0]).to_bytes(2, "little", signed=False)
            return bytes(out)
        if width == 4:
            if len(args) != 1:
                raise ValueError(f"{label}: needs 1 u32, got {args!r}")
            out += (int(args[0]) & 0xFFFFFFFF).to_bytes(4, "little")
            return bytes(out)
        if width == 8:
            if len(args) != 2:
                raise ValueError(f"{label}: needs 2 u32, got {args!r}")
            for v in args:
                out += (int(v) & 0xFFFFFFFF).to_bytes(4, "little")
            return bytes(out)
        if width == 12:
            if len(args) != 3:
                raise ValueError(f"{label}: needs 3 u32, got {args!r}")
            for v in args:
                out += (int(v) & 0xFFFFFFFF).to_bytes(4, "little")
            return bytes(out)
        if width == 16:
            if len(args) != 4:
                raise ValueError(f"{label}: needs 4 u32, got {args!r}")
            for v in args:
                out += (int(v) & 0xFFFFFFFF).to_bytes(4, "little")
            return bytes(out)
        if width == 3:
            if len(args) != 1 or not (isinstance(args[0], tuple) and args[0][0] == "raw"):
                raise ValueError(f"{label}: needs ('raw', bytes(3)), got {args!r}")
            raw = args[0][1]
            if len(raw) != 3:
                raise ValueError(f"{label}: raw must be 3 bytes, got {len(raw)}")
            out += raw
            return bytes(out)
        raise ValueError(f"{label}: unhandled const width {width}")

    if kind in ("cstr1", "cstr2"):
        n = 1 if kind == "cstr1" else 2
        if len(args) != n:
            raise ValueError(f"{label}: needs {n} strings, got {args!r}")
        for s in args:
            if isinstance(s, str):
                out += s.encode("latin1")
            else:
                out += bytes(s)
            out.append(0)
        return bytes(out)
    if kind == "cstr1+1" or kind == "cstr1+5":
        tail_len = 1 if kind == "cstr1+1" else 5
        if len(args) != 2:
            raise ValueError(f"{label}: needs (str, ('tail',bytes({tail_len}))), got {args!r}")
        s = args[0]
        tail = args[1]
        if not (isinstance(tail, tuple) and tail[0] == "tail" and len(tail[1]) == tail_len):
            raise ValueError(f"{label}: bad tail {tail!r}")
        out += (s.encode("latin1") if isinstance(s, str) else bytes(s))
        out.append(0)
        out += tail[1]
        return bytes(out)
    if kind == "u32+cstr1":
        if len(args) != 2:
            raise ValueError(f"{label}: needs (u32, str), got {args!r}")
        out += (int(args[0]) & 0xFFFFFFFF).to_bytes(4, "little")
        s = args[1]
        out += (s.encode("latin1") if isinstance(s, str) else bytes(s))
        out.append(0)
        return bytes(out)
    if kind == "u32+cstr2":
        if len(args) != 3:
            raise ValueError(f"{label}: needs (u32, str, str), got {args!r}")
        out += (int(args[0]) & 0xFFFFFFFF).to_bytes(4, "little")
        for s in args[1:]:
            out += (s.encode("latin1") if isinstance(s, str) else bytes(s))
            out.append(0)
        return bytes(out)

    raise ValueError(f"{label}: unhandled kind {kind}")


# --- text formatting helpers ---------------------------------------------
#
# .fkasm uses a single `OP` line per opcode with whitespace-separated args.
# Strings are double-quoted with C-style escapes. Raw byte tails are emitted
# as space-separated `0xNN`. The parser is forgiving — single/double quotes
# both work, hex literals can also be written as `NN` (bare hex) for tails.

_ESCAPES = {
    ord('"'): r'\"',
    ord("\\"): r"\\",
    ord("\n"): r"\n",
    ord("\r"): r"\r",
    ord("\t"): r"\t",
}


def quote_str(s):
    """C-style quote of a latin1-encodable string. Non-printables become \\xNN."""
    if isinstance(s, bytes):
        bs = s
    else:
        bs = s.encode("latin1", "replace")
    out = ['"']
    for b in bs:
        if b in _ESCAPES:
            out.append(_ESCAPES[b])
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append(f"\\x{b:02x}")
    out.append('"')
    return "".join(out)


def parse_quoted(s, i):
    """Parse a `"..."` quoted string starting at s[i]. Returns (bytes, next_i).
    Raises ValueError on malformed input."""
    if i >= len(s) or s[i] not in ('"', "'"):
        raise ValueError(f"expected quote at {i!r}: {s!r}")
    quote = s[i]
    i += 1
    out = bytearray()
    while i < len(s):
        c = s[i]
        if c == quote:
            return bytes(out), i + 1
        if c == "\\":
            i += 1
            if i >= len(s):
                raise ValueError("trailing backslash")
            esc = s[i]
            if esc == "n":
                out.append(0x0A)
            elif esc == "r":
                out.append(0x0D)
            elif esc == "t":
                out.append(0x09)
            elif esc == "\\":
                out.append(0x5C)
            elif esc in ('"', "'"):
                out.append(ord(esc))
            elif esc == "x":
                if i + 2 >= len(s):
                    raise ValueError("bad \\x")
                out.append(int(s[i + 1 : i + 3], 16))
                i += 2
            else:
                raise ValueError(f"bad escape \\{esc}")
            i += 1
        else:
            out.append(ord(c) & 0xFF)
            i += 1
    raise ValueError("unterminated string")


def format_op_line(label, args):
    """Render a single opcode as `OP <label> <args...>` text for .fkasm."""
    info = LABEL_TO_OP.get(label)
    if info is None:
        raise ValueError(f"unknown label {label!r}")
    _op, kind, _width = info
    parts = ["OP", label]
    if kind == "const" and _width == 3:
        # ('raw', bytes(3))
        raw = args[0][1]
        parts.append(" ".join(f"0x{b:02x}" for b in raw))
    elif kind == "cstr1+1" or kind == "cstr1+5":
        parts.append(quote_str(args[0]))
        tail = args[1][1]
        parts.append(" ".join(f"0x{b:02x}" for b in tail))
    elif kind in ("cstr1", "cstr2"):
        for s in args:
            parts.append(quote_str(s))
    elif kind == "u32+cstr1":
        parts.append(str(args[0]))
        parts.append(quote_str(args[1]))
    elif kind == "u32+cstr2":
        parts.append(str(args[0]))
        parts.append(quote_str(args[1]))
        parts.append(quote_str(args[2]))
    elif kind == "const":
        for v in args:
            parts.append(str(v))
    elif kind in ("stack", "halt"):
        pass
    else:
        raise ValueError(f"unhandled kind {kind} for {label}")
    return " ".join(parts)


def parse_op_line(line):
    """Parse a `OP <label> [arg ...]` text line. Returns (label, args)."""
    # Tokenise: respect quoted strings.
    s = line.strip()
    if not s.startswith("OP"):
        raise ValueError(f"not an OP line: {line!r}")
    i = 2
    while i < len(s) and s[i].isspace():
        i += 1
    j = i
    while j < len(s) and not s[j].isspace():
        j += 1
    label = s[i:j]
    info = LABEL_TO_OP.get(label)
    if info is None:
        raise ValueError(f"unknown label {label!r}")
    _op, kind, width = info

    # Pull whitespace-separated tokens, with quoted strings counting as one
    tokens = []
    i = j
    while i < len(s):
        while i < len(s) and s[i].isspace():
            i += 1
        if i >= len(s):
            break
        if s[i] in ('"', "'"):
            bs, i = parse_quoted(s, i)
            tokens.append(("str", bs))
        else:
            k = i
            while k < len(s) and not s[k].isspace():
                k += 1
            tokens.append(("tok", s[i:k]))
            i = k

    def need(tok, kind_):
        if tok[0] != kind_:
            raise ValueError(f"{label}: expected {kind_}, got {tok!r}")
        return tok[1]

    def hex_byte(tok):
        v = need(tok, "tok")
        if v.startswith("0x") or v.startswith("0X"):
            return int(v, 16)
        return int(v, 16)

    if kind in ("stack", "halt"):
        if tokens:
            raise ValueError(f"{label}: takes no args")
        return label, []
    if kind == "const":
        if width == 0:
            return label, []
        if width == 3:
            if len(tokens) != 3:
                raise ValueError(f"{label}: needs 3 raw bytes")
            raw = bytes(hex_byte(t) for t in tokens)
            return label, [("raw", raw)]
        # numeric
        n = {1: 1, 2: 1, 4: 1, 8: 2, 12: 3, 16: 4}.get(width)
        if n is None:
            raise ValueError(f"{label}: bad width {width}")
        if len(tokens) != n:
            raise ValueError(f"{label}: needs {n} args, got {len(tokens)}")
        out = []
        for t in tokens:
            v = need(t, "tok")
            if v.startswith("0x") or v.startswith("0X"):
                out.append(int(v, 16))
            elif v.startswith("-"):
                out.append(int(v) & 0xFFFFFFFF)
            else:
                out.append(int(v))
        return label, out
    if kind in ("cstr1", "cstr2"):
        n = 1 if kind == "cstr1" else 2
        if len(tokens) != n:
            raise ValueError(f"{label}: needs {n} string(s), got {len(tokens)}")
        strs = [need(t, "str") for t in tokens]
        return label, strs
    if kind == "cstr1+1" or kind == "cstr1+5":
        tail_len = 1 if kind == "cstr1+1" else 5
        if len(tokens) != 1 + tail_len:
            raise ValueError(f"{label}: needs string + {tail_len} bytes")
        s = need(tokens[0], "str")
        tail = bytes(hex_byte(t) for t in tokens[1:])
        return label, [s, ("tail", tail)]
    if kind == "u32+cstr1":
        if len(tokens) != 2:
            raise ValueError(f"{label}: needs u32, string")
        u = int(need(tokens[0], "tok"), 0)
        st = need(tokens[1], "str")
        return label, [u, st]
    if kind == "u32+cstr2":
        if len(tokens) != 3:
            raise ValueError(f"{label}: needs u32, string, string")
        u = int(need(tokens[0], "tok"), 0)
        s1 = need(tokens[1], "str")
        s2 = need(tokens[2], "str")
        return label, [u, s1, s2]
    raise ValueError(f"unhandled kind {kind}")
