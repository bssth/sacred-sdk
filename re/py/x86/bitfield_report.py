"""Every BIT operation on cCreature+0x1F4 -> which bit, which function."""
import sys, collections
from capstone.x86 import *
import scan
from scan import find_disp_refs, gh_func_of as func_of, gh_name, fmt

BITOPS = {"test","and","or","xor","cmp","bt","bts","btr","btc"}

def bits(v):
    v &= 0xFFFFFFFF
    return [1 << i for i in range(32) if v & (1 << i)]

def report(disp):
    refs = find_disp_refs(disp)
    kept = []
    for ins in refs:
        m = [o for o in ins.operands if o.type == X86_OP_MEM and o.mem.disp == disp]
        if not m: continue
        base = ins.reg_name(m[0].mem.base) if m[0].mem.base else None
        if base in (None, "esp"): continue          # stack locals -> noise
        kept.append((ins, base))
    print("== +0x%X : %d non-stack refs ==" % (disp, len(kept)))
    bybit = collections.defaultdict(list)
    print("\n--- bit ops with immediate ---")
    for ins, base in kept:
        if ins.mnemonic not in BITOPS: continue
        imm = [o.imm for o in ins.operands if o.type == X86_OP_IMM]
        f = func_of(ins.address)
        if imm:
            v = imm[0] & 0xFFFFFFFF
            print("  %-52s fn=%08x  bits=%s" % (fmt(ins), f or 0,
                  ",".join("0x%x" % b for b in bits(v if ins.mnemonic!="and" else ~v))))
            for b in bits(v if ins.mnemonic != "and" else ~v):
                bybit[b].append((ins.address, f, ins.mnemonic))
        else:
            print("  %-52s fn=%08x  (reg mask)" % (fmt(ins), f or 0))
    print("\n--- by bit ---")
    for b in sorted(bybit):
        fns = sorted(set("%08x/%s" % (f or 0, gh_name(f) if f else "?") for _, f, _ in bybit[b]))
        print("  0x%-9x n=%-3d fns: %s" % (b, len(bybit[b]), " ".join(fns)))
    print("\n--- functions touching +0x%X (non-stack) ---" % disp)
    byfn = collections.Counter(func_of(i.address) for i, _ in kept)
    for f, n in sorted(byfn.items(), key=lambda kv: -kv[1]):
        print("  %08x  x%-3d %s" % (f or 0, n, gh_name(f) if f else "?"))

if __name__ == "__main__":
    report(int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x1F4)
