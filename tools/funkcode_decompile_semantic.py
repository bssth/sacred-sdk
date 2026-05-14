"""Sacred FunkCode → high-level Lua decompiler.

Where `funkcode_decompile_lua.py` emits a one-to-one mnemonic dump (every
record becomes a `{tag, flags, {"LABEL", args...}, …}` table), THIS tool
recognises common vanilla patterns and emits **semantic calls** using the
builders in `custom/lua/lib/`:

    record  tag=0x43, ops=[DLG_OP_a "dq_belohnung" "\\x0b", END×3]
        → q.var "dq_belohnung"

    record  tag=0x69, ops=[DLG_OP_a "name" "\\x0b", END×3, U32_qid_a N]
        → q.assign("name", N)

    record  tag=0x44, ops=[DLG_OP_a "NN" "\\x0b", END×3]
        → q.set_hero_qbit(NN)

    record  tag=0x1a, ops=[STR_REF "name"]
        → d.trigger "name"

    record  tag=0x3c, ops=[DLG_OP_a "res:X" "tail"]
        → d.line("res:X", "tail")

    record  tag=0x42, ops=[EMIT_a N "target"]
        → d.emit(N, "target")

Records that don't match any pattern fall through to `raw.rec(...)` form so
the output ALWAYS round-trips byte-identical. As we recognise more patterns
the fallback shrinks.

The goal is to make vanilla Sacred quest scripts editable as readable Lua.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from funkcode_tags import label_for as tag_label
import funkcode_decompile as dec
import funkcode_ops as fops


# -------------------------------------------------------------------------
#  String escaping (Lua double-quoted, latin1 with \xNN)
# -------------------------------------------------------------------------
def _lua_str(b):
    if isinstance(b, str):
        b = b.encode("latin1", "replace")
    out = ['"']
    for byte in b:
        if byte == 0x22:           out.append('\\"')
        elif byte == 0x5C:         out.append("\\\\")
        elif byte == 0x0A:         out.append("\\n")
        elif byte == 0x0D:         out.append("\\r")
        elif byte == 0x09:         out.append("\\t")
        elif 0x20 <= byte < 0x7F:  out.append(chr(byte))
        else:                      out.append(f"\\x{byte:02x}")
    out.append('"')
    return "".join(out)


def _is_simple_ident(s):
    """True if `s` can be written as a Lua identifier (so we can use
    `q.var "name"` instead of `q.var("name")` syntax)."""
    if not s: return False
    if not (s[0].isalpha() or s[0] == "_"): return False
    return all(c.isalnum() or c == "_" for c in s)


def _name_arg(b):
    """Emit a name arg: bare quoted-string call if identifier-like, else
    explicit parens form. Returns the rendered text *without* parens."""
    if isinstance(b, bytes):
        try: s = b.decode("ascii")
        except UnicodeDecodeError: s = None
    elif isinstance(b, str):
        s = b
    else:
        s = None
    return _lua_str(b)


# -------------------------------------------------------------------------
#  Pattern matchers — each returns either the Lua line or None.
# -------------------------------------------------------------------------
def _ops_match(ops, *expected):
    """Match a sequence of ops against a list of `(label, args_predicate)`
    tuples. `args_predicate` may be a list of literals (compared with ==),
    `None` (don't care), or a callable (called with the args list).

    `ops` items are `(label_str, args_list)` per `funkcode_ops.disasm_all`."""
    if len(ops) != len(expected): return False
    for op, exp in zip(ops, expected):
        if op[0] != exp[0]: return False
        if exp[1] is None: continue
        if callable(exp[1]):
            if not exp[1](op[1]): return False
        else:
            if list(op[1]) != list(exp[1]): return False
    return True


def _try_var(tag, ops):
    """tag=0x43 DLG_OP_a name "\\x0b" END END END  →  q.var "name" """
    if tag != 0x43: return None
    if not _ops_match(ops,
                       ("DLG_OP_a", lambda a: len(a) == 2 and a[1] == b"\x0b"),
                       ("END", []), ("END", []), ("END", [])):
        return None
    name = ops[0][1][0]  # first arg of first op
    if isinstance(name, bytes) and _is_simple_ident(name.decode("latin1","replace")):
        return f'q.var {_lua_str(name)}'
    return f'q.var({_lua_str(name)})'


def _try_assign(tag, ops):
    """tag=0x69 DLG_OP_a name "\\x0b" END END END U32_qid_a N → q.assign("name", N)"""
    if tag != 0x69: return None
    if not _ops_match(ops,
                       ("DLG_OP_a", lambda a: len(a) == 2 and a[1] == b"\x0b"),
                       ("END", []), ("END", []), ("END", []),
                       ("U32_qid_a", lambda a: len(a) == 1 and isinstance(a[0], int))):
        return None
    name = ops[0][1][0]
    value = ops[4][1][0]
    return f'q.assign({_lua_str(name)}, {value})'


def _try_assign_inc(tag, ops):
    """tag=0x69 DLG_OP_a name "\\x0b\\x01" END END U32_qid_a N → q.assign_inc(...)"""
    if tag != 0x69: return None
    if not _ops_match(ops,
                       ("DLG_OP_a", lambda a: len(a) == 2 and a[1] == b"\x0b\x01"),
                       ("END", []), ("END", []),
                       ("U32_qid_a", lambda a: len(a) == 1 and isinstance(a[0], int))):
        return None
    name = ops[0][1][0]
    value = ops[3][1][0]
    return f'q.assign_inc({_lua_str(name)}, {value})'


def _try_set_hero_qbit(tag, ops):
    """tag=0x44 DLG_OP_a "NN" "\\x0b" END END END → q.set_hero_qbit(NN)"""
    if tag != 0x44: return None
    if not _ops_match(ops,
                       ("DLG_OP_a", lambda a: len(a) == 2 and a[1] == b"\x0b"),
                       ("END", []), ("END", []), ("END", [])):
        return None
    name = ops[0][1][0]
    if not isinstance(name, bytes):
        return None
    try:
        bit = int(name.decode("ascii"))
    except (UnicodeDecodeError, ValueError):
        return None
    return f"q.set_hero_qbit({bit})"


def _try_dialog_trigger(tag, ops):
    """tag=0x1a STR_REF name → d.trigger "name" """
    if tag != 0x1a: return None
    if not _ops_match(ops, ("STR_REF", lambda a: len(a) == 1)):
        return None
    name = ops[0][1][0]
    if isinstance(name, bytes) and _is_simple_ident(name.decode("latin1","replace")):
        return f'd.trigger {_lua_str(name)}'
    return f'd.trigger({_lua_str(name)})'


def _try_dialog_line(tag, ops):
    """tag=0x3c DLG_OP_a res btn → d.line("res", "btn")"""
    if tag != 0x3c: return None
    if not _ops_match(ops, ("DLG_OP_a", lambda a: len(a) == 2)):
        return None
    args = ops[0][1]
    return f'd.line({_lua_str(args[0])}, {_lua_str(args[1])})'


def _try_dialog_emit(tag, ops):
    """tag=0x42 EMIT_a N target → d.emit(N, "target")"""
    if tag != 0x42: return None
    if not _ops_match(ops, ("EMIT_a", lambda a: len(a) == 2 and isinstance(a[0], int))):
        return None
    args = ops[0][1]
    return f'd.emit({args[0]}, {_lua_str(args[1])})'


# Registered in order of preference. First match wins.
PATTERNS = [
    _try_var,
    _try_assign,
    _try_assign_inc,
    _try_set_hero_qbit,
    _try_dialog_trigger,
    _try_dialog_line,
    _try_dialog_emit,
]


# -------------------------------------------------------------------------
#  Fallback: raw.rec(...) form for unrecognised records.
# -------------------------------------------------------------------------
def _fmt_op_arg(a):
    if isinstance(a, int):
        return str(a)
    if isinstance(a, (bytes, bytearray)):
        return _lua_str(bytes(a))
    if isinstance(a, str):
        return _lua_str(a)
    if isinstance(a, tuple) and len(a) == 2 and a[0] in ("raw", "tail"):
        return _lua_str(bytes(a[1]))
    raise ValueError(f"can't fmt arg {a!r}")


def _fmt_op(label, args):
    if args:
        return "{" + _lua_str(label.encode()) + ", " + ", ".join(_fmt_op_arg(a) for a in args) + "}"
    return "{" + _lua_str(label.encode()) + "}"


def _fmt_rec(tag, flags, ops):
    op_text = [_fmt_op(l, a) for l, a in ops]
    body = (", ".join(op_text)) if op_text else ""
    return f"raw.rec(0x{tag:02x}, 0x{flags:02x}{', ' + body if body else ''})"


# -------------------------------------------------------------------------
#  Driver
# -------------------------------------------------------------------------
def decompile_semantic(buf):
    lines = []
    lines.append("-- Auto-generated by sdk/tools/funkcode_decompile_semantic.py")
    lines.append("-- Edit freely — same-name records re-bake to byte-identical .bin")
    lines.append(f"-- input bytes: {len(buf):,}")
    lines.append("")
    lines.append('local q   = require "quest"')
    lines.append('local d   = require "dialog"')
    lines.append('local raw = require "raw"')
    lines.append("")
    lines.append("return q.script {")

    n_records = 0
    n_semantic = 0
    n_fallback = 0
    for rec in dec.walk_records(buf):
        if rec[0] == "tail":
            _, off, raw_bytes = rec
            lines.append(f"  -- TAIL at 0x{off:08x}: {len(raw_bytes)} bytes (cannot emit; bake will drop)")
            continue
        _, off, tag, size, payload = rec
        flags = payload[0] if payload else 0
        body = payload[1:] if payload else b""
        try:
            ops = fops.disasm_all(body)
        except fops.CannotDecode:
            ops = None

        emitted = None
        if ops is not None and flags == 0x00:
            # Semantic patterns ONLY fire on flags==0 records to avoid
            # silently losing the flag byte. Add per-pattern flag support
            # later if needed.
            for p in PATTERNS:
                emitted = p(tag, ops)
                if emitted: break

        if emitted is not None:
            lines.append(f"  {emitted},  -- 0x{off:08x}")
            n_semantic += 1
        else:
            # Fallback: raw.rec form, byte-perfect.
            if ops is None:
                # Even structural decompose failed — use _HEX
                hex_s = body.hex()
                lines.append(f'  raw.rec(0x{tag:02x}, 0x{flags:02x}, {{"_HEX", "{hex_s}"}}),  -- 0x{off:08x}')
            else:
                lines.append(f"  {_fmt_rec(tag, flags, ops)},  -- 0x{off:08x}")
            n_fallback += 1
        n_records += 1

    lines.append("}")
    lines.insert(3, f"-- {n_records} records  ({n_semantic} semantic, {n_fallback} raw fallback)")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("-o", "--output", help="output .lua (default: <src>.lua)")
    args = ap.parse_args()
    with open(args.src, "rb") as f:
        data = f.read()
    text = decompile_semantic(data)
    out = args.output or (args.src + ".lua")
    os.makedirs(os.path.dirname(os.path.abspath(out)) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {out}  ({len(text):,} chars, {len(data):,} input bytes)")


if __name__ == "__main__":
    main()
