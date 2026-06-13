#!/usr/bin/env python3
"""Pull specific named string literals from the exe by VA, and scan a VA
window for ASCII strings (to recover FunkCode command keywords)."""
import sys, re
EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
BASE = 0x400000
with open(EXE, "rb") as f:
    d = f.read()

def cstr(va):
    o = va - BASE
    if o < 0 or o >= len(d):
        return None
    e = d.find(b"\x00", o)
    return d[o:e].decode("latin1", "replace")

# Known-referenced literals from the walker / handlers.
named = {
    0x0094eac4: "IF mit offenem Ende",
    0x0094ea88: "ELSE mit offenem Ende",
    0x0094eaa4: "ELSEIF mit offenem Ende",
    0x0094f504: "HeroQBit literal",
    0x0094f510: "HeroQBit prefix",
    0x00964120: "parseStatement ctx",
    0x00964078: "parseStatement err",
    0x009641c4: "loadScriptedSeq ctx",
    0x00964214: "resources kw",
    0x009641b8: "resource kw",
    0x009640cc: "pragma kw",
    0x0094e228: "NON_UNIQUE log",
    0x0094eca8: "CreateNPC failed fmt",
}
for va, tag in named.items():
    print(f"0x{va:08x} [{tag}] = {cstr(va)!r}")

print("\n--- ASCII scan of string blob 0x0094e000..0x00950000 ---")
o0, o1 = 0x0094e000 - BASE, 0x00950000 - BASE
blob = d[o0:o1]
for m in re.finditer(rb"[\x20-\x7e]{4,}", blob):
    s = m.group().decode("latin1")
    print(f"0x{BASE+o0+m.start():08x}  {s}")
