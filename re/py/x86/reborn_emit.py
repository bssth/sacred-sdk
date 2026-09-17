"""reborn_emit.py - turn the catalogue into patch records the SDK can apply.

Usage:  python reborn_catalog.py && python reborn_emit.py

Reads  sdk/re/hd_table.gen.json      (from reborn_catalog.py)
       sdk/re/hd_decisions.json      (optional, committed: per-address verdicts, no bytes)
Writes sdk/patchset/records_generated.inc   (gitignored - it holds engine and ReBorn bytes)

Three shapes of patch come out, all as ordinary patchset records:

1. Operand writes. ReBorn's init writes layout values into engine operands at
   run time. We know the resolution when we patch, so the value is baked in:
     mov r32, 1024 -> mov r32, W       (Imm32GeomSet)
     push 501      -> push 501 + offX  (Imm32GeomAdd; Imm16GeomAdd for word adds)

2. Stubs that reduce to operands. Most of ReBorn's stub cave exists only because
   `push [W]` is one byte longer than `push 1024`. When a stub replays exactly
   our instructions with immediates (or constant reads) turned into layout slots,
   we rewrite our instructions in place and need no stub at all.

3. Stubs that really add code. They are copied into the SDK's own cave and
   every outside reference is re-pointed: layout slots to our geometry block,
   engine calls and jumps to our addresses (proven by signature), engine data
   by operand votes over ReBorn's code, IAT slots by import name, read-only
   constants to a pooled copy. Anything that cannot be proven is left out.

Expected bytes always come out of OUR Sacred_decrypted.exe. Sites are grouped
into one record per function of ours, because a half-patched function draws a
torn frame and the patch engine applies a record atomically.
"""
import os, re, sys, json, bisect, struct, collections
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86 as X
import buildmap as bm
import reborn_catalog as rc

HERE = os.path.dirname(os.path.abspath(__file__))
RE_DIR = os.path.normpath(os.path.join(HERE, "..", ".."))
SDK_DIR = os.path.normpath(os.path.join(RE_DIR, ".."))
IN_JSON = os.path.join(RE_DIR, "hd_table.gen.json")
DECISIONS = os.path.join(RE_DIR, "hd_decisions.json")
OUT_INC = os.path.join(SDK_DIR, "patchset", "records_generated.inc")

MD = Cs(CS_ARCH_X86, CS_MODE_32)
MD.detail = True

SLOTS = (0x00A1EF00, 0x00A1F100)
CAVE_LO, CAVE_HI = 0x008E4300, 0x008E5000
TAIL_LO, TAIL_HI = 0x01AC6000, 0x01ADC000
IMAGE = (0x00400000, 0x01E00000)


SLOT_SET = set()   # addresses ReBorn's init actually writes in its table, filled by native_slots()
CONSTANT_IMMS = set()   # ReBorn instructions whose image-range immediate is a number (hd_decisions.json)


def is_slot(v):
    """A layout slot - by the init's own writes, not by address range: ReBorn's
    table shares its neighbourhood with CRT data (0xA1EF80 is a function pointer)."""
    return v in SLOT_SET


NATIVE = {}   # slot VA -> 8 raw bytes at 1024x768, filled by native_slots() from the emulator


def native_slots(reb):
    """Every slot's bytes at 1024x768, straight from running ReBorn's init.

    At the native size every layout value must equal the constant the engine
    was compiled with, so a site whose original operand differs from its slot
    here is a mislocated site or a behaviour change of ReBorn's, and is dropped
    one site at a time. The patch engine repeats the check on our C++ values.
    """
    import reborn_init_emu as emu
    SLOT_SET.clear()
    for size in ((1024, 768), (1920, 1080), (1280, 720)):
        o, _ = emu.run(*size, reb)
        SLOT_SET.update(a for a in o if 0xA1EF40 <= a < 0xA1F0E0)
    out, _ = emu.run(1024, 768, reb)
    out.update(sdk_slots(1024, 768))
    byte = {}
    for a, v in out.items():
        for k, x in enumerate(v):
            byte[a + k] = x
    NATIVE.clear()
    for s in range(SLOTS[0], 0xA1F114):
        NATIVE[s] = bytes(byte[s + k] if s + k in byte else (reb.rd(s + k, 1) or b"\0")[0] for k in range(8))


def native_ok(slot, original):
    """True if `slot` at 1024x768 holds exactly `original` (bytes, 4 or 8 long)."""
    v = NATIVE.get(slot)
    return v is not None and v[:len(original)] == original


def dis(img, va, n):
    b = img.rd(va, n)
    return list(MD.disasm(b, va)) if b else []


def branch_target(ins):
    if (ins.group(X.X86_GRP_JUMP) or ins.group(X.X86_GRP_CALL)) and ins.operands \
            and ins.operands[0].type == X.X86_OP_IMM:
        return ins.operands[0].imm & 0xFFFFFFFF
    return None


def abs_mem(op):
    return op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0


def c_bytes(b):
    return "{ " + ", ".join(f"0x{x:02X}" for x in b) + " }"


def imports(img):
    """IAT slot VA -> 'dll!name' (lower-case dll)."""
    d, pe = img.pe.data, img.pe
    e = struct.unpack_from("<I", d, 0x3C)[0]
    imp_rva = struct.unpack_from("<I", d, e + 24 + 104)[0]
    off, out = pe.va2off(imp_rva + pe.imagebase), {}
    while True:
        ilt, _, _, name, iat = struct.unpack_from("<IIIII", d, off)
        if name == 0:
            break
        dll = d[pe.va2off(name + pe.imagebase):].split(b"\0")[0].decode().lower()
        t, k = pe.va2off((ilt or iat) + pe.imagebase), 0
        while True:
            v = struct.unpack_from("<I", d, t + 4 * k)[0]
            if not v:
                break
            fn = f"#{v & 0xFFFF}" if v & 0x80000000 else \
                d[pe.va2off(v + pe.imagebase) + 2:].split(b"\0")[0].decode()
            out[pe.imagebase + iat + 4 * k] = f"{dll}!{fn}"
            k += 1
        off += 20
    return out


# ---------------------------------------------------------------------------
#  Address mapping, ReBorn -> ours
# ---------------------------------------------------------------------------
class Mapper:
    def __init__(self, ours, reb, dm, wild_spans):
        self.o, self.r, self.dm = ours, reb, dm
        self.iat_r = imports(reb)
        by_name = {v: k for k, v in imports(ours).items()}
        self.iat_map = {k: by_name.get(v) for k, v in self.iat_r.items()}
        self.wild = set()
        for lo, n in wild_spans:
            self.wild.update(range(lo, lo + n))

    def _w(self, va):
        return va in self.wild

    def code(self, va):
        """Our address of ReBorn code at `va` (a call target or a jump target)."""
        for before, after in ((16, 24), (0, 40), (24, 0)):
            got, _ = self.dm.locate_old(va, before=before, after=after, win=0x4000, wild=self._w)
            if got is not None:
                return got
        return None

    def data(self, va):
        """Our address of ReBorn data at `va`: IAT by name, else operand votes."""
        if va in self.iat_r:
            return self.iat_map.get(va)
        votes = collections.Counter()
        key = struct.pack("<I", va)
        pos, seen = self.r.text.find(key), 0
        while pos >= 0 and seen < 16:
            field = self.r.text_lo + pos
            if not self._w(field):
                got, _ = self.dm.locate_old(field, before=12, after=12, skip=4, win=0x4000, wild=self._w)
                if got is not None:
                    votes[struct.unpack("<I", self.o.rd(got, 4))[0]] += 1
            seen += 1
            pos = self.r.text.find(key, pos + 1)
        if votes:
            (best, n), = votes.most_common(1)
            return best if n == sum(votes.values()) else None     # unanimous or nothing
        return self.data_by_content(va)

    def data_by_content(self, va):
        """Initialised data nobody else references: find the same bytes in our .data.

        ReBorn's .data sits 0x1FF8..0x2080 below ours. A 32-byte window around the
        address, with enough non-zero bytes to mean something, must occur at
        exactly one shift in that neighbourhood."""
        if self.r.sec_of(va) not in (".data", ".rdata"):
            return None
        want = self.r.rd(va - 8, 32)
        if not want or len(want) < 32 or sum(1 for b in want if b) < 8:
            return None
        hits = [d for d in range(0x1E00, 0x2200, 4) if self.o.rd(va - 8 + d, 32) == want]
        return va + hits[0] if len(hits) == 1 else None

    def readonly_const(self, va, size):
        """ReBorn bytes of a constant nobody writes (.rdata outside the IAT, code, header, tail)."""
        if va in self.iat_r:
            return None
        sec = self.r.sec_of(va)
        if va < 0x401000:
            b = self.r.pe.data[va - 0x400000:va - 0x400000 + size]
        elif sec in (".rdata", ".text", ".rsrc"):
            b = self.r.rd(va, size)
        else:
            return None
        return b if b and len(b) == size else None


# ---------------------------------------------------------------------------
#  Stubs
# ---------------------------------------------------------------------------
def stub_body(reb, entry, maxlen=384):
    """Instructions of the stub at `entry`, linearly, through its last exit."""
    body, pos, need = [], entry, entry
    while pos < entry + maxlen:
        ins = next(MD.disasm(reb.rd(pos, 16), pos), None)
        if ins is None:
            return None
        body.append(ins)
        pos += ins.size
        t = branch_target(ins)
        if t is not None and ins.address < t < entry + maxlen and t - pos <= 64 and not reb.in_text(t):
            need = max(need, t + 1)                     # forward jump inside the stub
        if pos >= need and (ins.mnemonic == "ret" or
                            (ins.mnemonic == "jmp" and (t is None or reb.in_text(t)))):
            return body
    return None


def reduce_in_place(ours, reb, mp, site, span, our_site, body):
    """("ok", fixups) when our own span can do what the stub does; ("reject", why)
    when it could but the value would change the native 1024x768 picture; None when
    the shapes do not line up and the stub has to be relocated instead."""
    if not body or body[-1].mnemonic != "jmp" or branch_target(body[-1]) != site + span:
        return None
    theirs = body[:-1]
    mine = dis(ours, our_site, span)
    if sum(i.size for i in mine) != span or len(mine) != len(theirs):
        return None
    fx = []
    for a, b in zip(mine, theirs):
        if a.mnemonic != b.mnemonic or len(a.operands) != len(b.operands):
            return None
        for oa, ob in zip(a.operands, b.operands):
            if oa.type == X.X86_OP_REG and ob.type == X.X86_OP_REG:
                if oa.reg != ob.reg:
                    return None
            elif oa.type == X.X86_OP_IMM and ob.type == X.X86_OP_IMM:
                va_, vb = oa.imm & 0xFFFFFFFF, ob.imm & 0xFFFFFFFF
                if va_ != vb and not (branch_target(a) is not None):
                    return None
            elif oa.type == X.X86_OP_IMM and abs_mem(ob) and is_slot(ob.mem.disp & 0xFFFFFFFF):
                if a.imm_size != 4 or ob.size != 4:
                    return None
                slot = ob.mem.disp & 0xFFFFFFFF
                if not native_ok(slot, ours.rd(a.address + a.imm_offset, 4)):
                    return "reject", (f"`{a.mnemonic} {a.op_str}` would become slot {slot:#x}, "
                                      f"which is not this value at 1024x768")
                fx.append((a.address - our_site + a.imm_offset, "Imm32GeomSet", slot))
            elif abs_mem(oa) and abs_mem(ob):
                da, db = oa.mem.disp & 0xFFFFFFFF, ob.mem.disp & 0xFFFFFFFF
                if is_slot(db):
                    if oa.size != ob.size:
                        return None
                    if not native_ok(db, ours.rd(da, oa.size)):
                        return "reject", (f"`{a.mnemonic} {a.op_str}` would read slot {db:#x}, "
                                          f"which is not this constant at 1024x768")
                    fx.append((a.address - our_site + a.disp_offset, "Abs32Geom", db))
                elif ours.rd(da, oa.size) != reb.rd(db, ob.size):
                    return None
            elif oa.type == X.X86_OP_MEM and ob.type == X.X86_OP_MEM:
                ma, mb = oa.mem, ob.mem
                if (ma.base, ma.index, ma.scale, ma.disp, oa.size) != (mb.base, mb.index, mb.scale, mb.disp, ob.size):
                    return None
            else:
                return None
    return ("ok", fx) if fx else None


def relocate(ours, reb, mp, row, body):
    """Stub bytes + fixups for our cave, or (None, reason)."""
    site, span, our_site = row["reborn_va"], row["reborn_span"], row["our_va"]
    entry = body[0].address
    end = body[-1].address + body[-1].size
    blob, fx, stack = bytearray(), [], False
    for ins in body:
        base = len(blob)
        blob += ins.bytes
        t = branch_target(ins)
        if t is not None:
            if entry <= t < end:
                continue                                  # internal: copied along, stays valid
            if not reb.in_text(t):
                return None, f"branches into other ReBorn code at {t:#x}"
            if ins.imm_size != 4:
                return None, f"short branch out of the stub at {ins.address:#x}"
            our = our_site + span if t == site + span else mp.code(t)
            if our is None:
                return None, f"cannot map branch target {t:#x}"
            fx.append((base + ins.imm_offset, "Rel32ToVA", our))
            continue
        for op in ins.operands:
            if abs_mem(op):
                v, off = op.mem.disp & 0xFFFFFFFF, base + ins.disp_offset
                if is_slot(v):
                    fx.append((off, "Abs32Geom", v))
                    continue
                if v in mp.iat_r:
                    our = mp.iat_map.get(v)
                    if our is None:
                        return None, f"import {mp.iat_r[v]} missing in our build"
                    fx.append((off, "Abs32ToVA", our))
                    continue
                c = mp.readonly_const(v, op.size)
                if c is not None and op.size == 4:
                    fx.append((off, "Abs32Const", struct.unpack("<I", c)[0]))
                    continue
                our = mp.data(v)
                if our is None and 0xA1EF40 <= v < 0xA1F0E0:
                    # a scratch cell of ReBorn's own in its table block: nothing in
                    # the engine uses it, so it lives in our block as well
                    fx.append((off, "Abs32Geom", v))
                    continue
                if our is None:
                    return None, f"cannot map data {v:#x} ({reb.sec_of(v)})"
                if c is not None and ours.rd(our, op.size) != c:
                    return None, f"constant {v:#x} differs in our build"
                fx.append((off, "Abs32ToVA", our))
            elif op.type == X.X86_OP_MEM and op.mem.base in (X.X86_REG_ESP, X.X86_REG_EBP):
                stack = True
            elif op.type == X.X86_OP_IMM and IMAGE[0] <= (op.imm & 0xFFFFFFFF) < IMAGE[1]:
                v = op.imm & 0xFFFFFFFF
                our = mp.data(v)
                if our is None:
                    return None, f"cannot map address operand {v:#x}"
                fx.append((base + ins.imm_offset, "Abs32ToVA", our))
    if stack and not row["located_by"].startswith("delta"):
        return None, "stub uses the stack frame, and this function was located by shape (frames may differ)"
    return (bytes(blob), fx), None


# ---------------------------------------------------------------------------
#  Operand writes, from running ReBorn's init at several sizes
# ---------------------------------------------------------------------------
PROBE_SIZES = [(1024, 768), (1366, 768), (1600, 900), (1920, 1080), (2560, 1440), (1280, 1024), (1280, 720)]

# Slots that are ours, not ReBorn's: values its init computes inline on the
# x87 stack and writes straight into operands. Must match hd/geometry.h.
def sdk_slots(W, H):
    rnd = lambda v: int(round(v))                      # nearest-even, like nearbyint
    i = lambda v: struct.pack("<i", v)
    ox, oy = (W - 1024) >> 1, (H - 768) >> 1
    return {0xA1F100: i(rnd(W / 1024.0 * 256.0)),     # kSlotTile256W
            0xA1F104: i(rnd(H / 768.0 * 256.0)),      # kSlotTile256H
            0xA1F108: i(4 * W * H),                   # kSlotPixels4
            0xA1F10C: i(2 * ox),                      # kSlotOffX2
            0xA1F110: i(2 * oy)}                      # kSlotOffY2


def emulated_targets(reb):
    """Every .text address ReBorn's init writes, and the layout value it receives.

    Returns {reborn_va: dict(width, orig, kind, slot)} for matched targets and a
    list of (reborn_va, why) for the rest. `kind` is "set" or "add": the target
    equals a slot, or its original operand plus a slot, at every probe size.
    """
    import reborn_init_emu as emu
    runs, writers = {}, {}
    for r in PROBE_SIZES:
        out, _ = emu.run(*r, reb)
        out.update(sdk_slots(*r))
        runs[r] = out
        writers.update(emu.run.last_writers)
    slots = sorted({a for o in runs.values() for a in o if SLOTS[0] <= a < 0xA1F114})
    texts = sorted({a for o in runs.values() for a in o if reb.in_text(a)})

    def val(r, a, n):
        v = runs[r].get(a)
        return v[:n] if v is not None and len(v) >= n else reb.rd(a, n)

    found, missing = {}, []
    for t in texts:
        w = max(len(runs[r].get(t, b"")) for r in PROBE_SIZES)
        orig = reb.rd(t, w)
        mask = (1 << (8 * w)) - 1
        sets = [s for s in slots if all(val(r, t, w) == val(r, s, w) for r in PROBE_SIZES)]
        adds = [s for s in slots if all(
            val(r, t, w) == ((int.from_bytes(orig, "little", signed=True) +
                              int.from_bytes(val(r, s, 4), "little", signed=True)) & mask).to_bytes(w, "little")
            for r in PROBE_SIZES)]
        wrote_by = writers.get(t, "?")
        if wrote_by == "add" and adds:
            found[t] = dict(width=w, orig=orig, kind="add", slot=adds[0])
        elif sets:
            found[t] = dict(width=w, orig=orig, kind="set", slot=sets[0])
        elif adds:
            found[t] = dict(width=w, orig=orig, kind="add", slot=adds[0])
        else:
            missing.append((t, "value is no layout slot at every probe size (conditional or new formula)"))
    return found, missing


# ---------------------------------------------------------------------------
def align_cluster(ours, reb, dm, lo, hi, wild):
    """Port a run of ReBorn instructions [lo, hi) that reads layout slots but no
    longer lines up byte for byte with ours (push [slot] is a byte longer than
    push imm32, so ReBorn's block grew). The block is found in ours between the
    context before `lo` and the context after `hi`, and ported instruction by
    instruction when every pair is the same operation. Returns (va, len, fixups)
    of our span, or (None, None, why)."""
    pre, _ = dm.locate_old(lo, before=20, after=0, win=0x3000, wild=wild)
    if pre is None:
        return None, None, "block context not found"
    # The context after the block is searched only just past its start, and
    # short: a grown ReBorn function can swallow the padding that follows it.
    post = None
    for n in (16, 12, 8):
        want = reb.rd(hi, n)
        mk = bm.mask_of(want)
        hits = [c for c in range(pre + 1, pre + 97) if bm.masked_eq(ours.rd(c, n), want, mk)]
        if len(hits) == 1:
            post = hits[0]
            break
    if post is None:
        return None, None, "block end not found"
    if not 0 < post - pre <= 96:
        return None, None, f"block context inconsistent ({pre:#x}..{post:#x})"
    mine = dis(ours, pre, post - pre)
    theirs = dis(reb, lo, hi - lo)
    if sum(i.size for i in mine) != post - pre or len(mine) != len(theirs):
        return None, None, f"{len(theirs)} ReBorn instruction(s) against {len(mine)} of ours"
    fx = []
    for a, b in zip(mine, theirs):
        base = a.address - pre
        if a.mnemonic != b.mnemonic or len(a.operands) != len(b.operands):
            return None, None, f"`{a.mnemonic} {a.op_str}` against ReBorn `{b.mnemonic} {b.op_str}`"
        for oa, ob in zip(a.operands, b.operands):
            if abs_mem(ob) and is_slot(ob.mem.disp & 0xFFFFFFFF):
                slot = ob.mem.disp & 0xFFFFFFFF
                if oa.type == X.X86_OP_IMM and a.imm_size == 4 and ob.size == 4:
                    if not native_ok(slot, ours.rd(a.address + a.imm_offset, 4)):
                        return None, None, f"`{a.mnemonic} {a.op_str}`: slot {slot:#x} differs at 1024x768"
                    fx.append((base + a.imm_offset, "Imm32GeomSet", slot))
                elif abs_mem(oa) and oa.size == ob.size:
                    if not native_ok(slot, ours.rd(oa.mem.disp & 0xFFFFFFFF, oa.size)):
                        return None, None, f"`{a.mnemonic} {a.op_str}`: slot {slot:#x} differs at 1024x768"
                    fx.append((base + a.disp_offset, "Abs32Geom", slot))
                else:
                    return None, None, f"`{a.mnemonic} {a.op_str}` cannot take slot {slot:#x}"
            elif oa.type != ob.type:
                return None, None, f"`{a.mnemonic} {a.op_str}` against ReBorn `{b.mnemonic} {b.op_str}`"
            elif oa.type == X.X86_OP_REG and oa.reg != ob.reg:
                return None, None, f"`{a.mnemonic} {a.op_str}` uses other registers than ReBorn"
            elif oa.type == X.X86_OP_IMM and branch_target(a) is None and oa.imm != ob.imm \
                    and not IMAGE[0] <= (oa.imm & 0xFFFFFFFF) < IMAGE[1]:
                return None, None, f"`{a.mnemonic} {a.op_str}`: ReBorn changed the constant"
    if not fx:
        return None, None, "nothing to patch"
    return pre, post - pre, fx


def our_function(ours, va):
    """(entry, instructions) of the Ghidra function of ours holding `va`, decoded
    linearly from its entry - the only instruction boundaries we trust."""
    import scan
    f = scan.gh_func_of(va)
    if f is None:
        return None, []
    return f, dis(ours, f, scan.GH_MAX[f] - f + 1)


STACK_REGS = (X.X86_REG_ESP, X.X86_REG_EBP)


def stack_disps(insns):
    return {(op.mem.base, op.mem.disp) for i in insns for op in i.operands
            if op.type == X.X86_OP_MEM and op.mem.base in STACK_REGS}


def find_rewrite_block(ours, reb, dm, t, branch_wild):
    """The region around `t` that ReBorn rewrote in place, when it kept its length.

    Both ends are instruction boundaries of OUR function, decoded from its entry;
    the ReBorn side is the same offsets at the aligned distance, and its decode
    must land exactly on the block end too. Returns
    ((reb_start, reb_end, our_start), None) or (None, why)."""
    delta = None
    for back in (32, 64, 112, 176, 256):
        a, _ = dm.locate_old(t - back, before=20, after=0, win=0x3000, wild=branch_wild)
        if a is not None:
            delta = a - (t - back)
            break
    if delta is None:
        return None, "no aligned context before it"
    entry, insns = our_function(ours, t + delta)
    if not insns:
        return None, "not inside a known function of ours"
    at = {i.address: k for k, i in enumerate(insns)}
    k0 = max((k for k, i in enumerate(insns) if i.address <= t + delta - back), default=None)
    if k0 is None:
        return None, "context before it lies outside our function"
    start = None
    k = k0
    while k < len(insns) and insns[k].address <= t + delta:
        mine = insns[k]
        r = mine.address - delta
        if not any(branch_wild(r + j) for j in range(mine.size)):
            rb = reb.rd(r, mine.size)
            mb = bytes(mine.bytes)
            if rb is None or not bm.masked_eq(rb, mb, bm.mask_of(mb)):
                start = k
                break
        k += 1
    if start is None:
        return None, "no difference before it"
    for k in range(start + 1, len(insns)):
        y = insns[k].address
        if y <= t + delta:
            continue
        if y - insns[start].address > 192:
            break
        want = ours.rd(y, 12)
        if bm.masked_eq(reb.rd(y - delta, 12), want, bm.mask_of(want)):
            rs, re_ = insns[start].address - delta, y - delta
            if sum(i.size for i in dis(reb, rs, re_ - rs)) != re_ - rs:
                return None, "ReBorn's bytes do not decode to whole instructions over the block"
            return (rs, re_, insns[start].address), None
    return None, "the two builds do not agree again after it"


def port_block(ours, reb, mp, blk, found, is_layout):
    """Fixups that make ReBorn's rewritten bytes [start, end) run at our start."""
    start, end, our = blk
    delta = our - start
    entry, func_insns = our_function(ours, our)
    bounds = {i.address for i in func_insns}
    theirs = dis(reb, start, end - start)
    # ReBorn's code uses ReBorn's frame; if it touches a stack slot our function
    # never uses, the two compilations laid the frame out differently.
    alien = stack_disps(theirs) - stack_disps(func_insns)
    if alien:
        return None, "uses stack slots our function does not have: " + ", ".join(
            f"[{'esp' if b == X.X86_REG_ESP else 'ebp'}{d:+#x}]" for b, d in sorted(alien))
    fx = []
    for ins in theirs:
        base = ins.address - start
        t = branch_target(ins)
        if t is not None:
            if start <= t < end:
                continue                                  # inside: copied along
            if ins.imm_size != 4:
                if t + delta not in bounds:
                    return None, f"short branch to {t:#x} does not land on our instruction"
                continue                                  # same distance, same function
            m = mp.code(t)
            if m is None:
                return None, f"cannot map branch target {t:#x}"
            fx.append((base + ins.imm_offset, "Rel32ToVA", m))
            continue
        for op in ins.operands:
            if abs_mem(op):
                v, off = op.mem.disp & 0xFFFFFFFF, base + ins.disp_offset
                if is_layout(v):
                    fx.append((off, "Abs32Geom", v)); continue
                if v in mp.iat_r:
                    m = mp.iat_map.get(v)
                    if m is None:
                        return None, f"import {mp.iat_r[v]} missing in our build"
                    fx.append((off, "Abs32ToVA", m)); continue
                m = mp.data(v)
                if m is None:
                    c = mp.readonly_const(v, op.size)
                    if c is not None and op.size == 4:
                        fx.append((off, "Abs32Const", struct.unpack("<I", c)[0])); continue
                    return None, f"cannot map data {v:#x}"
                fx.append((off, "Abs32ToVA", m))
            elif op.type == X.X86_OP_IMM and IMAGE[0] <= (op.imm & 0xFFFFFFFF) < IMAGE[1]:
                v = op.imm & 0xFFFFFFFF
                m = mp.data(v)
                if m is None:
                    return None, f"cannot map address operand {v:#x}"
                fx.append((base + ins.imm_offset, "Abs32ToVA", m))
    kinds = {("set", 4): "Imm32GeomSet", ("set", 2): "Imm16GeomSet",
             ("add", 4): "Imm32GeomAdd", ("add", 2): "Imm16GeomAdd"}
    for tt, f in found.items():
        if start <= tt < end:
            fx.append((tt - start, kinds[(f["kind"], f["width"])], f["slot"]))
    return fx, None


def relocate_body(ours, reb, mp, body, map_text):
    """Stub bytes + fixups for `body` (ReBorn instructions), exits through map_text."""
    entry = body[0].address
    end = body[-1].address + body[-1].size
    blob, fx = bytearray(), []
    for ins in body:
        base = len(blob)
        blob += ins.bytes
        t = branch_target(ins)
        if t is not None:
            if entry <= t < end:
                continue
            if not reb.in_text(t):
                return None, f"stub branches into other ReBorn code at {t:#x}"
            if ins.imm_size != 4:
                return None, f"short branch out of the stub at {ins.address:#x}"
            m = map_text(t)
            if m is None:
                return None, f"cannot map stub exit {t:#x}"
            fx.append((base + ins.imm_offset, "Rel32ToVA", m))
            continue
        for op in ins.operands:
            if abs_mem(op):
                v, off = op.mem.disp & 0xFFFFFFFF, base + ins.disp_offset
                if is_slot(v) or (0xA1EF40 <= v < 0xA1F0E0 and mp.data(v) is None):
                    fx.append((off, "Abs32Geom", v)); continue
                if v in mp.iat_r:
                    m = mp.iat_map.get(v)
                    if m is None:
                        return None, f"import {mp.iat_r[v]} missing in our build"
                    fx.append((off, "Abs32ToVA", m)); continue
                c = mp.readonly_const(v, op.size)
                if c is not None and op.size == 4:
                    fx.append((off, "Abs32Const", struct.unpack("<I", c)[0])); continue
                m = mp.data(v)
                if m is None:
                    return None, f"cannot map stub data {v:#x}"
                fx.append((off, "Abs32ToVA", m))
            elif op.type == X.X86_OP_IMM and IMAGE[0] <= (op.imm & 0xFFFFFFFF) < IMAGE[1]:
                m = mp.data(op.imm & 0xFFFFFFFF)
                if m is None:
                    return None, f"cannot map stub address operand {op.imm & 0xFFFFFFFF:#x}"
                fx.append((base + ins.imm_offset, "Abs32ToVA", m))
    return (bytes(blob), fx), None


def port_function(ours, reb, dm, mp, f0, found, is_layout):
    """Replace a whole function of ours with ReBorn's rewrite of it.

    For functions ReBorn changed too much to port piecewise (the camera zoom
    grew by four `push [slot]` bytes). Conditions: ReBorn's version fits in our
    function plus the padding after it; nothing enters our function anywhere but
    its entry (no interior calls, no switch tables); a C++ exception prologue
    keeps our own handler. Every outside reference is re-pointed, addresses
    inside the function map by offset, and stubs it branches to come along.
    Returns (site dict, None) or (None, why)."""
    import scan, xrefs
    entries = sorted(scan.GH_ENTRIES)
    f1 = scan.GH_MAX.get(f0)
    if f1 is None:
        return None, "not a function entry"
    nxt = entries[bisect.bisect_right(entries, f1)]
    room = nxt - f0
    if any(xrefs.DWORDMAP.get(v) for v in range(f0 + 1, f1 + 1)):
        return None, "a table points into the function (switch?)"
    if any(not (f0 <= s <= f1) for v in range(f0 + 1, f1 + 1) for s in xrefs.CALLMAP.get(v, [])):
        return None, "code outside branches into the middle of the function"
    rs, _ = dm.locate_new(f0, before=0, after=24, win=0x3000)
    rn, _ = dm.locate_new(nxt, before=0, after=24, win=0x3000)
    if rs is None or rn is None or rn <= rs:
        return None, "ReBorn's copy of the function not found"
    re_ = rn
    while re_ > rs and reb.rd(re_ - 1, 1) in (b"\x90", b"\xcc"):
        re_ -= 1
    L = re_ - rs
    if L > room:
        return None, f"ReBorn's version is {L} bytes, ours has room for {room}"
    # A layout rewrite changes a function by a few bytes (four `push [slot]` grew
    # the zoom by 4). Much more means ReBorn changed what the function does, and
    # the port would carry that too (FUN_006E7AF0, +93 B, crashed in the menu).
    if L - (f1 + 1 - f0) > 16:
        return None, f"ReBorn's version is {L - (f1 + 1 - f0)} bytes longer - more than a layout rewrite"
    theirs = dis(reb, rs, L)
    if sum(i.size for i in theirs) != L:
        return None, "ReBorn's version does not decode to whole instructions"
    mine_list = dis(ours, f0, f1 + 1 - f0)
    mine_at = {i.address: i for i in mine_list}
    # Same function, same calls in the same order - proven through the address map.
    our_calls = [branch_target(i) for i in mine_list if i.group(X.X86_GRP_CALL) and branch_target(i) is not None]
    their_calls = []
    for i in theirs:
        t = branch_target(i) if i.group(X.X86_GRP_CALL) else None
        if t is None or CAVE_LO <= t < CAVE_HI or TAIL_LO <= t < TAIL_HI:
            continue
        their_calls.append(mp.code(t))
    ours_plain = [t for t in our_calls]
    if their_calls != ours_plain:
        return None, (f"its calls do not match ours ({len(their_calls)} against {len(ours_plain)}, "
                      f"first difference at #{next((k for k, (a, b) in enumerate(zip(their_calls, ours_plain)) if a != b), min(len(their_calls), len(ours_plain)))})")
    inside = lambda t: rs <= t < re_
    to_ours = lambda t: f0 + (t - rs) if inside(t) else mp.code(t)
    fx, stubs = [], []
    for ins in theirs:
        base = ins.address - rs
        t = branch_target(ins)
        if t is not None:
            if inside(t):
                continue
            if ins.imm_size != 4:
                return None, f"short branch out of the function at {ins.address:#x}"
            if CAVE_LO <= t < CAVE_HI or TAIL_LO <= t < TAIL_HI:
                body = stub_body(reb, t)
                if body is None:
                    return None, f"stub at {t:#x} has no clean end"
                got, why = relocate_body(ours, reb, mp, body, to_ours)
                if got is None:
                    return None, why
                stubs.append((len(stubs), got[0], got[1]))
                fx.append((base + ins.imm_offset, "Rel32ToStub", len(stubs) - 1))
                continue
            m = mp.code(t)
            if m is None:
                return None, f"cannot map call/jump target {t:#x}"
            fx.append((base + ins.imm_offset, "Rel32ToVA", m))
            continue
        for op in ins.operands:
            if abs_mem(op):
                v, off = op.mem.disp & 0xFFFFFFFF, base + ins.disp_offset
                if is_layout(v):
                    fx.append((off, "Abs32Geom", v)); continue
                if v in mp.iat_r:
                    m = mp.iat_map.get(v)
                    if m is None:
                        return None, f"import {mp.iat_r[v]} missing in our build"
                    fx.append((off, "Abs32ToVA", m)); continue
                m = mp.data(v)
                if m is None:
                    c = mp.readonly_const(v, op.size)
                    if c is not None and op.size == 4:
                        fx.append((off, "Abs32Const", struct.unpack("<I", c)[0])); continue
                    return None, f"cannot map data {v:#x}"
                fx.append((off, "Abs32ToVA", m))
            elif op.type == X.X86_OP_IMM and IMAGE[0] <= (op.imm & 0xFFFFFFFF) < IMAGE[1]:
                v = op.imm & 0xFFFFFFFF
                if ins.address in CONSTANT_IMMS:
                    continue
                if inside(v):
                    fx.append((base + ins.imm_offset, "Abs32ToVA", f0 + (v - rs))); continue
                mine = mine_at.get(f0 + base)
                if reb.in_text(v) and base < 16 and mine is not None and bytes(mine.bytes[:1]) == bytes(ins.bytes[:1]) \
                        and mine.imm_size == 4:
                    # the C++ exception handler thunk pushed by the prologue: keep ours
                    fx.append((base + ins.imm_offset, "Abs32ToVA", mine.operands[-1].imm & 0xFFFFFFFF)); continue
                m = mp.data(v) if not reb.in_text(v) else mp.code(v)
                if m is None:
                    return None, f"cannot map address operand {v:#x}"
                fx.append((base + ins.imm_offset, "Abs32ToVA", m))
    kinds = {("set", 4): "Imm32GeomSet", ("set", 2): "Imm16GeomSet",
             ("add", 4): "Imm32GeomAdd", ("add", 2): "Imm16GeomAdd"}
    for tt, f in found.items():
        if rs <= tt < re_:
            fx.append((tt - rs, kinds[(f["kind"], f["width"])], f["slot"]))
    new = reb.rd(rs, L) + b"\xcc" * (room - L)
    site = dict(va=f0, len=room, expect=ours.rd(f0, room), new=new, fx=fx, stub=None, whole=stubs,
                note=f"whole function: ReBorn {rs:#x}..{re_:#x} ({L} B) over ours ({room} B incl. padding), "
                     f"{len(stubs)} stub(s)")
    return site, None


def main():
    import scan
    import reborn_catalog as rc
    d = json.load(open(IN_JSON, encoding="utf-8"))
    # hd_decisions.json: {"0x<ReBorn VA>": {"verdict": "skip"|"reject", "why": "..."}}
    #   skip   - not a layout patch (ReBorn's own infrastructure); ignored entirely
    #   reject - a layout patch we will not port; its function stays unpatched
    decisions = {}
    if os.path.exists(DECISIONS):
        for k, v in json.load(open(DECISIONS, encoding="utf-8")).items():
            if not k.startswith("_"):
                decisions[int(k, 0)] = (v["verdict"], v.get("why", "")) if isinstance(v, dict) else (v, "")
    CONSTANT_IMMS.update(a for a, v in decisions.items() if v[0] == "constant")
    ours, reb, dm = bm.load()
    native_slots(reb)
    wild = [(r["reborn_va"], r["reborn_span"]) for r in d["sites"] if r["class"] != "selfpatch"]
    rc.WILD.clear()
    for lo, n in wild:
        rc.WILD.update(range(lo, lo + n))
    mp = Mapper(ours, reb, dm, wild)

    items = collections.defaultdict(list)   # (func, name) -> site dicts
    rejected = []                            # (our va or None, label, why)
    unported = []                            # (ReBorn VA, our func or None) of layout sites left out
    located = {}                             # ReBorn VA -> our func, for every located layout site
    tally = collections.Counter()

    blockable = []                           # (ReBorn VA, label, why) for find_rewrite_block
    def leave_out(reb_va, our_va, label, why, func=None):
        rejected.append((our_va, label, why))
        unported.append((reb_va, func))

    # --- 1. operand writes ----------------------------------------------------
    cat_rows = {r["reborn_target"]: r for r in d["sites"] if r["class"] == "selfpatch"}
    found, missing = emulated_targets(reb)
    for t, why in missing:
        if decisions.get(t, ("",))[0] != "skip":
            leave_out(t, None, f"operand {t:#x}", why)
    FIXKIND = {("set", 4): "Imm32GeomSet", ("set", 2): "Imm16GeomSet",
               ("add", 4): "Imm32GeomAdd", ("add", 2): "Imm16GeomAdd"}
    for t, f in sorted(found.items()):
        label = f"operand {t:#x}"
        verdict = decisions.get(t, ("", ""))
        if verdict[0] == "skip":
            continue
        if verdict[0] == "reject":
            leave_out(t, None, label, f"hd_decisions.json: {verdict[1]}"); continue
        host = rc.host_of(reb, t)
        if host is None:
            leave_out(t, None, label, "no instruction holds this address"); continue
        row = cat_rows.get(t)
        if row and row.get("our_host") and row.get("our_target"):
            our_host = int(row["our_host"].split(":")[0], 16)
        else:
            our_host, _, _ = rc.locate_site(ours, reb, dm, host.address, host.size)
        if our_host is None:
            blockable.append((t, label, "not located")); continue
        func = scan.gh_func_of(our_host)
        name = scan.gh_name(our_host) if func is not None else None
        located[t] = func
        hb = reb.rd(host.address, host.size)
        if not bm.masked_eq(ours.rd(our_host, host.size), hb, bm.mask_of(hb)):
            blockable.append((t, label, "ReBorn changed the host instruction itself")); continue
        ofs = t - host.address
        # The host found by voting over ReBorn's bytes can lose a prefix
        # (`66 C7 44 24 14 imm16` read as a 32-bit mov one byte later). Our
        # function decoded from its entry has the true boundaries: take the
        # instruction of ours that holds the same field.
        _, finsns = our_function(ours, our_host)
        field_va = our_host + ofs
        holder = next((i for i in finsns if i.address <= field_va < i.address + i.size), None)
        if holder is not None and holder.address != our_host:
            our_host, ofs = holder.address, field_va - holder.address
        ins = next(MD.disasm(ours.rd(our_host, 16), our_host), None)
        field_ok = ins is not None and (
            (ins.imm_size and ins.imm_offset == ofs and ins.imm_size >= f["width"]) or
            (ins.disp_size == 4 and ins.disp_offset == ofs and f["width"] <= 4))
        if not field_ok:
            leave_out(t, our_host, label, "the address is not an operand field of our instruction", func); continue
        if ours.rd(our_host + ofs, f["width"]) != f["orig"]:
            leave_out(t, our_host, label, "our operand differs from ReBorn's original", func); continue
        kind = FIXKIND[(f["kind"], f["width"])]
        items[(func, name)].append(dict(
            va=our_host, len=ins.size, expect=ours.rd(our_host, ins.size), new=ours.rd(our_host, ins.size),
            fx=[(ofs, kind, f["slot"])], stub=None,
            note=f"{ins.mnemonic} {ins.op_str}  <- {f['kind']} slot {f['slot']:#x}"))
        tally["operand write"] += 1

    # --- 1b. engine instructions ReBorn pointed at its table in place ----------
    # `fmul [1/1024]` -> `fmul [1/W]`, `cmp ecx, 0x3FF` -> `cmp ecx, [W-1]`: the
    # same length, so ReBorn needed no stub. Ours becomes the same instruction
    # reading our slot (Abs32Geom) or holding the slot's value (Imm32GeomSet).
    # ReBorn's stubs also keep computed layout values in scratch cells of their
    # table block (0xA1EFB0: round(256*H/768), and so on) that engine code then
    # reads. Those count as layout references too, or a function would be
    # ported with a bound that moved and a step that did not. A cell in that
    # block is engine data (CRT pointers live there) only if our own code
    # references its counterpart the same way.
    scratch_memo = {}
    def is_layout(v):
        if is_slot(v):
            return True
        if not 0xA1EF40 <= v < 0xA1F0DC:
            return False
        if v not in scratch_memo:
            scratch_memo[v] = mp.data(v) is None
        return scratch_memo[v]

    refs, seen = [], set()
    T, tlo = reb.text, reb.text_lo
    for i in range(len(T) - 4):
        v = struct.unpack_from("<I", T, i)[0]
        if not 0xA1EF40 <= v < 0xA1F0DC or tlo + i in rc.WILD or not is_layout(v):
            continue
        ins = rc.host_of(reb, tlo + i)
        if ins is None or ins.address in seen:
            continue
        if any(abs_mem(op) and (op.mem.disp & 0xFFFFFFFF) == v for op in ins.operands):
            seen.add(ins.address)
            refs.append(ins)
    for ins in refs:
        rc.WILD.update(range(ins.address, ins.address + ins.size))
    pending = []
    for ins in refs:
        label = f"slot ref {ins.address:#x}"
        verdict = decisions.get(ins.address, ("", ""))
        if verdict[0] == "skip":
            continue
        va, _, how = rc.locate_site(ours, reb, dm, ins.address, ins.size)
        if va is None:
            pending.append(ins); continue
        func = scan.gh_func_of(va)
        name = scan.gh_name(va) if func is not None else None
        located[ins.address] = func
        if verdict[0] == "reject":
            leave_out(ins.address, va, label, f"hd_decisions.json: {verdict[1]}", func); continue
        mine = next(MD.disasm(ours.rd(va, 16), va), None)
        why, fx = None, []
        if mine is None or mine.size != ins.size or len(mine.operands) != len(ins.operands):
            pending.append(ins); continue
        else:
            for oa, ob in zip(mine.operands, ins.operands):
                if abs_mem(ob) and is_layout(ob.mem.disp & 0xFFFFFFFF) and not is_slot(ob.mem.disp & 0xFFFFFFFF):
                    why = f"reads ReBorn scratch cell {ob.mem.disp & 0xFFFFFFFF:#x}, filled by one of its stubs"; break
                if abs_mem(ob) and is_slot(ob.mem.disp & 0xFFFFFFFF):
                    slot = ob.mem.disp & 0xFFFFFFFF
                    if abs_mem(oa) and oa.size == ob.size and mine.mnemonic == ins.mnemonic:
                        if not native_ok(slot, ours.rd(oa.mem.disp & 0xFFFFFFFF, oa.size)):
                            why = f"slot {slot:#x} is not our constant at 1024x768"; break
                        fx.append((mine.disp_offset, "Abs32Geom", slot))
                    elif oa.type == X.X86_OP_IMM and mine.imm_size == 4 and ob.size == 4:
                        if not native_ok(slot, ours.rd(va + mine.imm_offset, 4)):
                            why = f"slot {slot:#x} is not our immediate at 1024x768"; break
                        fx.append((mine.imm_offset, "Imm32GeomSet", slot))
                    else:
                        why = "rewritten"; break
                elif (oa.type, oa.size) != (ob.type, ob.size) or (
                        oa.type == X.X86_OP_REG and oa.reg != ob.reg):
                    why = "rewritten"; break
        if why == "rewritten":
            pending.append(ins); continue
        if why or not fx:
            leave_out(ins.address, va, label, why or "nothing to patch", func); continue
        items[(func, name)].append(dict(
            va=va, len=mine.size, expect=ours.rd(va, mine.size), new=ours.rd(va, mine.size), fx=fx,
            stub=None, note=f"{mine.mnemonic} {mine.op_str}  -> ReBorn `{ins.mnemonic} {ins.op_str}`"))
        tally["slot reference in place"] += 1

    clusters = []
    for ins in sorted(pending, key=lambda i: i.address):
        if clusters and ins.address - (clusters[-1][-1].address + clusters[-1][-1].size) <= 12:
            clusters[-1].append(ins)
        else:
            clusters.append([ins])
    for cl in clusters:
        lo, hi = cl[0].address, cl[-1].address + cl[-1].size
        va, ln, fx = align_cluster(ours, reb, dm, lo, hi, lambda a: a in rc.WILD)
        if va is None:
            for ins in cl:
                blockable.append((ins.address, f"slot ref {ins.address:#x}", f"block {lo:#x}..{hi:#x}: {fx}"))
            continue
        func = scan.gh_func_of(va)
        name = scan.gh_name(va) if func is not None else None
        for ins in cl:
            located[ins.address] = func
        items[(func, name)].append(dict(
            va=va, len=ln, expect=ours.rd(va, ln), new=ours.rd(va, ln), fx=fx, stub=None,
            note="block: " + " ; ".join(f"{i.mnemonic} {i.op_str}" for i in dis(ours, va, ln))))
        tally["slot block aligned"] += len(cl)

    # --- 2 + 3. branches into ReBorn's stubs ----------------------------------
    for r in d["sites"]:
        if r["class"] not in ("cave-jmp", "cave-call", "tail-jmp", "tail-call") or r["scope"] not in ("hd", "hd?"):
            continue
        site, span, our = r["reborn_va"], r["reborn_span"], r["our_va"]
        label = f"{r['class']} {site:#x}"
        verdict = decisions.get(site, ("", ""))
        if verdict[0] == "skip":
            continue
        func = r.get("our_func")
        if our is None and r["class"].endswith("jmp"):
            # ReBorn sometimes left the tail of the instructions it replaced as
            # dead bytes after its jmp instead of NOPs, so the catalogue saw a
            # 5-byte span followed by garbage. The stub's jump back says where
            # the replaced instructions really end.
            exits = [e for e in d["stubs"][hex(r["target"])]["exits"] if isinstance(e, int)]
            back = [e for e in exits if site + span < e <= site + 32]
            if len(back) == 1:
                va, ln, how = rc.locate_site(ours, reb, dm, site, back[0] - site)
                if va is not None:
                    span = back[0] - site
                    r = dict(r, reborn_span=span, our_va=va, our_span=ln, located_by=how,
                             our_func=scan.gh_func_of(va),
                             our_func_name=scan.gh_name(va) if scan.gh_func_of(va) is not None else None)
                    our, func = va, r["our_func"]
        if our is not None:
            located[site] = func
        if our is None:
            leave_out(site, None, label, f"not located ({r['located_by']})"); continue
        if verdict[0] == "reject":
            leave_out(site, our, label, f"hd_decisions.json: {verdict[1]}", func); continue
        mine = dis(ours, our, span)
        if r["our_span"] != span or sum(i.size for i in mine) != span:
            leave_out(site, our, label, "our span does not end on an instruction boundary", func); continue
        body = stub_body(reb, r["target"])
        if body is None:
            leave_out(site, our, label, "stub has no clean end", func); continue
        key = (r["our_func"], r["our_func_name"])
        if r["class"] == "cave-jmp":
            red = reduce_in_place(ours, reb, mp, site, span, our, body)
            if red and red[0] == "reject":
                leave_out(site, our, label, red[1], func); continue
            if red:
                items[key].append(dict(va=our, len=span, expect=ours.rd(our, span), new=ours.rd(our, span),
                                       fx=red[1], stub=None, note=f"in place: " + " ; ".join(
                                           f"{i.mnemonic} {i.op_str}" for i in mine)))
                tally["stub reduced to operands"] += 1
                continue
        got, why = relocate(ours, reb, mp, r, body)
        if got is None:
            leave_out(site, our, label, why, func); continue
        blob, sfx = got
        opcode = 0xE8 if r["class"].endswith("call") else 0xE9
        new = bytes([opcode, 0, 0, 0, 0]) + b"\x90" * (span - 5)
        items[key].append(dict(va=our, len=span, expect=ours.rd(our, span), new=new,
                               fx=[(1, "Rel32ToStub", None)], stub=(blob, sfx),
                               note=f"{'call' if opcode == 0xE8 else 'jmp'} stub ({len(blob)} B): " +
                                    " ; ".join(f"{i.mnemonic} {i.op_str}" for i in mine)))
        tally["stub relocated"] += 1

    # --- blocks ReBorn rewrote in place, same length ----------------------------
    branch_set = set()
    for lo, n in wild:
        branch_set.update(range(lo, lo + n))
    branch_wild = lambda a: a in branch_set
    blocks = {}
    for t, label, why in sorted(blockable):
        if any(b[0] <= t < b[1] for b in blocks):
            blocks[next(b for b in blocks if b[0] <= t < b[1])].append((t, label))
            continue
        blk, err = find_rewrite_block(ours, reb, dm, t, branch_wild)
        if blk is None:
            leave_out(t, None, label, f"{why}; no rewritten block: {err}")
            continue
        blocks.setdefault(blk, []).append((t, label))
    for blk, members in blocks.items():
        fx, err = port_block(ours, reb, mp, blk, found, is_layout)
        start, end, our_start = blk
        if fx is None:
            for t, label in members:
                leave_out(t, None, label, f"block {start:#x}..{end:#x}: {err}")
            continue
        func = scan.gh_func_of(our_start)
        name = scan.gh_name(our_start) if func is not None else None
        for t, _ in members:
            located[t] = func
        n = end - start
        items[(func, name)].append(dict(
            va=our_start, len=n, expect=ours.rd(our_start, n), new=reb.rd(start, n), fx=fx, stub=None,
            block=True, note=f"ReBorn's rewrite of {n} B ({start:#x}): " +
                             " ; ".join(f"{i.mnemonic} {i.op_str}" for i in dis(reb, start, n))))
        tally["rewritten block ported"] += 1

    # --- a function is patched whole or not at all ------------------------------
    # A layout site left out makes its function inconsistent (the crash of
    # 2026-09-16: a loop bound moved to W while its step stayed 256 walked off a
    # 12-entry texture table). An unlocated site has no function of ours, so it
    # is charged to the function of the nearest located site within 0x300 bytes.
    near = sorted(located.items())
    incomplete = {}
    for reb_va, func in unported:
        if func is None:
            best = min(near, key=lambda kv: abs(kv[0] - reb_va), default=None)
            if best and abs(best[0] - reb_va) <= 0x300:
                func = best[1]
        if func is not None:
            incomplete.setdefault(func, []).append(reb_va)
    # An incomplete function gets one more chance as a whole: ReBorn's version of
    # it replaces ours in one site, when the conditions of port_function hold.
    import scan
    names = {k[0]: k[1] for k in items}
    for func in list(incomplete):
        site, why = port_function(ours, reb, dm, mp, func, found, is_layout)
        name = names.get(func) or scan.gh_name(func)
        dropped = sum(len(v) for k, v in items.items() if k[0] == func)
        for k in [k for k in items if k[0] == func]:
            items.pop(k)
        if site is not None:
            items[(func, name)] = [site]
            tally["whole function ported"] += 1
            continue
        rejected.append((None, f"function {name}",
                         f"dropped {dropped} site(s): layout sites at ReBorn " +
                         ", ".join(f"{x:#x}" for x in incomplete[func]) + f" are not ported; as a whole: {why}"))
        tally["sites dropped with their function"] += dropped

    # --- emit ---------------------------------------------------------------
    out, rows = [], []
    for (func, name), sites in sorted(items.items(), key=lambda kv: min(s["va"] for s in kv[1])):
        # a ported block carries everything ReBorn did inside it
        blocks_here = [x for x in sites if x.get("block")]
        sites = [x for x in sites if x.get("block") or not any(
            b["va"] <= x["va"] < b["va"] + b["len"] or x["va"] <= b["va"] < x["va"] + x["len"]
            for b in blocks_here)]
        merged = {}
        for s in sorted(sites, key=lambda s: s["va"]):
            m = merged.get(s["va"])
            if m is None:
                merged[s["va"]] = dict(s, fx=list(s["fx"]))
            elif m["stub"] is None and s["stub"] is None and m["len"] == s["len"]:
                m["fx"] += s["fx"]
            else:
                rejected.append((s["va"], "merge", "two different patches at one address"))
        clean, last = [], -1
        for va in sorted(merged):
            if va < last:
                rejected.append((va, "merge", "overlaps the previous site")); continue
            clean.append(merged[va]); last = va + merged[va]["len"]
        if not clean:
            continue
        ident = name if name and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) else f"at_{clean[0]['va']:08x}"
        tag = f"hd_{ident.lower()}"
        nstubs = sum(1 for s in clean if s["stub"])
        out.append(f"// --- {tag}: {len(clean)} site(s), {nstubs} stub(s)")
        stub_rows, sid = [], 0
        for i, s in enumerate(clean):
            out.append(f"// {s['va']:#010x}  {s['note']}")
            out.append(f"static const uint8_t {tag}_e{i}[] = {c_bytes(s['expect'])};")
            out.append(f"static const uint8_t {tag}_n{i}[] = {c_bytes(s['new'])};")
            if s.get("whole"):
                remap = {}
                for lid, blob, sfx in s["whole"]:
                    remap[lid] = sid
                    out.append(f"static const uint8_t {tag}_s{sid}[] = {c_bytes(blob)};")
                    if sfx:
                        out.append(f"static const Fixup {tag}_sf{sid}[] = {{ " + ", ".join(
                            f"{{ {o}, Fix::{k}, 0x{a:08X}u }}" for o, k, a in sfx) + " };")
                    stub_rows.append(f"    {{ {sid}, {len(blob)}, {tag}_s{sid}, "
                                     f"{f'{tag}_sf{sid}' if sfx else 'nullptr'}, {len(sfx)}, {i} }},")
                    sid += 1
                s["fx"] = [(o, k, remap[a]) if k == "Rel32ToStub" else (o, k, a) for o, k, a in s["fx"]]
            if s["stub"]:
                s["fx"] = [(1, "Rel32ToStub", sid)]
                blob, sfx = s["stub"]
                out.append(f"static const uint8_t {tag}_s{sid}[] = {c_bytes(blob)};")
                if sfx:
                    out.append(f"static const Fixup {tag}_sf{sid}[] = {{ " + ", ".join(
                        f"{{ {o}, Fix::{k}, 0x{a:08X}u }}" for o, k, a in sfx) + " };")
                stub_rows.append(f"    {{ {sid}, {len(blob)}, {tag}_s{sid}, "
                                 f"{f'{tag}_sf{sid}' if sfx else 'nullptr'}, {len(sfx)}, {i} }},")
                sid += 1
            if s["fx"]:
                out.append(f"static const Fixup {tag}_f{i}[] = {{ " + ", ".join(
                    f"{{ {o}, Fix::{k}, 0x{a:08X}u }}" for o, k, a in s["fx"]) + " };")
        out.append(f"static const Site {tag}_sites[] = {{")
        for i, s in enumerate(clean):
            fxr = f"{tag}_f{i}" if s["fx"] else "nullptr"
            out.append(f"    {{ 0x{s['va']:08X}u, {s['len']}, {tag}_e{i}, {tag}_n{i}, {fxr}, {len(s['fx'])} }},")
        out.append("};")
        if stub_rows:
            out.append(f"static const Stub {tag}_stubs[] = {{")
            out += stub_rows
            out.append("};")
        out.append("")
        stubs_ref = f"{tag}_stubs, {len(stub_rows)}" if stub_rows else "nullptr, 0"
        rows.append(f'{{ "{tag}", "{name or hex(clean[0]["va"])} layout", "hd",\n'
                    f'  "OverLookers (ReBorn), HD - relocated by re/py/x86/reborn_emit.py",\n'
                    f'  nullptr, false, nullptr, 0, {stubs_ref}, {tag}_sites, {len(clean)} }},')

    # `--out FILE` writes somewhere else, so a table can be inspected without
    # touching the one the next build of the DLL picks up.
    out_path = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else OUT_INC
    print(f"writing {out_path}")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// GENERATED by re/py/x86/reborn_emit.py - do not edit, do not commit.\n"
                "// Expected bytes come from our own Sacred_decrypted.exe; layout values from\n"
                "// hd/geometry.cpp at apply time; stub bytes from the user's SacredReborn.exe.\n"
                "// See .claude/knowledge/re/reborn_hd_port.md.\n\n")
        f.write("#ifdef SDK_PATCH_DATA\n\n" + "\n".join(out) + "\n#endif // SDK_PATCH_DATA\n\n")
        f.write("#ifdef SDK_PATCH_ROWS\n\n" + "\n\n".join(rows) + "\n\n#endif // SDK_PATCH_ROWS\n")

    print(f"records: {len(rows)}  " + ", ".join(f"{k}: {v}" for k, v in tally.items()))
    print(f"left out: {len(rejected)}")
    for va, what, why in rejected:
        print(f"   {va and hex(va) or '?':>10}  {what:<22} {why}")


if __name__ == "__main__":
    main()
