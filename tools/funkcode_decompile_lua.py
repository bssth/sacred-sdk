"""Sacred FunkCode → Lua decompiler.

Produces a `.lua` data file representing one FunkCode .bin as a list of records.
Pairs with `sdk/lua_bake.cpp` (in-DLL baker) which re-assembles the same .bin
from the Lua. Round-trip is byte-perfect: every record that funkcode_ops can
decompose into mnemonics becomes a structured Lua entry; records that need
the raw-byte fallback are emitted as `{tag, flags, {"_HEX", "<hex>"}}` — the
`_HEX` pseudo-opcode is recognised by the baker as "copy these bytes verbatim
into the payload, after the flags byte".

Output format
-------------
    return {
      -- record 0  tag=0x14  Subsys_14
      { 0x14, 0x00, {"U32_qid_a", 9511} },

      -- record 1  tag=0x7d  EventBroadcast
      { 0x7d, 0x00,
        {"U32_qid_a", 3}, {"U32_qid_a", 40}, {"U32_qid_a", 2},
      },

      -- record N  tag=0x42  BlockReader (hex fallback)
      { 0x42, 0x00, {"_HEX", "97"} },
    }

`_HEX` is the escape hatch for opcodes funkcode_ops can't structurally model
yet — we round-trip them as raw bytes.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from funkcode_tags import label_for as tag_label
import funkcode_decompile as dec
import funkcode_ops as fops


def _escape_lua_str(b):
    """Render `bytes` as a Lua string literal (latin1 with \\xNN escapes)."""
    out = ['"']
    for byte in b:
        if byte == 0x22:           # "
            out.append('\\"')
        elif byte == 0x5C:         # \
            out.append("\\\\")
        elif byte == 0x0A:
            out.append("\\n")
        elif byte == 0x0D:
            out.append("\\r")
        elif byte == 0x09:
            out.append("\\t")
        elif 0x20 <= byte < 0x7F:
            out.append(chr(byte))
        else:
            out.append(f"\\x{byte:02x}")
    out.append('"')
    return "".join(out)


def _format_arg(arg):
    """Render one arg in Lua syntax. Integers stay as decimals (with hex hint
    via comment elsewhere). Bytes become `"\\xNN…"` quoted strings."""
    if isinstance(arg, int):
        return str(arg)
    if isinstance(arg, (bytes, bytearray)):
        return _escape_lua_str(bytes(arg))
    if isinstance(arg, str):
        return _escape_lua_str(arg.encode("latin1", "replace"))
    if isinstance(arg, tuple) and len(arg) == 2 and arg[0] in ("raw", "tail"):
        return _escape_lua_str(bytes(arg[1]))
    raise ValueError(f"can't format arg {arg!r}")


def _format_op(label, args):
    if args:
        return "{" + repr(label).replace("'", '"') + ", " + ", ".join(_format_arg(a) for a in args) + "}"
    return "{" + repr(label).replace("'", '"') + "}"


def decompile_to_lua(buf):
    out = []
    out.append("-- Auto-generated Sacred FunkCode → Lua dump.")
    out.append("-- This file is consumed by sdk/lua_bake.cpp at game startup.")
    out.append("-- Edit freely: re-run via overlay 'Rebake Lua mods' or restart Sacred.")
    out.append(f"-- input bytes: {len(buf):,}")
    out.append("")
    out.append("return {")

    n_records = 0
    n_hex_fallback = 0
    for rec in dec.walk_records(buf):
        if rec[0] == "tail":
            # We don't currently emit tail data through the Lua format. The
            # bake produces only the records — anything after them is dropped.
            # Sacred's bin files we tested all have clean record framing all
            # the way to EOF except for a couple of small .bin files (merc,
            # MultiStart) where the WHOLE file is a non-record blob; those
            # aren't FunkCode and should be byte-copied, not Lua-decompiled.
            _, off, raw = rec
            out.append(f"  -- WARNING: trailing {len(raw)} bytes at 0x{off:08x} not encoded")
            continue
        _, off, tag, size, payload = rec
        tname = tag_label(tag) or f"tag_{tag:02x}"
        flags = payload[0] if payload else 0
        body = payload[1:] if payload else b""

        # Try mnemonic decompose
        ops_text = None
        if body:
            try:
                ops = fops.disasm_all(body)
                rebuilt = b"".join(fops.assemble_op(l, a) for l, a in ops)
                if rebuilt == body:
                    ops_text = [_format_op(l, a) for l, a in ops]
            except fops.CannotDecode:
                pass

        out.append(f"  -- record {n_records}  offset=0x{off:08x}  tag=0x{tag:02x}  {tname}")
        if ops_text is None:
            # _HEX fallback
            hex_s = body.hex()
            out.append(f"  {{ 0x{tag:02x}, 0x{flags:02x}, {{\"_HEX\", \"{hex_s}\"}} }},")
            n_hex_fallback += 1
        elif len(ops_text) == 0:
            out.append(f"  {{ 0x{tag:02x}, 0x{flags:02x} }},")
        elif len(ops_text) == 1:
            out.append(f"  {{ 0x{tag:02x}, 0x{flags:02x}, {ops_text[0]} }},")
        else:
            out.append(f"  {{ 0x{tag:02x}, 0x{flags:02x},")
            for ot in ops_text:
                out.append(f"    {ot},")
            out.append("  },")
        n_records += 1

    out.append("}")
    out.insert(4, f"-- records: {n_records}  (hex-fallback: {n_hex_fallback})")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("-o", "--output", help="output .lua (default: <src>.lua)")
    args = ap.parse_args()
    with open(args.src, "rb") as f:
        data = f.read()
    text = decompile_to_lua(data)
    out = args.output or (args.src + ".lua")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {out}  ({len(text):,} chars, {len(data):,} input bytes)")


if __name__ == "__main__":
    main()
