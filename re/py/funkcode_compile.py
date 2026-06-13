"""Sacred FunkCode assembler — reads a `.fkasm` text and emits `.bin`.

Inverse of `funkcode_decompile.py`. Contract:

    compile(decompile(B)) == B           # for every retail FunkCode.bin

The text format is line-oriented; everything else (blank or `#`-prefixed) is
treated as a comment.

    REC tt                       # start a new record; `tt` is the 1-byte tag in hex
    FLAGS bb                     # one flags byte (first byte of the payload)
    OP <LABEL> <args...>         # mnemonic opcode — see funkcode_ops.py
    HEX b0 b1 ...                # raw payload bytes (used as fallback when no
                                 #   mnemonic form exists for an opcode)
    TAIL b0 b1 ...               # literal bytes outside the record framing

A record's payload is the concatenation, in order, of the FLAGS byte and every
OP/HEX line under that REC. Record `size` is **auto-computed** as
`3 + len(payload)` so editors don't have to maintain it manually.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import funkcode_ops as fops

_LINE = re.compile(r"^\s*(REC|FLAGS|OP|HEX|TAIL)\b\s*(.*?)\s*$")


def _parse_hex_payload(rest):
    """Decode whitespace-separated hex pairs into bytes. Tokens may be either
    `NN` or `0xNN` (we strip leading 0x)."""
    out = bytearray()
    for tok in rest.split():
        if tok.startswith("0x") or tok.startswith("0X"):
            tok = tok[2:]
        if len(tok) != 2:
            raise ValueError(f"bad hex token {tok!r}")
        out.append(int(tok, 16))
    return bytes(out)


def compile(text):
    """Turn `.fkasm` text into the .bin byte stream."""
    out = bytearray()
    cur_tag = None
    cur_payload = bytearray()

    def flush_record():
        if cur_tag is None:
            return
        size = 3 + len(cur_payload)
        if size > 0xFFFF:
            raise ValueError(f"record tag=0x{cur_tag:02x} payload too large: {len(cur_payload)} bytes (max 65532)")
        out.append(cur_tag)
        out.append((size >> 8) & 0xFF)
        out.append(size & 0xFF)
        out.extend(cur_payload)

    in_tail = False
    for lineno, raw in enumerate(text.splitlines(), 1):
        # Strip line comments but be careful — `#` may appear inside quoted
        # strings in OP lines. Cheap heuristic: only strip comments before
        # the first quote.
        cut = raw.find("#")
        first_quote = min(
            (raw.find(q) for q in ('"', "'") if raw.find(q) >= 0),
            default=-1,
        )
        if cut >= 0 and (first_quote < 0 or cut < first_quote):
            line = raw[:cut].rstrip()
        else:
            line = raw.rstrip()
        if not line.strip():
            continue
        m = _LINE.match(line)
        if not m:
            raise ValueError(f"line {lineno}: malformed: {raw!r}")
        kind, rest = m.group(1), m.group(2)

        if kind == "REC":
            flush_record()
            try:
                cur_tag = int(rest.split()[0], 16)
            except Exception:
                raise ValueError(f"line {lineno}: bad REC tag: {rest!r}")
            cur_payload = bytearray()
            in_tail = False
        elif kind == "FLAGS":
            if cur_tag is None or in_tail:
                raise ValueError(f"line {lineno}: FLAGS outside an active REC")
            try:
                cur_payload.append(int(rest.strip(), 0) & 0xFF)
            except Exception:
                raise ValueError(f"line {lineno}: bad FLAGS value: {rest!r}")
        elif kind == "OP":
            if cur_tag is None or in_tail:
                raise ValueError(f"line {lineno}: OP outside an active REC")
            try:
                label, args = fops.parse_op_line("OP " + rest)
                cur_payload.extend(fops.assemble_op(label, args))
            except Exception as e:
                raise ValueError(f"line {lineno}: OP failed: {e}")
        elif kind == "HEX":
            if cur_tag is None or in_tail:
                raise ValueError(f"line {lineno}: HEX outside an active REC")
            cur_payload.extend(_parse_hex_payload(rest))
        elif kind == "TAIL":
            flush_record()
            cur_tag = None
            in_tail = True
            out.extend(_parse_hex_payload(rest))
    flush_record()
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", help="path to a .fkasm")
    ap.add_argument("-o", "--output", help="output .bin (default: <src>.bin)")
    args = ap.parse_args()

    with open(args.src, "r", encoding="utf-8") as f:
        text = f.read()
    data = compile(text)
    out = args.output or (args.src + ".bin")
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out}  ({len(data):,} bytes)")


if __name__ == "__main__":
    main()
