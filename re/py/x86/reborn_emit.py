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
import os, re, json, struct, collections
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


def is_slot(v):
    return SLOTS[0] <= v < SLOTS[1]


NATIVE = {}   # slot VA -> 8 raw bytes at 1024x768, filled by native_slots() from the emulator


def native_slots(reb):
    """Every slot's bytes at 1024x768, straight from running ReBorn's init.

    At the native size every layout value must equal the constant the engine
    was compiled with, so a site whose original operand differs from its slot
    here is a mislocated site or a behaviour change of ReBorn's, and is dropped
    one site at a time. The patch engine repeats the check on our C++ values.
    """
    import reborn_init_emu as emu
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
            leave_out(t, None, label, "not located"); continue
        func = scan.gh_func_of(our_host)
        name = scan.gh_name(our_host) if func is not None else None
        located[t] = func
        hb = reb.rd(host.address, host.size)
        if not bm.masked_eq(ours.rd(our_host, host.size), hb, bm.mask_of(hb)):
            leave_out(t, our_host, label, "ReBorn changed the host instruction itself", func); continue
        ofs = t - host.address
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
    for key in list(items):
        if key[0] in incomplete:
            n = len(items.pop(key))
            rejected.append((None, f"function {key[1]}",
                             f"dropped {n} site(s): layout sites at ReBorn " +
                             ", ".join(f"{x:#x}" for x in incomplete[key[0]]) + " are not ported"))
            tally["sites dropped with their function"] += n

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
        ident = name if name and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) else f"at_{clean[0]['va']:08x}"
        tag = f"hd_{ident.lower()}"
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
