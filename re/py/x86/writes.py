import sys, collections
from capstone.x86 import *
import scan
from scan import find_disp_refs, gh_func_of, gh_name, fmt
disp = int(sys.argv[1], 0)
for ins in find_disp_refs(disp):
    m = [o for o in ins.operands if o.type == X86_OP_MEM and o.mem.disp == disp]
    if not m: continue
    base = ins.reg_name(m[0].mem.base) if m[0].mem.base else None
    if base in (None, "esp"): continue
    # writes: destination operand is the memory one
    dst_is_mem = ins.operands[0].type == X86_OP_MEM
    if not dst_is_mem: continue
    if ins.mnemonic in ("test","cmp"): continue
    f = gh_func_of(ins.address)
    print("%-52s fn=%08x %s" % (fmt(ins), f or 0, gh_name(ins.address)))
