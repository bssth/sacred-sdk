"""Extract every opcode's payload width and outbound function calls from
FUN_00472bc0. Output is a concrete bytecode-format spec we can implement.

For each opcode case body we look for:
  - `*param_1 = <expr> + *param_1`   → static increment to IP (payload width)
  - `pcVar19 = (char *)(uVar16 + 1 + param_2); ... do { ... } while (cVar5 != '\0');`
                                     → reads a C-string from payload
  - reads of `*(byte/short/dword *)(param_2 + offset)` → fixed-width fields
  - call sites `FUN_XXXXXXXX` and `cClass_method_XXXXXXXX`
"""
import re, sys, os, collections
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled\00472bc0_FUN_00472bc0.c"
src = open(DEC, encoding="utf-8", errors="replace").read()
lines = src.splitlines()

# Reuse the same grouping logic.
case_re = re.compile(r"^\s*case\s+([0-9xXa-fA-F]+):\s*$")
default_re = re.compile(r"^\s*default:\s*$")
case_starts = []
for i, ln in enumerate(lines):
    m = case_re.match(ln)
    if m:
        v = m.group(1)
        case_starts.append((i, int(v, 16) if v.lower().startswith("0x") else int(v)))
    elif default_re.match(ln):
        case_starts.append((i, -1))

groups = []
i = 0
while i < len(case_starts):
    base_line, opc = case_starts[i]
    opcs = [opc]
    j = i + 1
    while j < len(case_starts):
        nxt_line, nxt_opc = case_starts[j]
        between = "\n".join(lines[base_line+1: nxt_line]).strip()
        if between == "":
            opcs.append(nxt_opc); base_line = nxt_line; j += 1
        else:
            break
    body_end = case_starts[j][0] if j < len(case_starts) else len(lines)
    groups.append((opcs, base_line + 1, body_end))
    i = j

# --- analysis regexes ---
# `*param_1 = ... + N + *param_1`  OR  `*param_1 = *param_1 + N`
ip_add_re = [
    re.compile(r"\*param_1\s*=\s*[^+\n]+?\+\s*(\d+)\s*\+\s*\*param_1"),
    re.compile(r"\*param_1\s*=\s*\*param_1\s*\+\s*(\d+)"),
    re.compile(r"\*param_1\s*\+=\s*(\d+)"),
]
# A `~uVar... + 1 + *param_1` form — increments by `length(cstr) + 1`.
ip_cstr_re = re.compile(r"\*param_1\s*=\s*~[A-Za-z_]+\s*\+\s*1\s*\+\s*\*param_1")
# Function calls
call_re = re.compile(r"\b(FUN_[0-9a-f]{8}|c[A-Z][A-Za-z0-9_]+_[a-z][A-Za-z0-9_]*_[0-9a-f]{8})\s*\(")

# Recognise commonly-named API helpers
KNOWN_ROLE = {
    "FUN_0066ef40": "debug_log",                # printf-style trace
    "FUN_0045ee20": "string_format",
    "FUN_0084a760": "string_search",
    "FUN_00460720": "string_action_A",
    "FUN_0085aa60": "strncmp",
    "FUN_005fe000": "objmgr_getData",
    "FUN_0084a961": "dynamic_cast",
    "FUN_006725e0": "res_singleton_get",
    "FUN_00672720": "res_lookup",
    "FUN_00672740": "res_release",
    "FUN_0044a1c0": "hero_id_from_creature",
    "FUN_0084b2e5": "is_digit",
    "FUN_00603e30": "objmgr_getData2",
    "FUN_00673170": "alloc_64",
    "FUN_00673ad0": "set_value_A",
    "FUN_00673f30": "set_value_B",
    "FUN_006735a0": "set_value_C",
    "FUN_006739e0": "set_value_D",
}

# Number of leading bytes the case advances IP by, BEFORE running its body
# logic (e.g. `... + 1 + *param_1` after reading a cstring is +1 because the
# opcode itself was 1 byte plus the variable-length cstr).
def classify_width(body):
    has_cstr_inc = bool(ip_cstr_re.search(body))
    static_inc   = None
    for rx in ip_add_re:
        m = rx.search(body)
        if m:
            static_inc = int(m.group(1))
            break
    return static_inc, has_cstr_inc

def calls_in(body):
    out = []
    for m in call_re.finditer(body):
        c = m.group(1)
        out.append(c)
    return list(dict.fromkeys(out))   # preserve order, dedupe

# --- emit ---
print(f"{'op':>5}  {'group':>5}  {'lines':>5}  {'static_ip+':>10}  {'cstr':>4}  outbound calls")
print("-" * 110)
gid = 0
for opcs, s, e in groups:
    gid += 1
    body = "\n".join(lines[s:e])
    static_inc, has_cstr = classify_width(body)
    calls = calls_in(body)
    # remap to known role names
    pretty_calls = []
    for c in calls[:8]:
        role = KNOWN_ROLE.get(c)
        pretty_calls.append(f"{c}({role})" if role else c)
    for opc in opcs:
        op = f"{opc:#04x}" if opc >= 0 else "deflt"
        print(f"{op:>5}  {gid:>5}  {e-s:>5}  {'-' if static_inc is None else static_inc:>10}  "
              f"{'cstr' if has_cstr else '-':>4}  "
              f"{', '.join(pretty_calls)}")

# --- summary by feature ---
print(f"\n=== summary ===")
n_cstr = sum(1 for opcs, s, e in groups
             if ip_cstr_re.search('\n'.join(lines[s:e])))
n_static = sum(1 for opcs, s, e in groups
               if any(rx.search('\n'.join(lines[s:e])) for rx in ip_add_re))
print(f"groups reading 1 cstring : {n_cstr}")
print(f"groups with static IP+   : {n_static}")
print(f"groups with neither      : {len(groups) - n_cstr - n_static}")
