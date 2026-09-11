"""disasm_tiling_check.py -- does funkcode_disasm tell the truth?

Walks every record of every *canonical* script blob (FunkCode.bin AND
StartCode.bin; identical blobs are measured once, by md5) through
`funkcode_disasm.disasm_payload` and counts every operand line that fails to
tile the record's payload:

  truncated     a fixed-width operand needs more bytes than the payload has
                (the "(truncated, +N)" lines AUDIT.md Severity 8 counted)
  unterminated  an ASCIIZ operand has no NUL before the payload end
  overrun       the walk stepped past the payload end without saying so
  leftover      bytes remain after the reader's END (FUN_00472bc0 returns 0 at
                0x00 and never advances past it)
  unknown       a byte the disassembler has no form for

A record "tiles" when none of those happen and the walk ends exactly on the
payload's last byte. Counts are reported per source, per record tag and per
wire opcode (the opcode byte of the failing operand line).

Usage
-----
    python sdk/re/py/disasm_tiling_check.py                   # canonical, both blobs
    python sdk/re/py/disasm_tiling_check.py --examples 3      # + sample failures
    python sdk/re/py/disasm_tiling_check.py --disasm-module <path/to/other_funkcode_disasm.py>
    python sdk/re/py/disasm_tiling_check.py --json out.json   # machine-readable

`--disasm-module` measures a different copy of the disassembler (that is how
the baseline below was taken, against the pre-fix module). A module whose
`disasm_payload` accepts `tag=` is given the record tag, so per-tag raw layouts
(tags 0x28 / 0x29 / 0x2f, which the walker reads without the field reader) are
decoded as such.

The script also cross-checks the frozen legacy opcode vocabulary in
`funkcode_ops` against `sdk/lua_bake_opcodes.inc` (the C++ bake's copy); any
drift is reported as a failure, because it would make Lua dumps un-bakeable.

RESULTS
-------
Steam 2.0.2.28 install, 2026-09-11. Canonical sources, md5-distinct blobs
only: 11 FunkCode.bin + 10 StartCode.bin (addon:NetScript/StartCode.bin is
byte-identical to base:NetScript), 1,381,228 records.

AUDIT.md Severity 8, re-counted in the shards written with the old table:
420 "(truncated, +N)" lines in 138 of 741 shard files -- exact match.

baseline -- pre-fix funkcode_disasm.py (md5 9b37b1c565adbeb622317f5cb715b0a0),
measured through --disasm-module on a saved copy:
    tiling 1,308,583 / 1,381,228 records (94.74 %), failing 72,645
    failing operand lines 108,675 = truncated 19,484 + unterminated 144
        + overrun 14,384 (silent "cstr1+5" / "cstr1+1" tails)
        + unknown 74,663 (misaligned bytes 0x17 0x18 0x21 0x22 0x76 0x94 0xa2..0xff)
    worst tags (failing / all): 0x32 18,608/39,106  0x28 9,639/9,639
        0x08 8,891/93,205  0x01 7,558/64,246  0x3a 5,902/24,569
        0x42 5,383/47,744  0x2e 4,163/6,828  0x04 3,609/7,361
        0x29 2,640/4,400  0x03 2,184/24,420
    worst opcodes: 0x04 overrun 11,259 | 0x4d truncated 7,400 | 0x02 2,335 |
        0x56 2,017 | 0x33 1,246 | 0x0d overrun 1,245 | 0x34 1,095 | 0x40 overrun 999

fixed -- funkcode_disasm.GRAMMAR + RAW_LAYOUTS, tag passed:
    tiling 1,381,226 / 1,381,228 records, failing 2
    failing operand lines 2 = truncated 2, both KNOWN_RESIDUE (tag 0x32 /
        op 0x2a: one TriggerPatch record, shipped in two StartCode blobs,
        ends with a lone XYZ opcode); unexplained 0
    operand lines 4,642,336 (7,040,962 under the old table, 2,398,626 more:
        it cut one operand into several, e.g. MECHANICS' tag 0x44 payload
        `01 "43" 0b 05000000` printed as DLG_OP_a '43','\\x0b\\x05' + END + END
        = 3 lines, now NAME '43' + INT 5 = 2 lines)
    legacy vocabulary: funkcode_ops == lua_bake_opcodes.inc, 156 rows
    --verify-exe: OK -- all 256 opcodes land in the jump-table group GRAMMAR
        names (index table 0x4755d4, jump table 0x475534, switch on op-1, 0..0xa0)
    --verify-tags: OK -- 127 keywords on 125 tags (three resolvers) and all 123
        walker cases agree with funkcode_tags

fixed, but tag NOT passed (--no-tag; what a caller that omits tag= gets):
    tiling 1,368,947, failing 12,281 = tag 0x28 all 9,639 (payload byte 1 is
    0xFF -> END, 80 bytes "leftover") + tag 0x29 2,640 of 4,400 (selectors
    2/3/4 read as truncated ops 0x02/0x03/0x04; selector 1 tiles by accident)
    + the 2 known-residue lines. quest_script.dump_record (and so the shard
    writer) calls disasm_payload without tag= today.
"""
import os, sys, re, json, struct, argparse, importlib.util, inspect, collections

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
import funkcode_sources as fs

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

FILES = ("FunkCode.bin", "StartCode.bin")
KINDS = ("truncated", "unterminated", "overrun", "leftover", "unknown")

# one operand line of disasm_payload:  "+0004  0b  LABEL  rest..."
LINE_RE = re.compile(r"^\s*\+([0-9a-fA-F]{4,})\s+([0-9a-fA-F]{2}|--)\s+(\S+)\s?(.*)$")


def load_disasm(path=None):
    if not path:
        import funkcode_disasm as mod
        return mod
    spec = importlib.util.spec_from_file_location("funkcode_disasm_under_test", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _legacy_unterminated(mod, payload, op, ip):
    """Pre-fix module: an unterminated cstr is only visible as a trailing '...'.
    Re-derive it from the module's own table: no NUL after the string start."""
    info = getattr(mod, "OPCODE_TABLE", {}).get(op)
    if not info:
        return False
    kind = info[1]
    if "cstr" not in kind:
        return False
    start = ip + 1 + (4 if kind.startswith("u32+") else 0)
    return payload.find(0, start) < 0


def classify(mod, payload, tag, pass_tag):
    """Return (fails, end_ip, n_lines). fails = [(kind, op, ip)]."""
    kw = {"indent": 0}
    if pass_tag:
        kw["tag"] = tag
    out, _hist, end_ip = mod.disasm_payload(payload, **kw)
    legacy = getattr(mod, "GRAMMAR", None) is None
    fails = []
    last = (None, 0)
    n_lines = 0
    for ln in out:
        m = LINE_RE.match(ln)
        if not m:
            continue
        n_lines += 1
        ip = int(m.group(1), 16)
        opb = m.group(2)
        op = None if opb == "--" else int(opb, 16)
        label, rest = m.group(3), m.group(4)
        last = (op, ip)
        if "(truncated" in rest or "(u32 truncated)" in rest:
            fails.append(("truncated", op, ip))
        elif "(unterminated" in rest:
            fails.append(("unterminated", op, ip))
        elif "(leftover" in rest:
            fails.append(("leftover", op, ip))
        elif label.startswith("UNK"):
            fails.append(("unknown", op, ip))
        elif legacy and rest.endswith("...'") and _legacy_unterminated(mod, payload, op, ip):
            fails.append(("unterminated", op, ip))
    if end_ip > len(payload) and not any(k in ("truncated", "unterminated") for k, _, _ in fails):
        fails.append(("overrun", last[0], last[1]))
    return fails, end_ip, n_lines


def check_legacy_vocabulary():
    """funkcode_ops' label vocabulary must equal sdk/lua_bake_opcodes.inc."""
    inc = os.path.abspath(os.path.join(_HERE, "..", "..", "lua_bake_opcodes.inc"))
    if not os.path.isfile(inc):
        return None, "lua_bake_opcodes.inc not found"
    import funkcode_ops as fops
    rows = re.findall(r'\{"([^"]+)",\s*0x([0-9a-fA-F]+),\s*KIND_(\w+),\s*(\d+)\}', open(inc).read())
    kind_map = {"HALT": "halt", "STACK": "stack", "CONST": "const", "CSTR1": "cstr1",
                "CSTR2": "cstr2", "CSTR1_1": "cstr1+1", "CSTR1_5": "cstr1+5",
                "U32_CSTR1": "u32+cstr1", "U32_CSTR2": "u32+cstr2"}
    problems = []
    for label, op, kind, width in rows:
        got = fops.LABEL_TO_OP.get(label)
        want = (int(op, 16), kind_map[kind], int(width))
        if got is None:
            problems.append(f"{label}: missing from funkcode_ops.LABEL_TO_OP")
        elif (got[0], got[1], got[2] or 0) != want:
            problems.append(f"{label}: funkcode_ops {got} != lua_bake_opcodes.inc {want}")
    return len(rows), problems


# Failures that are properties of the DATA, not of the grammar. Keyed by
# (tag, wire opcode, kind); a run whose only failures are listed here exits 0.
KNOWN_RESIDUE = {
    (0x32, 0x2a, "truncated"):
        "tag 0x32 TriggerPatch 'chestTrigger' whose payload ends with a lone op 0x2a "
        "(base:NetScript StartCode #1937 @0x01666f and the byte-identical record in "
        "addon:SERAPHIM StartCode #1900 @0x0160d4). The handler FUN_00496f20:102-105 "
        "consumes 0x2a as an X/Y/Z triple (ctx+0xa860/64), and FUN_00472bc0 sizes it "
        "13 bytes, so the engine reads 12 bytes past the record end (stale walker "
        "buffer). An authoring defect in two shipped records.",
}

DECOMPILED = os.path.abspath(os.path.join(_HERE, "..", "ghidra", "decompiled"))


def reader_jump_table():
    """Re-derive FUN_00472bc0's opcode -> case-target map from machine code.
    Returns (groups {op: target}, default_target) or raises."""
    sys.path.insert(0, os.path.join(_HERE, "x86"))
    from pe import PE, md
    pe, m = PE(), md()
    fn = 0x00472bc0
    base = maxv = default = idx = jt = None
    for ins in m.disasm(pe.read(fn, 0x100), fn):
        mnem, ops = ins.mnemonic, ins.op_str
        if mnem == "lea" and re.search(r"\[e\w\w - 1\]", ops) and base is None:
            base = 1
        elif mnem == "cmp" and base is not None and maxv is None:
            maxv = int(ops.split(",")[1], 0)
        elif mnem == "ja" and maxv is not None and default is None:
            default = int(ops, 0)
        elif mnem == "mov" and re.search(r"byte ptr \[e\w\w \+ 0x[0-9a-f]+\]", ops) and default and idx is None:
            idx = int(re.search(r"\+ (0x[0-9a-f]+)\]", ops).group(1), 16)
        elif mnem == "jmp" and "*4" in ops:
            jt = int(re.search(r"\+ (0x[0-9a-f]+)\]", ops).group(1), 16)
            break
    if None in (base, maxv, default, idx, jt):
        raise RuntimeError("switch prologue not found in FUN_00472bc0")
    table = pe.read(idx, maxv + 1)
    nslot = max(table) + 1
    targets = struct.unpack_from("<%dI" % nslot, pe.read(jt, 4 * nslot))
    groups = {op: default for op in range(256)}
    for v in range(maxv + 1):
        groups[v + base] = targets[table[v]]
    return groups, default, dict(idx=idx, jt=jt, base=base, max=maxv)


def verify_exe(mod):
    groups, default, info = reader_jump_table()
    problems = []
    for op in range(256):
        row = mod.GRAMMAR[op]
        if groups[op] != row.case_va:
            problems.append("op 0x%02x: machine code -> 0x%08x, GRAMMAR says 0x%08x (%s)"
                            % (op, groups[op], row.case_va, row.form))
    return info, problems


def _resolver_keywords(pe):
    """keyword -> tag from the compiler's three resolvers (see funkcode_tags)."""
    assign = re.compile(r"^\s*(local_([0-9a-f]+))(?:\[(0x[0-9a-f]+|\d+)\])?\s*=\s*"
                        r"(?:\([^)]*\)\s*)?&?([A-Za-z_]\w*|0x[0-9a-f]+|\d+);", re.M)
    out = {}
    for fn in ("00452370_FUN_00452370.c", "00451be0_FUN_00451be0.c", "00452910_FUN_00452910.c"):
        src = open(os.path.join(DECOMPILED, fn), encoding="latin1").read()
        ptrs, ints = {}, {}
        for m in assign.finditer(src):
            off = -int(m.group(2), 16) + 4 * (int(m.group(3), 0) if m.group(3) else 0)
            rhs = m.group(4)
            if re.fullmatch(r"0x[0-9a-f]+|\d+", rhs):
                ints.setdefault(off, int(rhs, 0))
            else:
                mm = re.search(r"_([0-9a-f]{8})$", rhs)
                if mm and rhs.split("_")[0] in ("s", "DAT", "PTR"):
                    ptrs.setdefault(off, int(mm.group(1), 16))
        offs = sorted(ptrs)
        runs, cur = [], [offs[0]]
        for a, b in zip(offs, offs[1:]):
            if b - a == 4:
                cur.append(b)
            else:
                runs.append(cur); cur = [b]
        runs.append(cur)
        kws = max(runs, key=len)
        rm = (re.search(r"return (local_([0-9a-f]+))\[\w+\];", src)
              or re.search(r"_DAT_00aab714 = \(short\)(local_([0-9a-f]+))\[\w+\];", src))
        base = -int(rm.group(2), 16)
        for i, off in enumerate(kws):
            text = (pe.read(ptrs[off], 96) or b"").split(b"\0", 1)[0].decode("latin1")
            out.setdefault(ints[base + 4 * i], set()).add(text)
    return out


def _walker_cases():
    """tag -> (case line, first handler FUN called or 'inline') from FUN_00475680."""
    lines = open(os.path.join(DECOMPILED, "00475680_FUN_00475680.c"), encoding="latin1").read().splitlines()
    start = next(i for i, l in enumerate(lines) if "switch((ushort)local_a0c.pVFTable)" in l)
    case = re.compile(r"^  case (0x[0-9a-f]+|\d+):")
    skip = {"FUN_0084a961", "FUN_0045ee20", "FUN_00859690", "FUN_00849986", "FUN_0066ef40", "FUN_007d84a0",
            "FUN_00416780", "FUN_004166f0", "FUN_00416680", "FUN_00416710", "FUN_008321a0", "FUN_00849b70",
            "FUN_00832350", "FUN_00849410", "FUN_008498cd"}
    out, pending, i = {}, [], start + 1
    while i < len(lines) and i < start + 2600:
        l = lines[i]
        if l.startswith("  default:"):
            pending = []
        m = case.match(l)
        if m:
            pending.append((int(m.group(1), 0), i + 1))
            if i + 1 < len(lines) and case.match(lines[i + 1]):
                i += 1
                continue
            j, handler = i + 1, "inline"
            while j < len(lines) and not case.match(lines[j]) and not lines[j].startswith("  default:"):
                f = re.search(r"\b(FUN_[0-9a-f]{8})\(", lines[j])
                if f and f.group(1) not in skip:
                    handler = f.group(1)
                    break
                if re.match(r"^    (break|return|goto)", lines[j]) or "uVar9 = 1;" in lines[j]:
                    break
                j += 1
            for tag, ln in pending:
                out[tag] = (ln, handler)
            pending = []
        i += 1
    return out


# the walker handles these tags itself; the first FUN it calls is a helper
INLINE_HELPERS = {0x28: "FUN_004bb1d0",   # DlgNPC vector grow
                  0x2f: "FUN_004c5880",   # list lookup inside the raw 0x2f body
                  0x58: "FUN_00808e50",   # kernel event post (SetAnimMode)
                  0x86: "FUN_00808e50"}   # kernel event post (SetCinemaMode)


def verify_tags():
    sys.path.insert(0, os.path.join(_HERE, "x86"))
    from pe import PE
    import funkcode_tags as T
    problems = []
    kw = _resolver_keywords(PE())
    for t, names in kw.items():
        if set(T.KEYWORDS.get(t, ())) != names:
            problems.append("tag 0x%02x: resolvers say %s, funkcode_tags.KEYWORDS %s"
                            % (t, sorted(names), T.KEYWORDS.get(t)))
    for t in set(T.KEYWORDS) - set(kw):
        problems.append("tag 0x%02x: in KEYWORDS but no resolver maps a keyword to it" % t)
    wc = _walker_cases()
    for t, (ln, handler) in wc.items():
        if T.WALKER_CASE.get(t) != ln:
            problems.append("tag 0x%02x: walker case at :%d, WALKER_CASE says %s" % (t, ln, T.WALKER_CASE.get(t)))
        va = T.TAGS[t][1] if t in T.TAGS else "missing"
        if va is None:
            if handler not in ("inline", "FUN_00472bc0", INLINE_HELPERS.get(t)):
                problems.append("tag 0x%02x: table says inline, walker calls %s" % (t, handler))
        elif va == "missing" or handler != "FUN_" + va[2:]:
            problems.append("tag 0x%02x: walker calls %s, table says %s" % (t, handler, va))
    for t in set(T.WALKER_CASE) - set(wc):
        problems.append("tag 0x%02x: WALKER_CASE lists a case the walker does not have" % t)
    return dict(keyword_tags=len(kw), keywords=sum(len(v) for v in kw.values()),
                walker_cases=len(wc)), problems


def run(args):
    mod = load_disasm(args.disasm_module)
    pass_tag = "tag" in inspect.signature(mod.disasm_payload).parameters and not args.no_tag
    srcs = fs.resolve(args.sources, default="canonical")
    files = [f.strip() for f in args.files.split(",") if f.strip()]

    per_source = collections.OrderedDict()
    per_tag = collections.defaultdict(lambda: collections.Counter())
    per_op = collections.defaultdict(lambda: collections.Counter())
    per_tag_op = collections.Counter()
    examples = collections.defaultdict(list)
    totals = collections.Counter()
    skipped = []

    for fname in files:
        seen = {}
        for s in srcs:
            if not s.exists(fname):
                continue
            md5 = fs.file_md5(s, fname)
            if md5 in seen and not args.no_dedupe:
                skipped.append(f"{s.key}/{fname} == {seen[md5]}")
                continue
            seen.setdefault(md5, s.key)
            buf = s.read(fname)
            row = collections.Counter()
            for i, (off, tag, size, payload) in enumerate(fs_walk(mod, buf)):
                row["records"] += 1
                per_tag[tag]["records"] += 1
                fails, end_ip, n = classify(mod, payload, tag, pass_tag)
                row["operand_lines"] += n
                tiles = not fails and end_ip == len(payload)
                if tiles:
                    row["tiling"] += 1
                    per_tag[tag]["tiling"] += 1
                    continue
                if not fails:          # walked short without a marker
                    fails = [("overrun", None, end_ip)]
                row["records_failing"] += 1
                per_tag[tag]["records_failing"] += 1
                for kind, op, ip in fails:
                    row[kind] += 1
                    per_tag[tag][kind] += 1
                    per_op[op][kind] += 1
                    per_tag_op[(tag, op, kind)] += 1
                    key = (tag, op, kind)
                    if len(examples[key]) < args.examples:
                        examples[key].append(dict(source=s.key, file=fname, record=i,
                                                  offset=off, ip=ip,
                                                  payload=payload[:96].hex()))
            per_source[f"{s.key}/{fname}"] = row
            totals.update(row)

    report = dict(module=getattr(mod, "__file__", "?"), pass_tag=pass_tag,
                  totals=dict(totals), skipped_duplicates=skipped,
                  per_source={k: dict(v) for k, v in per_source.items()},
                  per_tag={f"0x{t:02x}": dict(v) for t, v in sorted(per_tag.items())},
                  per_op={("--" if o is None else f"0x{o:02x}"): dict(v)
                          for o, v in sorted(per_op.items(), key=lambda kv: -1 if kv[0] is None else kv[0])},
                  per_tag_op=[dict(tag=f"0x{t:02x}", op=("--" if o is None else f"0x{o:02x}"),
                                   kind=k, n=n) for (t, o, k), n in per_tag_op.most_common()],
                  examples={f"tag0x{t:02x}/op{'--' if o is None else f'0x{o:02x}'}/{k}": v
                            for (t, o, k), v in examples.items()})
    n_inc, problems = check_legacy_vocabulary()
    report["legacy_vocabulary"] = dict(inc_rows=n_inc, problems=problems)
    explained, unexplained = collections.Counter(), collections.Counter()
    for (tag, op, kind), n in per_tag_op.items():
        (explained if (tag, op, kind) in KNOWN_RESIDUE else unexplained)[kind] += n
    report["explained"] = dict(explained)
    report["unexplained"] = dict(unexplained)
    report["known_residue"] = {f"tag0x{t:02x}/op0x{o:02x}/{k}": why
                               for (t, o, k), why in KNOWN_RESIDUE.items() if per_tag_op.get((t, o, k))}
    if args.verify_exe:
        if hasattr(mod, "GRAMMAR"):
            info, probs = verify_exe(mod)
            info = {k: (hex(v) if isinstance(v, int) and v > 0xff else v) for k, v in info.items()}
        else:
            info, probs = {}, ["module under test has no GRAMMAR"]
        report["verify_exe"] = dict(info=info, problems=probs)
    if args.verify_tags:
        info, probs = verify_tags()
        report["verify_tags"] = dict(info=info, problems=probs)
    return report


def fs_walk(mod, buf):
    return mod.walk_records(buf)


def print_report(r, top):
    t = r["totals"]
    fl = sum(t.get(k, 0) for k in KINDS)
    print(f"module: {r['module']}   (tag passed to disasm_payload: {r['pass_tag']})")
    print(f"records {t.get('records',0):,}   tiling {t.get('tiling',0):,}   "
          f"failing {t.get('records_failing',0):,}   operand lines {t.get('operand_lines',0):,}")
    print("failing operand lines: " + "  ".join(f"{k}={t.get(k,0):,}" for k in KINDS) + f"   total={fl:,}")
    if r["skipped_duplicates"]:
        print("measured once (md5-identical): " + "; ".join(r["skipped_duplicates"]))
    print("\n== per source ==")
    for k, v in r["per_source"].items():
        print(f"  {k:32} recs={v.get('records',0):7,} tiling={v.get('tiling',0):7,} "
              + " ".join(f"{kk}={v.get(kk,0)}" for kk in KINDS if v.get(kk)))
    print("\n== per tag (failing records only) ==")
    for tg, v in r["per_tag"].items():
        if v.get("records_failing"):
            print(f"  tag {tg}: {v['records_failing']:6,}/{v['records']:7,} records fail   "
                  + " ".join(f"{kk}={v.get(kk,0)}" for kk in KINDS if v.get(kk)))
    print("\n== per wire opcode (op of the failing operand line) ==")
    for o, v in r["per_op"].items():
        print(f"  op {o}: " + " ".join(f"{kk}={v.get(kk,0)}" for kk in KINDS if v.get(kk)))
    print(f"\n== top {top} (tag, op, kind) ==")
    for row in r["per_tag_op"][:top]:
        print(f"  tag {row['tag']}  op {row['op']}  {row['kind']:12} {row['n']:,}")
    if r["examples"]:
        print("\n== examples ==")
        for key, lst in r["examples"].items():
            for e in lst:
                print(f"  {key}: {e['source']}/{e['file']} #{e['record']} @0x{e['offset']:06x} ip=0x{e['ip']:x}  {e['payload']}")
    lv = r["legacy_vocabulary"]
    if lv["inc_rows"] is None:
        print("\nlegacy vocabulary check: skipped (" + lv["problems"] + ")")
    else:
        print(f"\nlegacy vocabulary (funkcode_ops vs lua_bake_opcodes.inc, {lv['inc_rows']} rows): "
              + ("OK" if not lv["problems"] else f"{len(lv['problems'])} PROBLEMS"))
        for p in lv["problems"][:20]:
            print("  " + p)
    if r.get("known_residue"):
        print("\n== known residue (data defects, not grammar) ==")
        for k, why in r["known_residue"].items():
            print(f"  {k}: {why}")
    print("\nunexplained failing operand lines: "
          + (", ".join(f"{k}={v:,}" for k, v in r["unexplained"].items()) or "none"))
    for key in ("verify_exe", "verify_tags"):
        if key in r:
            v = r[key]
            print(f"\n{key}: {v['info']}  -> " + ("OK" if not v["problems"] else f"{len(v['problems'])} PROBLEMS"))
            for p in v["problems"][:30]:
                print("  " + p)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default="canonical")
    ap.add_argument("--files", default=",".join(FILES))
    ap.add_argument("--disasm-module", default=None)
    ap.add_argument("--no-dedupe", action="store_true")
    ap.add_argument("--examples", type=int, default=0)
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--json", default=None)
    ap.add_argument("--no-tag", action="store_true",
                    help="do not pass the record tag (what a caller that omits tag= sees)")
    ap.add_argument("--verify-exe", action="store_true",
                    help="re-derive FUN_00472bc0's jump table from the exe (needs capstone) and compare with GRAMMAR")
    ap.add_argument("--verify-tags", action="store_true",
                    help="re-extract the compiler keyword tables and the walker switch and compare with funkcode_tags")
    args = ap.parse_args()
    r = run(args)
    print_report(r, args.top)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(r, fh, indent=1)
        print(f"\nwrote {args.json}")
    lv = r["legacy_vocabulary"]
    bad = sum(r["unexplained"].values())
    bad += len(lv["problems"]) if lv["inc_rows"] is not None else 0
    for key in ("verify_exe", "verify_tags"):
        if key in r:
            bad += len(r[key]["problems"])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
