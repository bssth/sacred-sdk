"""Sacred FunkCode decompiler — emits a lossless `.fkasm` text representation.

Pairs with `funkcode_compile.py`. The contract is:

    compile(decompile(open(F, "rb").read())) == open(F, "rb").read()

…for every FunkCode.bin in retail Sacred. The format is intentionally simple:
**HEX bytes are the source of truth**; human-readable decoded opcodes appear
as `#`-prefixed comments next to them. Edit the hex, recompile, and the size
field gets recomputed for you.

Record framing (per docs/05-funkcode-grammar.md):

    [tag:u8][size:u16 BE][payload of (size - 3) bytes]

`payload[0]` is a flags byte; the rest is a sequence of opcodes interpreted by
FUN_00472bc0. The decoded comment annotates opcodes inside each payload so a
human can navigate, but the decompile is line-for-line preservable: it round-
trips bytes-identical via `funkcode_compile.py`.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from funkcode_tags import label_for as tag_label
from funkcode_disasm import OPCODE_TABLE, disasm_one
import funkcode_ops as fops


def walk_records(buf):
    """Yield (offset, tag, size, payload) for every well-formed record,
    plus a final ('tail', off, raw_bytes) tuple for any trailing bytes that
    don't form a complete record. The tail is preserved verbatim so
    round-trip stays correct even on files with odd trailers."""
    off = 0
    n = len(buf)
    while off + 3 <= n:
        tag = buf[off]
        size = (buf[off + 1] << 8) | buf[off + 2]
        if size < 3 or off + size > n:
            yield ("tail", off, buf[off:])
            return
        payload = buf[off + 3 : off + size]
        yield ("record", off, tag, size, payload)
        off += size
    if off < n:
        yield ("tail", off, buf[off:])


def decode_payload_comments(payload):
    """Return a list of '# +OFFS  op  LABEL  pretty'-style comment lines
    describing opcodes inside `payload`. Stops gracefully on the first byte
    the disassembler can't classify (the hex line remains valid regardless)."""
    if not payload:
        return []
    out = [f"#   flags=0x{payload[0]:02x}"]
    ip = 1
    while ip < len(payload):
        op = payload[ip]
        label, pr, next_ip = disasm_one(payload, ip)
        out.append(f"#   +{ip:04x}  {op:02x}  {label or f'UNK_{op:02X}'}  {pr}")
        if next_ip <= ip:
            break
        ip = next_ip
    return out


def hex_chunks(data, width=32):
    """Split `data` into width-byte rows of space-separated hex pairs."""
    rows = []
    for i in range(0, len(data), width):
        chunk = data[i : i + width]
        rows.append(" ".join(f"{b:02x}" for b in chunk))
    return rows


def _try_mnemonic_emit(payload):
    """Try to fully mnemonic-decode the payload (after the flags byte).
    Returns a list of `OP ...` lines that re-assemble byte-identically, or
    None if any opcode is unknown/complex/would overrun. Verified with a
    round-trip check to be safe."""
    if not payload:
        return None
    body = payload[1:]
    try:
        ops = fops.disasm_all(body)
    except fops.CannotDecode:
        return None
    # Verify exact byte-level round-trip — bug in assemble_op MUST surface here.
    rebuilt = bytearray()
    for label, args in ops:
        rebuilt += fops.assemble_op(label, args)
    if bytes(rebuilt) != body:
        return None
    out = [f"FLAGS 0x{payload[0]:02x}"]
    for label, args in ops:
        out.append(fops.format_op_line(label, args))
    return out


def decompile(buf):
    """Produce the .fkasm text representation of a FunkCode .bin buffer.

    For each record, we prefer mnemonic `OP <label> args...` lines (verified
    by re-assembly to byte-match the original). When mnemonic decode fails
    (unknown opcode, truncated string, etc.) we fall back to raw `HEX` lines
    so round-trip is still byte-perfect."""
    lines = []
    lines.append("# fkasm v2 — Sacred FunkCode disassembly")
    lines.append(f"# input bytes: {len(buf)}")
    lines.append("")

    n_mnemonic = 0
    n_hex = 0
    for rec in walk_records(buf):
        if rec[0] == "tail":
            _, off, raw = rec
            lines.append(f"# TAIL  offset=0x{off:08x}  size={len(raw)}  (preserved verbatim)")
            for row in hex_chunks(raw):
                lines.append(f"TAIL {row}")
            lines.append("")
            continue

        _, off, tag, size, payload = rec
        tname = tag_label(tag) or f"tag_{tag:02x}"
        lines.append(f"# RECORD  offset=0x{off:08x}  tag=0x{tag:02x}  {tname}  size={size}  payload={len(payload)}B")
        lines.append(f"REC {tag:02x}")

        mnem = _try_mnemonic_emit(payload)
        if mnem is not None:
            lines.extend(mnem)
            n_mnemonic += 1
        else:
            # Fallback: raw bytes. Commented decode for human reference.
            for cmt in decode_payload_comments(payload):
                lines.append(cmt)
            for row in hex_chunks(payload):
                lines.append(f"HEX {row}")
            n_hex += 1
        lines.append("")
    lines.insert(2, f"# records: mnemonic={n_mnemonic}  hex-fallback={n_hex}")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", help="path to a FunkCode.bin")
    ap.add_argument("-o", "--output", help="output .fkasm (default: <src>.fkasm)")
    args = ap.parse_args()

    with open(args.src, "rb") as f:
        data = f.read()
    text = decompile(data)
    out = args.output or (args.src + ".fkasm")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {out}  ({len(text):,} chars, {len(data):,} input bytes)")


if __name__ == "__main__":
    main()
