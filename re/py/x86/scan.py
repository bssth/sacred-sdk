"""Reusable .text scanning infrastructure for Sacred_decrypted.exe.

  find_disp_refs(disp)  -> every instruction whose memory operand uses `disp`
  func_starts()         -> function entry VAs (call-targets + prologue heuristic)
  func_of(va)           -> enclosing function start
  string_at(va)         -> ASCIIZ if va points at printable string data
  imm_refs()            -> (va, imm) for every push imm32 / mov r32,imm32
"""
import struct, sys, re, bisect, functools, pickle, os
from capstone import *
from capstone.x86 import *
from pe import PE, md

P = PE()
TEXT_VA, TEXT = P.text()
TEXT_END = TEXT_VA + len(TEXT)
MD = md()
CACHE = os.path.join(os.path.dirname(__file__), "_cache")
os.makedirs(CACHE, exist_ok=True)

def in_text(va): return TEXT_VA <= va < TEXT_END

def _cached(name, fn):
    p = os.path.join(CACHE, name + ".pkl")
    if os.path.exists(p):
        with open(p, "rb") as f: return pickle.load(f)
    v = fn()
    with open(p, "wb") as f: pickle.dump(v, f)
    return v

# ---------- function starts ----------
def _func_starts():
    starts = set()
    # 1. direct call rel32 targets  (E8 rel32)
    for i in range(len(TEXT) - 5):
        if TEXT[i] == 0xE8:
            rel = struct.unpack_from("<i", TEXT, i+1)[0]
            tgt = TEXT_VA + i + 5 + rel
            if in_text(tgt): starts.add(tgt)
    # 2. classic prologues preceded by int3/nop padding or ret
    for i in range(1, len(TEXT) - 4):
        if TEXT[i] == 0x55 and TEXT[i+1] == 0x8B and TEXT[i+2] == 0xEC:   # push ebp; mov ebp,esp
            prev = TEXT[i-1]
            if prev in (0xCC, 0x90, 0xC3) or (i >= 3 and TEXT[i-3:i] == b"\xC2"):
                starts.add(TEXT_VA + i)
    return sorted(starts)

FUNCS = _cached("funcs", _func_starts)


# ---------- Ghidra function table (authoritative) ----------
GH_CSV = "E:/SteamLibrary/steamapps/common/Sacred Gold/sdk/re/ghidra/functions.csv"
GH_ENTRIES, GH_NAME, GH_MAX = [], {}, {}
try:
    with open(GH_CSV, encoding="utf-8") as f:
        next(f)
        for line in f:
            parts = line.rstrip().split(",")
            if len(parts) < 5: continue
            e = int(parts[0], 16); mx = int(parts[2], 16)
            GH_ENTRIES.append(e); GH_NAME[e] = parts[4]; GH_MAX[e] = mx
    GH_ENTRIES.sort()
except FileNotFoundError:
    pass

def gh_func_of(va, back=64):
    """Enclosing Ghidra function entry, or None.

    Walks back several entries: Ghidra's list can contain small functions nested
    inside (or overlapping) much larger ones, so the nearest preceding entry is
    not always the containing one. Prefers the smallest container."""
    i = bisect.bisect_right(GH_ENTRIES, va) - 1
    best = None
    while i >= 0 and back > 0:
        e = GH_ENTRIES[i]
        if va <= GH_MAX[e]:
            if best is None or (GH_MAX[e] - e) < (GH_MAX[best] - best): best = e
        i -= 1; back -= 1
    return best

def gh_name(va):
    e = gh_func_of(va)
    return GH_NAME.get(e, "?") if e is not None else "?"

def fname(va):
    """Pretty 'name@entry' for any VA."""
    e = gh_func_of(va)
    if e is None: return "%08x(nofn)" % va
    return "%s" % GH_NAME[e]

def func_of(va):
    i = bisect.bisect_right(FUNCS, va) - 1
    return FUNCS[i] if i >= 0 else None

# ---------- displacement refs ----------
def _decode_at(off, count=1):
    return list(MD.disasm(TEXT[off:off+16], TEXT_VA + off, count=count))

def find_disp_refs(disp, maxback=14):
    """Find instructions with a memory operand whose displacement == disp."""
    pat = struct.pack("<i", disp)
    out = []
    seen = set()
    start = 0
    while True:
        i = TEXT.find(pat, start)
        if i < 0: break
        start = i + 1
        # try to find an instruction that ENDS at i+4 (disp32 is last field)
        # or has disp32 followed by an immediate (imm8/imm16/imm32)
        for back in range(2, maxback + 1):
            off = i - back
            if off < 0: continue
            ins = _decode_at(off)
            if not ins: continue
            ins = ins[0]
            if ins.address + ins.size < TEXT_VA + i + 4: continue
            if ins.address + ins.size > TEXT_VA + i + 4 + 4: continue
            ok = False
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.disp == disp:
                    ok = True
            if ok and ins.address not in seen:
                seen.add(ins.address)
                out.append(ins)
                break
    out.sort(key=lambda x: x.address)
    return out

def fmt(ins):
    return "%08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str)

# ---------- data / strings ----------
def string_at(va, maxlen=200):
    o = P.va2off(va)
    if o is None: return None
    b = P.data[o:o+maxlen]
    e = b.find(b"\0")
    if e <= 0: return None
    s = b[:e]
    if not all(32 <= c < 127 or c in (9,10,13) for c in s): return None
    return s.decode("latin1")

if __name__ == "__main__":
    print("funcs:", len(FUNCS), "text:", hex(TEXT_VA), "-", hex(TEXT_END))
    disp = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x1F4
    refs = find_disp_refs(disp)
    print("refs to +0x%x: %d" % (disp, len(refs)))
    for ins in refs:
        print("  %s   [fn %08x]" % (fmt(ins), func_of(ins.address) or 0))
