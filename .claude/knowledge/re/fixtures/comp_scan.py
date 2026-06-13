import struct, capstone
from comp_dis import D, SECS, va2off, off2va

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True

# linear sweep of .text, collect instructions that touch disp 0x251, 0x249, 0x1f4(write 4 bit), 0x1ec
TARGETS = {0x251, 0x249, 0x1f4, 0x1ec, 0x200}

def sweep():
    for name,sva,vsz,ptr,rsz in SECS:
        if name != '.text': continue
        base_va = 0x400000 + sva
        code = D[ptr:ptr+rsz]
        for ins in MD.disasm(code, base_va):
            txt = ins.op_str
            for t in TARGETS:
                hx = "0x%x" % t
                if hx in txt:
                    # only memory operands with that displacement
                    yield (ins.address, ins.mnemonic, txt, t)
                    break

if __name__ == '__main__':
    import sys
    want = int(sys.argv[1],16) if len(sys.argv)>1 else None
    mnem_filter = sys.argv[2] if len(sys.argv)>2 else None
    for addr,mn,op,t in sweep():
        if want is not None and t != want: continue
        if mnem_filter and mn != mnem_filter: continue
        print("%08x  %-6s %s   ; +0x%x" % (addr, mn, op, t))
