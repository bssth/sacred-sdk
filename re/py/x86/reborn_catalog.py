"""reborn_catalog.py - catalogue ReBorn's code patches and where each one lands in OUR build.

Usage:  python reborn_catalog.py            (paths: buildmap.OURS / buildmap.REBORN)

Reads the user's own SacredReborn.exe and our sdk/Sacred_decrypted.exe and writes
  sdk/re/hd_table.gen.json      every site, machine readable
  sdk/re/patch_review_queue.md  the same, for a human, one block per site
Both outputs contain bytes and disassembly of ReBorn's binary, so both are
gitignored and must never be committed. This script is the shippable part.

The sites are ENUMERATED, not diffed. Diffing two different compilations of the
same source mostly finds compiler noise; ReBorn's own patches announce
themselves because they branch out of engine code into code ReBorn added:
  cave     E9/E8/0F8x in .text whose target lies in the stub cave ReBorn cut
           into .rdata (0x8E4400..0x8E5000)
  tail     the same into ReBorn's code appended to .rsrc (0x1AC6000..0x1ADC000)
  selfpatch  instructions in that tail code that WRITE into .text at run time
           (ReBorn rewrites immediates inside engine instructions after start)

For each site we find our address in two ways, in order:
  bytes   masked byte context before and after the patched span (buildmap.locate)
  shape   instruction-shape context: mnemonic + operand kinds, registers and
          stack displacements ignored. Needed where the two compilations laid a
          function out differently (display init 0x815F30..0x817800 is one).
A site nobody can locate is reported, never guessed.
"""
import os, re, struct, json, bisect, collections
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86 as X
import buildmap as bm

HERE = os.path.dirname(os.path.abspath(__file__))
RE_DIR = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT_JSON = os.path.join(RE_DIR, "hd_table.gen.json")
OUT_MD = os.path.join(RE_DIR, "patch_review_queue.md")

CAVE = (0x008E4400, 0x008E5000)
TAIL = (0x01AC6000, 0x01ADC000)
HD_GLOBALS = (0x00A1EF00, 0x00A1F100)

MD = Cs(CS_ARCH_X86, CS_MODE_32)
MD.detail = True


def region_of(va):
    if CAVE[0] <= va < CAVE[1]: return "cave"
    if TAIL[0] <= va < TAIL[1]: return "tail"
    return None


def dis_one(img, va):
    b = img.rd(va, 16)
    return next(MD.disasm(b, va), None) if b else None


def dis_range(img, lo, hi):
    b = img.rd(lo, hi - lo)
    return list(MD.disasm(b, lo)) if b else []


# ---------------------------------------------------------------------------
#  Instruction boundaries
# ---------------------------------------------------------------------------
def sweep_back(img, va, span=64):
    """Instructions ending exactly at `va`, recovered without a function start.

    x86 resynchronises within a few instructions, so decode from every start in
    [va-span, va) and keep the boundaries that most starts agree on.
    """
    votes = collections.Counter()
    for st in range(va - span, va):
        b = img.rd(st, va - st)
        if not b:
            continue
        pos, bounds = st, []
        for ins in MD.disasm(b, st):
            bounds.append(ins.address)
            pos = ins.address + ins.size
        if pos == va:
            for x in bounds:
                votes[x] += 1
    if not votes:
        return []
    top = max(votes.values())
    good = sorted(x for x, c in votes.items() if c >= top * 0.6)
    out = []
    for a in good:
        ins = dis_one(img, a)
        if ins and ins.address + ins.size <= va and (not out or out[-1].address + out[-1].size == a):
            out.append(ins)
        elif ins and out and out[-1].address + out[-1].size != a:
            out = [ins]
    return [i for i in out if i.address + i.size <= va]


def shape(ins):
    """A compilation-independent token for one instruction."""
    ops = []
    for op in ins.operands:
        if op.type == X.X86_OP_REG:
            ops.append("r")
        elif op.type == X.X86_OP_IMM:
            v = op.imm & 0xFFFFFFFF
            ops.append("i" if bm.VA_LO <= v < bm.VA_HI or ins.group(X.X86_GRP_JUMP)
                       or ins.group(X.X86_GRP_CALL) else f"#{v:x}")
        elif op.type == X.X86_OP_MEM:
            m = op.mem
            if m.base == 0 and m.index == 0:
                ops.append("[abs]")
            elif m.base in (X.X86_REG_ESP, X.X86_REG_EBP):
                ops.append("[stk]")
            else:
                ops.append(f"[r+{m.disp & 0xFFFFFFFF:x}]" if abs(m.disp) < 0x1000 else "[r+d]")
    mn = ins.mnemonic
    if ins.group(X.X86_GRP_JUMP) and mn != "jmp":
        mn = "jcc"
    return mn + " " + ",".join(ops)


# ---------------------------------------------------------------------------
#  Locating a ReBorn site in our build
# ---------------------------------------------------------------------------
def our_candidates(dm, new_va):
    ds = []
    d = dm.new2old(new_va)
    if d is not None:
        ds.append(new_va - d)
    for x in dm.neighbour_deltas(new_va=new_va, k=4):
        if x not in ds:
            ds.append(x)
    return [new_va - d for d in ds]


def _overlap(a, b):
    """Share of `a`'s shapes also present in `b`, order ignored."""
    ca, cb = collections.Counter(a), collections.Counter(b)
    return sum(min(v, cb[k]) for k, v in ca.items()) / max(1, len(a))


def locate_by_shape(ours, reb, dm, site, span, n_ctx=8, win=0x1800):
    """Find the span in ours by instruction shapes around it.

    One side of the patch has to match in exact order; the other side only as a
    multiset (>= 70 %), because the two compilations schedule neighbouring
    instructions differently (display init: `push 0x38` and `neg` swap places).
    Our span is the run of whole instructions adjacent to the exact side whose
    length equals ReBorn's span, since ReBorn patches in place.
    """
    pre = sweep_back(reb, site)[-n_ctx:]
    post = []
    pos = site + span
    for ins in MD.disasm(reb.rd(pos, 128), pos):
        post.append(ins)
        if len(post) >= n_ctx:
            break
    if len(pre) < 4 or len(post) < 4:
        return None, "shape: no context"
    want_pre = [shape(i) for i in pre]
    want_post = [shape(i) for i in post]
    found = {}
    for cand in our_candidates(dm, site):
        lo = max(ours.text_lo, cand - win)
        seq = sweep_back(ours, lo + 64, span=64)
        start = seq[0].address if seq else lo
        insns = dis_range(ours, start, min(ours.text_hi, cand + win))
        shapes = [shape(i) for i in insns]
        addr_idx = {i.address: k for k, i in enumerate(insns)}
        n = len(insns)

        # exact post, span walks back to an exact byte count
        L = len(want_post)
        for j in range(1, n - L + 1):
            if shapes[j:j + L] != want_post:
                continue
            b = insns[j].address
            k, tot = j, 0
            while k > 0 and tot < span:
                k -= 1
                tot += insns[k].size
            if tot != span:
                continue
            if _overlap(want_pre, shapes[max(0, k - len(want_pre)):k]) >= 0.7:
                found[(insns[k].address, span)] = "shape-post"

        # exact pre, span walks forward
        L = len(want_pre)
        for k in range(0, n - L):
            if shapes[k:k + L] != want_pre:
                continue
            a = insns[k + L - 1].address + insns[k + L - 1].size
            j, tot = addr_idx.get(a), 0
            if j is None:
                continue
            while j < n and tot < span:
                tot += insns[j].size
                j += 1
            if tot != span:
                continue
            if _overlap(want_post, shapes[j:j + len(want_post)]) >= 0.7:
                found.setdefault((a, span), "shape-pre")
    if len(found) == 1:
        (a, ln), how = found.popitem()
        return (a, ln), how
    return None, (f"shape: ambiguous {len(found)}" if found else "shape: not found")


def patched_span(reb, site, ins):
    """The bytes ReBorn overwrote: the branch plus the NOP run it left behind."""
    n = ins.size
    while reb.rd(site + n, 1) == b"\x90":
        n += 1
    return n


def locate_site(ours, reb, dm, site, span):
    va, how = dm.locate_old(site, before=24, after=24, skip=span, win=0x2000)
    if va is not None:
        return va, span, how
    got, how2 = locate_by_shape(ours, reb, dm, site, span)
    if got:
        return got[0], got[1], how2
    return None, None, f"{how}; {how2}"


# ---------------------------------------------------------------------------
#  Stubs
# ---------------------------------------------------------------------------
def walk_stub(reb, entry, kind, limit=96):
    """Follow a stub from its entry to every exit. Returns a summary dict."""
    exits, globals_, calls, body = set(), set(), set(), []
    todo, seen = [entry], set()
    while todo and len(body) < limit:
        va = todo.pop()
        while va not in seen and len(body) < limit:
            seen.add(va)
            ins = dis_one(reb, va)
            if not ins:
                break
            body.append(ins)
            for op in ins.operands:
                if op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    d = op.mem.disp & 0xFFFFFFFF
                    if HD_GLOBALS[0] <= d < HD_GLOBALS[1]:
                        globals_.add(d)
            if ins.mnemonic == "ret":
                exits.add("ret")
                break
            if ins.group(X.X86_GRP_CALL) and ins.operands[0].type == X.X86_OP_IMM:
                calls.add(ins.operands[0].imm & 0xFFFFFFFF)
            if ins.group(X.X86_GRP_JUMP) and ins.operands[0].type == X.X86_OP_IMM:
                t = ins.operands[0].imm & 0xFFFFFFFF
                if reb.in_text(t):
                    exits.add(t)
                elif region_of(t):
                    todo.append(t)
                if ins.mnemonic == "jmp":
                    break
            va = ins.address + ins.size
    return {
        "entry": entry, "kind": kind, "len": sum(i.size for i in body),
        "exits": sorted(e for e in exits if e != "ret") + (["ret"] if "ret" in exits else []),
        "globals": sorted(globals_), "calls": sorted(calls),
        "asm": [f"{i.address:#x}: {i.mnemonic} {i.op_str}" for i in body],
    }


# ---------------------------------------------------------------------------
#  Enumeration
# ---------------------------------------------------------------------------
def enum_branch_sites(reb):
    sites = []
    T, lo = reb.text, reb.text_lo
    for i in range(len(T) - 6):
        b = T[i]
        if b in (0xE8, 0xE9):
            tgt = (lo + i + 5 + struct.unpack_from("<i", T, i + 1)[0]) & 0xFFFFFFFF
        elif b == 0x0F and 0x80 <= T[i + 1] <= 0x8F:
            tgt = (lo + i + 6 + struct.unpack_from("<i", T, i + 2)[0]) & 0xFFFFFFFF
        else:
            continue
        r = region_of(tgt)
        if not r:
            continue
        ins = dis_one(reb, lo + i)
        if not ins or ins.address != lo + i:
            continue
        if not (ins.group(X.X86_GRP_JUMP) or ins.group(X.X86_GRP_CALL)):
            continue
        # a random E8/E9 byte pair can point anywhere; the instruction before it
        # must decode to a boundary at this address
        back = sweep_back(reb, lo + i, span=32)
        if not back:
            continue
        sites.append((lo + i, ins, tgt, r))
    return sites


WRITE_MNEMONICS = {"mov", "add", "sub", "or", "and", "xor", "inc", "dec", "neg", "not", "movsd", "movss"}


def enum_selfpatches(reb):
    """Instructions in ReBorn's tail code that write to an absolute .text address."""
    out = []
    blob_lo, blob_hi = TAIL
    blob = reb.rd(blob_lo, blob_hi - blob_lo)
    seen = set()
    for i in range(len(blob) - 6):
        # absolute 32-bit address inside .text somewhere in the next 6 bytes
        v = struct.unpack_from("<I", blob, i)[0]
        if not reb.in_text(v):
            continue
        for back in range(1, 4):
            st = blob_lo + i - back
            if st in seen:
                continue
            ins = dis_one(reb, st)
            if not ins or not ins.operands:
                continue
            op0 = ins.operands[0]
            if op0.type != X.X86_OP_MEM or op0.mem.base or op0.mem.index:
                continue
            if (op0.mem.disp & 0xFFFFFFFF) != v or ins.mnemonic not in WRITE_MNEMONICS:
                continue
            seen.add(st)
            out.append((st, ins, v))
            break
    return out


# ---------------------------------------------------------------------------
def main():
    ours, reb, dm = bm.load()
    rows = []

    # --- branches into ReBorn's code ------------------------------------
    stubs = {}
    for site, ins, tgt, r in enum_branch_sites(reb):
        span = patched_span(reb, site, ins)
        kind = "call" if ins.group(X.X86_GRP_CALL) else ("jmp" if ins.mnemonic == "jmp" else "jcc")
        if tgt not in stubs:
            stubs[tgt] = walk_stub(reb, tgt, r)
        va, ln, how = locate_site(ours, reb, dm, site, span)
        rows.append({
            "class": f"{r}-{kind}", "reborn_va": site, "reborn_span": span,
            "target": tgt, "our_va": va, "our_span": ln, "located_by": how,
            "our_asm": ([f"{i.address:#x}: {i.mnemonic} {i.op_str}" for i in dis_range(ours, va, va + ln)]
                        if va is not None else []),
            "reborn_asm": [f"{i.address:#x}: {i.mnemonic} {i.op_str}" for i in dis_range(reb, site, site + span)],
        })

    # --- run-time writes into .text ---------------------------------------
    for st, ins, tva in enum_selfpatches(reb):
        # the written address is inside an instruction; find the instruction
        # that contains it in ReBorn, then that instruction in ours
        host = None
        for back in range(0, 12):
            h = dis_one(reb, tva - back)
            if h and h.address + h.size > tva and sweep_back(reb, h.address, span=32):
                host = h
                break
        row = {"class": "selfpatch", "writer_va": st, "writer_asm": f"{ins.mnemonic} {ins.op_str}",
               "reborn_target": tva, "reborn_host": None, "our_host": None, "our_target": None,
               "located_by": None}
        if host:
            row["reborn_host"] = f"{host.address:#x}: {host.mnemonic} {host.op_str}"
            va, ln, how = locate_site(ours, reb, dm, host.address, host.size)
            # ReBorn patches these operands at run time, so its FILE still holds
            # the original instruction: ours has to match it byte for byte
            # (addresses masked), whatever located it.
            if va is not None:
                hb = reb.rd(host.address, host.size)
                if not bm.masked_eq(ours.rd(va, host.size), hb, bm.mask_of(hb)):
                    how, va = f"{how} rejected: host bytes differ", None
            row["located_by"] = how
            if va is not None:
                oh = dis_one(ours, va)
                row["our_host"] = f"{oh.address:#x}: {oh.mnemonic} {oh.op_str}" if oh else None
                row["our_target"] = va + (tva - host.address)
                row["field_equal"] = ours.rd(row["our_target"], 4) == reb.rd(tva, 4)
        rows.append(row)

    # --- report ---------------------------------------------------------
    cnt = collections.Counter(r["class"] for r in rows)
    loc = collections.Counter((r["class"], (r.get("located_by") or "none").split(":")[0].split(";")[0])
                              for r in rows)
    print("sites:", dict(cnt))
    print("located:", {f"{k[0]}/{k[1]}": v for k, v in sorted(loc.items())})
    print("stubs:", len(stubs), " by region:", dict(collections.Counter(s["kind"] for s in stubs.values())))

    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump({"runs": len(dm.runs), "sites": rows,
                   "stubs": {hex(k): v for k, v in sorted(stubs.items())}}, f, indent=1, default=str)
    with open(OUT_MD, "w", encoding="utf-8") as f:
        f.write("# ReBorn patch review queue (GENERATED, gitignored - contains third-party disassembly)\n\n")
        for r in rows:
            if r["class"] == "selfpatch":
                f.write(f"## selfpatch  writer {r['writer_va']:#x}  -> reborn {r['reborn_target']:#x}"
                        f"  ours {r['our_target'] and hex(r['our_target'])}  ({r['located_by']})\n")
                f.write(f"- writer: `{r['writer_asm']}`\n- reborn host: `{r['reborn_host']}`\n"
                        f"- our host: `{r['our_host']}`  field_equal={r.get('field_equal')}\n\n")
                continue
            s = stubs[r["target"]]
            f.write(f"## {r['class']}  reborn {r['reborn_va']:#x}+{r['reborn_span']}  ->  "
                    f"ours {r['our_va'] and hex(r['our_va'])}+{r['our_span']}  ({r['located_by']})\n")
            f.write("- ours:   `" + " ; ".join(r["our_asm"]) + "`\n")
            f.write("- reborn: `" + " ; ".join(r["reborn_asm"]) + "`\n")
            f.write(f"- stub {s['entry']:#x} len={s['len']} exits={[hex(e) if isinstance(e, int) else e for e in s['exits']]}"
                    f" globals={[hex(g) for g in s['globals']]}\n")
            f.write("  ```\n  " + "\n  ".join(s["asm"][:24]) + "\n  ```\n\n")
    print("wrote", OUT_JSON, "and", OUT_MD)


if __name__ == "__main__":
    main()
