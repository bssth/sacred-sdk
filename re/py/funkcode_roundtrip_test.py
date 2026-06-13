"""Round-trip test for the FunkCode (de)compiler pair.

For every FunkCode.bin / QuestCode.bin / StartCode.bin / QuestPoolCode.bin under
the game's `bin/` tree, we run

    text = decompile(open(F, "rb").read())
    rebuilt = compile(text)

and assert `rebuilt == original`. Reports first mismatch byte for each failure
so we can iterate on the (de)compiler quickly.
"""
import glob
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import funkcode_decompile as dec
import funkcode_compile as cmp


def hash_bytes(b):
    return hashlib.sha256(b).hexdigest()[:16]


def diff_summary(a, b):
    """First few bytes of difference between `a` and `b`."""
    if a == b:
        return "identical"
    if len(a) != len(b):
        msg = f"len {len(a):,} != {len(b):,}"
    else:
        msg = f"same len {len(a):,}"
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            ctx_a = a[max(0, i-4): i+8].hex()
            ctx_b = b[max(0, i-4): i+8].hex()
            return f"{msg}; first diff @ 0x{i:08x}: orig={ctx_a} rebuilt={ctx_b}"
    return msg + " (one is a prefix of the other)"


def _safe_rel(path, base):
    """`os.path.relpath` raises on cross-drive paths on Windows. Best-effort."""
    try:
        return os.path.relpath(path, base)
    except ValueError:
        return path


def main():
    base = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    if len(sys.argv) < 2:
        # Default: full sweep under bin/.
        targets = sorted(glob.glob(os.path.join(base, "bin", "**", "*.bin"), recursive=True))
    else:
        targets = sys.argv[1:]
    if not targets:
        print("nothing to test")
        return 1

    ok = 0
    fail = 0
    for path in targets:
        try:
            with open(path, "rb") as f:
                orig = f.read()
            text = dec.decompile(orig)
            rebuilt = cmp.compile(text)
            rel = _safe_rel(path, base)
            if rebuilt == orig:
                print(f"  OK   {rel}  {len(orig):>8,}B  sha={hash_bytes(orig)}")
                ok += 1
            else:
                print(f"  FAIL {rel}  {diff_summary(orig, rebuilt)}")
                fail += 1
        except Exception as e:
            rel = _safe_rel(path, base)
            print(f"  EXC  {rel}: {type(e).__name__}: {e}")
            fail += 1

    print(f"\n=== {ok} OK, {fail} FAIL out of {ok + fail} files ===")
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
