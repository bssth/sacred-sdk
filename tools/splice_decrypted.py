"""Build Sacred_decrypted.exe by splicing the runtime-decrypted .text bytes
(captured by SacredSDK's dump_text worker into sdk\\logs\\text_dump.bin) back
into a copy of Sacred.exe.

After the splice the resulting EXE has clear x86 code in .text and can be
imported into Ghidra normally — all string xrefs and decompilation will
work.

Usage:
    python splice_decrypted.py
"""
import os, sys, math, collections
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

GAME = r"E:\SteamLibrary\steamapps\common\Sacred Gold"
ORIG = os.path.join(GAME, "Sacred.exe")
DUMP = os.path.join(GAME, "sdk", "logs", "text_dump.bin")
OUT  = os.path.join(GAME, "sdk", "Sacred_decrypted.exe")

# From recon and Ghidra agree:
#   .text  raw offset = 0x1000
#          raw size   = 0x48F000
TEXT_RAW_OFF  = 0x1000
TEXT_RAW_SIZE = 0x48F000

def entropy(b):
    if not b: return 0.0
    h = collections.Counter(b)
    n = len(b)
    return -sum((c/n) * math.log2(c/n) for c in h.values() if c)

if not os.path.exists(ORIG):
    sys.exit(f"missing {ORIG}")
if not os.path.exists(DUMP):
    sys.exit(f"missing {DUMP} — run Sacred via the proxy first to produce it")

orig = bytearray(open(ORIG, "rb").read())
dump = open(DUMP, "rb").read()

print(f"original: {len(orig):>10} bytes")
print(f"dump    : {len(dump):>10} bytes")

# Sanity 1: original .text bytes must be encrypted (entropy near 8.0)
orig_text_e = entropy(orig[TEXT_RAW_OFF : TEXT_RAW_OFF + TEXT_RAW_SIZE])
print(f"entropy(original.text) = {orig_text_e:.3f}  (>= 7.5 = encrypted, expected)")
if orig_text_e < 7.0:
    print("  ! warning: original .text doesn't look encrypted; are you sure this is the Steam build?")

# Sanity 2: dumped bytes should look like real x86 code (entropy ~5.5–6.5)
dump_e = entropy(dump[:TEXT_RAW_SIZE])
print(f"entropy(dump)          = {dump_e:.3f}  (5.5–6.5 = code, expected)")
if dump_e > 7.0:
    print("  ! warning: dump still looks encrypted; the worker thread may have")
    print("    fired BEFORE the .bind stub finished. Try increasing the poll budget")
    print("    in dump_text.cpp or wait longer before triggering the dump.")
    sys.exit(2)
if dump_e < 4.0:
    print("  ! warning: dump entropy unusually low; check the dump size.")

if len(dump) < TEXT_RAW_SIZE:
    sys.exit(f"dump too short: {len(dump)} < {TEXT_RAW_SIZE}")

# Splice
orig[TEXT_RAW_OFF : TEXT_RAW_OFF + TEXT_RAW_SIZE] = dump[:TEXT_RAW_SIZE]

# Optional sanity: look for canonical x86 function prologues
prologue_count = orig.count(b"\x55\x8b\xec", TEXT_RAW_OFF, TEXT_RAW_OFF + TEXT_RAW_SIZE)
print(f"\nfound {prologue_count} `push ebp; mov ebp, esp` prologues in spliced .text")
if prologue_count < 100:
    print("  ! warning: very few prologues — splice may be misaligned or dump incomplete.")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "wb").write(orig)
print(f"\nwrote {OUT}")
print(f"  size = {os.path.getsize(OUT)} bytes (should match original {len(orig)})")
print("\nNext: re-import into Ghidra:")
print("  sdk\\tools\\ghidra\\run_headless.bat import-decrypted")
print("(make sure run_headless.bat accepts the second target; if not, edit it.)")
