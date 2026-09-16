"""reborn_audit.py - check a generated records_generated.inc against our build.

Usage:  python reborn_audit.py [path/to/records_generated.inc]

For every site: both ends are instruction boundaries of our function (decoded
from its Ghidra entry), the new bytes decode to whole instructions, calls land
on function entries of ours, and every absolute data address the new code
reads is one our function already references (or a geometry slot). Stubs get
the same data check. A block ported from a mis-decoded start fails here: the
table of 6588bca had three.
"""
import re, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buildmap as bm, scan
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86 as X
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
ours = bm.Image(bm.OURS)
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "patchset", "records_generated.inc")
src = open(path, encoding="utf-8").read()
ENTRIES = set(scan.GH_ENTRIES)
def arr(name): return bytearray(int(x, 16) for x in re.search(r"static const uint8_t " + re.escape(name) + r"\[\] = \{ ([^}]*) \};", src).group(1).split(", "))
def fixes(name):
    m = re.search(r"static const Fixup " + re.escape(name) + r"\[\] = \{ (.*) \};", src)
    return [(int(o), k, int(a, 16)) for o, k, a in re.findall(r"\{ (\d+), Fix::(\w+), 0x([0-9A-F]+)u \}", m.group(1))] if m else []
def apply(b, base, fx):
    b = bytearray(b)
    for o, k, a in fx:
        if k == "Rel32ToVA": struct.pack_into("<i", b, o, a - (base + o + 4))
        elif k == "Abs32ToVA": struct.pack_into("<I", b, o, a)
        elif k == "Abs32Geom": struct.pack_into("<I", b, o, 0x7EE00000 | (a & 0xFFFF))
        elif k == "Abs32Const": struct.pack_into("<I", b, o, 0x7CC00000)
    return b
problems = 0
def check(insns, func_refs, where):
    global problems
    for i in insns:
        if i.group(X.X86_GRP_CALL) and i.operands and i.operands[0].type == X.X86_OP_IMM:
            t = i.operands[0].imm & 0xffffffff
            if bm.Image.in_text(ours, t) and t not in ENTRIES:
                print(f"  {where}: call {t:#x} is not a function entry of ours"); problems += 1
        for op in i.operands:
            if op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                v = op.mem.disp & 0xffffffff
                if 0x400000 <= v < 0x1e00000 and v not in func_refs and not (0x7EE00000 <= v < 0x7EE10000):
                    print(f"  {where}: `{i.mnemonic} {i.op_str}` reads {v:#x}, which our function never references"); problems += 1
for tag in re.findall(r"// --- (\w+):", src):
    sites = re.search(re.escape(tag) + r"_sites\[\] = \{(.*?)\};", src, re.S).group(1)
    for va, ln, e, n, f, nf in re.findall(r"\{ 0x([0-9A-F]+)u, (\d+), (\w+), (\w+), (\w+), (\d+) \}", sites):
        va, ln = int(va, 16), int(ln)
        fn = scan.gh_func_of(va)
        if fn is None: continue
        fins = list(md.disasm(ours.rd(fn, scan.GH_MAX[fn] - fn + 1), fn))
        refs = {op.mem.disp & 0xffffffff for i in fins for op in i.operands if op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0}
        refs |= {op.imm & 0xffffffff for i in fins for op in i.operands if op.type == X.X86_OP_IMM}
        if va not in {i.address for i in fins}:
            print(f"  {tag} site {va:#x}: not an instruction boundary of our function"); problems += 1
        if va + ln not in {i.address for i in fins}:
            print(f"  {tag} site {va:#x}+{ln}: end is not an instruction boundary"); problems += 1
        nb = apply(arr(n), va, [x for x in fixes(f) if x[1] not in ("Rel32ToStub",)])
        ins = list(md.disasm(bytes(nb), va))
        stub_calls = {va + o - 1 for o, k, a in fixes(f) if k == "Rel32ToStub"}
        ins_for_check = [i for i in ins if i.address not in stub_calls]
        if sum(i.size for i in ins) != ln:
            print(f"  {tag} site {va:#x}: new bytes do not decode to whole instructions"); problems += 1
        check(ins_for_check, refs, f"{tag} site {va:#x}")
    for sid, blob in re.findall(re.escape(tag) + r"_s(\d+)\[\] = \{ ([^}]*) \};", src):
        b = apply(bytearray(int(x, 16) for x in blob.split(", ")), 0x10000000, fixes(f"{tag}_sf{sid}"))
        fn = None
        m = re.search(re.escape(tag) + r"_sites\[\] = \{\s*\{ 0x([0-9A-F]+)u", src)
        fn = scan.gh_func_of(int(m.group(1), 16)) if m else None
        if fn is None: continue
        fins = list(md.disasm(ours.rd(fn, scan.GH_MAX[fn] - fn + 1), fn))
        refs = {op.mem.disp & 0xffffffff for i in fins for op in i.operands if op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0}
        check(list(md.disasm(bytes(b), 0x10000000)), refs, f"{tag} stub {sid}")
print("problems:", problems)
