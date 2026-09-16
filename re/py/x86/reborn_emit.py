"""reborn_emit.py - turn the catalogue into patch records the SDK can apply.

Usage:  python reborn_catalog.py && python reborn_emit.py

Reads  sdk/re/hd_table.gen.json      (from reborn_catalog.py)
       sdk/re/hd_decisions.json      (committed: per-site verdicts, addresses only)
Writes sdk/patchset/records_generated.inc   (gitignored - it holds engine bytes)

What it emits today: the run-time operand writes ReBorn's init performs, as
ordinary patch records. We know the resolution when we patch, so a value ReBorn
had to fetch through a global at run time is simply baked into the operand:
  mov  r32, 1024    ->  mov  r32, W          (Imm32GeomSet)
  push 501          ->  push 501 + offsetX   (Imm32GeomAdd)
  mov  word [..], 62 -> ... + offsetY        (Imm16GeomAdd)
The expected bytes come out of OUR Sacred_decrypted.exe, never out of ReBorn's
binary, so the record verifies against the engine we actually patch.

Sites are grouped into one record per function of ours, because a half-patched
function draws a torn frame; the patch engine applies a record atomically.
Everything the generator will not vouch for is listed at the end of the run and
left out, never guessed.
"""
import os, json, collections
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86 as X
import buildmap as bm

HERE = os.path.dirname(os.path.abspath(__file__))
RE_DIR = os.path.normpath(os.path.join(HERE, "..", ".."))
SDK_DIR = os.path.normpath(os.path.join(RE_DIR, ".."))
IN_JSON = os.path.join(RE_DIR, "hd_table.gen.json")
DECISIONS = os.path.join(RE_DIR, "hd_decisions.json")
OUT_INC = os.path.join(SDK_DIR, "patchset", "records_generated.inc")

MD = Cs(CS_ARCH_X86, CS_MODE_32)
MD.detail = True

FIX = {("mov", 4): "Imm32GeomSet", ("add", 4): "Imm32GeomAdd", ("add", 2): "Imm16GeomAdd",
       ("mov", 2): "Imm16GeomSet"}


def imm_field(ins, va):
    """Offset and width of the operand field at `va`, if it really is a field.

    A 2-byte write into the low half of a 4-byte immediate counts: ReBorn adds
    the offset as a word wherever the value cannot overflow into the high half,
    and x86 is little-endian, so the field starts at the same address.
    """
    for op in ins.operands:
        if op.type == X.X86_OP_IMM:
            w = op.size
            ofs = ins.size - w          # immediates sit at the end of the encoding
            if ins.address + ofs == va:
                return ofs, w
        elif op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            for ofs in range(2, ins.size - 3):
                if ins.address + ofs == va:
                    return ofs, 4
    return None, None


def c_bytes(b):
    return "{ " + ", ".join(f"0x{x:02X}" for x in b) + " }"


def main():
    d = json.load(open(IN_JSON, encoding="utf-8"))
    decisions = {}
    if os.path.exists(DECISIONS):
        decisions = {int(k, 0) if isinstance(k, str) and k.startswith("0x") else k: v
                     for k, v in json.load(open(DECISIONS, encoding="utf-8")).items()
                     if not str(k).startswith("_")}
    ours = bm.Image(bm.OURS)

    accepted, rejected = collections.defaultdict(list), []
    for r in d["sites"]:
        if r["class"] != "selfpatch":
            continue
        tgt, host_s = r.get("our_target"), r.get("our_host")
        why = None
        if tgt is None:
            why = f"not located ({r['located_by']})"
        elif r["src"] != "slot":
            why = f"value is not a layout slot ({r['src_val']})"
        elif not r.get("field_equal"):
            why = "our operand differs from ReBorn's original"
        elif decisions.get(tgt) == "reject":
            why = "rejected in hd_decisions.json"
        if why:
            rejected.append((r, why))
            continue
        host_va = int(host_s.split(":")[0], 16)
        ins = next(MD.disasm(ours.rd(host_va, 16), host_va), None)
        ofs, w = imm_field(ins, tgt) if ins else (None, None)
        kind = FIX.get((r["writer_op"], r["writer_width"]))
        if ofs is None or kind is None or w < r["writer_width"]:
            rejected.append((r, f"operand at {tgt:#x} is not a {r['writer_width']}-byte field of "
                                f"`{host_s}` ({r['writer_op']}/{r['writer_width']})"))
            continue
        if kind == "Imm16GeomSet":
            rejected.append((r, "16-bit set is not implemented in the patch engine"))
            continue
        accepted[(r["our_func"], r["our_func_name"])].append(
            dict(va=host_va, len=ins.size, bytes=ours.rd(host_va, ins.size),
                 ofs=ofs, kind=kind, slot=r["src_val"],
                 asm=f"{ins.mnemonic} {ins.op_str}", writer=r["writer_asm"]))

    # one record per function; sites in address order, one site per instruction
    # (two writes into the same instruction become two fixups), no overlaps
    out, rows, n = [], [], 0
    for (func, name), sites in sorted(accepted.items(), key=lambda kv: kv[0][0] or 0):
        merged = {}
        for s in sites:
            m = merged.setdefault(s["va"], dict(s, fx=[]))
            m["fx"].append((s["ofs"], s["kind"], s["slot"]))
        clean, last = [], -1
        for va in sorted(merged):
            s = merged[va]
            s["fx"].sort()
            if va < last:
                rejected.append(({"our_target": va}, "overlaps the previous site"))
                continue
            clean.append(s); last = va + s["len"]
        if not clean:
            continue
        tag = f"hd_{(name or 'at_%08x' % clean[0]['va']).lower()}"
        n += 1
        out.append(f"// --- {tag}: {len(clean)} operand(s) of {name or hex(func or 0)}")
        for i, s in enumerate(clean):
            out.append(f"// {s['va']:#010x}  {s['asm']:<34} <- {s['writer']}")
            out.append(f"static const uint8_t {tag}_b{i}[] = {c_bytes(s['bytes'])};")
            fx = ", ".join(f"{{ {o}, Fix::{k}, 0x{sl:08X}u }}" for o, k, sl in s["fx"])
            out.append(f"static const Fixup {tag}_f{i}[] = {{ {fx} }};")
        out.append(f"static const Site {tag}_sites[] = {{")
        for i, s in enumerate(clean):
            out.append(f"    {{ 0x{s['va']:08X}u, {s['len']}, {tag}_b{i}, {tag}_b{i}, "
                       f"{tag}_f{i}, {len(s['fx'])} }},")
        out.append("};\n")
        rows.append(f'{{ "{tag}", "{name or hex(func or 0)} layout", "hd",\n'
                    f'  "OverLookers (ReBorn), HD geometry - relocated by re/py/x86/reborn_emit.py",\n'
                    f'  nullptr, false, nullptr, 0, nullptr, 0, {tag}_sites, {len(clean)} }},')

    with open(OUT_INC, "w", encoding="utf-8", newline="\n") as f:
        f.write("// GENERATED by re/py/x86/reborn_emit.py - do not edit, do not commit.\n"
                "// Expected bytes come from our own Sacred_decrypted.exe; the layout values\n"
                "// come from hd/geometry.cpp at apply time. See\n"
                "// .claude/knowledge/re/reborn_hd_port.md.\n\n")
        f.write("#ifdef SDK_PATCH_DATA\n\n")
        f.write("\n".join(out))
        f.write("\n#endif // SDK_PATCH_DATA\n\n#ifdef SDK_PATCH_ROWS\n\n")
        f.write("\n\n".join(rows))
        f.write("\n\n#endif // SDK_PATCH_ROWS\n")

    print(f"records: {n}, sites: {sum(len(v) for v in accepted.values())} -> {OUT_INC}")
    byreason = collections.Counter(w.split("(")[0].strip() for _, w in rejected)
    print("left out:", dict(byreason))
    for r, w in rejected:
        t = r.get("our_target")
        print(f"   {t and hex(t) or '?':>10}  {w}")


if __name__ == "__main__":
    main()
