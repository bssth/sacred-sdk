"""Aggregate all callers of FUN_00472bc0 into a single per-caller opcode-dispatch
table. For each caller's decompile file, parse the `if (iVar2 == N)` chains and
record what opcodes that subsystem handles plus what helpers it calls."""
import os, re, sys, glob, collections
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

DEC_DIR = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\tools\ghidra\decompiled"
CALLERS = [
    "00491170", "004915a0", "004ac940", "00478780", "00496f20",
    "00478d80", "004790c0", "004793d0", "0047a0c0", "0047b480",
]

# Pattern: `... = FUN_00472bc0(...)` then a chain of `if (X == OPC)` blocks.
opcode_re = re.compile(r"^\s*(?:else\s+)?if\s*\(\s*(\w+)\s*==\s*(0x[0-9a-fA-F]+|\d+)\s*\)\s*\{", re.M)
fun_call_re = re.compile(r"\b(FUN_[0-9a-f]{8}|c[A-Z][A-Za-z0-9_]+_[a-z][A-Za-z0-9_]*_[0-9a-f]{8})\(")
str_use_re  = re.compile(r"(s_[A-Za-z_][A-Za-z0-9_]*_[0-9a-f]{8})")

def parse_caller(va_hex):
    path = os.path.join(DEC_DIR, f"{va_hex}_FUN_{va_hex}.c")
    if not os.path.exists(path):
        return None
    text = open(path, encoding="utf-8", errors="replace").read()
    # locate the variable assigned from FUN_00472bc0
    m = re.search(r"(\w+)\s*=\s*FUN_00472bc0\s*\(", text)
    if not m:
        return None
    rv = m.group(1)
    # Find all `if (rv == OPC)` matches
    opcs = []
    for opc_m in opcode_re.finditer(text):
        var, val = opc_m.group(1), opc_m.group(2)
        if var != rv: continue
        try:
            v = int(val, 16) if val.startswith("0x") else int(val)
        except ValueError: continue
        opcs.append((v, opc_m.start()))
    # For each opcode handler, find the slice of text up to next `else if (rv ==` / `}\n`
    handlers = []
    for i, (op, start) in enumerate(opcs):
        end = opcs[i+1][1] if i+1 < len(opcs) else min(start + 1500, len(text))
        slice_ = text[start:end]
        funs = list(dict.fromkeys(fun_call_re.findall(slice_)))
        strs = list(dict.fromkeys(str_use_re.findall(slice_)))
        handlers.append((op, funs, strs))
    # Also extract any string literal references near the function for context
    return handlers

# Compile master table: caller × opcode -> handler hints
print("=== per-caller opcode dispatch table ===\n")
all_caller_ops = {}
for c in CALLERS:
    h = parse_caller(c)
    all_caller_ops[c] = h
    if h is None:
        print(f"--- {c}: (no decompile yet) ---")
        continue
    print(f"--- {c} ({len(h)} opcode-handlers) ---")
    for op, funs, strs in h:
        flist = ", ".join(funs[:5])
        slist = ", ".join(strs[:3])
        print(f"  op {op:#04x}: calls=[{flist}]")
        if slist:
            print(f"             strs=[{slist}]")
    print()

# Cross-tab: which opcodes are handled by which callers
print("\n=== opcode → which callers handle it ===\n")
op2callers = collections.defaultdict(list)
for c, h in all_caller_ops.items():
    if h is None: continue
    for op, _, _ in h:
        op2callers[op].append(c)
for op in sorted(op2callers):
    print(f"  op {op:#04x}: {', '.join(op2callers[op])}")
