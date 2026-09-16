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
import os, json, struct, collections
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

SLOTS = (0x00A1EF00, 0x00A1F100)
IMAGE = (0x00400000, 0x01E00000)
OPWRITE_FIX = {("mov", 4): "Imm32GeomSet", ("add", 4): "Imm32GeomAdd", ("add", 2): "Imm16GeomAdd",
               ("mov", 2): "Imm16GeomSet"}


def is_slot(v):
    return SLOTS[0] <= v < SLOTS[1]


def slots_at_native():
    """Slot address -> raw bytes at 1024x768, mirroring hd/geometry.cpp.

    Only a filter: at the native size every slot must equal the constant the
    engine was compiled with, so a site whose original operand differs is a
    mislocated site or a behaviour change of ReBorn's, and is dropped here one
    site at a time. The patch engine repeats the check on the real values and
    refuses a whole record if this mirror ever drifts from the C++.
    """
    W, H = 1024, 768
    hw, hh, ox, oy = W >> 1, H >> 1, 0, 0
    f = lambda v: struct.pack("<f", v)
    i = lambda v: struct.pack("<i", v)
    dbl = lambda v: struct.pack("<d", v)
    sx, sy = 267.0 * W / 1024.0, 200.0 * H / 768.0
    t = {
        0xA1EFD0: i(W), 0xA1EFD4: i(H), 0xA1EFD8: i(hw), 0xA1EFDC: i(hh),
        0xA1F020: i(-hw), 0xA1F024: i(-hh), 0xA1F078: i(-W), 0xA1F0C0: i(-H),
        0xA1EFE0: f(W), 0xA1EFE4: f(H), 0xA1EFE8: f(hw), 0xA1EFEC: f(hh),
        0xA1EFF0: f(-hw), 0xA1EFF4: f(-hh),
        0xA1EFF8: f(sx), 0xA1EFFC: f(-sx), 0xA1F000: f(sy), 0xA1F004: f(-sy),
        0xA1F010: dbl(sx), 0xA1F018: dbl(-sx), 0xA1F0A0: dbl(sy), 0xA1F0A8: dbl(-sy),
        0xA1F008: f(1.0 / W), 0xA1F0B0: f(1.0 / H), 0xA1EFC4: f(1.0), 0xA1EFC8: f(1.0),
        0xA1F00C: i(W + 200), 0xA1F0CC: i(H + 200),
        0xA1F054: i(ox), 0xA1F050: f(ox), 0xA1F0BC: i(oy), 0xA1F0B8: f(oy),
        0xA1F05C: f(501.0), 0xA1F060: f(522.0), 0xA1F058: i(162), 0xA1F064: i(170),
        0xA1F080: f(1024.0), 0xA1F07C: f(W - 70.0), 0xA1F070: i(W - 1), 0xA1F074: i(H - 1),
        0xA1F068: i(W - 92), 0xA1F06C: i(W - 126), 0xA1F098: f(H + 50.0),
        0xA1EFA8: i(105), 0xA1EFAC: i(700), 0xA1F0C4: f(379.0), 0xA1F0C8: f(389.0),
        0xA1EF68: f(hw + 1.0), 0xA1EF60: f(H - 58.0), 0xA1EF64: f(H - 57.0),
        0xA1EF50: i(W - 32), 0xA1EF54: i(H - 32), 0xA1EF58: i(400), 0xA1EF5C: i(680),
        0xA1F0D4: f(710.0), 0xA1EF44: f(0.5), 0xA1EF48: f(1.0), 0xA1EF4C: f(2.0),
    }
    return t


NATIVE = slots_at_native()


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
        if not votes:
            return None
        (best, n), = votes.most_common(1)
        return best if n == sum(votes.values()) else None     # unanimous or nothing

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
def main():
    d = json.load(open(IN_JSON, encoding="utf-8"))
    decisions = {}
    if os.path.exists(DECISIONS):
        decisions = {int(k, 0): v for k, v in json.load(open(DECISIONS, encoding="utf-8")).items()
                     if not k.startswith("_")}
    ours, reb, dm = bm.load()
    wild = [(r["reborn_va"], r["reborn_span"]) for r in d["sites"] if r["class"] != "selfpatch"]
    mp = Mapper(ours, reb, dm, wild)

    items = collections.defaultdict(list)   # (func, name) -> site dicts
    rejected = []
    tally = collections.Counter()

    # --- 1. operand writes ----------------------------------------------------
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
            rejected.append((tgt, "operand write", why)); continue
        host_va = int(host_s.split(":")[0], 16)
        ins = next(MD.disasm(ours.rd(host_va, 16), host_va), None)
        kind = OPWRITE_FIX.get((r["writer_op"], r["writer_width"]))
        ofs = None
        if ins:
            for op in ins.operands:
                if op.type == X.X86_OP_IMM and host_va + ins.imm_offset == tgt:
                    ofs = ins.imm_offset
                elif abs_mem(op) and host_va + ins.disp_offset == tgt:
                    ofs = ins.disp_offset
        if ofs is None or kind in (None, "Imm16GeomSet"):
            rejected.append((tgt, "operand write", f"not a patchable field of `{host_s}` "
                                                   f"({r['writer_op']}/{r['writer_width']})"))
            continue
        width = 2 if kind == "Imm16GeomAdd" else 4
        orig = ours.rd(host_va + ofs, width)
        native = NATIVE.get(r["src_val"])
        if kind == "Imm32GeomSet" and not native_ok(r["src_val"], orig):
            rejected.append((tgt, "operand write", f"slot {r['src_val']:#x} is not `{host_s}`'s value at 1024x768"))
            continue
        if kind != "Imm32GeomSet" and (native is None or native[:4] != bytes(4)):
            rejected.append((tgt, "operand write", f"adds slot {r['src_val']:#x}, which is not 0 at 1024x768"))
            continue
        items[(r["our_func"], r["our_func_name"])].append(dict(
            va=host_va, len=ins.size, expect=ours.rd(host_va, ins.size),
            new=ours.rd(host_va, ins.size), fx=[(ofs, kind, r["src_val"])], stub=None,
            note=f"{ins.mnemonic} {ins.op_str}  <- {r['writer_asm']}"))
        tally["operand write"] += 1

    # --- 2 + 3. branches into ReBorn's stubs ----------------------------------
    for r in d["sites"]:
        if r["class"] not in ("cave-jmp", "cave-call", "tail-jmp", "tail-call") or r["scope"] not in ("hd", "hd?"):
            continue
        site, span, our = r["reborn_va"], r["reborn_span"], r["our_va"]
        label = f"{r['class']} {site:#x}"
        if our is None:
            rejected.append((None, label, f"not located ({r['located_by']})")); continue
        if decisions.get(our) == "reject":
            rejected.append((our, label, "rejected in hd_decisions.json")); continue
        mine = dis(ours, our, span)
        if r["our_span"] != span or sum(i.size for i in mine) != span:
            rejected.append((our, label, "our span does not end on an instruction boundary")); continue
        body = stub_body(reb, r["target"])
        if body is None:
            rejected.append((our, label, "stub has no clean end")); continue
        key = (r["our_func"], r["our_func_name"])
        if r["class"] == "cave-jmp":
            red = reduce_in_place(ours, reb, mp, site, span, our, body)
            if red and red[0] == "reject":
                rejected.append((our, label, red[1])); continue
            if red:
                fx = red[1]
                items[key].append(dict(va=our, len=span, expect=ours.rd(our, span), new=ours.rd(our, span),
                                       fx=fx, stub=None, note=f"in place: " + " ; ".join(
                                           f"{i.mnemonic} {i.op_str}" for i in mine)))
                tally["stub reduced to operands"] += 1
                continue
        got, why = relocate(ours, reb, mp, r, body)
        if got is None:
            rejected.append((our, label, why)); continue
        blob, sfx = got
        opcode = 0xE8 if r["class"].endswith("call") else 0xE9
        new = bytes([opcode, 0, 0, 0, 0]) + b"\x90" * (span - 5)
        items[key].append(dict(va=our, len=span, expect=ours.rd(our, span), new=new,
                               fx=[(1, "Rel32ToStub", None)], stub=(blob, sfx),
                               note=f"{'call' if opcode == 0xE8 else 'jmp'} stub ({len(blob)} B): " +
                                    " ; ".join(f"{i.mnemonic} {i.op_str}" for i in mine)))
        tally["stub relocated"] += 1

    # --- emit ---------------------------------------------------------------
    out, rows = [], []
    for (func, name), sites in sorted(items.items(), key=lambda kv: min(s["va"] for s in kv[1])):
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
        tag = f"hd_{(name or 'at_%08x' % clean[0]['va']).lower()}"
        nstubs = sum(1 for s in clean if s["stub"])
        out.append(f"// --- {tag}: {len(clean)} site(s), {nstubs} stub(s)")
        stub_rows, sid = [], 0
        for i, s in enumerate(clean):
            out.append(f"// {s['va']:#010x}  {s['note']}")
            out.append(f"static const uint8_t {tag}_e{i}[] = {c_bytes(s['expect'])};")
            out.append(f"static const uint8_t {tag}_n{i}[] = {c_bytes(s['new'])};")
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
            out.append(f"static const Fixup {tag}_f{i}[] = {{ " + ", ".join(
                f"{{ {o}, Fix::{k}, 0x{a:08X}u }}" for o, k, a in s["fx"]) + " };")
        out.append(f"static const Site {tag}_sites[] = {{")
        for i, s in enumerate(clean):
            out.append(f"    {{ 0x{s['va']:08X}u, {s['len']}, {tag}_e{i}, {tag}_n{i}, {tag}_f{i}, {len(s['fx'])} }},")
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

    with open(OUT_INC, "w", encoding="utf-8", newline="\n") as f:
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
