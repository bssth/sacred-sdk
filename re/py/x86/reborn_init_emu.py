"""reborn_init_emu.py - run ReBorn's HD init on paper for a given W x H.

ReBorn computes its layout numbers in two short routines in its .rsrc tail: the
width half starts at 0x1AC853D with eax = W, the height half at 0x1AC953D with
eax = H. They are plain integer and x87 arithmetic over constants, so a tiny
emulator runs them exactly. We never ship their code: this exists to
  * list every place the init writes to (engine operands and layout slots),
  * prove that sdk/hd/geometry.cpp computes the same numbers, at any size,
  * tell the emitter which layout value each engine operand receives.

Usage:  python reborn_init_emu.py 1920 1080     (prints what gets written)
"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86 as X
import buildmap as bm

ENTRY_W = 0x01AC853D
ENTRY_H = 0x01AC953D

MD = Cs(CS_ARCH_X86, CS_MODE_32)
MD.detail = True

REG32 = {X.X86_REG_EAX: "eax", X.X86_REG_EBX: "ebx", X.X86_REG_ECX: "ecx", X.X86_REG_EDX: "edx",
         X.X86_REG_ESI: "esi", X.X86_REG_EDI: "edi", X.X86_REG_EBP: "ebp"}
REG16 = {X.X86_REG_AX: "eax", X.X86_REG_BX: "ebx", X.X86_REG_CX: "ecx", X.X86_REG_DX: "edx"}
REG8 = {X.X86_REG_AL: "eax", X.X86_REG_BL: "ebx", X.X86_REG_CL: "ecx", X.X86_REG_DL: "edx"}


class Stop(Exception):
    pass


def x87_round(v):
    """Round to nearest, ties to even - the x87 default."""
    return int(round(v))


class Emu:
    def __init__(self, reb):
        self.reb = reb
        self.mem = {}            # overlay: address -> byte
        self.reg = dict.fromkeys(("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp"), 0)
        self.st = []             # x87 stack, top = st[-1]
        self.flags = (0, 0)      # (a, b) of the last cmp
        self.writes = {}         # address -> size, for every memory write
        self.writer = {}         # address -> mnemonic of the last instruction writing it
        self.cur = None

    # --- memory -----------------------------------------------------------
    def rd(self, va, n):
        out = bytearray()
        for k in range(n):
            a = va + k
            if a in self.mem:
                out.append(self.mem[a])
            else:
                if a < 0x401000:
                    b = self.reb.pe.data[a - 0x400000:a - 0x400000 + 1]
                else:
                    b = self.reb.rd(a, 1)
                out.append(b[0] if b else 0)
        return bytes(out)

    def wr(self, va, data):
        for k, b in enumerate(data):
            self.mem[va + k] = b
        prev = self.writes.get(va, 0)
        self.writes[va] = max(prev, len(data))
        if self.cur is not None:
            self.writer[va] = self.cur.mnemonic

    def ea(self, op):
        m = op.mem
        if m.segment != 0 or m.base == X.X86_REG_ESP:
            raise Stop("stack or segment reference")
        a = m.disp & 0xFFFFFFFF
        if m.base:
            a += self.reg[REG32[m.base]]
        if m.index:
            a += self.reg[REG32[m.index]] * m.scale
        return a & 0xFFFFFFFF

    def get(self, op):
        if op.type == X.X86_OP_IMM:
            return op.imm & ((1 << (8 * op.size)) - 1)
        if op.type == X.X86_OP_REG:
            if op.reg in REG32: return self.reg[REG32[op.reg]]
            if op.reg in REG16: return self.reg[REG16[op.reg]] & 0xFFFF
            if op.reg in REG8: return self.reg[REG8[op.reg]] & 0xFF
            raise Stop(f"register {op.reg}")
        return int.from_bytes(self.rd(self.ea(op), op.size), "little")

    def put(self, op, v):
        if op.type == X.X86_OP_REG:
            if op.reg in REG32:
                self.reg[REG32[op.reg]] = v & 0xFFFFFFFF
            elif op.reg in REG16:
                r = REG16[op.reg]; self.reg[r] = (self.reg[r] & ~0xFFFF | (v & 0xFFFF)) & 0xFFFFFFFF
            elif op.reg in REG8:
                r = REG8[op.reg]; self.reg[r] = (self.reg[r] & ~0xFF | (v & 0xFF)) & 0xFFFFFFFF
            else:
                raise Stop(f"register {op.reg}")
            return
        self.wr(self.ea(op), (v & ((1 << (8 * op.size)) - 1)).to_bytes(op.size, "little"))

    # --- x87 helpers --------------------------------------------------------
    def fget(self, op):
        b = self.rd(self.ea(op), op.size)
        return struct.unpack("<d" if op.size == 8 else "<f", b)[0]

    def fput(self, op, v):
        fmt = "<d" if op.size == 8 else "<f"
        self.wr(self.ea(op), struct.pack(fmt, v))

    # --- run ------------------------------------------------------------------
    def run(self, entry, limit=4000):
        pc = entry
        for _ in range(limit):
            ins = next(MD.disasm(self.reb.rd(pc, 16), pc), None)
            if ins is None:
                return pc, "undecodable"
            nxt = pc + ins.size
            mn, ops = ins.mnemonic, ins.operands
            self.cur = ins
            try:
                if mn in ("call", "ret", "push", "pop") or ins.group(X.X86_GRP_CALL):
                    return pc, mn
                if mn == "mov":
                    self.put(ops[0], self.get(ops[1]))
                elif mn in ("add", "sub", "and", "or", "xor"):
                    a, b = self.get(ops[0]), self.get(ops[1])
                    if ops[1].size < ops[0].size and ops[1].type == X.X86_OP_IMM:
                        b = ops[1].imm & ((1 << (8 * ops[0].size)) - 1)
                    r = {"add": a + b, "sub": a - b, "and": a & b, "or": a | b, "xor": a ^ b}[mn]
                    self.put(ops[0], r)
                    self.flags = (a, b)
                elif mn == "cmp":
                    b = self.get(ops[1]) if ops[1].type != X.X86_OP_IMM else ops[1].imm & 0xFFFFFFFF
                    self.flags = (self.get(ops[0]), b)
                elif mn == "shr":
                    self.put(ops[0], self.get(ops[0]) >> self.get(ops[1]) if len(ops) > 1 else self.get(ops[0]) >> 1)
                elif mn == "sar":
                    v = self.get(ops[0]); n = self.get(ops[1]) if len(ops) > 1 else 1
                    if v & 0x80000000: v -= 1 << 32
                    self.put(ops[0], v >> n)
                elif mn == "not":
                    self.put(ops[0], ~self.get(ops[0]))
                elif mn == "neg":
                    self.put(ops[0], -self.get(ops[0]))
                elif mn in ("inc", "dec"):
                    self.put(ops[0], self.get(ops[0]) + (1 if mn == "inc" else -1))
                elif mn == "imul" and len(ops) == 3:
                    a = self.get(ops[1]); a -= (1 << 32) if a & 0x80000000 else 0
                    self.put(ops[0], a * ops[2].imm)
                elif mn == "lea":
                    self.put(ops[0], self.ea(ops[1]))
                elif mn in ("jmp", "jl", "jge", "jae", "jb", "je", "jne", "jle", "jg", "jbe", "ja"):
                    a, b = self.flags
                    sa = a - (1 << 32) if a & 0x80000000 else a
                    sb = b - (1 << 32) if b & 0x80000000 else b
                    take = {"jmp": True, "jl": sa < sb, "jge": sa >= sb, "jle": sa <= sb, "jg": sa > sb,
                            "jae": a >= b, "jb": a < b, "jbe": a <= b, "ja": a > b,
                            "je": a == b, "jne": a != b}[mn]
                    if take:
                        nxt = ops[0].imm & 0xFFFFFFFF
                elif mn == "fld":
                    self.st.append(self.fget(ops[0]))
                elif mn == "fild":
                    v = int.from_bytes(self.rd(self.ea(ops[0]), ops[0].size), "little", signed=True)
                    self.st.append(float(v))
                elif mn in ("fst", "fstp"):
                    self.fput(ops[0], self.st[-1])
                    if mn == "fstp": self.st.pop()
                elif mn in ("fist", "fistp"):
                    v = x87_round(self.st[-1])
                    self.wr(self.ea(ops[0]), (v & ((1 << (8 * ops[0].size)) - 1)).to_bytes(ops[0].size, "little"))
                    if mn == "fistp": self.st.pop()
                elif mn in ("fld1", "fldz"):
                    self.st.append(1.0 if mn == "fld1" else 0.0)
                elif mn == "fchs":
                    self.st[-1] = -self.st[-1]
                elif mn == "fpatan":
                    x = self.st.pop()
                    self.st[-1] = __import__("math").atan2(self.st[-1], x)
                elif mn in ("fadd", "fsub", "fmul", "fdiv") and ops and ops[0].type == X.X86_OP_REG \
                        and len(ops) == 1:
                    i = ops[0].reg - X.X86_REG_ST0
                    a, b = self.st[-1], self.st[-1 - i]
                    self.st[-1] = {"fadd": lambda: a + b, "fsub": lambda: a - b,
                                   "fmul": lambda: a * b, "fdiv": lambda: a / b}[mn]()
                elif mn in ("fadd", "fsub", "fmul", "fdiv", "fsubr", "fdivr",
                            "fiadd", "fisub", "fimul", "fidiv") and ops and ops[0].type == X.X86_OP_MEM:
                    if mn.startswith("fi"):
                        b = float(int.from_bytes(self.rd(self.ea(ops[0]), ops[0].size), "little", signed=True))
                        mn = "f" + mn[2:]
                    else:
                        b = self.fget(ops[0])
                    a = self.st[-1]
                    self.st[-1] = {"fadd": lambda: a + b, "fsub": lambda: a - b, "fmul": lambda: a * b,
                                   "fdiv": lambda: a / b, "fsubr": lambda: b - a, "fdivr": lambda: b / a}[mn]()
                else:
                    return pc, f"unsupported `{mn} {ins.op_str}`"
            except Stop as e:
                return pc, str(e)
            pc = nxt
        return pc, "step limit"


def run(W, H, reb=None):
    """Every address the init writes, with the bytes it ends up holding."""
    reb = reb or bm.Image(bm.REBORN)
    e = Emu(reb)
    e.reg["eax"] = W
    stop_w = e.run(ENTRY_W)
    e.reg["eax"] = H
    stop_h = e.run(ENTRY_H)
    out = {a: e.rd(a, n) for a, n in e.writes.items()}
    run.last_writers = e.writer
    return out, (stop_w, stop_h)


if __name__ == "__main__":
    W = int(sys.argv[1]) if len(sys.argv) > 1 else 1920
    H = int(sys.argv[2]) if len(sys.argv) > 2 else 1080
    reb = bm.Image(bm.REBORN)
    out, stops = run(W, H, reb)
    print(f"stopped at {stops[0][0]:#x} ({stops[0][1]}) and {stops[1][0]:#x} ({stops[1][1]})")
    text = {a: v for a, v in out.items() if reb.in_text(a)}
    print(f"{len(out)} addresses written, {len(text)} of them in .text")
    for a in sorted(out):
        v = out[a]
        iv = int.from_bytes(v, "little", signed=True)
        fv = struct.unpack("<f", v)[0] if len(v) == 4 else None
        print(f"  {a:#010x} {'text' if reb.in_text(a) else reb.sec_of(a):>6} {len(v)}B  int={iv}"
              + (f"  float={fv:g}" if fv is not None else ""))
