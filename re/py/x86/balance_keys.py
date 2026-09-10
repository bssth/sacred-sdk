"""Extract Sacred's balance-key -> global-variable table from FUN_005eb010."""
import struct, re, sys
from capstone.x86 import *
import scan, strmap
from scan import MD, TEXT, TEXT_VA, GH_MAX

FN = 0x005eb010
end = GH_MAX[FN]
ins = list(MD.disasm(TEXT[FN-TEXT_VA:end-TEXT_VA+1], FN))

def is_store_abs(i):
    """mov [imm32], reg  |  fstp/fist dword [imm32]  -> absolute VA, or None."""
    for op in i.operands:
        if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            d = op.mem.disp & 0xFFFFFFFF
            if 0x008e0000 <= d < 0x01900000:
                if i.mnemonic in ("mov", "fstp", "fistp", "movsx", "movzx") and i.operands[0].type == X86_OP_MEM:
                    return d
                if i.mnemonic in ("fstp", "fistp"):
                    return d
    return None

rows = []
for idx, i in enumerate(ins):
    # a key string is pushed as an immediate
    key = None
    for op in i.operands:
        if op.type == X86_OP_IMM:
            v = op.imm & 0xFFFFFFFF
            s = strmap.STRINGS.get(v)
            if s and re.match(r"^[A-Za-z_][A-Za-z0-9_:.]{2,}$", s):
                key = (v, s)
    if not key: continue
    tgt = None
    for j in range(idx + 1, min(idx + 40, len(ins))):
        d = is_store_abs(ins[j])
        if d is not None:
            tgt = (d, ins[j].mnemonic)
            break
        if ins[j].mnemonic == "push":
            nx = ins[j].operands
            if nx and nx[0].type == X86_OP_IMM and (nx[0].imm & 0xFFFFFFFF) in strmap.STRINGS:
                break     # next key block started
    rows.append((i.address, key[1], tgt))

print("key blocks found: %d (with a resolved global: %d)" %
      (len(rows), sum(1 for _, _, t in rows if t)))
print()
print("%-28s %-10s %s" % ("key", "global", "store"))
for a, k, t in rows:
    print("%-28s %-10s %s" % (k[:28], ("%08x" % t[0]) if t else "-", t[1] if t else ""))
