"""Parse FUN_00475680 (the FunkCode outer-walker / tag dispatcher) to extract
the full record-tag → action-function mapping.

This is THE central routing table of Sacred's quest engine: the value at
`local_a0c.pVFTable` (a u16 read from the in-memory record header) selects
which subsystem function processes the rest of the record.
"""
import os, re, sys, collections
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled\00475680_FUN_00475680.c"
src = open(DEC, encoding="utf-8", errors="replace").read()
lines = src.splitlines()

case_re = re.compile(r"^\s*case\s+(0x[0-9a-fA-F]+|\d+):\s*$")
default_re = re.compile(r"^\s*default:\s*$")
fun_call_re = re.compile(r"\b(FUN_[0-9a-f]{8}|c[A-Z][A-Za-z0-9_]+_[a-z][A-Za-z0-9_]*_[0-9a-f]{8})\s*\(")
str_use_re  = re.compile(r"(s_[A-Za-z_][A-Za-z0-9_]*_[0-9a-f]{8})")

# Find ONLY top-level cases of the OUTER switch.  We scan starting from the
# `switch((ushort)local_a0c.pVFTable)` line and stop when the matching brace
# closes the switch.
sw_start = None
for i, ln in enumerate(lines):
    if "switch((ushort)local_a0c.pVFTable)" in ln:
        sw_start = i
        break
if sw_start is None:
    print("can't find dispatcher switch"); sys.exit(1)

# Find matching closing brace by counting depth from the switch line.
# We start at sw_start where line ends with `{`. depth=0 means closed.
depth = 0
sw_end = None
for j in range(sw_start, len(lines)):
    depth += lines[j].count("{") - lines[j].count("}")
    if j > sw_start and depth == 0:
        sw_end = j; break
print(f"switch body: lines {sw_start+1}..{sw_end+1} ({sw_end-sw_start} lines)")

# Now collect TOP-LEVEL case labels (depth==1 within switch).
case_labels = []  # (line_idx, value)
depth = 0
in_case_body_depth = 0
for j in range(sw_start, sw_end+1):
    ln = lines[j]
    # Process opens BEFORE checking case (the switch's `{` is on sw_start line)
    new_depth = depth + ln.count("{") - ln.count("}")
    if depth == 1:
        m = case_re.match(ln)
        if m:
            v = m.group(1)
            val = int(v, 16) if v.lower().startswith("0x") else int(v)
            case_labels.append((j, val))
        elif default_re.match(ln):
            case_labels.append((j, -1))
    depth = new_depth

# Group consecutive case labels (fall-through) and extract bodies
groups = []
i = 0
while i < len(case_labels):
    base_line, val = case_labels[i]
    vals = [val]
    j = i + 1
    while j < len(case_labels):
        next_line, next_val = case_labels[j]
        between = "\n".join(lines[base_line+1: next_line]).strip()
        if between == "":
            vals.append(next_val); base_line = next_line; j += 1
        else:
            break
    body_end = case_labels[j][0] if j < len(case_labels) else sw_end
    groups.append((vals, base_line+1, body_end))
    i = j

print(f"\nfound {sum(len(g[0]) for g in groups)} case labels in {len(groups)} groups\n")

# Per-group: first FUN call (= the subsystem entry), strings used, line count
print(f"{'tag':>6}  {'lines':>5}  first-FUN          strings (sample)")
print("-" * 100)
master = []
for vals, s, e in groups:
    body = "\n".join(lines[s:e])
    funs = list(dict.fromkeys(fun_call_re.findall(body)))
    # Skip FUN_00472bc0 (the dispatcher itself) — we want the real subsystem entry
    funs_meaningful = [f for f in funs if f != "FUN_00472bc0"]
    strs = list(dict.fromkeys(str_use_re.findall(body)))
    first_fun = funs_meaningful[0] if funs_meaningful else (funs[0] if funs else "(no FUN call)")
    sample_str = strs[0] if strs else ""
    for v in vals:
        marker = "*" if v == vals[0] else " "
        master.append((v, e-s, first_fun, sample_str))
        tag_s = f"0x{v:02x}" if v >= 0 else "deflt"
        print(f"  {tag_s:>4}  {e-s:>5} {marker} {first_fun:<22} {sample_str}")
    print()

# Group cases by their first-FUN to find the unique subsystems
print("\n=== unique subsystems (by first FUN call) ===")
by_fun = collections.defaultdict(list)
for v, _, ff, _ in master:
    by_fun[ff].append(v)
for ff, vs in sorted(by_fun.items(), key=lambda x: -len(x[1])):
    tagstr = ", ".join(f"0x{v:02x}" if v>=0 else "deflt" for v in sorted(vs))
    print(f"  {ff:<25}  ({len(vs)} cases)  tags: {tagstr}")
