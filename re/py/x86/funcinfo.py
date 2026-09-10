"""Profile a function: size, referenced strings, called functions, imm constants."""
import sys, struct, bisect, collections
from capstone.x86 import *
import scan
from scan import P, TEXT_VA, TEXT, MD, FUNCS, func_of, string_at, in_text, fmt

def func_range(va, cap=0x6000):
    i = bisect.bisect_right(FUNCS, va) - 1
    start = FUNCS[i] if i >= 0 else va
    nxt = FUNCS[i+1] if i+1 < len(FUNCS) else TEXT_VA + len(TEXT)
    # extend past "next start" if the code clearly continues (no ret/jmp before it)
    end = min(nxt, start + cap)
    return start, end

def walk(start, end):
    off = start - TEXT_VA
    return list(MD.disasm(TEXT[off:end - TEXT_VA], start))

def profile(va, cap=0x6000, quiet=False):
    start, end = func_range(va, cap)
    ins = walk(start, end)
    strs, calls, imms = [], collections.Counter(), collections.Counter()
    for i in ins:
        for op in i.operands:
            if op.type == X86_OP_IMM:
                v = op.imm & 0xFFFFFFFF
                if i.mnemonic in ("call","jmp") or i.mnemonic.startswith("j"):
                    if i.mnemonic == "call" and in_text(v): calls[v] += 1
                    continue
                s = string_at(v)
                if s and len(s) >= 3: strs.append((i.address, v, s))
                else: imms[v] += 1
            if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.disp:
                s = string_at(op.mem.disp & 0xFFFFFFFF)
                if s and len(s) >= 3: strs.append((i.address, op.mem.disp & 0xFFFFFFFF, s))
    if not quiet:
        print("=== fn %08x  (range %08x..%08x, %d insns) ===" % (va, start, end, len(ins)))
        if strs:
            print("-- strings --")
            for a, v, s in strs: print("   %08x -> %08x  %r" % (a, v, s[:110]))
        if calls:
            print("-- calls --")
            print("   " + " ".join("%08x(x%d)" % (k, n) for k, n in calls.most_common(40)))
    return start, end, strs, calls, imms

if __name__ == "__main__":
    for a in sys.argv[1:]:
        profile(int(a, 0))
        print()
