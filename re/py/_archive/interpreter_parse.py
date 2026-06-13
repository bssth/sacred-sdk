"""Parse the Ghidra-decompiled FUN_00472bc0 to extract every case's body and
classify it by the API calls / RTTI types / string literals it touches.

Output: a per-opcode behavior map suitable for refining the type
dictionary in funkcode_ops.py.
"""
import re, sys, os, collections
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled\00472bc0_FUN_00472bc0.c"
src = open(DEC, encoding="utf-8", errors="replace").read()
lines = src.splitlines()

# Find every `case <N>:` label and remember its line number.
case_re   = re.compile(r"^\s*case\s+([0-9xXa-fA-F]+):\s*$")
default_re = re.compile(r"^\s*default:\s*$")

case_starts = []   # (line_index, opcode_int)
for i, ln in enumerate(lines):
    m = case_re.match(ln)
    if m:
        v = m.group(1)
        opc = int(v, 16) if v.lower().startswith("0x") else int(v)
        case_starts.append((i, opc))
    elif default_re.match(ln):
        case_starts.append((i, -1))   # use -1 as "default"

# Determine each case's body range. Multiple cases can stack (fall-through).
# A "group" is consecutive case-labels with NO statements between, sharing one
# body. The body extends until we hit `break;` (most common), `goto LAB_...`,
# `return ...`, or the start of the NEXT *non-empty* case label.
print(f"found {len(case_starts)} case labels")

# Group consecutive case labels.
groups = []  # list of (list_of_opcodes, body_start_line, body_end_line)
i = 0
while i < len(case_starts):
    base_line, opc = case_starts[i]
    group_opcs = [opc]
    j = i + 1
    while j < len(case_starts):
        next_line, next_opc = case_starts[j]
        # Check that between current label and next label, there's no body
        between = "\n".join(lines[base_line+1: next_line]).strip()
        if between == "":
            group_opcs.append(next_opc)
            base_line = next_line
            j += 1
        else:
            break
    body_start = base_line + 1
    # body end = line of next case OR closing `}` of switch
    body_end = len(lines)
    if j < len(case_starts):
        body_end = case_starts[j][0]
    groups.append((group_opcs, body_start, body_end))
    i = j

print(f"deduped into {len(groups)} groups (consecutive-fallthrough merged)\n")

# --- classification heuristics ---
SIGNALS = [
    ("string",      r"local_110|in_ECX \+ 0xa460|memcpy|FUN_0085aa60"),
    ("DLG",         r"DLGNPC|cScriptResourceDlg|cScriptResource|FUN_006725e0"),
    ("RES",         r"CPOS_RES_|res:|FUN_00672"),
    ("hero/CPOS",   r"CPOS_hero|hero|HERO_"),
    ("creature",    r"cCreature::|RTTI_Type_Descriptor|cObjectManager_getData"),
    ("quest",       r"QUEST|cQuest"),
    ("tptarget",    r"tptarget"),
    ("dialog/log",  r"DLG_|Dialog|FUN_0066ef40|sdk_log"),
    ("counter",     r"counter|count|cnt"),
    ("kill/dead",   r"kill|dead|Tot|tot"),
    ("region",      r"region|Region|RG_"),
    ("npc-type",    r"NPC|RTYPE"),
    ("item",        r"item|Item|ITM_"),
    ("math/expr",   r"\+ 1 \+|\+ 2 \+|<< |>> |% "),
    ("ip-advance",  r"\*param_1 = "),
    ("error/panic", r"Pointer NULL|fault|failed|FUN_0066ef40"),
    ("audio",       r"audio|Audio|sound|Sound"),
    ("FunkCode",    r"FunkCode|cFunkCode"),
    ("regional",    r"BLDH|UW|OW|DW"),
]

# Helper: count IP increments (`*param_1 = ... + N + *param_1` or `+= N`).
ip_re = re.compile(r"\*param_1\s*=.*\+\s*(\d+)\s*\+\s*\*param_1|\*param_1\s*\+=\s*(\d+)")

print(f"{'op (hex)':>8}  {'op (dec)':>8}  {'body lines':>10}  ip+={'?':>3}  signals  --  first line")
print("-" * 130)
classified = []
for opcs, s, e in groups:
    body = "\n".join(lines[s:e])
    body_short = body.strip().split("\n")[0][:80] if body.strip() else "(empty)"
    sigs = []
    for tag, rx in SIGNALS:
        if re.search(rx, body):
            sigs.append(tag)
    # detect IP advance value
    ip_inc = None
    for m in ip_re.finditer(body):
        v = m.group(1) or m.group(2)
        if v:
            ip_inc = v
            break
    # only show first-line summary for the group
    op_list = ", ".join(f"{o:#x}" if o>=0 else "default" for o in opcs)
    sigs_s = ",".join(sigs) if sigs else "-"
    for opc in opcs:
        marker = "*" if opc == opcs[0] else " "
        classified.append((opc, len(lines[s:e]), ip_inc, sigs_s, body_short, marker))

# Sort by opcode, print
print(f"{'op':>4} (hex)  {'op':>4} (dec)  {'lines':>5}  {'ip+':>4}  signals")
print("-" * 100)
for opc, lines_n, ip_inc, sigs, snippet, marker in sorted(
        classified, key=lambda x: (-1 if x[0] < 0 else x[0])):
    op_hex = f"{opc:#04x}" if opc >= 0 else "deflt"
    op_dec = str(opc) if opc >= 0 else "-"
    ip_s   = ip_inc if ip_inc else "-"
    print(f"{op_hex:>9}  {op_dec:>9}  {lines_n:>5}  {ip_s:>4} {marker} {sigs}")

# Also: which signal-clusters cluster opcodes together? Useful to identify
# "this group of opcodes is all dialog ops".
print("\n=== opcode clusters by signal set ===")
by_sigs = collections.defaultdict(list)
for opc, _, _, sigs, _, _ in classified:
    by_sigs[sigs].append(opc)
for sigs, opcs in sorted(by_sigs.items(), key=lambda x: -len(x[1])):
    if len(opcs) >= 2:
        opc_str = ", ".join(f"{o:#x}" if o>=0 else "default" for o in sorted(opcs))
        print(f"  [{sigs}]  ({len(opcs)} opcodes) : {opc_str}")
