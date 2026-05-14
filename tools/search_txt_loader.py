"""Hunt for evidence that Sacred can load uncompiled .txt FunkCode."""
import re, struct, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
data = open(EXE, "rb").read()

# Patterns of interest
PATTERNS = [
    rb"FunkCode\.txt",
    rb"FunkCode_src",
    rb"\.txt",
    rb"ScriptCompile",
    rb"Compile",
    rb"Parse",
    rb"DEBUG_SCRIPT",
    rb"FunkCode\.[a-z]+",
    rb"StartCode\.[a-z]+",
    rb"QuestCode\.[a-z]+",
    rb"\\\\Bin\\\\",
    rb"src\\\\",
    rb"script.*\.txt",
    rb"\.fkn",
    rb"\.src",
    rb"compile",
]

print(f"Searching {len(data):,} bytes of decrypted EXE\n")

for pat_bytes in PATTERNS:
    rx = re.compile(pat_bytes, re.IGNORECASE)
    hits = []
    for m in rx.finditer(data):
        off = m.start()
        # Read surrounding context (try ASCII + null-terminated)
        start = max(0, off - 30)
        end_nul = data.find(b'\x00', off, off + 100)
        if end_nul < 0: end_nul = min(len(data), off + 80)
        # Backtrack to last null/non-printable before off
        back_start = off
        while back_start > start and 0x20 <= data[back_start-1] <= 0x7E:
            back_start -= 1
        ctx_start = back_start
        ctx = data[ctx_start:end_nul].decode("latin1", "replace")
        hits.append((off, ctx))
    if hits:
        seen = set()
        unique_hits = []
        for off, ctx in hits:
            if ctx not in seen and len(ctx) >= len(pat_bytes.decode("latin1")):
                seen.add(ctx)
                unique_hits.append((off, ctx))
        if unique_hits:
            print(f"=== '{pat_bytes.decode('latin1', 'replace')}'  ({len(unique_hits)} unique) ===")
            for off, ctx in unique_hits[:20]:
                print(f"  @ 0x{off:08x}  {ctx[:120]!r}")
            if len(unique_hits) > 20:
                print(f"  ... and {len(unique_hits)-20} more")
            print()
