"""re_rewards_npc_world_disasm.py -- linear capstone listing of a VA range in
sdk/Sacred_decrypted.exe with string / call-target annotations (wave-2 R4).

    python re_rewards_npc_world_disasm.py 0x4ac940 [0x4ad9f0]   # start [end]
    python re_rewards_npc_world_disasm.py --fn 0x57fd80           # whole function (functions.csv bounds)
    python re_rewards_npc_world_disasm.py --grep "0x3ec|0x1f4" --fn 0x4ac940

Uses sdk/re/py/x86/scan.py (PE loader, function bounds, string_at). Read-only.
"""
import os, sys, re, argparse, bisect

HERE = os.path.dirname(os.path.abspath(__file__))
X86 = os.path.join(HERE, "x86")
if X86 not in sys.path:
    sys.path.insert(0, X86)

import scan                                   # noqa: E402
from scan import TEXT_VA, TEXT, MD, FUNCS     # noqa: E402


def fn_bounds(va):
    i = bisect.bisect_right(FUNCS, va) - 1
    start = FUNCS[i] if i >= 0 else va
    end = FUNCS[i + 1] if i + 1 < len(FUNCS) else TEXT_VA + len(TEXT)
    return start, end


def listing(start, end, grep=None):
    off = start - TEXT_VA
    rx = re.compile(grep) if grep else None
    out = []
    for ins in MD.disasm(TEXT[off:end - TEXT_VA], start):
        note = ""
        for tok in re.findall(r"0x[0-9a-f]{6,8}", ins.op_str):
            v = int(tok, 16)
            s = scan.string_at(v)
            if s and len(s) >= 3:
                note += "  ; %r" % s[:60]
            elif ins.mnemonic == "call":
                try:
                    note += "  ; %s" % scan.gh_name(v)
                except Exception:
                    pass
        line = "%08x  %-7s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note)
        if rx is None or rx.search(line):
            out.append(line)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("start", nargs="?")
    ap.add_argument("end", nargs="?")
    ap.add_argument("--fn", default=None)
    ap.add_argument("--grep", default=None)
    a = ap.parse_args(argv)
    if a.fn:
        s, e = fn_bounds(int(a.fn, 0))
    else:
        s = int(a.start, 0)
        e = int(a.end, 0) if a.end else s + 0x100
    print("; range %08x..%08x" % (s, e))
    for line in listing(s, e, a.grep):
        print(line)


if __name__ == "__main__":
    main()
